/* ===== kernel/drivers/keyboard.c (RISC-V): UART 16550 -> 合成 set-1 扫描码 =====
 * virt 机器无 PS/2: 键盘输入来自 UART (0x10000000) 的 ASCII。
 * 策略: 把 ASCII 字符翻译为 set-1 make 码推入 sc_buffer (与 x86 语义一致),
 * 大写/符号键合成 LShift make+break 对, 使 key 层 translate_scancode 正常工作。
 * 特殊序列: ESC[A=Up(0x48) ESC[B=Down(0x50) ESC[D=Left(0x4B) ESC[C=Right(0x4D),
 *           ESC=Esc(0x01), CR=Enter(0x1C), BS=Backspace(0x0E)。
 * 无中断: 全部轮询。桌面/VM/LX 的输入 = 本驱动 + 主机 shell (C:\>) 共用 UART。
 */
#include <driver.h>
#include <keyboard.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define UART_BASE 0x10000000UL
#define UART_RBR 0x00
#define UART_LSR 0x05

#define KEY_BUF_SIZE 256
#define SC_BUF_SIZE 64

extern uint32_t sched_get_ticks(void);

static char key_buffer[KEY_BUF_SIZE];
static int key_head = 0;
static int key_tail = 0;

static uint8_t sc_buffer[SC_BUF_SIZE];
static int sc_head = 0;
static int sc_tail = 0;

static int shift_down;
static int caps_on;

/* set-1 make 码表 (x86 布局, 与键盘下层一致) */
static const unsigned char scancode_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const unsigned char scancode_shift_ascii[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

/* ascii -> {scancode, shifted} 查找表 */
static uint8_t ascii_plain[128];
static uint8_t ascii_shift[128];

static void build_ascii_map(void) {
    for (int i = 0; i < 128; i++) {
        ascii_plain[i] = 0xFF;
        ascii_shift[i] = 0xFF;
    }
    for (unsigned s = 0; s < sizeof(scancode_ascii); s++) {
        unsigned char c = scancode_ascii[s];
        if (c && c < 128 && ascii_plain[c] == 0xFF)
            ascii_plain[c] = (uint8_t)s;
        unsigned char S = scancode_shift_ascii[s];
        if (S && S < 128 && ascii_shift[S] == 0xFF)
            ascii_shift[S] = (uint8_t)s;
    }
    /* 大写字母 = shifted 小写字母 (scancode_shift_ascii 已含) */
}

static void sc_push(uint8_t sc) {
    int next = (sc_tail + 1) % SC_BUF_SIZE;
    if (next != sc_head) {
        sc_buffer[sc_tail] = sc;
        sc_tail = next;
    }
}

static void sc_push_pair(uint8_t make) {
    sc_push(make);
    sc_push((uint8_t)(make | 0x80));
}

static void sc_push_key(int sc, int need_shift) {
    if (need_shift) {
        sc_push(0x2A);
        sc_push((uint8_t)sc);
        sc_push(0xAA);
        sc_push((uint8_t)(sc | 0x80));
    } else {
        sc_push_pair((uint8_t)sc);
    }
}

/* 处理一个 ASCII 字符 -> 扫描码序列 */
static void feed_ascii(unsigned char c) {
    if (c == 0x1B) {
        sc_push_pair(0x01);            /* Esc */
        return;
    }
    if (c == '\r' || c == '\n') {
        sc_push_pair(0x1C);            /* Enter */
        return;
    }
    if (c == 0x7F || c == 0x08) {
        sc_push_pair(0x0E);            /* Backspace */
        return;
    }
    if (c == '\t') {
        sc_push_pair(0x0F);            /* Tab */
        return;
    }
    if (c >= 128 || c == 0) return;
    int sc = ascii_plain[c];
    if (sc != 0xFF) { sc_push_pair((uint8_t)sc); return; }
    sc = ascii_shift[c];
    if (sc != 0xFF) { sc_push_key(sc, 1); return; }
}

/* UART 轮询: 读入字节, 处理 ESC 序列, 翻译入 sc_buffer */
static void uart_drain(void) {
    while (*(volatile uint8_t *)(UART_BASE + UART_LSR) & 0x01) {
        unsigned char c = (unsigned char)(*(volatile uint8_t *)(UART_BASE + UART_RBR));
        if (c == 0x1B) {
            /* 等最多 8 字节的 ESC 序列 */
            uint8_t seq[8];
            int si = 0;
            seq[si++] = c;
            while (si < 8 && (*(volatile uint8_t *)(UART_BASE + UART_LSR) & 0x01)) {
                seq[si] = (unsigned char)(*(volatile uint8_t *)(UART_BASE + UART_RBR));
                if (si >= 1 && seq[si] >= '@' && seq[si] <= '~') { si++; break; }
                si++;
            }
            if (si >= 3 && seq[1] == '[') {
                switch (seq[2]) {
                case 'A': sc_push_pair(0x48); break;  /* Up */
                case 'B': sc_push_pair(0x50); break;  /* Down */
                case 'D': sc_push_pair(0x4B); break;  /* Left */
                case 'C': sc_push_pair(0x4D); break;  /* Right */
                default:  sc_push_pair(0x01); break;  /* 其他 -> Esc */
                }
            } else {
                sc_push_pair(0x01);
            }
            continue;
        }
        feed_ascii(c);
    }
}

int keyboard_poll_scancode(void) {
    uart_drain();
    if (sc_head == sc_tail) return -1;
    unsigned char sc = sc_buffer[sc_head];
    sc_head = (sc_head + 1) % SC_BUF_SIZE;
    return sc;
}

int keyboard_scancode_pending(void) {
    uart_drain();
    return sc_head != sc_tail;
}

int keyboard_getchar(void) {
    uart_drain();
    if (key_head != key_tail) {
        char c = key_buffer[key_head];
        key_head = (key_head + 1) % KEY_BUF_SIZE;
        return c;
    }
    while (sc_head != sc_tail) {
        unsigned char sc = sc_buffer[sc_head];
        sc_head = (sc_head + 1) % SC_BUF_SIZE;
        if (sc == 0x2A || sc == 0x36) { shift_down = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_down = 0; continue; }
        if (sc == 0x3A) { caps_on = !caps_on; continue; }
        if (sc & 0x80) continue;
        unsigned char c;
        if (shift_down ^ caps_on)
            c = scancode_shift_ascii[sc];
        else
            c = scancode_ascii[sc];
        if (c) return c;
    }
    return -1;
}

int keyboard_pending(void) {
    uart_drain();
    if (key_head != key_tail) return 1;
    return sc_head != sc_tail;
}

void keyboard_flush(void) {
    uint32_t t0 = sched_get_ticks();
    uint32_t last_activity = t0;
    for (;;) {
        int had = 0;
        while (*(volatile uint8_t *)(UART_BASE + UART_LSR) & 0x01) {
            (void)*(volatile uint8_t *)(UART_BASE + UART_RBR);
            had = 1;
        }
        while (sc_head != sc_tail) { sc_head = (sc_head + 1) % SC_BUF_SIZE; had = 1; }
        key_head = 0;
        key_tail = 0;
        shift_down = 0;
        caps_on = 0;
        uint32_t now = sched_get_ticks();
        if ((uint32_t)(now - t0) >= 50)
            break;
        if (had || now != last_activity) {
            last_activity = now;
        } else if (last_activity == t0 && now == t0) {
            for (volatile uint32_t i = 0; i < 60000; i++)
                ;
            if (sched_get_ticks() == t0 &&
                !(*(volatile uint8_t *)(UART_BASE + UART_LSR) & 0x01) &&
                sc_head == sc_tail)
                break;
            continue;
        }
        if ((uint32_t)(now - last_activity) >= 5)
            break;
        while (sched_get_ticks() == now) {}
    }
}

void keyboard_init(void) {
    build_ascii_map();
    shift_down = 0;
    caps_on = 0;
    key_head = 0;
    key_tail = 0;
    sc_head = 0;
    sc_tail = 0;
    while (*(volatile uint8_t *)(UART_BASE + UART_LSR) & 0x01)
        (void)*(volatile uint8_t *)(UART_BASE + UART_RBR);
}

static int keyboard_drv_init(void) {
    keyboard_init();
    return 0;
}

static Driver keyboard_driver = {
    .name = "keyboard",
    .type = DRV_TYPE_KEYBOARD,
    .init = keyboard_drv_init,
};

int keyboard_driver_register(void) {
    return driver_register(&keyboard_driver);
}
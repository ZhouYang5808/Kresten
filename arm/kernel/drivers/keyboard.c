/* ===== kernel/drivers/keyboard.c: PS/2 keyboard via PL050 KMI0 (versatilepb) =====
 * QEMU 的 ps2 键盘模拟对 PL050 KMI 注入 set-2 码 (与 8042 前级一致):
 *   make 码见 set2_to_set1 表, break = 0xF0 前缀, 扩展 = 0xE0 前缀。
 * 提供:
 *   - sc_buffer: set-1 make 码 (keyboard_poll_scancode/scancode_pending)
 *   - key 层: ASCII (keyboard_getchar/pending)
 * 注意: 主机 shell (C:\>) 的输入走 UART, 不经此驱动; 桌面/VM/LX 用此驱动。
 * 无中断：全部轮询 KMI0。
 */
#include <driver.h>
#include <keyboard.h>
#include <stdio.h>
#include <stdint.h>

#define KBD_BASE    0x10006000
#define KMI_CR      0x00
#define KMI_STAT    0x04
#define KMI_DATA    0x08
#define KMI_CLKDIV  0x0C

#define KMI_STAT_RXFULL 0x10

#define KEY_BUF_SIZE 256
#define SC_BUF_SIZE 64

extern uint32_t sched_get_ticks(void);   /* drivers/timer.c, SP804 100Hz */

static char key_buffer[KEY_BUF_SIZE];
static int key_head = 0;
static int key_tail = 0;

static uint8_t sc_buffer[SC_BUF_SIZE];
static int sc_head = 0;
static int sc_tail = 0;

static int ext_pending;
static int brk_pending;
static int shift_down;
static int caps_on;

static void sc_push(uint8_t sc) {
    int next = (sc_tail + 1) % SC_BUF_SIZE;
    if (next != sc_head) {
        sc_buffer[sc_tail] = sc;
        sc_tail = next;
    }
}

/* set2 -> set1 make code（含 0xE0 扩展前缀键）。返回 -1 = 忽略 */
static int set2_to_set1(uint8_t b, int ext) {
    if (ext) {
        switch (b) {
        case 0x75: return 0x48; /* Up    */
        case 0x72: return 0x50; /* Down  */
        case 0x6B: return 0x4B; /* Left  */
        case 0x74: return 0x4D; /* Right */
        case 0x6C: return 0x47; /* Home  */
        case 0x69: return 0x4F; /* End   */
        case 0x7D: return 0x49; /* PgUp  */
        case 0x7A: return 0x51; /* PgDn  */
        case 0x70: return 0x52; /* Ins   */
        case 0x71: return 0x53; /* Del   */
        case 0x14: return 0x1D; /* RCtrl */
        case 0x11: return 0x38; /* RAlt  */
        case 0x5A: return 0x1C; /* KP-Enter */
        default:   return -1;
        }
    }
    switch (b) {
    case 0x76: return 0x01; /* Esc   */
    case 0x0E: return 0x29; /* `     */
    case 0x16: return 0x02; /* 1     */
    case 0x1E: return 0x03; /* 2     */
    case 0x26: return 0x04; /* 3     */
    case 0x25: return 0x05; /* 4     */
    case 0x2E: return 0x06; /* 5     */
    case 0x36: return 0x07; /* 6     */
    case 0x3D: return 0x08; /* 7     */
    case 0x3E: return 0x09; /* 8     */
    case 0x46: return 0x0A; /* 9     */
    case 0x45: return 0x0B; /* 0     */
    case 0x4E: return 0x0C; /* -     */
    case 0x55: return 0x0D; /* =     */
    case 0x66: return 0x0E; /* BkSp  */
    case 0x0D: return 0x0F; /* Tab   */
    case 0x15: return 0x10; /* q     */
    case 0x1D: return 0x11; /* w     */
    case 0x24: return 0x12; /* e     */
    case 0x2D: return 0x13; /* r     */
    case 0x2C: return 0x14; /* t     */
    case 0x35: return 0x15; /* y     */
    case 0x3C: return 0x16; /* u     */
    case 0x43: return 0x17; /* i     */
    case 0x44: return 0x18; /* o     */
    case 0x4D: return 0x19; /* p     */
    case 0x54: return 0x1A; /* [     */
    case 0x5B: return 0x1B; /* ]     */
    case 0x5A: return 0x1C; /* Enter */
    case 0x1C: return 0x1E; /* a     */
    case 0x1B: return 0x1F; /* s     */
    case 0x23: return 0x20; /* d     */
    case 0x2B: return 0x21; /* f     */
    case 0x34: return 0x22; /* g     */
    case 0x33: return 0x23; /* h     */
    case 0x3B: return 0x24; /* j     */
    case 0x42: return 0x25; /* k     */
    case 0x4B: return 0x26; /* l     */
    case 0x4C: return 0x27; /* ;     */
    case 0x52: return 0x28; /* '     */
    case 0x5D: return 0x2B; /* \     */
    case 0x1A: return 0x2C; /* z     */
    case 0x22: return 0x2D; /* x     */
    case 0x21: return 0x2E; /* c     */
    case 0x2A: return 0x2F; /* v     */
    case 0x32: return 0x30; /* b     */
    case 0x31: return 0x31; /* n     */
    case 0x3A: return 0x32; /* m     */
    case 0x41: return 0x33; /* ,     */
    case 0x49: return 0x34; /* .     */
    case 0x4A: return 0x35; /* /     */
    case 0x29: return 0x39; /* Space */
    case 0x12: return 0x2A; /* LShift */
    case 0x59: return 0x36; /* RShift */
    case 0x58: return 0x3A; /* Caps  */
    case 0x11: return 0x38; /* Alt   */
    case 0x14: return 0x1D; /* Ctrl  */
    case 0x05: return 0x3B; /* F1    */
    case 0x06: return 0x3C; /* F2    */
    case 0x04: return 0x3D; /* F3    */
    case 0x0C: return 0x3E; /* F4    */
    case 0x03: return 0x3F; /* F5    */
    case 0x0B: return 0x40; /* F6    */
    case 0x83: return 0x41; /* F7    */
    case 0x0A: return 0x42; /* F8    */
    case 0x01: return 0x43; /* F9    */
    case 0x09: return 0x44; /* F10   */
    case 0x78: return 0x57; /* F11   */
    case 0x07: return 0x58; /* F12   */
    default:   return -1;
    }
}

/* KMI0 读取: 把队列中所有 set-2 码翻译为 set-1 make code 入 sc_buffer */
static void kbd_drain(void) {
    while (*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL) {
        uint8_t b = (uint8_t)(*(volatile uint32_t *)(KBD_BASE + KMI_DATA) & 0xFF);
        if (b == 0xE0) { ext_pending = 1; continue; }
        if (b == 0xF0) { brk_pending = 1; continue; }
        int code = set2_to_set1(b, ext_pending);
        ext_pending = 0;
        if (code < 0) { brk_pending = 0; continue; }
        if (brk_pending) {
            brk_pending = 0;
            code |= 0x80;           /* make+break 合成,与 x86 一致 */
        }
        sc_push((uint8_t)code);
    }
}

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

static unsigned char translate_scancode(unsigned char sc) {
    if (sc >= sizeof(scancode_ascii)) return 0;
    if (shift_down ^ caps_on) return scancode_shift_ascii[sc];
    return scancode_ascii[sc];
}

int keyboard_poll_scancode(void) {
    kbd_drain();
    if (sc_head == sc_tail) return -1;
    unsigned char sc = sc_buffer[sc_head];
    sc_head = (sc_head + 1) % SC_BUF_SIZE;
    return sc;
}

int keyboard_scancode_pending(void) {
    kbd_drain();
    return sc_head != sc_tail;
}

int keyboard_getchar(void) {
    kbd_drain();
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
        unsigned char c = translate_scancode(sc);
        if (c) return c;
    }
    return -1;
}

int keyboard_pending(void) {
    kbd_drain();
    if (key_head != key_tail) return 1;
    return sc_head != sc_tail;
}

/* 排空 KMI0 + 全部缓冲。QEMU 异步注入：KMI 空不代表注入队列已送达，
 * 用真实时间（SP804 sched_get_ticks, 100 Hz）等待静默 50ms 才算完成。 */
void keyboard_flush(void) {
    uint32_t t0 = sched_get_ticks();
    uint32_t last_activity = t0;
    for (;;) {
        int had = 0;
        while (*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL) {
            (void)*(volatile uint32_t *)(KBD_BASE + KMI_DATA);
            had = 1;
        }
        while (sc_head != sc_tail) { sc_head = (sc_head + 1) % SC_BUF_SIZE; had = 1; }
        key_head = 0;
        key_tail = 0;
        shift_down = 0;
        caps_on = 0;
        uint32_t now = sched_get_ticks();
        if ((uint32_t)(now - t0) >= 50)             /* 硬上限 500 ms */
            break;
        if (had || now != last_activity) {
            last_activity = now;
        } else if (last_activity == t0 && now == t0) {
            /* 定时器无动静（极早期）：尽力而为 */
            for (volatile uint32_t i = 0; i < 60000; i++)
                ;
            if (sched_get_ticks() == t0 &&
                !(*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL) &&
                sc_head == sc_tail)
                break;
            continue;
        }
        if ((uint32_t)(now - last_activity) >= 5)   /* 静默 50 ms */
            break;
        while (sched_get_ticks() == now) {}
    }
}

void keyboard_init(void) {
    *(volatile uint32_t *)(KBD_BASE + KMI_CR) = 0x11;
    *(volatile uint32_t *)(KBD_BASE + KMI_CLKDIV) = 0x0A;
    while (*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL)
        (void)*(volatile uint32_t *)(KBD_BASE + KMI_DATA);
    shift_down = 0;
    caps_on = 0;
    key_head = 0;
    key_tail = 0;
    sc_head = 0;
    sc_tail = 0;
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
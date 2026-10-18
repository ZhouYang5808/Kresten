#include <keyboard.h>
#include <idt.h>
#include <io.h>
#include <driver.h>

#define KEYBOARD_DATA 0x60
#define KEYBOARD_STATUS 0x64

#define KEY_BUF_SIZE 256

static char key_buffer[KEY_BUF_SIZE];
static int key_head = 0;
static int key_tail = 0;

/* Raw make-code queue for navigation keys (arrows, Esc, Enter, Alt, F4, Tab) */
#define SC_BUF_SIZE 64
static uint8_t sc_buffer[SC_BUF_SIZE];
static int sc_head = 0;
static int sc_tail = 0;

static void sc_push(uint8_t sc) {
    int next = (sc_tail + 1) % SC_BUF_SIZE;
    if (next != sc_head) {
        sc_buffer[sc_tail] = sc;
        sc_tail = next;
    }
}

static const unsigned char scancode_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0x7F,
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const unsigned char scancode_shift_ascii[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0x7F,
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static int shift_down = 0;
static int caps_on = 0;

static unsigned char translate_scancode(unsigned char sc) {
    if (sc >= sizeof(scancode_ascii)) return 0;
    unsigned char c = shift_down ? scancode_shift_ascii[sc] : scancode_ascii[sc];
    if (caps_on && c >= 'a' && c <= 'z') c -= 32;
    else if (caps_on && c >= 'A' && c <= 'Z') c += 32;
    return c;
}

static void keyboard_handler(registers_t *r) {
    (void)r;
    while (inb(KEYBOARD_STATUS) & 0x01) {
        unsigned char scancode = inb(KEYBOARD_DATA);
        if (scancode == 0x2A || scancode == 0x36) { shift_down = 1; continue; }
        if (scancode == 0xAA || scancode == 0xB6) { shift_down = 0; continue; }
        if (scancode == 0x3A) { caps_on = !caps_on; continue; }
        if (scancode & 0x80) continue;
        sc_push(scancode);
        unsigned char c = translate_scancode(scancode);
        if (c) {
            int next = (key_tail + 1) % KEY_BUF_SIZE;
            if (next != key_head) {
                key_buffer[key_tail] = c;
                key_tail = next;
            }
        }
    }
}

int keyboard_poll_raw(void) {
    int c = -1;
    __asm__ volatile("cli");
    while (inb(KEYBOARD_STATUS) & 0x01) {
        unsigned char scancode = inb(KEYBOARD_DATA);
        if (scancode == 0x2A || scancode == 0x36) { shift_down = 1; continue; }
        if (scancode == 0xAA || scancode == 0xB6) { shift_down = 0; continue; }
        if (scancode == 0x3A) { caps_on = !caps_on; continue; }
        if (scancode & 0x80) continue;
        unsigned char ch = translate_scancode(scancode);
        if (ch) { c = ch; break; }
    }
    __asm__ volatile("sti");
    return c;
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_handler);
}

int keyboard_getchar(void) {
    if (key_head == key_tail) {
        if (inb(KEYBOARD_STATUS) & 0x01) return keyboard_poll_raw();
        return -1;
    }
    unsigned char c = key_buffer[key_head];
    key_head = (key_head + 1) % KEY_BUF_SIZE;
    return c;
}

int keyboard_pending(void) {
    if (key_head != key_tail) return 1;
    return (inb(KEYBOARD_STATUS) & 0x01) ? 1 : 0;
}

/* 排空 8042 + 全部缓冲。后端(QEMU 等)异步注入: 8042 空不代表注入队列
 * 已送达, 必须用真实时间(PIT sched_ticks, 100 Hz)等待, 直到连续一段
 * 时间既无 8042 数据也无缓冲内容, 才算排空完成。 */
void keyboard_flush(void) {
    uint32_t t0 = sched_ticks;
    uint32_t last_activity = t0;
    for (;;) {
        __asm__ volatile("cli");
        int had = (key_head != key_tail) || (sc_head != sc_tail);
        key_head = 0;
        key_tail = 0;
        sc_head = 0;
        sc_tail = 0;
        while (inb(KEYBOARD_STATUS) & 0x01) {
            inb(KEYBOARD_DATA);
            had = 1;
        }
        __asm__ volatile("sti");
        uint32_t now = sched_ticks;
        if ((uint32_t)(now - t0) >= 400)            /* 硬上限 4000 ms: 键流式到达时继续排空 */
            break;
        if (had)
            last_activity = now;
        else if (last_activity == t0 && now == t0) {
            /* PIT 无动静(极早期/异常): 用端口访问让出事件循环, 尽力而为 */
            for (volatile uint32_t i = 0; i < 60000; i++)
                outb(0x80, 0);
            if (sched_ticks == t0 && !(inb(KEYBOARD_STATUS) & 0x01) &&
                key_head == key_tail)
                break;
            continue;
        }
        if ((uint32_t)(now - last_activity) >= 20)  /* 静默 200 ms 视为排空 */
            break;
        while (sched_ticks == now) {}
    }
}

/* 只丢弃此刻已缓冲/已送达 8042 的键，不等待。
 * 用于"程序运行前清键"：等待式排空会把运行后才到达的键(流式注入)
 * 一起吞掉，饿死用户程序；此处只需清掉加载期间收集的残留。 */
void keyboard_discard_pending(void) {
    __asm__ volatile("cli");
    key_head = 0;
    key_tail = 0;
    sc_head = 0;
    sc_tail = 0;
    while (inb(KEYBOARD_STATUS) & 0x01)
        inb(KEYBOARD_DATA);
    __asm__ volatile("sti");
}

int keyboard_poll_scancode(void) {
    if (sc_head == sc_tail)
        return -1;
    unsigned char sc = sc_buffer[sc_head];
    sc_head = (sc_head + 1) % SC_BUF_SIZE;
    return sc;
}

int keyboard_scancode_pending(void) {
    return sc_head != sc_tail;
}

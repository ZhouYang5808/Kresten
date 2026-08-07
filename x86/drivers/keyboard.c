#include <keyboard.h>
#include <idt.h>
#include <io.h>

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

static int shift_down = 0;
static int caps_on = 0;

static unsigned char translate_scancode(unsigned char sc) {
    if (sc >= sizeof(scancode_ascii)) return 0;
    if (shift_down ^ caps_on) return scancode_shift_ascii[sc];
    return scancode_ascii[sc];
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

int keyboard_poll_scancode(void) {
    if (sc_head == sc_tail)
        return -1;
    unsigned char sc = sc_buffer[sc_head];
    sc_head = (sc_head + 1) % SC_BUF_SIZE;
    return sc;
}

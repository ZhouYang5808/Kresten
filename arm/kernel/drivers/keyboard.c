/* ===== kernel/drivers/keyboard.c: PS/2 keyboard via PL050 KMI0 (versatilepb) =====
 * Drains KMI0 and translates PS/2 scancode set 2 into the set-1 make codes
 * the shared desktop expects (arrows, Enter, Esc, Alt, F4).
 */
#include <driver.h>
#include <keyboard.h>
#include <stdint.h>

#define KBD_BASE    0x10006000
#define KMI_CR      0x00
#define KMI_STAT    0x04
#define KMI_DATA    0x08
#define KMI_CLKDIV  0x0C

#define KMI_STAT_RXFULL 0x10

static int ext_pending;
static int brk_pending;

static int set2_to_set1(uint8_t b, int ext) {
    if (ext) {
        switch (b) {
        case 0x75: return 0x48; /* Up    */
        case 0x72: return 0x50; /* Down  */
        case 0x6B: return 0x4B; /* Left  */
        case 0x74: return 0x4D; /* Right */
        default:   return -1;
        }
    }
    switch (b) {
    case 0x76: return 0x01; /* Esc   */
    case 0x5A: return 0x1C; /* Enter */
    case 0x11: return 0x38; /* Alt   */
    case 0x0C: return 0x3E; /* F4    */
    default:   return -1;
    }
}

int keyboard_poll_scancode(void) {
    if (!(*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL))
        return -1;
    uint8_t b = (uint8_t)(*(volatile uint32_t *)(KBD_BASE + KMI_DATA) & 0xFF);
    if (b == 0xE0) {
        ext_pending = 1;
        return -1;
    }
    if (b == 0xF0) {
        brk_pending = 1;
        return -1;
    }
    int code = set2_to_set1(b, ext_pending);
    ext_pending = 0;
    if (code < 0) {
        brk_pending = 0;
        return -1;
    }
    if (brk_pending) {
        brk_pending = 0;
        code |= 0x80;
    }
    return code;
}

void keyboard_init(void) {
    *(volatile uint32_t *)(KBD_BASE + KMI_CR) = 0x11;
    *(volatile uint32_t *)(KBD_BASE + KMI_CLKDIV) = 0x0A;
    while (*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL)
        (void)*(volatile uint32_t *)(KBD_BASE + KMI_DATA);
    ext_pending = 0;
    brk_pending = 0;
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

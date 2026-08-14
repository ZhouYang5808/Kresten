/* ===== shared/drivers/ps2packet.c: PS/2 mouse packet state machine =====
 * Arch-independent: parses 3-byte PS/2 mouse packets (standard
 * IntelliMouse-compatible 3-byte format) and accumulates movement /
 * button state. Platform mouse drivers feed raw bytes via
 * ps2_packet_feed() (x86: from the IRQ12 handler; ARM: from a polled
 * PL050 KMI1 drain) and the desktop polls via mouse_poll().
 *
 * The state is read-modify-written here; callers that may race with an
 * interrupt handler (x86) must serialize access with their own
 * cli/sti around mouse_poll(). The ARM port is single-threaded.
 */
#include <stdint.h>
#include <mouse.h>

void ps2_packet_feed(uint8_t b);
int ps2_consume(int *dx, int *dy, int *buttons);

static volatile int s_dx = 0;
static volatile int s_dy = 0;
static volatile int s_btns = 0;

static uint8_t packet[3];
static int state = 0;

void ps2_packet_feed(uint8_t b) {
    if (state == 0) {
        if (!(b & 0x08)) /* sync byte: bit 3 must be set */
            return;
        packet[0] = b;
        state = 1;
    } else {
        packet[state] = b;
        state++;
        if (state == 3) {
            state = 0;
            s_dx += (int8_t)packet[1];
            s_dy -= (int8_t)packet[2];
            s_btns = packet[0] & 0x07;
        }
    }
}

int ps2_consume(int *dx, int *dy, int *buttons) {
    int x = s_dx, y = s_dy, b = s_btns;
    s_dx = 0;
    s_dy = 0;
    s_btns = 0;
    if (dx) *dx = x;
    if (dy) *dy = y;
    if (buttons) *buttons = b;
    return (x != 0 || y != 0 || b != 0) ? 1 : 0;
}

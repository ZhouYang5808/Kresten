#ifndef _PS2PACKET_H
#define _PS2PACKET_H

#include <stdint.h>

/* Feed one raw byte from the PS/2 mouse data stream (packet parser). */
void ps2_packet_feed(uint8_t b);

/* Consume accumulated movement/button state; returns 1 if any was set. */
int ps2_consume(int *dx, int *dy, int *buttons);

#endif

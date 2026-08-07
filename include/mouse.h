#ifndef _MOUSE_H
#define _MOUSE_H

#include <stdint.h>

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

int mouse_init(void);
/* Returns 1 if new data was consumed, 0 otherwise.
 * dx/dy/buttons receive the accumulated movement and button state. */
int mouse_poll(int *dx, int *dy, int *buttons);

#endif

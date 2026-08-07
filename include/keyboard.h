#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
int keyboard_getchar(void);
int keyboard_poll_scancode(void);

#endif

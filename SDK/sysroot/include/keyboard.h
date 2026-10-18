#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
int keyboard_getchar(void);
int keyboard_pending(void);
int keyboard_poll_scancode(void);
int keyboard_scancode_pending(void);
void keyboard_flush(void);
void keyboard_discard_pending(void);

#endif

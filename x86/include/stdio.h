#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

void putchar(char c);
void puts(const char *s);

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list args);

void serial_init(void);

void gets(char *buf, int max_len);

void set_output_capture(char *buf, int size);
void end_output_capture(void);

#endif

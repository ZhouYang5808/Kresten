#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <io.h>
#include <driver.h>
#include <keyboard.h>

// Anchor for compressed CRT function pointer table
void _crt_base(void) {}

static int capture_active = 0;
static char *capture_buf = 0;
static int capture_pos = 0;
static int capture_max = 0;

/* No UART (e.g. VMware without a serial port): port reads float high (0xFF). */
static int serial_present = 1;

void set_output_capture(char *buf, int size) {
    capture_active = 1;
    capture_buf = buf;
    capture_pos = 0;
    capture_max = size - 1;
}

void end_output_capture(void) {
    if (capture_active && capture_buf)
        capture_buf[capture_pos > capture_max ? capture_max : capture_pos] = '\0';
    capture_active = 0;
    capture_buf = 0;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC1);
    outb(COM1 + 4, 0x0B);
    serial_present = (inb(COM1 + 5) != 0xFF);
}

void putchar(char c) {
    uint32_t f;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(f));
    if (capture_active && capture_buf && capture_pos < capture_max) {
        capture_buf[capture_pos++] = c;
        if (f & 0x200) __asm__ volatile ("sti");
        return;
    }
    vga_putc(c);
    if (serial_present) {
        int spins = 0;
        while (!(inb(COM1 + 5) & 0x20)) {
            if (++spins > 50000) break;
        }
        if (spins <= 50000) {
            if (c == '\n') outb(COM1, '\r');
            outb(COM1, c);
        }
        /* on timeout: keep the port, just drop this char */
    }
    if (f & 0x200) __asm__ volatile ("sti");
}

void puts(const char *s) {
    while (*s) putchar(*s++);
}

#define HIST_SIZE 8
static char history[HIST_SIZE][128];
static int hist_count = 0;
static int hist_cur = 0;

static void hist_add(const char *line) {
    if (!line || !*line) return;
    if (hist_count > 0 && strcmp(history[(hist_count - 1) % HIST_SIZE], line) == 0) return;
    int idx = hist_count % HIST_SIZE;
    strncpy(history[idx], line, 127);
    history[idx][127] = '\0';
    hist_count++;
    hist_cur = hist_count;
}

void gets(char *buf, int max_len) {
    int i = 0;
    int hist_idx = hist_cur;
    while (i < max_len - 1) {
        unsigned char c;
        int got = 0;
        if (serial_present && (inb(COM1 + 5) & 0x01)) {
            c = (unsigned char)inb(COM1);
            got = 1;
        } else {
            int k = keyboard_getchar();
            if (k >= 0) {
                c = (unsigned char)k;
                got = 1;
            }
        }
        if (!got) {
            sched_yield();  /* cooperative mode: hand CPU to tasks while idle */
            continue;
        }
        if (c == '\r' || c == '\n') {
            putchar('\n');
            if (serial_present && (inb(COM1 + 5) & 0x01)) {
                unsigned char n = (unsigned char)inb(COM1);
                if (n != '\r' && n != '\n') (void)n;
            }
            break;
        } else if (c == '\b' || c == 127) {
            if (i > 0) { i--; putchar('\b'); putchar(' '); putchar('\b'); }
        } else if (c == 0x1B && serial_present) {
            unsigned char seq[8];
            int si = 0;
            seq[si++] = c;
            for (int j = 0; j < 7; j++) {
                int sg = 0;
                while (!(inb(COM1 + 5) & 0x01)) {
                    if (++sg > 200000) break;
                }
                if (sg > 200000) break;
                seq[si] = (unsigned char)inb(COM1);
                if ((seq[si] >= 'a' && seq[si] <= 'z') || (seq[si] >= 'A' && seq[si] <= 'Z') || seq[si] == '~') { si++; break; }
                si++;
            }
            if (si >= 2 && seq[0] == 0x1B && seq[1] == '[') {
                if (si >= 3 && seq[2] == 'A' && hist_idx > 0) {
                    while (i > 0) { i--; putchar('\b'); putchar(' '); putchar('\b'); }
                    hist_idx--;
                    int hidx = hist_idx % HIST_SIZE;
                    if (strlen(history[hidx]) > 0) {
                        strncpy(buf, history[hidx], max_len - 1);
                        buf[max_len - 1] = '\0';
                        i = strlen(buf);
                        puts(buf);
                    }
                } else if (si >= 3 && seq[2] == 'B' && hist_idx < hist_count) {
                    while (i > 0) { i--; putchar('\b'); putchar(' '); putchar('\b'); }
                    if (hist_idx < hist_count - 1) hist_idx++;
                    else { buf[0] = '\0'; i = 0; continue; }
                    int hidx = hist_idx % HIST_SIZE;
                    if (strlen(history[hidx]) > 0) {
                        strncpy(buf, history[hidx], max_len - 1);
                        buf[max_len - 1] = '\0';
                        i = strlen(buf);
                        puts(buf);
                    }
                }
            }
        } else if (c >= ' ' && c <= '~') {
            buf[i++] = c;
            putchar(c);
        }
    }
    buf[i] = '\0';
    hist_add(buf);
}

static void print_hex(unsigned int num) {
    char *digits = "0123456789abcdef";
    if (num == 0) { putchar('0'); return; }
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        int nibble = (num >> shift) & 0xF;
        if (nibble || started) { putchar(digits[nibble]); started = 1; }
    }
    if (!started) putchar('0');
}

static void print_hex_upper(unsigned int num) {
    char *digits = "0123456789ABCDEF";
    if (num == 0) { putchar('0'); return; }
    int started = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        int nibble = (num >> shift) & 0xF;
        if (nibble || started) { putchar(digits[nibble]); started = 1; }
    }
    if (!started) putchar('0');
}

int vprintf(const char *fmt, va_list args) {
    int count = 0;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int left = 0, zeropad = 0;
            while (*fmt == '-' || *fmt == '0') {
                if (*fmt == '-') left = 1;
                if (*fmt == '0') zeropad = 1;
                fmt++;
            }
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
            if (*fmt == 'c') {
                char c = (char)va_arg(args, int);
                putchar(c); count++;
            } else if (*fmt == 's') {
                char *s = va_arg(args, char *);
                if (!s) s = "(null)";
                int len = strlen(s);
                if (!left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                puts(s);
                if (left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                count += (width > len ? width : len);
            } else if (*fmt == 'd' || *fmt == 'i') {
                int num = va_arg(args, int);
                char buf[32]; itoa(num, buf, 10);
                int len = strlen(buf);
                if (!left && width > 0 && len < width) {
                    char pad = zeropad ? '0' : ' ';
                    if (zeropad && num < 0) { putchar('-'); buf[0] = '0'; len--; }
                    for (int i = 0; i < width - len; i++) putchar(pad);
                }
                puts(buf);
                if (left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                count += (width > len ? width : len);
            } else if (*fmt == 'u') {
                unsigned int num = va_arg(args, unsigned int);
                char buf[32]; itoa((int)num, buf, 10);
                int len = strlen(buf);
                if (!left && width > 0 && len < width) {
                    char pad = zeropad ? '0' : ' ';
                    for (int i = 0; i < width - len; i++) putchar(pad);
                }
                puts(buf);
                if (left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                count += (width > len ? width : len);
            } else if (*fmt == 'x') {
                unsigned int num = va_arg(args, unsigned int);
                int len = 0; unsigned int tmp = num;
                if (tmp == 0) len = 1; else while (tmp) { len++; tmp >>= 4; }
                if (!left && width > 0 && len < width) {
                    char pad = zeropad ? '0' : ' ';
                    for (int i = 0; i < width - len; i++) putchar(pad);
                }
                print_hex(num);
                if (left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                count += (width > len ? width : len);
            } else if (*fmt == 'X') {
                unsigned int num = va_arg(args, unsigned int);
                int len = 0; unsigned int tmp = num;
                if (tmp == 0) len = 1; else while (tmp) { len++; tmp >>= 4; }
                if (!left && width > 0 && len < width) {
                    char pad = zeropad ? '0' : ' ';
                    for (int i = 0; i < width - len; i++) putchar(pad);
                }
                print_hex_upper(num);
                if (left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                count += (width > len ? width : len);
            } else if (*fmt == 'p') {
                putchar('0'); putchar('x');
                unsigned int num = (unsigned int)va_arg(args, void *);
                print_hex(num); count += 10;
            } else if (*fmt == '%') {
                putchar('%'); count++;
            } else {
                putchar('%'); putchar(*fmt); count += 2;
            }
        } else {
            putchar(*fmt); count++;
        }
        fmt++;
    }
    return count;
}

int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf(fmt, args);
    va_end(args);
    return ret;
}

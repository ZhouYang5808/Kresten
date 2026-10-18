/* ===== kernel/libc/stdio.c (RISC-V): 16550 UART @ 0x10000000 ===== */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define UART_BASE 0x10000000UL
#define UART_THR 0x00
#define UART_RBR 0x00
#define UART_LSR 0x05

void _crt_base(void) {}

static int capture_active = 0;
static char *capture_buf = 0;
static int capture_pos = 0;
static int capture_max = 0;
static int capture_discard = 0;   /* snprintf: 超出 size 的字符直接丢弃, 不落 UART */

void set_output_capture(char *buf, int size) {
    capture_active = 1;
    capture_buf = buf;
    capture_pos = 0;
    capture_max = size - 1;
}

void end_output_capture(void) {
    if (capture_active && capture_buf && capture_max >= 0)
        capture_buf[capture_pos > capture_max ? capture_max : capture_pos] = '\0';
    capture_active = 0;
    capture_buf = 0;
    capture_discard = 0;
}

static uint8_t uart_rx_ready(void) {
    return (*(volatile uint8_t *)(UART_BASE + UART_LSR)) & 0x01;
}

static uint8_t uart_tx_ready(void) {
    return (*(volatile uint8_t *)(UART_BASE + UART_LSR)) & 0x20;
}

static void uart_putc(char c) {
    while (!uart_tx_ready()) { }
    *(volatile uint8_t *)(UART_BASE + UART_THR) = (uint8_t)c;
}

void putchar(char c) {
    if (capture_active && capture_buf) {
        if (capture_pos < capture_max) {
            capture_buf[capture_pos++] = c;
        } else if (!capture_discard) {
            uart_putc(c);            /* 旧行为: 捕获满则转 UART; snprintf 下丢弃 */
        }
        return;
    }
    uart_putc(c);
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
        while (!uart_rx_ready()) { }
        unsigned char c = (unsigned char)(*(volatile uint8_t *)(UART_BASE + UART_RBR));
        if (c == '\r' || c == '\n') {
            putchar('\n');
            if (uart_rx_ready()) {
                (void)*(volatile uint8_t *)(UART_BASE + UART_RBR);
            }
            break;
        } else if (c == '\b' || c == 127) {
            if (i > 0) { i--; putchar('\b'); putchar(' '); putchar('\b'); }
        } else if (c == 0x1B) {
            unsigned char seq[8];
            int si = 0;
            seq[si++] = c;
            for (int j = 0; j < 7; j++) {
                while (!uart_rx_ready()) { }
                seq[si] = (unsigned char)(*(volatile uint8_t *)(UART_BASE + UART_RBR));
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
                    else { buf[0] = '\0'; i = 0; puts(""); continue; }
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
                char buf[32];
                int len = 0;
                do { buf[len++] = (char)('0' + num % 10); num /= 10; }
                while (num && len < 31);
                buf[len] = '\0';
                for (int j = 0; j < len / 2; j++) {
                    char t = buf[j]; buf[j] = buf[len - 1 - j]; buf[len - 1 - j] = t;
                }
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
                unsigned long long num = (unsigned long long)(uintptr_t)va_arg(args, void *);
                unsigned int lo = (unsigned int)num;
                print_hex(lo);
                if ((num >> 32) != 0) {
                    putchar('0'); putchar('x');
                    print_hex((unsigned int)(num >> 32));
                }
                count += 10;
            } else if (*fmt == 'l') {
                /* %lx / %ld / %llx: 按 64 位读, 低位打印 (32 位 vprintf) */
                fmt++;
                unsigned long long v;
                if (*fmt == 'x' || *fmt == 'X') {
                    v = va_arg(args, unsigned long long);
                    int len = 0; unsigned long long tmp = v;
                    char b64[24];
                    if (tmp == 0) len = 1; else while (tmp) { len++; tmp >>= 4; }
                    if (!left && width > 0 && len < width) {
                        char pad = zeropad ? '0' : ' ';
                        for (int i = 0; i < width - len; i++) putchar(pad);
                    }
                    char *d = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                    int i2 = len;
                    b64[i2] = '\0';
                    while (i2 > 0) { i2--; b64[i2] = d[v & 0xF]; v >>= 4; }
                    puts(b64);
                    if (left && width > 0 && len < width) for (int i = 0; i < width - len; i++) putchar(' ');
                    count += (width > len ? width : len);
                } else if (*fmt == 'd' || *fmt == 'i') {
                    long long num = va_arg(args, long long);
                    char b64[24];
                    int i2 = 0;
                    if (num < 0) { putchar('-'); num = -num; i2++; }
                    if (num == 0) b64[i2++] = '0';
                    while (num && i2 < 22) { b64[i2++] = (char)('0' + num % 10); num /= 10; }
                    while (i2 > 0) { i2--; putchar(b64[i2]); }
                    count += 2;
                } else {
                    putchar('%'); putchar('l'); putchar(*fmt); count += 3;
                }
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

int vsprintf(char *buf, const char *fmt, va_list args) {
    set_output_capture(buf, 4096);
    int ret = vprintf(fmt, args);
    end_output_capture();
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsprintf(buf, fmt, args);
    va_end(args);
    return ret;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret;
    static char dummy[1];
    capture_discard = 1;
    if (!buf || size == 0) {
        /* 无缓冲区/零长度: 只统计, 不写任何内存 */
        set_output_capture(dummy, 1);
    } else {
        set_output_capture(buf, (int)size);
    }
    ret = vprintf(fmt, args);
    end_output_capture();
    va_end(args);
    return ret;
}
#include <stdlib.h>
#include <ctype.h>

int atoi(const char *s) {
    int sign = 1;
    int num = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) { num = num * 10 + (*s - '0'); s++; }
    return sign * num;
}

char *itoa(int num, char *buf, int base) {
    char *digits = "0123456789abcdef";
    char temp[32];
    int i = 0;
    int is_negative = 0;
    if (base == 10 && num < 0) { is_negative = 1; num = -num; }
    if (num == 0) { temp[i++] = '0'; }
    else {
        while (num > 0) {
            int n = num, quotient = 0;
            while (n >= base) { n -= base; quotient++; }
            temp[i++] = digits[n];
            num = quotient;
        }
    }
    if (is_negative) temp[i++] = '-';
    int len = i;
    for (int j = 0; j < len; j++) buf[j] = temp[len - 1 - j];
    buf[len] = '\0';
    return buf;
}

static unsigned int _seed = 1;

int rand(void) { _seed = _seed * 1103515245 + 12345; return (int)(_seed / 65536) % 32768; }

void srand(unsigned int seed) { _seed = seed; }

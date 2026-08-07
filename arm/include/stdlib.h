#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

int atoi(const char *s);
char *itoa(int num, char *buf, int base);
int rand(void);
void srand(unsigned int seed);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

void heap_init(void *start, size_t len);

#endif

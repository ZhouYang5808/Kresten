/* ===== kernel: stdlib.c ===== */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

/* ===== kernel: stdlib ===== */

int atoi(const char *s) {
    int sign = 1;
    int num = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) {
        num = num * 10 + (*s - '0');
        s++;
    }
    return sign * num;
}

char *itoa(int num, char *buf, int base) {
    char *digits = "0123456789abcdef";
    char temp[32];
    int i = 0;
    int is_negative = 0;
    
    if (base == 10 && num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    if (num == 0) {
        temp[i++] = '0';
    } else {
        // 用减法模拟除法，避免依赖库
        while (num > 0) {
            int n = num;
            int quotient = 0;
            while (n >= base) {
                n -= base;
                quotient++;
            }
            temp[i++] = digits[n];
            num = quotient;
        }
    }
    
    if (is_negative) {
        temp[i++] = '-';
    }
    
    int len = i;
    for (int j = 0; j < len; j++) {
        buf[j] = temp[len - 1 - j];
    }
    buf[len] = '\0';
    return buf;
}

// 简单的伪随机数生成器
static unsigned int _seed = 1;

int rand(void) {
    _seed = _seed * 1103515245 + 12345;
    return (int)(_seed / 65536) % 32768;
}

void srand(unsigned int seed) {
    _seed = seed;
}

/* ===== kernel: stdlib (malloc) ===== */
/* ===== kernel: malloc ===== */

#define HEAP_SIZE (64 * 1024)
#define ALIGN 4
#define MAGIC_FREE 0x1234
#define MAGIC_USED 0x5678

typedef struct block {
    unsigned short magic;
    unsigned short pad;
    size_t size;
    struct block *next;
    struct block *prev;
} block_t;

static char heap_pool[HEAP_SIZE];
static int heap_inited = 0;
static block_t *free_list = 0;

void heap_init(void *start, size_t len) {
    (void)start; (void)len;
    block_t *b = (block_t *)heap_pool;
    b->magic = MAGIC_FREE;
    b->size = HEAP_SIZE - sizeof(block_t);
    b->next = 0;
    b->prev = 0;
    free_list = b;
    heap_inited = 1;
}

static void heap_ensure(void) {
    if (!heap_inited) heap_init(0, 0);
}

static block_t *find_free(size_t size) {
    block_t *b = free_list;
    while (b) {
        if (b->magic == MAGIC_FREE && b->size >= size) return b;
        b = b->next;
    }
    return 0;
}

static void split_block(block_t *b, size_t size) {
    if (b->size < size + sizeof(block_t) + 8) return;
    block_t *newb = (block_t *)((char *)b + sizeof(block_t) + size);
    newb->magic = MAGIC_FREE;
    newb->size = b->size - size - sizeof(block_t);
    newb->next = b->next;
    newb->prev = b;
    if (b->next) b->next->prev = newb;
    b->next = newb;
    b->size = size;
}

static void remove_free(block_t *b) {
    if (b->prev) b->prev->next = b->next;
    else free_list = b->next;
    if (b->next) b->next->prev = b->prev;
}

static void insert_free(block_t *b) {
    b->magic = MAGIC_FREE;
    b->prev = 0;
    b->next = free_list;
    if (free_list) free_list->prev = b;
    free_list = b;
}

static block_t *merge_adjacent(block_t *a) {
    block_t *b = (block_t *)((char *)a + sizeof(block_t) + a->size);
    if ((char *)b < (char *)heap_pool + HEAP_SIZE &&
        b->magic == MAGIC_FREE && (void *)b > (void *)a) {
        remove_free(b);
        a->size += sizeof(block_t) + b->size;
    }
    return a;
}

void *malloc(size_t size) {
    heap_ensure();
    if (size == 0) return 0;
    size = (size + ALIGN - 1) & ~(ALIGN - 1);
    block_t *b = find_free(size);
    if (!b) return 0;
    split_block(b, size);
    b->magic = MAGIC_USED;
    remove_free(b);
    return (char *)b + sizeof(block_t);
}

void free(void *ptr) {
    if (!ptr) return;
    block_t *b = (block_t *)((char *)ptr - sizeof(block_t));
    if (b->magic != MAGIC_USED) return;
    b->magic = MAGIC_FREE;
    insert_free(b);
    merge_adjacent(b);
    block_t *prev = (block_t *)((char *)b - sizeof(block_t));
    if ((char *)prev >= (char *)heap_pool && prev->magic == MAGIC_FREE) {
        merge_adjacent(prev);
    }
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return 0; }
    block_t *b = (block_t *)((char *)ptr - sizeof(block_t));
    size_t old_size = b->size;
    void *newp = malloc(size);
    if (!newp) return 0;
    size_t copy = old_size < size ? old_size : size;
    memcpy(newp, ptr, copy);
    free(ptr);
    return newp;
}


/* ===== x86/gdt.c: 64-bit GDT (long mode) ===== */
#include <stdint.h>

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

/* [0]=null [1]=code64 DPL0(08) [2]=data64 DPL0(10) [3]=code16(18) [4]=data16(20)
 * [5]=user code64 DPL3(28) [6]=user data64 DPL3(30)
 * [7..8]=TSS64(38, 16-byte descriptor) */
static uint64_t gdt[9];

static uint8_t tss[104] __attribute__((aligned(16)));
static uint8_t tss_stack[16384] __attribute__((aligned(16)));

static void tss_init(void) {
    uint64_t base = (uint64_t)(uintptr_t)tss;
    tss[0] = 0;
    uint64_t rsp0 = (uint64_t)(uintptr_t)(tss_stack + sizeof(tss_stack));
    tss[4] = rsp0 & 0xFF;
    tss[5] = (rsp0 >> 8) & 0xFF;
    tss[6] = (rsp0 >> 16) & 0xFF;
    tss[7] = (rsp0 >> 24) & 0xFF;
    tss[8] = (rsp0 >> 32) & 0xFF;
    tss[9] = (rsp0 >> 40) & 0xFF;
    tss[10] = (rsp0 >> 48) & 0xFF;
    tss[11] = (rsp0 >> 56) & 0xFF;
    tss[100] = sizeof(tss);     /* I/O map base (end of TSS, no bitmap) */

    uint64_t limit = sizeof(tss) - 1;
    gdt[7] = (limit & 0xFFFF) |
             ((base & 0xFFFFFF) << 16) |
             ((uint64_t)0x89 << 40) |
             ((uint64_t)(((limit >> 16) & 0xF) | 0x40) << 48) |
             ((uint64_t)((base >> 24) & 0xFF) << 56);
    gdt[8] = base >> 32;
}

void gdt_init(void) {
    gdt[0] = 0x0000000000000000ULL;                 /* null */
    gdt[1] = 0x00AF9A000000FFFFULL;                 /* code64: base 0, L=1, DPL0, P, S, X */
    gdt[2] = 0x00AF92000000FFFFULL;                 /* data64: base 0, L=1, DPL0, P, S, W */
    gdt[3] = 0x00009A000000FFFFULL;                 /* code16: for real-mode switch */
    gdt[4] = 0x000092000000FFFFULL;                 /* data16 */
    gdt[5] = 0x00AFFA000000FFFFULL;                 /* user code64: DPL3 */
    gdt[6] = 0x00AFF2000000FFFFULL;                 /* user data64: DPL3 */

    tss_init();

    gdt_ptr_t p;
    p.limit = sizeof(gdt) - 1;
    p.base = (uint64_t)gdt;

    __asm__ volatile ("lgdt %0" : : "m"(p));
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        ::: "ax", "memory");
    __asm__ volatile ("mov $0x38, %%ax; ltr %%ax" ::: "ax");
}

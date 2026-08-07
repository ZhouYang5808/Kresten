/* ===== x86/gdt.c: own flat GDT (no dependence on GRUB's GDT) ===== */
#include <stdint.h>

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static uint64_t gdt[5];

void gdt_init(void) {
    gdt[0] = 0x0000000000000000ULL;                 /* null */
    gdt[1] = 0x00CF9A000000FFFFULL;                 /* code32: base 0, limit 4G, DPL0, P, S, X */
    gdt[2] = 0x00CF92000000FFFFULL;                 /* data32: base 0, limit 4G, DPL0, P, S, W */
    gdt[3] = 0x00009A000000FFFFULL;                 /* code16: for real-mode switch */
    gdt[4] = 0x000092000000FFFFULL;                 /* data16 */

    gdt_ptr_t p;
    p.limit = sizeof(gdt) - 1;
    p.base = (uint32_t)gdt;

    __asm__ volatile ("lgdt %0" : : "m"(p));
    __asm__ volatile (
        "ljmp $0x08, $1f\n"
        "1:\n"
        ::: "memory");
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        ::: "ax", "memory");
}

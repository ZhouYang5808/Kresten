#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed)) registers_t;

void idt_init(void);
void isr_handler(registers_t *r);
void irq_handler(registers_t *r);
void irq_install_handler(int irq, void (*handler)(registers_t *));
void irq_uninstall_handler(int irq);

#endif

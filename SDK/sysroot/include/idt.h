#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rsp;
    uint64_t int_no;
    uint64_t err_code;
    uint64_t rip, cs, eflags;
} __attribute__((packed)) registers_t;

void idt_init(void);
void isr_handler(registers_t *r);
void irq_handler(registers_t *r);
void irq_install_handler(int irq, void (*handler)(registers_t *));
void irq_uninstall_handler(int irq);

#endif

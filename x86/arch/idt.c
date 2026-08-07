#include <idt.h>
#include <stdio.h>
#include <io.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_ICW4  0x11
#define ICW1_INIT  0x10

extern uint32_t isr_stub_table[];
extern uint32_t irq_stub_table[];

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

static void (*irq_handlers[16])(registers_t *);
static const char *exception_msgs[32] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security Exception",
    "Reserved"
};

static void idt_set_entry(int n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].offset_low = base & 0xFFFF;
    idt[n].selector = sel;
    idt[n].zero = 0;
    idt[n].type_attr = flags;
    idt[n].offset_high = (base >> 16) & 0xFFFF;
}

static void pic_remap(void) {
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xFD);
    outb(PIC2_DATA, 0xFF);
}

static void pic_send_eoi(int irq_no) {
    if (irq_no >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;
    for (int i = 0; i < 256; i++)
        idt_set_entry(i, 0, 0, 0);
    for (int i = 0; i < 32; i++)
        idt_set_entry(i, isr_stub_table[i], 0x08, 0x8E);
    pic_remap();
    for (int i = 0; i < 16; i++)
        idt_set_entry(i + 32, irq_stub_table[i], 0x08, 0x8E);
    for (int i = 0; i < 16; i++)
        irq_handlers[i] = 0;
    __asm__ volatile ("lidt %0" : : "m"(idt_ptr));
    __asm__ volatile ("sti");
}

void isr_handler(registers_t *r) {
    printf("[EXCEPTION] %s (int 0x%x, err 0x%x)\n", exception_msgs[r->int_no], r->int_no, r->err_code);
    printf("  EIP=0x%08x CS=0x%04x EFLAGS=0x%08x\n", r->eip, r->cs, r->eflags);
    printf("  EAX=0x%08x EBX=0x%08x ECX=0x%08x EDX=0x%08x\n", r->eax, r->ebx, r->ecx, r->edx);
    printf("  ESP=0x%08x EBP=0x%08x ESI=0x%08x EDI=0x%08x\n", r->esp, r->ebp, r->esi, r->edi);
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void irq_handler(registers_t *r) {
    int irq = r->int_no - 32;
    if (irq >= 0 && irq < 16 && irq_handlers[irq])
        irq_handlers[irq](r);
    pic_send_eoi(irq);
}

void irq_install_handler(int irq, void (*handler)(registers_t *)) {
    if (irq >= 0 && irq < 16) irq_handlers[irq] = handler;
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) irq_handlers[irq] = 0;
}

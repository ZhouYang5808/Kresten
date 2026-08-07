.section .text
.code32

.macro ISR_NOERR n
.align 4
.globl isr_stub_\n
isr_stub_\n:
    push $0
    push $\n
    jmp isr_common_handler
.endm

.macro ISR_ERR n
.align 4
.globl isr_stub_\n
isr_stub_\n:
    push $\n
    jmp isr_common_handler
.endm

.macro IRQ_STUB n
.align 4
.globl irq_stub_\n
irq_stub_\n:
    push $0
    push $\n
    jmp irq_common_handler
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47

isr_common_handler:
    cli
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    push %esp
    call isr_handler
    add $4, %esp
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

irq_common_handler:
    cli
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    push %esp
    call irq_handler
    add $4, %esp
    movl sched_switch_flag, %eax
    testl %eax, %eax
    jz 1f
    movl sched_next_esp, %esp
    movl $0, sched_switch_flag
1:
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

.section .data
.globl isr_stub_table
isr_stub_table:
.long isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3
.long isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7
.long isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11
.long isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
.long isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
.long isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
.long isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
.long isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31

.globl irq_stub_table
irq_stub_table:
.long irq_stub_32, irq_stub_33, irq_stub_34, irq_stub_35
.long irq_stub_36, irq_stub_37, irq_stub_38, irq_stub_39
.long irq_stub_40, irq_stub_41, irq_stub_42, irq_stub_43
.long irq_stub_44, irq_stub_45, irq_stub_46, irq_stub_47

.section .text
.code64

.macro ISR_NOERR n
.align 4
.globl isr_stub_\n
isr_stub_\n:
    pushq $0
    pushq $\n
    jmp isr_common_handler
.endm

.macro ISR_ERR n
.align 4
.globl isr_stub_\n
isr_stub_\n:
    pushq $\n
    jmp isr_common_handler
.endm

.macro IRQ_STUB n
.align 4
.globl irq_stub_\n
irq_stub_\n:
    pushq $0
    pushq $\n
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

/* registers_t layout (low -> high):
 *   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
 *   rsp int_no err_code rip cs eflags */
isr_common_handler:
    cli
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    pushq %rsp
    movq %rsp, %rdi
    call isr_handler
    addq $8, %rsp
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    /* skip int_no, err_code; land on rip slot. iretq pops the 5 qwords
     * the CPU pushed at interrupt time (rip cs rflags rsp ss) */
    addq $16, %rsp
    iretq

irq_common_handler:
    cli
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    pushq %rsp
    movq %rsp, %rdi
    call irq_handler
    addq $8, %rsp
    cmpq $0, sched_switch_flag(%rip)
    jne sched_restore_common
1:
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    /* skip int_no, err_code; land on rip slot. iretq pops the 5 qwords
     * the CPU pushed at interrupt time (rip cs rflags rsp ss) */
    addq $16, %rsp
    iretq

/* Shared resume path: switch RSP to sched_next_rsp (a full interrupt frame
 * either saved by the PIT handler or built by sched_yield_impl) and iretq
 * back into the task. Used by both the preemptive (timer) and the
 * cooperative (sched_yield) switch, which is what makes live mode
 * switching possible: the frame format is identical. */
sched_restore_common:
    movq sched_next_rsp(%rip), %rsp
    movq $0, sched_switch_flag(%rip)
    addq $8, %rsp
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    /* skip int_no, err_code; land on rip slot. 64-bit IRETQ pops 5 qwords
     * (rip cs eflags rsp ss) in that order, and the popped SS is
     * validated: an invalid SS raises #GP(err=SS) -- observed #GP(0xd6c8)
     * from a popped SS of 0x64d6c8 when a yield frame lacked ss/rsp
     * slots. Both the PIT path (CPU pushes valid rsp/ss at interrupt
     * time) and sched_yield_impl (explicit slots) guarantee valid
     * values here. */
    addq $16, %rsp
    iretq

/* Cooperative yield: build a synthetic interrupt frame for the *current*
 * task (the call site's return address becomes rip) on its own stack,
 * then jump into sched_restore_common to resume the target task.
 *
 * Frame layout (low -> high), identical to a real timer frame:
 *   rsp field | rax..r15 (15) | int_no | err_code | rip | cs | eflags
 *   | rsp | ss
 *
 * The rsp/ss slots are REQUIRED: 64-bit IRETQ pops 5 qwords (rip cs
 * eflags rsp ss) and the popped SS is validated -- a yield frame without
 * them exposes the task's live stack as SS, raising #GP(err=SS)
 * (observed #GP(0xd6c8), popped SS 0x64d6c8). The layout matches the
 * C-built initial frames (sched_setup_frame): rsp slot = address of the
 * ss slot, ss slot = kernel data 0x10, so after the resume path
 * sched_yield() finishes normally -- its own frame and the caller's
 * return address are right above, so the final `ret` returns to the
 * caller correctly.
 *
 * rcx/rax slots hold scratch values; they are caller-saved registers so
 * the C side never relies on them after the call.
 *
 * The new frame address is written straight into sched_task_ctx[owner]
 * (owner = the yielding task, set by sched_yield) BEFORE the switch, so
 * every task slot is always the latest valid frame. This is what makes
 * the switch robust: no "write back later" step can clobber a slot with
 * a stale frame.
 *
 * Caller must have set sched_next_rsp = target context_rsp. Off we go. */
.globl sched_yield_impl
sched_yield_impl:
    cli
    popq %rax            /* return address -> rip slot, rsp = R0 */
    pushq $0x10          /* ss slot (kernel data); rsp = R0-8 */
    pushq %rsp           /* rsp slot = R0-8 (pre-decrement value = address
                          * of the ss slot, same layout the C-built initial
                          * frames use) */
    pushq $0x202         /* eflags: IF=1 */
    pushq $0x08          /* cs (kernel) */
    pushq %rax           /* rip = return address */
    pushq $0             /* err_code */
    pushq $0             /* int_no */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    pushq %rsp           /* rsp field (points past itself => FB+8) */
    leaq sched_task_ctx(%rip), %rdx
    movq sched_yield_owner(%rip), %rcx
    movq %rsp, (%rdx,%rcx,8)
    jmp sched_restore_common

.section .data
.globl isr_stub_table
isr_stub_table:
.quad isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3
.quad isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7
.quad isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11
.quad isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15
.quad isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19
.quad isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23
.quad isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27
.quad isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31

.globl irq_stub_table
irq_stub_table:
.quad irq_stub_32, irq_stub_33, irq_stub_34, irq_stub_35
.quad irq_stub_36, irq_stub_37, irq_stub_38, irq_stub_39
.quad irq_stub_40, irq_stub_41, irq_stub_42, irq_stub_43
.quad irq_stub_44, irq_stub_45, irq_stub_46, irq_stub_47

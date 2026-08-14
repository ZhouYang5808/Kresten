.section .text
.code64

/* Context layout (offsets match Context/CoopCtx in C):
 *   rip(0) rsp(8) rbp(16) r12(24) r15(32) r14(40) r13(48) rbx(56) rflags(64) */
.globl context_switch
context_switch:
    /* 保存调用方 RFLAGS（含 IF）到 ctx.rflags（偏移 64），再进切换窗口。
     * 恢复时 popfq 还原各流自己的 IF 状态：
     *   被切走时 IF=1（shell 轮询）→ 恢复 IF=1，继续收中断；
     *   被切走时 IF=0（syscall 处理）→ 恢复 IF=0，syscall 全程不被中断。
     * 旧实现恢复后无条件 sti，会让 syscall 处理（lx_trap_stack）被 PIT 打断，
     * 同 CPL 中断恢复路径 iretq 从错位槽位弹帧而 #GP。 */
    pushfq
    cli
    popq %rax
    movq %rax, 64(%rdi)

    movq %rbx, 56(%rdi)
    movq %r13, 48(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 32(%rdi)
    movq %r12, 24(%rdi)
    movq %rbp, 16(%rdi)
    movq %rsp, 8(%rdi)
    movq (%rsp), %rax
    movq %rax, 0(%rdi)

    /* Load EVERYTHING (entry included) before popfq, so the only
     * instruction after IF=1 is `jmp *%rax`: if the PIT fires in that
     * 1-instruction window, the saved frame is a fully-switched, consistent
     * state and re-running the jmp is idempotent. The old order
     * (popfq; mov entry; jmp) left a 2-instruction window where a preempt
     * captured a half-switched state and the later restore jmp'd to
     * garbage (observed #GP(0) at 0xff53f000, task0 RIP=0x104430 =
     * the `mov rax,[rsi]` slot). r11 is a caller-saved scratch reg;
     * the push/popfq pair below runs on the NEW stack and restores rsp. */
    movq 64(%rsi), %r11
    movq 0(%rsi), %rax
    movq 8(%rsi), %rsp
    movq 16(%rsi), %rbp
    movq 24(%rsi), %r12
    movq 32(%rsi), %r15
    movq 40(%rsi), %r14
    movq 48(%rsi), %r13
    movq 56(%rsi), %rbx
    pushq %r11
    popfq
    jmp *%rax

.globl context_switch_end
context_switch_end:

.globl proc_entry_wrapper
proc_entry_wrapper:
    movq (%rsp), %rdi
    call proc_entry
    movq $0, %rdi
    call proc_exit

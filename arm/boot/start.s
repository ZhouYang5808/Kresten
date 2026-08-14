.section .text
.globl _start
_start:
    // 关中断
    mrs r0, cpsr
    orr r0, r0, #0xC0
    msr cpsr, r0

    // 安装异常向量表到 0x0（16 words, 位置无关 ldr-pc 模式, 拷贝后可运行）
    ldr r0, =vectors
    ldr r1, =0x0
    mov r2, #16
vec_copy:
    ldr r3, [r0], #4
    str r3, [r1], #4
    subs r2, r2, #1
    bne vec_copy

    // 设置栈指针（link.ld 保留的 .stack 区域顶部, 16KB）
    ldr sp, =_stack_end

    // 清空 BSS
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
bss_loop:
    cmp r0, r1
    strcc r2, [r0], #4
    bcc bss_loop

    // 调用 C 的 kernel_main
    bl kernel_main

hang:
    b hang

/* ============ 异常向量表（拷贝到 0x0） ============
 * 位置无关: 每个槽 ldr pc,[pc,#24] 从表尾的 .word 文字池取地址,
 * 因此拷到 0x0 后仍指向原 .text 里的 handler。
 * 0x00 Reset  0x04 Undef  0x08 SVC  0x0C PrefetchAbort
 * 0x10 DataAbort  0x14 Reserved  0x18 IRQ  0x1C FIQ      */
vectors:
    ldr pc, [pc, #24]      /* 0x00 Reset           */
    ldr pc, [pc, #24]      /* 0x04 Undef           */
    ldr pc, [pc, #24]      /* 0x08 SVC (LX syscall)*/
    ldr pc, [pc, #24]      /* 0x0C Prefetch Abort  */
    ldr pc, [pc, #24]      /* 0x10 Data Abort      */
    ldr pc, [pc, #24]      /* 0x14 Reserved        */
    ldr pc, [pc, #24]      /* 0x18 IRQ             */
    ldr pc, [pc, #24]      /* 0x1C FIQ             */
    .word _start
    .word undef_handler
    .word svc_handler
    .word pabt_handler
    .word dabt_handler
    .word hang
    .word irq_handler
    .word hang

undef_handler:
    /* 模拟 guest(USR) 的 CP15 c13 访问: glibc 用 mcr p15,0,rt,c13,c0,3
     * 写 TLS 槽(ARM926 无 TPIDRURO)。帧布局与 svc_handler 相同。
     * 失败(非 mcr/mrc c13 或 rt>11)则挂起。 */
    ldr r12, =lx_save_sp
    str sp, [r12]
    ldr sp, =lx_trap_stack+8192
    stmdb sp!, {r0-r11, lr}
    sub sp, sp, #12
    ldr r0, [sp, #60]          // lr_undef (faulting insn 在 lr-4)
    mov r1, sp                 // 帧指针
    bl lx_undef_dispatch       // 返回新 lr; 0 = 无法模拟
    cmp r0, #0
    beq hang
    str r0, [sp, #60]
    add sp, sp, #12
    ldmia sp!, {r0-r11, lr}
    ldr r12, =lx_save_sp
    ldr sp, [r12]
    movs pc, lr

pabt_handler:
    b hang

dabt_handler:
    b hang

irq_handler:
    b hang

/* ============ svc 异常入口（LX guest syscall 通道） ============
 * 进入(ARM EABI): r7 = 调用号, r0..r5 = a1..a6(只用前 5),
 *                  lr_svc = 返回地址, spsr = guest CPSR(USR 模式)。
 * guest 运行在 USR 模式(无 MMU): svc 自陷到 SVC, r14_usr/r13_usr 不受影响。
 * 处理: 内核 sp 存 lx_save_sp → 切 lx_trap_stack(8KB, 8 对齐) →
 *       C dispatch → 恢复 -> movs pc, lr(还原 guest pc+cpsr)。
 * 见 lx_syscall.c: lx_save_sp / lx_trap_stack。 */
.globl svc_handler
svc_handler:
    ldr r12, =lx_save_sp
    str sp, [r12]              // 保存内核 sp
    ldr sp, =lx_trap_stack+8192  // 切陷阱栈(顶部, 8 对齐)
    stmdb sp!, {r0-r11, lr}    // 保存 12 regs + lr (52B)
    sub sp, sp, #12            // 对齐到 8(mod 8 = 4+12) + 2 栈参数槽
    // 帧: [sp+0]=a5槽 [sp+4]=a4槽 [sp+8]=pad
    //      [sp+12]=r0(原) [sp+16]=r1 ... [sp+40]=r7(num) [sp+60]=lr
    ldr r0, [sp, #40]          // num = r7
    ldr r1, [sp, #12]          // a1 = 原 r0
    ldr r2, [sp, #16]          // a2 = 原 r1
    ldr r3, [sp, #20]          // a3 = 原 r2
    ldr r4, [sp, #24]          // a4 = 原 r3
    ldr r5, [sp, #28]          // a5 = 原 r4
    str r4, [sp, #0]           // a4 入栈
    str r5, [sp, #4]           // a5 入栈
    bl linux_syscall_dispatch
    mov r12, r0                // 保留返回值 (r12 可被 guest 破坏)
    add sp, sp, #12
    ldmia sp!, {r0-r11, lr}    // 恢复全部 (r0 随后被返回值覆盖)
    mov r0, r12                // r0 = 返回值
    ldr r12, =lx_save_sp
    ldr sp, [r12]              // 恢复内核 sp
    movs pc, lr                // 返回 guest + 恢复 CPSR (spsr = USR)
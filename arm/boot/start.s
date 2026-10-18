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
    /* 崩溃守护已武装 (magic+state 检查): 未定义指令 = 非法指令崩溃, 交守护 */
    ldr r12, =0x7FF0000
    ldr r11, [r12]            /* magic */
    ldr r10, =0x31445247
    cmp r11, r10
    bne 1f                    /* 未武装: 走原 LX CP15 模拟 */
    ldr r11, [r12, #4]        /* state */
    cmp r11, #1
    bne 1f
    ldr r12, =0x7FF0120
    str r0, [r12]             /* 存原 r0 -> H_SNAP[0] */
    sub r0, lr, #4            /* 未定义指令地址 = lr - 4 */
    str r0, [r12, #-16]       /* H_PC = 0x110 */
    mrs r0, spsr
    str r0, [r12, #-8]        /* H_EXTRA = 0x118 */
    b grd_hook_common
1:  /* 模拟 guest(USR) 的 CP15 c13 访问: glibc 用 mcr p15,0,rt,c13,c0,3
     * 写 TLS 槽(ARM926 无 TPIDRURO)。帧布局与 svc_handler 相同。
     * 失败(非 mcr/mrc c13 或 rt>11)则挂起。
     * 实现经内核桥指针 lx_undef_dispatch 间接调用 (vm.kplg init 注册)。 */
    ldr r12, =lx_save_sp
    str sp, [r12]
    ldr sp, =lx_trap_stack+8192
    stmdb sp!, {r0-r11, lr}
    sub sp, sp, #12
    ldr r0, [sp, #60]          // lr_undef (faulting insn 在 lr-4)
    mov r1, sp                 // 帧指针
    ldr r12, =lx_undef_dispatch
    ldr r12, [r12]             // 函数指针 (未注册=0 -> 挂起)
    cmp r12, #0
    beq hang
    mov lr, pc
    mov pc, r12                // lx_undef_dispatch(lr_undef, frame) -> 新 lr
    cmp r0, #0
    beq hang
    str r0, [sp, #60]
    add sp, sp, #12
    ldmia sp!, {r0-r11, lr}
    ldr r12, =lx_save_sp
    ldr sp, [r12]
    movs pc, lr

/* ============ pabt/dabt: 崩溃守护 hook ============
 * 现场: r0-r12 完整 (除 r0/r12 暂存), sp/lr = 内核 SVC 栈/返回地址。
 * 先存 lr_abt/spsr_abt (banked, 切模式即丢) 到 hdr 槽 (区固定 0x7FF0000),
 * 再用 msr cpsr_c 直接切 SVC (不破坏寄存器), 现场压内核栈,
 * 填 hdr 后跳保留区报告入口 (region+0x500)。结尾恢复现场 r11/r12。 */
pabt_handler:
    ldr r12, =0x7FF0120
    str r0, [r12]              /* 存原 r0 -> H_SNAP[0] */
    sub r0, lr, #4             /* pabt: 故障 pc = lr - 4 */
    str r0, [r12, #-16]        /* H_PC = 0x120-0x10 = 0x110 */
    mrs r0, spsr
    str r0, [r12, #-8]         /* H_EXTRA = 0x118 */
    b grd_hook_common

dabt_handler:
    ldr r12, =0x7FF0120
    str r0, [r12]              /* 存原 r0 -> H_SNAP[0] */
    sub r0, lr, #8             /* dabt: 故障 pc = lr - 8 */
    str r0, [r12, #-16]        /* H_PC */
    mrs r0, spsr
    str r0, [r12, #-8]         /* H_EXTRA */
    b grd_hook_common

grd_hook_common:
    msr cpsr_c, #0xD3          /* -> SVC, IRQ/FIQ 关 (不破坏寄存器) */
    ldr r12, =0x7FF0120
    ldr r0, [r12]              /* 恢复原 r0 */
    stmdb sp!, {r0-r12}        /* 现场入内核栈 (52B); 之后 r0-r12 可破坏 */
    add r9, sp, #52            /* r9 = 故障时内核 sp (压栈前) */
    /* regs[0..12] = r0..r12 (8 字节槽, 高位 0) */
    mov r0, #13
    mov r12, sp
    ldr r10, =0x7FF0088
1:  ldr r11, [r12], #4
    str r11, [r10]
    mov r11, #0
    str r11, [r10, #4]
    add r10, r10, #8
    subs r0, r0, #1
    bne 1b
    /* regs[13]=sp, regs[14]=lr_svc, regs[15]=0 */
    ldr r10, =0x7FF00F0
    str r9, [r10]
    mov r11, #0
    str r11, [r10, #4]
    str lr, [r10, #8]
    str r11, [r10, #12]
    str r11, [r10, #16]
    str r11, [r10, #20]
    /* H_SP = 故障 sp; H_SNAP = 故障栈顶 8 词 (压栈向下, 原内容未破坏) */
    ldr r10, =0x7FF0108
    str r9, [r10]
    mov r11, #0
    str r11, [r10, #4]
    ldr r10, =0x7FF0120
    mov r12, r9
    mov r0, #8
2:  ldr r1, [r12], #4
    str r1, [r10]
    mov r1, #0
    str r1, [r10, #4]
    add r10, r10, #8
    subs r0, r0, #1
    bne 2b
    /* cause=1 + "UNHANDLED TRAP" */
    ldr r10, =0x7FF0008
    mov r1, #1
    str r1, [r10]
    ldr r10, =0x7FF0048
    ldr r1, =0x41484E55
    str r1, [r10]
    ldr r1, =0x454C444E
    str r1, [r10, #4]
    ldr r1, =0x52542044
    str r1, [r10, #8]
    ldr r1, =0x00005041
    str r1, [r10, #12]
    /* 恢复现场 r11/r12, 跳报告 (r0 = region 基址) */
    ldr r11, [sp, #44]
    ldr r12, [sp, #48]
    ldr r0, =0x7FF0000
    ldr r12, =0x7FF0500
    mov pc, r12

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
    ldr r12, =linux_syscall_dispatch
    ldr r12, [r12]             // 函数指针 (vm.kplg init 注册; 未注册=0 -> 挂起)
    cmp r12, #0
    beq hang
    mov lr, pc
    mov pc, r12                // linux_syscall_dispatch(num, a1..a5)
    mov r12, r0                // 保留返回值 (r12 可被 guest 破坏)
    add sp, sp, #12
    ldmia sp!, {r0-r11, lr}    // 恢复全部 (r0 随后被返回值覆盖)
    mov r0, r12                // r0 = 返回值
    ldr r12, =lx_save_sp
    ldr sp, [r12]              // 恢复内核 sp
    movs pc, lr                // 返回 guest + 恢复 CPSR (spsr = USR)
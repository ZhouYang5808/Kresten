/* ===== boot/start.S (RISC-V RV64): M-mode 启动 + U-mode 访客陷阱 =====
 * QEMU virt, -bios none: 复位后以 M-mode 从 0x80000000 开始。
 * 访客(LX busybox)以 U-mode 运行: ecall 自陷到 M-mode trap_entry,
 * 保存全部 GPR 到 lx_trap_stack 帧, C 分发(lx_syscall.c), 恢复后 mret。
 * 帧布局(8 字节槽):
 *   [0]=ra [8]=gp [16]=tp [24..232]=t0..t6/s0..s11/a0..a7 (共 30 个)
 *   [240]=访客 sp  [248]=mepc  [256]=mstatus   FRAME_SIZE=264
 * 偏移: a0=64 a1=72 a2=80 a3=88 a4=96 a5=104 a6=112 a7=120
 */
.section .text
.globl _start
_start:
    csrr t0, mhartid
    bnez t0, .Lwait          /* 只让 hart0 跑 */
    la sp, _stack_end        /* 内核栈 (link.ld .stack) */
    la t0, _bss_start
    la t1, _bss_end
.Lbss:
    bgeu t0, t1, .Lbss_done
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lbss
.Lbss_done:
    /* 允许浮点 + 向量: mstatus.FS=01 (Initial), VS=01 (Initial)
     * — 访客(riscv64gc 工具链默认含 v) 会执行向量指令, VS=0 会非法 */
    csrr t0, mstatus
    li t1, 0x6600
    or t0, t0, t1
    csrw mstatus, t0
    /* 陷阱向量 */
    la t0, trap_entry
    csrw mtvec, t0
    /* PMP: 允许 U-mode 访客访问全部内存 (NAPOT 单条, 覆盖 2TB) */
    li t0, 0x1FFFFFFFFF
    csrw pmpaddr0, t0
    li t0, 0x1F
    csrw pmpcfg0, t0
    call kernel_main
.Lwait:
    wfi
    j .Lwait

/* ============ 陷阱入口 ============
 * 注意: 必须 4 字节对齐 (mtvec 低 2 位是 MODE, 非零 = vectored 模式)。
 * 先保存全部 GPR (t5/t6 最后, 因为取 mcause 要破坏它们), 再分派。 */
.globl trap_entry
.balign 16
trap_entry:
    csrr t6, mscratch          /* t6 = 旧 mscratch, 马上被覆盖但无害 */
    csrw mscratch, sp          /* mscratch = 访客 sp */
    la sp, lx_trap_stack_end
    addi sp, sp, -264
    /* 保存 GPR x1..x31 (x0 不用); t6 槽存旧 mscratch */
    sd ra,   0(sp)
    sd gp,   8(sp)
    sd tp,  16(sp)
    sd t0,  24(sp)
    sd t1,  32(sp)
    sd t2,  40(sp)
    sd s0,  48(sp)
    sd s1,  56(sp)
    sd a0,  64(sp)
    sd a1,  72(sp)
    sd a2,  80(sp)
    sd a3,  88(sp)
    sd a4,  96(sp)
    sd a5, 104(sp)
    sd a6, 112(sp)
    sd a7, 120(sp)
    sd s2, 128(sp)
    sd s3, 136(sp)
    sd s4, 144(sp)
    sd s5, 152(sp)
    sd s6, 160(sp)
    sd s7, 168(sp)
    sd s8, 176(sp)
    sd s9, 184(sp)
    sd s10,192(sp)
    sd s11,200(sp)
    sd t3, 208(sp)
    sd t4, 216(sp)
    sd t5, 224(sp)
    sd t6, 232(sp)
    csrr t0, mscratch
    sd t0, 240(sp)             /* 访客 sp */
    csrr t0, mepc
    sd t0, 248(sp)
    csrr t0, mstatus
    sd t0, 256(sp)
    /* 现在可以安全破坏 t5/t6 分派 */
    csrr t6, mcause
    li t5, 8                   /* ecall from U-mode (LX syscall) */
    beq t6, t5, .Lsvc
    li t5, 3                   /* ebreak (glibc _start 尾部) -> exit */
    beq t6, t5, .Lsvc
    li   t5, 2                   /* illegal instruction -> 打印 + 挂起 */
    beq  t6, t5, .Ldie
    /* 其他异常: 打印 mcause/mepc/mstatus 后挂起 */
.Ldie:
    /* ---- 崩溃守护 hook: 若已武装, 填充现场并转 guardian 报告 ----
     * 保留区 0x87FF0000: magic 0x31445247, report 入口 +0x500。
     * 常量不得用 li 32 位符号扩展 (会变成 0xFFFFFFFF87FF0000 越界地址),
     * 用 lui + slli + srli 构造正的 64 位值。 */
    lui  t1, 0x87FF0
    slli t1, t1, 32
    srli t1, t1, 32
    lw   t2, 0(t1)
    li   t3, 0x31445247
    bne  t2, t3, .Ldie_nog
    /* 拷贝 16 个寄存器 (trap 帧, 顺序同 guard.h regs[16]) */
    addi t2, t1, 0x88
    mv   t3, sp
    li   t4, 16
1:  ld   t5, 0(t3)
    sd   t5, 0(t2)
    addi t3, t3, 8
    addi t2, t2, 8
    addi t4, t4, -1
    bnez t4, 1b
    ld   t5, 240(sp)              /* 访客 sp */
    sd   t5, 0x108(t1)
    ld   t5, 248(sp)              /* mepc */
    sd   t5, 0x110(t1)
    ld   t5, 256(sp)              /* mstatus */
    sd   t5, 0x118(t1)
    /* 栈快照: 访客 sp 起 8 个词 */
    ld   t3, 240(sp)
    addi t2, t1, 0x120
    li   t4, 8
2:  ld   t5, 0(t3)
    sd   t5, 0(t2)
    addi t3, t3, 8
    addi t2, t2, 8
    addi t4, t4, -1
    bnez t4, 2b
    /* cause=1 (EXCEPTION), cause_str="UNHANDLED TRAP" (sw 连续, 无 NUL 夹断) */
    li   t2, 1
    sw   t2, 0x08(t1)
    li   t2, 0x41484E55           /* "UNHA" */
    sw   t2, 0x48(t1)
    li   t2, 0x454C444E           /* "NDLE" */
    sw   t2, 0x4C(t1)
    li   t2, 0x52542044           /* "D TR" */
    sw   t2, 0x50(t1)
    li   t2, 0x00005041           /* "AP\0" */
    sw   t2, 0x54(t1)
    /* 转 guardian 报告 (不返回); 入口约定 a0=region */
    mv   a0, t1
    li   t0, 0x500
    add  t0, t0, t1
    jalr zero, t0, 0
.Ldie_nog:
    csrr a0, mcause
    csrr a1, mepc
    csrr a2, mstatus
    /* 实现经内核桥指针 trap_unhandled 间接调用 (vm.kplg init 注册; 未注册挂起) */
    la   t6, trap_unhandled
    ld   t6, 0(t6)
    beqz t6, 1f
    jalr ra, t6, 0
1:  wfi
    j 1b

.Lsvc:
    /* C 分发: a0=num(访客 a7) a1..a5=访客 a0..a4。
     * ebreak (mcause=3) 是 glibc _start 尾部的退出陷阱: a7 无意义,
     * 强制 num=94 (exit_group)。 */
    csrr t5, mcause
    li t6, 3
    bne t5, t6, 1f
    li t6, 94
    sd t6, 120(sp)
1:
    ld a0, 120(sp)
    ld a1,  64(sp)
    ld a2,  72(sp)
    ld a3,  80(sp)
    ld a4,  88(sp)
    ld a5,  96(sp)
    /* 实现经内核桥指针 linux_syscall_dispatch 间接调用 (vm.kplg init 注册) */
    la   t6, linux_syscall_dispatch
    ld   t6, 0(t6)
    beqz t6, .Ldie
    jalr ra, t6, 0
    sd a0,  64(sp)             /* 返回值写入访客 a0 */
    /* mepc += 4 (ecall 32 位; ebreak 是 16 位但走 exit 路径不会回来),
     * 并立即写回 CSR (恢复路径从帧取)。 */
    ld t0, 248(sp)
    addi t0, t0, 4
    sd t0, 248(sp)
    csrw mepc, t0
    /* 恢复访客 mstatus: 帧里已含 HW 写入的 MPP=U/MPIE=1 */
    ld t1, 256(sp)
    csrw mstatus, t1
    /* 恢复 GPR (t0 从帧还原, 尾段用 t1 作临时) */
    ld ra,   0(sp)
    ld gp,   8(sp)
    ld tp,  16(sp)
    ld t0,  24(sp)
    ld t2,  40(sp)
    ld s0,  48(sp)
    ld s1,  56(sp)
    ld a0,  64(sp)
    ld a1,  72(sp)
    ld a2,  80(sp)
    ld a3,  88(sp)
    ld a4,  96(sp)
    ld a5, 104(sp)
    ld a6, 112(sp)
    ld a7, 120(sp)
    ld s2, 128(sp)
    ld s3, 136(sp)
    ld s4, 144(sp)
    ld s5, 152(sp)
    ld s6, 160(sp)
    ld s7, 168(sp)
    ld s8, 176(sp)
    ld s9, 184(sp)
    ld s10,192(sp)
    ld s11,200(sp)
    ld t3, 208(sp)
    ld t4, 216(sp)
    ld t5, 224(sp)
    ld t6, 232(sp)
    ld t1, 248(sp)
    csrw mepc, t1
    ld t1,  32(sp)             /* 访客 t1: 必须在 mepc 写回之后还原, 否则 ecall 会破坏访客 t1 */
    ld sp, 240(sp)             /* 访客 sp (最后换) */
    mret

/* ============ LX 蹦床: 进入访客 (U-mode) ============
 * a0=entry a1=访客栈 a2=argc a3=argv。
 * 保存内核 s0..s11/sp/ra 到 lx_resume_* (exit_group 恢复用),
 * 设置 mstatus(MPP=U, MPIE=1, FS=1), mepc=entry, 清访客寄存器, mret。 */
.globl lx_jump_to_entry
.balign 16
lx_jump_to_entry:
    la t6, lx_resume_save
    sd s0,   0(t6)
    sd s1,   8(t6)
    sd s2,  16(t6)
    sd s3,  24(t6)
    sd s4,  32(t6)
    sd s5,  40(t6)
    sd s6,  48(t6)
    sd s7,  56(t6)
    sd s8,  64(t6)
    sd s9,  72(t6)
    sd s10, 80(t6)
    sd s11, 88(t6)
    la t6, lx_resume_sp
    sd sp, 0(t6)
    la t6, lx_resume_ra
    sd ra, 0(t6)
    /* 切 U 模式 */
    mv t0, a0                /* entry */
    csrr t6, mstatus
    li t1, 0x1800
    not t1, t1
    and t6, t6, t1           /* MPP=0 (U) */
    ori t6, t6, 0x80         /* MPIE=1 */
    li t1, 0x6600
    or t6, t6, t1            /* FS=1 + VS=1 */
    csrw mstatus, t6
    csrw mepc, t0
    mv sp, a1                /* 访客栈 */
    mv a0, a2                /* argc */
    mv a1, a3                /* argv */
    li a2, 0
    li a3, 0
    li a4, 0
    li a5, 0
    li a6, 0
    li a7, 0
    li t0, 0
    li t1, 0
    li t2, 0
    li t3, 0
    li t4, 0
    li t5, 0
    li t6, 0
    li gp, 0
    li tp, 0
    li s1, 0
    li s2, 0
    li s3, 0
    li s4, 0
    li s5, 0
    li s6, 0
    li s7, 0
    li s8, 0
    li s9, 0
    li s10,0
    li s11,0
    li ra, 0
    mret

/* ============ LX 恢复: exit_group -> 回内核 lx_run_test 恢复点 ============
 * 从 lx_resume_* 还原内核 sp/s0..s11/ra, 跳 lx_resume_pc。
 * 访客会改写全部 GPR(尤其 s0=fp), 必须全部还原。 */
.globl lx_resume_helper
.balign 16
lx_resume_helper:
    la t6, lx_resume_save
    ld s0,   0(t6)
    ld s1,   8(t6)
    ld s2,  16(t6)
    ld s3,  24(t6)
    ld s4,  32(t6)
    ld s5,  40(t6)
    ld s6,  48(t6)
    ld s7,  56(t6)
    ld s8,  64(t6)
    ld s9,  72(t6)
    ld s10, 80(t6)
    ld s11, 88(t6)
    la t6, lx_resume_sp
    ld sp, 0(t6)
    la t6, lx_resume_ra
    ld ra, 0(t6)
    la t6, lx_resume_pc
    ld t6, 0(t6)
    jr t6

/* ============ 软重启 ============ */
.globl lx_sys_reboot
.balign 16
lx_sys_reboot:
    la t0, _start
    jr t0
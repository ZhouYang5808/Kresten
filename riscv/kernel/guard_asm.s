/* ===== kernel/guard_asm.s: 崩溃守护 blob (RISC-V RV64, 纯汇编, PIC) =====
 * 与内核完全无关的独立汇编系统。内核启动时被 guard_init() memcpy 到
 * 保留区 (0x87FF0000, 64KB), 之后通过固定偏移 (guard.h GRD_*_OFF) 调用:
 *   grd_arm     武装 (初始化 hdr, 设看门狗超时)
 *   grd_panic   主动上报入口 (glue 已填 cause/cause_str) -> 转 report
 *   grd_kick    心跳: deadline = now + timeout
 *   grd_watch   看门狗: now > deadline -> 转 report (cause=WATCHDOG)
 *   grd_disarm  自毁: 把 tail 拷到内核 .data, 跳过去抹除整个保留区后回内核
 *   grd_report  崩溃转储: 切到自己栈, 串口完整报告 + 屏幕红带/CRASH 字形, 停机
 * 所有代码位置无关 (仅用 a0 基址 + 相对偏移), 从保留区运行时也正确。
 * 约定: 入口 a0=region; helpers clobber a0/a1/t0-t2, 保存 ra。 */
.section .rodata.guard, "a"
.balign 16
.globl guard_blob_start
guard_blob_start:

/* ---- 常量 (与 include/guard.h 一致) ---- */
.set H_MAGIC, 0x00
.set H_STATE, 0x04
.set H_CAUSE, 0x08
.set H_FLAGS, 0x0C
.set H_HEART, 0x10
.set H_DEADL, 0x18
.set H_TIMEO, 0x20
.set H_ARMED, 0x28
.set H_SERIA, 0x30
.set H_SCREE, 0x38
.set H_SFMT,  0x40
.set H_ARCH,  0x44
.set H_CSTR,  0x48
.set H_REGS,  0x88
.set H_SP,    0x108
.set H_PC,    0x110
.set H_EXTRA, 0x118
.set H_SNAP,  0x120
.set GRD_MAG, 0x31445247
.set GRD_TIMEOUT, 5000
.set GRD_WATCH_CAUSE, 3
.set UART,    0x10000000
.set REPORT_OFF, 0x500
.set DATA_OFF,   0x800
.set TAIL_OFF,   0xB00
.set TAIL_LEN,   64

.option norvc
.option norelax

/* ================= grd_arm @ +0x200 =================
 * a0=region, a1=now_ms -> a0=0 */
.org 0x200
.globl grd_arm
grd_arm:
    li   t0, GRD_MAG
    sw   t0, H_MAGIC(a0)
    li   t0, 1
    sw   t0, H_STATE(a0)
    sw   zero, H_CAUSE(a0)
    sw   zero, H_FLAGS(a0)
    sd   zero, H_HEART(a0)
    sd   zero, H_DEADL(a0)
    li   t0, GRD_TIMEOUT
    sw   t0, H_TIMEO(a0)
    sd   a1, H_ARMED(a0)
    li   a0, 0
    ret

/* ================= grd_panic @ +0x240 =================
 * a0=region -> 转 report, 不返回 */
.org 0x240
.globl grd_panic
grd_panic:
    li   t0, REPORT_OFF
    add  t0, t0, a0
    jalr zero, t0, 0

/* ================= grd_kick @ +0x280 =================
 * a0=region, a1=now_ms: 心跳, deadline = now + timeout */
.org 0x280
.globl grd_kick
grd_kick:
    ld   t0, H_HEART(a0)
    addi t0, t0, 1
    sd   t0, H_HEART(a0)
    lw   t0, H_TIMEO(a0)
    add  t0, t0, a1
    sd   t0, H_DEADL(a0)
    li   a0, 0
    ret

/* ================= grd_watch @ +0x2C0 =================
 * a0=region, a1=now_ms -> a0=0 正常; 超时 -> 转 report (不返回) */
.org 0x2C0
.globl grd_watch
grd_watch:
    ld   t0, H_DEADL(a0)
    beqz t0, .Lwatch_ok
    bge  t0, a1, .Lwatch_ok
    li   t0, GRD_WATCH_CAUSE
    sw   t0, H_CAUSE(a0)
    li   t0, 0x43544157          /* "WATC" */
    sw   t0, H_CSTR(a0)
    li   t0, 0x474F4448          /* "HDOG" */
    sw   t0, H_CSTR+4(a0)
    li   t0, 0x4D495420          /* " TIM" */
    sw   t0, H_CSTR+8(a0)
    li   t0, 0x54554F45          /* "EOUT" */
    sw   t0, H_CSTR+12(a0)
    li   t0, 0x54                /* "T\0" */
    sw   t0, H_CSTR+16(a0)
    li   t0, REPORT_OFF
    add  t0, t0, a0
    jalr zero, t0, 0
.Lwatch_ok:
    li   a0, 0
    ret

/* ================= grd_disarm @ +0x340 =================
 * a0=region a1=ksp a2=tail_dst a3=after: 拷贝 tail 到内核 .data 并跳转,
 * tail 抹除整个保留区后 jr a3 (不返回)。 */
.org 0x340
.globl grd_disarm
grd_disarm:
    li   t0, TAIL_OFF
    add  t0, t0, a0
    mv   t1, a2
    li   t2, TAIL_LEN
1:  lbu  t3, 0(t0)
    sb   t3, 0(t1)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, -1
    bnez t2, 1b
    jr   a2

/* ================= 内部 helpers (0x380+, leaf) ================= */
/* putc: a0=char (UART 16550 @0x10000000, LSR bit5) */
.balign 4
grd_putc:
    li   t0, UART
    li   t1, 0x20
1:  lbu  t2, 5(t0)
    and  t2, t2, t1
    beqz t2, 1b
    sb   a0, 0(t0)
    ret

/* putstr: a0=ptr */
grd_putstr:
    mv   t1, a0
1:  lbu  a0, 0(t1)
    beqz a0, 2f
    addi t1, t1, 1
    addi sp, sp, -16
    sd   t1, 0(sp)
    sd   ra, 8(sp)
    jal  grd_putc
    ld   t1, 0(sp)
    ld   ra, 8(sp)
    addi sp, sp, 16
    j    1b
2:  ret

/* nl: \r\n */
grd_nl:
    addi sp, sp, -16
    sd   ra, 0(sp)
    li   a0, '\r'
    jal  grd_putc
    li   a0, '\n'
    jal  grd_putc
    ld   ra, 0(sp)
    addi sp, sp, 16
    ret

/* puthex: a0=value -> "0x" + 16 hex 数字 */
grd_puthex:
    addi sp, sp, -40
    sd   ra, 32(sp)
    li   t1, 16
    mv   t2, sp
1:  andi t3, a0, 0xF
    li   t4, 10
    blt  t3, t4, 2f
    addi t3, t3, 55
    j    3f
2:  addi t3, t3, 48
3:  sb   t3, 0(t2)
    addi t2, t2, 1
    srli a0, a0, 4
    addi t1, t1, -1
    bnez t1, 1b
    li   a0, '0'
    jal  grd_putc
    li   a0, 'x'
    jal  grd_putc
    /* 反向打印 16 位数字 (sp+15 最高位起); 计数器用 t6
     * (putc 只破坏 t0/t1/t2), 地址每次重算 (t2 会被 putc 破坏) */
    li   t6, 16
4:  addi t6, t6, -1
    add  t2, sp, t6
    lbu  a0, 0(t2)
    jal  grd_putc
    bnez t6, 4b
    ld   ra, 32(sp)
    addi sp, sp, 40
    ret

/* ================= grd_report @ +0x500 =================
 * a0=region: 关中断 (防止 timer 等重新进入钩子), 切到自身栈,
 * 串口转储 + 屏幕红带, 停机 (不返回) */
.org 0x500
.globl grd_report
grd_report:
    csrw mie, zero
    li   t0, 0xF000
    add  sp, a0, t0
    addi sp, sp, -64
    sd   ra, 56(sp)
    sd   s0, 48(sp)
    sd   s1, 40(sp)
    sd   s2, 32(sp)
    sd   s3, 24(sp)
    sd   s4, 16(sp)
    mv   s0, a0
    li   t0, DATA_OFF
    add  s1, s0, t0            /* data 基址 */

    addi a0, s1, 0x000
    jal  grd_putstr
    addi a0, s1, 0x040
    jal  grd_putstr
    addi a0, s1, 0x060
    jal  grd_putstr
    lw   t0, H_CAUSE(s0)
    li   t1, 1
    beq  t0, t1, .Lc_exc
    li   t1, 2
    beq  t0, t1, .Lc_panic
    addi a0, s1, 0x0C0
    j    .Lc_done
.Lc_exc:
    addi a0, s1, 0x080
    j    .Lc_done
.Lc_panic:
    addi a0, s1, 0x0A0
.Lc_done:
    jal  grd_putstr
    addi a0, s1, 0x0E0
    jal  grd_putstr
    addi a0, s0, H_CSTR
    jal  grd_putstr
    addi a0, s1, 0x100
    jal  grd_putstr
    ld   a0, H_ARMED(s0)
    jal  grd_puthex
    addi a0, s1, 0x120
    jal  grd_putstr

    /* 16 个寄存器: name=0x... (循环状态用 s3/s4 — puthex 破坏 t3/t4) */
    li   s3, H_REGS
    li   s4, 16
.Lregs:
    mv   a0, s3
    addi a0, a0, -H_REGS
    srli a0, a0, 1
    add  a0, a0, s1
    addi a0, a0, 0x1E8
    jal  grd_putstr
    li   a0, '='
    jal  grd_putc
    add  t0, s0, s3
    ld   a0, 0(t0)
    jal  grd_puthex
    jal  grd_nl
    addi s3, s3, 8
    addi s4, s4, -1
    bnez s4, .Lregs

    addi a0, s1, 0x140
    jal  grd_putstr
    ld   a0, H_SP(s0)
    jal  grd_puthex
    addi a0, s1, 0x160
    jal  grd_putstr
    ld   a0, H_PC(s0)
    jal  grd_puthex
    addi a0, s1, 0x180
    jal  grd_putstr
    ld   a0, H_EXTRA(s0)
    jal  grd_puthex
    jal  grd_nl

    addi a0, s1, 0x1A0
    jal  grd_putstr
    li   s3, H_SNAP
    li   s4, 8
.Lsnap:
    li   a0, ' '
    jal  grd_putc
    add  t0, s0, s3
    ld   a0, 0(t0)
    jal  grd_puthex
    addi s3, s3, 8
    addi s4, s4, -1
    bnez s4, .Lsnap
    jal  grd_nl

    /* 屏幕: RGB565 红带 + "CRASH!" 白字形 (无帧缓冲则跳过) */
    ld   s2, H_SCREE(s0)
    beqz s2, .Lno_screen
    lw   t1, H_SFMT(s0)
    li   t2, 3
    bne  t1, t2, .Lno_screen
    /* 顶部 32 行红色 0xF800 (640x480, stride 1280) */
    li   t3, 32
    li   t6, 0xF800
    mv   t4, s2
.Lrowf:
    li   t5, 640
1:  sh   t6, 0(t4)
    addi t4, t4, 2
    addi t5, t5, -1
    bnez t5, 1b
    addi t3, t3, -1
    bnez t3, .Lrowf
    /* "CRASH!" 字形: x=8.., y=40..46 */
    li   t5, 0
.Lchar:
    add  t0, s1, t5
    lbu  a0, 0x228(t0)
    /* 字形索引: 'A'-'Z' -> +10; 其他 -> 36 ('!') */
    li   t1, 65
    blt  a0, t1, .Lidx_bang
    li   t2, 90
    bgt  a0, t2, .Lidx_bang
    addi a0, a0, -55
    j    .Lidx_done
.Lidx_bang:
    li   a0, 36
.Lidx_done:
    li   t1, 5
    mul  a0, a0, t1
    add  a0, a0, s1
    addi a0, a0, 0x240         /* 字形字节表 */
    /* 像素地址 = s2 + y*1280 + x*2; 字符位置 (8 + t5*6, 40) */
    li   t3, 0
.Lrow7:
    li   t4, 0
.Lcol5:
    add  t0, a0, t4
    lbu  t6, 0(t0)
    srl  t6, t6, t3
    andi t6, t6, 1
    beqz t6, .Lpix_red
    li   t6, 0xFFFF
    j    .Lpix_go
.Lpix_red:
    li   t6, 0xF800
.Lpix_go:
    li   t0, 1280
    mul  t0, t3, t0
    add  t0, t0, s2
    li   t1, 51200
    add  t0, t0, t1
    li   t1, 12
    mul  t1, t5, t1
    add  t0, t0, t1
    li   t1, 16
    add  t0, t0, t1
    li   t1, 2
    mul  t1, t4, t1
    add  t0, t0, t1
    sh   t6, 0(t0)
    addi t4, t4, 1
    li   t1, 5
    blt  t4, t1, .Lcol5
    addi t3, t3, 1
    li   t1, 7
    blt  t3, t1, .Lrow7
    addi t5, t5, 1
    li   t1, 6
    blt  t5, t1, .Lchar
.Lno_screen:
    addi a0, s1, 0x1C0
    jal  grd_putstr
.Lhalt:
    wfi
    j    .Lhalt

/* ================= 数据区 @ +0x800 =================
 * 字符串/表按固定偏移放置, S_* = 相对 DATA_OFF (0x800) 的纯数值常量,
 * 引用处立即数直接折叠 (RISC-V GAS 不做标签差折叠)。 */
.org 0x800
.Lb_head:  .asciz "\r\n========== GUARDIAN CRASH REPORT ==========\r\n"   /* 0x000, 57B */
.org 0x840
.Lb_arch:  .asciz "arch   : riscv64 (RV64)\r\n"                          /* 0x040 */
.org 0x860
.Lb_cause: .asciz "cause  : "                                            /* 0x060 */
.org 0x880
.Lb_exc:   .asciz "EXCEPTION\r\n"                                        /* 0x080 */
.org 0x8A0
.Lb_panic: .asciz "KERNEL PANIC\r\n"                                     /* 0x0A0 */
.org 0x8C0
.Lb_watch: .asciz "WATCHDOG TIMEOUT\r\n"                                 /* 0x0C0 */
.org 0x8E0
.Lb_det:   .asciz "detail : "                                            /* 0x0E0 */
.org 0x900
.Lb_armed: .asciz "\r\narmed  : "                                          /* 0x100 */
.org 0x920
.Lb_regs:  .asciz "\r\nregs   :\r\n"                                     /* 0x120 */
.org 0x940
.Lb_sp:    .asciz "\r\nsp     : "                                          /* 0x140 */
.org 0x960
.Lb_pc:    .asciz "\r\npc     : "                                          /* 0x160 */
.org 0x980
.Lb_extra: .asciz "\r\nmstatus: "                                          /* 0x180 */
.org 0x9A0
.Lb_snap:  .asciz "\r\nstack  :"                                         /* 0x1A0 */
.org 0x9C0
.Lb_halt:  .asciz "\r\n\r\n*** system halted by guardian ***\r\n"        /* 0x1C0, 40B */
.org 0x9E8
.Lnames:
    .byte 'r','a',0,0                                                    /* 0x1E8 */
    .byte 'g','p',0,0
    .byte 't','p',0,0
    .byte 't','0',0,0
    .byte 't','1',0,0
    .byte 't','2',0,0
    .byte 's','0',0,0
    .byte 's','1',0,0
    .byte 'a','0',0,0
    .byte 'a','1',0,0
    .byte 'a','2',0,0
    .byte 'a','3',0,0
    .byte 'a','4',0,0
    .byte 'a','5',0,0
    .byte 'a','6',0,0
    .byte 'a','7',0,0
.org 0xA28
.Ltxt: .asciz "CRASH!"                                                   /* 0x228 */
.org 0xA40
/* 5x7 字形 (37 项, 列字节, bit0..6 = 行 0..6) */                        /* 0x240 */
.Lfont:
    .byte 0x00,0x00,0x00,0x00,0x00   /* 0  (未用) */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 1  */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 2  */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 3  */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 4  */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 5  */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 6  */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 7  */
    .byte 0x7F,0x08,0x08,0x08,0x7F   /* 8  H */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 9  */
    .byte 0x7E,0x09,0x09,0x09,0x7E   /* 10 A */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 11 */
    .byte 0x3E,0x41,0x41,0x41,0x3E   /* 12 C */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 13 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 14 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 15 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 16 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 17 */
    .byte 0x7F,0x09,0x39,0x49,0x46   /* 18 R */
    .byte 0x46,0x09,0x09,0x49,0x37   /* 19 S */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 20 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 21 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 22 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 23 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 24 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 25 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 26 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 27 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 28 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 29 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 30 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 31 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 32 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 33 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 34 */
    .byte 0x00,0x00,0x00,0x00,0x00   /* 35 */
    .byte 0x00,0x5F,0x00,0x00,0x00   /* 36 ! */

.set S_HEAD,  0x000
.set S_ARCH,  0x040
.set S_CAUSE, 0x060
.set S_EXC,   0x080
.set S_PANIC, 0x0A0
.set S_WATCH, 0x0C0
.set S_DET,   0x0E0
.set S_ARMED, 0x100
.set S_REGS,  0x120
.set S_SP,    0x140
.set S_PC,    0x160
.set S_EXTRA, 0x180
.set S_SNAP,  0x1A0
.set S_HALT,  0x1C0
.set S_NAMES, 0x1E8
.set S_TXT,   0x228
.set S_FONT,  0x240

/* ================= tail @ +0xB00 (拷到内核 .data 后执行) =================
 * a0=region a1=ksp a3=after: 恢复内核栈, 抹除整个保留区, jr a3。 */
.org 0xB00
.globl guard_tail
guard_tail:
    mv   sp, a1
    mv   t1, a0
    li   t2, 0x10000
    add  t3, t1, t2
1:  sd   zero, 0(t1)
    addi t1, t1, 8
    bltu t1, t3, 1b
    jr   a3

.globl guard_blob_end
guard_blob_end:

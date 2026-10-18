/* ===== kernel/guard_asm.s: 崩溃守护 blob (ARM32, 纯汇编, PIC) =====
 * 与内核完全无关的独立汇编系统。内核启动时被 guard_init() memcpy 到
 * 保留区 (0x7FF0000, 64KB), 之后通过固定偏移 (guard.h GRD_*_OFF) 调用:
 *   grd_arm     武装 (初始化 hdr, 设看门狗超时, 存帧缓冲地址)
 *   grd_panic   主动上报入口 (glue 已填 cause/cause_str) -> 转 report
 *   grd_kick    心跳: deadline = now + timeout
 *   grd_watch   看门狗: now > deadline -> 转 report (cause=WATCHDOG)
 *   grd_disarm  自毁: 把 tail 拷到内核 .data, 跳过去抹除整个保留区后回内核
 *   grd_report  崩溃转储: 切到自己栈, PL011 完整报告 + PL110 红带/CRASH 字形, 停机
 * 所有代码位置无关 (仅用 r0 基址 + 相对偏移/PC 相对字面池), 从保留区运行时也正确。
 * 约定: 入口 r0=region; helpers 只破坏 r0-r4 (puthex64 额外压栈 r4), 保存 lr;
 * 报告主体状态在 r4-r11 (callee-saved)。ARM 模式 (无 Thumb), armv5te。
 * 字面池: ldr rN, =const 的常量池经 .ltorg 显式放在 grd_disarm 后与
 * grd_putc 后 (必须在本节内, 保证从保留区运行时 PC 相对偏移指向 blob 拷贝),
 * 且不能越过后续 .org。 */
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
.set UART,    0x101f1000        /* PL011 */
.set REPORT_OFF, 0x500
.set DATA_OFF,   0x800
.set TAIL_OFF,   0xB00
.set TAIL_LEN,   64

/* ================= grd_arm @ +0x200 =================
 * r0=region, r1=now_ms, r2=fb, r3=fmt -> r0=0 (20 条 = 0x50, 0x200+0x50=0x250) */
.org 0x200
.globl grd_arm
grd_arm:
    ldr r4, =GRD_MAG
    str r4, [r0, #H_MAGIC]
    mov r4, #1
    str r4, [r0, #H_STATE]
    mov r4, #0
    str r4, [r0, #H_CAUSE]
    str r4, [r0, #H_FLAGS]
    str r4, [r0, #H_HEART]
    str r4, [r0, #H_HEART+4]
    str r4, [r0, #H_DEADL]
    str r4, [r0, #H_DEADL+4]
    ldr r4, =GRD_TIMEOUT
    str r4, [r0, #H_TIMEO]
    str r1, [r0, #H_ARMED]
    mov r4, #0
    str r4, [r0, #H_ARMED+4]
    str r2, [r0, #H_SCREE]
    str r3, [r0, #H_SFMT]
    mov r0, #0
    mov pc, lr

/* ================= grd_panic @ +0x250 =================
 * r0=region -> 转 report, 不返回 */
.org 0x250
.globl grd_panic
grd_panic:
    add r0, r0, #REPORT_OFF
    mov pc, r0

/* ================= grd_kick @ +0x270 =================
 * r0=region, r1=now_ms: 心跳, deadline = now + timeout */
.org 0x270
.globl grd_kick
grd_kick:
    ldr r4, [r0, #H_HEART]
    add r4, r4, #1
    str r4, [r0, #H_HEART]
    ldr r4, [r0, #H_TIMEO]
    add r4, r4, r1
    str r4, [r0, #H_DEADL]
    mov r0, #0
    mov pc, lr

/* ================= grd_watch @ +0x2A0 =================
 * r0=region, r1=now_ms -> r0=0 正常; 超时 -> 转 report (不返回) */
.org 0x2A0
.globl grd_watch
grd_watch:
    ldr r4, [r0, #H_DEADL]
    cmp r4, #0
    beq .Lwatch_ok
    cmp r4, r1
    bge .Lwatch_ok
    mov r4, #GRD_WATCH_CAUSE
    str r4, [r0, #H_CAUSE]
    ldr r4, =0x43544157          /* "WATC" */
    str r4, [r0, #H_CSTR]
    ldr r4, =0x474F4448          /* "HDOG" */
    str r4, [r0, #H_CSTR+4]
    ldr r4, =0x4D495420          /* " TIM" */
    str r4, [r0, #H_CSTR+8]
    ldr r4, =0x54554F45          /* "EOUT" */
    str r4, [r0, #H_CSTR+12]
    mov r4, #0x54                /* "T\0\0\0" */
    str r4, [r0, #H_CSTR+16]
    add r0, r0, #REPORT_OFF
    mov pc, r0
.Lwatch_ok:
    mov r0, #0
    mov pc, lr

/* ================= grd_disarm @ +0x340 =================
 * r0=region r1=ksp r2=tail_dst r3=after: 拷贝 tail 到内核 .data 并跳转,
 * tail 抹除整个保留区后 jr a3 (不返回)。 */
.org 0x340
.globl grd_disarm
grd_disarm:
    add r4, r0, #TAIL_OFF
    mov r6, r2                /* r6 = 拷贝目标 (r2 保持原值, 用作回跳) */
    mov r5, #TAIL_LEN
1:  ldrb r7, [r4], #1
    strb r7, [r6], #1
    subs r5, r5, #1
    bne 1b
    mov pc, r2                /* 跳内核 .data 中的 tail 拷贝 */

/* ---- 字面池 (grd_arm/grd_watch/putc 的 ldr = 常量) ---- */
.ltorg

/* ================= 内部 helpers (0x380+, leaf) ================= */
/* putc: r0=char (PL011 @0x101f1000); 破坏 r0-r2。
 * 不等待 TXFF (fire-and-forget, 与内核 stdio.c 一致: QEMU 下 TXFF 忙等极慢) */
.org 0x380
grd_putc:
    ldr r1, =UART
    strb r0, [r1]
    mov pc, lr
/* putc 的 UART 字面量池: 必须在本节内, 保证从保留区运行时
 * PC 相对偏移仍指向 blob 拷贝内 (段尾池指向 ELF 地址, 从区运行必坏) */
.ltorg

/* putstr: r0=ptr; 破坏 r0-r2 (指针压栈, putc 调用后重载,
 * 不能用 r0 跨调用保存指针 -- putc 用 r0 传字符) */
grd_putstr:
    sub sp, sp, #8
    str lr, [sp, #4]
    str r0, [sp]
1:  ldr r0, [sp]
    ldrb r2, [r0], #1
    str r0, [sp]
    cmp r2, #0
    beq 2f
    mov r0, r2
    bl grd_putc
    b 1b
2:  ldr lr, [sp, #4]
    add sp, sp, #8
    mov pc, lr

/* nl: \r\n; 破坏 r0-r2 */
grd_nl:
    sub sp, sp, #8
    str lr, [sp, #4]
    mov r0, #13
    bl grd_putc
    mov r0, #10
    bl grd_putc
    ldr lr, [sp, #4]
    add sp, sp, #8
    mov pc, lr

/* puthex: r0=32 位值 -> "0x" + 8 数字; 破坏 r0-r3 */
grd_puthex:
    sub sp, sp, #8
    str lr, [sp, #4]
    str r0, [sp]
    mov r0, #'0'
    bl grd_putc
    mov r0, #'x'
    bl grd_putc
    ldr r0, [sp]
    bl grd_puthex_digits
    ldr lr, [sp, #4]
    add sp, sp, #8
    mov pc, lr

/* puthex_digits: r0=32 位值 -> 8 数字 (无前缀); 破坏 r0-r3 */
grd_puthex_digits:
    sub sp, sp, #12
    str lr, [sp, #8]
    add r2, sp, #0
    mov r3, #8
1:  and r1, r0, #0xF
    cmp r1, #10
    addlo r1, r1, #48
    addhs r1, r1, #55
    strb r1, [r2], #1
    lsr r0, r0, #4
    subs r3, r3, #1
    bne 1b
    /* 反向打印 (sp+7 最高位起); 计数器 r3, 地址 r2 每次重算
     * (putc 破坏 r1/r2), r3 不被 putc 破坏 */
    mov r3, #8
2:  sub r3, r3, #1
    add r2, sp, r3
    ldrb r0, [r2]
    bl grd_putc
    cmp r3, #0
    bne 2b
    ldr lr, [sp, #8]
    add sp, sp, #12
    mov pc, lr

/* puthex64: r0=lo r1=hi -> "0x" + hi8 + lo8; 破坏 r0-r5 (压栈 r4/r5) */
grd_puthex64:
    sub sp, sp, #12
    str lr, [sp, #4]
    str r4, [sp]
    str r5, [sp, #8]
    mov r4, r0               /* 存 lo */
    mov r5, r1               /* 存 hi (putc 会破坏 r1!) */
    mov r0, #'0'
    bl grd_putc
    mov r0, #'x'
    bl grd_putc
    mov r0, r5               /* hi */
    bl grd_puthex_digits
    mov r0, r4               /* lo */
    bl grd_puthex_digits
    ldr r4, [sp]
    ldr r5, [sp, #8]
    ldr lr, [sp, #4]
    add sp, sp, #12
    mov pc, lr

/* ================= grd_report @ +0x500 =================
 * r0=region: 切到自身栈, PL011 转储 + PL110 红带/CRASH 字形, 停机 (不返回)
 * 状态寄存器: r4=region, r5=data; 循环用 r8/r9/r10 (helpers 不破坏 r4-r11) */
.org 0x500
.globl grd_report
grd_report:
    mov r4, r0
    add sp, r4, #0xF000       /* 自身栈: region+0xF000-64 */
    sub sp, sp, #64
    mov r5, r4
    add r5, r5, #DATA_OFF

    mov r0, r5
    bl grd_putstr             /* head    @0x000 */
    add r0, r5, #0x040
    bl grd_putstr             /* arch    @0x040 */
    add r0, r5, #0x060
    bl grd_putstr             /* cause:  @0x060 */
    ldr r0, [r4, #H_CAUSE]
    cmp r0, #1
    beq .Lc_exc
    cmp r0, #2
    beq .Lc_panic
    add r0, r5, #0x0C0        /* WATCHDOG */
    b .Lc_done
.Lc_exc:
    add r0, r5, #0x080        /* EXCEPTION */
    b .Lc_done
.Lc_panic:
    add r0, r5, #0x0A0        /* KERNEL PANIC */
.Lc_done:
    bl grd_putstr
    add r0, r5, #0x0E0
    bl grd_putstr             /* detail : */
    add r0, r4, #H_CSTR
    bl grd_putstr             /* cause_str */
    add r0, r5, #0x100
    bl grd_putstr             /* armed  : */
    ldr r1, [r4, #H_ARMED+4]
    ldr r0, [r4, #H_ARMED]
    bl grd_puthex64
    add r0, r5, #0x120
    bl grd_putstr             /* regs   : */

    /* 16 个寄存器 (8 字节槽): name=S_NAMES+i*4, value=H_REGS+i*8 */
    mov r8, #H_REGS
    mov r9, #16
    mov r10, #0
.Lregs:
    add r0, r5, #0xE8         /* 0x1E8 = 0xE8 + 0x100 */
    add r0, r0, #0x100
    add r0, r0, r10, lsl #2
    bl grd_putstr
    mov r0, #'='
    bl grd_putc
    add r3, r4, r8
    ldr r0, [r3]
    ldr r1, [r3, #4]
    bl grd_puthex64
    bl grd_nl
    add r8, r8, #8
    add r10, r10, #1
    subs r9, r9, #1
    bne .Lregs

    add r0, r5, #0x140
    bl grd_putstr             /* sp     : */
    ldr r1, [r4, #H_SP+4]
    ldr r0, [r4, #H_SP]
    bl grd_puthex64
    add r0, r5, #0x160
    bl grd_putstr             /* pc     : */
    ldr r1, [r4, #H_PC+4]
    ldr r0, [r4, #H_PC]
    bl grd_puthex64
    add r0, r5, #0x180
    bl grd_putstr             /* cpsr   : */
    ldr r1, [r4, #H_EXTRA+4]
    ldr r0, [r4, #H_EXTRA]
    bl grd_puthex64
    bl grd_nl

    /* 栈快照: 8 × 64 位 */
    add r0, r5, #0x1A0
    bl grd_putstr             /* stack  : */
    mov r8, #H_SNAP
    mov r9, #8
.Lsnap:
    mov r0, #' '
    bl grd_putc
    add r3, r4, r8
    ldr r0, [r3]
    ldr r1, [r3, #4]
    bl grd_puthex64
    add r8, r8, #8
    subs r9, r9, #1
    bne .Lsnap
    bl grd_nl

    /* 屏幕: BGR565 红带 (0x001F) + "CRASH!" 白字形 (0xFFFF), 无帧缓冲则跳过 */
    ldr r8, [r4, #H_SCREE]
    cmp r8, #0
    beq .Lno_screen
    ldr r9, [r4, #H_SFMT]
    cmp r9, #2
    bne .Lno_screen
    /* 顶部 32 行红 (640x480, stride 1280B, 0x001F) */
    mov r9, #0x5000           /* 32*640 = 20480 半字 */
    mov r10, #31              /* 0x001F */
.Lrowf:
    mov r11, #640
1:  strh r10, [r8], #2
    subs r11, r11, #1
    bne 1b
    subs r9, r9, #1
    bne .Lrowf
    /* "CRASH!" 字形: 字符 x=8+i*6, y=40..46 */
    mov r9, #0                /* char idx */
.Lchar:
    add r0, r5, #0x28         /* S_TXT = 0x228 = 0x28 + 0x200 */
    add r0, r0, #0x200
    add r0, r0, r9
    ldrb r0, [r0]
    sub r0, r0, #65           /* 'A'-'Z' -> +10; 其他 -> 36 ('!') */
    cmp r0, #25
    addls r0, r0, #10
    movhi r0, #36
    mov r1, r0, lsl #5        /* idx*35 = idx*32 + idx*2 + idx */
    add r1, r1, r0, lsl #1
    add r1, r1, r0
    add r1, r5, r1
    add r1, r1, #0x240        /* S_FONT 字形字节表 */
    ldr r8, [r4, #H_SCREE]
    mov r2, #25
    lsl r2, r2, #11           /* 51200 = 40*1280 (y=40) */
    add r10, r8, r2
    add r10, r10, #16         /* + 8*2 (x=8) */
    add r10, r10, r9, lsl #3  /* + i*8 */
    add r10, r10, r9, lsl #2  /* + i*4 -> i*12 = i*6*2 */
    mov r2, #7                /* row */
    mov r3, r10               /* row 基址 */
.Lrow7:
    mov r11, #5               /* col */
    mov r12, r3
.Lcol5:
    ldrb r6, [r1, r11]
    lsr r6, r6, r2            /* bit = 列字节 bit(row) */
    and r6, r6, #1
    cmp r6, #0
    mvnne r6, #0              /* 白 0xFFFF (半字截断) */
    moveq r6, #31             /* 红 0x001F */
    strh r6, [r12], #2
    subs r11, r11, #1
    bne .Lcol5
    add r3, r3, #1280         /* 0x500 下一行 */
    subs r2, r2, #1
    bne .Lrow7
    add r9, r9, #1
    cmp r9, #6
    blt .Lchar
.Lno_screen:
    add r0, r5, #0x1C0
    bl grd_putstr             /* halted 消息 */
.Lhalt:
    b .Lhalt

/* ================= 数据区 @ +0x800 =================
 * 字符串/表按固定偏移放置, S_* = 相对 DATA_OFF (0x800) 的数值常量。 */
.org 0x800
.Lb_head:  .asciz "\r\n========== GUARDIAN CRASH REPORT ==========\r\n"   /* 0x000, 57B */
.org 0x840
.Lb_arch:  .asciz "arch   : arm32 (ARM926)\r\n"                          /* 0x040 */
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
.Lb_armed: .asciz "\r\narmed  : "                                        /* 0x100 */
.org 0x920
.Lb_regs:  .asciz "\r\nregs   :\r\n"                                     /* 0x120 */
.org 0x940
.Lb_sp:    .asciz "\r\nsp     : "                                        /* 0x140 */
.org 0x960
.Lb_pc:    .asciz "\r\npc     : "                                        /* 0x160 */
.org 0x980
.Lb_extra: .asciz "\r\ncpsr   : "                                        /* 0x180 */
.org 0x9A0
.Lb_snap:  .asciz "\r\nstack  :"                                         /* 0x1A0 */
.org 0x9C0
.Lb_halt:  .asciz "\r\n\r\n*** system halted by guardian ***\r\n"        /* 0x1C0, 40B */
.org 0x9E8
.Lnames:
    .byte 'r','0',0,0                                                    /* 0x1E8 */
    .byte 'r','1',0,0
    .byte 'r','2',0,0
    .byte 'r','3',0,0
    .byte 'r','4',0,0
    .byte 'r','5',0,0
    .byte 'r','6',0,0
    .byte 'r','7',0,0
    .byte 'r','8',0,0
    .byte 'r','9',0,0
    .byte '1','0',0,0
    .byte '1','1',0,0
    .byte '1','2',0,0
    .byte 's','p',0,0
    .byte 'l','r',0,0
    .byte 'p','s','r',0
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
 * r0=region r1=ksp r3=after: 恢复内核栈, 抹除整个保留区, jr r3。 */
.org 0xB00
.globl guard_tail
guard_tail:
    mov sp, r1
    mov r4, r0
    mov r5, #0x10000           /* region+0x10000 */
    add r5, r4, r5
1:  mov r6, #0
    str r6, [r4], #4
    cmp r4, r5
    bne 1b
    mov pc, r3

.globl guard_blob_end
guard_blob_end:

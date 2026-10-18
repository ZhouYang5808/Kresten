/* ===== arch/guard_asm.s: 崩溃守护 blob (x86-64, 纯汇编, PIC) =====
 * 与内核完全无关的独立汇编系统。内核启动时被 guard_init() memcpy 到
 * 保留区 (0x3000000, 64KB), 之后通过固定偏移 (guard.h GRD_*_OFF) 调用:
 *   grd_arm     武装 (初始化 hdr, 设看门狗超时, 存帧缓冲地址)
 *   grd_panic   主动上报入口 (glue 已填 cause/cause_str) -> 转 report
 *   grd_kick    心跳: deadline = now + timeout
 *   grd_watch   看门狗: now > deadline -> 转 report (cause=WATCHDOG)
 *   grd_disarm  自毁: 把 tail 拷到内核 .data, 跳过去抹除整个保留区后回内核
 *   grd_report  崩溃转储: 切到自己栈, COM1 完整报告 + 屏幕红带/CRASH 字形, 停机
 * 所有代码位置无关 (x86-64 用 mov $imm 立即数, 无字面池), 从保留区运行也正确。
 * 约定: 入口 rdi=region (grd_disarm: rdi=region rsi=ksp rdx=tail_dst rcx=after);
 * helpers 只破坏 caller-saved 寄存器 (rax rcx rdx rsi rdi r8-r11, puthex64 额外
 * 压栈 rbx/r12), 报告主体状态在 rbp r12-r15 (callee-saved, helpers 都保存)。 */
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
.set COM1,    0x3F8          /* 串口 1 */
.set REPORT_OFF, 0x500
.set DATA_OFF,   0x800
.set TAIL_OFF,   0xB00
.set TAIL_LEN,   64

/* ================= grd_arm @ +0x200 =================
 * rdi=region rsi=now_ms rdx=fb rcx=fmt -> rax=0 */
.org 0x200
.globl grd_arm
grd_arm:
    movl $GRD_MAG, 0x00(%rdi)
    movl $1, 0x04(%rdi)          /* state = ARMED */
    movl $0, 0x08(%rdi)          /* cause = NONE */
    movl $0, 0x0C(%rdi)          /* flags (会覆盖 QEMU loader 的测试标志, 故 glue 先读) */
    movq $0, 0x10(%rdi)          /* heartbeat */
    movq $0, 0x18(%rdi)          /* deadline */
    movl $GRD_TIMEOUT, 0x20(%rdi)
    movl %esi, 0x28(%rdi)        /* armed_ms = now */
    movl $0, 0x2C(%rdi)
    movl $0, 0x30(%rdi)          /* serial (未用) */
    movl $0, 0x34(%rdi)
    movl %edx, 0x38(%rdi)        /* screen (fb) */
    movl $0, 0x3C(%rdi)          /* pitch 高 32 位 */
    movl %ecx, 0x40(%rdi)        /* fmt */
    movl $1, 0x44(%rdi)          /* arch = GRD_ARCH_X86_64 */
    xor %eax, %eax
    ret

/* ================= grd_panic @ +0x280 =================
 * rdi=region -> 转 report, 不返回 */
.org 0x280
.globl grd_panic
grd_panic:
    lea REPORT_OFF(%rdi), %rax
    jmp *%rax

/* ================= grd_kick @ +0x2A0 =================
 * rdi=region rsi=now_ms: 心跳, deadline = now + timeout */
.org 0x2A0
.globl grd_kick
grd_kick:
    incq 0x10(%rdi)              /* heartbeat++ */
    movl 0x20(%rdi), %eax
    addl %esi, %eax
    movl %eax, 0x18(%rdi)        /* deadline = now + timeout */
    xor %eax, %eax
    ret

/* ================= grd_watch @ +0x2D0 =================
 * rdi=region rsi=now_ms -> rax=0 正常; 超时 -> 转 report (不返回) */
.org 0x2D0
.globl grd_watch
grd_watch:
    movl 0x18(%rdi), %eax        /* deadline */
    test %eax, %eax
    jz .Lwatch_ok
    cmpl %eax, %esi              /* now < deadline -> OK */
    jb .Lwatch_ok
    movl $GRD_WATCH_CAUSE, 0x08(%rdi)
    movq $0x43415457, 0x48(%rdi) /* "WATC" */
    movq $0x484F4447, 0x50(%rdi) /* "HDOG" */
    movq $0x2054494D, 0x58(%rdi) /* " TIM" */
    movq $0x54454F55, 0x60(%rdi) /* "EOUT" */
    movq $0x00000054, 0x68(%rdi) /* "T\0\0\0\0\0\0\0" */
    lea REPORT_OFF(%rdi), %rax
    jmp *%rax
.Lwatch_ok:
    xor %eax, %eax
    ret

/* ================= grd_disarm @ +0x340 =================
 * rdi=region rsi=ksp rdx=tail_dst rcx=after: 把 tail 拷到内核 .data 并跳转,
 * tail 抹除整个保留区后恢复 rsp=ksp 并 jmp after (不返回)。 */
.org 0x340
.globl grd_disarm
grd_disarm:
    push %rdi                    /* region */
    push %rsi                    /* ksp */
    push %rcx                    /* after */
    push %rdx                    /* tail dst */
    lea TAIL_OFF(%rdi), %rsi     /* src = region+tail */
    mov %rdx, %rdi               /* dst */
    mov $TAIL_LEN, %ecx
    rep movsb
    pop %rdx                     /* tail dst = 跳转目标 */
    pop %rcx                     /* after */
    pop %rsi                     /* ksp */
    pop %rdi                     /* region */
    jmp *%rdx

/* ================= 内部 helpers (0x380+, leaf) ================= */
/* putc: edi=char (COM1 @0x3F8); 有界轮询 LSR.THRE (与内核 stdio.c 同思路,
 * QEMU 下 TXFF 忙等会阻塞, 这里轮询几次后丢弃)。破坏 rax rcx rdx。 */
.org 0x380
.balign 16
grd_putc:
    movl $COM1+5, %edx
    mov $20000, %ecx
1:  inb %dx, %al
    test $0x20, %al              /* LSR bit5 = THRE */
    jnz 2f
    loop 1b
    ret
2:  movl $COM1, %edx
    movb %dil, %al
    outb %al, %dx
    ret

/* putstr: rdi=ptr; 指针放 rbx (callee-saved), putc 用 edi 传字符 */
.balign 16
grd_putstr:
    push %rbx
    mov %rdi, %rbx
1:  movzbl (%rbx), %eax
    test %eax, %eax
    jz 2f
    movzbl %al, %edi
    call grd_putc
    inc %rbx
    jmp 1b
2:  pop %rbx
    ret

/* nl: \r\n; 破坏 rax rcx rdx */
.balign 16
grd_nl:
    push %rax
    mov $13, %edi
    call grd_putc
    mov $10, %edi
    call grd_putc
    pop %rax
    ret

/* puthex: edi=32 位值 -> "0x" + 8 数字 */
.balign 16
grd_puthex:
    push %rax
    push %rsi
    mov %rdi, %rsi               /* 存值 */
    mov $'0', %edi
    call grd_putc
    mov $'x', %edi
    call grd_putc
    mov %rsi, %rdi
    call grd_puthex_digits
    pop %rsi
    pop %rax
    ret

/* puthex_digits: edi=32 位值 -> 8 数字 (无前缀); 破坏 rax rcx rdx r9 */
.balign 16
grd_puthex_digits:
    push %rbx
    movl %edi, %ebx              /* 值 (零扩展) */
    mov $28, %ecx                /* 移位 28..0 */
    mov $8, %r9d
1:  mov %ebx, %eax
    shr %cl, %eax
    and $0xF, %eax
    cmp $10, %eax
    jb 2f
    add $7, %eax
2:  add $'0', %eax
    mov %eax, %edi
    push %rcx                    /* putc 破坏 rcx (轮询计数) */
    push %r9
    call grd_putc
    pop %r9
    pop %rcx
    sub $4, %ecx
    dec %r9d
    jnz 1b
    pop %rbx
    ret

/* puthex64: rdi=lo rsi=hi -> "0x" + hi8 + lo8; 保存 rbx r12 */
.balign 16
grd_puthex64:
    push %rbx
    push %r12
    mov %rdi, %rbx               /* lo */
    mov %rsi, %r12               /* hi (putc 破坏 rsi!) */
    mov $'0', %edi
    call grd_putc
    mov $'x', %edi
    call grd_putc
    mov %r12d, %edi
    call grd_puthex_digits
    mov %ebx, %edi
    call grd_puthex_digits
    pop %r12
    pop %rbx
    ret

/* ================= grd_report @ +0x500 =================
 * rdi=region: cli, 切到自身栈, COM1 转储 + 屏幕红带/CRASH 字形, 停机 (不返回)
 * 状态: rbp=region, r13=data; 循环用 r12 (helpers 不破坏 rbp/r12-r15) */
.org 0x500
.balign 16
.globl grd_report
grd_report:
    cli
    mov %rdi, %rbp
    lea 0xEFC0(%rbp), %rsp       /* 自身栈: region+0xF000-64 */
    lea DATA_OFF(%rbp), %r13     /* data 基址 */

    lea 0x000(%r13), %rdi
    call grd_putstr              /* head    @0x000 */
    lea 0x040(%r13), %rdi
    call grd_putstr              /* arch    @0x040 */
    lea 0x060(%r13), %rdi
    call grd_putstr              /* cause:  @0x060 */
    movl 0x08(%rbp), %eax
    cmp $1, %eax
    je .Lc_exc
    cmp $2, %eax
    je .Lc_panic
    lea 0x0C0(%r13), %rdi        /* WATCHDOG */
    jmp .Lc_done
.Lc_exc:
    lea 0x080(%r13), %rdi        /* EXCEPTION */
    jmp .Lc_done
.Lc_panic:
    lea 0x0A0(%r13), %rdi        /* KERNEL PANIC */
.Lc_done:
    call grd_putstr
    lea 0x0E0(%r13), %rdi
    call grd_putstr              /* detail : */
    lea H_CSTR(%rbp), %rdi
    call grd_putstr              /* cause_str */
    lea 0x100(%r13), %rdi
    call grd_putstr              /* armed  : */
    mov 0x2C(%rbp), %esi
    mov 0x28(%rbp), %rdi
    call grd_puthex64
    lea 0x120(%r13), %rdi
    call grd_putstr              /* regs   : */

    /* 16 个寄存器 (8 字节槽): name=S_NAMES+i*4, value=H_REGS+i*8 */
    xor %r12d, %r12d
.Lregs:
    lea 0x1E8(%r13), %rdi
    mov %r12, %rax
    shl $2, %rax
    add %rax, %rdi
    call grd_putstr              /* name */
    mov $'=', %edi
    call grd_putc
    mov %r12, %rax
    shl $3, %rax
    mov 0x8C(%rbp,%rax), %esi    /* regs[i] 高 32 位 */
    mov 0x88(%rbp,%rax), %rdi
    call grd_puthex64
    call grd_nl
    inc %r12d
    cmp $16, %r12d
    jne .Lregs

    lea 0x140(%r13), %rdi
    call grd_putstr              /* sp     : */
    mov 0x10C(%rbp), %esi
    mov 0x108(%rbp), %rdi
    call grd_puthex64
    lea 0x160(%r13), %rdi
    call grd_putstr              /* pc     : */
    mov 0x114(%rbp), %esi
    mov 0x110(%rbp), %rdi
    call grd_puthex64
    lea 0x180(%r13), %rdi
    call grd_putstr              /* eflags : */
    mov 0x11C(%rbp), %esi
    mov 0x118(%rbp), %rdi
    call grd_puthex64
    call grd_nl

    /* 栈快照: 8 × 64 位 */
    lea 0x1A0(%r13), %rdi
    call grd_putstr              /* stack  : */
    xor %r12d, %r12d
.Lsnap:
    mov $' ', %edi
    call grd_putc
    mov %r12, %rax
    shl $3, %rax
    mov 0x124(%rbp,%rax), %esi   /* snap[i] 高 32 位 */
    mov 0x120(%rbp,%rax), %rdi
    call grd_puthex64
    inc %r12d
    cmp $8, %r12d
    jne .Lsnap
    call grd_nl

    /* 屏幕: fmt 4 (8bpp 索引: 红=0x90 白=0x0F) / fmt 5 (XRGB8888:
     * 红=0xFFFF0000 白=0xFFFFFFFF), 红带 32 行 + "CRASH!" 白字形 (y=40),
     * 无帧缓冲或 fmt 不符则跳过。H_SCREE = fb | pitch<<32 */
    mov 0x38(%rbp), %r8          /* screen */
    test %r8, %r8
    jz .Lno_screen
    movl 0x40(%rbp), %r9d        /* fmt */
    cmp $4, %r9d
    je .Lscr8
    cmp $5, %r9d
    jne .Lno_screen
    mov %r8d, %ebx               /* fb */
    shr $32, %r8
    mov %r8, %r15                /* pitch */
    movl $0xFFFF0000, %r11d      /* 红 */
    movl $0xFFFFFFFF, %r10d      /* 白 */
    jmp .Lband32
.Lscr8:
    mov %r8d, %ebx
    shr $32, %r8
    mov %r8, %r15
    movl $0x00000090, %r11d      /* 红 = 调色板 0x90 */
    movl $0x0000000F, %r10d      /* 白 = 调色板 0x0F */
.Lband8:
    mov $32, %r12d               /* 32 行 */
1:  movl %r15d, %ecx             /* pitch 字节 */
2:  movb %r11b, (%rbx)
    inc %rbx
    dec %ecx
    jnz 2b
    dec %r12d
    jnz 1b
    jmp .Lband_done
.Lband32:
    mov $32, %r12d
1:  movl %r15d, %ecx
    shr $2, %ecx                 /* pitch/4 双字 */
2:  movl %r11d, (%rbx)
    add $4, %rbx
    dec %ecx
    jnz 2b
    dec %r12d
    jnz 1b
.Lband_done:

    /* "CRASH!" 字形: 字符 x=8+i*12, y=40..46 */
    mov 0x38(%rbp), %r8
    mov %r8d, %ebx               /* fb 重新载入 */
    shr $32, %r8
    mov %r8, %r15
    movl 0x40(%rbp), %r9d
    cmp $5, %r9d
    jne .Lgl8
    mov $2, %r9d                 /* bytespp log2 = 2 */
    jmp .Lglc
.Lgl8:
    mov $0, %r9d                 /* bytespp log2 = 0 */
.Lglc:
    xor %r12d, %r12d             /* 字符下标 */
.Lchar:
    lea 0x228(%r13), %rax        /* S_TXT */
    mov %r12, %rcx
    add %rcx, %rax
    movzbl (%rax), %eax          /* c */
    sub $65, %eax
    cmp $25, %eax
    jbe .Lgk
    mov $36, %eax                /* '!' 字形 */
    jmp .Lgd
.Lgk:
    add $10, %eax
.Lgd:
    lea 0x240(%r13), %r14        /* S_FONT */
    mov %eax, %ecx
    imul $35, %ecx               /* 字形 = 35 字节 */
    add %rcx, %r14               /* 字形指针 */
    /* 像素基址 = fb + 40*pitch + ((8 + i*12) << log2) */
    mov %r15, %rax
    imul $40, %rax
    add %rbx, %rax
    mov %r12d, %ecx
    imul $12, %ecx
    add $8, %ecx
    mov %r9d, %r8d
    shl %cl, %r8d                /* x 偏移 (字节) */
    add %r8, %rax
    mov $7, %r10d                /* 行位 7..1 */
.Lrow7:
    mov %rax, %rdx               /* 行基址 */
    mov $5, %r11d                /* 列 */
.Lcol5:
    mov %r14, %r8
    add %r11, %r8
    movzbl (%r8), %r8d           /* 字形列字节 */
    mov %r10d, %ecx
    shr %cl, %r8d                /* bit = byte >> row */
    and $1, %r8d
    jnz .Lw
    cmp $0, %r9d                 /* 红 */
    jne .Lr32
    movb $0x90, (%rdx)
    jmp .Lwd
.Lr32:
    movl $0xFFFF0000, (%rdx)
    jmp .Lwd
.Lw:
    cmp $0, %r9d                 /* 白 */
    jne .Lw32
    movb $0x0F, (%rdx)
    jmp .Lwd
.Lw32:
    movl $0xFFFFFFFF, (%rdx)
.Lwd:
    mov %r9d, %ecx
    mov $1, %r8d
    shl %cl, %r8d                /* bytespp */
    add %r8, %rdx
    dec %r11d
    jnz .Lcol5
    add %r15, %rax               /* 下一行 */
    dec %r10d
    jnz .Lrow7
    inc %r12d
    cmp $6, %r12d
    jl .Lchar
.Lno_screen:
    lea 0x1C0(%r13), %rdi
    call grd_putstr              /* halted 消息 */
.Lhalt:
    jmp .Lhalt

/* ================= 数据区 @ +0x800 =================
 * 字符串/表按固定偏移放置, S_* = 相对 DATA_OFF (0x800) 的数值常量。 */
.org 0x800
.Lb_head:  .asciz "\r\n========== GUARDIAN CRASH REPORT ==========\r\n"   /* 0x000 */
.org 0x840
.Lb_arch:  .asciz "arch   : x86-64 (AMD64)\r\n"                          /* 0x040 */
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
.Lb_extra: .asciz "\r\neflags : "                                        /* 0x180 */
.org 0x9A0
.Lb_snap:  .asciz "\r\nstack  :"                                         /* 0x1A0 */
.org 0x9C0
.Lb_halt:  .asciz "\r\n\r\n*** system halted by guardian ***\r\n"        /* 0x1C0 */
.org 0x9E8
.Lnames:                                                               /* 0x1E8 */
    .byte 'r','1','5',0
    .byte 'r','1','4',0
    .byte 'r','1','3',0
    .byte 'r','1','2',0
    .byte 'r','1','1',0
    .byte 'r','1','0',0
    .byte 'r','9',0,0
    .byte 'r','8',0,0
    .byte 'r','b','p',0
    .byte 'r','d','i',0
    .byte 'r','s','i',0
    .byte 'r','d','x',0
    .byte 'r','c','x',0
    .byte 'r','b','x',0
    .byte 'r','a','x',0
    .byte 'r','s','p',0
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
 * rdi=region: 抹除整个保留区后沿 grd_call 栈链 ret (rbp 由 grd_call 的
 * leave 还原), 返回到 guard_finish 的 L_after。注意不能 mov rsp,ksp 或
 * jmp after: grd_call 的返回地址 0x1086b3 还在栈上, 直接 jmp 会让 L_after
 * 的 leave/ret 再次弹出它 -> 二次进入 L_after -> heap_extend 双重执行。 */
.org 0xB00
.globl guard_tail
guard_tail:
    cld
    mov %rdi, %r8                /* region */
    xor %eax, %eax
    mov $8192, %ecx              /* 64KB / 8 */
    mov %r8, %rdi
    rep stosq
    ret

.globl guard_blob_end
guard_blob_end:

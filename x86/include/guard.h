#ifndef GUARD_H
#define GUARD_H

#include <stdint.h>
#include <idt.h>

/* ===== 崩溃守护系统 (guardian) 共享布局 (x86-64) =====
 * 纯汇编 blob (arch/guard_asm.s) 在启动时被拷入保留区 GRD_REGION
 * (48MB, 64KB), 内核通过固定偏移调用。启动成功 (桌面首帧) 后
 * guard_finish() 触发自毁 (tail 拷贝到内核 .data 执行, 抹除整个保留区),
 * 内存回收给堆。偏移常量必须与 guard_asm.s 中的 .set 一致。 */

#define GRD_MAGIC        0x31445247u    /* 'GRD1' */
#define GRD_REGION       0x3000000UL    /* 48MB: LX 运行区 (至 0x2400000) 之上, 绝对空闲 */
#define GRD_SIZE         0x10000UL
#define GRD_TIMEOUT_MS   5000u

/* hdr 偏移 (blob asm 同步) */
#define GRD_HDR_MAGIC      0x00
#define GRD_HDR_STATE      0x04
#define GRD_HDR_CAUSE      0x08
#define GRD_HDR_FLAGS      0x0C
#define GRD_HDR_HEARTBEAT  0x10
#define GRD_HDR_DEADLINE   0x18
#define GRD_HDR_TIMEOUT    0x20
#define GRD_HDR_ARMED_MS   0x28
#define GRD_HDR_SERIAL     0x30
#define GRD_HDR_SCREEN     0x38   /* fb | pitch<<32 (面板行字节数) */
#define GRD_HDR_SCREEN_FMT 0x40
#define GRD_HDR_ARCH_ID    0x44
#define GRD_HDR_CAUSE_STR  0x48
#define GRD_HDR_REGS       0x88
#define GRD_HDR_SP         0x108
#define GRD_HDR_PC         0x110
#define GRD_HDR_EXTRA      0x118
#define GRD_HDR_STACK_SNAP 0x120

#define GRD_STATE_OFF   0
#define GRD_STATE_ARMED 1

#define GRD_CAUSE_NONE  0
#define GRD_CAUSE_EXC   1
#define GRD_CAUSE_PANIC 2
#define GRD_CAUSE_WATCH 3

#define GRD_FMT_NONE    0
#define GRD_FMT_VGA_TEXT 1
#define GRD_FMT_BGR565  2
#define GRD_FMT_RGB565  3
#define GRD_FMT_8BPP    4       /* VBE 8bpp: 调色板索引, 红=0x90, 白=0x0F */
#define GRD_FMT_XRGB8888 5      /* SVGA 32bpp: 红=0xFFFF0000, 白=0xFFFFFFFF */

#define GRD_ARCH_X86_64 1
#define GRD_ARCH_ARM32  2
#define GRD_ARCH_RV64   3

/* blob 入口偏移 (x86-64 版本) */
#define GRD_ARM_OFF    0x200
#define GRD_PANIC_OFF  0x280
#define GRD_KICK_OFF   0x2A0
#define GRD_WATCH_OFF  0x2D0
#define GRD_DISARM_OFF 0x340
#define GRD_REPORT_OFF 0x500

typedef struct {
    uint32_t magic, state, cause, flags;
    uint64_t heartbeat, deadline_ms;
    uint32_t timeout_ms, pad;
    uint64_t armed_ms;
    uint64_t serial_addr, screen_addr;
    uint32_t screen_fmt, arch_id;
    char cause_str[64];
    uint64_t regs[16];
    uint64_t sp, pc, extra;
    uint64_t stack_snap[8];
} guard_hdr_t;

void guard_init(void);
void guard_panic(const char *msg);
void guard_kick(void);
void guard_watch(void);
void guard_finish(void);
int guard_armed(void);
void guard_trap(registers_t *r, const char *msg);

#endif

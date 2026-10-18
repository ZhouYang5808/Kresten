/* ===== kernel/guard.c: 崩溃守护系统胶水 (x86-64) =====
 * 把纯汇编 blob (arch/guard_asm.s) 拷入保留区 0x3000000 (48MB, 64KB),
 * 武装后接管: 异常转储 (idt.c isr_handler hook) / panic / 看门狗。
 * 启动成功 (桌面首帧) 后 guard_finish() 自毁: tail 抹除保留区,
 * 64KB 通过 heap_extend() 回收进内核堆。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <guard.h>
#include <idt.h>

extern char guard_blob_start[], guard_blob_end[];

uint32_t sched_get_ticks(void);

static uint8_t grd_tail[96];

static volatile guard_hdr_t *grd_h(void) {
    return (volatile guard_hdr_t *)GRD_REGION;
}

int guard_armed(void) {
    volatile guard_hdr_t *h = grd_h();
    return h->magic == GRD_MAGIC && h->state == GRD_STATE_ARMED;
}

static uint32_t grd_now(void) {
    return (uint32_t)sched_get_ticks() * 10u;   /* 厘秒 -> 毫秒 */
}

typedef uint64_t (*grd_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t);

static uint64_t grd_call(uint64_t off, uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5) {
    return ((grd_fn_t)(GRD_REGION + off))(a1, a2, a3, a4, a5, 0);
}

void guard_init(void) {
    volatile guard_hdr_t *h = grd_h();
    if (h->magic == GRD_MAGIC) return;
    uint32_t testflag = *(volatile uint32_t *)(GRD_REGION + GRD_HDR_FLAGS);
    size_t size = (size_t)(guard_blob_end - guard_blob_start);
    if (size == 0 || size > GRD_SIZE) return;
    memcpy((void *)GRD_REGION, guard_blob_start, size);
    grd_call(GRD_ARM_OFF, GRD_REGION, grd_now(), 0, GRD_FMT_NONE, 0);
    printf("[GUARD] armed: blob %u B, region 0x%08x (%u KB reserved)\n",
           (unsigned)size, (unsigned)GRD_REGION, (unsigned)(GRD_SIZE >> 10));
    if (testflag & 1) {
        printf("[GUARD] boot crash test requested - triggering illegal "
               "instruction\n");
        __asm__ volatile("ud2");
        for (;;);
    }
}

void guard_panic(const char *msg) {
    volatile guard_hdr_t *h;
    if (!guard_armed()) return;
    h = grd_h();
    h->cause = GRD_CAUSE_PANIC;
    strncpy((char *)h->cause_str, msg ? msg : "panic",
            sizeof h->cause_str - 1);
    h->cause_str[sizeof h->cause_str - 1] = '\0';
    grd_call(GRD_PANIC_OFF, GRD_REGION, 0, 0, 0, 0);
    for (;;);
}

void guard_kick(void) {
    if (guard_armed()) grd_call(GRD_KICK_OFF, GRD_REGION, grd_now(), 0, 0, 0);
}

void guard_watch(void) {
    if (guard_armed()) grd_call(GRD_WATCH_OFF, GRD_REGION, grd_now(), 0, 0, 0);
}

void guard_trap(registers_t *r, const char *msg) {
    volatile guard_hdr_t *h = grd_h();
    uint64_t *g, *sn, *f;
    if (h->magic != GRD_MAGIC || h->state != GRD_STATE_ARMED) return;
    h->cause = GRD_CAUSE_EXC;
    strncpy((char *)h->cause_str, msg ? msg : "EXCEPTION",
            sizeof h->cause_str - 1);
    h->cause_str[sizeof h->cause_str - 1] = '\0';
    f = (uint64_t *)(uintptr_t)r;
    g = (uint64_t *)(void *)&h->regs;
    g[0] = f[15];  g[1] = f[14];  g[2] = f[13];  g[3] = f[12];
    g[4] = f[11];  g[5] = f[10];  g[6] = f[9];   g[7] = f[8];
    g[8] = f[7];   g[9] = f[6];   g[10] = f[5];  g[11] = f[4];
    g[12] = f[3];  g[13] = f[2];  g[14] = f[1];  g[15] = f[0];
    h->sp = f[0];
    h->pc = f[18];
    h->extra = f[20];
    sn = (uint64_t *)(void *)&h->stack_snap;
    for (int i = 0; i < 8; i++) sn[i] = f[i];
    grd_call(GRD_REPORT_OFF, GRD_REGION, 0, 0, 0, 0);
    for (;;);
}

void guard_finish(void) {
    uint64_t ksp, after;
    if (!guard_armed()) return;
    printf("[GUARD] boot complete - self-destructing, releasing %u KB\n",
           (unsigned)(GRD_SIZE >> 10));
    __asm__ volatile("mov %%rsp, %0" : "=r"(ksp));
    after = (uint64_t)&&L_after;
    grd_call(GRD_DISARM_OFF, GRD_REGION, ksp, (uint64_t)grd_tail, after, 0);
L_after:
    heap_extend((void *)GRD_REGION, GRD_SIZE);
    printf("[GUARD] reclaimed: heap extended by %u KB\n",
           (unsigned)(GRD_SIZE >> 10));
}

void _panic(const char *message, const char *file, int line) {
    printf("\n*** KERNEL PANIC: %s (%s:%d) ***\n",
           message ? message : "?", file ? file : "?", line);
    guard_panic(message);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

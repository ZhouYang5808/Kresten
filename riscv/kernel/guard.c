/* ===== kernel/guard.c: 崩溃守护系统胶水 (RISC-V) =====
 * 把纯汇编 blob (guard_asm.s) 拷入保留区 0x87FF0000 (RAM 顶部 64KB),
 * 武装后接管: 异常转储 (start.S trap 路径) / panic / 看门狗。
 * 启动成功 (桌面首帧) 后 guard_finish() 自毁: tail 抹除保留区,
 * 64KB 通过 heap_extend() 回收进内核堆。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <guard.h>

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

static uint64_t grd_now(void) {
    return (uint64_t)sched_get_ticks() * 10u;   /* 厘秒 -> 毫秒 */
}

typedef uint64_t (*grd_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t grd_call(uint64_t off, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4) {
    return ((grd_fn_t)(GRD_REGION + off))(a1, a2, a3, a4);
}

void guard_init(void) {
    volatile guard_hdr_t *h = grd_h();
    if (h->magic == GRD_MAGIC) return;
    uint32_t testflag = *(volatile uint32_t *)(GRD_REGION + GRD_HDR_FLAGS);
    size_t size = (size_t)(guard_blob_end - guard_blob_start);
    if (size == 0 || size > GRD_SIZE) return;
    memcpy((void *)GRD_REGION, guard_blob_start, size);
    grd_call(GRD_ARM_OFF, GRD_REGION, grd_now(), 0, 0);
    printf("[GUARD] armed: blob %u B, region 0x%08x (%u KB reserved)\n",
           (unsigned)size, (unsigned)GRD_REGION, (unsigned)(GRD_SIZE >> 10));
    if (testflag & 1) {
        printf("[GUARD] boot crash test requested - triggering illegal "
               "instruction\n");
        __asm__ volatile(".word 0x00000000");
        for (;;);
    }
}

void guard_panic(const char *msg) {
    volatile guard_hdr_t *h;
    if (!guard_armed()) return;
    h = grd_h();
    h->cause = GRD_CAUSE_PANIC;
    snprintf((char *)h->cause_str, sizeof h->cause_str, "%s", msg);
    grd_call(GRD_PANIC_OFF, GRD_REGION, 0, 0, 0);
    for (;;);
}

void guard_kick(void) {
    if (guard_armed()) grd_call(GRD_KICK_OFF, GRD_REGION, grd_now(), 0, 0);
}

void guard_watch(void) {
    if (guard_armed()) grd_call(GRD_WATCH_OFF, GRD_REGION, grd_now(), 0, 0);
}

void guard_finish(void) {
    uint64_t ksp, after;
    if (!guard_armed()) return;
    printf("[GUARD] boot complete - self-destructing, releasing %u KB\n",
           (unsigned)(GRD_SIZE >> 10));
    __asm__ volatile("mv %0, sp" : "=r"(ksp));
    after = (uint64_t)&&L_after;
    __asm__ volatile(
        "mv a0, %0\n"
        "mv a1, %1\n"
        "mv a2, %2\n"
        "mv a3, %3\n"
        "jalr t0, %4\n"
        : : "r"((uintptr_t)GRD_REGION), "r"(ksp),
            "r"((uintptr_t)grd_tail), "r"(after),
            "r"((uintptr_t)(GRD_REGION + GRD_DISARM_OFF))
        : "a0", "a1", "a2", "a3", "t0", "memory");
L_after:
    heap_extend((void *)GRD_REGION, GRD_SIZE);
    printf("[GUARD] reclaimed: heap extended by %u KB\n",
           (unsigned)(GRD_SIZE >> 10));
}

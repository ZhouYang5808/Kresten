/* ===== x86/drivers/sched.c: preemptive/cooperative scheduler driver =====
 *
 * Both modes share ONE task context format: a full interrupt frame
 * (registers_t, context_rsp points at the rsp-field slot). This is what
 * makes live mode switching possible -- switching modes is just updating
 * the sched_mode flag, no context conversion is needed.
 *
 * Preemptive mode (Sched.Mode=preempt, the default): the PIT (channel 0,
 * 100 Hz) raises IRQ0; the timer handler saves the interrupted task's
 * register frame (the stub's stack frame), picks the next task
 * round-robin, and asks the IRQ stub (interrupt.s) to restore that task's
 * frame: `mov sched_next_rsp, %rsp; pop rax..r15; add $16, %rsp; iretq`.
 *
 * Cooperative mode (Sched.Mode=coop): the PIT still ticks (for timing) but
 * never switches tasks; each task must call sched_yield() to hand the CPU
 * to the next task. sched_yield() builds the SAME interrupt-frame layout
 * on the current task's stack (sched_yield_impl in interrupt.s) and jumps
 * into the shared resume path, so frames stay uniform across modes.
 * The main flow (shell) yields while idle in gets().
 *
 * The mode is taken from the registry at boot (sched_apply_registry(),
 * called after reg_init). It can ALSO be switched at runtime with
 * sched_set_mode() (e.g. from the shell `sched` command) -- because the
 * frame format is identical, the switch is immediate and safe: the next
 * timer tick (or the next sched_yield) uses the new mode.
 *
 * Each task owns a kernel stack; slot 0 is the main flow
 * (kernel_main / shell); extra slots are created via sched_create().
 */

#include <driver.h>
#include <stdio.h>
#include <string.h>
#include <idt.h>
#include <io.h>
#include <registry.h>

#define SCHED_MAX_TASKS  4
#define SCHED_STACK_SIZE 8192

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIC1_DATA 0x21

/* Consumed by irq_common_handler in interrupt.s */
uint64_t sched_switch_flag = 0;
uint64_t sched_next_rsp = 0;

volatile uint32_t sched_ticks = 0;

uint32_t sched_get_ticks(void) {
    return sched_ticks;
}

static int sched_mode = SCHED_MODE_PREEMPT;

typedef struct {
    char name[16];
    char stack[SCHED_STACK_SIZE];
} SchedTask;

static SchedTask sched_tasks[SCHED_MAX_TASKS];
uint64_t sched_task_ctx[SCHED_MAX_TASKS]; /* context_rsp per task */
static int sched_task_count = 0;
static int sched_current = 0;

/* Set by sched_yield() before calling sched_yield_impl (interrupt.s):
 * the slot to which the asm must write the frame it just built. This
 * keeps every task's slot fresh at the moment of the switch -- no stale
 * frame is ever left in a slot, and nobody has to "write back" later. */
uint64_t sched_yield_owner = 0;

extern void sched_yield_impl(void);

/* Legacy process switcher (arch/switch.s). Never capture a task's frame
 * while it is inside context_switch: a preempt in the IF=1 window at its
 * tail could save a half-switched register state whose later restore
 * jmp'd to garbage (observed #GP(0), task0 RIP=0x104430 = the entry
 * load before `jmp rax`). Skip the switch for that tick only. */
extern void context_switch(void);
extern void context_switch_end(void);

int sched_get_mode(void) {
    return sched_mode;
}

/* Debug dump for the exception handler (idt.c) */
void sched_dump_state(void) {
    printf("  [SCHED] mode=%d cur=%d n=%d flag=%x next=%x\n",
           sched_mode, sched_current, sched_task_count,
           (unsigned)sched_switch_flag, (unsigned)sched_next_rsp);
    for (int i = 0; i < sched_task_count && i < SCHED_MAX_TASKS; i++)
        printf("  [SCHED] task[%d]=%s ctx=%x\n", i, sched_tasks[i].name,
               (unsigned)sched_task_ctx[i]);
    {
        uint64_t *f = (uint64_t *)(uintptr_t)sched_next_rsp;
        printf("  [SCHED] frame@%x next=%x flag=%x (window -8..23):\n",
               (unsigned)(uintptr_t)f, (unsigned)sched_next_rsp,
               (unsigned)sched_switch_flag);
        for (int i = -8; i < 24; i += 4)
            printf("    [%d]=%08x [%d]=%08x [%d]=%08x [%d]=%08x\n",
                   i, (unsigned)f[i], i + 1, (unsigned)f[i + 1],
                   i + 2, (unsigned)f[i + 2], i + 3, (unsigned)f[i + 3]);
        for (int i = 1; i < sched_task_count && i < SCHED_MAX_TASKS; i++) {
            uint64_t *g = (uint64_t *)(uintptr_t)sched_task_ctx[i];
            printf("  [SCHED] task[%d]=%s frame@%x:\n", i, sched_tasks[i].name,
                   (unsigned)(uintptr_t)g);
            for (int j = 13; j < 21; j += 4)
                printf("    [%d]=%08x [%d]=%08x [%d]=%08x [%d]=%08x\n",
                       j, (unsigned)g[j], j + 1, (unsigned)g[j + 1],
                       j + 2, (unsigned)g[j + 2], j + 3, (unsigned)g[j + 3]);
        }
    }
}

/* Live mode switch. Since both modes use the same interrupt-frame context,
 * this is just a flag flip: the next PIT tick (preempt) or sched_yield()
 * (coop) picks up the new mode. Old mode is returned. */
int sched_set_mode(int mode) {
    int old = sched_mode;
    if (mode != SCHED_MODE_PREEMPT && mode != SCHED_MODE_COOP) return -1;
    sched_mode = mode;
    printf("[SCHED] mode=%s (was %s, live switch)\n",
           mode == SCHED_MODE_COOP ? "cooperative" : "preemptive",
           old == SCHED_MODE_COOP ? "cooperative" : "preemptive");
    return old;
}

/* Cooperative yield: build the current task's interrupt frame and resume
 * the next task through the shared path (sched_restore_common). No-op in
 * preemptive mode (the timer does the switching). */
void sched_yield(void) {
    if (sched_mode != SCHED_MODE_COOP) return;
    if (sched_task_count < 2) return;
    int old = sched_current;
    sched_current = (sched_current + 1) % sched_task_count;
    if (sched_current == old) return;
    sched_yield_owner = old;
    sched_next_rsp = sched_task_ctx[sched_current];
    printf("[Y] old=%d cur=%d next=%x\n", old, sched_current, (unsigned)sched_next_rsp);
    if (sched_current == 0) {
        uint64_t *f = (uint64_t *)(uintptr_t)sched_next_rsp;
        printf("  [S0] pre-restore shell frame@%x (flag=%x):\n",
               (unsigned)sched_next_rsp, (unsigned)sched_switch_flag);
        for (int i = 0; i < 24; i += 4)
            printf("    [%d]=%08x [%d]=%08x [%d]=%08x [%d]=%08x\n",
                   i, (unsigned)f[i], i + 1, (unsigned)f[i + 1],
                   i + 2, (unsigned)f[i + 2], i + 3, (unsigned)f[i + 3]);
    } else {
        static int seen[2] = {0, 0};
        if (!seen[sched_current - 1]) {
            seen[sched_current - 1] = 1;
            uint64_t *f = (uint64_t *)(uintptr_t)sched_next_rsp;
            printf("  [S%d] first-time pre-restore frame@%x:\n",
                   sched_current, (unsigned)sched_next_rsp);
            for (int i = 0; i < 24; i += 4)
                printf("    [%d]=%08x [%d]=%08x [%d]=%08x [%d]=%08x\n",
                       i, (unsigned)f[i], i + 1, (unsigned)f[i + 1],
                       i + 2, (unsigned)f[i + 2], i + 3, (unsigned)f[i + 3]);
        }
    }
    sched_yield_impl();  /* never returns here until this task is resumed */
}

void sched_apply_registry(void) {
    const char *v = reg_get("Sched.Mode");
    int mode = SCHED_MODE_PREEMPT;
    if (v && strcmp(v, "coop") == 0) mode = SCHED_MODE_COOP;
    sched_mode = mode;  /* direct set: no "live switch" banner at boot */
    printf("[SCHED] mode=%s (Sched.Mode=%s)\n",
           mode == SCHED_MODE_COOP ? "cooperative" : "preemptive",
           v ? v : "default");
}

static void sched_setup_frame(int idx, void (*entry)(void)) {
    SchedTask *t = &sched_tasks[idx];
    uint64_t *sp = (uint64_t *)((uintptr_t)t->stack + SCHED_STACK_SIZE);
    sp = (uint64_t *)((uintptr_t)sp & ~0xFULL);
    /* stub frame layout (low -> high), same as a real interrupt frame:
     *   rsp int_no err_code rip cs eflags (with 15 GPRs above rsp field) */
    *--sp = 0x10;                        /* ss */
    sp--;
    *sp = (uint64_t)(uintptr_t)(sp + 1); /* rsp (address of ss slot) */
    *--sp = 0x202;                        /* eflags: IF=1 */
    *--sp = 0x08;                         /* cs */
    *--sp = (uint64_t)(uintptr_t)entry;   /* rip */
    *--sp = 0;                            /* err_code */
    *--sp = 0;                            /* int_no */
    *--sp = 0;                            /* r15 */
    *--sp = 0;                            /* r14 */
    *--sp = 0;                            /* r13 */
    *--sp = 0;                            /* r12 */
    *--sp = 0;                            /* r11 */
    *--sp = 0;                            /* r10 */
    *--sp = 0;                            /* r9 */
    *--sp = 0;                            /* r8 */
    *--sp = 0;                            /* rbp */
    *--sp = 0;                            /* rdi */
    *--sp = 0;                            /* rsi */
    *--sp = 0;                            /* rdx */
    *--sp = 0;                            /* rcx */
    *--sp = 0;                            /* rbx */
    *--sp = 0;                            /* rax */
    sp--;
    *sp = (uint64_t)(uintptr_t)(sp + 1);  /* rsp field */
    sched_task_ctx[idx] = (uint64_t)(uintptr_t)sp;
}

static void sched_timer_handler(registers_t *r) {
    (void)r;
    sched_ticks++;
    if (sched_mode != SCHED_MODE_PREEMPT) return;  /* cooperative: time only */
    if (sched_task_count < 2) return;
    if (r->rip >= (uint64_t)(uintptr_t)context_switch &&
        r->rip <  (uint64_t)(uintptr_t)context_switch_end)
        return;  /* task is mid-context_switch: don't capture a half-switched frame */
    /* r points at the rsp-field slot, matching the switch-path pops */
    sched_task_ctx[sched_current] = (uint64_t)(uintptr_t)r;
    sched_current = (sched_current + 1) % sched_task_count;
    sched_next_rsp = sched_task_ctx[sched_current];
    sched_switch_flag = 1;
}

int sched_create(const char *name, void (*entry)(void)) {
    if (sched_task_count >= SCHED_MAX_TASKS || !entry) return -1;
    SchedTask *t = &sched_tasks[sched_task_count];
    strncpy(t->name, name, 15);
    t->name[15] = '\0';
    sched_setup_frame(sched_task_count, entry);
    sched_task_count++;
    return sched_task_count - 1;
}

static int sched_print_enabled(void) {
    const char *v = reg_get("Sched.Print");
    return v && *v == '1';
}

static void sched_demo_a(void) __attribute__((unused));
static void sched_demo_a(void) {
    uint32_t n = 0;
    uint32_t last = 0;
    for (;;) {
        uint32_t now = (uint32_t)sched_ticks;
        if ((uint32_t)(now - last) >= 100) {
            last = now;
            if (sched_print_enabled())
                printf("[SCHED] task-a: count=%u tick=%u\n", n++, now);
        }
        sched_yield();
    }
}

static void sched_demo_b(void) __attribute__((unused));
static void sched_demo_b(void) {
    uint32_t n = 0;
    uint32_t last = 0;
    for (;;) {
        uint32_t now = (uint32_t)sched_ticks;
        if ((uint32_t)(now - last) >= 100) {
            last = now;
            if (sched_print_enabled())
                printf("[SCHED] task-b: count=%u tick=%u\n", n++, now);
        }
        sched_yield();
    }
}

int sched_init(void) {
    if (sched_task_count != 0) return 0;

    irq_install_handler(0, sched_timer_handler);
    sched_current = 0;               /* slot 0 = main flow */
    sched_task_ctx[0] = 0;
    sched_task_count = 1;
    sched_create("demo-a", sched_demo_a);
    sched_create("demo-b", sched_demo_b);

    /* PIT channel 0: mode 2 (rate generator), divisor 11932 -> 100 Hz */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, 11932 & 0xFF);
    outb(PIT_CH0, (11932 >> 8) & 0xFF);

    /* unmask IRQ0 (keep IRQ1); ticks start from here */
    outb(PIC1_DATA, inb(PIC1_DATA) & ~0x01); /* unmask IRQ0, keep existing (IRQ1+IRQ2) */

    printf("[SCHED] Preemptive scheduler: %d task(s) @ 100 Hz\n", sched_task_count);
    return 0;
}

static Driver sched_driver = {
    .name = "sched",
    .type = DRV_TYPE_SCHEDULER,
    .init = sched_init,
};

int sched_driver_register(void) {
    return driver_register(&sched_driver);
}

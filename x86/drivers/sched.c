/* ===== x86/drivers/sched.c: preemptive/cooperative scheduler driver =====
 *
 * Preemptive mode (Sched.Mode=preempt, the default): the PIT (channel 0,
 * 100 Hz) raises IRQ0; the timer handler saves the interrupted task's
 * register frame (the stub's stack frame), picks the next task
 * round-robin, and asks the IRQ stub (interrupt.s) to restore that task's
 * frame: `mov sched_next_esp, %esp; popa; add $8, %esp; iret`.
 *
 * Cooperative mode (Sched.Mode=coop): the PIT still ticks (for timing) but
 * never switches tasks; each task must call sched_yield() to hand the CPU
 * to the next task. The main flow (shell) yields while idle in gets().
 *
 * The mode is taken from the registry at boot (sched_apply_registry(),
 * called after reg_init) -- changing Sched.Mode requires a restart.
 *
 * Each task owns a kernel stack; its "context" is either a stub frame
 * (preemptive) or a saved register set (cooperative). Slot 0 is the main
 * flow (kernel_main / shell); extra slots are created via sched_create().
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
unsigned int sched_switch_flag = 0;
unsigned int sched_next_esp = 0;

volatile uint32_t sched_ticks = 0;

uint32_t sched_get_ticks(void) {
    return sched_ticks;
}

static int sched_mode = SCHED_MODE_PREEMPT;

/* Cooperative context: register set layout matching context_switch()
 * in switch.s: { eip, esp, ebp, edi, esi, ebx }. */
typedef struct {
    uint32_t eip, esp, ebp, edi, esi, ebx;
} CoopCtx;

typedef struct {
    char name[16];
    uint32_t context_esp;   /* preemptive: ESP at the stub frame (registers_t) */
    CoopCtx coop_ctx;       /* cooperative: saved registers */
    char stack[SCHED_STACK_SIZE];
} SchedTask;

static SchedTask sched_tasks[SCHED_MAX_TASKS];
static int sched_task_count = 0;
static int sched_current = 0;

int sched_get_mode(void) {
    return sched_mode;
}

/* defined in switch.s: saves old, restores new (never returns to caller) */
extern void context_switch(void *old, void *new);

/* Cooperative yield: hand the CPU to the next task. No-op in preemptive
 * mode (the timer does the switching). */
void sched_yield(void) {
    if (sched_mode != SCHED_MODE_COOP) return;
    if (sched_task_count < 2) return;
    int old = sched_current;
    sched_current = (sched_current + 1) % sched_task_count;
    if (sched_current == old) return;
    context_switch(&sched_tasks[old].coop_ctx, &sched_tasks[sched_current].coop_ctx);
}

void sched_apply_registry(void) {
    const char *v = reg_get("Sched.Mode");
    if (v && strcmp(v, "coop") == 0) {
        sched_mode = SCHED_MODE_COOP;
        printf("[SCHED] mode=cooperative (Sched.Mode=%s)\n", v);
    } else {
        sched_mode = SCHED_MODE_PREEMPT;
        printf("[SCHED] mode=preemptive (Sched.Mode=%s)\n", v ? v : "default");
    }
}

static void sched_setup_frame(SchedTask *t, void (*entry)(void)) {
    uint32_t *sp = (uint32_t *)((uint32_t)t->stack + SCHED_STACK_SIZE);
    sp = (uint32_t *)((uint32_t)sp & ~0xF);
    uint32_t frame_base = (uint32_t)sp - 17 * 4; /* 17 dwords total */
    *--sp = 0x202;                 /* eflags: IF=1 */
    *--sp = 0x08;                  /* cs */
    *--sp = (uint32_t)entry;       /* eip */
    *--sp = 0;                     /* err_code */
    *--sp = 0;                     /* int_no */
    *--sp = 0;                     /* edi */
    *--sp = 0;                     /* esi */
    *--sp = 0;                     /* ebp */
    *--sp = frame_base + 52;       /* esp: task's initial stack pointer */
    *--sp = 0;                     /* ebx */
    *--sp = 0;                     /* edx */
    *--sp = 0;                     /* ecx */
    *--sp = 0;                     /* eax */
    *--sp = 0x10;                  /* ds */
    *--sp = 0x10;                  /* es */
    *--sp = 0x10;                  /* fs */
    *--sp = 0x10;                  /* gs */
    t->context_esp = (uint32_t)sp;
}

static void sched_timer_handler(registers_t *r) {
    (void)r;
    sched_ticks++;
    if (sched_mode != SCHED_MODE_PREEMPT) return;  /* cooperative: time only */
    if (sched_task_count < 2) return;
    sched_tasks[sched_current].context_esp = (uint32_t)r;
    sched_current = (sched_current + 1) % sched_task_count;
    sched_next_esp = sched_tasks[sched_current].context_esp;
    sched_switch_flag = 1;
}

int sched_create(const char *name, void (*entry)(void)) {
    if (sched_task_count >= SCHED_MAX_TASKS || !entry) return -1;
    SchedTask *t = &sched_tasks[sched_task_count];
    strncpy(t->name, name, 15);
    t->name[15] = '\0';
    sched_setup_frame(t, entry);
    t->coop_ctx.eip = (uint32_t)entry;
    t->coop_ctx.esp = ((uint32_t)t->stack + SCHED_STACK_SIZE) & ~0xFu;
    t->coop_ctx.ebp = 0;
    t->coop_ctx.edi = 0;
    t->coop_ctx.esi = 0;
    t->coop_ctx.ebx = 0;
    sched_task_count++;
    return sched_task_count - 1;
}

static int sched_print_enabled(void) {
    const char *v = reg_get("Sched.Print");
    return v && *v == '1';
}

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
    sched_tasks[0].context_esp = 0;
    sched_task_count = 1;
    sched_create("demo-a", sched_demo_a);
    sched_create("demo-b", sched_demo_b);

    /* PIT channel 0: mode 2 (rate generator), divisor 11932 -> 100 Hz */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, 11932 & 0xFF);
    outb(PIT_CH0, (11932 >> 8) & 0xFF);

    /* unmask IRQ0 (keep IRQ1); ticks start from here */
    outb(PIC1_DATA, inb(PIC1_DATA) & ~0x01); /* unmask IRQ0, keep existing (IRQ1+IRQ2) */

    printf("[SCHED] Preemptive scheduler: %d tasks @ 100 Hz\n", sched_task_count);
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

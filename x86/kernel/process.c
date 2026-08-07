#include <process.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static Process procs[MAX_PROC];
static int proc_count = 0;
static int current = 0;

extern void context_switch(Context *old, Context *new);
extern void proc_entry_wrapper(void);

void proc_init(void) {
    proc_count = 0;
    current = 0;
}

void proc_entry(Process *p) {
    void (*entry)(void) = (void (*)(void))p->entry;
    entry();
}

int proc_create(const char *name, uint32_t entry) {
    if (proc_count >= MAX_PROC) return -1;
    Process *p = &procs[proc_count];
    strncpy(p->name, name, PROC_NAME_LEN - 1);
    p->name[PROC_NAME_LEN - 1] = '\0';
    p->state = PROC_READY;
    p->entry = entry;
    p->stack = malloc(PROC_STACK_SIZE);
    if (!p->stack) return -1;
    uint32_t *sp = (uint32_t *)((char *)p->stack + PROC_STACK_SIZE);
    *--sp = (uint32_t)p;
    p->context.eip = (uint32_t)proc_entry_wrapper;
    p->context.esp = (uint32_t)sp;
    p->context.ebp = 0;
    p->context.edi = 0;
    p->context.esi = 0;
    p->context.ebx = 0;
    proc_count++;
    return proc_count - 1;
}

void proc_yield(void) {
    if (proc_count < 2) return;
    int old = current;
    do {
        current = (current + 1) % proc_count;
    } while (current != old && procs[current].state != PROC_READY);
    if (current == old) return;
    procs[old].state = PROC_READY;
    procs[current].state = PROC_RUNNING;
    context_switch(&procs[old].context, &procs[current].context);
}

void proc_exit(int code) {
    (void)code;
    Process *p = &procs[current];
    p->state = PROC_EXITED;
    if (p->stack) free(p->stack);
    p->stack = 0;
    for (int i = 0; i < proc_count; i++) {
        if (i != current && procs[i].state == PROC_READY) {
            current = i;
            procs[current].state = PROC_RUNNING;
            context_switch(&p->context, &procs[current].context);
        }
    }
    puts("[PROC] All processes exited.\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

void proc_list(void) {
    puts("  PID NAME                 STATE\n");
    for (int i = 0; i < proc_count; i++) {
        const char *state_str = "?";
        switch (procs[i].state) {
            case PROC_READY: state_str = "READY"; break;
            case PROC_RUNNING: state_str = "RUN"; break;
            case PROC_WAITING: state_str = "WAIT"; break;
            case PROC_EXITED: state_str = "EXIT"; break;
        }
        printf("  %3d %-20s %s\n", i, procs[i].name, state_str);
    }
}

Process *proc_current(void) {
    if (proc_count == 0) return 0;
    return &procs[current];
}

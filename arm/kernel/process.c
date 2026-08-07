#include <process.h>
#include <stdio.h>
#include <string.h>

static Process procs[MAX_PROC];
static int proc_count = 0;
static int current = 0;

void proc_init(void) {
    proc_count = 0;
    current = 0;
}

int proc_create(const char *name, uint32_t entry) {
    if (proc_count >= MAX_PROC) return -1;
    Process *p = &procs[proc_count];
    strncpy(p->name, name, PROC_NAME_LEN - 1);
    p->name[PROC_NAME_LEN - 1] = '\0';
    p->state = PROC_READY;
    p->entry = entry;
    p->stack = 0;
    proc_count++;
    return proc_count - 1;
}

void proc_yield(void) {
}

void proc_exit(int code) {
    (void)code;
    procs[current].state = PROC_EXITED;
}

void proc_list(void) {
    puts("  PID NAME                 STATE\n");
    for (int i = 0; i < proc_count; i++) {
        const char *st = "?";
        switch (procs[i].state) {
            case PROC_READY: st = "READY"; break;
            case PROC_RUNNING: st = "RUN"; break;
            case PROC_WAITING: st = "WAIT"; break;
            case PROC_EXITED: st = "EXIT"; break;
        }
        printf("  %3d %-20s %s\n", i, procs[i].name, st);
    }
}

Process *proc_current(void) {
    if (proc_count == 0) return 0;
    return &procs[current];
}

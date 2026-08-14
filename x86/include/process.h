#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>

#define MAX_PROC 16
#define PROC_NAME_LEN 24
#define PROC_STACK_SIZE 4096

typedef enum { PROC_READY, PROC_RUNNING, PROC_WAITING, PROC_EXITED } ProcState;

typedef struct {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t rbx;
    uint64_t rflags;
} __attribute__((packed)) Context;

typedef struct {
    char name[PROC_NAME_LEN];
    ProcState state;
    Context context;
    uint64_t entry;
    void *stack;
} Process;

void proc_init(void);
int proc_create(const char *name, void (*entry)(void));
void proc_yield(void);
void proc_exit(int code);
void proc_list(void);
Process *proc_current(void);

#endif

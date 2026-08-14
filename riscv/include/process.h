#ifndef _PROCESS_H
#define _PROCESS_H

#include <stdint.h>

#define MAX_PROC 16
#define PROC_NAME_LEN 24
#define PROC_STACK_SIZE 4096

typedef enum { PROC_READY, PROC_RUNNING, PROC_WAITING, PROC_EXITED } ProcState;

typedef struct {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebp;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
} __attribute__((packed)) Context;

typedef struct {
    char name[PROC_NAME_LEN];
    ProcState state;
    Context context;
    uint32_t entry;
    uint32_t heap_base;
    uint32_t heap_size;
    void *stack;
} Process;

void proc_init(void);
int proc_create(const char *name, uint32_t entry);
void proc_yield(void);
void proc_exit(int code);
void proc_list(void);
Process *proc_current(void);

#endif

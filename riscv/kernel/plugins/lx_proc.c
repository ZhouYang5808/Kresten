/* LX 进程运行环境 (RISC-V 版): 无进程调度器(proc_yield 为空), 所以 guest
 * 与内核同步轮转: 内核建好栈帧后借助蹦床跳入 guest(U-mode),
 * guest 每次 ecall 自陷到 start.S trap_entry -> linux_syscall_dispatch,
 * 处理完 mret 返回 guest。exit_group 由 syscall 侧经 lx_resume_helper
 * (start.S) 直接恢复内核 sp/s0..s11/ra 回到本文件 lx_run_test 的恢复点。
 * RISC-V 注意: guest 会改写全部 GPR(含 s0=fp), 恢复由 lx_resume_helper
 * 从 lx_resume_save 还原 s0..s11 (内核编译器需 -fno-omit-frame-pointer)。 */

#include "lx_int.h"
#include <stdio.h>
#include <string.h>
#include <keyboard.h>

volatile uint64_t lx_entry = 0;
volatile int lx_finished = 0;

/* 恢复点 (exit_group 跳回内核的位置)。必须 64 位: start.S 蹦床对它们
 * 做 8 字节 sd/ld, 32 位定义会互相踩踏 (sd sp 会盖掉 lx_resume_pc)。 */
volatile uint64_t lx_resume_sp = 0;
volatile uint64_t lx_resume_pc = 0;
volatile uint64_t lx_resume_ra = 0;
volatile uint64_t lx_resume_save[12];   /* s0..s11, start.S 蹦床保存 */

/* 进程参数 (.bss; argv[0]=程序名) */
static char lx_prog_name[48];
static char lx_arg_buf[512];
static char *lx_argv_ptr[24];
static int lx_argc = 0;

static void lx_fill_auxv(uint64_t *out, int *count, const uint64_t *rand_ptr, const char *execfn) {
    const Elf64Hdr *h = (const Elf64Hdr *)(uintptr_t)LX_IMG_BASE;
    uint64_t phdr = LX_IMG_BASE + h->e_phoff;
    uint64_t v[56];
    int n = 0;
    v[n++] = 3;   v[n++] = phdr;                 /* AT_PHDR */
    v[n++] = 4;   v[n++] = 56;                   /* AT_PHENT */
    v[n++] = 5;   v[n++] = h->e_phnum;           /* AT_PHNUM */
    v[n++] = 6;   v[n++] = 4096;                 /* AT_PAGESZ */
    v[n++] = 7;   v[n++] = 0;                    /* AT_BASE */
    v[n++] = 9;   v[n++] = h->e_entry;           /* AT_ENTRY */
    v[n++] = 11;  v[n++] = 0;                    /* AT_UID */
    v[n++] = 12;  v[n++] = 0;                    /* AT_EUID */
    v[n++] = 13;  v[n++] = 0;                    /* AT_GID */
    v[n++] = 14;  v[n++] = 0;                    /* AT_EGID */
    v[n++] = 16;  v[n++] = 0x3F;                 /* AT_HWCAP (基本扩展) */
    v[n++] = 17;  v[n++] = 100;                  /* AT_CLKTCK */
    v[n++] = 23;  v[n++] = 0;                    /* AT_SECURE */
    v[n++] = 25;  v[n++] = (uint64_t)(uintptr_t)rand_ptr;   /* AT_RANDOM */
    v[n++] = 31;  v[n++] = (uint64_t)(uintptr_t)execfn;     /* AT_EXECFN */
    v[n++] = 0;   v[n++] = 0;                    /* AT_NULL */
    for (int i = 0; i < n; i++) out[i] = v[i];
    *count = n;
}

static uint64_t lx_build_stack(void) {
    uint64_t auxv[56];
    int auxn = 0;
    uint64_t *top = (uint64_t *)(LX_STACK_BASE + LX_STACK_SIZE);

    uint32_t rnd[4];
    rnd[0] = 0x4c584b52u ^ (uint32_t)(uintptr_t)&rnd;
    rnd[1] = 0x45535453u ^ (uint32_t)(uintptr_t)top;
    rnd[2] = 0x4b524553u ^ 0xdeadbeefu;
    rnd[3] = 0x54454e31u ^ 0x12345678u;
    uint8_t *rand_ptr = (uint8_t *)top - 16;
    memcpy(rand_ptr, rnd, 16);

    size_t plen = strlen(lx_prog_name) + 1;
    uint8_t *execfn = rand_ptr - ((plen + 15) & ~15u);
    memcpy(execfn, lx_prog_name, plen);

    uint8_t *argstr = execfn - 512;
    memcpy(argstr, lx_arg_buf, 512);

    uint64_t *argvp = (uint64_t *)(((uint64_t)(uintptr_t)argstr - (uint64_t)(lx_argc + 1) * 8) & ~7ull);
    for (int i = 0; i < lx_argc; i++) {
        if (lx_argv_ptr[i] == lx_prog_name)
            argvp[i] = (uint64_t)(uintptr_t)execfn;
        else
            argvp[i] = (uint64_t)(uintptr_t)(argstr + (lx_argv_ptr[i] - lx_arg_buf));
    }
    argvp[lx_argc] = 0;
    top = argvp;

    lx_fill_auxv(auxv, &auxn, (uint64_t *)rand_ptr, (const char *)execfn);
    for (int i = auxn - 1; i >= 0; i--) *--top = auxv[i];
    *--top = 0;                                 /* envp[0] = NULL */
    *--top = 0;                                 /* argv[argc] = NULL */
    for (int i = lx_argc - 1; i >= 0; i--) *--top = argvp[i];
    *--top = (uint64_t)lx_argc;                 /* argc */
    /* SP 16 对齐 (RISC-V ABI) 且必须指向 argc 槽: argc 槽若未 16 对齐,
     * 在其下压一格 pad 并把 sp 指到 argc 槽 (pad+8)。 */
    uint64_t sp = (uint64_t)(uintptr_t)top;
    if ((sp & 0xF) != 0) {
        *--top = 0;
        sp = (uint64_t)(uintptr_t)(top + 1);
    }
    return sp;
}

int lx_run_test(const LxTest *t, int argc, char **argv) {
    printf("[LX] t: name=%s size=%u elf=%p\n", t->name, (unsigned)t->size,
           (const void *)(uintptr_t)t->elf);
    uint64_t entry = 0;
    if (lx_elf_load(t->elf, t->size, &entry, t->name) != 0) return -1;
    lx_entry = entry;
    lx_finished = 0;
    if (argc < 1) argc = 1;
    if (argc > 23) argc = 23;
    memset(lx_arg_buf, 0, sizeof(lx_arg_buf));
    memset(lx_argv_ptr, 0, sizeof(lx_argv_ptr));
    strncpy(lx_prog_name, t->name, sizeof(lx_prog_name) - 1);
    lx_prog_name[sizeof(lx_prog_name) - 1] = '\0';
    lx_argv_ptr[0] = lx_prog_name;
    int n = 1;
    size_t used = 0;
    for (int i = 1; i < argc && n < 23; i++) {
        if (!argv || !argv[i]) break;
        size_t len = strlen(argv[i]) + 1;
        if (used + len > sizeof(lx_arg_buf)) break;
        memcpy(lx_arg_buf + used, argv[i], len);
        lx_argv_ptr[n++] = lx_arg_buf + used;
        used += len;
    }
    lx_argc = n;
    printf("[LX] running '%s' entry=0x%08lx (argc=%d)\n", t->name,
           entry, n);
    uint64_t gsp = lx_build_stack();
    keyboard_flush();   /* 清掉加载期间收集的键 */
    __asm__ volatile ("mv %0, sp" : "=r"(lx_resume_sp));
    __asm__ volatile ("mv %0, ra" : "=r"(lx_resume_ra));
    lx_resume_pc = (uint32_t)(uintptr_t)&&lx_resume_point;
    lx_jump_to_entry(entry, gsp, (uint64_t)lx_argc, gsp + 8);
lx_resume_point:
    printf("[LX] '%s' exited\n", t->name);
    return 0;
}
/* LX 进程运行环境 (ARM 版): 无进程调度器(proc_yield 为空), 所以 guest
 * 与内核同步轮转: 内核建好栈帧后借助蹦床跳入 guest(USR 模式),
 * guest 每次 svc 自陷到 svc_handler -> lx_syscall_dispatch, 处理完
 * movs pc,lr 返回 guest。exit_group 由 syscall 侧经 lx_resume_helper
 * 直接恢复内核 sp/pc 回到本文件 lx_run_test 的恢复点。 */

#include "lx_int.h"
#include <stdio.h>
#include <string.h>
#include <keyboard.h>

volatile uint32_t lx_entry = 0;
volatile int lx_finished = 0;

/* 恢复点 (exit_group 跳回内核的位置) */
volatile uint32_t lx_resume_sp = 0;
volatile uint32_t lx_resume_pc = 0;
volatile uint32_t lx_resume_fp = 0;   /* guest 会改写 r11, 恢复前必须还原内核 fp */

/* 进程参数 (.bss; argv[0]=程序名) */
static char lx_prog_name[48];
static char lx_arg_buf[512];
static char *lx_argv_ptr[24];
static int lx_argc = 0;

/* 蹦床: 清零寄存器, 切 USR 模式(保留中断屏蔽), 设 guest 栈, 跳 ELF entry。
 * r0=entry r1=用户栈 r2=argc r3=argv。ARM 无 MMU, USR/SVC 只是模式位。 */
__attribute__((naked, used, noinline))
void lx_jump_to_entry(uint32_t entry, uint32_t rsp, uint32_t argc, uint32_t argv) {
    __asm__ volatile (
        "mov r4, r0\n\t"        /* 保存 entry */
        "mov r5, r1\n\t"        /* 保存 guest 栈 */
        "mov r6, r2\n\t"        /* argc */
        "mov r7, r3\n\t"        /* argv */
        "mrs r0, cpsr\n\t"
        "bic r0, r0, #0x1F\n\t" /* 清模式位 */
        "orr r0, r0, #0x10\n\t" /* USR 模式 (SVC 自陷时 lr_svc/sp_svc 可被覆盖) */
        "msr cpsr_c, r0\n\t"
        "mov sp, r5\n\t"
        "mov r0, r6\n\t"
        "mov r1, r7\n\t"
        "mov r2, #0\n\t"
        "mov r3, #0\n\t"
        "mov r8, #0\n\t"
        "mov r9, #0\n\t"
        "mov r10, #0\n\t"
        "mov r11, #0\n\t"
        "mov r12, #0\n\t"
        "mov lr, #0\n\t"
        "bx r4\n\t"
    );
}

/* exit_group 恢复入口: 从 svc 陷阱栈切回内核栈, 跳 lx_resume_pc。
 * sp 已在 svc_handler 存入 lx_save_sp; r11(fp) 被 guest 破坏,
 * 用 lx_run_test 跳转前保存的 lx_resume_fp 还原, 否则恢复点代码
 * 的 fp 相对寻址会读到 guest 栈, pop {fp,pc} 会弹出 guest 的 lr。 */
__attribute__((naked, used, noinline))
void lx_resume_helper(void) {
    __asm__ volatile (
        "ldr r12, =lx_save_sp\n\t"
        "ldr sp, [r12]\n\t"
        "ldr r12, =lx_resume_fp\n\t"
        "ldr fp, [r12]\n\t"
        "ldr r12, =lx_resume_pc\n\t"
        "ldr pc, [r12]\n\t"
    );
}

static void lx_fill_auxv(uint32_t *out, int *count, const uint32_t *rand_ptr, const char *execfn) {
    const Elf32Hdr *h = (const Elf32Hdr *)(uintptr_t)LX_IMG_BASE;
    uint32_t phdr = LX_IMG_BASE + h->e_phoff;
    uint32_t v[56];
    int n = 0;
    v[n++] = 3;   v[n++] = phdr;                 /* AT_PHDR */
    v[n++] = 5;   v[n++] = h->e_phnum;           /* AT_PHNUM */
    v[n++] = 6;   v[n++] = 4096;                 /* AT_PAGESZ */
    v[n++] = 7;   v[n++] = 0;                    /* AT_BASE */
    v[n++] = 9;   v[n++] = h->e_entry;           /* AT_ENTRY */
    v[n++] = 11;  v[n++] = 0;                    /* AT_UID */
    v[n++] = 12;  v[n++] = 0;                    /* AT_EUID */
    v[n++] = 13;  v[n++] = 0;                    /* AT_GID */
    v[n++] = 14;  v[n++] = 0;                    /* AT_EGID */
    v[n++] = 23;  v[n++] = 0;                    /* AT_SECURE */
    v[n++] = 25;  v[n++] = (uint32_t)(uintptr_t)rand_ptr;   /* AT_RANDOM */
    v[n++] = 31;  v[n++] = (uint32_t)(uintptr_t)execfn;     /* AT_EXECFN */
    v[n++] = 0;   v[n++] = 0;                    /* AT_NULL */
    for (int i = 0; i < n; i++) out[i] = v[i];
    *count = n;
}

static uint32_t lx_build_stack(void) {
    uint32_t auxv[56];
    int auxn = 0;
    uint32_t *top = (uint32_t *)(LX_STACK_BASE + LX_STACK_SIZE);

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

    uint32_t *argvp = (uint32_t *)(argstr - 24 * 4);
    for (int i = 0; i < lx_argc; i++) {
        if (lx_argv_ptr[i] == lx_prog_name)
            argvp[i] = (uint32_t)(uintptr_t)execfn;
        else
            argvp[i] = (uint32_t)(uintptr_t)(argstr + (lx_argv_ptr[i] - lx_arg_buf));
    }
    argvp[lx_argc] = 0;
    top = argvp;

    lx_fill_auxv(auxv, &auxn, (uint32_t *)rand_ptr, (const char *)execfn);
    for (int i = auxn - 1; i >= 0; i--) *--top = auxv[i];
    *--top = 0;                                 /* envp[0] = NULL */
    *--top = 0;                                 /* argv[argc] = NULL */
    for (int i = lx_argc - 1; i >= 0; i--) *--top = argvp[i];
    *--top = (uint32_t)lx_argc;                 /* argc */
    /* SP 8 对齐: 刚入栈 4B argc, 再退一格保持 sp%8==0 */
    *--top = 0;
    return (uint32_t)(uintptr_t)(top + 1);
}

int lx_run_test(const LxTest *t, int argc, char **argv) {
    printf("[LX] t: name=%s size=%u elf=%p\n", t->name, (unsigned)t->size,
           (const void *)(uintptr_t)t->elf);
    lx_session_reset();     /* 清上一会话残留的 brk/mmap 游标 */
    uint32_t entry = 0;
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
    printf("[LX] running '%s' entry=0x%08x (argc=%d)\n", t->name, (unsigned)entry, n);
    uint32_t gsp = lx_build_stack();
    keyboard_flush();   /* 清掉加载期间收集的键 */
    __asm__ volatile ("mov %0, sp" : "=r"(lx_resume_sp));
    __asm__ volatile ("mov %0, fp" : "=r"(lx_resume_fp));
    lx_resume_pc = (uint32_t)(uintptr_t)&&lx_resume_point;
    lx_jump_to_entry(entry, gsp, (uint32_t)lx_argc, (uint32_t)(uintptr_t)(gsp + 4));
lx_resume_point:
    printf("[LX] '%s' exited\n", t->name);
    return 0;
}
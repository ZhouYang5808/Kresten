/* LX 进程运行环境：proc_create 注册（ps/yield 可见）+ Linux 启动栈帧
 * + 蹦床跳转到 ELF entry。 */

#include "lx_int.h"
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <keyboard.h>

volatile uint64_t lx_entry = 0;
volatile int lx_finished = 0;

/* 进程参数（.bss，identity 映射下用户可读；argv[0]=程序名） */
static char lx_prog_name[48];
static char lx_arg_buf[512];
static char *lx_argv_ptr[24];
static int lx_argc = 0;

/* 蹦床：清空寄存器，iretq 降权到 ring3（CS=0x28|3, SS=0x30|3）跳到 ELF entry。
 * rdi=entry, rsi=用户栈, rdx=argc, rcx=argv；rflags IF=1（中断走 TSS.RSP0）。 */
__attribute__((naked, used, noinline))
void lx_jump_to_entry(uint64_t entry, uint64_t rsp, uint64_t argc, uint64_t argv) {
    __asm__ volatile (
        "movq %rdi, %r10\n\t"   /* 保存 entry */
        "movq %rsi, %r9\n\t"    /* 保存用户栈 */
        "movq %rdx, %rdi\n\t"   /* argc */
        "movq %rcx, %rsi\n\t"   /* argv */
        "xorq %rax, %rax\n\t"
        "xorq %rbx, %rbx\n\t"
        "xorq %rbp, %rbp\n\t"
        "xorq %r8, %r8\n\t"
        "xorq %r12, %r12\n\t"
        "xorq %r13, %r13\n\t"
        "xorq %r14, %r14\n\t"
        "xorq %r15, %r15\n\t"
        "xorq %rdx, %rdx\n\t"
        "xorq %rcx, %rcx\n\t"
        "movq $0x202, %r11\n\t" /* rflags: IF=1 */
        "pushq $0x33\n\t"       /* user ss */
        "pushq %r9\n\t"
        "pushq %r11\n\t"
        "pushq $0x2B\n\t"       /* user cs */
        "pushq %r10\n\t"
        "iretq\n\t"
    );
}

static void lx_fill_auxv(uint64_t *out, int *count, const void *rand_ptr, const void *execfn) {
    /* Linux auxv：AT_PHDR/AT_PHNUM 从镜像区 ELF 头读取 */
    const Elf64Hdr *h = (const Elf64Hdr *)(uintptr_t)LX_IMG_BASE;
    uint64_t phdr = LX_IMG_BASE + h->e_phoff;
    uint64_t v[48];
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
    v[n++] = 25;  v[n++] = (uint64_t)(uintptr_t)rand_ptr;   /* AT_RANDOM */
    v[n++] = 31;  v[n++] = (uint64_t)(uintptr_t)execfn;     /* AT_EXECFN */
    v[n++] = 0;   v[n++] = 0;                    /* AT_NULL */
    for (int i = 0; i < n; i++) out[i] = v[i];
    *count = n;
}

void lx_thread_main(void) {
    /* Linux ABI 启动栈帧（自栈底 0x1840000 向下排布，全部落在用户栈页内）：
     *   [AT_RANDOM 16B][AT_EXECFN 字符串][argv 字符串区][argv 指针数组]
     *   [auxv...AT_NULL][envp NULL][argv[argc] NULL][argv...][argc] ← 栈顶 */
    uint64_t auxv[48];
    int auxn = 0;
    uint64_t *top = (uint64_t *)(LX_STACK_BASE + LX_STACK_SIZE);

    uint64_t rnd[2];
    rnd[0] = 0x4c584b5245535453ULL ^ (uint64_t)(uintptr_t)&rnd;
    rnd[1] = 0x4b52455354454e31ULL ^ (uint64_t)(uintptr_t)top;
    uint8_t *rand_ptr = (uint8_t *)top - 16;
    memcpy(rand_ptr, rnd, 16);

    size_t plen = strlen(lx_prog_name) + 1;
    uint8_t *execfn = rand_ptr - ((plen + 15) & ~15ULL);
    memcpy(execfn, lx_prog_name, plen);

    uint8_t *argstr = execfn - 512;
    memcpy(argstr, lx_arg_buf, 512);

    uint64_t *argvp = (uint64_t *)(argstr - 24 * 8);
    for (int i = 0; i < lx_argc; i++) {
        if (lx_argv_ptr[i] == lx_prog_name)
            argvp[i] = (uint64_t)(uintptr_t)execfn;     /* argv[0] = 程序名副本 */
        else
            argvp[i] = (uint64_t)(uintptr_t)(argstr + (lx_argv_ptr[i] - lx_arg_buf));
    }
    argvp[lx_argc] = 0;
    top = argvp;

    lx_fill_auxv(auxv, &auxn, rand_ptr, execfn);
    for (int i = auxn - 1; i >= 0; i--) *--top = auxv[i];  /* auxv（含 AT_NULL） */
    *--top = 0;                                 /* envp[0] = NULL */
    *--top = 0;                                 /* argv[argc] = NULL */
    for (int i = lx_argc - 1; i >= 0; i--) *--top = argvp[i];
    *--top = (uint64_t)lx_argc;                 /* argc */
    keyboard_flush();   /* 清掉 ELF 加载/导入期间收集的键，避免流式灌入用户程序 */
    lx_jump_to_entry(lx_entry, (uint64_t)(uintptr_t)top,
                     (uint64_t)lx_argc,
                     (uint64_t)(uintptr_t)(top + 1));
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* 把 LX 运行区（镜像/brk/mmap/栈所在 2MiB 大页）标为用户可访问。
 * boot.s 的 identity 页表 pml4/pdpt/pd 各层 PTE 都是 supervisor（bit2=0），
 * ring3 访问会 #PF；权限检查逐层进行，三层都要置 U/S。 */
static void lx_mark_user_pages(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(uintptr_t)cr3;
    for (uint64_t v = LX_IMG_BASE; v < 0x01A00000ULL; v += 0x200000ULL) {
        int i = (v >> 39) & 0x1FF;
        int j = (v >> 30) & 0x1FF;
        int k = (v >> 21) & 0x1FF;
        pml4[i] |= 0x4;
        uint64_t *pdpt = (uint64_t *)(uintptr_t)(pml4[i] & ~0xFFFULL);
        pdpt[j] |= 0x4;
        uint64_t *pd = (uint64_t *)(uintptr_t)(pdpt[j] & ~0xFFFULL);
        pd[k] |= 0x4;
    }
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

int lx_run_test(const LxTest *t, int argc, char **argv) {
    printf("[LX] t: name=%s size=%u elf=%p\n", t->name, (unsigned)t->size, (const void *)(uintptr_t)t->elf);
    lx_mark_user_pages();
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
    printf("[LX] running '%s' entry=0x%08x%08x (argc=%d)\n", t->name,
           (unsigned)(entry >> 32), (unsigned)entry, n);
    if (proc_create(t->name, lx_thread_main) < 0) {
        printf("[LX] proc_create failed\n");
        return -1;
    }
    while (!lx_finished)
        proc_yield();           /* 切到 LX 进程；exit_group 置标志并切回 */
    printf("[LX] '%s' exited\n", t->name);
    return 0;
}

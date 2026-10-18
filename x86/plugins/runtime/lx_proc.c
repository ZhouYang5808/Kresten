/* LX 进程运行环境：proc_create 注册（ps/yield 可见）+ Linux 启动栈帧
 * + 蹦床跳转到 ELF entry。 */

#include "lx_int.h"
#include <winefs.h>
#include <iso9660.h>
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

/* wine 模式 auxv 源 (由 lx_run_wine 填充; wine_mode=1 时 ld.so 用) */
static int wine_mode = 0;
static uint64_t wine_auxv_main_base = 0;   /* wine loader 装载基 */
static uint64_t wine_auxv_main_phoff = 0;
static unsigned wine_auxv_main_phnum = 0;
static uint64_t wine_auxv_main_entry = 0;
static uint64_t wine_auxv_ldso_base = 0;

static void lx_setup_args(int argc, char **argv) {
    if (argc < 1) argc = 1;
    if (argc > 23) argc = 23;
    memset(lx_arg_buf, 0, sizeof(lx_arg_buf));
    memset(lx_argv_ptr, 0, sizeof(lx_argv_ptr));
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
}

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
    uint64_t phdr, phnum, entry, base;
    if (wine_mode) {
        /* ld.so 视角: AT_PHDR/AT_ENTRY 指 wine loader, AT_BASE 指 ld.so 自身装载基 */
        phdr  = wine_auxv_main_base + wine_auxv_main_phoff;
        phnum = wine_auxv_main_phnum;
        entry = wine_auxv_main_entry;
        base  = wine_auxv_ldso_base;
    } else {
        /* busybox 等 ET_EXEC: 直载 vaddr, phdr 在镜像区 ELF 头 */
        const Elf64Hdr *h = (const Elf64Hdr *)(uintptr_t)LX_IMG_BASE;
        phdr  = LX_IMG_BASE + h->e_phoff;
        phnum = h->e_phnum;
        entry = h->e_entry;
        base  = 0;
    }
    /* Linux auxv：AT_PHDR/AT_PHNUM 从镜像区 ELF 头读取 */
    uint64_t v[48];
    int n = 0;
    v[n++] = 3;   v[n++] = phdr;                 /* AT_PHDR */
    v[n++] = 5;   v[n++] = phnum;                /* AT_PHNUM */
    v[n++] = 6;   v[n++] = 4096;                 /* AT_PAGESZ */
    v[n++] = 7;   v[n++] = base;                 /* AT_BASE */
    v[n++] = 9;   v[n++] = entry;                /* AT_ENTRY */
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
    /* SysV ABI: 进程入口 rsp%16==0 (argc 在 rsp)。栈顶 0x1840000 本身 16 对齐,
     * 布局整体下移使 argc 落到 %16==0 地址。wine 模式 3 个 env 串 + NULL = 4 个
     * envp 槽(32B), 与 busybox 的 1 槽(8B) 差 24B=16 的整数倍, 同一 -16 起始即对齐。 */
    uint64_t *top = (uint64_t *)(LX_STACK_BASE + LX_STACK_SIZE - 16);

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
    if (wine_mode) {
        /* wine loader dlopen("ntdll.so") 等无路径名, 靠 LD_LIBRARY_PATH 定位 */
        const char ldp[] = "LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/wine/x86_64-unix";
        /* WINELOADERNOEXEC=1: 跳过 wine loader 的 execve 自重启 (LX 无 execve),
         * 直接进入 loader 模式 (ntdll/unix/loader.c __wine_main). */
        const char wle[] = "WINELOADERNOEXEC=1";
        /* HOME: 避免 init_paths 走 getpwuid (/etc/passwd+nscd socket) */
        const char home[] = "HOME=/root";
        /* envstr 放 argstr 缓冲尾部 (argstr+384..argstr+512): 位于所有压栈区
         * (auxv/envp/argv/argc, 均在 argvp 之下) 之上、guest 栈(自 argc 向下)之外,
         * 不会被任何写入覆盖。之前用 auxv 底部-128 落在压栈区内: WLE 恰被 argc
         * 覆盖、HOME 恰被 envp[0] 覆盖 (env1 打印 '\x02'=argc=2 即铁证)。 */
        uint8_t *envstr = argstr + (512 - 128);
        memcpy(envstr, ldp, sizeof(ldp));
        memcpy(envstr + 64, wle, sizeof(wle));
        memcpy(envstr + 96, home, sizeof(home));
        *--top = 0;                             /* envp[3] = NULL 终止（先写, 高地址） */
        *--top = (uint64_t)(uintptr_t)(envstr + 96);   /* envp[2] = HOME */
        *--top = (uint64_t)(uintptr_t)(envstr + 64);   /* envp[1] = WINELOADERNOEXEC */
        *--top = (uint64_t)(uintptr_t)envstr;   /* envp[0] = LD_LIBRARY_PATH（后写, 栈顶方向） */
    } else {
        *--top = 0;                             /* envp[0] = NULL */
    }
    *--top = 0;                                 /* argv[argc] = NULL */
    for (int i = lx_argc - 1; i >= 0; i--) *--top = argvp[i];
    *--top = (uint64_t)lx_argc;                 /* argc */
    keyboard_discard_pending();   /* 只清已收集的键; 不等待, 避免吞掉运行后才到达的键 */
    printf("[LX] jump: entry=0x%08x%08x rsp=0x%08x%08x argc=%u\n",
           (unsigned)(lx_entry >> 32), (unsigned)lx_entry,
           (unsigned)((uint64_t)(uintptr_t)top >> 32), (unsigned)(uintptr_t)top,
           (unsigned)lx_argc);
    if (wine_mode) {
        uint64_t *envp = (uint64_t *)((uintptr_t)top + 8 * ((uintptr_t)lx_argc + 1 + 1));
        printf("[LX] env0='%s' env1='%s' env2='%s'\n",
               (char *)(uintptr_t)envp[0], (char *)(uintptr_t)envp[1],
               (char *)(uintptr_t)envp[2]);
    }
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
    /* 镜像/brk/栈 + wine 装载区 + mmap 池 (0x400000..0x91000000;
     * 池到 0x9000_0000, glibc NOREPLACE 探测 0x8000_0000 起) */
    for (uint64_t v = LX_IMG_BASE; v < 0x91000000ULL; v += 0x200000ULL) {
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
    lx_session_reset();     /* 清上一会话残留的 fd/socket/mmap 槽 */
    lx_mark_user_pages();
    uint64_t entry = 0;
    wine_mode = 0;
    if (lx_elf_load(t->elf, t->size, &entry, 0, t->name, 1) != 0) return -1;
    lx_entry = entry;
    lx_finished = 0;
    strncpy(lx_prog_name, t->name, sizeof(lx_prog_name) - 1);
    lx_prog_name[sizeof(lx_prog_name) - 1] = '\0';
    lx_argv_ptr[0] = lx_prog_name;
    lx_setup_args(argc, argv);
    printf("[LX] running '%s' entry=0x%08x%08x (argc=%d)\n", t->name,
           (unsigned)(entry >> 32), (unsigned)entry, lx_argc);
    if (proc_create(t->name, lx_thread_main) < 0) {
        printf("[LX] proc_create failed\n");
        return -1;
    }
    while (!lx_finished)
        proc_yield();           /* 切到 LX 进程；exit_group 置标志并切回 */
    printf("[LX] '%s' exited\n", t->name);
    return 0;
}

/* wine 运行时 (tiny ISO): 装载 wine loader (ET_DYN) -> PT_INTERP 指
 * ld-linux -> 由 ld.so 完成动态链接 (它用 mmap/open/read/fstat 加载
 * libc/wine *.so, 全部经 wine_iso_map 表落 ISO) 后经 AT_ENTRY 跳
 * wine loader; wine loader 再按 argv[1] 打开 PE。 */
int lx_run_wine(int argc, char **argv) {
    if (!winefs_ready()) {
        if (wine_import() != 0 || !winefs_ready()) {
            printf("[LX] wine: wine-tiny.iso (WINETINY) not found on CD\n");
            return -1;
        }
    }
    static uint8_t loader_buf[32 * 1024];
    static uint8_t ldso_buf[512 * 1024];
    iso_file_t iso;
    printf("[LX] wine: mount ok, lookup loader\n");
    if (iso_lookup("/WINE/WINE", &iso) != 0) {
        printf("[LX] wine: loader /WINE/WINE missing on tiny ISO\n");
        return -1;
    }
    printf("[LX] wine: loader found size=%u\n", (unsigned)iso.size);
    if (iso.size > sizeof(loader_buf) ||
        iso_read_file(&iso, loader_buf) != 0) {
        printf("[LX] wine: read loader failed\n");
        return -1;
    }
    printf("[LX] wine: loader read ok\n");
    uint32_t loader_size = iso.size;
    char interp[64];
    printf("[LX] wine: interp lookup\n");
    if (!lx_elf_interp(loader_buf, loader_size, interp, sizeof(interp))) {
        printf("[LX] wine: no PT_INTERP in loader\n");
        return -1;
    }
    printf("[LX] wine: interp '%s'\n", interp);
    char isopath[160];
    if (!lx_wine_iso_path(interp, isopath, sizeof(isopath))) {
        printf("[LX] wine: '%s' not in wine_iso_map\n", interp);
        return -1;
    }
    printf("[LX] wine: ldso on iso '%s'\n", isopath);
    if (iso_lookup(isopath, &iso) != 0) {
        printf("[LX] wine: ld-linux '%s' (%s) missing on tiny ISO\n",
               interp, isopath);
        return -1;
    }
    printf("[LX] wine: ldso found size=%u\n", (unsigned)iso.size);
    if (iso.size > sizeof(ldso_buf) ||
        iso_read_file(&iso, ldso_buf) != 0) {
        printf("[LX] wine: read ld-linux failed\n");
        return -1;
    }
    printf("[LX] wine: ldso read ok\n");
    uint32_t ldso_size = iso.size;

    lx_session_reset();     /* 清上一会话残留的 fd/socket/mmap 槽 */
    lx_mark_user_pages();
    uint64_t main_entry = 0, ld_entry = 0;
    if (lx_elf_load(loader_buf, loader_size, &main_entry,
                    LX_WINE_MAIN_BASE, "wine", 1) != 0)
        return -1;
    if (lx_elf_load(ldso_buf, ldso_size, &ld_entry,
                    LX_WINE_LDSO_BASE, "ld-linux", 0) != 0)
        return -1;

    const Elf64Hdr *mh = (const Elf64Hdr *)loader_buf;
    wine_mode = 1;
    wine_auxv_main_base  = LX_WINE_MAIN_BASE;
    wine_auxv_main_phoff = mh->e_phoff;
    wine_auxv_main_phnum = mh->e_phnum;
    wine_auxv_main_entry = main_entry;
    wine_auxv_ldso_base  = LX_WINE_LDSO_BASE;
    lx_entry = ld_entry;
    lx_finished = 0;
    strncpy(lx_prog_name, "wine", sizeof(lx_prog_name) - 1);
    lx_prog_name[sizeof(lx_prog_name) - 1] = '\0';
    lx_argv_ptr[0] = lx_prog_name;
    lx_setup_args(argc, argv);
    printf("[LX] running wine (ld.so@0x%08x%08x, loader@0x%08x%08x argc=%d)\n",
           (unsigned)(ld_entry >> 32), (unsigned)ld_entry,
           (unsigned)(main_entry >> 32), (unsigned)main_entry, lx_argc);
    if (proc_create("wine", lx_thread_main) < 0) {
        printf("[LX] wine: proc_create failed\n");
        wine_mode = 0;
        return -1;
    }
    while (!lx_finished)
        proc_yield();
    wine_mode = 0;
    printf("[LX] wine exited\n");
    return 0;
}

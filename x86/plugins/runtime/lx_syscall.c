/* LX syscall translation layer (WSL1 lxcore 对应物)。
 * 入口经 IA32_LSTAR 直挂（插件静态链接，代码位于内核代码区），
 * 把 Linux x86-64 `syscall` ABI 翻译为 Kresten 内核服务。 */

#include "lx_int.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <process.h>
#include <rtc.h>
#include <io.h>
#include <keyboard.h>
#include <rootfs.h>
#include <iso9660.h>
#include <stddef.h>

/* syscall 运行时状态（.bss）。注意：trap 栈必须声明在状态变量之后，
 * 保证 lea lx_trap_stack+8192 的栈顶与 iret 帧落在纯粹栈区，
 * 不与下方状态变量重叠（bss 按声明顺序连续分配）。 */
uint64_t linux_user_rsp = 0;
uint64_t linux_ret_rip = 0;
uint64_t linux_ret_rflags = 0;
uint8_t lx_trap_stack[8192] __attribute__((aligned(16)));

/* syscall 入口（LSTAR 目标，纯汇编，代码区性能路径）：
 *   进入：rax=调用号, rdi/rsi/rdx/r10/r8/r9=参数(按 Linux ABI)
 *         rcx=返回rip, r11=调用者rflags（CPU syscall 语义）
 *   翻译：切翻译栈 → cli 临界区 → C dispatch（SysV 参数位）
 *   返回：翻译栈上构造 iretq 帧 [rip cs rflags rsp ss] 弹回用户态 */
__attribute__((naked, used, noinline))
void linux_syscall_entry(void) {
    __asm__ volatile (
        "cli\n\t"
        "movq %rsp, linux_user_rsp(%rip)\n\t"
        "leaq lx_trap_stack+8192(%rip), %rsp\n\t"
        "movq %rcx, linux_ret_rip(%rip)\n\t"
        "movq %r11, linux_ret_rflags(%rip)\n\t"
        /* Linux ABI: syscall 只破坏 rcx/r11/rax；rdi/rsi/rdx/r8/r9/r10
         * 必须原样保留给用户态（musl 在 syscall 后直接使用它们） */
        "pushq %rdi\n\t"
        "pushq %rsi\n\t"
        "pushq %rdx\n\t"
        "pushq %r8\n\t"
        "pushq %r9\n\t"
        "pushq %r10\n\t"
        "movq %r8, %r9\n\t"
        "movq %r10, %r8\n\t"
        "movq %rdx, %rcx\n\t"
        "movq %rsi, %rdx\n\t"
        "movq %rdi, %rsi\n\t"
        "movq %rax, %rdi\n\t"
        "call linux_syscall_dispatch\n\t"
        "popq %r10\n\t"
        "popq %r9\n\t"
        "popq %r8\n\t"
        "popq %rdx\n\t"
        "popq %rsi\n\t"
        "popq %rdi\n\t"
        "pushq $0x33\n\t"
        "pushq linux_user_rsp(%rip)\n\t"
        "pushq linux_ret_rflags(%rip)\n\t"
        "pushq $0x2B\n\t"
        "pushq linux_ret_rip(%rip)\n\t"
        /* 不加 sti：iretq 弹出的 RFLAGS（linux_ret_rflags 含 IF=1）
         * 已恢复用户态 IF；若先 sti 再 iretq，PIT 可能在这 1-2 条指令
         * 窗口内触发同 CPL 中断，恢复路径 iretq 会多弹 2 项使 RSP+16，
         * 导致本 iretq 从错位位置弹帧而 #GP。 */
        "iretq\n\t"
    );
}

#define LX_ENOSYS (-38L)
#define LX_ENOMEM (-12L)
#define LX_EINVAL (-22L)
#define LX_EBADF  (-9L)
#define LX_ENOTTY (-25L)
#define LX_ENOENT (-2L)
#define LX_EMFILE (-24L)
#define LX_EACCES (-13L)
#define LX_ENOTDIR (-20L)
#define LX_EISDIR  (-21L)

/* rootfs 文件描述符表(定义提前: readv 也使用) */
#define LX_FD_MAX 32

typedef struct {
    int in_use;
    const rf_node *node;         /* rootfs 节点; tty fd 该字段为 NULL */
    uint32_t off;                /* 读偏移 */
    /* 光驱文件: node 为 NULL 且 iso_ready 非 0 时, 走 ISO 读取 */
    iso_file_t iso;              /* CD 上文件位置 */
    int iso_fd;                  /* 1 = ISO 打开 */
} lx_fd;

static lx_fd lx_fds[LX_FD_MAX];

/* ---- syscall 服务实现 ---- */

/* read(0)：从键盘阻塞读一字节（busybox sh 交互输入；只用 8042，
 * 不走 COM1——串口 LSR 在部分虚拟后端恒置位会读到垃圾字符）
 * read(fd>2)：从 rootfs 文件读取 */
static long lx_sc_read(long fd, long buf, long len) {
    if (!buf || len <= 0) return LX_EINVAL;
    if (fd == 0) {
        unsigned char c;
        for (;;) {
            int k = keyboard_getchar();
            if (k >= 0) { c = (unsigned char)k; break; }
            proc_yield();
        }
        *(char *)(uintptr_t)buf = (char)c;
        return 1;
    }
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    const rf_node *n = lx_fds[fd].node;
    if (lx_fds[fd].iso_fd) {
        if (lx_fds[fd].iso.flags & 2) return LX_EISDIR;
        long left = lx_fds[fd].iso.size - lx_fds[fd].off;
        if (left <= 0) return 0;
        if (len > left) len = left;
        if (iso_read_at(&lx_fds[fd].iso, lx_fds[fd].off, (void *)(uintptr_t)buf,
                        (uint32_t)len) != 0)
            return LX_EINVAL;
        lx_fds[fd].off += (uint32_t)len;
        return len;
    }
    if (!n || n->type == RF_TYPE_DIR) return LX_EISDIR;
    if (n->type == RF_TYPE_SYMLNK) return LX_EISDIR;
    long left = n->size - lx_fds[fd].off;
    if (left <= 0) return 0;
    if (len > left) len = left;
    memcpy((void *)(uintptr_t)buf, rf_data(n) + lx_fds[fd].off, (size_t)len);
    lx_fds[fd].off += (uint32_t)len;
    return len;
}

static long lx_sc_write(long fd, long buf, long len) {
    if ((fd != 1 && fd != 2) || !buf || len <= 0) return LX_EBADF;
    const char *s = (const char *)(uintptr_t)buf;
    long n = 0;
    while (n < len) {
        putchar(s[n]);
        n++;
    }
    return n;
}

static long lx_sc_brk(long addr) {
    static uint64_t brk_cur = LX_BRK_BASE;
    if (addr == 0) return (long)brk_cur;
    if ((uint64_t)addr < LX_BRK_BASE ||
        (uint64_t)addr > LX_BRK_BASE + LX_BRK_SIZE) {
        return (long)brk_cur;       /* 失败：保持原 brk */
    }
    brk_cur = (uint64_t)addr;
    return (long)brk_cur;
}

#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

static long lx_sc_mmap(long addr, long length, long prot, long flags, long fd) {
    (void)prot;
    uint64_t len = (uint64_t)length;
    if (len == 0) return LX_EINVAL;
    if ((flags & MAP_FIXED) || addr != 0) return LX_EINVAL;  /* v0: 匿名非固定 */
    len = (len + 4095) & ~4095ULL;
    static uint64_t mmap_cur = LX_MMAP_BASE;
    uint64_t p = mmap_cur;
    if (p + len > LX_MMAP_BASE + LX_MMAP_SIZE) return LX_ENOMEM;
    mmap_cur = p + len;
    memset((void *)(uintptr_t)p, 0, (size_t)len);
    if (!(flags & MAP_ANONYMOUS) && fd >= 3 && fd < LX_FD_MAX && lx_fds[fd].in_use) {
        const rf_node *n = lx_fds[fd].node;
        if (n && (n->type == RF_TYPE_FILE || n->type == RF_TYPE_SYMLNK)) {
            unsigned long nc = (unsigned long)n->size;
            if (nc > (unsigned long)len) nc = (unsigned long)len;
            memcpy((void *)(uintptr_t)p, rf_data(n), nc);
        }
    }
    return (long)p;
}

static long lx_sc_munmap(long addr, long length) {
    (void)addr;
    (void)length;
    return 0;                       /* v0: 池为线性分配，忽略释放 */
}

static long lx_sc_arch_prctl(long code, long addr) {
    if (code == 0x1002) {           /* ARCH_SET_FS: TLS 基址 */
        uint64_t fs = (uint64_t)(uintptr_t)addr;
        __asm__ volatile ("wrmsr"
                          : : "c"(0xC0000100ULL),
                              "a"((uint32_t)fs),
                              "d"((uint32_t)(fs >> 32))
                          : "memory");
        return 0;
    }
    if (code == 0x1003) {           /* ARCH_GET_FS */
        uint64_t fs;
        uint32_t lo, hi;
        __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100ULL));
        fs = ((uint64_t)hi << 32) | lo;
        if (addr) *(uint64_t *)(uintptr_t)addr = fs;
        return 0;
    }
    return LX_EINVAL;
}

typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} lx_utsname;

static long lx_sc_uname(long ptr) {
    lx_utsname *u = (lx_utsname *)(uintptr_t)ptr;
    if (!u) return LX_EINVAL;
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "Kresten");
    strcpy(u->nodename, "kresten");
    strcpy(u->release, "1.0-lx");
    strcpy(u->version, "LX subsystem");
    strcpy(u->machine, "x86_64");
    strcpy(u->domainname, "(none)");
    return 0;
}

static long lx_rtc_epoch(void) {
    RTC_Time t;
    rtc_read(&t);
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    long days = 10957;              /* 1970-01-01 -> 2000-01-01 */
    for (int y = 2000; y < t.year; y++)
        days += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
    for (int m = 1; m < t.month; m++)
        days += mdays[m - 1];
    if (t.month > 2 &&
        ((t.year % 4 == 0 && t.year % 100 != 0) || t.year % 400 == 0))
        days++;
    days += t.day - 1;
    return days * 86400L + t.hour * 3600L + t.minute * 60L + t.second;
}

static long lx_sc_time(long ptr) {
    long sec = lx_rtc_epoch();
    if (ptr) *(long *)(uintptr_t)ptr = sec;
    return sec;
}

typedef struct { long tv_sec; long tv_usec; } lx_timeval;
typedef struct { long tv_sec; long tv_nsec; } lx_timespec;

static long lx_sc_gettimeofday(long tv, long tz) {
    (void)tz;
    lx_timeval *p = (lx_timeval *)(uintptr_t)tv;
    if (!p) return LX_EINVAL;
    long sec = lx_rtc_epoch();
    p->tv_sec = sec;
    p->tv_usec = 0;
    return 0;
}

static long lx_sc_clock_gettime(long clock_id, long ts) {
    (void)clock_id;
    lx_timespec *p = (lx_timespec *)(uintptr_t)ts;
    if (!p) return LX_EINVAL;
    long sec = lx_rtc_epoch();
    p->tv_sec = sec;
    p->tv_nsec = 0;
    return 0;
}

static void lx_sc_exit_group(void) {
    lx_finished = 1;            /* 告知 lx_run_test 的等待循环 */
    proc_exit(0);               /* 切回调用者（main flow），永不返回 */
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

/* 信号支持 v0：内核无信号模型，按默认语义应答（glibc/musl 启动即通过） */
static long lx_sc_mremap(long old, long osz, long nsz, long flags) {
    (void)old;
    (void)osz;
    (void)nsz;
    (void)flags;
    return LX_ENOMEM;               /* glibc 静态启动不使用 mremap */
}

static long lx_sc_rt_sigaction(long sig, long act, long oldact, long size) {
    (void)sig;
    (void)act;
    (void)size;
    if (oldact) memset((void *)(uintptr_t)oldact, 0, 16);  /* 空 sigset_t */
    return 0;
}

typedef struct { uint64_t base; uint64_t len; } lx_iovec;

static long lx_sc_readv(long fd, long iov, long cnt) {
    if (!iov || cnt <= 0) return LX_EINVAL;
    if (fd == 0) {                  /* 键盘: 单字节 */
        if (cnt < 1) return 0;
        lx_iovec *v = (lx_iovec *)(uintptr_t)iov;
        if (!v[0].base || !v[0].len) return 0;
        return lx_sc_read(0, (long)v[0].base, (long)v[0].len);
    }
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    const rf_node *n = lx_fds[fd].node;
    lx_iovec *v = (lx_iovec *)(uintptr_t)iov;
    long total = 0;
    for (long i = 0; i < cnt && i < 1024; i++) {
        if (!v[i].base || !v[i].len) continue;
        long left;
        if (lx_fds[fd].iso_fd) {
            if (!(lx_fds[fd].iso.flags & 2))
                left = lx_fds[fd].iso.size - lx_fds[fd].off;
            else
                return LX_EISDIR;
        } else {
            if (!n || n->type != RF_TYPE_FILE) return LX_EISDIR;
            left = n->size - lx_fds[fd].off;
        }
        if (left <= 0) break;
        long want = (long)v[i].len;
        if (want > left) want = left;
        if (lx_fds[fd].iso_fd) {
            if (iso_read_at(&lx_fds[fd].iso, lx_fds[fd].off,
                            (void *)(uintptr_t)v[i].base, (uint32_t)want) != 0)
                return LX_EINVAL;
        } else {
            memcpy((void *)(uintptr_t)v[i].base, rf_data(n) + lx_fds[fd].off,
                   (size_t)want);
        }
        lx_fds[fd].off += (uint32_t)want;
        total += want;
        if (want < (long)v[i].len) break;
    }
    return total;
}

static long lx_sc_writev(long fd, long iov, long cnt) {
    if ((fd != 1 && fd != 2) || !iov || cnt <= 0) return LX_EBADF;
    lx_iovec *v = (lx_iovec *)(uintptr_t)iov;
    long total = 0;
    for (long i = 0; i < cnt && i < 1024; i++) {
        if (!v[i].base || !v[i].len) continue;
        const char *s = (const char *)(uintptr_t)v[i].base;
        for (uint64_t j = 0; j < v[i].len; j++) putchar(s[j]);
        total += (long)v[i].len;
    }
    return total;
}

typedef struct { uint64_t rlim_cur; uint64_t rlim_max; } lx_rlimit;

static long lx_sc_prlimit64(long pid, long res, long newlim, long oldlim) {
    (void)pid;
    (void)newlim;
    if (oldlim) {
        lx_rlimit *o = (lx_rlimit *)(uintptr_t)oldlim;
        o->rlim_cur = o->rlim_max = 0xFFFFFFFFFFFFFFFFULL;
        if (res == 3) { o->rlim_cur = o->rlim_max = 0x800000; }  /* RLIMIT_STACK 8MB */
    }
    return 0;
}

static long lx_sc_getrandom(long buf, long len, long flags) {
    (void)flags;
    uint64_t seed = (uint64_t)lx_rtc_epoch() ^ (uint64_t)(uintptr_t)buf;
    seed ^= seed << 21; seed ^= seed >> 35; seed ^= seed << 4;
    uint8_t *p = (uint8_t *)(uintptr_t)buf;
    long n = 0;
    while (n < len && n < 8) { p[n] = (uint8_t)(seed >> (8 * n)); n++; }
    return n;
}

static long lx_sc_rt_sigprocmask(long how, long set, long oldset, long size) {
    (void)how;
    (void)set;
    (void)size;
    if (oldset) memset((void *)(uintptr_t)oldset, 0, 16);  /* 空 sigset_t */
    return 0;
}

static long lx_sc_rseq(long rseq, long len, long flags, long sig) {
    (void)rseq;
    (void)len;
    (void)flags;
    (void)sig;
    return LX_ENOSYS;           /* glibc 2.35+ 启动探测，ENOSYS 即回退 */
}

/* 终端/文件基础（busybox sh 命令行所需）。兼容层: 无真正文件系统,
 * rootfs 内存树 (imported from CD) 提供只读 / 目录与二进制。 */

/* x86-64 struct stat 布局 */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime; int64_t st_atime_nsec;
    int64_t st_mtime; int64_t st_mtime_nsec;
    int64_t st_ctime; int64_t st_ctime_nsec;
    int64_t __unused[3];
} lx_stat;

#define LX_S_IFCHR 0x2000
#define LX_S_IFREG 0x8000
#define LX_S_IFDIR 0x4000
#define LX_S_IFLNK 0xA000

static const rf_node *lx_path_resolve(const char *path) {
    if (!path) return 0;
    if (path[0] != '/') return 0;   /* v0: 仅绝对路径 */
    return rf_lookup(path);
}

static long lx_fd_alloc(void) {
    for (int i = 3; i < LX_FD_MAX; i++)
        if (!lx_fds[i].in_use) return i;
    return -LX_EMFILE;
}

/* 把 "/mnt/cdrom/..." 转成 ISO 相对路径("/..." 或 "/子路径")
 * 返回 0 表示不是光驱路径。 */
static int lx_iso_path(const char *p, char *abspath, size_t cap) {
    if (strncmp(p, "/mnt/cdrom", 10) != 0) return 0;
    const char *ip = p + 10;
    while (*ip == '/') ip++;
    size_t n = 0;
    if (ip[0] == '\0') {
        if (cap < 2) return 0;
        abspath[0] = '/';
        abspath[1] = '\0';
        return 1;
    }
    if (ip[0] != '/') {
        if (n + 1 >= cap) return 0;
        abspath[n++] = '/';
    }
    while (*ip && n + 1 < cap) abspath[n++] = *ip++;
    if (n + 1 >= cap) return 0;
    abspath[n] = '\0';
    return 1;
}

static long lx_sc_close(long fd) {
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    lx_fds[fd].in_use = 0;
    return 0;
}

#define LX_F_DUPFD 0
#define LX_F_DUPFD_CLOEXEC 0x406

/* fcntl: v0 只支持 F_DUPFD/F_DUPFD_CLOEXEC(ash 的 savefd 需要) */
static long lx_sc_fcntl(long fd, long cmd, long arg) {
    if (cmd == LX_F_DUPFD || cmd == LX_F_DUPFD_CLOEXEC) {
        if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return -LX_EBADF;
        if (arg < 3) arg = 3;
        if (arg >= LX_FD_MAX) return -LX_EMFILE;
        long n = -1;
        for (long i = arg; i < LX_FD_MAX; i++)
            if (!lx_fds[i].in_use) { n = i; break; }
        if (n < 0) return -LX_EMFILE;
        lx_fds[n] = lx_fds[fd];   /* 复制 fd 表项(共享 node/off) */
        return n;
    }
    return 0;                      /* 其余命令 no-op */
}

static long lx_sc_open(long path, long flags, long mode) {
    (void)mode;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    const rf_node *n = lx_path_resolve(p);
    long fd = -1;
    if (n) {
        fd = lx_fd_alloc();
        if (fd < 0) return fd;
        lx_fds[fd].in_use = 1;
        lx_fds[fd].node = n;
        lx_fds[fd].off = 0;
        lx_fds[fd].iso_fd = 0;
        return fd;
    }
    /* 虚拟光驱: /mnt/cdrom/<path> 落到 ISO9660 镜像 */
    char abspath[160];
    if (lx_iso_path(p, abspath, sizeof(abspath))) {
        iso_file_t iso;
        if (iso_lookup(abspath, &iso) != 0)
            return LX_ENOENT;
        fd = lx_fd_alloc();
        if (fd < 0) return fd;
        lx_fds[fd].in_use = 1;
        lx_fds[fd].node = NULL;
        lx_fds[fd].off = 0;
        lx_fds[fd].iso_fd = 1;
        lx_fds[fd].iso = iso;
        return fd;
    }
    return LX_ENOENT;
}

static long lx_sc_lseek(long fd, long off, long whence) {
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    const rf_node *n = lx_fds[fd].node;
    int64_t size;
    if (lx_fds[fd].iso_fd) {
        if (lx_fds[fd].iso.flags & 2) return LX_EISDIR;
        size = lx_fds[fd].iso.size;
    } else {
        if (!n || n->type == RF_TYPE_DIR) return LX_EISDIR;
        size = n->size;
    }
    int64_t base;
    switch (whence) {
    case 0: base = 0; break;               /* SEEK_SET */
    case 1: base = lx_fds[fd].off; break;  /* SEEK_CUR */
    case 2: base = size; break;            /* SEEK_END */
    default: return LX_EINVAL;
    }
    int64_t res = base + off;
    if (res < 0) return LX_EINVAL;
    lx_fds[fd].off = (uint32_t)res;
    return lx_fds[fd].off;
}

static void lx_stat_fill(lx_stat *s, const rf_node *n) {
    memset(s, 0, sizeof(*s));
    s->st_ino = 1 + (uint32_t)(n - rf_root());
    s->st_nlink = 1;
    s->st_blksize = 512;
    switch (n->type) {
    case RF_TYPE_DIR:   s->st_mode = LX_S_IFDIR  | 0755; break;
    case RF_TYPE_SYMLNK:s->st_mode = LX_S_IFLNK  | 0777; s->st_size = n->size; break;
    default:            s->st_mode = LX_S_IFREG  | 0644; s->st_size = n->size; break;
    }
    s->st_blocks = (s->st_size + 511) / 512;
}

static void lx_stat_fill_iso(lx_stat *s, const iso_file_t *iso) {
    memset(s, 0, sizeof(*s));
    s->st_ino = 0xCD000 | (iso->extent & 0xFFFF);
    s->st_nlink = 1;
    s->st_blksize = 512;
    if (iso->flags & 2)
        s->st_mode = LX_S_IFDIR | 0555;
    else
        s->st_mode = LX_S_IFREG | 0444;
    s->st_size = iso->size;
    s->st_blocks = (s->st_size + 511) / 512;
}

static long lx_sc_fstat(long fd, long buf) {
    lx_stat *s = (lx_stat *)(uintptr_t)buf;
    if (!s) return LX_EINVAL;
    if (fd == 1 || fd == 2) {       /* stdout/stderr: 终端 */
        memset(s, 0, sizeof(*s));
        s->st_mode = LX_S_IFCHR | 0600;
        s->st_nlink = 1;
        return 0;
    }
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    if (lx_fds[fd].iso_fd) {
        lx_stat_fill_iso(s, &lx_fds[fd].iso);
        return 0;
    }
    if (!lx_fds[fd].node) {         /* stdin 终端 */
        memset(s, 0, sizeof(*s));
        s->st_mode = LX_S_IFCHR | 0600;
        s->st_nlink = 1;
        return 0;
    }
    lx_stat_fill(s, lx_fds[fd].node);
    return 0;
}

static long lx_sc_stat(long path, long buf) {
    const char *p = (const char *)(uintptr_t)path;
    lx_stat *s = (lx_stat *)(uintptr_t)buf;
    if (!p || !s) return LX_EINVAL;
    const rf_node *n = lx_path_resolve(p);
    if (n) {
        lx_stat_fill(s, n);
        return 0;
    }
    char abspath[160];
    if (lx_iso_path(p, abspath, sizeof(abspath))) {
        iso_file_t iso;
        if (iso_lookup(abspath, &iso) == 0) {
            lx_stat_fill_iso(s, &iso);
            return 0;
        }
    }
    return LX_ENOENT;
}

static long lx_sc_lstat(long path, long buf) {
    return lx_sc_stat(path, buf);
}

/* 光驱目录项缓冲(单实例: 同一时刻只遍历一个目录) */
static uint8_t iso_dir_snap[64 * 1024];

static long lx_sc_access(long path, long amode) {
    (void)amode;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (lx_path_resolve(p)) return 0;
    char abspath[160];
    if (lx_iso_path(p, abspath, sizeof(abspath))) {
        iso_file_t iso;
        if (iso_lookup(abspath, &iso) == 0)
            return 0;
    }
    return -LX_ENOENT;
}

/* getdents64: 目录条目 */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char     d_name[];
} lx_dirent64;

static long lx_sc_getdents64(long fd, long buf, long len) {
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    const rf_node *n = lx_fds[fd].node;
    if (lx_fds[fd].iso_fd) {
        /* 光驱目录: 整体读入快照, off 为已返回的记录数 */
        if (!(lx_fds[fd].iso.flags & 2)) return LX_ENOTDIR;
        if (lx_fds[fd].iso.size > sizeof(iso_dir_snap)) return LX_ENOTDIR;
        if (iso_read_file(&lx_fds[fd].iso, iso_dir_snap) != 0) return LX_ENOENT;
        uint8_t *out = (uint8_t *)(uintptr_t)buf;
        long total = 0;
        uint32_t pos = 0;
        uint32_t skip = lx_fds[fd].off;
        uint32_t idx = 0;
        while (pos + 34 <= lx_fds[fd].iso.size) {
            const uint8_t *rec = iso_dir_snap + pos;
            uint8_t reclen = rec[0];
            if (reclen == 0) break;
            if (pos + reclen > lx_fds[fd].iso.size) break;
            char name[128];
            int o = 0;
            uint8_t nl = rec[32];
            for (int i = 0; i < nl && i < 126; i++) {
                char c = rec[33 + i];
                if (c == ';') break;
                name[o++] = c;
            }
            name[o] = '\0';
            if (o == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                pos += reclen;
                if (skip == idx) {         /* 跳过记录也算一记录 */
                    skip++;
                    idx++;
                }
                continue;
            }
            if (skip > 0) {
                skip--;
                idx++;
                pos += reclen;
                continue;
            }
            uint32_t extent = rec[2] | (rec[3] << 8) | (rec[4] << 16) |
                              ((uint32_t)rec[5] << 24);
            uint8_t flags = rec[25];
            unsigned short eran = (unsigned short)(sizeof(lx_dirent64) + o + 1);
            if (total + eran > len) break;
            lx_dirent64 *d = (lx_dirent64 *)(out + total);
            d->d_ino = 0xCD000 | (extent & 0xFFFF);
            d->d_off = (int64_t)idx;
            d->d_reclen = eran;
            d->d_type = (flags & 2) ? 4 : 8;
            memcpy(d->d_name, name, o);
            d->d_name[o] = '\0';
            total += eran;
            idx++;
            pos += reclen;
        }
        lx_fds[fd].off = idx;
        return total;
    }
    if (!n || n->type != RF_TYPE_DIR) return LX_ENOTDIR;
    uint8_t *out = (uint8_t *)(uintptr_t)buf;
    long total = 0;
    uint32_t idx = lx_fds[fd].off;
    const rf_node *cur = rf_child(n);
    for (uint32_t i = 0; cur && i < idx; i++) cur = rf_next(cur);
    while (cur && total + sizeof(lx_dirent64) + 2 <= len) {
        const char *name = rf_name(cur);
        uint32_t nl = (uint32_t)strlen(name);
        unsigned short reclen = (unsigned short)(sizeof(lx_dirent64) + nl + 1);
        if (total + reclen > len) break;
        lx_dirent64 *d = (lx_dirent64 *)(out + total);
        d->d_ino = 1 + (uint32_t)(cur - rf_root());
        d->d_off = (int64_t)(idx + 1);
        d->d_reclen = reclen;
        d->d_type = (cur->type == RF_TYPE_DIR) ? 4 :
                    (cur->type == RF_TYPE_SYMLNK) ? 10 : 8;
        memcpy(d->d_name, name, nl);
        d->d_name[nl] = '\0';
        total += reclen;
        idx++;
        cur = rf_next(cur);
    }
    lx_fds[fd].off = idx;
    return total;
}

/* linux_dirent (syscall 141, 32-bit 目录项头, 供部分工具备用) */
typedef struct {
    uint64_t d_ino;
    int64_t  d_off;
    unsigned short d_reclen;
    char     d_name[];
} lx_dirent;

static long lx_sc_getdents(long fd, long buf, long len) {
    /* 关同 getdents64: 对我们目录项布局, 两个调用一致 */
    return lx_sc_getdents64(fd, buf, len);
}

static long lx_sc_readlink(long path, long buf, long bufsz) {
    const char *p = (const char *)(uintptr_t)path;
    if (!p || !buf) return LX_EINVAL;
    const rf_node *n = lx_path_resolve(p);
    if (!n) return LX_ENOENT;
    if (n->type != RF_TYPE_SYMLNK) return LX_EINVAL;
    long sz = n->size;
    if (sz > bufsz) sz = bufsz;
    memcpy((void *)(uintptr_t)buf, rf_data(n), (size_t)sz);
    return sz;
}

/* x86-64 termios 布局 */
typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_line;
    unsigned char c_cc[32];
    uint32_t c_ispeed, c_ospeed;
} lx_termios;

#define LX_TCGETS 0x5401
#define LX_TIOCSCTTY 0x540E
#define LX_TCGETPGRP 0x540F
#define LX_TCSETPGRP 0x5410
#define LX_TCSETS 0x5402
#define LX_TIOCGWINSZ 0x5413

static long lx_sc_ioctl(long fd, long req, long arg) {
    (void)fd;
    if (req == LX_TCGETS) {
        lx_termios *t = (lx_termios *)(uintptr_t)arg;
        if (!t) return LX_EINVAL;
        memset(t, 0, sizeof(*t));
        t->c_iflag = 0x0501;            /* ICRNL|IXON|IXOFF? 按 Linux 默认 */
        t->c_oflag = 0x0005;            /* OPOST|ONLCR */
        t->c_cflag = 0x00BF;            /* CS8|CREAD */
        t->c_lflag = 0x000B;            /* ISIG|ICANON|ECHO */
        t->c_line = 0;
        t->c_cc[3] = 0x7F;             /* VERASE: DEL (keyboard sends 0x7F) */
        t->c_cc[6] = 1;                /* VMIN = 1 */
        return 0;
    }
    if (req == LX_TCSETS) return 0;     /* 接受终端设置（busybox 设 raw 模式） */
    if (req == LX_TIOCSCTTY) return LX_ENOTTY;
    if (req == LX_TCGETPGRP) return LX_ENOTTY;
    if (req == LX_TCSETPGRP) return 0;
    if (req == LX_TIOCGWINSZ) {         /* 窗口尺寸 80x25 */
        uint16_t *w = (uint16_t *)(uintptr_t)arg;
        if (!w) return LX_EINVAL;
        w[0] = 25; w[1] = 80; w[2] = 0; w[3] = 0;
        return 0;
    }
    return LX_ENOTTY;
}

static long lx_sc_getcwd(long buf, long size) {
    if (!buf || size < 2) return LX_EINVAL;
    char *p = (char *)(uintptr_t)buf;
    p[0] = '/';
    p[1] = '\0';
    return 1;                       /* v0: 根目录固定 */
}

static long lx_sc_chdir(long path) {
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (p[0] == '/' && (p[1] == '\0')) return 0;    /* 只有 / 可访问 */
    char abspath[160];
    if (lx_iso_path(p, abspath, sizeof(abspath))) {
        iso_file_t iso;
        if (iso_lookup(abspath, &iso) == 0 && (iso.flags & 2))
            return 0;
        return LX_ENOENT;
    }
    return LX_ENOENT;
}

typedef struct { int fd; short events; short revents; } lx_pollfd;

static int lx_input_pending(void) {
    return keyboard_pending();
}

static long lx_sc_poll(long fds, long nfds, long timeout) {
    if (!fds || nfds <= 0) return LX_EINVAL;
    lx_pollfd *p = (lx_pollfd *)(uintptr_t)fds;
    long n = nfds < 64 ? nfds : 64;
    for (long i = 0; i < n; i++) p[i].revents = 0;
    for (;;) {
        long ready = 0;
        for (long i = 0; i < n; i++) {
            if (p[i].fd == 0 && (p[i].events & 1) && lx_input_pending()) {
                p[i].revents = 1;      /* POLLIN */
                ready++;
            }
        }
        if (ready) return ready;
        if (timeout == 0) return 0;
        if (timeout > 0) { timeout -= 10; if (timeout <= 0) return 0; }
        proc_yield();
    }
}

static long lx_sc_openat(long dirfd, long path, long flags, long mode) {
    (void)dirfd;
    return lx_sc_open(path, flags, mode);
}

/* ---- 分发器：Linux x86-64 调用号 -> Kresten 服务 ---- */

long linux_syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5) {
    (void)num; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    switch (num) {
    case 0:   return lx_sc_read(a1, a2, a3);            /* read */
    case 1:   return lx_sc_write(a1, a2, a3);           /* write */
    case 2:   return lx_sc_open(a1, a2, a3);            /* open */
    case 3:   return lx_sc_close(a1);                   /* close */
    case 4:   return lx_sc_stat(a1, a2);                /* stat */
    case 5:   return lx_sc_fstat(a1, a2);               /* fstat */
    case 6:   return lx_sc_lstat(a1, a2);               /* lstat */
    case 8:   return lx_sc_lseek(a1, a2, a3);           /* lseek */
    case 7:   return lx_sc_poll(a1, a2, a3);            /* poll */
    case 9:   return lx_sc_mmap(a1, a2, a3, a4, a5);    /* mmap */
    case 10:  return 0;                                 /* mprotect (no-op) */
    case 11:  return lx_sc_munmap(a1, a2);              /* munmap */
    case 12:  return lx_sc_brk(a1);                     /* brk */
    case 13:  return lx_sc_rt_sigaction(a1, a2, a3, a4); /* rt_sigaction */
    case 14:  return lx_sc_rt_sigprocmask(a1, a2, a3, a4); /* rt_sigprocmask */
    case 16:  return lx_sc_ioctl(a1, a2, a3);           /* ioctl */
    case 19:  return lx_sc_readv(a1, a2, a3);           /* readv */
    case 20:  return lx_sc_writev(a1, a2, a3);          /* writev */
    case 21:  return lx_sc_access(a1, a2);              /* access */
    case 24:  proc_yield(); return 0;                   /* sched_yield */
    case 25:  return lx_sc_mremap(a1, a2, a3, a4);      /* mremap */
    case 28:  return 0;                                 /* madvise (no-op) */
    case 39:  return 1;                                 /* getpid */
    case 60:  lx_sc_exit_group(); return 0;             /* exit */
    case 62:  return 0;                                 /* kill (no-op) */
    case 63:  return lx_sc_uname(a1);                   /* uname */
    case 65:  return 0;                                 /* setpgid */
    case 72:  return lx_sc_fcntl(a1, a2, a3);           /* fcntl */
    case 79:  return lx_sc_getcwd(a1, a2);              /* getcwd */
    case 80:  return lx_sc_chdir(a1);                   /* chdir */
    case 89:  return lx_sc_readlink(a1, a2, a3);        /* readlink */
    case 96:  return lx_sc_gettimeofday(a1, a2);        /* gettimeofday */
    case 102: return 0;                                 /* getuid */
    case 104: return 0;                                 /* getgid */
    case 107: return 0;                                 /* geteuid */
    case 108: return 0;                                 /* getegid */
    case 110: return 1;                                 /* getppid */
    case 112: return -LX_ENOSYS;                        /* setsid: no job control */
    case 129: return 1;                                 /* getpgrp */
    case 141: return lx_sc_getdents(a1, a2, a3);        /* getdents */
    case 158: return lx_sc_arch_prctl(a1, a2);          /* arch_prctl */
    case 186: return 1;                                 /* gettid */
    case 201: return lx_sc_time(a1);                    /* time */
    case 202: return 0;                                 /* futex (no-op) */
    case 217: return lx_sc_getdents64(a1, a2, a3);      /* getdents64 */
    case 218: return 1;                                 /* set_tid_address */
    case 228: return lx_sc_clock_gettime(a1, a2);       /* clock_gettime */
    case 231: lx_sc_exit_group(); return 0;             /* exit_group */
    case 257: return lx_sc_openat(a1, a2, a3, a4);      /* openat */
    case 273: return 0;                                 /* set_robust_list */
    case 302: return lx_sc_prlimit64(a1, a2, a3, a4);   /* prlimit64 */
    case 318: return lx_sc_getrandom(a1, a2, a3);       /* getrandom */
    case 334: return lx_sc_rseq(a1, a2, a3, a4);        /* rseq */
    default:
        return LX_ENOSYS;        /* unknown: ENOSYS, silent (v0) */
    }
}

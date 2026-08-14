/* LX syscall 翻译层 (ARM EABI 32 位版)。
 * 入口: start.s 的 svc_handler (guest 以 USR 模式运行, svc 自陷)。
 * 把 Linux ARM EABI syscall ABI 翻译为 Kresten 内核服务。
 * 与 x86 版区别: 32 位指针/结构体, stat64 布局, mmap2(offset 页单位),
 * set_tls 用 RAM 模拟槽(ARM926 无 TPIDRURO)。 */

#include "lx_int.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rtc.h>
#include <keyboard.h>

/* svc 陷阱栈与内核 sp 保存区 (start.s 引用) */
uint8_t lx_trap_stack[8192] __attribute__((aligned(16)));
uint32_t lx_save_sp = 0;

/* TLS 模拟槽: get_tls 桩读它, set_tls/undef mcr 写它 */
uint32_t lx_tls_slot = 0;

/* 由 lx_proc.c 提供, exit_group 时经 lx_resume_helper 恢复 */
extern volatile uint32_t lx_resume_sp;
extern volatile uint32_t lx_resume_pc;

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

/* ---- 基础服务 ---- */

static long lx_sc_read(long fd, long buf, long len) {
    if (!buf || len <= 0) return LX_EINVAL;
    if (fd == 0) {
        unsigned char c;
        for (;;) {
            int k = keyboard_getchar();
            if (k >= 0) { c = (unsigned char)k; break; }
        }
        *(char *)(uintptr_t)buf = (char)c;
        return 1;
    }
    if (fd == 1 || fd == 2) return 0;   /* 终端只读 */
    return LX_EBADF;
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
    static uint32_t brk_cur = LX_BRK_BASE;
    if (addr == 0) return (long)brk_cur;
    if ((uint32_t)addr < LX_BRK_BASE ||
        (uint32_t)addr > LX_BRK_BASE + LX_BRK_SIZE)
        return (long)brk_cur;
    brk_cur = (uint32_t)addr;
    return (long)brk_cur;
}

#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

/* ARM EABI: mmap2 第 6 参 offset 以页为单位; 只支持匿名映射 */
static long lx_sc_mmap2(long addr, long length, long prot, long flags, long fd, long offset) {
    (void)prot;
    (void)offset;
    uint32_t len = (uint32_t)length;
    if (len == 0) return LX_EINVAL;
    if ((flags & MAP_FIXED) || addr != 0) return LX_EINVAL;
    len = (len + 4095) & ~4095u;
    static uint32_t mmap_cur = LX_MMAP_BASE;
    uint32_t p = mmap_cur;
    if (p + len > LX_MMAP_BASE + LX_MMAP_SIZE) return LX_ENOMEM;
    mmap_cur = p + len;
    memset((void *)(uintptr_t)p, 0, (size_t)len);
    if (!(flags & MAP_ANONYMOUS) && fd >= 0 && fd < 3) return LX_EACCES;
    return (long)p;
}

static long lx_sc_munmap(long addr, long length) {
    (void)addr;
    (void)length;
    return 0;
}

static long lx_sc_mprotect(long addr, long len, long prot) {
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

static long lx_sc_mremap(long old, long osz, long nsz, long flags) {
    (void)old;
    (void)osz;
    (void)nsz;
    (void)flags;
    return LX_ENOMEM;
}

static long lx_sc_madvise(long addr, long len, long advice) {
    (void)addr;
    (void)len;
    (void)advice;
    return 0;
}

/* ---- 时间 ---- */

static long lx_rtc_epoch(void) {
    RTC_Time t;
    rtc_read(&t);
    static const int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    long days = 10957;
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

/* ARM: long 为 32 位, timeval/timespec 均 8 字节 */
typedef struct { uint32_t tv_sec; uint32_t tv_usec; } lx_timeval;
typedef struct { uint32_t tv_sec; uint32_t tv_nsec; } lx_timespec;

static long lx_sc_gettimeofday(long tv, long tz) {
    (void)tz;
    lx_timeval *p = (lx_timeval *)(uintptr_t)tv;
    if (!p) return LX_EINVAL;
    p->tv_sec = (uint32_t)lx_rtc_epoch();
    p->tv_usec = 0;
    return 0;
}

static long lx_sc_clock_gettime(long clock_id, long ts) {
    (void)clock_id;
    lx_timespec *p = (lx_timespec *)(uintptr_t)ts;
    if (!p) return LX_EINVAL;
    p->tv_sec = (uint32_t)lx_rtc_epoch();
    p->tv_nsec = 0;
    return 0;
}

static long lx_sc_clock_getres(long clock_id, long ts) {
    (void)clock_id;
    lx_timespec *p = (lx_timespec *)(uintptr_t)ts;
    if (!p) return LX_EINVAL;
    p->tv_sec = 0;
    p->tv_nsec = 1000000;
    return 0;
}

/* ---- 进程/信号 ---- */

static void lx_sc_exit_group(void) {
    lx_finished = 1;
    lx_resume_helper();         /* 直接恢复内核 sp/pc, 永不返回 */
    for (;;) { }
}

static long lx_sc_rt_sigaction(long sig, long act, long oldact, long size) {
    (void)sig;
    (void)act;
    (void)size;
    if (oldact) memset((void *)(uintptr_t)oldact, 0, 8);   /* 空 sigset_t(128 bit=16B? 8B 够首字) */
    return 0;
}

static long lx_sc_rt_sigprocmask(long how, long set, long oldset, long size) {
    (void)how;
    (void)set;
    (void)size;
    if (oldset) memset((void *)(uintptr_t)oldset, 0, 8);
    return 0;
}

static long lx_sc_sigaltstack(long ss, long oss) {
    (void)ss;
    if (oss) memset((void *)(uintptr_t)oss, 0, 12);        /* stack_t: u32*3 */
    return 0;
}

static long lx_sc_set_tls(long tls) {
    lx_tls_slot = (uint32_t)tls;    /* get_tls 桩的模拟槽 */
    return 0;
}

static long lx_sc_getrandom(long buf, long len, long flags) {
    (void)flags;
    uint32_t seed = (uint32_t)lx_rtc_epoch() ^ (uint32_t)(uintptr_t)buf;
    seed ^= seed << 21; seed ^= seed >> 11; seed ^= seed << 4;
    uint8_t *p = (uint8_t *)(uintptr_t)buf;
    long n = 0;
    while (n < len && n < 8) { p[n] = (uint8_t)(seed >> (8 * n)); n++; }
    return n;
}

static long lx_sc_sched_yield(void) {
    return 0;
}

static long lx_sc_nanosleep(long req, long rem) {
    (void)rem;
    lx_timespec *p = (lx_timespec *)(uintptr_t)req;
    if (!p) return LX_EINVAL;
    extern uint32_t sched_get_ticks(void);
    uint32_t start = sched_get_ticks();
    uint32_t d = (uint32_t)((p->tv_sec * 100u) + (p->tv_nsec / 10000000u));
    while (sched_get_ticks() - start < d) { }
    return 0;
}

/* ---- 终端 ---- */

/* ARM termios 布局与 x86-64 相同: 4×u32 flags + c_line + c_cc[32] + 2×u32 */
typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[32];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
} lx_termios;

#define LX_ICRNL 0x0100
#define LX_IXON  0x0400
#define LX_OPOST 0x0001
#define LX_ONLCR 0x0004
#define LX_ICANON 0x0002
#define LX_ECHO  0x0008
#define LX_ECHOE 0x0010
#define LX_ECHOK 0x0020
#define LX_ECHONL 0x0040
#define LX_ISIG  0x0001
#define LX_BRKINT 0x0002
#define LX_CS8   0x0030
#define LX_CREAD 0x0080
#define LX_B38400 0x000F

#define LX_TCGETS   0x5401
#define LX_TCSETS   0x5402
#define LX_TCSETSW  0x5403
#define LX_TCSETSF  0x5404
#define LX_TIOCGWINSZ 0x5413
#define LX_TIOCSCTTY 0x540E
#define LX_TCSBRK   0x5409

static void lx_fill_termios(lx_termios *t) {
    memset(t, 0, sizeof(*t));
    t->c_iflag = LX_BRKINT | LX_ICRNL | LX_IXON;
    t->c_oflag = LX_OPOST | LX_ONLCR;
    t->c_cflag = LX_CS8 | LX_CREAD | LX_B38400;
    t->c_lflag = LX_ISIG | LX_ICANON | LX_ECHO | LX_ECHOE | LX_ECHOK | LX_ECHONL;
    t->c_cc[0] = 0x03;   /* VINTR */
    t->c_cc[1] = 0x1C;   /* VQUIT */
    t->c_cc[2] = 0x7F;   /* VERASE */
    t->c_cc[3] = 0x15;   /* VKILL */
    t->c_cc[4] = 0x04;   /* VEOF */
    t->c_cc[5] = 1;      /* VTIME */
    t->c_cc[6] = 0;      /* VMIN */
    t->c_cc[7] = 0x11;   /* VSTART */
    t->c_cc[8] = 0x13;   /* VSTOP */
    t->c_cc[9] = 0x1A;   /* VSUSP */
    t->c_ispeed = LX_B38400;
    t->c_ospeed = LX_B38400;
}

static long lx_sc_ioctl(long fd, long req, long arg) {
    if (fd < 0 || fd > 2) return LX_EBADF;
    switch (req) {
    case LX_TCGETS:
        lx_fill_termios((lx_termios *)(uintptr_t)arg);
        return 0;
    case LX_TCSETS:
    case LX_TCSETSW:
    case LX_TCSETSF:
    case LX_TCSBRK:
        return 0;
    case LX_TIOCGWINSZ:
        memset((void *)(uintptr_t)arg, 0, 8);   /* winsize */
        return 0;
    case LX_TIOCSCTTY:
        return -LX_ENOTTY;      /* job control 关闭 (同 x86) */
    default:
        return -LX_ENOTTY;
    }
}

/* ---- 文件: ARM 无文件系统, /dev 伪设备 + 一律 ENOENT ---- */

static int lx_is_devpath(const char *p) {
    return strcmp(p, "/dev/null") == 0 ||
           strcmp(p, "/dev/tty") == 0 ||
           strcmp(p, "/dev/console") == 0;
}

static long lx_sc_open(long path, long flags, long mode) {
    (void)flags;
    (void)mode;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (lx_is_devpath(p)) return 3;
    if (strcmp(p, "/") == 0) return 4;      /* root dir fd */
    return LX_ENOENT;
}

static long lx_sc_close(long fd) {
    if (fd < 0 || fd > 2) return LX_EBADF;
    return 0;
}

/* ARM glibc struct stat64 (EABI, 104 字节) */
typedef struct {
    uint64_t st_dev;
    uint8_t  __pad0[4];
    uint32_t __st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint8_t  __pad3[4];
    int64_t  st_size;
    uint32_t st_blksize;
    int64_t  st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint64_t st_ino;
} lx_stat64;

#define LX_S_IFCHR 0x2000
#define LX_S_IFREG 0x8000
#define LX_S_IFDIR 0x4000

static void lx_fill_stat(lx_stat64 *s, uint32_t mode, uint64_t size) {
    uint32_t now = (uint32_t)lx_rtc_epoch();
    memset(s, 0, sizeof(*s));
    s->st_dev = 0x8888;
    s->st_ino = 1;
    s->st_mode = mode;
    s->st_nlink = 1;
    s->st_uid = 0;
    s->st_gid = 0;
    s->st_rdev = 0x8888;
    s->st_size = (int64_t)size;
    s->st_blksize = 4096;
    s->st_blocks = (size + 511) / 512;
    s->st_atime = s->st_mtime = s->st_ctime = now;
}

static long lx_sc_stat64(long path, long buf) {
    const char *p = (const char *)(uintptr_t)path;
    lx_stat64 *s = (lx_stat64 *)(uintptr_t)buf;
    if (!p || !s) return LX_EINVAL;
    if (lx_is_devpath(p)) { lx_fill_stat(s, LX_S_IFCHR | 0x1B6, 0); return 0; }
    if (strcmp(p, "/") == 0) { lx_fill_stat(s, LX_S_IFDIR | 0x1ED, 0); return 0; }
    return LX_ENOENT;
}

static long lx_sc_fstat64(long fd, long buf) {
    lx_stat64 *s = (lx_stat64 *)(uintptr_t)buf;
    if (!s) return LX_EINVAL;
    if (fd == 3) { lx_fill_stat(s, LX_S_IFCHR | 0x1B6, 0); return 0; }
    if (fd == 4) { lx_fill_stat(s, LX_S_IFDIR | 0x1ED, 0); return 0; }
    if (fd == 1 || fd == 2) { lx_fill_stat(s, LX_S_IFCHR | 0x1B6, 0); return 0; }
    if (fd == 0) { lx_fill_stat(s, LX_S_IFCHR | 0x1B6, 0); return 0; }
    return LX_EBADF;
}

static long lx_sc_access(long path, long mode) {
    (void)mode;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (lx_is_devpath(p) || strcmp(p, "/") == 0) return 0;
    return LX_ENOENT;
}

static long lx_sc_chdir(long path) {
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (strcmp(p, "/") == 0) return 0;
    return LX_ENOENT;
}

static long lx_sc_getcwd(long buf, long size) {
    char *b = (char *)(uintptr_t)buf;
    if (!b || size < 2) return LX_EINVAL;
    strcpy(b, "/");
    return 2;
}

static long lx_sc_readlink(long path, long buf, long bufsiz) {
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (strcmp(p, "/proc/self/exe") == 0) {
        char *b = (char *)(uintptr_t)buf;
        static const char exe[] = "/bin/busybox";
        long n = sizeof(exe) - 1;
        if (!b || bufsiz <= 0) return LX_EINVAL;
        if (n > bufsiz) n = bufsiz;
        for (long i = 0; i < n; i++) b[i] = exe[i];
        return n;
    }
    return LX_ENOENT;
}

/* ARM readlinkat(dirfd, path, buf, bufsiz): glibc _dl_get_origin 读
 * /proc/self/exe 断言 linkval[0]=='/', 返回绝对路径即可 (无真实文件系统)。 */
static long lx_sc_readlinkat(long dirfd, long path, long buf, long bufsiz) {
    (void)dirfd;
    return lx_sc_readlink(path, buf, bufsiz);
}

static long lx_sc_getdents64(long fd, long buf, long count) {
    (void)fd;
    (void)buf;
    (void)count;
    return 0;                       /* 根目录为空 */
}

/* ---- 基础杂项 ---- */

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
    strcpy(u->machine, "armv5te");
    strcpy(u->domainname, "(none)");
    return 0;
}

/* ARM iovec: 32 位指针 + 32 位长度 = 8 字节 */
typedef struct { uint32_t base; uint32_t len; } lx_iovec;

static long lx_sc_readv(long fd, long iov, long cnt) {
    if (!iov || cnt <= 0) return LX_EINVAL;
    if (fd == 0) {
        lx_iovec *v = (lx_iovec *)(uintptr_t)iov;
        if (!v[0].base || !v[0].len) return 0;
        return lx_sc_read(0, (long)v[0].base, (long)v[0].len);
    }
    return LX_EBADF;
}

static long lx_sc_writev(long fd, long iov, long cnt) {
    if ((fd != 1 && fd != 2) || !iov || cnt <= 0) return LX_EBADF;
    lx_iovec *v = (lx_iovec *)(uintptr_t)iov;
    long total = 0;
    for (long i = 0; i < cnt && i < 1024; i++) {
        if (!v[i].base || !v[i].len) continue;
        const char *s = (const char *)(uintptr_t)v[i].base;
        for (uint32_t j = 0; j < v[i].len; j++) putchar(s[j]);
        total += (long)v[i].len;
    }
    return total;
}

typedef struct { uint32_t rlim_cur; uint32_t rlim_max; } lx_rlimit;

static long lx_sc_ugetrlimit(long res, long oldlim) {
    if (oldlim) {
        lx_rlimit *o = (lx_rlimit *)(uintptr_t)oldlim;
        o->rlim_cur = o->rlim_max = 0xFFFFFFFFu;
        if (res == 3) { o->rlim_cur = o->rlim_max = 0x800000u; }  /* RLIMIT_STACK 8MB */
    }
    return 0;
}

static long lx_sc_prlimit64(long pid, long res, long newlim, long oldlim) {
    (void)pid;
    (void)newlim;
    if (oldlim) {
        lx_rlimit *o = (lx_rlimit *)(uintptr_t)oldlim;
        o->rlim_cur = o->rlim_max = 0xFFFFFFFFu;
        if (res == 3) { o->rlim_cur = o->rlim_max = 0x800000u; }
    }
    return 0;
}

static long lx_sc_fcntl64(long fd, long cmd, long arg) {
    (void)fd;
    if (cmd == 0) return arg;       /* F_DUPFD */
    if (cmd == 1) return 0;         /* F_GETFD */
    if (cmd == 2) return 0;         /* F_SETFD */
    if (cmd == 4) return 0;         /* F_GETFL */
    return LX_EINVAL;
}

static long lx_sc_dup3(long oldfd, long newfd, long flags) {
    (void)flags;
    if (oldfd < 0 || oldfd > 2) return LX_EBADF;
    return newfd;
}

static long lx_sc_futex(long uaddr, long op, long val, long timeout) {
    (void)uaddr;
    (void)op;
    (void)val;
    (void)timeout;
    return 0;
}

static long lx_sc_kill(long pid, long sig) {
    (void)pid;
    (void)sig;
    return 0;
}

static long lx_sc_tgkill(long tgid, long pid, long sig) {
    (void)tgid;
    (void)pid;
    (void)sig;
    return 0;
}

static long lx_sc_getpid(void) { return 1; }
static long lx_sc_getppid(void) { return 1; }
static long lx_sc_getpgrp(void) { return 1; }
static long lx_sc_setsid(void) { return -LX_ENOSYS; }
static long lx_sc_execve(void) { return -LX_EACCES; }
static long lx_sc_fork(void) { return -LX_ENOSYS; }
static long lx_sc_pipe2(void) { return -LX_ENOSYS; }
static long lx_sc_wait4(void) { return -LX_ENOSYS; }

typedef struct { int fd; short events; short revents; } lx_pollfd;

#define LX_POLLIN 0x001
#define LX_POLLOUT 0x004
#define LX_POLLERR 0x008

static long lx_sc_poll(long fds, long nfds, long timeout) {
    (void)timeout;
    lx_pollfd *p = (lx_pollfd *)(uintptr_t)fds;
    if (!p || nfds <= 0) return LX_EINVAL;
    long ready = 0;
    for (long i = 0; i < nfds && i < 64; i++) {
        p[i].revents = 0;
        if (p[i].fd == 1 || p[i].fd == 2) {
            if (p[i].events & LX_POLLOUT) p[i].revents |= LX_POLLOUT;
            ready++;
        } else if (p[i].fd == 0) {
            extern int keyboard_pending(void);
            if (keyboard_pending()) {
                if (p[i].events & LX_POLLIN) p[i].revents |= LX_POLLIN;
                ready++;
            } else if (!(p[i].events & LX_POLLIN)) {
                p[i].revents |= LX_POLLIN;
                ready++;
            }
        }
    }
    if (ready == 0) return 0;       /* v0: 不等待, 立即返回 */
    return ready;
}

/* ---- 主分发 ---- */

/* undef_handler 调用: 模拟 guest(USR) 的 CP15 c13 访问。
 * frame 布局同 svc_handler: [frame+12]=r0 ... [frame+60]=lr。
 * 返回 0 = 无法模拟(挂起); 否则返回新 lr(undef 时 lr = fault+4, 原样返回)。 */
uint32_t lx_undef_dispatch(uint32_t lr, uint32_t frame) {
    uint32_t insn = *(volatile uint32_t *)(lr - 4);
    uint32_t rt;
    if ((insn & 0x0FFF0FFF) == 0x0E0D0F70u) {   /* mcr p15,0,rt,c13,c0,3 */
        rt = (insn >> 12) & 0xF;
        if (rt > 11) return 0;
        lx_tls_slot = *(volatile uint32_t *)(frame + 12 + rt * 4);
        return lr;
    }
    if ((insn & 0x0FFF0FFF) == 0x0E1D0F70u) {   /* mrc p15,0,rt,c13,c0,3 */
        rt = (insn >> 12) & 0xF;
        if (rt > 11) return 0;
        *(volatile uint32_t *)(frame + 12 + rt * 4) = lx_tls_slot;
        return lr;
    }
    if ((insn & 0xFFF000F0) == 0xE7F000F0u) {   /* udf #imm: SIGILL 语义 */
        printf("[LX] udf at 0x%08x (SIGILL) -> exit\n", (unsigned)lr - 4);
        lx_sc_exit_group();                     /* 直接终止 guest, 不挂起 */
        return lr;                              /* 不可达 */
    }
    printf("[LX] undef pc=0x%08x insn=0x%08x (无法模拟)\n", (unsigned)lr, (unsigned)insn);
    return 0;
}

long linux_syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5) {
    if (num != 158 && num != 3 && num != 4 && num != 240)
        printf("[LX] svc num=%d a1=0x%x a2=0x%x a3=0x%x a4=0x%x\n",
               num, a1, a2, a3, a4);
    switch (num) {
    case 3:  return lx_sc_read(a1, a2, a3);
    case 4:  return lx_sc_write(a1, a2, a3);
    case 5:  return lx_sc_open(a1, a2, a3);
    case 6:  return lx_sc_close(a1);
    case 19: return 0;                          /* lseek: 无文件 */
    case 20: return lx_sc_getpid();
    case 33: return lx_sc_access(a1, a2);
    case 37: return lx_sc_kill(a1, a2);
    case 45: return lx_sc_brk(a1);
    case 54: return lx_sc_ioctl(a1, a2, a3);
    case 57: return -LX_ENOSYS;                 /* setpgid */
    case 64: return lx_sc_getppid();
    case 65: return lx_sc_getpgrp();
    case 66: return lx_sc_setsid();
    case 78: return lx_sc_gettimeofday(a1, a2);
    case 85: return lx_sc_readlink(a1, a2, a3);
    case 91: return lx_sc_munmap(a1, a2);
    case 12: return lx_sc_chdir(a1);
    case 13: return lx_sc_time(a1);
    case 122: return lx_sc_uname(a1);
    case 125: return lx_sc_mprotect(a1, a2, a3);
    case 141: return lx_sc_getdents64(a1, a2, a3);   /* getdents(旧) 同处理 */
    case 145: return lx_sc_readv(a1, a2, a3);
    case 146: return lx_sc_writev(a1, a2, a3);
    case 158: return lx_sc_sched_yield();
    case 162: return lx_sc_nanosleep(a1, a2);
    case 163: return lx_sc_mremap(a1, a2, a3, a4);
    case 168: return lx_sc_poll(a1, a2, a3);
    case 174: return lx_sc_rt_sigaction(a1, a2, a3, a4);
    case 175: return lx_sc_rt_sigprocmask(a1, a2, a3, a4);
    case 183: return lx_sc_getcwd(a1, a2);
    case 186: return lx_sc_sigaltstack(a1, a2);
    case 191: return lx_sc_ugetrlimit(a1, a2);
    case 192: return lx_sc_mmap2(a1, a2, a3, a4, a5, 0);
    case 195: return lx_sc_stat64(a1, a2);      /* stat64 */
    case 196: return lx_sc_stat64(a1, a2);      /* lstat64 */
    case 197: return lx_sc_fstat64(a1, a2);     /* fstat64 */
    case 199: return 0;                         /* getuid32 */
    case 200: return 0;                         /* getgid32 */
    case 201: return 0;                         /* geteuid32 */
    case 202: return 0;                         /* getegid32 */
    case 217: return lx_sc_getdents64(a1, a2, a3);
    case 220: return lx_sc_madvise(a1, a2, a3);
    case 221: return lx_sc_fcntl64(a1, a2, a3);
    case 240: return lx_sc_futex(a1, a2, a3, a4);
    case 248: lx_sc_exit_group(); return 0;     /* exit_group */
    case 256: return 0;                         /* set_tid_address */
    case 263: return lx_sc_clock_gettime(a1, a2);
    case 264: return lx_sc_clock_getres(a1, a2);
    case 268: return lx_sc_tgkill(a1, a2, a3);
    case 322: return lx_sc_open(a2, a3, a4);    /* openat: 忽略 dirfd */
    case 327: return lx_sc_stat64(a2, a3);      /* fstatat64 */
    case 332: return lx_sc_readlinkat(a1, a2, a3, a4);  /* readlinkat */
    case 338: return 0;                         /* set_robust_list */
    case 358: return lx_sc_dup3(a1, a2, a3);
    case 359: return lx_sc_pipe2();
    case 369: return lx_sc_prlimit64(a1, a2, a3, a4);
    case 384: return lx_sc_getrandom(a1, a2, a3);
    case 1:   lx_sc_exit_group(); return 0;     /* exit */
    case 2:   return lx_sc_fork();              /* fork */
    case 11:  return lx_sc_execve();            /* execve */
    case 114: return lx_sc_wait4();             /* wait4 */
    case 983045: return lx_sc_set_tls(a1);      /* ARM EABI set_tls */
    default:
        return -LX_ENOSYS;      /* 静默 (同 x86 策略, 不打印) */
    }
}
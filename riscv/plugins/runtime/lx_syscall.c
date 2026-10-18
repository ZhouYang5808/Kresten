/* LX syscall 翻译层 (RISC-V RV64 版)。
 * 入口: boot/start.S 的 trap_entry (guest 以 U-mode 运行, ecall 自陷)。
 * 把 Linux RISC-V 64 位 syscall ABI 翻译为 Kresten 内核服务。
 * RV64 特性: 64 位指针/结构体, mmap 无 mmap2 (offset 按字节),
 * 无 set_tls (riscv glibc 直接写 tp), stat 为 128 字节 riscv 布局。
 * 未知 syscall 返回 -LX_ENOSYS 静默。 */

#include "lx_int.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <rtc.h>
#include <keyboard.h>

/* ecall 陷阱栈与内核 sp 保存区 (start.S 引用) */
/* ecall 陷阱栈: 已移到内核 lx_bridge.c (start.S 共用) */

/* brk/mmap 游标: 会话级重置 (lx_session_reset) 需要文件作用域 */
static uint64_t lx_brk_cur = LX_BRK_BASE;
static uint64_t lx_mmap_cur = LX_MMAP_BASE;
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
        if (c == '\b') c = 0x7F;    /* VERASE=0x7F, ash 才能删除 */
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
    if (addr == 0) return (long)lx_brk_cur;
    if ((uint64_t)addr < LX_BRK_BASE ||
        (uint64_t)addr > LX_BRK_BASE + LX_BRK_SIZE)
        return (long)lx_brk_cur;
    lx_brk_cur = (uint64_t)addr;
    return (long)lx_brk_cur;
}

#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20

/* RISC-V: mmap 第 6 参 offset 按字节 (无 mmap2); 只支持匿名映射 */
static long lx_sc_mmap(long addr, long length, long prot, long flags, long fd, long offset) {
    (void)prot;
    (void)offset;
    uint64_t len = (uint64_t)length;
    if (len == 0) return LX_EINVAL;
    if ((flags & MAP_FIXED) || addr != 0) return LX_EINVAL;
    len = (len + 4095) & ~4095ull;
    uint64_t p = lx_mmap_cur;
    if (p + len > LX_MMAP_BASE + LX_MMAP_SIZE) return LX_ENOMEM;
    lx_mmap_cur = p + len;
    memset((void *)(uintptr_t)p, 0, (size_t)len);
    if (!(flags & MAP_ANONYMOUS) && fd >= 0 && fd < 3) return LX_EACCES;
    return (long)p;
}

static long lx_sc_munmap(long addr, long length) {
    (void)addr;
    (void)length;
    return 0;
}

/* 会话级重置: 每个 `vm linux` 会话开始前调用 (lx_proc.c)。
 * brk/mmap 游标跨会话单调增长, 16MB 池最终耗尽拒绝新映射。 */
void lx_session_reset(void) {
    lx_brk_cur = LX_BRK_BASE;
    lx_mmap_cur = LX_MMAP_BASE;
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

/* ---- 时间 (RV64: timeval/timespec 均 16 字节) ---- */

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

typedef struct { int64_t tv_sec; int64_t tv_usec; } lx_timeval;
typedef struct { int64_t tv_sec; int64_t tv_nsec; } lx_timespec;

static long lx_sc_gettimeofday(long tv, long tz) {
    (void)tz;
    lx_timeval *p = (lx_timeval *)(uintptr_t)tv;
    if (!p) return LX_EINVAL;
    p->tv_sec = lx_rtc_epoch();
    p->tv_usec = 0;
    return 0;
}

static long lx_sc_clock_gettime(long clock_id, long ts) {
    (void)clock_id;
    lx_timespec *p = (lx_timespec *)(uintptr_t)ts;
    if (!p) return LX_EINVAL;
    p->tv_sec = lx_rtc_epoch();
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
    lx_resume_helper();         /* start.S: 恢复内核 sp/s0..s11/ra, 永不返回 */
    for (;;) { }
}

/* RV64 sigaction: handler(8) + flags(8) + mask(16) + restorer(8) = 40 字节 */
static long lx_sc_rt_sigaction(long sig, long act, long oldact, long size) {
    (void)sig;
    (void)act;
    (void)size;
    if (oldact) memset((void *)(uintptr_t)oldact, 0, 40);
    return 0;
}

static long lx_sc_rt_sigprocmask(long how, long set, long oldset, long size) {
    (void)how;
    (void)set;
    (void)size;
    if (oldset) memset((void *)(uintptr_t)oldset, 0, 16);
    return 0;
}

/* RV64 stack_t: ss_sp(8) + ss_flags(4) + pad(4) + ss_size(8) = 24 字节 */
static long lx_sc_sigaltstack(long ss, long oss) {
    (void)ss;
    if (oss) memset((void *)(uintptr_t)oss, 0, 24);
    return 0;
}

static long lx_sc_getrandom(long buf, long len, long flags) {
    (void)flags;
    uint64_t seed = (uint64_t)lx_rtc_epoch() ^ (uint64_t)(uintptr_t)buf;
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
    uint32_t d = (uint32_t)((p->tv_sec * 100) + (p->tv_nsec / 10000000));
    while (sched_get_ticks() - start < d) { }
    return 0;
}

/* ---- 终端 (RV64 termios 60 字节) ---- */

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

/* riscv64 glibc 用 termios2 (struct termios2 = 44 字节):
 * _IOC 编码: dir 在 bits 30-31 — 读=2(0x8...), 写=1(0x4...) */
#define LX_TCGETS2  0x802C542A
#define LX_TCSETS2  0x402C542B
#define LX_TCSETSW2 0x402C542C
#define LX_TCSETSF2 0x402C542D

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

static void lx_fill_termios2(void *dst) {
    /* asm-generic termios2: iflag,oflag,cflag,lflag (各4) + line(1) + c_cc[19] + ispeed/ospeed (各4) */
    uint8_t *t = (uint8_t *)dst;
    uint32_t u;
    memset(t, 0, 44);
    u = LX_BRKINT | LX_ICRNL | LX_IXON;  memcpy(t, &u, 4);
    u = LX_OPOST | LX_ONLCR;             memcpy(t + 4, &u, 4);
    u = LX_CS8 | LX_CREAD | LX_B38400;   memcpy(t + 8, &u, 4);
    u = LX_ISIG | LX_ICANON | LX_ECHO | LX_ECHOE | LX_ECHOK | LX_ECHONL;
                                         memcpy(t + 12, &u, 4);
    t[16] = 0;                            /* c_line */
    t[17] = 0x03;                         /* VINTR */
    t[18] = 0x1C;                         /* VQUIT */
    t[19] = 0x7F;                         /* VERASE */
    t[20] = 0x15;                         /* VKILL */
    t[21] = 0x04;                         /* VEOF */
    t[22] = 1;                            /* VTIME */
    t[23] = 0;                            /* VMIN */
    t[24] = 0x11;                         /* VSTART */
    t[25] = 0x13;                         /* VSTOP */
    t[26] = 0x1A;                         /* VSUSP */
    u = LX_B38400; memcpy(t + 36, &u, 4); /* ispeed */
    u = LX_B38400; memcpy(t + 40, &u, 4); /* ospeed */
}

static long lx_sc_ioctl(long fd, long req, long arg) {
    if (fd < 0 || fd > 2) return LX_EBADF;
    switch (req) {
    case LX_TCGETS:
        lx_fill_termios((lx_termios *)(uintptr_t)arg);
        return 0;
    case LX_TCGETS2:
        lx_fill_termios2((void *)(uintptr_t)arg);
        return 0;
    case LX_TCSETS:
    case LX_TCSETSW:
    case LX_TCSETSF:
    case LX_TCSBRK:
    case LX_TCSETS2:
    case LX_TCSETSW2:
    case LX_TCSETSF2:
        return 0;
    case LX_TIOCGWINSZ:
        memset((void *)(uintptr_t)arg, 0, 8);   /* winsize */
        return 0;
    case LX_TIOCSCTTY:
        return -LX_ENOTTY;      /* job control 关闭 */
    default:
        return -LX_ENOTTY;
    }
}

/* ---- 文件: RISC-V 无文件系统, /dev 伪设备 + 一律 ENOENT ---- */

static int lx_is_devpath(const char *p) {
    return strcmp(p, "/dev/null") == 0 ||
           strcmp(p, "/dev/tty") == 0 ||
           strcmp(p, "/dev/console") == 0;
}

static long lx_sc_openat(long dirfd, long path, long flags, long mode) {
    (void)dirfd;
    (void)flags;
    (void)mode;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    if (lx_is_devpath(p)) return 3;
    if (strcmp(p, "/") == 0) return 4;      /* root dir fd */
    return LX_ENOENT;
}

static long lx_sc_close(long fd) {
    if (fd < 0 || fd > 4) return LX_EBADF;
    return 0;
}

/* RV64 struct stat (riscv, 128 字节) */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
} lx_stat;

#define LX_S_IFCHR 0x2000
#define LX_S_IFREG 0x8000
#define LX_S_IFDIR 0x4000

static void lx_fill_stat(lx_stat *s, uint32_t mode, uint64_t size) {
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

static long lx_sc_newfstatat(long dirfd, long path, long buf, long flags) {
    (void)dirfd;
    (void)flags;
    const char *p = (const char *)(uintptr_t)path;
    lx_stat *s = (lx_stat *)(uintptr_t)buf;
    if (!p || !s) return LX_EINVAL;
    if (lx_is_devpath(p)) { lx_fill_stat(s, LX_S_IFCHR | 0x1B6, 0); return 0; }
    if (strcmp(p, "/") == 0) { lx_fill_stat(s, LX_S_IFDIR | 0x1ED, 0); return 0; }
    return LX_ENOENT;
}

static long lx_sc_fstat(long fd, long buf) {
    lx_stat *s = (lx_stat *)(uintptr_t)buf;
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

static long lx_sc_readlinkat(long dirfd, long path, long buf, long bufsiz) {
    (void)dirfd;
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
    strcpy(u->machine, "riscv64");
    strcpy(u->domainname, "(none)");
    return 0;
}

/* RV64 iovec: 64 位指针 + 64 位长度 = 16 字节 */
typedef struct { uint64_t base; uint64_t len; } lx_iovec;

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
        for (uint64_t j = 0; j < v[i].len; j++) putchar(s[j]);
        total += (long)v[i].len;
    }
    return total;
}

/* RV64 rlimit: u64 cur + u64 max = 16 字节 */
typedef struct { uint64_t rlim_cur; uint64_t rlim_max; } lx_rlimit;

static long lx_sc_getrlimit(long res, long oldlim) {
    if (oldlim) {
        lx_rlimit *o = (lx_rlimit *)(uintptr_t)oldlim;
        o->rlim_cur = o->rlim_max = 0xFFFFFFFFFFFFFFFFull;
        if (res == 3) { o->rlim_cur = o->rlim_max = 0x800000ull; }  /* RLIMIT_STACK 8MB */
    }
    return 0;
}

static long lx_sc_prlimit64(long pid, long res, long newlim, long oldlim) {
    (void)pid;
    (void)newlim;
    if (oldlim) {
        lx_rlimit *o = (lx_rlimit *)(uintptr_t)oldlim;
        o->rlim_cur = o->rlim_max = 0xFFFFFFFFFFFFFFFFull;
        if (res == 3) { o->rlim_cur = o->rlim_max = 0x800000ull; }
    }
    return 0;
}

static long lx_sc_fcntl(long fd, long cmd, long arg) {
    (void)fd;
    if (cmd == 0) return arg;       /* F_DUPFD */
    if (cmd == 1) return 0;         /* F_GETFD */
    if (cmd == 2) return 0;         /* F_SETFD */
    if (cmd == 3) return 0;         /* F_GETFL */
    if (cmd == 4) return 0;         /* F_SETFL */
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
static long lx_sc_pipe2(void) { return -LX_ENOSYS; }
static long lx_sc_wait4(void) { return -LX_ENOSYS; }

/* RV64 statfs (112 字节) */
typedef struct {
    int64_t  f_type;
    int64_t  f_bsize;
    int64_t  f_blocks;
    int64_t  f_bfree;
    int64_t  f_bavail;
    int64_t  f_files;
    int64_t  f_ffree;
    int32_t  f_fsid[2];
    int64_t  f_namelen;
    int64_t  f_frsize;
    int64_t  f_flags;
    int64_t  f_spare[4];
} lx_statfs;

static void lx_fill_statfs(lx_statfs *s) {
    memset(s, 0, sizeof(*s));
    s->f_type = 0xEF53;             /* EXT2_SUPER_MAGIC */
    s->f_bsize = 4096;
    s->f_blocks = 32768;
    s->f_bfree = 32768;
    s->f_bavail = 32768;
    s->f_namelen = 255;
    s->f_frsize = 4096;
}

static long lx_sc_statfs(long path, long buf) {
    (void)path;
    lx_statfs *s = (lx_statfs *)(uintptr_t)buf;
    if (!s) return LX_EINVAL;
    lx_fill_statfs(s);
    return 0;
}

static long lx_sc_fstatfs(long fd, long buf) {
    (void)fd;
    lx_statfs *s = (lx_statfs *)(uintptr_t)buf;
    if (!s) return LX_EINVAL;
    lx_fill_statfs(s);
    return 0;
}

typedef struct { int fd; short events; short revents; } lx_pollfd;

#define LX_POLLIN 0x001
#define LX_POLLOUT 0x004
#define LX_POLLERR 0x008
#define LX_POLLNVAL 0x010

static long lx_sc_ppoll(long fds, long nfds, long tsp, long sigmask) {
    (void)sigmask;
    lx_pollfd *p = (lx_pollfd *)(uintptr_t)fds;
    if (!p || nfds <= 0) return LX_EINVAL;
    extern uint32_t sched_get_ticks(void);
    uint32_t deadline = 0;
    if (tsp != 0) {
        long sec = *(long *)(uintptr_t)tsp;
        long nsec = *(long *)(uintptr_t)(tsp + 8);
        long ms = sec * 1000 + nsec / 1000000;
        deadline = sched_get_ticks() + (uint32_t)(ms / 10) + 1;
    }
    for (;;) {
        long ready = 0;
        for (long i = 0; i < nfds && i < 64; i++) {
            p[i].revents = 0;
            if (p[i].fd == 1 || p[i].fd == 2) {
                if (p[i].events & LX_POLLOUT) { p[i].revents |= LX_POLLOUT; ready++; }
            } else if (p[i].fd == 0) {
                extern int keyboard_pending(void);
                if (keyboard_pending()) {
                    if (p[i].events & LX_POLLIN) { p[i].revents |= LX_POLLIN; ready++; }
                } else if (!(p[i].events & LX_POLLIN)) {
                    p[i].revents |= LX_POLLIN;
                    ready++;
                }
            } else {
                p[i].revents |= LX_POLLNVAL;    /* 无效 fd: 立即返回 */
                ready++;
            }
        }
        if (ready) return ready;
        if (tsp != 0 && sched_get_ticks() >= deadline) return 0;
        while (!keyboard_pending()) {           /* tsp==NULL: 无限等待 */
            if (tsp != 0 && sched_get_ticks() >= deadline) return 0;
        }
    }
}

/* ---- 主分发 (RISC-V 64 位 syscall 号) ---- */

/* start.S trap_entry 对无法处理的异常 (mcause != ecall/ebreak) 调用:
 * 打印后挂起 (无 IDT/异常恢复机制)。mstatus 用于诊断访客特权模式。
 * 实现名 _impl: 由 vm.kplg 的 init 注册到内核桥指针 trap_unhandled。 */
void trap_unhandled_impl(long mcause, long mepc, long mstatus) {
    printf("\n[LX] unhandled trap: mcause=0x%lx mepc=0x%lx mstatus=0x%lx (guest halted)\n",
           mcause, mepc, mstatus);
    for (;;) { }
}

/* 实现名 _impl: 由 vm.kplg 的 init 注册到内核桥指针 linux_syscall_dispatch */
long linux_syscall_impl(long num, long a1, long a2, long a3, long a4, long a5) {
    switch (num) {
    case 63:  return lx_sc_read(a1, a2, a3);
    case 64:  return lx_sc_write(a1, a2, a3);
    case 56:  return lx_sc_openat(a1, a2, a3, a4);
    case 57:  return lx_sc_close(a1);
    case 62:  return 0;                         /* lseek: 无文件 */
    case 214: return lx_sc_brk(a1);
    case 222: return lx_sc_mmap(a1, a2, a3, a4, a5, 0);
    case 215: return lx_sc_munmap(a1, a2);
    case 216: return lx_sc_mremap(a1, a2, a3, a4);
    case 226: return lx_sc_mprotect(a1, a2, a3);
    case 233: return lx_sc_madvise(a1, a2, a3);
    case 134: return lx_sc_rt_sigaction(a1, a2, a3, a4);
    case 135: return lx_sc_rt_sigprocmask(a1, a2, a3, a4);
    case 132: return lx_sc_sigaltstack(a1, a2);
    case 29:  return lx_sc_ioctl(a1, a2, a3);
    case 17:  return lx_sc_getcwd(a1, a2);
    case 49:  return lx_sc_chdir(a1);
    case 48:  return lx_sc_access(a1, a2);
    case 160: return lx_sc_uname(a1);
    case 174: return 0;                         /* getuid */
    case 175: return 0;                         /* geteuid */
    case 176: return 0;                         /* getgid */
    case 177: return 0;                         /* getegid */
    case 172: return lx_sc_getpid();
    case 173: return lx_sc_getppid();
    case 178: return lx_sc_getpid();            /* gettid */
    case 93:  lx_sc_exit_group(); return 0;     /* exit */
    case 94:  lx_sc_exit_group(); return 0;     /* exit_group */
    case 131: return lx_sc_tgkill(a1, a2, a3);
    case 129: return lx_sc_kill(a1, a2);
    case 78:  return lx_sc_readlinkat(a1, a2, a3, a4);
    case 278: return lx_sc_getrandom(a1, a2, a3);
    case 113: return lx_sc_clock_gettime(a1, a2);
    case 114: return lx_sc_clock_getres(a1, a2);
    case 169: return lx_sc_gettimeofday(a1, a2);
    case 98:  return lx_sc_futex(a1, a2, a3, a4);
    case 66:  return lx_sc_writev(a1, a2, a3);
    case 65:  return lx_sc_readv(a1, a2, a3);
    case 25:  return lx_sc_fcntl(a1, a2, a3);
    case 24:  return lx_sc_dup3(a1, a2, a3);
    case 79:  return lx_sc_newfstatat(a1, a2, a3, a4);  /* stat/lstat */
    case 80:  return lx_sc_fstat(a1, a2);
    case 61:  return lx_sc_getdents64(a1, a2, a3);
    case 73:  return lx_sc_ppoll(a1, a2, a3, a4);
    case 101: return lx_sc_nanosleep(a1, a2);
    case 124: return lx_sc_sched_yield();
    case 96:  return 0;                         /* set_tid_address */
    case 99:  return 0;                         /* set_robust_list */
    case 163: return lx_sc_getrlimit(a1, a2);
    case 261: return lx_sc_prlimit64(a1, a2, a3, a4);
    case 166: return 0;                         /* umask */
    case 283: return 0;                         /* membarrier */
    case 259: return 0;                         /* riscv_flush_icache */
    case 258: return -LX_ENOSYS;                /* riscv_hwprobe */
    case 154: return -LX_ENOSYS;                /* setpgid */
    case 157: return lx_sc_setsid();
    case 260: return lx_sc_wait4();             /* wait4 */
    case 221: return lx_sc_execve();            /* execve */
    case 95:  return -LX_ENOSYS;                /* waitid */
    case 59:  return lx_sc_pipe2();             /* pipe2 */
    case 43:  return lx_sc_statfs(a1, a2);
    case 44:  return lx_sc_fstatfs(a1, a2);
    default:
        return -LX_ENOSYS;      /* 静默 (不打印) */
    }
}
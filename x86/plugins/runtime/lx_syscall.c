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

/* struct iovec for recvmsg/sendmsg */
struct iovec {
    void  *iov_base;
    size_t iov_len;
};

/* 调试追踪开关: 默认关闭 (逐 syscall / 路径诊断会刷屏; 需要时置 1 重编译)。 */
static int lx_dbg_trace = 0;
#define LXT(...) do { if (lx_dbg_trace) printf(__VA_ARGS__); } while (0)

/* syscall 运行时状态（.bss）。注意：trap 栈必须声明在状态变量之后，
 * 保证 lea lx_trap_stack+8192 的栈顶与 iret 帧落在纯粹栈区，
 * 不与下方状态变量重叠（bss 按声明顺序连续分配）。 */
uint64_t linux_user_rsp __attribute__((visibility("hidden"))) = 0;
uint64_t linux_ret_rip __attribute__((visibility("hidden"))) = 0;
uint64_t linux_ret_rflags __attribute__((visibility("hidden"))) = 0;
uint8_t lx_trap_stack[8192] __attribute__((aligned(16), visibility("hidden")));

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
        "pushq 8(%rsp)\n\t"             /* 第7参(offset) = 保存槽[原r9]; 被调函数读 [rbp+0x10]=[rsp_call] */
        "call linux_syscall_dispatch\n\t"
        "addq $8, %rsp\n\t"
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

#include "wine_iso_map.h"

/* wine loader 的真实路径 (realpath /proc/self/exe 的结果,
 * 与 lx_proc.c lx_run_wine 的装载来源一致; tiny ISO 上为 /WINE/WINE) */
#define LX_WINE_EXE "/usr/lib/x86_64-linux-gnu/wine/wine"

#define LX_ENOSYS (-38L)
#define LX_ENOMEM (-12L)
#define LX_EINVAL (-22L)
#define LX_EBADF  (-9L)
#define LX_ENOTTY (-25L)
#define LX_ENOENT (-2L)
#define LX_EMFILE (-24L)
#define LX_ECHILD (-10L)
#define LX_EACCES (-13L)
#define LX_ENOTDIR (-20L)
#define LX_EISDIR  (-21L)
#define LX_EEXIST  (-17L)
#define LX_ERANGE  (-34L)
#define LX_EAFNOSUPPORT (-97L)
#define LX_ECONNREFUSED (-111L)
#define LX_ENOTCONN   (-107L)
#define LX_EINPROGRESS (-115L)

/* rootfs 文件描述符表(定义提前: readv 也使用) */
#define LX_FD_MAX 64

#define LX_AF_UNIX 1
#define LX_SOCK_STREAM 1

typedef struct {
    int in_use;
    const rf_node *node;
    uint32_t off;
    iso_file_t iso;
    int iso_fd;
    int synth;
    int vn;
    int sock;
} lx_fd;

static lx_fd lx_fds[LX_FD_MAX];

typedef struct {
    int in_use;
    int type;
    int state;
    char path[160];
    int peer;
    char rxbuf[4096];
    int rx_len, rx_off;
    char txbuf[4096];
    int txbuf_len;
} lx_sock;

static lx_sock lx_socks[LX_FD_MAX];

static long lx_sc_socket(long domain, long type, long protocol) {
    (void)protocol;
    if (domain != LX_AF_UNIX || type != LX_SOCK_STREAM) return LX_EAFNOSUPPORT;
    for (int i = 3; i < LX_FD_MAX; i++) {
        if (!lx_fds[i].in_use) {
            lx_fds[i].in_use = 1;
            lx_fds[i].vn = -1;
            lx_fds[i].node = NULL;
            lx_fds[i].iso_fd = 0;
            lx_fds[i].synth = 0;
            lx_fds[i].sock = i;
            memset(&lx_socks[i], 0, sizeof(lx_sock));
            lx_socks[i].type = LX_SOCK_STREAM;
            return i;
        }
    }
    return LX_EMFILE;
}

static long lx_sc_socketpair(long domain, long type, long protocol, long sv) {
    (void)protocol;
    if (domain != LX_AF_UNIX || type != LX_SOCK_STREAM) return LX_EAFNOSUPPORT;
    int *socksv = (int *)(uintptr_t)sv;
    if (!socksv) return LX_EINVAL;
    int fd[2] = {-1, -1};
    for (int i = 0; i < 2; i++) {
        for (int j = 3; j < LX_FD_MAX; j++) {
            if (!lx_fds[j].in_use) {
                fd[i] = j;
                lx_fds[j].in_use = 1;
                lx_fds[j].vn = -1;
                lx_fds[j].node = NULL;
                lx_fds[j].iso_fd = 0;
                lx_fds[j].synth = 0;
                lx_fds[j].sock = j;
                memset(&lx_socks[j], 0, sizeof(lx_sock));
                lx_socks[j].type = LX_SOCK_STREAM;
                lx_socks[j].state = 3;
                break;
            }
        }
    }
    if (fd[0] < 0 || fd[1] < 0) return LX_EMFILE;
    lx_socks[fd[0]].peer = fd[1];
    lx_socks[fd[1]].peer = fd[0];
    socksv[0] = fd[0];
    socksv[1] = fd[1];
    LXT("[LX] socketpair -> %d,%d\n", fd[0], fd[1]);
    return 0;
}

static long lx_sc_bind(long fd, long addr, long addrlen) {
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    struct { short sun_family; char sun_path[108]; } *sa = (void *)(uintptr_t)addr;
    if (!sa || sa->sun_family != LX_AF_UNIX) return LX_EINVAL;
    strncpy(lx_socks[fd].path, sa->sun_path, sizeof(lx_socks[fd].path) - 1);
    lx_socks[fd].state = 1;
    return 0;
}

static long lx_sc_listen(long fd, long backlog) {
    (void)backlog;
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    if (lx_socks[fd].state != 1) return LX_EINVAL;
    lx_socks[fd].state = 2;
    return 0;
}

static long lx_sc_accept4(long fd, long addr, long addrlen, long flags) {
    (void)flags; (void)addr; (void)addrlen;
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    if (lx_socks[fd].state != 2) return LX_EINVAL;
    for (int i = 3; i < LX_FD_MAX; i++) {
        if (!lx_fds[i].in_use) {
            lx_fds[i].in_use = 1;
            lx_fds[i].vn = -1;
            lx_fds[i].node = NULL;
            lx_fds[i].iso_fd = 0;
            lx_fds[i].synth = 0;
            lx_fds[i].sock = i;
            memset(&lx_socks[i], 0, sizeof(lx_sock));
            lx_socks[i].type = LX_SOCK_STREAM;
            lx_socks[i].state = 3;
            lx_socks[i].peer = fd;
            lx_socks[fd].peer = i;
            return i;
        }
    }
    return LX_EMFILE;
}

/* Minimal wineserver protocol handling */
#define WINE_REQ_SET_PROCESS_INFO    1
#define WINE_REQ_GET_PROCESS_INFO    2
#define WINE_REQ_SET_THREAD_INFO     3
#define WINE_REQ_GET_THREAD_INFO     4
#define WINE_REQ_SET_SIGNAL_INFO     5
#define WINE_REQ_PROCESS_READY       6
#define WINE_REQ_INIT_PROCESS_DONE   7
#define WINE_REQ_OPEN_PROCESS        8
#define WINE_REQ_LOAD_REGISTRY       9
#define WINE_REQ_REG_OPEN_KEY       10
#define WINE_REQ_REG_QUERY_VALUE    11

static void lx_wine_server_init(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;
    /* wineserver child creates /tmp/.wine-0/server-<dev>-<ino> directory and socket */
    /* The dev/ino come from stat(".") of /root/.wine -> st_dev=0x7777, st_ino=0xF0000+0=0xF0000 */
    const char *sock_path = "/tmp/.wine-0/server-7777-f0000/socket";
    
    /* Ensure directory exists (vnode already seeded) */
    for (int i = 3; i < LX_FD_MAX; i++) {
        if (!lx_fds[i].in_use) {
            lx_fds[i].in_use = 1;
            lx_fds[i].vn = -1;
            lx_fds[i].node = NULL;
            lx_fds[i].iso_fd = 0;
            lx_fds[i].synth = 0;
            lx_fds[i].sock = i;
            memset(&lx_socks[i], 0, sizeof(lx_sock));
            lx_socks[i].type = LX_SOCK_STREAM;
            lx_socks[i].state = 2;  /* listening */
            strncpy(lx_socks[i].path, sock_path, sizeof(lx_socks[i].path) - 1);
            printf("[LX] wineserver socket created at %s\n", sock_path);
            break;
        }
    }
    printf("[LX] wineserver initialized (in-kernel)\n");
}

/* Minimal wineserver protocol handler */
#define WINE_REQ_SET_PROCESS_INFO    1
#define WINE_REQ_GET_PROCESS_INFO    2
#define WINE_REQ_SET_SIGNAL_INFO     5
#define WINE_REQ_PROCESS_READY       6
#define WINE_REQ_INIT_PROCESS_DONE   7
#define WINE_REQ_OPEN_PROCESS        8
#define WINE_REQ_LOAD_REGISTRY       9
#define WINE_REQ_REG_OPEN_KEY       10
#define WINE_REQ_REG_QUERY_VALUE    11

static void lx_wine_handle_request(int client_fd) {
    if (lx_socks[client_fd].rx_len < 16) return;
    uint32_t *hdr = (uint32_t *)lx_socks[client_fd].rxbuf;
    uint32_t req = hdr[0];
    uint32_t reply_size = hdr[1];
    uint32_t req_id = hdr[2];
    LXT("[LX] wine req=%u reply_size=%u id=%u\n", req, reply_size, req_id);
    
    char reply[128];
    memset(reply, 0, sizeof(reply));
    uint32_t *rhdr = (uint32_t *)reply;
    rhdr[0] = req;
    rhdr[1] = 0;
    rhdr[2] = req_id;
    rhdr[3] = 0;  /* status = success */
    
    size_t rlen = 16;
    switch (req) {
    case WINE_REQ_SET_PROCESS_INFO:
    case WINE_REQ_GET_PROCESS_INFO:
    case WINE_REQ_SET_SIGNAL_INFO:
    case WINE_REQ_PROCESS_READY:
    case WINE_REQ_INIT_PROCESS_DONE:
    case WINE_REQ_OPEN_PROCESS:
    case WINE_REQ_LOAD_REGISTRY:
    case WINE_REQ_REG_OPEN_KEY:
    case WINE_REQ_REG_QUERY_VALUE:
        /* Minimal success response */
        break;
    }
    
    if (lx_socks[client_fd].txbuf_len + rlen > sizeof(lx_socks[client_fd].txbuf)) 
        rlen = sizeof(lx_socks[client_fd].txbuf) - lx_socks[client_fd].txbuf_len;
    memcpy(lx_socks[client_fd].txbuf + lx_socks[client_fd].txbuf_len, reply, rlen);
    lx_socks[client_fd].txbuf_len += rlen;
}

static long lx_sc_connect(long fd, long addr, long addrlen) {
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    struct { short sun_family; char sun_path[108]; } *sa = (void *)(uintptr_t)addr;
    if (!sa || sa->sun_family != LX_AF_UNIX) return LX_EINVAL;
    LXT("[LX] connect: checking path '%s'\n", sa->sun_path);
    for (int i = 3; i < LX_FD_MAX; i++) {
        if (lx_fds[i].in_use && lx_fds[i].sock == i && lx_socks[i].state == 2 &&
            strcmp(lx_socks[i].path, sa->sun_path) == 0) {
            lx_socks[fd].state = 3;
            lx_socks[fd].peer = i;
            lx_socks[i].peer = fd;
            LXT("[LX] connect: found existing socket, connecting\n");
            return 0;
        }
    }
    /* Auto-create wineserver socket if connecting to wineserver path */
    if (strstr(sa->sun_path, "wineserver") || strstr(sa->sun_path, "/tmp/.wine-")) {
        LXT("[LX] connect: auto-creating wineserver socket\n");
        lx_wine_server_init();
        for (int i = 3; i < LX_FD_MAX; i++) {
            if (lx_fds[i].in_use && lx_fds[i].sock == i && lx_socks[i].state == 2 &&
                strcmp(lx_socks[i].path, sa->sun_path) == 0) {
                lx_socks[fd].state = 3;
                lx_socks[fd].peer = i;
                lx_socks[i].peer = fd;
                LXT("[LX] connect: created new socket, connecting\n");
                return 0;
            }
        }
        /* If wine uses short 'socket' path, try to match our socket */
        if (!strstr(sa->sun_path, "wineserver") && strstr(sa->sun_path, "socket")) {
            LXT("[LX] connect: wine used short socket path, trying to match\n");
            /* Check if our socket vnode exists and connect to it */
            for (int i = 3; i < LX_FD_MAX; i++) {
                if (lx_fds[i].in_use && lx_fds[i].sock == i && lx_socks[i].state == 2) {
                    lx_socks[fd].state = 3;
                    lx_socks[fd].peer = i;
                    lx_socks[i].peer = fd;
                    LXT("[LX] connect: connected to our socket at '%s'\n", lx_socks[i].path);
                    return 0;
                }
            }
        }
    } else if (strstr(sa->sun_path, "socket")) {
        /* Wine used just 'socket' path - connect to our default socket */
        LXT("[LX] connect: wine used 'socket' path, connecting to our default\n");
        for (int i = 3; i < LX_FD_MAX; i++) {
            if (lx_fds[i].in_use && lx_fds[i].sock == i && lx_socks[i].state == 2) {
                lx_socks[fd].state = 3;
                lx_socks[fd].peer = i;
                lx_socks[i].peer = fd;
                LXT("[LX] connect: connected to our socket at '%s'\n", lx_socks[i].path);
                return 0;
            }
        }
    }
    LXT("[LX] connect: socket init done but not found\n");
    return LX_ECONNREFUSED;
}

static long lx_sc_send(long fd, long buf, long len, long flags) {
    (void)flags;
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    int peer = lx_socks[fd].peer;
    if (peer < 0 || peer >= LX_FD_MAX || !lx_fds[peer].in_use) return LX_ENOTCONN;
    size_t l = len > sizeof(lx_socks[peer].rxbuf) - lx_socks[peer].rx_len ? sizeof(lx_socks[peer].rxbuf) - lx_socks[peer].rx_len : len;
    memcpy(lx_socks[peer].rxbuf + lx_socks[peer].rx_len, (void *)(uintptr_t)buf, l);
    lx_socks[peer].rx_len += l;
    return l;
}

static long lx_sc_recv(long fd, long buf, long len, long flags) {
    (void)flags;
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    if (lx_socks[fd].rx_len == 0) return 0;
    size_t l = len < lx_socks[fd].rx_len ? len : lx_socks[fd].rx_len;
    memcpy((void *)(uintptr_t)buf, lx_socks[fd].rxbuf, l);
    memmove(lx_socks[fd].rxbuf, lx_socks[fd].rxbuf + l, lx_socks[fd].rx_len - l);
    lx_socks[fd].rx_len -= l;
    /* If this is a wineserver client socket, process any complete requests */
    if (lx_socks[fd].peer >= 0 && lx_socks[fd].peer < LX_FD_MAX && 
        lx_fds[lx_socks[fd].peer].in_use && lx_fds[lx_socks[fd].peer].sock == lx_socks[fd].peer) {
        lx_wine_handle_request(fd);
    }
    return l;
}

static long lx_sc_recvmsg(int fd, long iov_int, long iovcnt_long, long unused_long, long resid_long) {
    struct iovec *iov = (struct iovec *)(uintptr_t)iov_int;
    int iovcnt = (int)iovcnt_long;
    (void)unused_long;
    size_t *resid = (size_t *)(uintptr_t)resid_long;
    LXT("[LX] recvmsg: fd=%d, iovcnt=%d\n", fd, iovcnt);
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) { LXT("[LX] recvmsg: bad fd, returning EBADF\n"); return LX_EBADF; }
    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0) continue;
        size_t l = lx_socks[fd].rx_len;
        if (l > (size_t)iov[i].iov_len) l = (size_t)iov[i].iov_len;
        if (l > 0) {
            memcpy((void *)(uintptr_t)iov[i].iov_base, lx_socks[fd].rxbuf + (total ? total : 0), l);
            total += l;
        }
        if (total >= (size_t)iov[i].iov_len) break;
    }
    if (resid) *resid = lx_socks[fd].rx_len - total;
        LXT("[LX] recvmsg: returning %ld bytes\n", total);
    return total;
}

static long lx_sc_sendmsg(int fd, long iov_int, long iovcnt_long, long unused_long, long resid_long) {
    struct iovec *iov = (struct iovec *)(uintptr_t)iov_int;
    int iovcnt = (int)iovcnt_long;
    (void)unused_long;
    size_t *resid = (size_t *)(uintptr_t)resid_long;
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use || lx_fds[fd].sock != fd) return LX_EBADF;
    long total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len <= 0) continue;
        size_t l = iov[i].iov_len;
        if (lx_socks[fd].txbuf_len + l > sizeof(lx_socks[fd].txbuf)) {
            l = sizeof(lx_socks[fd].txbuf) - lx_socks[fd].txbuf_len;
        }
        if (l > 0) {
            memcpy(lx_socks[fd].txbuf + lx_socks[fd].txbuf_len, (void *)(uintptr_t)iov[i].iov_base, l);
            lx_socks[fd].txbuf_len += l;
            total += l;
        }
    }
    if (resid) *resid = lx_socks[fd].txbuf_len - total;
    return total;
}

    void  *iov_base;
    size_t iov_len;

/* 虚拟 /etc/passwd, /etc/group: ntdll 启动 getpwuid()/getgrgid() 需要
 * passwd 条目; 否则 pw_dir==NULL → strdup(NULL) → strlen #PF (CR2=0)。
 * glibc nss: /etc/nsswitch.conf ENOENT→默认 "files", 再 open+读取这两个文件。 */
static const char lx_synth_passwd[] = "root:x:0:0:root:/root:/bin/sh\n";
static const char lx_synth_group[]  = "root:x:0:\n";

static const char *lx_synth_data(int s) {
    return s == 1 ? lx_synth_passwd : lx_synth_group;
}
static long lx_synth_size(int s) {
    return s == 1 ? (long)sizeof(lx_synth_passwd) - 1
                  : (long)sizeof(lx_synth_group) - 1;
}

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
    if (lx_fds[fd].synth) {
        long left = lx_synth_size(lx_fds[fd].synth) - lx_fds[fd].off;
        if (left <= 0) return 0;
        if (len > left) len = left;
        memcpy((void *)(uintptr_t)buf,
               lx_synth_data(lx_fds[fd].synth) + lx_fds[fd].off, (size_t)len);
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

static long lx_sc_pread64(long fd, long buf, long len, long off) {
    if (!buf || len <= 0) return LX_EINVAL;
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    if (lx_fds[fd].iso_fd) {
        if (lx_fds[fd].iso.flags & 2) return LX_EISDIR;
        long left = lx_fds[fd].iso.size - off;
        if (left <= 0) return 0;
        if (len > left) len = left;
        if (iso_read_at(&lx_fds[fd].iso, (uint32_t)off, (void *)(uintptr_t)buf,
                        (uint32_t)len) != 0)
            return LX_EINVAL;
        return len;
    }
    if (lx_fds[fd].synth) {
        if (off >= lx_synth_size(lx_fds[fd].synth)) return 0;
        long left = lx_synth_size(lx_fds[fd].synth) - off;
        if (len > left) len = left;
        memcpy((void *)(uintptr_t)buf, lx_synth_data(lx_fds[fd].synth) + off,
               (size_t)len);
        return len;
    }
    const rf_node *n = lx_fds[fd].node;
    if (!n || n->type == RF_TYPE_DIR) return LX_EISDIR;
    if (n->type == RF_TYPE_SYMLNK) return LX_EISDIR;
    if (off >= (long)n->size) return 0;
    long left = n->size - off;
    if (len > left) len = left;
    memcpy((void *)(uintptr_t)buf, rf_data(n) + off, (size_t)len);
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
#define LX_MAP_FIXED_NOREPLACE 0x100000   /* glibc dl-map-segments: 占用则 EEXIST */

/* mmap 占用跟踪 (glibc 2.35+ 用 MAP_FIXED_NOREPLACE 探测空闲槽:
 * 成功则记录, 重叠返回 EEXIST 让 ld.so 走下一槽位; munmap 释放) */
static struct { uint64_t s, e; } lx_mtrack[64];
static int lx_mtrack_n = 0;
static uint64_t lx_mmap_cur = LX_MMAP_BASE;

static int lx_mtrack_overlap(uint64_t s, uint64_t e) {
    for (int i = 0; i < lx_mtrack_n; i++)
        if (s < lx_mtrack[i].e && e > lx_mtrack[i].s) return 1;
    return 0;
}

static void lx_mtrack_add(uint64_t s, uint64_t e) {
    for (int i = 0; i < lx_mtrack_n; i++) {
        if (s < lx_mtrack[i].e && e > lx_mtrack[i].s) {
            if (lx_mtrack[i].s < s) s = lx_mtrack[i].s;
            if (lx_mtrack[i].e > e) e = lx_mtrack[i].e;
            lx_mtrack[i] = lx_mtrack[lx_mtrack_n - 1];
            lx_mtrack_n--;
            i--;
        }
    }
    if (lx_mtrack_n < 64) {
        lx_mtrack[lx_mtrack_n].s = s;
        lx_mtrack[lx_mtrack_n].e = e;
        lx_mtrack_n++;
    }
}

static void lx_mtrack_del(uint64_t s, uint64_t e) {
    for (int i = 0; i < lx_mtrack_n; i++) {
        if (lx_mtrack[i].s >= e || lx_mtrack[i].e <= s) continue;
        if (lx_mtrack[i].s >= s && lx_mtrack[i].e <= e) {
            lx_mtrack[i] = lx_mtrack[lx_mtrack_n - 1];
            lx_mtrack_n--;
            i--;
        } else if (lx_mtrack[i].s < s && lx_mtrack[i].e > e) {
            if (lx_mtrack_n < 64) {
                lx_mtrack[lx_mtrack_n].s = e;
                lx_mtrack[lx_mtrack_n].e = lx_mtrack[i].e;
                lx_mtrack_n++;
            }
            lx_mtrack[i].e = s;
        } else if (lx_mtrack[i].s < s) {
            lx_mtrack[i].e = s;
        } else {
            lx_mtrack[i].s = e;
        }
    }
}

static long lx_sc_mmap(long addr, long length, long prot, long flags,
                       long fd, long offset) {
    (void)prot;
    uint64_t len = (uint64_t)length;
    if (len == 0) return LX_EINVAL;
    len = (len + 4095) & ~4095ULL;
    uint64_t p;
    if (flags & (MAP_FIXED | LX_MAP_FIXED_NOREPLACE)) {
        p = (uint64_t)(uintptr_t)addr & ~4095ULL;
        if (p < LX_MMAP_BASE || p + len > LX_MMAP_BASE + LX_MMAP_SIZE)
            return LX_EINVAL;       /* fixed 必须落在 mmap 池内 */
        if (flags & LX_MAP_FIXED_NOREPLACE) {
            if (lx_mtrack_overlap(p, p + len))
                return -17;         /* EEXIST: 槽位被占, ld.so 走下一槽 */
        }
        lx_mtrack_add(p, p + len);  /* 段映射与预留合并; 不推进池游标 */
    } else {
        p = lx_mmap_cur;
        if (p + len > LX_MMAP_BASE + LX_MMAP_SIZE) return LX_ENOMEM;
        lx_mmap_cur = p + len;
        lx_mtrack_add(p, p + len);
    }
    memset((void *)(uintptr_t)p, 0, (size_t)len);
    if (!(flags & MAP_ANONYMOUS) && fd >= 3 && fd < LX_FD_MAX && lx_fds[fd].in_use) {
        const rf_node *n = lx_fds[fd].node;
        if (lx_fds[fd].iso_fd) {
            /* ISO 文件 (wine tiny ISO / 光驱): 从 ISO 读入 mmap 池 */
            unsigned long off = (unsigned long)offset;
            unsigned long nc = (unsigned long)lx_fds[fd].iso.size;
            if (off > nc) nc = 0; else nc -= off;
            if (nc > (unsigned long)len) nc = (unsigned long)len;
            LXT("[LX] mmap iso: fd=%d off=0x%08x%08x p=0x%08x%08x len=0x%08x%08x nc=0x%08x%08x\n",
                   fd, (unsigned)(off >> 32), (unsigned)off,
                   (unsigned)(p >> 32), (unsigned)p,
                   (unsigned)(len >> 32), (unsigned)len,
                   (unsigned)(nc >> 32), (unsigned)nc);
            if (nc)
                iso_read_at(&lx_fds[fd].iso, (uint32_t)off, (void *)(uintptr_t)p,
                            (uint32_t)nc);
            return (long)p;
        }
        if (lx_fds[fd].synth) {
            unsigned long off = (unsigned long)offset;
            unsigned long nc = (unsigned long)lx_synth_size(lx_fds[fd].synth);
            if (off > nc) nc = 0; else nc -= off;
            if (nc > (unsigned long)len) nc = (unsigned long)len;
            memcpy((void *)(uintptr_t)p, lx_synth_data(lx_fds[fd].synth) + off,
                   nc);
        }
        if (n && (n->type == RF_TYPE_FILE || n->type == RF_TYPE_SYMLNK)) {
            unsigned long off = (unsigned long)offset;
            unsigned long nc = (unsigned long)n->size;
            if (off > nc) nc = 0; else nc -= off;
            if (nc > (unsigned long)len) nc = (unsigned long)len;
            LXT("[LX] mmap rf: fd=%d off=0x%08x%08x p=0x%08x%08x len=0x%08x%08x nc=0x%08x%08x\n",
                   fd, (unsigned)(off >> 32), (unsigned)off,
                   (unsigned)(p >> 32), (unsigned)p,
                   (unsigned)(len >> 32), (unsigned)len,
                   (unsigned)(nc >> 32), (unsigned)nc);
            memcpy((void *)(uintptr_t)p, rf_data(n) + off, nc);
        }
    }
    return (long)p;
}

static long lx_sc_munmap(long addr, long length) {
    uint64_t s = (uint64_t)(uintptr_t)addr & ~4095ULL;
    uint64_t e = s + (((uint64_t)length + 4095) & ~4095ULL);
    lx_mtrack_del(s, e);            /* 释放跟踪区间, 供 NOREPLACE 复用 */
    return 0;
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
        } else if (lx_fds[fd].synth) {
            left = lx_synth_size(lx_fds[fd].synth) - lx_fds[fd].off;
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
        } else if (lx_fds[fd].synth) {
            memcpy((void *)(uintptr_t)v[i].base,
                   lx_synth_data(lx_fds[fd].synth) + lx_fds[fd].off,
                   (size_t)want);
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

/* ============ 可写虚拟 FS（wine 配置前缀）============
 * 只读层 (ISO/rootfs) 之上叠加的内存目录树: mkdir/symlink/chdir/stat/getdents
 * 均可命中。wine 的 setup_config_dir 需要:
 *   chdir(/root/.wine) -> ENOENT -> stat(/root) -> mkdir(/root/.wine)
 *   -> chdir -> stat(".") -> mkdir(dosdevices) mkdir(drive_c)
 *   -> symlink(../drive_c, dosdevices/c:)  symlink(/, dosdevices/z:)
 * 目录/符号链接"存在"; 文件写入 (system.reg 等) 留待 wine server 阶段。 */

static int lx_iso_path(const char *p, char *abspath, size_t cap);
static int lx_wine_path_exists(const char *p);

#define LX_VN_MAX 64
typedef struct {
    char path[160];         /* 规范化绝对路径 */
    int  type;              /* 0=dir 1=symlink */
    char target[160];       /* symlink 目标 (原样) */
} lx_vnode_t;

static lx_vnode_t lx_vn[LX_VN_MAX];
static int lx_vn_count = 0;
static char lx_cwd[256] = "/";      /* 进程 cwd (vnode 层) */

static void lx_vn_seed(void) {
    if (lx_vn_count) return;
    strcpy(lx_vn[0].path, "/root");              lx_vn[0].type = 0;
    strcpy(lx_vn[1].path, "/tmp");               lx_vn[1].type = 0;
    strcpy(lx_vn[2].path, "/tmp/.wine-0");       lx_vn[2].type = 0;
    strcpy(lx_vn[3].path, "/tmp/.wine-0/server-7777-f0000"); lx_vn[3].type = 0;
    strcpy(lx_vn[4].path, "/tmp/.wine-0/server-7777-f0000/socket"); lx_vn[4].type = 3;  /* socket */
    lx_vn_count = 5;
}

static int lx_vn_find(const char *path) {
    for (int i = 0; i < lx_vn_count; i++)
        if (strcmp(lx_vn[i].path, path) == 0) return i;
    return -1;
}

/* 会话级重置: 每次 `vm linux` / wine 会话开始前调用 (lx_proc.c)。
 * 上一会话若未 close/munmap, fd/socket/mmap 槽会跨会话残留:
 * 64 个 fd 槽最终耗尽 (EMFILE), mmap 游标单调增长直至 2GB 池耗尽。 */
void lx_session_reset(void) {
    memset(lx_fds, 0, sizeof(lx_fds));
    memset(lx_socks, 0, sizeof(lx_socks));
    lx_mtrack_n = 0;
    lx_mmap_cur = LX_MMAP_BASE;
    lx_vn_count = 0;
    lx_vn_seed();
    strcpy(lx_cwd, "/");
}

/* 规范化绝对路径: 相对路径拼 cwd, 解析 "." / ".." */
static void lx_abspath(const char *in, char *out, size_t cap) {
    char tmp[300];
    size_t n = 0;
    if (in[0] == '/') {
        tmp[0] = '\0';
    } else {
        size_t l = strlen(lx_cwd);
        if (l >= sizeof(tmp) - 2) l = sizeof(tmp) - 2;
        memcpy(tmp, lx_cwd, l);
        tmp[l] = '/';
        tmp[l + 1] = '\0';
        n = l + 1;
    }
    size_t il = strlen(in);
    if (n + il >= sizeof(tmp)) il = sizeof(tmp) - 1 - n;
    memcpy(tmp + n, in, il);
    tmp[n + il] = '\0';

    size_t comp[32];
    size_t complen[32];
    int cnt = 0;
    const char *p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - s);
        if (len == 1 && s[0] == '.') continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (cnt > 0) cnt--;
            continue;
        }
        if (cnt < 32) { comp[cnt] = (size_t)(s - tmp); complen[cnt] = len; cnt++; }
    }
    size_t o = 0;
    if (cnt == 0) {
        out[0] = '/';
        out[1] = '\0';
    } else {
        for (int i = 0; i < cnt && o + 2 < cap; i++) {
            out[o++] = '/';
            const char *s = tmp + comp[i];
            size_t l = complen[i];
            if (l > cap - o - 1) l = cap - o - 1;
            memcpy(out + o, s, l);
            o += l;
        }
        out[o] = '\0';
    }
}

/* 分层路径解析: 1=rootfs 2=虚拟节点 3=ISO 4=wine 树目录 5=合成文件 */
typedef struct {
    int kind;
    const rf_node *node;    /* kind 1 */
    int vn;                 /* kind 2 (-1 无) */
    iso_file_t iso;         /* kind 3 */
    int via_wine_iso;       /* kind 3 且来自 wine_iso 映射 */
    int synth;              /* kind 5 */
} lx_look;

static void lx_look_clear(lx_look *r) {
    memset(r, 0, sizeof(*r));
    r->vn = -1;
}

/* 单层解析 (不跟随虚拟符号链接); path 必须是绝对路径 */
static int lx_lookup1(const char *abspath, lx_look *r) {
    lx_vn_seed();
    lx_look_clear(r);
    const rf_node *n = rf_lookup(abspath);
    if (n) { r->kind = 1; r->node = n; return 1; }
    int vi = lx_vn_find(abspath);
    if (vi >= 0) { r->kind = 2; r->vn = vi; return 1; }
    char ab[160];
    if (lx_iso_path(abspath, ab, sizeof(ab))) {
        if (iso_lookup(ab, &r->iso) == 0) { r->kind = 3; return 1; }
    }
    char ip[160];
    if (lx_wine_iso_path(abspath, ip, sizeof(ip))) {
        if (iso_lookup(ip, &r->iso) == 0) { r->kind = 3; r->via_wine_iso = 1; return 1; }
    }
    if (lx_wine_path_exists(abspath)) { r->kind = 4; return 1; }
    if (strcmp(abspath, "/etc/passwd") == 0) { r->kind = 5; r->synth = 1; return 1; }
    if (strcmp(abspath, "/etc/group") == 0)  { r->kind = 5; r->synth = 2; return 1; }
    return 0;
}

/* 完整解析: 跟随虚拟符号链接 (≤8 跳)。nofollow=1 只解析不跟随 (lstat 语义) */
static int lx_lookup(const char *path, lx_look *r, int nofollow) {
    char abs[256];
    lx_abspath(path, abs, sizeof(abs));
    int hops = 0;
    for (;;) {
        if (lx_lookup1(abs, r)) {
            if (nofollow || r->kind != 2 || lx_vn[r->vn].type != 1) return 1;
            const char *tgt = lx_vn[r->vn].target;
            char joined[300];
            if (tgt[0] == '/') {
                size_t l = strlen(tgt);
                if (l >= sizeof(joined)) l = sizeof(joined) - 1;
                memcpy(joined, tgt, l);
                joined[l] = '\0';
            } else {
                char base[256];
                strcpy(base, abs);
                char *sl = strrchr(base, '/');
                if (sl && sl != base) *sl = '\0'; else strcpy(base, "/");
                size_t bl = strlen(base);
                size_t tl = strlen(tgt);
                if (bl + 1 + tl >= sizeof(joined)) tl = sizeof(joined) - 2 - bl;
                memcpy(joined, base, bl);
                joined[bl] = '/';
                memcpy(joined + bl + 1, tgt, tl);
                joined[bl + 1 + tl] = '\0';
            }
            lx_abspath(joined, abs, sizeof(abs));
            if (++hops > 8) return 0;   /* ELOOP */
            continue;
        }
        return 0;
    }
}

/* 虚拟节点 → stat 填充 */
#define LX_S_IFSOCK 0xC000

static void lx_stat_fill_vn(lx_stat *s, int vn) {
    memset(s, 0, sizeof(*s));
    s->st_dev = 0x7777;
    s->st_ino = 0xF0000 + (uint32_t)vn;
    s->st_nlink = 1;
    s->st_blksize = 512;
    if (lx_vn[vn].type == 0) {
        s->st_mode = LX_S_IFDIR | 0700;
        s->st_size = 4096;
    } else if (lx_vn[vn].type == 1) {
        s->st_mode = LX_S_IFLNK | 0777;
        s->st_size = (int64_t)strlen(lx_vn[vn].target);
    } else if (lx_vn[vn].type == 3) {
        s->st_mode = LX_S_IFSOCK | 0777;
        s->st_size = 0;
    }
    s->st_blocks = 1;
}

/* 统一路径解析的 stat: nofollow=1 时符号链接本身 (lstat) */
static void lx_stat_fill(lx_stat *s, const rf_node *n);
static void lx_stat_fill_iso(lx_stat *s, const iso_file_t *iso);
static long lx_stat_path(const char *p, lx_stat *s, int nofollow) {
    lx_look r;
    if (!lx_lookup(p, &r, nofollow)) { LXT("[LX] stat_path: lookup failed for '%s'\n", p); return LX_ENOENT; }
    LXT("[LX] stat_path: kind=%d for '%s'\n", r.kind, p);
    switch (r.kind) {
    case 1:  lx_stat_fill(s, r.node); return 0;
    case 2:  lx_stat_fill_vn(s, r.vn); return 0;
    case 3:  lx_stat_fill_iso(s, &r.iso); return 0;
    case 4:
        memset(s, 0, sizeof(*s));
        s->st_mode = LX_S_IFDIR | 0755;
        s->st_nlink = 1;
        return 0;
    case 5:
        memset(s, 0, sizeof(*s));
        s->st_ino = 0xEAC00 + (uint32_t)r.synth;
        s->st_nlink = 1;
        s->st_blksize = 512;
        s->st_mode = LX_S_IFREG | 0444;
        s->st_size = (uint64_t)lx_synth_size(r.synth);
        s->st_blocks = 1;
        return 0;
    }
    return LX_ENOENT;
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

/* wine 运行时文件: 真实路径 -> tiny ISO 路径 (wine_iso_map 精确匹配) */
int lx_wine_iso_path(const char *real, char *out, int cap) {
    const struct wine_iso_ent *e;
    for (e = wine_iso_map; e->real; e++) {
        if (strcmp(e->real, real) == 0 && (int)strlen(e->iso) < cap) {
            strcpy(out, e->iso);
            return 1;
        }
    }
    return 0;
}

/* wine ISO 映射树中的路径存在性: 1=存在(文件或目录前缀), 0=不存在。
 * glibc realpath 会 readlink/lstat 每个路径组件, 目录组件必须"存在"
 * (返回 EINVAL) 否则 realpath 失败 (读 /proc/self/exe 后无法继续)。 */
static int lx_wine_path_exists(const char *p) {
    const struct wine_iso_ent *e;
    size_t pl = strlen(p);
    if (pl == 0) return 0;
    if (p[pl - 1] == '/') pl--;
    if (pl == 0) return 1;              /* "/" 永远存在 */
    for (e = wine_iso_map; e->real; e++) {
        if (strcmp(e->real, p) == 0) return 1;
        if (strncmp(e->real, p, pl) == 0 && e->real[pl] == '/') return 1;
    }
    return 0;
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
    LXT("[LX] open path='%s' flags=%08x\n", p, (unsigned)flags);
    lx_look r;
    if (!lx_lookup(p, &r, 0)) return LX_ENOENT;
    long fd = lx_fd_alloc();
    if (fd < 0) return fd;
    lx_fds[fd].in_use = 1;
    lx_fds[fd].off = 0;
    lx_fds[fd].iso_fd = 0;
    lx_fds[fd].synth = 0;
    lx_fds[fd].vn = -1;
    lx_fds[fd].node = NULL;
    switch (r.kind) {
    case 1:
        lx_fds[fd].node = r.node;
        return fd;
    case 2:                     /* 虚拟节点 (链接已跟随; 仅目录可开) */
        if (lx_vn[r.vn].type != 0) return LX_ENOENT;
        lx_fds[fd].vn = r.vn;
        return fd;
    case 3:                     /* ISO 文件 */
        if (r.via_wine_iso && (r.iso.flags & 2)) return LX_EISDIR;
        lx_fds[fd].iso = r.iso;
        lx_fds[fd].iso_fd = 1;
        return fd;
    case 5:
        lx_fds[fd].synth = r.synth;
        return fd;
    default:
        return LX_ENOENT;       /* wine 树目录: 无 fd 语义 */
    }
}

static long lx_sc_lseek(long fd, long off, long whence) {
    if (fd < 3 || fd >= LX_FD_MAX || !lx_fds[fd].in_use) return LX_EBADF;
    const rf_node *n = lx_fds[fd].node;
    int64_t size;
    if (lx_fds[fd].iso_fd) {
        if (lx_fds[fd].iso.flags & 2) return LX_EISDIR;
        size = lx_fds[fd].iso.size;
    } else if (lx_fds[fd].synth) {
        size = lx_synth_size(lx_fds[fd].synth);
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
    if (lx_fds[fd].synth) {         /* 虚拟 /etc/passwd 等 */
        memset(s, 0, sizeof(*s));
        s->st_ino = 0xEAC00 + (uint32_t)lx_fds[fd].synth;
        s->st_nlink = 1;
        s->st_blksize = 512;
        s->st_mode = LX_S_IFREG | 0444;
        s->st_size = (uint64_t)lx_synth_size(lx_fds[fd].synth);
        s->st_blocks = (s->st_size + 511) / 512;
        return 0;
    }
    if (lx_fds[fd].vn >= 0) {       /* 虚拟 FS 目录 */
        lx_stat_fill_vn(s, lx_fds[fd].vn);
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
    return lx_stat_path(p, s, 0);
}

static long lx_sc_lstat(long path, long buf) {
    const char *p = (const char *)(uintptr_t)path;
    lx_stat *s = (lx_stat *)(uintptr_t)buf;
    if (!p || !s) return LX_EINVAL;
    return lx_stat_path(p, s, 1);
}

/* 光驱目录项缓冲(单实例: 同一时刻只遍历一个目录) */
static uint8_t iso_dir_snap[64 * 1024];

static long lx_sc_access(long path, long amode) {
    (void)amode;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    lx_look r;
    if (lx_lookup(p, &r, 0)) return 0;
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
    if (lx_fds[fd].vn >= 0) {
        /* 虚拟 FS 目录: 子节点 = 路径前缀匹配的 vnode */
        const char *dir = lx_vn[lx_fds[fd].vn].path;
        size_t dl = strlen(dir);
        uint8_t *out = (uint8_t *)(uintptr_t)buf;
        long total = 0;
        uint32_t idx = lx_fds[fd].off;
        uint32_t skip = idx;
        for (int i = 0; i < lx_vn_count; i++) {
            const char *cp = lx_vn[i].path;
            if (strncmp(cp, dir, dl) != 0 || cp[dl] != '/') continue;
            const char *rest = cp + dl + 1;
            if (strchr(rest, '/')) continue;    /* 非直接子项 */
            if (skip > 0) { skip--; continue; }
            uint32_t nl = (uint32_t)strlen(rest);
            unsigned short reclen = (unsigned short)(sizeof(lx_dirent64) + nl + 1);
            if (total + reclen > len) break;
            lx_dirent64 *d = (lx_dirent64 *)(out + total);
            d->d_ino = 0xF0000 + (uint32_t)i;
            d->d_off = (int64_t)idx;
            d->d_reclen = reclen;
            d->d_type = (lx_vn[i].type == 0) ? 4 : 10;
            memcpy(d->d_name, rest, nl);
            d->d_name[nl] = '\0';
            total += reclen;
            idx++;
        }
        lx_fds[fd].off = idx;
        return total;
    }
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
    LXT("[LX] readlink path='%s'\n", (const char *)(uintptr_t)path);
    /* /proc 幻影 (wine loader 的 realpath("/proc/self/exe") 组件遍历):
     *   readlink("/proc")        -> EINVAL (目录存在, 非符号链接)
     *   readlink("/proc/self")   -> "1"    (对称链接 -> 本进程 pid)
     *   readlink("/proc/1")      -> EINVAL (目录)
     *   readlink("/proc/self/exe") 或 "/proc/1/exe" -> wine loader 路径 */
    if (strncmp(p, "/proc", 5) == 0) {
        if (strcmp(p, "/proc/self") == 0) {
            if (bufsz < 1) return LX_EINVAL;
            ((char *)(uintptr_t)buf)[0] = '1';
            return 1;
        }
        if (strcmp(p, "/proc/self/exe") == 0 ||
            strcmp(p, "/proc/1/exe") == 0) {
            const char t[] = LX_WINE_EXE;
            long sz = (long)sizeof(t) - 1;
            if (sz > bufsz) sz = bufsz;
            memcpy((void *)(uintptr_t)buf, t, (size_t)sz);
            return sz;
        }
        return LX_EINVAL;   /* /proc 下其余项按"存在且非链接"处理 */
    }
    lx_look r;
    if (lx_lookup(p, &r, 1) && r.kind == 2 && lx_vn[r.vn].type == 1) {
        const char *t = lx_vn[r.vn].target;
        long sz = (long)strlen(t);
        if (sz > bufsz) sz = bufsz;
        memcpy((void *)(uintptr_t)buf, t, (size_t)sz);
        return sz;
    }
    const rf_node *n = lx_path_resolve(p);
    if (n) {
        if (n->type != RF_TYPE_SYMLNK) return LX_EINVAL;
        long sz = n->size;
        if (sz > bufsz) sz = bufsz;
        memcpy((void *)(uintptr_t)buf, rf_data(n), (size_t)sz);
        return sz;
    }
    char abspath[160];
    if (lx_iso_path(p, abspath, sizeof(abspath))) {
        iso_file_t iso;
        if (iso_lookup(abspath, &iso) != 0)
            return LX_ENOENT;
        return LX_EINVAL;   /* 存在但不是符号链接 */
    }
    if (lx_wine_path_exists(p))
        return LX_EINVAL;   /* wine 树内路径存在 (文件或目录前缀) */
    return LX_ENOENT;
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
    size_t l = strlen(lx_cwd);
    if (l + 1 > (size_t)size) return LX_ERANGE;
    char *p = (char *)(uintptr_t)buf;
    memcpy(p, lx_cwd, l + 1);
    return (long)l;                 /* Linux: 不含 NUL 的长度 */
}

static long lx_sc_chdir(long path) {
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    
    /* Fix corrupted wineserver path: if path contains ELF magic (0x7f 'E' 'L' 'F'), use correct server_dir */
    const char *s = p;
    while (*s) {
        if (s[0] == (char)0x7f && s[1] == 'E' && s[2] == 'L' && s[3] == 'F') {
            p = "/tmp/.wine-0/server-7777-f0000";
            break;
        }
        s++;
    }
    
    lx_look r;
    if (!lx_lookup(p, &r, 0)) return LX_ENOENT;
    int isdir = 0;
    switch (r.kind) {
    case 1: isdir = (r.node->type == RF_TYPE_DIR); break;
    case 2: isdir = (lx_vn[r.vn].type == 0); break;
    case 3: isdir = (r.iso.flags & 2); break;
    case 4: isdir = 1; break;
    }
    if (!isdir) return LX_ENOTDIR;
    lx_abspath(p, lx_cwd, sizeof(lx_cwd));
    return 0;
}

/* 创建虚拟节点的公共部分: 父目录必须已存在且为目录 */
static int lx_vn_create(const char *abspath, int type, const char *target) {
    lx_look r;
    if (lx_lookup1(abspath, &r)) return LX_EEXIST;
    char parent[256];
    strcpy(parent, abspath);
    char *sl = strrchr(parent, '/');
    if (!sl) return LX_ENOENT;
    if (sl == parent) strcpy(parent, "/"); else *sl = '\0';
    lx_look pr;
    if (!lx_lookup1(parent, &pr)) return LX_ENOENT;
    int pdir = 0;
    if (pr.kind == 1) pdir = (pr.node->type == RF_TYPE_DIR);
    else if (pr.kind == 2) pdir = (lx_vn[pr.vn].type == 0);
    else if (pr.kind == 3) pdir = (pr.iso.flags & 2);
    else if (pr.kind == 4) pdir = 1;
    if (!pdir) return LX_ENOTDIR;
    if (lx_vn_count >= LX_VN_MAX) return LX_EMFILE;
    strcpy(lx_vn[lx_vn_count].path, abspath);
    lx_vn[lx_vn_count].type = type;
    lx_vn[lx_vn_count].target[0] = '\0';
    if (type == 1 && target) {
        size_t tl = strlen(target);
        if (tl >= sizeof(lx_vn[lx_vn_count].target)) tl = sizeof(lx_vn[lx_vn_count].target) - 1;
        memcpy(lx_vn[lx_vn_count].target, target, tl);
        lx_vn[lx_vn_count].target[tl] = '\0';
    }
    lx_vn_count++;
    return 0;
}

static long lx_sc_mkdirat(long dirfd, long path, long mode) {
    (void)mode;
    if ((int)dirfd != -100) return LX_EINVAL;
    const char *p = (const char *)(uintptr_t)path;
    if (!p) return LX_EINVAL;
    lx_vn_seed();
    char abs[256];
    lx_abspath(p, abs, sizeof(abs));
    return lx_vn_create(abs, 0, NULL);
}

static long lx_sc_execve(long path, long argv, long envp) {
    const char *p = (const char *)(uintptr_t)path;
    LXT("[LX] execve path='%s' argv=0x%lx\n", p, (unsigned long)argv);
    if (p && (strstr(p, "wineserver") != NULL)) {
        LXT("[LX] wineserver exec intercepted - simulating wineserver child setup\n");
        lx_wine_server_init();
        return 0;
    }
    return LX_ENOSYS;
}

static long lx_sc_unlink(long path) {
    const char *p = (const char *)(uintptr_t)path;
    LXT("[LX] unlink path='%s'\n", p);
    if (!p) return LX_EINVAL;
    if (strstr(p, "wineserver") != NULL) {
        LXT("[LX] wineserver binary unlink intercepted - allowing\n");
        return 0;
    }
    return LX_ENOSYS;
}

static long lx_sc_symlinkat(long target, long dirfd, long path) {
    if ((int)dirfd != -100) return LX_EINVAL;    /* 仅 AT_FDCWD */
    const char *t = (const char *)(uintptr_t)target;
    const char *p = (const char *)(uintptr_t)path;
    if (!t || !p) return LX_EINVAL;
    lx_vn_seed();
    char abs[256];
    lx_abspath(p, abs, sizeof(abs));
    return lx_vn_create(abs, 1, t);
}

/* newfstatat: AT_FDCWD + 相对路径; AT_SYMLINK_NOFOLLOW(0x100) -> lstat */
static long lx_sc_newfstatat(long dirfd, long path, long buf, long flags) {
    if ((int)dirfd != -100) return LX_EINVAL;
    const char *p = (const char *)(uintptr_t)path;
    lx_stat *s = (lx_stat *)(uintptr_t)buf;
    if (!p || !s) return LX_EINVAL;
    LXT("[LX] newfstatat path='%s' flags=%x\n", p, (unsigned)flags);
    return lx_stat_path(p, s, (flags & 0x100) ? 1 : 0);
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
    if ((int)dirfd != -100) return LX_EINVAL;    /* 仅 AT_FDCWD */
    return lx_sc_open(path, flags, mode);
}

/* readlinkat: AT_FDCWD + 绝对路径 -> 走 readlink; 其余 EINVAL */
static long lx_sc_readlinkat(long dirfd, long path, long buf, long bufsz) {
    if ((int)dirfd != -100) return LX_EINVAL;   /* 无相对路径目录支持 */
    return lx_sc_readlink(path, buf, bufsz);
}

static long lx_sc_fork(void) {
    LXT("[LX] fork called - simulating wineserver child creation\n");
    lx_wine_server_init();
    return 2;  /* fake child PID */
}

static long lx_sc_clone(long flags, long stack, long ptid, long tls, long ctid) {
    LXT("[LX] clone flags=%x stack=%x\n", (unsigned)flags, (unsigned)stack);
    if (flags & 0x100) {  /* CLONE_VM - likely wineserver posix_spawn */
        lx_wine_server_init();
        return 2;  /* fake child PID */
    }
    return LX_ENOSYS;
}

static long lx_sc_wait4(long pid, long status, long options, long rusage) {
    LXT("[LX] wait4 pid=%d options=%x\n", (int)pid, (unsigned)options);
    if (pid == 2 || pid == -1) {
        if (status) {
            int *status_ptr = (int *)(uintptr_t)status;
            *status_ptr = 0;
        }
        return 2;
    }
    return LX_ECHILD;
}

/* ---- 分发器：Linux x86-64 调用号 -> Kresten 服务 ---- */

static long lx_dispatch0(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)num; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
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
    case 9:   return lx_sc_mmap(a1, a2, a3, a4, a5, a6);/* mmap */
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
    case 41:  return lx_sc_socket(a1, a2, a3);        /* socket */
    case 42:  return lx_sc_connect(a1, a2, a3);       /* connect */
    case 44:  return lx_sc_send(a1, a2, a3, a4);      /* send */
    case 45:  return lx_sc_recv(a1, a2, a3, a4);      /* recv */
    case 18:  return lx_sc_recvmsg(a1, a2, a3, a4, a5); /* recvmsg */
    case 46:  return lx_sc_sendmsg(a1, a2, a3, a4, a5); /* sendmsg */
    case 49:  return lx_sc_bind(a1, a2, a3);          /* bind */
    case 50:  return lx_sc_listen(a1, a2);            /* listen */
    case 56:  return lx_sc_clone(a1, a2, a3, a4, a5);                     /* clone */
    case 57:  return lx_sc_fork();                       /* fork */
    case 58:  return LX_ENOSYS;                         /* vfork */
    case 59:  return lx_sc_execve(a1, a2, a3);        /* execve */
    case 60:  lx_sc_exit_group(); return 0;           /* exit */
    case 61:  return lx_sc_wait4(a1, a2, a3, a4);       /* wait4 */
    case 62:  return 0;                                 /* kill (no-op) */
    case 63:  return lx_sc_uname(a1);                   /* uname */
    case 65:  return 0;                                 /* setpgid */
    case 66:  return lx_sc_socketpair(a1, a2, a3, a4); /* socketpair */
    case 72:  return lx_sc_fcntl(a1, a2, a3);           /* fcntl */
    case 79:  return lx_sc_getcwd(a1, a2);              /* getcwd */
    case 80:  return lx_sc_chdir(a1);                   /* chdir */
    case 83:  return lx_sc_mkdirat(-100, a1, a2);       /* mkdir (glibc->mkdirat) */
    case 87:  return lx_sc_unlink(a1);                     /* unlink */
    case 88:  return lx_sc_symlinkat(a1, -100, a2);     /* symlink (glibc->symlinkat) */
    case 89:  return lx_sc_readlink(a1, a2, a3);        /* readlink */
    case 267: return lx_sc_readlinkat(a1, a2, a3, a4); /* readlinkat (glibc realpath) */
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
    case 258: return lx_sc_mkdirat(a1, a2, a3);         /* mkdirat */
    case 262: return lx_sc_newfstatat(a1, a2, a3, a4);  /* newfstatat */
    case 266: return lx_sc_symlinkat(a1, a2, a3);       /* symlinkat */
case 273: return 0;                                 /* set_robust_list */
    case 288: return lx_sc_accept4(a1, a2, a3, a4);   /* accept4 */
    case 302: return lx_sc_prlimit64(a1, a2, a3, a4); /* prlimit64 */
    case 318: return lx_sc_getrandom(a1, a2, a3);       /* getrandom */
    case 334: return lx_sc_rseq(a1, a2, a3, a4);        /* rseq */
    case 17:  return lx_sc_pread64(a1, a2, a3, a4);     /* pread64 (wine) */
    default:
        return LX_ENOSYS;        /* unknown: ENOSYS, silent (v0) */
    }
}

/* 调试追踪: 打印每次 syscall (调用者 RIP/用户 RSP + 参数 + 返回值)
 * 默认关闭 (lx_dbg_trace=0); 注意: 内核 printf 不支持 %ld/%llx, 全部按 32 位段打印 */
long linux_syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    if (lx_dbg_trace && num == 102) {   /* getuid: libc 已加载, 探测 __environ 是否被 ld.so 写入 */
        uint64_t *env_var = (uint64_t *)0x10219de8;   /* libc __environ (base 0x1000000 + 0x219de8) */
        uint64_t ep = *env_var;
        printf("[DBG] getuid __environ=0x%08x%08x\n", (unsigned)(ep >> 32), (unsigned)ep);
        if (ep) {
            uint64_t *p = (uint64_t *)(uintptr_t)ep;
            for (int i = 0; i < 4; i++) {
                uint64_t s = p[i];
                if (!s) { printf("[DBG]   env[%d]=NULL\n", i); break; }
                printf("[DBG]   env[%d]='%s'\n", i, (char *)(uintptr_t)s);
            }
        } else {
            printf("[DBG]   __environ NULL\n");
        }
    }
    if (lx_dbg_trace && num == 59) {   /* execve: 打印路径/argv 帮助定位 wine 自重启 */
        const char *p = (const char *)(uintptr_t)a1;
        const char *v0 = (const char *)(uintptr_t)((uint64_t*)a2)[0];
        const char *v1 = (const char *)(uintptr_t)((uint64_t*)a2)[1];
        printf("[S] EXECVE path='%s' argv0='%s' argv1='%s'\n", p, v0, v1);
    }
    long r = lx_dispatch0(num, a1, a2, a3, a4, a5, a6);
    if (lx_dbg_trace) {
        printf("[S] n=%08x a1=%08x/%08x a2=%08x/%08x a3=%08x/%08x a4=%08x/%08x a5=%08x/%08x a6=%08x/%08x\n",
               (unsigned)(uint64_t)num & 0xFFFFFFFFu,
               (unsigned)((uint64_t)a1 >> 32), (unsigned)(uint64_t)a1 & 0xFFFFFFFFu,
               (unsigned)((uint64_t)a2 >> 32), (unsigned)(uint64_t)a2 & 0xFFFFFFFFu,
               (unsigned)((uint64_t)a3 >> 32), (unsigned)(uint64_t)a3 & 0xFFFFFFFFu,
               (unsigned)((uint64_t)a4 >> 32), (unsigned)(uint64_t)a4 & 0xFFFFFFFFu,
               (unsigned)((uint64_t)a5 >> 32), (unsigned)(uint64_t)a5 & 0xFFFFFFFFu,
               (unsigned)((uint64_t)a6 >> 32), (unsigned)(uint64_t)a6 & 0xFFFFFFFFu);
        printf("  r=%08x/%08x rsp=%08x/%08x rip=%08x/%08x\n",
               (unsigned)((uint64_t)r >> 32), (unsigned)(uint64_t)r & 0xFFFFFFFFu,
               (unsigned)(linux_user_rsp >> 32), (unsigned)linux_user_rsp & 0xFFFFFFFFu,
               (unsigned)(linux_ret_rip >> 32), (unsigned)linux_ret_rip & 0xFFFFFFFFu);
    }
    return r;
}

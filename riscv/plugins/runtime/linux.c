/* LX 子系统 guest (RISC-V): 注册到 vm 插件上的 "linux" guest。
 *   `vm linux [program [args...]]`   (program 默认 busybox sh)
 * RISC-V 无 rootfs/CD 导入, 只跑内置 busybox; syscall 通道 = boot/start.S 的
 * trap_entry (guest 以 U-mode 运行, ecall 自陷)。
 */

#include <plugin.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <keyboard.h>
#include "lx_int.h"
#include "lx_tests.h"
#include <vm_guestif.h>

static const LxTest *lx_find_test(const char *name) {
    for (int i = 0; lx_tests[i].name; i++)
        if (strcmp(lx_tests[i].name, name) == 0) return &lx_tests[i];
    return 0;
}

/* ---- vm guest callbacks ---- */

static int lx_self_registered = 0;

static int lx_guest_init(void);
static int lx_guest_run(const char *guest_args);

const vm_guest_t lx_guest = {
    "linux",
    "Linux subsystem - busybox sh (RISC-V RV64, ecall syscall emulation)",
    lx_guest_init,
    lx_guest_run
};

static int lx_guest_init(void) {
    if (lx_self_registered) return 0;   /* vm_guest_register 会回调 init, 防重入 */
    lx_self_registered = 1;
    /* LX 异常桥接: start.S 的 ecall/异常经内核 lx_bridge.c 指针转入本模块
     * (本模块提供 trap_unhandled_impl/linux_syscall_impl; 同模块引用, 无跨模块依赖) */
    extern void trap_unhandled_impl(long mcause, long mepc, long mstatus);
    extern long linux_syscall_impl(long num, long a1, long a2, long a3, long a4, long a5);
    extern void (*trap_unhandled)(long mcause, long mepc, long mstatus);
    extern long (*linux_syscall_dispatch)(long num, long a1, long a2, long a3, long a4, long a5);
    trap_unhandled = trap_unhandled_impl;
    linux_syscall_dispatch = linux_syscall_impl;
    puts("[LX] Linux subsystem guest registered (RISC-V, runs inside vm)\n");
    vm_guest_register(&lx_guest);
    return 0;
}

/* ---- 插件入口 (linux.kplg 是正式插件: 带 kplg_plugin 才有 init 入口) ----
 * 加载顺序(kplg_auto)排在 vm 之后, plugin_init_all 统一 init 时把自己
 * 注册到 vm 平台的 guest 表; 无 command, 一切入口走 `vm linux ...`。 */

Plugin kplg_plugin __attribute__((visibility("default"))) = {
    "linux", "Linux subsystem (busybox sh, runs inside vm)",
    lx_guest_init, 0
};

/* run the Linux subsystem as a vm guest. 默认 busybox sh;
 * `vm linux <program> [args...]` 可指定内置程序。
 */
static int lx_guest_run(const char *guest_args) {
    const char *args = guest_args ? guest_args : "";

    keyboard_flush();                /* drop keys typed before the session */

    char name[32];
    int n = 0;
    while (args[n] && args[n] != ' ' && n < 31) {
        name[n] = args[n];
        n++;
    }
    name[n] = '\0';
    const char *rest = args + n;
    while (*rest == ' ') rest++;

    const LxTest *t;
    if (!name[0] || strcmp(name, "sh") == 0) {
        t = lx_find_test("busybox");
        name[0] = 0;
        rest = n ? "" : "sh";       /* `vm linux` 与 `vm linux sh` -> busybox sh */
    } else if (strcmp(name, "list") == 0) {
        printf("[LX] installed Linux subsystem (RISC-V):\n");
        for (int i = 0; lx_tests[i].name; i++)
            printf("  %s (%u bytes)\n", lx_tests[i].name,
                   (unsigned)lx_tests[i].size);
        keyboard_flush();
        return 0;
    } else {
        t = lx_find_test(name);
        if (!t) {
            printf("linux: unknown program '%s'\n", name);
            keyboard_flush();
            return 0;
        }
    }

    /* 解析剩余参数（空格分隔，最多 16 个） */
    static char *argv[17];
    int argc = 1;                       /* argv[0] = 程序名 */
    argv[0] = name;
    if (*rest) {
        char argbuf[512];
        strncpy(argbuf, rest, sizeof(argbuf) - 1);
        argbuf[sizeof(argbuf) - 1] = '\0';
        char *p = argbuf;
        while (*p && argc < 16) {
            while (*p == ' ') p++;
            if (!*p) break;
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) { *p = '\0'; p++; }
        }
    }
    argv[argc] = 0;
    lx_run_test(t, argc, argv);
    keyboard_flush();                /* keep leftovers out of the host shell */
    return 0;
}

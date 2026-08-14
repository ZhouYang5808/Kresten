/* LX 子系统 guest (ARM 版): 注册到 vm 插件上的 "linux" guest。
 *   `vm linux [program [args...]]`   (program 默认 busybox sh)
 * ARM 无 rootfs/CD 导入, 只跑内置 busybox; syscall 通道为 start.s 的
 * svc_handler (guest 以 USR 模式运行, svc 自陷)。 */

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

static int lx_guest_init(void) {
    puts("[LX] Linux subsystem guest registered (ARM, runs inside vm)\n");
    return 0;
}

/* run the Linux subsystem as a vm guest. 默认 busybox sh；
 * `vm linux <program> [args...]` 可指定内置程序。 */
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
        printf("[LX] installed Linux subsystem (ARM):\n");
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

const vm_guest_t lx_guest = {
    "linux",
    "Linux subsystem - busybox sh (ARM EABI, svc syscall emulation)",
    lx_guest_init,
    lx_guest_run
};
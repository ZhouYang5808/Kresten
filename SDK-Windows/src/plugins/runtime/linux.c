/* LX 子系统 guest（WSL2 对应物）：Linux 子系统不再是一个独立插件，
 * 而是注册到 vm 插件（虚拟化平台，Hyper-V 式）上的一个 guest：
 *   `vm linux [program [args...]]`   （program 默认 busybox sh）
 * init 时（vm 插件加载）装 syscall 挂载点，run 时经 vm 平台会话运行。 */

#include <plugin.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <keyboard.h>
#include "lx_int.h"
#include "lx_tests.h"
#include <vm_guestif.h>
#include <rootfs.h>
#include <ata.h>

static int lx_installed = 0;

static void lx_install_syscall(void) {
    if (lx_installed) return;
    /* 启用原生 SSE/MMX：CR4.OSFXSR|OSXMMEXCPT，并清 CR0.EM。
     * glibc/musl 启动即执行 movd/movdqa，未开则 #UD。 */
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9) | (1ULL << 10);
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080ULL));
    uint64_t efer = ((uint64_t)hi << 32) | lo;
    if (!(efer & 1)) {              /* EFER.SCE: 允许 syscall 指令 */
        efer |= 1;
        __asm__ volatile ("wrmsr"
                          : : "c"(0xC0000080ULL),
                              "a"((uint32_t)efer),
                              "d"((uint32_t)(efer >> 32)));
    }
    uint64_t lst = (uint64_t)(uintptr_t)&linux_syscall_entry;
    __asm__ volatile ("wrmsr"
                      : : "c"(0xC0000082ULL),
                          "a"((uint32_t)lst),
                          "d"((uint32_t)(lst >> 32)));
    __asm__ volatile ("wrmsr"
                      : : "c"(0xC0000081ULL),
                          "a"(0x00000000U),
                          "d"(0x002B0008U));
    uint32_t rlo, rhi;
    __asm__ volatile ("rdmsr" : "=a"(rlo), "=d"(rhi) : "c"(0xC0000082ULL));
    lx_installed = 1;
    printf("[LX] syscall entry installed (IA32_LSTAR = %p, readback=%08x%08x)\n",
           (void *)(uintptr_t)lst, rhi, rlo);
}

static const LxTest *lx_find_test(const char *name) {
    for (int i = 0; lx_tests[i].name; i++)
        if (strcmp(lx_tests[i].name, name) == 0) return &lx_tests[i];
    return 0;
}

/* ---- rootfs: 从 CD 导入发行版 ---- */

static int lx_import_rootfs(void) {
    if (rootfs_ready()) return 1;
    puts("[LX] importing Linux distribution from CD ...");
    if (rootfs_import()) {
        printf("[LX] distribution installed: %s (%u nodes)\n",
               rootfs_distro_name(), rf_node_count());
        return 1;
    }
    puts("[LX] import failed: no distribution ISO found");
    return 0;
}

/* 列出发行版 /bin /usr/bin 下的命令行程序 */
static void lx_list_commands(void) {
    if (!rootfs_ready()) return;
    printf("  distribution: %s\n", rootfs_distro_name());
    const char *dirs[] = { "/bin", "/usr/bin", 0 };
    for (int di = 0; dirs[di]; di++) {
        const rf_node *dir = rf_lookup(dirs[di]);
        if (!dir) continue;
        printf("  %s:", dirs[di]);
        int col = 0;
        for (const rf_node *n = rf_child(dir); n; n = rf_next(n)) {
            if (n->type != RF_TYPE_FILE) continue;
            if (col == 4) { printf("\n      "); col = 0; }
            printf(" %s", rf_name(n));
            col++;
        }
        printf("\n");
    }
}

/* ---- vm guest callbacks ---- */

static int lx_self_registered = 0;

static int lx_guest_init(void);
static int lx_guest_run(const char *guest_args);

const vm_guest_t lx_guest = {
    "linux",
    "Linux subsystem - distro import (ISO rootfs) + busybox sh (ELF + syscall emulation)",
    lx_guest_init,
    lx_guest_run
};

static int lx_guest_init(void) {
    if (lx_self_registered) return 0;   /* vm_guest_register 会回调 init, 防重入 */
    lx_self_registered = 1;
    puts("[LX] Linux subsystem guest registered (WSL2-style, runs inside vm)\n");
    lx_install_syscall();
    vm_guest_register(&lx_guest);
    return 0;
}

/* ---- 插件入口 (linux.kplg 是正式插件: 带 kplg_plugin 才有 init 入口) ----
 * 加载顺序(kplg_auto)排在 vm 之后, plugin_init_all 统一 init 时把自己
 * 注册到 vm 平台的 guest 表; 无 command, 一切入口走 `vm linux ...`。 */

Plugin kplg_plugin __attribute__((visibility("default"))) = {
    "linux", "Linux subsystem (distro import + busybox sh)",
    lx_guest_init, 0
};

/* run the Linux subsystem as a vm guest. 默认 busybox sh；
 * `vm linux <program> [args...]` 可指定内置程序（busybox / hello）；
 * `vm linux wine <prog> [args]` 用 wine 运行时（tiny ISO）运行 Windows
 * 程序: ld.so 动态链接 + wine loader, argv[1] 为 PE 程序路径。 */
static int lx_guest_run(const char *guest_args) {
    const char *args = guest_args ? guest_args : "";

    /* 首次运行: 从 CD 导入发行版(若 BIOS 可枚举到 ATAPI CD) */
    lx_import_rootfs();

    lx_install_syscall();
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

    if (strcmp(name, "wine") == 0) {
        /* Windows 程序由 wine 运行时运行 (tiny ISO 载入, wine_iso_map 映射);
         * argv[1] 为 PE 程序路径 (如 /winebin/hello.exe) */
        static char *wargv[17];
        int wargc = 1;
        wargv[0] = "wine";
        if (*rest) {
            char argbuf[512];
            strncpy(argbuf, rest, sizeof(argbuf) - 1);
            argbuf[sizeof(argbuf) - 1] = '\0';
            char *p = argbuf;
            while (*p && wargc < 16) {
                while (*p == ' ') p++;
                if (!*p) break;
                wargv[wargc++] = p;
                while (*p && *p != ' ') p++;
                if (*p) { *p = '\0'; p++; }
            }
        }
        wargv[wargc] = 0;
        lx_run_wine(wargc, wargv);
        keyboard_flush();            /* keep leftovers out of the host shell */
        return 0;
    }

    const LxTest *t;
    if (!name[0] || strcmp(name, "sh") == 0) {
        t = lx_find_test("busybox");
        name[0] = 0;
        rest = n ? "" : "sh";       /* `vm linux` 与 `vm linux sh` -> busybox sh */
    } else if (strcmp(name, "list") == 0) {
        printf("[LX] installed Linux subsystem:\n");
        if (rootfs_ready()) {
            printf("  rootfs: %s (%u nodes)\n", rootfs_distro_name(), rf_node_count());
            lx_list_commands();
        } else {
            puts("  (no distribution installed - import from CD on first `vm linux`)");
        }
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
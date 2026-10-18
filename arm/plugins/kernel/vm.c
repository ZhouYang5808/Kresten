/* ===== plugins/vm.c (ARM 版): 虚拟化平台 (Hyper-V 式) =====
 * ARM 无 VT-x/VMX 硬件支持, 所以 "kresten" guest 固定走软件仿真模式
 * (vm_soft.c)。
 * 同时注册 "linux" guest (linux.c, LX 子系统)。
 *
 * `vm`          运行默认 guest (kresten, 软件模式)
 * `vm linux`    运行 Linux 子系统 (busybox sh)
 * `vm list`     列出已注册 guest
 */

#include <plugin.h>
#include <stdio.h>
#include <string.h>
#include <keyboard.h>
#include <vm_guestif.h>

/* ---- 状态字符串 (vm_soft.c 用; 桌面/调试可查) ---- */
static char vm_status_str[64] = "ready";

void vm_set_status(const char *s) {
    if (!s) return;
    strncpy(vm_status_str, s, sizeof(vm_status_str) - 1);
    vm_status_str[sizeof(vm_status_str) - 1] = '\0';
}

const char *plugin_vm_status(void) {
    return vm_status_str;
}

int plugin_vm_available(void) {
    return 0;                       /* ARM: 无硬件虚拟化 */
}

int vm_run_soft(void);              /* vm_soft.c */

/* ---- vm guest interface (Hyper-V style platform) ---- */

static const vm_guest_t *vm_guests[VM_GUEST_MAX];
static int vm_guest_count;

int vm_guest_register(const vm_guest_t *g) {
    if (!g || !g->name || vm_guest_count >= VM_GUEST_MAX)
        return -1;
    for (int i = 0; i < vm_guest_count; i++)
        if (strcmp(vm_guests[i]->name, g->name) == 0)
            return 0;               /* already registered */
    vm_guests[vm_guest_count++] = g;
    printf("[VM] guest registered: %s - %s\n", g->name, g->desc);
    if (g->init)
        g->init();
    return 0;
}

const vm_guest_t *vm_guest_find(const char *name) {
    for (int i = 0; i < vm_guest_count; i++)
        if (strcasecmp(vm_guests[i]->name, name) == 0)
            return vm_guests[i];
    return 0;
}

void vm_guest_list(void) {
    puts("available guests:\n");
    for (int i = 0; i < vm_guest_count; i++)
        printf("  %-12s %s\n", vm_guests[i]->name, vm_guests[i]->desc);
    putchar('\n');
}

/* ---- default guest "kresten": 软件仿真模式 (ARM 无 VT-x) ---- */
static int vm_guest_kresten_run(const char *args) {
    (void)args;
    vm_run_soft();
    return 0;
}

static const vm_guest_t vm_guest_kresten = {
    "kresten",
    "Kresten VM guest OS (software emulation)",
    0,
    vm_guest_kresten_run
};

/* the Linux subsystem guest (linux.kplg) self-registers in its own init
 * (linux.kplg loads right after vm.kplg, vm_guest_register resolved via
 * already-loaded module dynsym) */

/* linux.kplg 提供的 LX 异常实现 (lx_syscall.c), 由 linux 插件自己的 init
 * 桥接到内核函数指针 (linux.kplg 排在 vm 之后加载, 单向依赖: linux -> vm) */

int plugin_vm_init(void) {
    puts("[VM] plugin loaded (ARM): software emulation mode (no VT-x on ARM)");
    vm_set_status("ready (software mode)");
    /* LX 桥接由 linux.kplg 自己的 init 完成 (自注册时), 此处只注册 kresten */
    vm_guest_register(&vm_guest_kresten);
    /* linux guest 由 linux.kplg 的 init 自注册 (排在 vm 之后加载) */
    return 0;
}

int plugin_vm_cmd(char *args) {
    if (!args || !*args) {              /* `vm` = default (kresten) guest */
        vm_guest_kresten_run(NULL);
        return 0;
    }
    char gname[32];
    int n = 0;
    while (args[n] && args[n] != ' ' && n < 31) {
        gname[n] = args[n];
        n++;
    }
    gname[n] = '\0';
    const char *rest = args + n;
    while (*rest == ' ')
        rest++;
    if (strcasecmp(gname, "list") == 0 || strcasecmp(gname, "help") == 0 ||
        strcasecmp(gname, "?") == 0) {
        vm_guest_list();
        return 0;
    }
    const vm_guest_t *g = vm_guest_find(gname);
    if (!g) {
        printf("vm: unknown guest '%s'\n", gname);
        vm_guest_list();
        return 0;
    }
    printf("vm: starting guest '%s'\n", g->name);
    g->run(*rest ? rest : NULL);
    keyboard_flush();               /* keep guest leftovers out of the shell */
    return 0;
}

REGISTER_PLUGIN(vm);
#include <plugin.h>
#include <driver.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <iso9660.h>
#include <ata.h>
#include "kplg.h"
#include "kplg_embed.h"

#define MAX_PLUGINS 16

static Plugin *plugins[MAX_PLUGINS];
static int plugin_count = 0;

/* 启动时自动加载顺序(依赖顺序: 平台 vm 先于 linux(linux init 引用
 * vm_guest_register); hello 在 shell 前(ARM/RV 引用 plugin_hello_cmd);
 * desktop 最后(引用 vm 状态查询与驱动模块符号)) */
static const char *kplg_auto[] = {
    "runtime", "vm", "linux", "shell", "desktop", "hello", "taskmgr", 0
};

static int plugin_register(Plugin *p) {
    if (!p || !p->name[0]) return -1;
    if (plugin_find(p->name)) return 0;          /* already loaded */
    if (plugin_count >= MAX_PLUGINS) return -1;
    plugins[plugin_count++] = p;
    printf("[PLUGIN] registered: %s\n", p->name);
    return 0;
}

Plugin *plugin_find(const char *name) {
    for (int i = 0; i < plugin_count; i++)
        if (strcasecmp(plugins[i]->name, name) == 0) return plugins[i];
    return 0;
}

int plugin_dispatch(const char *cmd, const char *args) {
    Plugin *p = plugin_find(cmd);
    if (!p || !p->command) return -1;
    p->command((char *)args);
    return 0;
}

void plugin_list_all(void) {
    puts("  PLUGIN              BASE              DESC\n");
    puts("  ------------------  ----------------  ------------------------------\n");
    for (int i = 0; i < plugin_count; i++) {
        const char *mn = "?";
        void *mb = 0;
        kplg_mod_list(i, &mn, &mb);
        printf("  %-16s  %p  %s\n", plugins[i]->name, mb, plugins[i]->desc);
    }
    putchar('\n');
}

/* ---- 从插件 ISO 装载 ---- */

static int kplg_drive = -1;

/* 扫描 ATAPI 槽位, 挂载含 /plugins 目录的镜像(含启动盘) */
static int kplg_mount_iso(void) {
    if (kplg_drive >= 0) return 1;
    for (int d = 0; d < 4; d++) {
        if (ata_dev_type(d) != ATA_TYPE_ATAPI) continue;
        if (iso_mount(d) != 0) continue;
        iso_file_t probe;
        if (iso_lookup("/plugins", &probe) == 0 && (probe.flags & 2)) {
            kplg_drive = d;
            return 1;
        }
    }
    return 0;
}

/* ISO9660 会把文件名压成 8.3("runtime.kplg" -> "RUNTIME.KPL")。
 * 无法直接按 .kplg 查找: 先按源码归属遍历 /plugins 的三个子目录
 * (kernel/runtime/user) 按基础名匹配, 再用 8.3 名 iso_lookup 拿到
 * extent 读取。 */
typedef struct {
    char want[32];
    char found[32];
} kplg_scan_t;

/* 插件在 ISO 上的目录布局(与源码树 plugins/{kernel,runtime,user} 对应):
 * kernel/  = 内核层插件 (runtime/shell/vm), runtime/  = 子系统 (linux),
 * user/    = 用户层 (desktop/hello)。 */
static const char *kplg_iso_dirs[] = {
    "/plugins/kernel", "/plugins/runtime", "/plugins/user", 0
};

/* 外围驱动在 ISO 上的目录: 驱动 /drivers/, 字体资源 /fonts/ */
static const char *kdrv_iso_dirs[] = {
    "/fonts", "/drivers", 0
};

/* 驱动加载顺序(依赖序): font 先于 gfx(gfx_save_font 引 gfx_font_bmp),
 * mouse 引 ps2packet, rootfs 引 inflate。全部先于插件加载
 * (desktop 引 gfx_get_font_aa 与 gfx/mouse 符号, shell 引 kbd/rtc, linux 引 rootfs)。 */
static const char *kdrv_auto[] = {
    "ps2packet", "inflate", "font", "kbd", "gfx", "rtc", "mouse", "rootfs", 0
};

static int kplg_scan_cb(const char *name, uint32_t size, uint8_t flags, void *arg) {
    (void)size;
    kplg_scan_t *s = (kplg_scan_t *)arg;
    if (flags & 2) return 0;                     /* 目录跳过 */
    int i = 0;
    while (name[i] && name[i] != '.' && i < 31) i++;
    int j = 0;
    for (; j < i; j++) {
        char a = s->want[j], b = name[j];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    if (s->want[j] != '\0') return 0;
    strncpy(s->found, name, 31);
    s->found[31] = '\0';
    return 1;
}

static int kplg_find_in_dirs(const char **dirs, const char *name,
                             uint8_t **buf, uint32_t *size) {
    kplg_scan_t s;
    strncpy(s.want, name, 31);
    s.want[31] = '\0';
    for (int d = 0; dirs[d]; d++) {
        s.found[0] = '\0';
        iso_read_dir(dirs[d], kplg_scan_cb, &s);
        if (!s.found[0]) continue;

        char path[64];
        int n = 0;
        const char *pfx = dirs[d];
        while (*pfx && n < 55) path[n++] = *pfx++;
        if (n < 62) path[n++] = '/';
        for (int i = 0; s.found[i] && n < 62; i++) path[n++] = s.found[i];
        path[n] = '\0';

        iso_file_t f;
        if (iso_lookup(path, &f) != 0 || (f.flags & 2)) return -1;
        uint8_t *b = (uint8_t *)kplg_alloc(f.size);
        if (!b) return -1;
        if (iso_read_file(&f, b) != 0) return -1;
        *buf = b;
        *size = f.size;
        return 0;
    }
    return -1;
}

static int kplg_find_on_iso(const char *name, uint8_t **buf, uint32_t *size) {
    return kplg_find_in_dirs(kplg_iso_dirs, name, buf, size);
}

/* 从 ISO /plugins/{kernel,runtime,user}/<name>.kplg 加载并注册(不 init) */
static int plugin_load_iso_only(const char *name) {
    if (plugin_find(name)) return 0;
    if (!kplg_mount_iso()) {
        puts("[PLUGIN] no plugin ISO found (need /plugins on a CD drive)\n");
        return -1;
    }

    uint8_t *buf = 0;
    uint32_t size = 0;
    if (kplg_find_on_iso(name, &buf, &size) != 0) {
        printf("[PLUGIN] %s: not found on ISO\n", name);
        return -1;
    }
    Plugin *p = 0;
    if (kplg_load(buf, size, &p) != 0) {
        printf("[PLUGIN] %s: load failed\n", name);
        return -1;
    }
    if (!p) return 0;              /* 依赖模块(无 kplg_plugin),已注册供符号解析 */
    return plugin_register(p);
}

/* 从嵌入表(内存 .kplg)加载并注册(不 init)。无插件 ISO 时使用:
 * x86 安装内核(硬盘启动无 CD)、以及无光驱的 ARM/RISC-V 版。 */
static int plugin_load_embed(const char *name) {
    if (plugin_find(name)) return 0;
    for (uint32_t i = 0; i < kplg_embed_count; i++) {
        if (strcmp(kplg_embed_table[i].name, name) != 0) continue;
        Plugin *p = 0;
        if (kplg_load(kplg_embed_table[i].buf, kplg_embed_table[i].size, &p) != 0) {
            printf("[PLUGIN] %s: embed load failed\n", name);
            return -1;
        }
        if (!p) return 0;          /* 依赖模块(无 kplg_plugin) */
        return plugin_register(p);
    }
    return -1;
}

/* 手动加载: 加载 + 注册 + 立即 init */
int plugin_load_file(const char *name) {
    if (plugin_find(name)) return 0;
    if (plugin_load_iso_only(name) != 0)
        if (plugin_load_embed(name) != 0) return -1;
    Plugin *p = plugin_find(name);
    if (p && p->init) p->init();
    return 0;
}

int plugin_init_all(void) {
    int loaded = 0;
    if (kplg_mount_iso()) {
        for (int i = 0; kplg_auto[i]; i++) {
            if (plugin_load_iso_only(kplg_auto[i]) == 0) loaded++;
        }
    } else {
        puts("[PLUGIN] no plugin ISO found - loading embedded plugins\n");
        for (int i = 0; kplg_auto[i]; i++) {
            if (plugin_load_embed(kplg_auto[i]) == 0) loaded++;
        }
    }
    /* 全部加载完成后统一 init(跨插件引用已解析) */
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i]->init) {
            printf("[PLUGIN] init: %s\n", plugins[i]->name);
            plugins[i]->init();
        }
    }
    printf("[PLUGIN] %d plugins ready\n", plugin_count);
    return loaded;
}

/* ---- 可加载外围驱动 (.kdrv = ELF64 ET_DYN, 导出 kdrv_driver) ----
 * 在 plugin_init_all() 之前由 kernel_main 调用: ata/iso 已就绪,
 * gfx/mouse/kbd/rtc/rootfs 等符号按 kdrv_auto 顺序先于插件解析。 */

/* 驱动模块加载: kplg_load 已注册符号模块; kdrv_driver 结构并入驱动表 */
static int drv_load_buf(const uint8_t *buf, uint32_t size) {
    Plugin *p = 0;
    if (kplg_load(buf, size, &p) != 0) return -1;
    Driver *d = kplg_last_driver();
    if (d) driver_register(d);
    (void)p;
    return 0;
}

/* ISO 优先, 嵌入表兜底(installer/无 CD) */
static int drv_load_one(const char *name) {
    if (driver_find(name)) return 0;
    uint8_t *buf = 0;
    uint32_t size = 0;
    if (kplg_mount_iso() && kplg_find_in_dirs(kdrv_iso_dirs, name, &buf, &size) == 0) {
        if (drv_load_buf(buf, size) != 0) {
            printf("[DRV] %s: load failed\n", name);
            return -1;
        }
        return 0;
    }
    for (uint32_t i = 0; i < kplg_embed_count; i++) {
        if (strcmp(kplg_embed_table[i].name, name) != 0) continue;
        if (drv_load_buf(kplg_embed_table[i].buf, kplg_embed_table[i].size) != 0) {
            printf("[DRV] %s: embed load failed\n", name);
            return -1;
        }
        return 0;
    }
    printf("[DRV] %s: not found on ISO %s or embed\n", name, kdrv_iso_dirs[0]);
    return -1;
}

int drv_load_all(void) {
    int before = driver_count();
    int loaded = 0;
    for (int i = 0; kdrv_auto[i]; i++) {
        if (drv_load_one(kdrv_auto[i]) == 0) loaded++;
    }
    /* 只 init 本次新注册的驱动(静态驱动已由 driver_init_all 处理) */
    driver_init_from(before);
    printf("[DRV] %d modules loaded (%d registered)\n", loaded, driver_count());
    return loaded;
}

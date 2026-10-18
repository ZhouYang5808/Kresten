#include <plugin.h>
#include <driver.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "kplg.h"
#include "kplg_embed.h"

#define MAX_PLUGINS 16

static Plugin *plugins[MAX_PLUGINS];
static int plugin_count = 0;

/* 启动时自动加载顺序(依赖顺序: crt_bind 绑定 CRT 导出表先加载;
 * linux 排在 vm 之后: linux.kplg 的 init 引用 vm 的 vm_guest_register
 * (跨模块符号, 加载期解析), 自注册到 vm 平台; hello 先于 shell,
 * shell.c 直接引用 plugin_hello_cmd; desktop 最后(引用 vm 状态查询) */
static const char *kplg_auto[] = {
    "crt_bind", "vm", "linux", "hello", "shell", "desktop", 0
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

/* ---- 从嵌入表(内存 .kplg)加载 ---- */

/* 加载并注册(不 init)。无插件结构的依赖模块(如 linux.kplg)返回 0。 */
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
    if (plugin_load_embed(name) != 0) return -1;
    Plugin *p = plugin_find(name);
    if (p && p->init) p->init();
    return 0;
}

int plugin_init_all(void) {
    int loaded = 0;
    for (int i = 0; kplg_auto[i]; i++) {
        if (plugin_load_embed(kplg_auto[i]) == 0) loaded++;
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

/* ---- 可加载外围驱动 (.kdrv = ELF32 ET_DYN, 导出 kdrv_driver) ----
 * 在 plugin_init_all() 之前由 kernel_main 调用: 驱动框架已就绪,
 * gfx/mouse/kbd/lcd/rtc/ps2packet/font 等符号按 kdrv_auto 顺序
 * 先于插件解析(ARM/RISC-V 无光驱, 全部从嵌入表加载)。 */

/* 驱动加载顺序(依赖序): font 先于 gfx(gfx_draw_char 引 gfx_font_bmp),
 * mouse 引 ps2packet。全部先于插件加载
 * (desktop 引 gfx_get_font_aa 与 gfx/mouse 符号, shell 引 kbd/lcd/rtc)。 */
static const char *kdrv_auto[] = {
    "ps2packet", "font", "kbd", "lcd", "gfx", "rtc", "mouse", 0
};

/* 驱动模块加载: kplg_load 已注册符号模块; kdrv_driver 结构并入驱动表 */
static int drv_load_buf(const uint8_t *buf, uint32_t size) {
    Plugin *p = 0;
    if (kplg_load(buf, size, &p) != 0) return -1;
    Driver *d = kplg_last_driver();
    if (d) driver_register(d);
    (void)p;
    return 0;
}

/* 从嵌入表加载单个驱动(.kdrv), 已注册则跳过 */
static int drv_load_one(const char *name) {
    if (driver_find(name)) return 0;
    for (uint32_t i = 0; i < kplg_embed_count; i++) {
        if (strcmp(kplg_embed_table[i].name, name) != 0) continue;
        if (drv_load_buf(kplg_embed_table[i].buf, kplg_embed_table[i].size) != 0) {
            printf("[DRV] %s: embed load failed\n", name);
            return -1;
        }
        return 0;
    }
    printf("[DRV] %s: not found in embed table\n", name);
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

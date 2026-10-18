#ifndef _PLUGIN_H
#define _PLUGIN_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[32];
    char desc[64];
    int (*init)(void);
    int (*command)(char *args);
} Plugin;

/* 可加载插件(.kplg = ELF64 ET_DYN, RISC-V): 每个插件导出唯一的 kplg_plugin 符号。
 * 加载器解析该符号后注册; init/command 经动态链接解析到内核导出符号。 */
#define REGISTER_PLUGIN(name) \
    Plugin kplg_plugin __attribute__((visibility("default"))) = \
        { #name, #name " plugin", plugin_##name##_init, plugin_##name##_cmd }

/* ---- kernel side ---- */
int plugin_init_all(void);
Plugin *plugin_find(const char *name);
int plugin_dispatch(const char *cmd, const char *args);
void plugin_list_all(void);
/* 从嵌入式 .kplg 表加载并初始化单个插件;已加载则跳过 */
int plugin_load_file(const char *name);
/* 从嵌入表按 kdrv_auto 顺序加载外围驱动 .kdrv 并注册/init;
 * 必须在 plugin_init_all() 之前调用(插件引用 gfx/mouse/kbd/rtc 等驱动符号)。 */
int drv_load_all(void);

/* 内核导出符号表(构建期由 gen_kernel_symtab.py 从 kernel.elf 生成) */
typedef struct {
    const char *name;
    void *addr;
} Ksym;
extern const Ksym kernel_symtab[];
extern const uint32_t kernel_symtab_count;

/* vm plugin status queries (used by the desktop app) */
int plugin_vm_available(void);
const char *plugin_vm_status(void);

#endif

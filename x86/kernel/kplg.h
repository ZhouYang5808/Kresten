#ifndef _KPLG_H
#define _KPLG_H

#include <stdint.h>
#include <driver.h>

/* 可加载插件 (.kplg = ELF64 ET_DYN, x86-64, PIE)
 * 加载器: kernel/kplg.c
 * 插件区: 0x04000000..0x05000000 (boot.s 2MiB 页身份映射 0..4G, RWX)
 * 避开 0x01A00000..0x03200000 的 rootfs 解包缓冲(rf_heap + gzbuf)。 */

#define KPLG_AREA_BASE 0x04000000u
#define KPLG_AREA_SIZE (16u * 1024 * 1024)

int kplg_load(const uint8_t *file, uint32_t size, Plugin **out);
/* 枚举已加载模块: idx 越界返回 -1 */
int kplg_mod_list(int idx, const char **name, void **base);
/* 插件区 bump 分配(供装载 ISO 文件缓冲等) */
void *kplg_alloc(uint32_t size);
/* 最近一次 kplg_load 的驱动模块(.kdrv)导出的 Driver 结构; 非驱动模块返回 0 */
Driver *kplg_last_driver(void);

#endif

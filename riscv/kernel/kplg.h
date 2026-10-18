#ifndef _KPLG_H
#define _KPLG_H

#include <stdint.h>
#include <driver.h>

/* 可加载插件 (.kplg = ELF64 ET_DYN, RISC-V, machine=243)
 * 加载器: kernel/kplg.c
 * 插件区: 0x80A00000..0x80B00000 (16 MB bump 分配)。
 * 避开: 内核 0x80000000..、lx_img 0x80300000..0x8050xxxx、
 *       lcd_fb 0x80800000..0x80960000; 128MB RAM(0x88000000)内空闲。 */

#define KPLG_AREA_BASE 0x80A00000u
#define KPLG_AREA_SIZE (16u * 1024 * 1024)

int kplg_load(const uint8_t *file, uint32_t size, Plugin **out);
/* 枚举已加载模块: idx 越界返回 -1 */
int kplg_mod_list(int idx, const char **name, void **base);
/* 插件区 bump 分配(供装载缓冲) */
void *kplg_alloc(uint32_t size);
/* 驱动装载器取回最近一次 kplg_load 的 kdrv_driver 结构(无则为 0) */
Driver *kplg_last_driver(void);

#endif

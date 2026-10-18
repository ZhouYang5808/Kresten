#ifndef _KPLG_H
#define _KPLG_H

#include <stdint.h>
#include <driver.h>

/* 可加载插件 (.kplg = ELF32 ET_DYN, ARM, machine=40)
 * 加载器: kernel/kplg.c
 * 插件区: 0x02000000..0x03000000 (16 MB bump 分配)。
 * 避开: 内核 0x10000..0x1A0000、显存 0x100000..0x196000、
 *       lx_img 0x1A0000..0x3B1C28、LX 区 0x400000..0x1C0000(0x1000000 brk
 *       + 0x1400000 mmap + 0x1800000 栈)之上; 128MB RAM 内空闲。 */

#define KPLG_AREA_BASE 0x02000000u
#define KPLG_AREA_SIZE (16u * 1024 * 1024)

int kplg_load(const uint8_t *file, uint32_t size, Plugin **out);
/* 枚举已加载模块: idx 越界返回 -1 */
int kplg_mod_list(int idx, const char **name, void **base);
/* 插件区 bump 分配(供装载缓冲) */
void *kplg_alloc(uint32_t size);
/* 驱动装载器取回最近一次 kplg_load 的 kdrv_driver 结构(无则为 0) */
Driver *kplg_last_driver(void);

#endif

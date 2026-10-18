#ifndef _KPLG_EMBED_H
#define _KPLG_EMBED_H

#include <stdint.h>

/* 嵌入式 .kplg 表(由 scripts/pack_kplg_embed.py 生成 kplg_embed.c):
 * ARM/RISC-V 无光驱, 插件从内存表加载。 */
typedef struct {
    const char *name;
    const uint8_t *buf;
    uint32_t size;
} KplgEmbed;

extern const KplgEmbed kplg_embed_table[];
extern const uint32_t kplg_embed_count;

#endif

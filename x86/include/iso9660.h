#ifndef _ISO9660_H
#define _ISO9660_H

#include <stdint.h>

typedef struct {
    uint32_t extent;   /* start LBA (2048-byte sectors) */
    uint32_t size;
    uint8_t  flags;    /* bit1 = directory */
} iso_file_t;

/* 解析 drive 上光驱的 PVD(卷描述符);返回 0 成功。 */
int  iso_mount(uint8_t drive);
const char *iso_volid(void);
/* 当前挂载的光驱槽位号 */
uint8_t iso_drive(void);
/* 按大小写不敏感路径查找,如 "/boot/core.gz" 或 "boot/core.gz"。 */
int  iso_lookup(const char *path, iso_file_t *out);
/* 读出已查找文件的连续数据到 dst(限制 size 内)。 */
int  iso_read_file(const iso_file_t *f, void *dst);
/* 从已查找文件的 off 偏移起读 len 字节(跨扇区分段)。返回 0 成功。 */
int  iso_read_at(const iso_file_t *f, uint32_t off, void *dst, uint32_t len);
/* 遍历目录 path 的每条记录(含文件与子目录,不含 . / ..)。
 * cb 返回非 0 时停止;返回 0 遍历完成。 */
typedef int (*iso_dir_cb)(const char *name, uint32_t size, uint8_t flags,
                          void *arg);
int  iso_read_dir(const char *path, iso_dir_cb cb, void *arg);

#endif
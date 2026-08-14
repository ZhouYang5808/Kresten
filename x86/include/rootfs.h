#ifndef _ROOTFS_H
#define _ROOTFS_H

#include <stdint.h>

#define RF_TYPE_DIR    1
#define RF_TYPE_FILE   2
#define RF_TYPE_SYMLNK 3

typedef struct rf_node {
    uint32_t parent;      /* 父节点索引, 0xFFFFFFFF = 无 */
    uint32_t child;       /* 首子节点索引, 0xFFFFFFFF = 无 */
    uint32_t sibling;     /* 下一个兄弟索引, 0xFFFFFFFF = 无 */
    uint32_t name_off;    /* 名字在 name_pool 中的偏移 */
    uint32_t size;        /* 文件大小 */
    uint32_t data_off;    /* 数据区偏移(文件); symlink: 目标字符串偏移 */
    uint32_t type;        /* RF_TYPE_* */
} rf_node;

#define RF_BAD 0xFFFFFFFFu

/* 导入: 从 CD 读发行版 rootfs(core.gz) 装入内存树。
 * iso_slot: 0xFF = 自动扫描。返回 1 成功。 */
int  rootfs_import(void);
int  rootfs_ready(void);
const char *rootfs_distro_name(void);
const rf_node *rf_root(void);
const rf_node *rf_lookup(const char *path);       /* "/bin/sh" 或 "bin/sh" */
const rf_node *rf_child(const rf_node *dir);
const rf_node *rf_next(const rf_node *node);
const char *rf_name(const rf_node *node);
const uint8_t *rf_data(const rf_node *node);
uint32_t rf_node_count(void);

#endif
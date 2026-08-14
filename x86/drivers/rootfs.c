/* 内存 rootfs: gzip core.gz -> cpio newc 解包 -> 目录树。
 * 数据区直接落在解压缓冲内(文件数据零拷贝)。 */

#include <rootfs.h>
#include <iso9660.h>
#include <inflate.h>
#include <ata.h>
#include <stdio.h>
#include <string.h>

#define RF_CPIO_MAX   (12u * 1024 * 1024)   /* 解压缓冲(亦是数据区) */
#define RF_NODE_MAX   4096
#define RF_NAME_POOL  (256u * 1024)

/* 大缓冲固定在 0x1A00000(恰在 LX 池上界之后), 避免撑大内核 .bss 覆盖 LX 栈。
 * 内存 128MB(QEMU 默认)下此区空闲。*/
#define RF_HEAP_PHYS 0x01A00000u
#define RF_GZ_PHYS   (RF_HEAP_PHYS + RF_CPIO_MAX)
#define RF_GZ_SIZE   (12u * 1024 * 1024)

static uint8_t *rf_heap   = (uint8_t *)RF_HEAP_PHYS;
static uint8_t *gzbuf     = (uint8_t *)RF_GZ_PHYS;
static rf_node  rf_nodes[RF_NODE_MAX];
static char     rf_pool[RF_NAME_POOL];
static uint32_t rf_nnodes = 0;
static uint32_t rf_pool_used = 0;
static char     distro[64];
static int      rf_ok = 0;

int rootfs_ready(void) { return rf_ok; }
const char *rootfs_distro_name(void) { return distro; }
uint32_t rf_node_count(void) { return rf_nnodes; }
const rf_node *rf_root(void) { return rf_nodes; }

static uint32_t node_alloc(void) {
    if (rf_nnodes >= RF_NODE_MAX) return RF_BAD;
    return rf_nnodes++;
}

static uint32_t name_add(const char *s) {
    uint32_t len = (uint32_t)strlen(s) + 1;
    if (rf_pool_used + len > RF_NAME_POOL) return RF_BAD;
    uint32_t off = rf_pool_used;
    memcpy(rf_pool + off, s, len);
    rf_pool_used += len;
    return off;
}

const char *rf_name(const rf_node *node) {
    return rf_pool + node->name_off;
}

const rf_node *rf_child(const rf_node *dir) {
    if (dir->child != RF_BAD) return &rf_nodes[dir->child];
    return 0;
}

const rf_node *rf_next(const rf_node *node) {
    if (node->sibling != RF_BAD) return &rf_nodes[node->sibling];
    return 0;
}

const uint8_t *rf_data(const rf_node *node) {
    return rf_heap + node->data_off;
}

/* 在目录节点下查找名为 name 的子节点 */
static const rf_node *find_child(const rf_node *dir, const char *name) {
    const rf_node *n = (dir->child != RF_BAD) ? &rf_nodes[dir->child] : 0;
    for (; n; n = (n->sibling != RF_BAD) ? &rf_nodes[n->sibling] : 0)
        if (strcmp(rf_name(n), name) == 0) return n;
    return 0;
}

const rf_node *rf_lookup(const char *path) {
    if (!rf_ok) return 0;
    const rf_node *cur = rf_root();
    const char *p = path;
    while (*p == '/') p++;
    for (;;) {
        const char *end = p;
        while (*end && *end != '/') end++;
        if (end == p) break;
        char comp[128];
        int cl = end - p;
        if (cl >= 128) cl = 127;
        memcpy(comp, p, cl);
        comp[cl] = '\0';
        cur = find_child(cur, comp);
        if (!cur) return 0;
        if (*end == '\0') return cur;
        p = end + 1;
        while (*p == '/') p++;
        if (!*p) return cur;
    }
    return 0;
}

/* ---- cpio newc 解包 ---- */

static uint32_t hex2u(const char *s, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    }
    return v;
}

static uint32_t make_node(const char *name, uint32_t type, uint32_t size, uint32_t data_off) {
    uint32_t idx = node_alloc();
    if (idx == RF_BAD) return RF_BAD;
    uint32_t noff = name_add(name);
    if (noff == RF_BAD) return RF_BAD;
    rf_nodes[idx].parent = RF_BAD;
    rf_nodes[idx].child = RF_BAD;
    rf_nodes[idx].sibling = RF_BAD;
    rf_nodes[idx].name_off = noff;
    rf_nodes[idx].size = size;
    rf_nodes[idx].data_off = data_off;
    rf_nodes[idx].type = type;
    return idx;
}

/* 路径 "bin/busybox" 拆成分量, 找到或创建父链, 返回最终节点 idx */
static uint32_t path_insert(const char *path, uint32_t type, uint32_t size, uint32_t data_off) {
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    uint32_t cur = 0;               /* root = nodes[0] */
    char *p = tmp;
    for (;;) {
        char *sep = strchr(p, '/');
        if (sep) *sep = '\0';
        if (*p) {
            const rf_node *c = find_child(&rf_nodes[cur], p);
            uint32_t ci;
            if (c) {
                ci = (uint32_t)(c - rf_nodes);
            } else {
                ci = make_node(p, RF_TYPE_DIR, 0, 0);
                if (ci == RF_BAD) return RF_BAD;
                rf_nodes[ci].parent = cur;
                rf_nodes[ci].sibling = rf_nodes[cur].child;
                rf_nodes[cur].child = ci;
            }
            cur = ci;
            if (!sep) {
                /* 最终分量: 若是文件/链接覆盖占位目录 */
                rf_nodes[cur].type = type;
                rf_nodes[cur].size = size;
                rf_nodes[cur].data_off = data_off;
                return cur;
            }
            p = sep + 1;
        } else {
            if (!sep) break;
            p = sep + 1;
        }
    }
    return RF_BAD;
}

static void set_children_ref(void) {
    /* 树已通过 sibling 链接构建, child 链即正确, 无额外工作 */
}

int rootfs_import(void) {
    rf_nnodes = 0;
    rf_pool_used = 0;
    rf_ok = 0;

    /* 1. root 节点 */
    if (make_node("", RF_TYPE_DIR, 0, 0) == RF_BAD) return 0;

    /* 2. 逐一尝试所有 ATAPI 光驱槽位, 找含 /boot/core.gz 的发行版盘
     * (Kresten 启动盘是 El Torito ISO, 也可能占用一个 ATAPI 槽) */
    printf("[ROOTFS] ATA slots: %d %d %d %d\n",
           ata_dev_type(0), ata_dev_type(1), ata_dev_type(2), ata_dev_type(3));
    iso_file_t core;
    int found = 0;
    for (int d = 0; d < 4; d++) {
        if (ata_dev_type(d) != ATA_TYPE_ATAPI) continue;
        if (iso_mount(d) != 0) continue;
        if (iso_lookup("/boot/core.gz", &core) == 0) {
            printf("[ROOTFS] distro ISO on drive %d\n", d);
            found = 1;
            break;
        }
        puts("[ROOTFS] ISO has no /boot/core.gz, trying next drive");
    }
    if (!found) {
        printf("[ROOTFS] no distribution ISO found\n");
        return 0;
    }
    printf("[ROOTFS] core.gz: extent=%u size=%u\n", core.extent, core.size);

    /* 3. 读入原始 gzip 缓冲 */
    if (core.size > RF_GZ_SIZE) {
        printf("[ROOTFS] core.gz too big (%u)\n", core.size);
        return 0;
    }
    if (iso_read_file(&core, gzbuf) != 0) {
        printf("[ROOTFS] read core.gz failed\n");
        return 0;
    }

    /* 4. gunzip */
    uint32_t cplen;
    printf("[ROOTFS] gz: %02x %02x %02x %02x\n",
           gzbuf[0], gzbuf[1], gzbuf[2], gzbuf[3]);
    if (gzbuf[0] != 0x1F || gzbuf[1] != 0x8B) {
        printf("[ROOTFS] not a gzip stream\n");
        return 0;
    }
    {
        int r = gunzip(gzbuf, core.size, rf_heap, RF_CPIO_MAX);
        if (r < 0) { printf("[ROOTFS] gunzip failed\n"); return 0; }
        cplen = (uint32_t)r;
    }
    printf("[ROOTFS] cpio: %u bytes\n", cplen);

    /* 5. 解 cpio newc */
    uint32_t off = 0;
    int files = 0;
    while (off + 110 <= cplen) {
        int badmagic = 1;
        {
            const char *m = (const char *)rf_heap + off;
            if (m[0] == '0' && m[1] == '7' && m[2] == '0' && m[3] == '7' &&
                m[4] == '0' && m[5] == '1') badmagic = 0;
        }
        if (badmagic) {
            printf("[ROOTFS] cpio magic? off=%u\n", off);
            break;
        }
        const char *h = (const char *)rf_heap + off;
        uint32_t hino   = hex2u(h + 6, 8);
        uint32_t hmode  = hex2u(h + 14, 8);
        uint32_t hsize  = hex2u(h + 54, 8);
        uint32_t hnames = hex2u(h + 94, 8);
        (void)hino;
        const char *name = h + 110;
        uint32_t namepad = (4 - (110 + hnames) % 4) % 4;
        uint32_t datpad  = (4 - hsize % 4) % 4;
        uint32_t data = off + 110 + hnames + namepad;

        if (strcmp(name, "TRAILER!!!") == 0) break;

        uint32_t t;
        uint32_t mode = hmode & 0xF000;
        if (mode == 0x4000) t = RF_TYPE_DIR;
        else if (mode == 0xA000) t = RF_TYPE_SYMLNK;
        else t = RF_TYPE_FILE;
        if (mode == 0x0000) t = RF_TYPE_FILE;

        uint32_t idx = path_insert(name, t, hsize, data);
        if (idx == RF_BAD) { printf("[ROOTFS] nodes full at %s\n", name); break; }
        if (t == RF_TYPE_FILE && hsize == 0) {
            /* 空文件: data_off 指向尽力而为 */
        }
        files++;
        off = data + hsize + datpad;
    }
    (void)set_children_ref;

    /* 6. 发行版名: 读 /etc/tc-release(存在时), 否则取 ISO 卷标 */
    const rf_node *rel = rf_lookup("/etc/tc-release");
    if (rel) {
        uint32_t n = rel->size;
        if (n > 63) n = 63;
        memcpy(distro, rf_data(rel), n);
        distro[n] = '\0';
        for (int i = 0; distro[i]; i++)
            if (distro[i] == '\n') distro[i] = 0;
    } else {
        strncpy(distro, iso_volid(), 62);
        distro[62] = '\0';
    }

    rf_ok = 1;
    printf("[ROOTFS] imported: '%s', %u nodes, %u files\n", distro, rf_nnodes, files);
    return 1;
}
#include <iso9660.h>
#include <ata.h>
#include <stdio.h>
#include <string.h>

#define ISO_SECTOR 2048
#define ISO_PVD_SECTOR 16

static uint8_t pvd[ISO_SECTOR];
static char volid[32];
static int  iso_ready = 0;
static uint8_t iso_dev = 0;

/* 目录记录(primary volume descriptor / 目录条目) */
typedef struct {
    const uint8_t *rec;
    uint32_t extent;
    uint32_t size;
    uint8_t  flags;
    char     name[128];
} isorec;

static const uint8_t *dir_data;     /* 指向已读入的目录缓冲 */
static uint32_t dir_len;

static int iso_read_sector(uint32_t lba, void *dst) {
    int r = atapi_read(iso_dev, lba, 1, dst);
    if (r != 0) printf("[ISO] read sector %u (drive %d) failed\n", lba, iso_dev);
    return r;
}

/* 解析一条目录记�?rec 指向记录首字�?目录缓冲基址已知)�?*/
static void iso_parse_rec(const uint8_t *rec, isorec *r) {
    r->rec = rec;
    r->extent = rec[2] | (rec[3] << 8) | (rec[4] << 16) | ((uint32_t)rec[5] << 24);
    r->size   = rec[10] | (rec[11] << 8) | (rec[12] << 16) | ((uint32_t)rec[13] << 24);
    r->flags  = rec[25];
    uint8_t nl = rec[32];
    int out = 0;
    for (int i = 0; i < nl && i < 126; i++) {
        char c = rec[33 + i];
        if (c == ';' || c == '.') {     /* ';' = 版本�? '.' 保留(文件名含点仍保留) */
            if (c == ';') break;
        }
        r->name[out++] = c;
    }
    r->name[out] = '\0';
}

/* 找出目录数据缓冲中的子记�? 遍历记录链直到命中名�?忽略 . �?..)�?*/
static int dir_find(const char *name, isorec *found) {
    uint32_t off = 0;
    while (off + 34 <= dir_len) {
        const uint8_t *rec = dir_data + off;
        uint8_t l = rec[0];
        if (l == 0) break;              /* 目录记录结束 */
        if (off + l > dir_len) break;
        isorec r;
        iso_parse_rec(rec, &r);
        if (r.name[0] == 0 || strcmp(r.name, ".") == 0 || strcmp(r.name, "..") == 0)
            goto next;
        /* 大小写不敏感比较 */
        int match = 1;
        const char *a = r.name, *b = name;
        for (; *a && *b; a++, b++) {
            char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) { match = 0; break; }
        }
        if (match && !*a && !*b) {
            *found = r;
            return 1;
        }
next:
        off += l;
    }
    return 0;
}

/* 载入目录 extent 数据到静态缓�? 返回缓冲指针(�?NULL)�?*/
static const uint8_t *dir_load(uint32_t extent, uint32_t size) {
    static uint8_t buf[64 * 1024];
    if (size > sizeof(buf)) size = sizeof(buf);
    uint32_t sectors = (size + ISO_SECTOR - 1) / ISO_SECTOR;
    for (uint32_t s = 0; s < sectors; s++) {
        if (iso_read_sector(extent + s, buf + s * ISO_SECTOR) != 0)
            return NULL;
    }
    dir_data = buf;
    dir_len = size;
    return buf;
}

int iso_mount(uint8_t drive) {
    iso_ready = 0;
    /* drive=0xFF: 自动扫描所�?ATAPI 槽位 */
    int first = (drive == 0xFF) ? 0 : drive;
    int last  = (drive == 0xFF) ? 3 : drive;
    for (int d = first; d <= last; d++) {
        if (ata_dev_type(d) != ATA_TYPE_ATAPI) continue;
        iso_dev = d;
        if (iso_read_sector(ISO_PVD_SECTOR, pvd) != 0) {
            printf("[ISO] drive %d: PVD read failed\n", d);
            continue;
        }
        if (pvd[0] != 1 || !(pvd[1] == 'C' && pvd[2] == 'D' && pvd[3] == '0' &&
              pvd[4] == '0' && pvd[5] == '1')) {
            printf("[ISO] drive %d: bad PVD (%02x %c%c%c%c%c)\n",
                   d, pvd[0], pvd[1], pvd[2], pvd[3], pvd[4], pvd[5]);
            continue;
        }
        int n = 0;
        for (int i = 40; i < 72 && pvd[i] && n < 31; i++) volid[n++] = pvd[i];
        volid[n] = '\0';
        iso_ready = 1;
        printf("[ISO] mounted: drive %d volid '%s'\n", d, volid);
        return 0;
    }
    return -1;
}

const char *iso_volid(void) {
    return iso_ready ? volid : "";
}

uint8_t iso_drive(void) {
    return iso_ready ? iso_dev : 0;
}

int iso_lookup(const char *path, iso_file_t *out) {
    if (!iso_ready) return -1;
    /* 根目录记录在 PVD 偏移 156 */
    isorec cur;
    iso_parse_rec(pvd + 156, &cur);
    if (!dir_load(cur.extent, cur.size)) return -1;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == '\0') {          /* 根路径 "/" */
        out->extent = cur.extent;
        out->size = cur.size;
        out->flags = cur.flags;
        return 0;
    }
    for (;;) {
        const char *end = p;
        while (*end && *end != '/') end++;
        char comp[128];
        int cl = end - p;
        if (cl >= 128) cl = 127;
        memcpy(comp, p, cl);
        comp[cl] = '\0';
        if (!dir_find(comp, &cur)) return -1;
        if (*end == '\0') break;
        if (!(cur.flags & 2)) return -1;    /* 中间组件必须是目�?*/
        if (!dir_load(cur.extent, cur.size)) return -1;
        p = end + 1;
        while (*p == '/') p++;
    }
    out->extent = cur.extent;
    out->size = cur.size;
    out->flags = cur.flags;
    return 0;
}

int iso_read_file(const iso_file_t *f, void *dst) {
    if (!iso_ready) return -1;
    uint8_t *out = (uint8_t *)dst;
    uint32_t n = f->size;
    uint32_t iter = 0;
    while (n > 0) {
        uint32_t chunk = n > ISO_SECTOR ? ISO_SECTOR : n;
        if (iso_read_sector(f->extent + ((f->size - n) / ISO_SECTOR), out) != 0) {
            printf("[ISO] read_file: extent=%u size=%u n=%u iter=%u\n",
                   f->extent, f->size, n, iter);
            return -1;
        }
        out += chunk;
        n -= chunk;
        iter++;
    }
    return 0;
}

/* 偏移�? 跨扇区分段拷贝。ISO 扇区 2048 字节对齐, 无需缓冲对齐假设�?*/
int iso_read_at(const iso_file_t *f, uint32_t off, void *dst, uint32_t len) {
    if (!iso_ready) return -1;
    if (off >= f->size) return -1;
    if (off + len > f->size) len = f->size - off;
    uint8_t *out = (uint8_t *)dst;
    while (len > 0) {
        uint32_t sec = f->extent + off / ISO_SECTOR;
        uint32_t skip = off % ISO_SECTOR;
        uint32_t chunk = ISO_SECTOR - skip;
        if (chunk > len) chunk = len;
        uint8_t tmp[ISO_SECTOR];
        if (iso_read_sector(sec, tmp) != 0) return -1;
        memcpy(out, tmp + skip, chunk);
        out += chunk;
        off += chunk;
        len -= chunk;
    }
    return 0;
}

/* 遍历目录记录。目录也�?ISO 文件(extent/size/flags), 复用 lookup�?*/
int iso_read_dir(const char *path, iso_dir_cb cb, void *arg) {
    if (!iso_ready) return -1;
    iso_file_t d;
    if (iso_lookup(path, &d) != 0) return -1;
    if (!(d.flags & 2)) return -1;          /* 不是目录 */
    const uint8_t *buf = dir_load(d.extent, d.size);
    if (!buf) return -1;
    uint32_t off = 0;
    while (off + 34 <= dir_len) {
        const uint8_t *rec = dir_data + off;
        uint8_t l = rec[0];
        if (l == 0) break;
        if (off + l > dir_len) break;
        isorec r;
        iso_parse_rec(rec, &r);
        if (r.name[0] == 0 || strcmp(r.name, ".") == 0 || strcmp(r.name, "..") == 0)
            goto next;
        if (cb(r.name, r.size, r.flags, arg))
            return 0;
next:
        off += l;
    }
    return 0;
}

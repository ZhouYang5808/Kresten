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

/* 鐩綍璁板綍(primary volume descriptor / 鐩綍鏉＄洰) */
typedef struct {
    const uint8_t *rec;
    uint32_t extent;
    uint32_t size;
    uint8_t  flags;
    char     name[128];
} isorec;

static const uint8_t *dir_data;     /* 鎸囧悜宸茶鍏ョ殑鐩綍缂撳啿 */
static uint32_t dir_len;

static int iso_read_sector(uint32_t lba, void *dst) {
    int r = atapi_read(iso_dev, lba, 1, dst);
    if (r != 0) printf("[ISO] read sector %u (drive %d) failed\n", lba, iso_dev);
    return r;
}

/* 瑙ｆ瀽涓€鏉＄洰褰曡锟?rec 鎸囧悜璁板綍棣栧瓧锟?鐩綍缂撳啿鍩哄潃宸茬煡)锟?*/
static void iso_parse_rec(const uint8_t *rec, isorec *r) {
    r->rec = rec;
    r->extent = rec[2] | (rec[3] << 8) | (rec[4] << 16) | ((uint32_t)rec[5] << 24);
    r->size   = rec[10] | (rec[11] << 8) | (rec[12] << 16) | ((uint32_t)rec[13] << 24);
    r->flags  = rec[25];
    uint8_t nl = rec[32];
    int out = 0;
    for (int i = 0; i < nl && i < 126; i++) {
        char c = rec[33 + i];
        if (c == ';') break;            /* ';' 版本号: 截断; '.' 保留 (LIBC.SO6) */
        r->name[out++] = c;
    }
    if (out > 0 && r->name[out - 1] == '.') out--;   /* xorriso 无扩展文件 = 'NAME.' */
    r->name[out] = '\0';
}

/* 鎵惧嚭鐩綍鏁版嵁缂撳啿涓殑瀛愯锟? 閬嶅巻璁板綍閾剧洿鍒板懡涓悕锟?蹇界暐 . 锟?..)锟?*/
static int dir_find(const char *name, isorec *found) {
    uint32_t off = 0;
    while (off + 34 <= dir_len) {
        const uint8_t *rec = dir_data + off;
        uint8_t l = rec[0];
        if (l == 0) { off++; continue; }              /* 鐩綍璁板綍缁撴潫 */
        if (off + l > dir_len) break;
        isorec r;
        iso_parse_rec(rec, &r);
        if (r.name[0] == 0 || strcmp(r.name, ".") == 0 || strcmp(r.name, "..") == 0)
            goto next;
        /* 澶у皬鍐欎笉鏁忔劅姣旇緝 */
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

/* 杞藉叆鐩綍 extent 鏁版嵁鍒伴潤鎬佺紦锟? 杩斿洖缂撳啿鎸囬拡(锟?NULL)锟?*/
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
    /* drive=0xFF: 鑷姩鎵弿鎵€锟?ATAPI 妲戒綅 */
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
        while (n > 0 && volid[n - 1] == ' ') n--;   /* xorriso 卷标右填充空格 */
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

int iso_mount_volid(const char *vol) {
    for (int d = 0; d < 4; d++) {
        if (ata_dev_type(d) != ATA_TYPE_ATAPI) continue;
        uint8_t saved = iso_dev;   /* 卷标不匹配时保持原挂载不变 */
        iso_dev = d;
        if (iso_read_sector(ISO_PVD_SECTOR, pvd) != 0) {
            iso_dev = saved;
            continue;
        }
        if (pvd[0] != 1 || !(pvd[1] == 'C' && pvd[2] == 'D' && pvd[3] == '0' &&
              pvd[4] == '0' && pvd[5] == '1')) {
            iso_dev = saved;
            continue;
        }
        int n = 0;
        for (int i = 40; i < 72 && pvd[i] && n < 31; i++) volid[n++] = pvd[i];
        while (n > 0 && volid[n - 1] == ' ') n--;   /* xorriso 卷标右填充空格 */
        volid[n] = '\0';
        if (strcmp(volid, vol) != 0) {
            iso_dev = saved;
            continue;
        }
        iso_ready = 1;
        printf("[ISO] mounted: drive %d volid '%s'\n", d, volid);
        return 0;
    }
    return -1;
}

uint8_t iso_drive(void) {
    return iso_ready ? iso_dev : 0;
}

int iso_lookup(const char *path, iso_file_t *out) {
    if (!iso_ready) return -1;
    /* 鏍圭洰褰曡褰曞湪 PVD 鍋忕Щ 156 */
    isorec cur;
    iso_parse_rec(pvd + 156, &cur);
    if (!dir_load(cur.extent, cur.size)) return -1;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == '\0') {          /* 鏍硅矾寰?"/" */
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
        if (!(cur.flags & 2)) return -1;    /* 涓棿缁勪欢蹇呴』鏄洰锟?*/
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
    while (n > 0) {
        uint32_t secs = (n + ISO_SECTOR - 1) / ISO_SECTOR;
        if (secs > 8) secs = 8;           /* QEMU IDE io_buffer=16384B=8 扇区, 大批卡死 */
        uint32_t chunk = secs * ISO_SECTOR;
        if (chunk > n) chunk = n;         /* 末段不足整扇区: 仍读整扇区, 缓冲须 >= size */
        printf("[ISO] read_file: extent=%u size=%u n=%u secs=%u\n", f->extent, f->size, n, secs);
        if (atapi_read(iso_dev, f->extent + ((f->size - n) / ISO_SECTOR), secs, out) != 0) {
            printf("[ISO] read_file: FAIL extent=%u size=%u n=%u\n", f->extent, f->size, n);
            return -1;
        }
        out += chunk;
        n -= chunk;
    }
    printf("[ISO] read_file: done size=%u\n", f->size);
    return 0;
}

/* 鍋忕Щ璇? 璺ㄦ墖鍖哄垎娈垫嫹璐濄€侷SO 鎵囧尯 2048 瀛楄妭瀵归綈銆?
 * 棣栧熬鏈榻愭墖鍖洪€愪釜璇? 涓棿鏁存墖鍖哄潡鎵归噺璇汇€?*/
int iso_read_at(const iso_file_t *f, uint32_t off, void *dst, uint32_t len) {
    if (!iso_ready) return -1;
    if (off >= f->size) return -1;
    if (off + len > f->size) len = f->size - off;
    uint8_t *out = (uint8_t *)dst;
    /* 1. 棣栭儴鏈榻愭墖鍖?*/
    while (len > 0 && off % ISO_SECTOR != 0) {
        uint8_t tmp[ISO_SECTOR];
        uint32_t sec = f->extent + off / ISO_SECTOR;
        if (iso_read_sector(sec, tmp) != 0) return -1;
        uint32_t skip = off % ISO_SECTOR;
        uint32_t chunk = ISO_SECTOR - skip;
        if (chunk > len) chunk = len;
        memcpy(out, tmp + skip, chunk);
        out += chunk; off += chunk; len -= chunk;
    }
    /* 2. 涓棿鏁存墖鍖哄潡 */
    while (len >= ISO_SECTOR) {
        uint32_t secs = len / ISO_SECTOR;
        if (secs > 31) secs = 31;
        if (atapi_read(iso_dev, f->extent + off / ISO_SECTOR, secs, out) != 0) return -1;
        uint32_t chunk = secs * ISO_SECTOR;
        out += chunk; off += chunk; len -= chunk;
    }
    /* 3. 灏鹃儴鏈榻愭墖鍖?*/
    if (len > 0) {
        uint8_t tmp[ISO_SECTOR];
        if (iso_read_sector(f->extent + off / ISO_SECTOR, tmp) != 0) return -1;
        memcpy(out, tmp, len);
    }
    return 0;
}

/* 閬嶅巻鐩綍璁板綍銆傜洰褰曚篃锟?ISO 鏂囦欢(extent/size/flags), 澶嶇敤 lookup锟?*/
int iso_read_dir(const char *path, iso_dir_cb cb, void *arg) {
    if (!iso_ready) return -1;
    iso_file_t d;
    if (iso_lookup(path, &d) != 0) return -1;
    if (!(d.flags & 2)) return -1;          /* 涓嶆槸鐩綍 */
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

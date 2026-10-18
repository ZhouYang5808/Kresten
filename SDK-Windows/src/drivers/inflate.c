/* 最小 gzip (RFC1951 deflate + RFC1952 wrapper) 解压器。
 * 支持 stored / fixed / dynamic Huffman 块;无 CRC 校验。 */

#include <inflate.h>
#include <stdio.h>
#include <string.h>
#include <driver.h>

#define MAX_BITS 15

typedef struct {
    uint16_t cnt[MAX_BITS + 1];
    uint16_t sym[288 + 32];     /* 规范哈夫曼有序符号表 */
} htable;

static const uint8_t *in;
static uint32_t inlen, inpos;
static uint8_t *out;
static uint32_t outlen, outcap;
static uint32_t bitbuf;
static int      bitcnt;

static int rd_byte(void) {
    if (inpos >= inlen) return -1;
    return in[inpos++];
}

static int rd_bits(int n) {
    while (bitcnt < n) {
        int b = rd_byte();
        if (b < 0) return -1;
        bitbuf |= (uint32_t)b << bitcnt;
        bitcnt += 8;
    }
    uint32_t v = bitbuf & ((1u << n) - 1);
    bitbuf >>= n;
    bitcnt -= n;
    return (int)v;
}

static int build_table(htable *h, const uint16_t *lens, int nsym) {
    memset(h, 0, sizeof(*h));
    for (int i = 0; i < nsym; i++) {
        if (lens[i] > MAX_BITS) return -1;   /* 防御: 非法码长(如 EOF 的 0xFFFF)不得索引 cnt[] */
        h->cnt[lens[i]]++;
    }
    h->cnt[0] = 0;
    int left = 1;
    for (int len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h->cnt[len];
        if (left < 0) return -1;
    }
    uint16_t off[MAX_BITS + 1];
    off[1] = 0;
    for (int len = 1; len < MAX_BITS; len++)
        off[len + 1] = off[len] + h->cnt[len];
    for (int i = 0; i < nsym; i++) {
        int l = lens[i];
        if (l) h->sym[off[l]++] = (uint16_t)i;
    }
    return 0;
}

/* 用位流解码一个符号。返回符号或 -1。 */
static int decode_sym(const htable *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        int b = rd_bits(1);
        if (b < 0) return -1;
        code |= b;
        int count = h->cnt[len];
        if (code - first < count)
            return h->sym[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
    67,83,99,115,131,163,195,227,258
};
static const uint16_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint16_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const uint16_t clen_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static int emit(uint8_t b) {
    if (outlen >= outcap) return -1;
    out[outlen++] = b;
    if ((outlen & 0x03FFFFFF) == 0)
        printf("[WINEFS] inflate: %u MB\n", outlen >> 20);
    return 0;
}

static int copy_dist(uint16_t dist, uint16_t len) {
    if (dist > outlen) return -1;
    for (int i = 0; i < len; i++) {
        uint8_t b = out[outlen - dist];
        if (emit(b) < 0) return -1;
    }
    return 0;
}

static int inflate_block(void) {
    int final = rd_bits(1);
    int type = rd_bits(2);
    if (type < 0 || final < 0) return -1;

    if (type == 0) {
        /* stored */
        bitbuf = 0; bitcnt = 0;
        int lo = rd_byte();
        int hi = rd_byte();
        if (lo < 0 || hi < 0) return -1;
        int len = lo | (hi << 8);       /* LEN (LE16) */
        rd_byte(); rd_byte();           /* 跳过 NLEN */
        for (int i = 0; i < len; i++) {
            int b = rd_byte();
            if (b < 0 || emit((uint8_t)b) < 0) return -1;
        }
        return final;
    }

    htable lit, dst;
    if (type == 1) {
        /* fixed: lit 0-143 8bit, 144-255 9bit, 256-279 7bit, 280-287 8bit */
        uint16_t lens[288];
        for (int i = 0; i < 144; i++) lens[i] = 8;
        for (int i = 144; i < 256; i++) lens[i] = 9;
        for (int i = 256; i < 280; i++) lens[i] = 7;
        for (int i = 280; i < 288; i++) lens[i] = 8;
        if (build_table(&lit, lens, 288) < 0) return -1;
        uint16_t dlen[30];
        for (int i = 0; i < 30; i++) dlen[i] = 5;
        if (build_table(&dst, dlen, 30) < 0) return -1;
    } else if (type == 2) {
        int rhlit = rd_bits(5), rhdist = rd_bits(5), rhclen = rd_bits(4);
        if (rhlit < 0 || rhdist < 0 || rhclen < 0) return -1;   /* EOF: 不能把 -1 当有效值 */
        int hlit = rhlit + 257;
        int hdist = rhdist + 1;
        int hclen = rhclen + 4;
        uint16_t clens[19];
        memset(clens, 0, sizeof(clens));
        for (int i = 0; i < hclen; i++) {
            int v = rd_bits(3);
            if (v < 0) return -1;       /* 否则 (uint16_t)-1 = 0xFFFF 越界写 cnt[] */
            clens[clen_order[i]] = (uint16_t)v;
        }
        htable clen;
        if (build_table(&clen, clens, 19) < 0) return -1;

        /* 解码 288+ 码长 */
        uint16_t lens[288 + 32];
        int n = hlit + hdist, idx = 0, prev = 0;
        while (idx < n) {
            int sym = decode_sym(&clen);
            if (sym < 0) return -1;
            if (sym < 16) {
                lens[idx++] = (uint16_t)sym;
                prev = sym;
            } else if (sym == 16) {
                if (idx == 0) return -1;
                int e = rd_bits(2);
                if (e < 0) return -1;
                for (int i = 0; i < e + 3 && idx < n; i++) lens[idx++] = (uint16_t)prev;
            } else if (sym == 17) {
                int e = rd_bits(3);
                if (e < 0) return -1;
                for (int i = 0; i < e + 3 && idx < n; i++) lens[idx++] = 0;
            } else {
                int e = rd_bits(7);
                if (e < 0) return -1;
                for (int i = 0; i < e + 11 && idx < n; i++) lens[idx++] = 0;
            }
        }
        if (build_table(&lit, lens, hlit) < 0) return -1;
        if (build_table(&dst, lens + hlit, hdist) < 0) return -1;
    } else {
        return -1;
    }

    /* 解码符号流 */
    for (;;) {
        int sym = decode_sym(&lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (emit((uint8_t)sym) < 0) return -1;
        } else if (sym == 256) {
            break;
        } else if (sym < 286) {
            int li = sym - 257;
            uint16_t len = len_base[li];
            int e = rd_bits(len_extra[li]);
            if (e < 0) return -1;
            len += (uint16_t)e;
            int dsym = decode_sym(&dst);
            if (dsym < 0 || dsym >= 30) return -1;
            uint16_t dist = dist_base[dsym];
            int de = rd_bits(dist_extra[dsym]);
            if (de < 0) return -1;
            dist += (uint16_t)de;
            if (copy_dist(dist, len) < 0) return -1;
        } else {
            return -1;
        }
    }
    return final;
}

int gunzip(const uint8_t *src, uint32_t srclen, uint8_t *dst, uint32_t dstcap) {
    in = src; inlen = srclen; inpos = 0;
    out = dst; outlen = 0; outcap = dstcap;
    bitbuf = 0; bitcnt = 0;

    /* gzip header: 1F 8B 08 FLG ... */
    if (srclen < 10 || src[0] != 0x1F || src[1] != 0x8B || src[2] != 8) return -1;
    uint8_t flg = src[3];
    inpos = 10;
    if (flg & 0x04) {           /* FEXTRA: 2 字节 XLEN + 数据 */
        int xlen = src[10] | (src[11] << 8);
        inpos += 2 + xlen;
    }
    if (flg & 0x08) {           /* FNAME: 0 结尾 */
        while (inpos < srclen && src[inpos]) inpos++;
        inpos++;
    }
    if (flg & 0x10) {           /* FCOMMENT */
        while (inpos < srclen && src[inpos]) inpos++;
        inpos++;
    }
    if (flg & 0x02) inpos += 2; /* FHCRC */
    if (inpos > srclen) return -1;

    do {
        int fin = inflate_block();
        if (fin < 0) return -1;
        if (fin) break;
    } while (1);

    return (int)outlen;
}

/* .kdrv 驱动模块: 纯函数库(rootfs.kdrv 依赖 gunzip), 无 init */
REGISTER_DRIVER("inflate", DRV_TYPE_FS, 0);
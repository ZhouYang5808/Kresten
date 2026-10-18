#ifndef _INFLATE_H
#define _INFLATE_H

#include <stdint.h>

/* 解压 gzip 数据(src..src+srclen)到 dst(容量 dstcap)。
 * 返回解压后字节数;失败返回 -1。 */
int gunzip(const uint8_t *src, uint32_t srclen, uint8_t *dst, uint32_t dstcap);

#endif
#ifndef _WINEFS_H
#define _WINEFS_H

/* wine 运行时 = 独立 tiny ISO (dist/wine-tiny.iso, 卷标 WINETINY),
 * 由 scripts/gen_tiny_iso.sh 生成 (~9MB: wine loader + x86_64-unix 库 +
 * ld-linux/libc)。不再解压发行版大镜像。
 * wine_import() 扫描 ATA 槽挂载该 ISO; 之后 LX 的 wine 装载与
 * open/stat 等通过 wine_iso_map 表 (plugins/runtime/wine_iso_map.h)
 * 把真实路径映射到 ISO 8.3 路径读取。 */

/* 扫描 0..3 槽挂载 wine-tiny.iso (volid WINETINY); 返回 0 成功 */
int wine_import(void);

/* tiny ISO 已挂载 */
int winefs_ready(void);

#endif
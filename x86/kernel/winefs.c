/* winefs: wine 运行时容器 (wine-tiny.iso, 卷标 WINETINY)
 * 不再解压发行版大镜像: wine_import() 只扫描 ATA 槽挂载 tiny ISO,
 * LX 侧经 wine_iso_map 路径映射 + iso_lookup/iso_read_at 读取。 */

#include "winefs.h"
#include <iso9660.h>
#include <stdio.h>
#include <string.h>

static int ready = 0;

int wine_import(void) {
    if (ready) return 0;
    if (iso_mount_volid("WINETINY") == 0) {
        ready = 1;
        printf("[WINEFS] wine-tiny.iso mounted\n");
        return 0;
    }
    puts("[WINEFS] no wine-tiny.iso (volid WINETINY) found - wine unavailable");
    return -1;
}

int winefs_ready(void) {
    return ready;
}

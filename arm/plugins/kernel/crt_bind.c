#include <crt.h>
#include <plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fs.h>
#include <env.h>
#include <rtc.h>
#include <editor.h>
#include <process.h>
#include <elf.h>

extern void _crt_base(void);

extern const uint16_t crt_compressed_start[];
extern const uint16_t crt_compressed_end[];

static CRTExport crt_table;
CRTExport *crt = &crt_table;

int plugin_crt_bind_init(void) {
    uint32_t base = (uint32_t)&_crt_base;
    uint32_t *dst = (uint32_t *)&crt_table;
    int count = crt_compressed_end - crt_compressed_start;
    for (int i = 0; i < count; i++)
        dst[i] = base + crt_compressed_start[i];
    return 0;
}

int plugin_crt_bind_cmd(char *args) {
    (void)args;
    int count = crt_compressed_end - crt_compressed_start;
    printf("CRT export table bound — %d functions\n", count);
    printf("Compressed: %u bytes, Uncompressed: %u bytes\n",
           (unsigned)(count * 2), (unsigned)(count * 4));
    return 0;
}

REGISTER_PLUGIN(crt_bind);

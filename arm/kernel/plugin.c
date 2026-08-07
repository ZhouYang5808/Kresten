#include <plugin.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fs.h>

int plugin_init_all(void) {
    Plugin *p = __plugins_start;
    int count = 0;
    while (p < __plugins_end) {
        if (p->name[0] && p->init) {
            printf("[PLUGIN] %s: %s\n", p->name, p->desc);
            p->init();
            count++;
        }
        p++;
    }
    return count;
}

Plugin *plugin_find(const char *name) {
    Plugin *p = __plugins_start;
    while (p < __plugins_end) {
        if (p->name[0] && strcasecmp(p->name, name) == 0) return p;
        p++;
    }
    return 0;
}

int plugin_dispatch(const char *cmd, const char *args) {
    Plugin *p = plugin_find(cmd);
    if (!p || !p->command) return -1;
    p->command((char *)args);
    return 0;
}

void plugin_list_all(void) {
    Plugin *p = __plugins_start;
    puts("  PLUGIN              DESC\n");
    puts("  ------------------  ------------------------------\n");
    while (p < __plugins_end) {
        if (p->name[0]) printf("  %-16s  %s\n", p->name, p->desc);
        p++;
    }
    putchar('\n');
}

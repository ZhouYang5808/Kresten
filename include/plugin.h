#ifndef _PLUGIN_H
#define _PLUGIN_H

#include <stddef.h>

typedef struct {
    char name[32];
    char desc[64];
    int (*init)(void);
    int (*command)(char *args);
} Plugin;

#define REGISTER_PLUGIN(name) \
    Plugin __plugin_##name \
    __attribute__((section(".plugins"), used, aligned(sizeof(void *)))) = \
        { #name, #name " plugin", plugin_##name##_init, plugin_##name##_cmd }

extern Plugin __plugins_start[];
extern Plugin __plugins_end[];

int plugin_init_all(void);
Plugin *plugin_find(const char *name);
int plugin_dispatch(const char *cmd, const char *args);
void plugin_list_all(void);

#endif

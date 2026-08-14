/* ===== kernel/drivers/driver.c: driver framework ===== */
#include <driver.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRIVERS 16

static Driver g_drivers[MAX_DRIVERS];
static int g_driver_count = 0;

int driver_register(Driver *d) {
    if (!d || !d->name[0] || g_driver_count >= MAX_DRIVERS)
        return -1;
    for (int i = 0; i < g_driver_count; i++) {
        if (strcmp(g_drivers[i].name, d->name) == 0)
            return -1;
    }
    g_drivers[g_driver_count++] = *d;
    return 0;
}

Driver *driver_find(const char *name) {
    for (int i = 0; i < g_driver_count; i++) {
        if (strcmp(g_drivers[i].name, name) == 0)
            return &g_drivers[i];
    }
    return 0;
}

void driver_list_all(void) {
    puts("\n  DRIVER   TYPE           STATUS\n");
    puts("  -------  -------------  --------\n");
    for (int i = 0; i < g_driver_count; i++) {
        const char *type_str = "?";
        switch (g_drivers[i].type) {
        case DRV_TYPE_FS:       type_str = "filesystem"; break;
        case DRV_TYPE_LCD:      type_str = "display";    break;
        case DRV_TYPE_KEYBOARD: type_str = "keyboard";   break;
        case DRV_TYPE_NET:      type_str = "network";    break;
        default: break;
        }
        printf("  %-7s  %-13s  %s\n", g_drivers[i].name, type_str,
               g_drivers[i].data ? "ready" : "pending");
    }
    putchar('\n');
}

int driver_init_all(void) {
    for (int i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].init) {
            int rc = g_drivers[i].init();
            g_drivers[i].data = rc == 0 ? (void *)1 : (void *)0;
        }
    }
    return 0;
}

/* ===== startup: register all drivers ===== */

int fs_driver_register(void);
int lcd_driver_register(void);
int kbd_driver_register(void);
int timer_driver_register(void);
int gfx_driver_register(void);
int keyboard_driver_register(void);
int mouse_driver_register(void);

int drivers_register_all(void) {
    if (fs_driver_register() != 0)
        return -1;
    if (lcd_driver_register() != 0)
        return -1;
    if (kbd_driver_register() != 0)
        return -1;
    if (timer_driver_register() != 0)
        return -1;
    if (gfx_driver_register() != 0)
        return -1;
    if (keyboard_driver_register() != 0)
        return -1;
    if (mouse_driver_register() != 0)
        return -1;
    return 0;
}

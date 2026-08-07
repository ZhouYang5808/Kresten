#ifndef _DRIVER_H
#define _DRIVER_H

#include <stdint.h>

typedef enum {
    DRV_TYPE_FS,
    DRV_TYPE_LCD,
    DRV_TYPE_VGA,
    DRV_TYPE_KEYBOARD,
    DRV_TYPE_NET,
    DRV_TYPE_SCHEDULER,
    DRV_TYPE_COUNT
} DriverType;

typedef struct Driver {
    char name[32];
    DriverType type;
    int (*init)(void);
    void *data;
} Driver;

int driver_register(Driver *d);
Driver *driver_find(const char *name);
void driver_list_all(void);
int driver_init_all(void);

/* LCD driver API (PL110 @ 0x10120000) */
int lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
int lcd_test(void);

/* VGA driver API (x86 text mode @ 0xB8000) */
int vga_init(void);
void vga_clear(void);
void vga_put_char(int x, int y, char c, uint8_t color);
void vga_putc(char c);
int vga_test(void);

/* Keyboard driver API (PL050 @ 0x10006000) */
int kbd_init(void);
int kbd_data_available(void);
int kbd_read_scancode(void);
int kbd_test(void);

/* Scheduler driver API (PIT IRQ0 preemptive / cooperative round-robin) */
#define SCHED_MODE_PREEMPT 0
#define SCHED_MODE_COOP    1
int sched_init(void);
int sched_create(const char *name, void (*entry)(void));
void sched_yield(void);
int sched_get_mode(void);
void sched_apply_registry(void);
extern volatile uint32_t sched_ticks;

#endif

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
    DRV_TYPE_RTC,
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
int driver_count(void);
Driver *driver_at(int i);
void driver_init_from(int from);

/* 可加载驱动(.kdrv = ELF64 ET_DYN): 每个驱动模块导出唯一的 kdrv_driver
 * 结构, 启动时由 drv_load_all() 从 ISO /drivers/ 加载并 driver_register。
 * 静态驱动(启动子系统必需, 如 vga/sched/keyboard/ata/iso9660)不导出此符号。 */
#define REGISTER_DRIVER(drv_name, drv_type, drv_init) \
    Driver kdrv_driver __attribute__((visibility("default"))) = \
        { drv_name, drv_type, drv_init, 0 }

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
int sched_set_mode(int mode);
void sched_apply_registry(void);
void sched_dump_state(void);
extern volatile uint32_t sched_ticks;

#endif

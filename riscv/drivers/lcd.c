/* ===== riscv/drivers/lcd.c (RISC-V): 虚拟 LCD — RAM 帧缓冲 =====
 * 可加载驱动模块 (.kdrv), 由 drv_load_all() 加载注册; virt 机器无显示
 * 设备: 帧缓冲 = link.ld .lcd_fb (0x80800000, 0x96000),
 * 16bpp RGB565, 640x480, RISC-V 原生帧缓冲 API。桌面/VM 直接绘制, 无回读。
 */
#include <driver.h>
#include <stdio.h>
#include <stdint.h>

#define LCD_WIDTH    640
#define LCD_HEIGHT   480

extern uint8_t _lcd_fb_start[];
#define lcd_fb ((uint8_t *)_lcd_fb_start)

static int lcd_enabled = 0;

int lcd_init(void) {
    lcd_clear(0x0000);
    lcd_enabled = 1;
    return 0;
}

void lcd_clear(uint16_t color) {
    uint16_t *p = (uint16_t *)lcd_fb;
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
        p[i] = color;
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;
    uint16_t *p = (uint16_t *)lcd_fb;
    for (int row = 0; row < h; row++) {
        uint16_t *line = p + (y + row) * LCD_WIDTH + x;
        for (int col = 0; col < w; col++)
            line[col] = color;
    }
}

int lcd_test(void) {
    lcd_init();
    lcd_clear(0x0000);
    lcd_fill_rect(0, 0, LCD_WIDTH / 3, LCD_HEIGHT / 3, 0xF800);
    lcd_fill_rect(LCD_WIDTH / 3, LCD_HEIGHT / 3, LCD_WIDTH / 3, LCD_HEIGHT / 3, 0x07E0);
    lcd_fill_rect(2 * LCD_WIDTH / 3, 2 * LCD_HEIGHT / 3, LCD_WIDTH / 3, LCD_HEIGHT / 3, 0x001F);
    printf("LCD test: %dx%d 16bpp, fb=0x%08X (RAM virtual, no hw)\n",
           LCD_WIDTH, LCD_HEIGHT, (unsigned)(uintptr_t)_lcd_fb_start);
    printf("  fb[0]=0x%04X (expect 0xF800)\n", (unsigned)((uint16_t *)lcd_fb)[0]);
    return 0;
}

/* .kdrv 驱动模块: 加载器(plugin.c drv_load_all)解析 kdrv_driver 注册 */
REGISTER_DRIVER("lcd", DRV_TYPE_LCD, lcd_init);
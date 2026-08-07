/* ===== kernel/drivers/lcd_drv.c: LCD driver (PL110 @ 0x10120000) ===== */
#include <driver.h>
#include <stdio.h>
#include <stdint.h>

#define LCD_BASE     0x10120000
#define LCD_TIMING0  0x00
#define LCD_TIMING1  0x04
#define LCD_TIMING2  0x08
#define LCD_TIMING3  0x0C
#define LCD_UPBASE   0x10
#define LCD_LPBASE   0x14
#define LCD_IMSC     0x18
#define LCD_CONTROL  0x1C
#define LCD_RIS      0x20
#define LCD_MIS      0x24
#define LCD_ICR      0x28

#define LCD_CR_EN    0x001
#define LCD_CR_BGR   0x100
#define LCD_CR_PWR   0x800
#define LCD_BPP_16   (4 << 1)

#define LCD_WIDTH    640
#define LCD_HEIGHT   480

extern uint8_t _lcd_fb_start[];
#define lcd_fb ((uint8_t *)_lcd_fb_start)

static uint32_t lcd_reg_read(uint32_t off) {
    return *(volatile uint32_t *)(LCD_BASE + off);
}

static void lcd_reg_write(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(LCD_BASE + off) = val;
}

int lcd_init(void) {
    uint32_t t0 = ((LCD_WIDTH / 4) - 4) & 0xFC;
    uint32_t t1 = (LCD_HEIGHT - 1) & 0x3FF;
    lcd_reg_write(LCD_TIMING0, t0);
    lcd_reg_write(LCD_TIMING1, t1);
    lcd_reg_write(LCD_TIMING2, 0x0);
    lcd_reg_write(LCD_TIMING3, 0x0);
    lcd_reg_write(LCD_UPBASE, (uint32_t)lcd_fb);
    lcd_reg_write(LCD_LPBASE, (uint32_t)lcd_fb);
    lcd_clear(0x0000);
    /* Versatile PL110 swaps LCDControl and LCDIMSC: writing 0x18 sets cr */
    lcd_reg_write(LCD_IMSC, LCD_CR_EN | LCD_BPP_16 | LCD_CR_PWR);
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
    uint32_t cr = lcd_reg_read(LCD_IMSC);
    uint32_t ris = lcd_reg_read(LCD_RIS);
    uint32_t upbase = lcd_reg_read(LCD_UPBASE);
    printf("LCD test: %dx%d 16bpp, fb=0x%08X\n", LCD_WIDTH, LCD_HEIGHT, (unsigned)upbase);
    printf("  cr=0x%03X (expect 0x809 EN|16bpp|PWR)\n", (unsigned)(cr & 0xFFF));
    printf("  RIS=0x%X, fb[0]=0x%04X (expect 0xF800)\n",
           (unsigned)ris, (unsigned)((uint16_t *)lcd_fb)[0]);
    if ((cr & 0xFFF) == (LCD_CR_EN | LCD_BPP_16 | LCD_CR_PWR))
        puts("  LCD status: OK\n");
    else
        puts("  LCD status: MISMATCH\n");
    return 0;
}

static Driver lcd_driver = {
    .name = "lcd",
    .type = DRV_TYPE_LCD,
    .init = lcd_init,
};

int lcd_driver_register(void) {
    return driver_register(&lcd_driver);
}

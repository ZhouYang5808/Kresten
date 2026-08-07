/* ===== kernel/drivers/gfx.c: VGA-style graphics on PL110 (versatilepb) =====
 * Logical 320x200 8-bit backbuffer with the 16-color VGA palette, auto-scaled
 * to fit the physical LCD panel: scale = largest integer factor that fits,
 * content centered with black letterbox bars (adaptive resolution).
 */
#include <driver.h>
#include <gfx.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "font8x16.h"

#define LCD_BASE    0x10120000
#define LCD_IMSC    0x18
#define LCD_CR_EN   0x001
#define LCD_CR_BGR  0x100
#define LCD_CR_PWR  0x800
#define LCD_BPP_16  (4 << 1)

/* VersatilePB PL110 panel: 640x480.  Changing these adapts the whole
 * scaling/letterbox pipeline (simulates different panel sizes). */
#define LCD_WIDTH   640
#define LCD_HEIGHT  480

extern uint8_t _lcd_fb_start[];

static uint8_t fb[GFX_W * GFX_H];

/* Layout computed from panel size: integer scale + centered offset */
static int scl = 1;
static int off_x = 0;
static int off_y = 0;

/* PL110 16bpp reads framebuffer as BGR565, so palette entries are R/B-swapped */
static const uint16_t vga_palette[16] = {
    0x0000, 0xA800, 0x02A0, 0xAAA0, 0x0015, 0xA815, 0x0554, 0xBDD7,
    0x8410, 0xAC10, 0x06B0, 0xAEB0, 0x001F, 0xA81F, 0x055E, 0xFFFF,
};

static void lcd_reg_write(uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(LCD_BASE + off) = val;
}

void gfx_get_screen_size(int *w, int *h) {
    if (w) *w = LCD_WIDTH;
    if (h) *h = LCD_HEIGHT;
}

/* Auto-fit: largest integer scale that fits the panel, centered. */
static void gfx_compute_layout(void) {
    int sx = LCD_WIDTH / GFX_W;
    int sy = LCD_HEIGHT / GFX_H;
    scl = (sx < sy) ? sx : sy;
    if (scl < 1) scl = 1;
    off_x = (LCD_WIDTH - GFX_W * scl) / 2;
    off_y = (LCD_HEIGHT - GFX_H * scl) / 2;
}

int gfx_enter(void) {
    /* Versatile PL110 quirk: programming the LCDIMSC offset enables LCD */
    lcd_reg_write(LCD_IMSC, LCD_CR_EN | LCD_BPP_16 | LCD_CR_PWR);
    gfx_compute_layout();
    printf("[GFX] panel %dx%d, desktop %dx%d, scale %dx, offset (%d,%d)\n",
           LCD_WIDTH, LCD_HEIGHT, GFX_W, GFX_H, scl, off_x, off_y);
    memset(fb, 0, sizeof(fb));
    memset((uint8_t *)_lcd_fb_start, 0, LCD_WIDTH * LCD_HEIGHT * 2);
    return 0;
}

void gfx_leave(void) {
    lcd_reg_write(LCD_IMSC, 0);
}

int gfx_active(void) {
    return 1;
}

const uint8_t *gfx_get_font(void) {
    return vga_font8x16;
}

void gfx_clear(uint8_t color) {
    memset(fb, color, sizeof(fb));
}

void gfx_blit(const uint8_t *src) {
    uint16_t *dst = (uint16_t *)_lcd_fb_start;
    /* letterbox: clear full panel first */
    memset((uint8_t *)_lcd_fb_start, 0, LCD_WIDTH * LCD_HEIGHT * 2);
    for (int y = 0; y < GFX_H; y++) {
        const uint8_t *row = src + y * GFX_W;
        for (int sy = 0; sy < scl; sy++) {
            uint16_t *line = dst + (off_y + y * scl + sy) * LCD_WIDTH + off_x;
            for (int x = 0; x < GFX_W; x++) {
                uint16_t c = vga_palette[row[x] & 0x0F];
                for (int sx = 0; sx < scl; sx++)
                    line[x * scl + sx] = c;
            }
        }
    }
}

void gfx_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= GFX_W || y < 0 || y >= GFX_H)
        return;
    fb[y * GFX_W + x] = color;
    uint16_t c = vga_palette[color & 0x0F];
    uint16_t *p = (uint16_t *)_lcd_fb_start + (off_y + y * scl) * LCD_WIDTH + off_x + x * scl;
    for (int sy = 0; sy < scl; sy++)
        for (int sx = 0; sx < scl; sx++)
            p[sy * LCD_WIDTH + sx] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= GFX_W || y >= GFX_H || w <= 0 || h <= 0)
        return;
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    for (int row = 0; row < h; row++)
        memset(fb + (y + row) * GFX_W + x, color, (size_t)w);
}

void gfx_hline(int x, int y, int w, uint8_t color) {
    gfx_fill_rect(x, y, w, 1, color);
}

void gfx_vline(int x, int y, int h, uint8_t color) {
    gfx_fill_rect(x, y, 1, h, color);
}

void gfx_draw_char(int x, int y, unsigned char ch, uint8_t fg) {
    const uint8_t *glyph = vga_font8x16 + ch * 32;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col))
                gfx_pixel(x + col, y + row, fg);
        }
    }
}

void gfx_draw_string(int x, int y, const char *s, uint8_t fg) {
    while (*s) {
        gfx_draw_char(x, y, (unsigned char)*s, fg);
        x += 8;
        s++;
    }
}

int gfx_test(void) {
    gfx_enter();
    return 0;
}

static Driver gfx_driver = {
    .name = "gfx",
    .type = DRV_TYPE_VGA,
    .init = gfx_enter,
};

int gfx_driver_register(void) {
    return driver_register(&gfx_driver);
}

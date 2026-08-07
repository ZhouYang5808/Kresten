/* ===== kernel/drivers/gfx.c: VGA-style graphics on PL110 (versatilepb) =====
 * Logical 320x200 8-bit backbuffer with a 256-color custom palette (same
 * blue-white theme as the x86 build), auto-scaled to fit the physical LCD
 * panel: scale = largest integer factor that fits, content centered with
 * black letterbox bars (adaptive resolution).
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

/* 256-color palette in PL110 16bpp BGR565 (R/B-swapped) layout, built at
 * gfx_enter() from the same 6-bit DAC values as the x86 custom palette. */
static uint16_t vga_palette[256];

static uint16_t bgr565(uint8_t r6, uint8_t g6, uint8_t b6) {
    return (uint16_t)(((uint16_t)(b6 * 31 / 63) << 11) |
                      ((uint16_t)g6 << 5) |
                      ((uint16_t)(r6 * 31 / 63)));
}

static void gfx_build_palette(void) {
    static const uint8_t p16[48] = {
        0, 0, 0,       0, 0, 42,     0, 42, 0,     0, 42, 42,
        42, 0, 0,      42, 0, 42,    42, 21, 0,    42, 42, 42,
        21, 21, 21,    21, 21, 63,   21, 63, 21,   21, 63, 63,
        63, 21, 21,    63, 21, 63,   63, 63, 21,   63, 63, 63
    };
    for (int i = 0; i < 16; i++)
        vga_palette[i] = bgr565(p16[i * 3], p16[i * 3 + 1], p16[i * 3 + 2]);
    /* 16-31: desktop gradient (white -> light blue) */
    for (int i = 0; i < 16; i++)
        vga_palette[16 + i] = bgr565(63 - i * 22 / 15, 63 - i * 11 / 15, 63);
    /* 32-47: white surface gradient (panel/card base) */
    for (int i = 0; i < 16; i++)
        vga_palette[32 + i] = bgr565(63 - i * 9 / 15, 63 - i * 10 / 15, 63);
    /* 48-63: light blue surface gradient (hover/active) */
    for (int i = 0; i < 16; i++)
        vga_palette[48 + i] = bgr565(46 - i * 14 / 15, 57 - i * 8 / 15, 63);
    /* 64-79: accent blue -> cyan */
    for (int i = 0; i < 16; i++)
        vga_palette[64 + i] = bgr565(14 + i * 9 / 15, 40 + i * 22 / 15, 63);
    /* 80-95: purple -> pink */
    for (int i = 0; i < 16; i++)
        vga_palette[80 + i] = bgr565(38 + i * 25 / 15, 23 + i * 9 / 15,
                                     63 - i * 5 / 15);
    /* 96-111: teal */
    for (int i = 0; i < 16; i++)
        vga_palette[96 + i] = bgr565(16 + i * 9 / 15, 54 + i * 8 / 15,
                                     40 + i * 22 / 15);
    /* 112-127: orange -> yellow */
    for (int i = 0; i < 16; i++)
        vga_palette[112 + i] = bgr565(63, 26 + i * 32 / 15, 4 + i * 9 / 15);
    /* 128-143: green */
    for (int i = 0; i < 16; i++)
        vga_palette[128 + i] = bgr565(16 + i * 11 / 15, 47 + i * 15 / 15,
                                     21 + i * 13 / 15);
    /* 144-159: red */
    for (int i = 0; i < 16; i++)
        vga_palette[144 + i] = bgr565(60, 7 + i * 11 / 15, 7 + i * 11 / 15);
    /* 160-255: 96-step gray ramp (8 -> 63) */
    for (int i = 0; i < 96; i++) {
        uint8_t gv = (uint8_t)(8 + i * 55 / 95);
        vga_palette[160 + i] = bgr565(gv, gv, gv);
    }
}

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
    gfx_build_palette();
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
                uint16_t c = vga_palette[row[x]];
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
    uint16_t c = vga_palette[color];
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

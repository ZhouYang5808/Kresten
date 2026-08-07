#ifndef _GFX_H
#define _GFX_H

#include <stdint.h>

/* Logical desktop size: 320x200 by default; gfx_enter() raises it to the
 * physical panel size (native resolution) when a VBE/SVGA mode is active. */
extern int gfx_w;
extern int gfx_h;
#define GFX_W (gfx_w)
#define GFX_H (gfx_h)

/* Largest possible panel, used for compile-time buffers (desktop back-buffer). */
#define GFX_MAX_W 1280
#define GFX_MAX_H 768

/* Physical screen (panel) size in pixels; desktop logical size is GFX_W x GFX_H */
void gfx_get_screen_size(int *w, int *h);
int gfx_enter(void);
void gfx_leave(void);
int gfx_active(void);
const uint8_t *gfx_get_font(void);
void gfx_clear(uint8_t color);
void gfx_blit(const uint8_t *fb);
void gfx_pixel(int x, int y, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_draw_char(int x, int y, unsigned char ch, uint8_t fg);
void gfx_draw_string(int x, int y, const char *s, uint8_t fg);
int gfx_test(void);

#endif

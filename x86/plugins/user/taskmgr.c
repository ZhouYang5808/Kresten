#include <plugin.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <gfx.h>
#include <keyboard.h>

/* ---- back buffer ---- */
#ifndef GFX_MAX_W
#define GFX_MAX_W GFX_W
#define GFX_MAX_H GFX_H
#endif
static uint8_t fb[GFX_MAX_W * GFX_MAX_H];
static const uint8_t *font;

/* AA text blending: 5-5-5 RGB -> nearest palette index lookup */
static const uint8_t *pal_rgb;
static uint8_t mix_lut[32768];
static int mix_lut_ok;

static void build_mix_lut(void) {
    if (mix_lut_ok)
        return;
    pal_rgb = gfx_get_palette_rgb();
    for (int r = 0; r < 32; r++)
        for (int g = 0; g < 32; g++)
            for (int b = 0; b < 32; b++) {
                int best = 0, bestd = 1 << 30;
                for (int i = 0; i < 256; i++) {
                    int dr = (int)pal_rgb[i * 3] - (r * 8 + 4);
                    int dg = (int)pal_rgb[i * 3 + 1] - (g * 8 + 4);
                    int db = (int)pal_rgb[i * 3 + 2] - (b * 8 + 4);
                    int d = dr * dr + dg * dg + db * db;
                    if (d < bestd) { bestd = d; best = i; }
                }
                mix_lut[(r << 10) | (g << 5) | b] = (uint8_t)best;
            }
    mix_lut_ok = 1;
}

static void fb_px(int x, int y, uint8_t c) {
    if (x < 0 || x >= GFX_W || y < 0 || y >= GFX_H)
        return;
    fb[(uint32_t)y * GFX_W + (uint32_t)x] = c;
}

static void fb_aa_px(int x, int y, uint8_t fg, int a) {
    if (x < 0 || x >= GFX_W || y < 0 || y >= GFX_H || a <= 0)
        return;
    if (a >= 15) {
        fb_px(x, y, fg);
        return;
    }
    uint8_t bg = fb[(uint32_t)y * GFX_W + (uint32_t)x];
    const uint8_t *fr = pal_rgb + fg * 3;
    const uint8_t *br = pal_rgb + bg * 3;
    int r = (fr[0] * a + br[0] * (15 - a) + 7) / 15;
    int g = (fr[1] * a + br[1] * (15 - a) + 7) / 15;
    int b = (fr[2] * a + br[2] * (15 - a) + 7) / 15;
    fb_px(x, y, mix_lut[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)]);
}

static void fb_text(int x, int y, const char *s, uint8_t fg) {
    if (!s)
        return;
    build_mix_lut();
    while (*s) {
        const uint8_t *glyph = font + (uint32_t)(uint8_t)*s * 64;
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 8; col++) {
                int a = (glyph[row * 4 + (col >> 1)] >> ((col & 1) ? 0 : 4)) & 0x0F;
                if (a)
                    fb_aa_px(x + col, y + row, fg, a);
            }
        x += 8;
        s++;
    }
}

static void fb_text_center(int cx, int y, const char *s, uint8_t fg) {
    fb_text(cx - (int)(8 * strlen(s)) / 2, y, s, fg);
}

static void fb_uint(int x, int y, int v, uint8_t fg) {
    char buf[12];
    int n = 0;
    if (v <= 0) {
        buf[n++] = '0';
    } else {
        while (v > 0) {
            buf[n++] = (char)('0' + v % 10);
            v /= 10;
        }
        for (int i = 0; i < n / 2; i++) {
            char t = buf[i];
            buf[i] = buf[n - 1 - i];
            buf[n - 1 - i] = t;
        }
    }
    buf[n] = '\0';
    fb_text(x, y, buf, fg);
}

/* ---- shared data helpers ---- */

static const char *state_name(ProcState st) {
    switch (st) {
        case PROC_READY:   return "READY";
        case PROC_RUNNING: return "RUNNING";
        case PROC_WAITING: return "WAITING";
        case PROC_EXITED:  return "EXITED";
    }
    return "UNKNOWN";
}

/* ---- console mode ---- */

static void print_process_list(void) {
    int count = proc_get_count();
    Process *procs = proc_get_all();
    printf("  PID  NAME                 STATE\n");
    printf("  ----  ------------------  -------\n");
    for (int i = 0; i < count; i++)
        printf("  %3d  %-18s  %s\n", i, procs[i].name, state_name(procs[i].state));
}

static void print_system_info(void) {
    printf("\n=== Kresten OS Task Manager ===\n");
    printf("Processes: %d / %d\n", proc_get_count(), MAX_PROC);
    printf("\n");
}

/* ---- graphical mode ---- */

static void graphical_mode(void) {
    gfx_enter();
    font = gfx_get_font_aa();
    build_mix_lut();
    keyboard_flush();

    while (1) {
        int sc;
        while ((sc = keyboard_poll_scancode()) >= 0) {
            if (sc == 0x01) {  /* ESC */
                keyboard_flush();
                gfx_leave();
                return;
            }
        }

        int vw = GFX_W, vh = GFX_H;
        for (int i = 0; i < vw * vh; i++) fb[i] = 0;

        /* header */
        fb_text_center(vw / 2, 20, "Task Manager", 0x0F);
        fb_text(vw / 2 - 200, 60, "PID", 205);
        fb_text(vw / 2 - 160, 60, "NAME", 205);
        fb_text(vw / 2 + 80, 60, "STATE", 205);

        int count = proc_get_count();
        Process *procs = proc_get_all();
        int y = 90;
        for (int i = 0; i < count && y < vh - 50; i++) {
            fb_uint(vw / 2 - 200, y, i, 0x0F);
            fb_text(vw / 2 - 160, y, procs[i].name, 0x0F);
            fb_text(vw / 2 + 80, y, state_name(procs[i].state), 0x0F);
            y += 20;
        }

        fb_text_center(vw / 2, vh - 30, "Press ESC to exit", 205);

        gfx_blit(fb);
        proc_yield();
    }
}

int plugin_taskmgr_init(void) {
    printf("[TASKMGR] init\n");
    return 0;
}

int plugin_taskmgr_cmd(char *args) {
    if (args && strcmp(args, "--graphical") == 0) {
        graphical_mode();
        return 0;
    }
    print_system_info();
    print_process_list();
    return 0;
}

REGISTER_PLUGIN(taskmgr);

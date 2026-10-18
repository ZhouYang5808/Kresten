/* ===== shared/plugins/desktop.c: tabbed fullscreen desktop =====
 * Browser-style tab bar on top; every app lives in its own fullscreen
 * tab.  No windows, no taskbar.  When the last tab is closed the
 * App Manager opens automatically (acts as the launcher / new-tab page).
 * Controls: mouse (click) or keyboard (arrows/Enter/Esc,
 * Alt+F4 closes the active tab, Alt+F4 on App Manager quits the desktop).
 * Platform back-ends: gfx.h (gfx_blit), mouse.h, keyboard.h, sched_get_ticks(). */
#include <stdio.h>
#include <string.h>
#include <gfx.h>
#include <keyboard.h>
#include <mouse.h>
#include <plugin.h>
#include <fs.h>
#include <process.h>
#include <guard.h>

extern uint32_t sched_get_ticks(void);
void sys_poweroff(void);

/* ---- soft blue-white theme palette (indexes into gfx.c custom palette) ---- */
#define C_DESK_TOP   16   /* desktop gradient: soft blue-white -> muted blue */
#define C_DESK_BOT   31
#define C_SURFACE    32   /* soft blue-white panels, app content */
#define C_SURFACE_HI 40
#define C_BRIGHT     52   /* soft light-blue hover / active surfaces */
#define C_TAB_BG     32   /* soft blue-white tab bar */
#define C_TAB_HOVER  52
#define C_TAB_ACTIVE 48
#define C_ACCENT     70   /* steady accent blue */
#define C_ACCENT_DIM 66
#define C_PURPLE     84
#define C_TEAL       104
#define C_ORANGE     118
#define C_GREEN      134
#define C_RED        148
#define C_GRAY_HI    190  /* ink text: deep blue-black */
#define C_GRAY_MID   210
#define C_GRAY_LOW   225
#define C_GRAY_DARK  205  /* blue-gray borders / separators */
#define C_BLACK      0
#define C_WHITE      15

/* ---- back buffer (avoids flicker) ---- */
#ifndef GFX_MAX_W
#define GFX_MAX_W GFX_W
#define GFX_MAX_H GFX_H
#endif
static uint8_t fb[GFX_MAX_W * GFX_MAX_H];
static const uint8_t *font;
static int dirty = 1;

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

/* draw one AA pixel: fg over current fb background, alpha 0..15 */
static void fb_px(int x, int y, uint8_t c);
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

static void fb_px(int x, int y, uint8_t c) {
    if (x < 0 || x >= GFX_W || y < 0 || y >= GFX_H)
        return;
    fb[(uint32_t)y * GFX_W + (uint32_t)x] = c;
}

static void fb_rect(int x, int y, int w, int h, uint8_t c) {
    int x2 = x + w, y2 = y + h;
    if (x2 < 0 || y2 < 0 || x >= GFX_W || y >= GFX_H || w <= 0 || h <= 0)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > GFX_W) x2 = GFX_W;
    if (y2 > GFX_H) y2 = GFX_H;
    for (int yy = y; yy < y2; yy++)
        for (int xx = x; xx < x2; xx++)
            fb[(uint32_t)yy * GFX_W + (uint32_t)xx] = c;
}

static void fb_hline(int x, int y, int w, uint8_t c) {
    fb_rect(x, y, w, 1, c);
}

static void fb_vline(int x, int y, int h, uint8_t c) {
    fb_rect(x, y, 1, h, c);
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

/* 2x scaled text (8x16 -> 16x32), for headings */
static void fb_text2x(int x, int y, const char *s, uint8_t fg) {
    if (!s)
        return;
    build_mix_lut();
    while (*s) {
        const uint8_t *glyph = font + (uint32_t)(uint8_t)*s * 64;
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 8; col++) {
                int a = (glyph[row * 4 + (col >> 1)] >> ((col & 1) ? 0 : 4)) & 0x0F;
                if (!a)
                    continue;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++)
                        fb_aa_px(x + col * 2 + dx, y + row * 2 + dy, fg, a);
            }
        x += 16;
        s++;
    }
}

static void fb_text2x_center(int cx, int y, const char *s, uint8_t fg) {
    fb_text2x(cx - (int)(8 * strlen(s)), y, s, fg);
}

/* rounded rectangle (pixel-circle corners) */
static void fb_rrect(int x, int y, int w, int h, int r, uint8_t c) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    fb_rect(x + r, y, w - 2 * r, h, c);
    fb_rect(x, y + r, w, h - 2 * r, c);
    for (int dy = 0; dy < r; dy++)
        for (int dx = 0; dx < r; dx++) {
            int ox = r - dx, oy = r - dy;
            if (ox * ox + oy * oy <= r * r) {
                fb_px(x + dx, y + dy, c);
                fb_px(x + w - 1 - dx, y + dy, c);
                fb_px(x + dx, y + h - 1 - dy, c);
                fb_px(x + w - 1 - dx, y + h - 1 - dy, c);
            }
        }
}

/* rounded rect outline (border drawn around inner fill) */
static void fb_rrect_outline(int x, int y, int w, int h, int r,
                             uint8_t border, uint8_t inner) {
    fb_rrect(x, y, w, h, r, border);
    fb_rrect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, inner);
}

/* top-rounded rect (Chrome-style tab: rounded top, square bottom) */
static void fb_top_rrect(int x, int y, int w, int h, int r, uint8_t c) {
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r > h) r = h;
    fb_rect(x + r, y, w - 2 * r, h, c);
    fb_rect(x, y + r, w, h - r, c);
    for (int dy = 0; dy < r; dy++)
        for (int dx = 0; dx < r; dx++) {
            int ox = r - dx, oy = r - dy;
            if (ox * ox + oy * oy <= r * r) {
                fb_px(x + dx, y + dy, c);
                fb_px(x + w - 1 - dx, y + dy, c);
            }
        }
}

/* filled circle */
static void fb_circle(int cx, int cy, int r, uint8_t c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r)
                fb_px(cx + dx, cy + dy, c);
}

/* mouse position (used by hover states; updated in the main loop) */
static int cur_x = 160, cur_y = 100;

/* ---- icon pixel art (shared by App Manager and apps) ---- */
enum {
    ACT_SHELL, ACT_FMGR, ACT_MYCOMP, ACT_RECYCLE
};

/* ---- modern pixel icons, 24x24 ---- */
static void draw_icon_art(int x, int y, int kind) {
    switch (kind) {
    case ACT_SHELL: /* terminal */
        fb_rrect(x, y, 24, 24, 4, C_GRAY_MID);
        fb_rrect(x + 2, y + 3, 20, 18, 3, C_BLACK);
        fb_text(x + 5, y + 6, ">", C_TEAL);
        fb_hline(x + 13, y + 7, 4, C_TEAL);
        fb_hline(x + 5, y + 17, 14, C_TEAL);
        break;
    case ACT_FMGR: /* folder */
        fb_rect(x + 3, y + 5, 10, 4, C_ORANGE);
        fb_rect(x + 3, y + 7, 10, 3, C_GRAY_HI);
        fb_rrect(x + 2, y + 6, 20, 16, 3, C_ORANGE);
        fb_rrect(x + 4, y + 10, 16, 10, 2, C_GRAY_HI);
        fb_rect(x + 4, y + 12, 16, 2, C_ORANGE);
        break;
    case ACT_MYCOMP: /* monitor */
        fb_rrect(x + 2, y + 1, 20, 15, 3, C_GRAY_DARK);
        fb_rect(x + 4, y + 3, 16, 11, C_ACCENT_DIM);
        fb_hline(x + 6, y + 5, 7, C_ACCENT);
        fb_rect(x + 10, y + 16, 4, 3, C_GRAY_MID);
        fb_rect(x + 6, y + 21, 12, 2, C_GRAY_MID);
        break;
    case ACT_RECYCLE: /* recycle bin */
        fb_rect(x + 6, y + 2, 12, 4, C_TEAL);
        fb_rect(x + 6, y + 3, 12, 2, C_GRAY_HI);
        fb_rrect(x + 3, y + 6, 18, 16, 3, C_TEAL);
        fb_vline(x + 9, y + 8, 12, C_GREEN);
        fb_vline(x + 14, y + 8, 12, C_GREEN);
        fb_hline(x + 5, y + 19, 14, C_GREEN);
        break;
    default:
        break;
    }
}

/* ---- apps ---- */
enum {
    APP_MANAGER, APP_SHELL, APP_FMGR, APP_MYCOMP, APP_RECYCLE
};

static const char *app_titles[] = {
    "App Manager", "MS-DOS Prompt", "File Manager",
    "My Computer", "Recycle Bin"
};

/* launchable apps shown in the App Manager */
static const int app_kinds[] = { APP_SHELL, APP_FMGR, APP_MYCOMP, APP_RECYCLE };
#define APP_COUNT ((int)(sizeof(app_kinds) / sizeof(app_kinds[0])))

static const char *app_desc[] = {
    "Fullscreen console with the Kresten command line",
    "Browse files and directories on the C: drive",
    "Drive information and disk usage",
    "Deleted files live here",
};

/* ---- tabs ---- */
#define MAX_TABS 8
#define TAB_BAR_H 30

typedef struct {
    int kind;
} Tab;

static Tab tabs[MAX_TABS];
static int tab_count = 0;
static int active = -1;

static int tab_w(int idx) {
    int len = (int)strlen(app_titles[tabs[idx].kind]);
    int w = 8 * len + 34;
    if (w < 56) w = 56;
    if (w > 170) w = 170;
    return w;
}

static int tab_x(int idx) {
    int x = 2;
    for (int i = 0; i < idx; i++)
        x += tab_w(i) + 2;
    return x;
}

/* ---- file manager state ---- */
static char fm_entries[16][32];
static int fm_count = 0;

static void fm_collect(const FileEntry *e, void *arg) {
    (void)arg;
    if (fm_count >= 16)
        return;
    const char *name = e->name;
    const char *slash = strrchr(name, '\\');
    if (slash && slash[1])
        name = slash + 1;
    strncpy(fm_entries[fm_count], name, 31);
    fm_entries[fm_count][31] = '\0';
    if (e->type == FS_TYPE_DIR) {
        int len = (int)strlen(fm_entries[fm_count]);
        if (len < 31)
            fm_entries[fm_count][len] = '\\';
    }
    fm_count++;
}

static void app_init(int kind) {
    if (kind == APP_FMGR) {
        fm_count = 0;
        fs_list_dir("C:\\", fm_collect, NULL);
    }
}

static void tab_open(int kind) {
    for (int i = 0; i < tab_count; i++)
        if (tabs[i].kind == kind) {
            active = i;
            dirty = 1;
            return;
        }
    if (tab_count >= MAX_TABS)
        return;
    tabs[tab_count].kind = kind;
    tab_count++;
    active = tab_count - 1;
    app_init(kind);
    dirty = 1;
}

static void tab_close(int idx) {
    for (int i = idx; i < tab_count - 1; i++)
        tabs[i] = tabs[i + 1];
    tab_count--;
    if (tab_count == 0) {
        active = -1;
        tab_open(APP_MANAGER);
        return;
    }
    if (active > idx)
        active--;
    if (active >= tab_count)
        active = tab_count - 1;
    dirty = 1;
}

/* ---- shell: fullscreen console session ---- */
static void run_shell(void) {
    gfx_leave();
    plugin_dispatch("shell", NULL);
    gfx_enter();
    font = gfx_get_font_aa();
    while (keyboard_poll_scancode() >= 0) /* flush */
        ;
    dirty = 1;
}

/* ---- App Manager (launcher / new-tab page, list style) ---- */
static int mng_sel = 0;

/* compact mode on small (320x200) screens: shorter rows, no description */
#define MNG_ROW_W   ((GFX_W) < 500 ? (GFX_W) - 40 : 460)
#define MNG_ROW_H   ((GFX_H) < 300 ? 30 : 56)
#define MNG_ROW_GAP ((GFX_H) < 300 ? 6 : 10)

static void mng_geom(int *x0, int *y0) {
    *x0 = (GFX_W - MNG_ROW_W) / 2;
    *y0 = TAB_BAR_H + 96;
}

static int mng_row_y(int i) {
    int x0, y0;
    mng_geom(&x0, &y0);
    return y0 + i * (MNG_ROW_H + MNG_ROW_GAP);
}

/* visible row count (small screens scroll the list) */
static int mng_vis_rows(void) {
    int x0, y0;
    mng_geom(&x0, &y0);
    int n = (GFX_H - y0 - 6) / (MNG_ROW_H + MNG_ROW_GAP);
    if (n < 1) n = 1;
    if (n > APP_COUNT) n = APP_COUNT;
    return n;
}

static int mng_scroll(void) {
    int n = mng_vis_rows();
    if (APP_COUNT <= n)
        return 0;
    int s = mng_sel - (n - 1) / 2;
    if (s < 0) s = 0;
    if (s > APP_COUNT - n) s = APP_COUNT - n;
    return s;
}

static void draw_bg_gradient(void) {
    int area_h = GFX_H - TAB_BAR_H;
    for (int i = 0; i < 16; i++) {
        int y0 = TAB_BAR_H + i * area_h / 16;
        int y1 = TAB_BAR_H + (i + 1) * area_h / 16;
        fb_rect(0, y0, GFX_W, y1 - y0, C_DESK_TOP + i);
    }
}

static int mng_hover(void) {
    if (cur_y < TAB_BAR_H)
        return -1;
    int x0, y0;
    mng_geom(&x0, &y0);
    int s = mng_scroll(), n = mng_vis_rows();
    for (int k = 0; k < n; k++) {
        int y = mng_row_y(k);
        if (cur_x >= x0 && cur_x < x0 + MNG_ROW_W &&
            cur_y >= y && cur_y < y + MNG_ROW_H)
            return s + k;
    }
    return -1;
}

static void draw_app_manager(void) {
    int x0, y0;
    mng_geom(&x0, &y0);
    draw_bg_gradient();
    /* heading */
    fb_text2x_center(GFX_W / 2, TAB_BAR_H + 34, "App Manager", C_GRAY_HI);
    fb_rect(GFX_W / 2 - 90, TAB_BAR_H + 70, 180, 3, C_ACCENT);
    /* app list rows (scrollable on small screens) */
    int hov = mng_hover();
    int compact = MNG_ROW_H == 30;
    int s = mng_scroll(), n = mng_vis_rows();
    for (int k = 0; k < n; k++) {
        int i = s + k;
        int y = mng_row_y(k);
        int lit = (i == mng_sel) || (i == hov);
        uint8_t fill = lit ? C_BRIGHT : C_SURFACE_HI;
        fb_rrect_outline(x0, y, MNG_ROW_W, MNG_ROW_H, 8, C_GRAY_DARK, fill);
        if (lit)
            fb_rect(x0, y, 6, MNG_ROW_H, C_ACCENT);
        draw_icon_art(x0 + 12, compact ? y + 3 : y + 16, i);
        fb_text(x0 + 46, compact ? y + 7 : y + 10, app_titles[app_kinds[i]], C_GRAY_HI);
        if (!compact) {
            fb_text(x0 + 56, y + 32, app_desc[i], C_GRAY_LOW);
            fb_text(x0 + MNG_ROW_W - 24, y + 20, ">", lit ? C_ACCENT : C_GRAY_MID);
        }
    }
    /* footer hint (skip on compact screens) */
    if (!compact)
        fb_text_center(GFX_W / 2, GFX_H - 34,
                       "Up/Down to select, Enter to open  |  Alt+F4 closes tab  |  + opens App Manager",
                       C_GRAY_LOW);
}

static void manager_click(int mx, int my) {
    int x0, y0;
    mng_geom(&x0, &y0);
    int s = mng_scroll(), n = mng_vis_rows();
    for (int k = 0; k < n; k++) {
        int y = mng_row_y(k);
        if (mx >= x0 && mx < x0 + MNG_ROW_W &&
            my >= y && my < y + MNG_ROW_H) {
            mng_sel = s + k;
            tab_open(app_kinds[s + k]);
            return;
        }
    }
}

static void manager_key(int sc) {
    if (sc == 0x48 || sc == 0x50) { /* up / down */
        mng_sel = (mng_sel + (sc == 0x50 ? 1 : APP_COUNT - 1)) % APP_COUNT;
        dirty = 1;
    } else if (sc == 0x1C) { /* enter */
        tab_open(app_kinds[mng_sel]);
    }
}

/* ---- apps ---- */
static void app_header(const char *title) {
    fb_rect(0, TAB_BAR_H, GFX_W, 30, C_SURFACE);
    fb_rect(14, TAB_BAR_H + 8, 4, 14, C_ACCENT);
    fb_text(26, TAB_BAR_H + 7, title, C_GRAY_HI);
    fb_hline(0, TAB_BAR_H + 29, GFX_W, C_GRAY_DARK);
}

static void shell_draw(void) {
    int cx = GFX_W / 2;
    int cy = TAB_BAR_H + (GFX_H - TAB_BAR_H) / 2;
    fb_rect(0, TAB_BAR_H, GFX_W, GFX_H - TAB_BAR_H, C_BLACK);
    /* terminal window */
    int fx = cx - 130, fy = cy - 140, fw = 260, fh = 200;
    fb_rrect(fx, fy, fw, fh, 12, C_SURFACE_HI);
    fb_rect(fx, fy + 20, fw, fh - 20, C_BLACK);
    fb_circle(fx + 28, fy + 10, 4, C_RED);
    fb_circle(fx + 46, fy + 10, 4, C_ORANGE);
    fb_circle(fx + 64, fy + 10, 4, C_GREEN);
    fb_text(fx + 24, fy + 34, "> kresten shell", C_TEAL);
    fb_text(fx + 24, fy + 56, "click to launch the", C_GRAY_MID);
    fb_text(fx + 24, fy + 76, "fullscreen console", C_GRAY_MID);
    fb_hline(fx + 24, fy + 100, 60, C_TEAL);
    /* heading */
    fb_text2x_center(cx, fy + fh + 18, "MS-DOS Prompt", C_GRAY_HI);
    fb_text_center(cx, fy + fh + 62, "Click anywhere or press Enter to enter the console",
                   C_GRAY_MID);
}

static void shell_click(int mx, int my) {
    (void)mx;
    (void)my;
    run_shell();
}

static void shell_key(int sc) {
    if (sc == 0x1C)
        run_shell();
}

static void fmgr_draw(void) {
    app_header("File Manager");
    int base = TAB_BAR_H + 30;
    fb_rect(0, base, GFX_W, GFX_H - base, C_SURFACE);
    fb_text(14, base + 8, "C:\\", C_ACCENT);
    fb_hline(14, base + 28, GFX_W - 28, C_GRAY_DARK);
    if (fm_count == 0) {
        fb_text(14, base + 40, "No files", C_GRAY_MID);
    } else {
        int n = fm_count < 16 ? fm_count : 16;
        for (int i = 0; i < n; i++) {
            int len = (int)strlen(fm_entries[i]);
            int y = base + 40 + i * 26;
            if (len && fm_entries[i][len - 1] == '\\')
                fb_text(14, y, fm_entries[i], C_TEAL);
            else
                fb_text(14, y, fm_entries[i], C_GRAY_HI);
        }
        if (fm_count > 16)
            fb_text(14, base + 40 + 16 * 26, "...", C_GRAY_LOW);
    }
}

static void mycomp_draw(void) {
    int cx = GFX_W / 2;
    int base = TAB_BAR_H + 30;
    int cy = base + (GFX_H - base) / 2;
    app_header("My Computer");
    fb_rect(0, base, GFX_W, GFX_H - base, C_SURFACE);
    /* big drive card */
    int cw = 380, ch = 250;
    int cxx = cx - cw / 2, cyy = cy - ch / 2;
    fb_rrect_outline(cxx, cyy, cw, ch, 12, C_GRAY_DARK, C_SURFACE_HI);
    /* disk icon */
    int ix = cx - 32, iy = cyy + 34;
    fb_rrect(ix, iy, 64, 64, 8, C_GRAY_DARK);
    fb_rrect(ix + 6, iy + 6, 52, 44, 5, C_ACCENT_DIM);
    fb_hline(ix + 14, iy + 16, 20, C_ACCENT);
    fb_rect(ix + 24, iy + 50, 16, 5, C_GRAY_MID);
    fb_text2x_center(cx, iy + 72, "C:", C_GRAY_HI);
    fb_text_center(cx, iy + 112, "Hard disk", C_GRAY_MID);
    fb_text_center(cx, iy + 132, "Local Disk (C:)", C_GRAY_LOW);
}

static void recycle_draw(void) {
    int cx = GFX_W / 2;
    int base = TAB_BAR_H + 30;
    int cy = base + (GFX_H - base) / 2;
    app_header("Recycle Bin");
    fb_rect(0, base, GFX_W, GFX_H - base, C_SURFACE);
    /* big bin icon */
    fb_rect(cx - 14, cy - 96, 28, 8, C_TEAL);
    fb_rrect(cx - 21, cy - 88, 42, 36, 6, C_TEAL);
    fb_vline(cx - 11, cy - 84, 28, C_GREEN);
    fb_vline(cx + 11, cy - 84, 28, C_GREEN);
    fb_hline(cx - 19, cy - 58, 38, C_GREEN);
    fb_text2x_center(cx, cy - 40, "Recycle Bin", C_GRAY_HI);
    fb_text_center(cx, cy + 2, "The Recycle Bin is empty.", C_GRAY_MID);
}

static void app_close_key(int sc) {
    if (sc == 0x01) /* escape: close this app's tab */
        tab_close(active);
}

/* ---- render ---- */
static void draw_tab_bar(void) {
    fb_rect(0, 0, GFX_W, TAB_BAR_H, C_TAB_BG);
    fb_hline(0, TAB_BAR_H - 1, GFX_W, C_GRAY_DARK);
    for (int i = 0; i < tab_count; i++) {
        int x = tab_x(i), w = tab_w(i);
        if (x >= GFX_W)
            break;
        int hov = i != active && cur_y < TAB_BAR_H &&
                  cur_x >= x && cur_x < x + w;
        if (i == active) {
            fb_top_rrect(x, 3, w, TAB_BAR_H - 3, 8, C_TAB_ACTIVE);
            fb_rect(x, TAB_BAR_H - 1, w, 1, C_TAB_ACTIVE);
            fb_text(x + 8, 8, app_titles[tabs[i].kind], C_GRAY_HI);
        } else {
            fb_top_rrect(x, 4, w, TAB_BAR_H - 9, 8, hov ? C_TAB_HOVER : C_TAB_BG);
            fb_text(x + 8, 8, app_titles[tabs[i].kind],
                    hov ? C_GRAY_HI : C_GRAY_MID);
        }
        /* close button (all but App Manager) */
        if (tabs[i].kind != APP_MANAGER) {
            int bx = x + w - 19, by = 10;
            int xhov = cur_y >= by && cur_y < by + 13 &&
                       cur_x >= bx && cur_x < bx + 13;
            uint8_t bcol = xhov ? C_RED : (i == active ? C_TAB_ACTIVE : C_TAB_BG);
            fb_circle(bx + 6, by + 6, 6, bcol);
            uint8_t xc = xhov ? C_WHITE : C_GRAY_MID;
            for (int k = 0; k < 4; k++) {
                fb_px(bx + 4 + k, by + 4 + k, xc);
                fb_px(bx + 8 - k, by + 4 + k, xc);
            }
        }
    }
    /* new-tab (+) button */
    int cx = GFX_W - 16, cy = 15;
    fb_circle(cx, cy, 6, C_TAB_HOVER);
    fb_hline(cx - 3, cy, 6, C_GRAY_HI);
    fb_vline(cx, cy - 3, 6, C_GRAY_HI);
}

static void draw_all(void) {
    draw_tab_bar();
    if (active < 0)
        return;
    switch (tabs[active].kind) {
    case APP_MANAGER:
        draw_app_manager();
        break;
    case APP_SHELL:
        shell_draw();
        break;
    case APP_FMGR:
        fmgr_draw();
        break;
    case APP_MYCOMP:
        mycomp_draw();
        break;
    case APP_RECYCLE:
        recycle_draw();
        break;
    default:
        break;
    }
}

/* ---- input ---- */
static int prev_sc = -1;
static int prev_btns = 0;
static volatile int desktop_exit = 0;

static const uint8_t arrow[11][10] = {
    { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 0, 1, 0, 0, 0, 0 },
    { 0, 1, 0, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 1, 0, 0, 0, 0, 0, 1, 0, 0 },
    { 0, 1, 0, 0, 1, 1, 1, 1, 1, 0 },
    { 0, 1, 0, 0, 1, 0, 0, 0, 0, 0 },
    { 0, 0, 1, 0, 1, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
};

static void draw_cursor(void) {
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 10; c++)
            if (arrow[r][c])
                gfx_pixel(cur_x + c + 1, cur_y + r + 1, C_WHITE);
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 10; c++)
            if (arrow[r][c])
                gfx_pixel(cur_x + c, cur_y + r, C_BLACK);
}

static void blit(void) {
    gfx_blit(fb);
    draw_cursor();
}

static void refresh(void) {
    if (dirty) {
        draw_all();
        blit();
        dirty = 0;
    }
}

static void click_at(int mx, int my) {
    if (my < TAB_BAR_H) {
        /* new-tab button */
        if (mx >= GFX_W - 26 && mx < GFX_W - 4) {
            tab_open(APP_MANAGER);
            return;
        }
        for (int i = 0; i < tab_count; i++) {
            int x = tab_x(i), w = tab_w(i);
            if (x >= GFX_W)
                break;
            if (mx >= x && mx < x + w) {
                if (tabs[i].kind != APP_MANAGER && mx >= x + w - 14) {
                    tab_close(i);
                } else if (i != active) {
                    active = i;
                    dirty = 1;
                }
                return;
            }
        }
        return;
    }
    /* content area: dispatch to the active app */
    if (active < 0)
        return;
    switch (tabs[active].kind) {
    case APP_MANAGER:
        manager_click(mx, my);
        break;
    case APP_SHELL:
        shell_click(mx, my);
        break;
    default:
        break;
    }
}

static void handle_key(int sc) {
    if (sc == 0x3E && prev_sc == 0x38) { /* Alt+F4 */
        prev_sc = -1;
        if (active >= 0) {
            if (tabs[active].kind == APP_MANAGER)
                desktop_exit = 1; /* close the manager = quit the desktop */
            else
                tab_close(active);
        }
        return;
    }
    prev_sc = (sc == 0x38) ? 0x38 : -1;
    if (sc == 0x38) /* alt make: wait for F4 */
        return;
    if (active < 0)
        return;
    switch (tabs[active].kind) {
    case APP_MANAGER:
        manager_key(sc);
        break;
    case APP_SHELL:
        shell_key(sc);
        break;
    case APP_FMGR:
    case APP_MYCOMP:
    case APP_RECYCLE:
        app_close_key(sc);
        break;
    default:
        break;
    }
}

/* ---- plugin ---- */

void desktop_enter(void) {
    puts("[DESKTOP] tabbed desktop started\n");
    desktop_exit = 0;
    gfx_enter();
    guard_finish();         /* 启动成功: 崩溃守护自毁并回收 64KB */
    font = gfx_get_font_aa();
    {
        int sw = 0, sh = 0;
        gfx_get_screen_size(&sw, &sh);
        printf("[DESKTOP] screen %dx%d, desktop %dx%d (auto-fit)\n",
               sw, sh, GFX_W, GFX_H);
    }

    cur_x = GFX_W / 2;
    cur_y = GFX_H / 2;
    prev_btns = 0;
    prev_sc = -1;
    tab_count = 0;
    active = -1;
    mng_sel = 0;
    tab_open(APP_MANAGER);
    dirty = 1;
    while (!desktop_exit) {
        int dx, dy, btns;
        int got = 0;
        int sc;
        if (mouse_poll(&dx, &dy, &btns) || btns != prev_btns) {
            cur_x += dx;
            cur_y += dy;
            if (cur_x < 0) cur_x = 0;
            if (cur_x >= GFX_W) cur_x = GFX_W - 1;
            if (cur_y < 0) cur_y = 0;
            if (cur_y >= GFX_H) cur_y = GFX_H - 1;
            if ((btns & MOUSE_BTN_LEFT) && !(prev_btns & MOUSE_BTN_LEFT)) {
                click_at(cur_x, cur_y);
            }
            prev_btns = btns;
            dirty = 1;
            got = 1;
        }
        /* drain keyboard buffer completely before processing */
        while ((sc = keyboard_poll_scancode()) >= 0) {
            handle_key(sc);
            got = 1;
        }
        if (dirty)
            refresh();
        if (!got)
            proc_yield();
    }
    gfx_leave();
    puts("[DESKTOP] exiting\n");
}

int plugin_desktop_init(void) {
    return 0;
}

int plugin_desktop_cmd(char *args) {
    (void)args;
    desktop_enter();
    return 0;
}

REGISTER_PLUGIN(desktop);

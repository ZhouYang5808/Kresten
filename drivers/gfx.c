/* ===== x86/drivers/gfx.c: VGA graphics driver =====
 * Preferred mode: Bochs VBE linear framebuffer 640x480x8 @ 0xE0000000
 * (supported by QEMU std VGA and VMware).  Fallback: VGA mode 13h
 * 320x200x256 @ 0xA0000.
 *
 * The desktop back-buffer is GFX_W x GFX_H; it is scaled by an integer
 * factor and centered on the physical panel, like the ARM driver.
 */
#include <driver.h>
#include <stdio.h>
#include <stdint.h>
#include <io.h>
#include <gfx.h>
#include <font8x16.h>

extern void vga_clear(void);

#define GFX_MEM ((volatile uint8_t *)0xA0000)
#define VBE_LFB ((volatile uint8_t *)0xE0000000)

#define VBE_INDEX 0x1CE
#define VBE_DATA  0x1CF
#define VBE_DISPI_ID        0x0
#define VBE_DISPI_XRES      0x1
#define VBE_DISPI_YRES      0x2
#define VBE_DISPI_BPP       0x3
#define VBE_DISPI_ENABLE    0x4
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_ENABLED   0x01

static int gfx_on = 0;
static uint8_t font_buf[8192]; /* 8x16 font: 256 glyphs x 32 bytes (plane 2) */

/* ---- runtime panel state ---- */
static volatile uint8_t *scr_mem = GFX_MEM;
static int scr_w = 0, scr_h = 0;
int gfx_w = 320, gfx_h = 200;     /* logical desktop size (GFX_W/GFX_H macros) */
static int use_vbe = 0;
static int use_svga = 0;
static int bpp = 8;               /* bytes per pixel on the panel */
static uint32_t svga_pal[256];    /* 8-bit index -> 32bpp color (SVGA) */

const uint8_t *gfx_get_font(void) {
    return font_buf;
}

/* ---- VGA register sets ----

 * Mode 13h (320x200x256) */
static const uint8_t seq_13h[] = {0x03, 0x01, 0x0F, 0x00, 0x0E};
static const uint8_t crtc_13h[] = {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
                                   0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF};
static const uint8_t gra_13h[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF};
static const uint8_t att_13h[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                  0x41, 0x00, 0x0F, 0x00, 0x00};

/* Mode 3 (80x25 text) */
static const uint8_t seq_t3[] = {0x03, 0x00, 0x03, 0x00, 0x02};
static const uint8_t crtc_t3[] = {0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
                                  0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
                                  0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF};
static const uint8_t gra_t3[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF};
static const uint8_t att_t3[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
                                 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
                                 0x0C, 0x00, 0x0F, 0x08, 0x00};

static void write_regs(uint16_t port, const uint8_t *vals, int n) {
    for (int i = 0; i < n; i++) {
        outb(port, (uint8_t)i);
        outb(port + 1, vals[i]);
    }
}

static void vga_set_mode(uint8_t misc,
                         const uint8_t *seq, const uint8_t *crtc,
                         const uint8_t *gra, const uint8_t *att) {
    /* misc output first: color mode, 0xA0000/0xB8000 plane */
    outb(0x3C2, misc);
    /* sequencer: sync reset on, program, sync reset off */
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01);
    write_regs(0x3C4, seq, 5);
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03);
    /* CRTC: clear protection bit, program */
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x00);
    write_regs(0x3D4, crtc, 25);
    /* graphics controller */
    write_regs(0x3CE, gra, 9);
    /* attribute controller: index and data share port 0x3C0,
     * they must be written alternately; a read of 0x3DA resets
     * the flip-flop back to index mode */
    inb(0x3DA);
    for (int i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, att[i]);
    }
    inb(0x3DA);
    outb(0x3C0, 0x20); /* palette address bit 5: enable display */
}

/* ---- Bochs VBE extensions (QEMU std VGA / VMware) ---- */

static void vbe_write(uint16_t reg, uint16_t val) {
    outw(VBE_INDEX, reg);
    outw(VBE_DATA, val);
}

static uint16_t vbe_read(uint16_t reg) {
    outw(VBE_INDEX, reg);
    return inw(VBE_DATA);
}

/* ---- minimal PCI config space access (0xCF8/0xCFC) ---- */

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_read_cfg(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    uint32_t addr = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                    | ((uint32_t)func << 8) | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_cfg_helper(uint8_t bus, uint8_t dev, uint8_t func,
                                 uint8_t reg, uint32_t val) {
    uint32_t addr = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11)
                    | ((uint32_t)func << 8) | (reg & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* Find the VGA controller's framebuffer BAR.  Returns 0 if none found.
 * On the VMware SVGA the first BAR is an I/O region; the framebuffer
 * is in BAR1 in that case. */
static uint32_t pci_find_vga_lfb(void) {
    for (int dev = 0; dev < 32; dev++) {
        for (int func = 0; func < 8; func++) {
            uint32_t id = pci_read_cfg(0, dev, func, 0x00);
            if (id == 0xFFFFFFFF || id == 0)
                break; /* no function; don't skip funcs for non-multifunc */
            uint32_t class_rev = pci_read_cfg(0, dev, func, 0x08);
            if (((class_rev >> 24) & 0xFF) == 0x03) { /* display controller */
                uint32_t bar0 = pci_read_cfg(0, dev, func, 0x10);
                if (bar0 & 0x01) {
                    uint32_t bar1 = pci_read_cfg(0, dev, func, 0x14);
                    if (bar1 & 0x01)
                        continue;
                    return bar1 & 0xFFFFFFF0;
                }
                return bar0 & 0xFFFFFFF0;
            }
        }
    }
    return 0;
}

/* Try to switch to 640x480x8 linear framebuffer.  Returns 0 on success. */
static int vbe_enter_640x480(void) {
    uint16_t id = vbe_read(VBE_DISPI_ID);
    if ((id & 0xFFF0) < 0xB0C0)
        return -1;
    vbe_write(VBE_DISPI_ENABLE, 0);
    vbe_write(VBE_DISPI_XRES, 640);
    vbe_write(VBE_DISPI_YRES, 480);
    vbe_write(VBE_DISPI_BPP, 8);
    vbe_write(VBE_DISPI_ENABLE, VBE_DISPI_LFB_ENABLED | VBE_DISPI_ENABLED);
    if (!(vbe_read(VBE_DISPI_ENABLE) & VBE_DISPI_ENABLED))
        return -1;
    if (vbe_read(VBE_DISPI_XRES) != 640 || vbe_read(VBE_DISPI_YRES) != 480)
        return -1;
    return 0;
}

/* ---- VMware SVGA II (PCI 15AD:0405), index/value I/O port pair ----
 * BAR0 = I/O ports: index at BAR0+0, value at BAR0+4 (both dword).
 * Framebuffer address comes from SVGA_REG_FB_START (+ FB_OFFSET). */

#define SVGA_REG_ID             0
#define SVGA_REG_ENABLE         1
#define SVGA_REG_WIDTH          2
#define SVGA_REG_HEIGHT         3
#define SVGA_REG_MAX_WIDTH      4
#define SVGA_REG_MAX_HEIGHT     5
#define SVGA_REG_BITS_PER_PIXEL 7
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_FB_START       13
#define SVGA_REG_FB_OFFSET      14
#define SVGA_REG_FB_SIZE        16
#define SVGA_REG_VRAM_SIZE      15
#define SVGA_REG_CAPABILITIES   17
#define SVGA_REG_MEM_START      18
#define SVGA_REG_MEM_SIZE       19
#define SVGA_REG_CONFIG_DONE    20
#define SVGA_REG_SYNC           21
#define SVGA_REG_TRACES         45
#define SVGA_REG_DEPTH          6
#define SVGA_REG_PSEUDOCOLOR    8
#define SVGA_REG_PITCHLOCK      32
/* FIFO: commands start 64 bytes in, past the header fields */
#define SVGA_FIFO_MIN_BYTE      64
#define SVGA_ID_2               0x90000002
#define SVGA_ID_1               0x90000001
#define SVGA_ID_0               0x90000000
#define SVGA_CAP_8BIT_EMULATION 0x00000200

static uint16_t svga_io_base = 0;
static uint8_t svga_val_off = 1; /* SVGA II: value port at BAR0+1; else +4 */
static uint32_t svga_pitch = 0;  /* actual bytes per line, from BYTES_PER_LINE */
static volatile uint32_t *svga_fifo = 0; /* FIFO memory (for update cmds) */
static uint32_t svga_fb_start = 0;
static uint32_t svga_fb_off = 0;

static uint32_t svga_read(uint16_t idx) {
    outl((uint16_t)(svga_io_base + 0), idx);
    return inl((uint16_t)(svga_io_base + svga_val_off));
}

static void svga_write(uint16_t idx, uint32_t val) {
    outl((uint16_t)(svga_io_base + 0), idx);
    outl((uint16_t)(svga_io_base + svga_val_off), val);
}

/* Find VMware SVGA device, return 0 on success. */
static int svga_find(void) {
    for (int dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read_cfg(0, dev, 0, 0x00);
        if (id == 0xFFFFFFFF || id == 0)
            continue;
        uint32_t dev_id = (id >> 16) & 0xFFFF;
        uint32_t vendor = id & 0xFFFF;
        if (vendor == 0x15AD && dev_id == 0x0405) {
            uint32_t bar0 = pci_read_cfg(0, dev, 0, 0x10);
            if (!(bar0 & 0x01))
                continue; /* must be I/O space */
            svga_io_base = (uint16_t)(bar0 & 0xFFFC);
            /* enable I/O + memory access */
            uint32_t cmd = pci_read_cfg(0, dev, 0, 0x04);
            pci_write_cfg_helper(0, dev, 0, 0x04, cmd | 0x07);
            /* read framebuffer BARs: GFB is BAR1 (or BAR2 on some layouts) */
            uint32_t bar1 = pci_read_cfg(0, dev, 0, 0x14);
            uint32_t bar2 = pci_read_cfg(0, dev, 0, 0x18);
            printf("[SVGA] found: dev=%d id=%04X:%04X bar0=%08X io_base=%04X bar1=%08X bar2=%08X\n",
                   dev, vendor, dev_id, (unsigned)bar0, svga_io_base,
                   (unsigned)bar1, (unsigned)bar2);
            return 0;
        }
        /* also accept SVGA3 (0x040F, 0x0410) for VMware with 3D */
        if (vendor == 0x15AD && (dev_id == 0x040F || dev_id == 0x0410)) {
            printf("[SVGA] SVGA3 device id=%04X:%04X (MMIO not supported yet)\n",
                   vendor, dev_id);
            return -1;
        }
    }
    return -1;
}

/* Issue SVGA_CMD_UPDATE via FIFO to tell VMware to refresh the display.
 * Commands start at byte SVGA_FIFO_MIN (64), past the header fields
 * (MIN/MAX/NEXT/STOP/CAPABILITIES/FLAGS/FENCE/FENCE_GOAL/CONFIG_DONE/BUSY...).
 * The guest advances NEXT after appending a command; the device consumes
 * from STOP.  A write to SVGA_REG_SYNC kicks the device to process the
 * FIFO (CONFIG_DONE is only for the one-time FIFO setup). */
static void svga_fifo_update(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!svga_fifo)
        return;
    uint32_t min = svga_fifo[0]; /* SVGA_FIFO_MIN */
    uint32_t max = svga_fifo[1]; /* SVGA_FIFO_MAX */
    uint32_t next = svga_fifo[2]; /* SVGA_FIFO_NEXT */
    /* ring: wrap around if the command would not fit; the device is
     * expected to have consumed everything (STOP caught up) by then */
    if (next < min || next + 5 * 4 > max)
        next = min;
    uint32_t base = next / 4;
    /* SVGA_CMD_UPDATE = 3, followed by x, y, w, h (all uint32) */
    svga_fifo[base + 0] = 3;
    svga_fifo[base + 1] = x;
    svga_fifo[base + 2] = y;
    svga_fifo[base + 3] = w;
    svga_fifo[base + 4] = h;
    svga_fifo[2] = next + 5 * 4; /* NEXT advances */
    svga_write(SVGA_REG_CONFIG_DONE, 1); /* kick */
}

/* Try VMware SVGA 32bpp.  Resolution: the display's current size from
 * SVGA_REG_WIDTH/HEIGHT (VMware mirrors the monitor), except the
 * 640x480 default which must be bumped to 800x600 or QEMU's
 * vmsvga_check_size would skip the surface switch.  Returns 0 on success. */
static int svga_enter(void) {
    svga_fifo = 0;
    svga_fb_start = 0;
    svga_fb_off = 0;
    if (svga_find() != 0) {
        printf("[SVGA] FAIL: device not found\n");
        return -1;
    }
    /* detect value-port layout: SVGA II (0405) uses BAR0+1,
     * older chipsets BAR0+4 */
    svga_val_off = 1;
    svga_write(SVGA_REG_ID, SVGA_ID_2);
    if (svga_read(SVGA_REG_ID) != SVGA_ID_2) {
        svga_val_off = 4;
        svga_write(SVGA_REG_ID, SVGA_ID_2);
        if (svga_read(SVGA_REG_ID) != SVGA_ID_2) {
            printf("[SVGA] FAIL: ID probe failed\n");
            return -1;
        }
    }
    /* version negotiation: ID_2 down to ID_0 */
    uint32_t id = SVGA_ID_2;
    while (1) {
        svga_write(SVGA_REG_ID, id);
        if (svga_read(SVGA_REG_ID) == id)
            break;
        if (id == SVGA_ID_0) {
            printf("[SVGA] FAIL: version negotiate failed\n");
            return -1;
        }
        id--;
    }
    /* pick the display resolution */
    uint32_t cur_w = svga_read(SVGA_REG_WIDTH);
    uint32_t cur_h = svga_read(SVGA_REG_HEIGHT);
    uint32_t max_w = svga_read(SVGA_REG_MAX_WIDTH);
    uint32_t max_h = svga_read(SVGA_REG_MAX_HEIGHT);
    uint32_t vram = svga_read(SVGA_REG_VRAM_SIZE);
    uint32_t mode_w = 800, mode_h = 600;
    if (cur_w >= 640 && cur_h >= 480 && cur_w <= max_w && cur_h <= max_h &&
        !(cur_w == 640 && cur_h == 480)) {
        if (!vram || (uint64_t)cur_w * cur_h * 4 <= vram)
            mode_w = cur_w, mode_h = cur_h;
    }
    /* disable first */
    svga_write(SVGA_REG_ENABLE, 0);
    svga_write(SVGA_REG_WIDTH, mode_w);
    svga_write(SVGA_REG_HEIGHT, mode_h);
    svga_write(SVGA_REG_BITS_PER_PIXEL, 32);
    if (svga_read(SVGA_REG_BITS_PER_PIXEL) != 32) {
        printf("[SVGA] FAIL: BPP not 32\n");
        return -1;
    }
    /* initialize the command FIFO header so the device accepts SVGA mode */
    {
        uint32_t mem_start = svga_read(SVGA_REG_MEM_START);
        if (mem_start < 0x100000 || (mem_start & 0x3)) {
            printf("[SVGA] FAIL: bad mem_start\n");
            return -1;
        }
        svga_fifo = (volatile uint32_t *)mem_start;
        svga_fifo[0] = SVGA_FIFO_MIN_BYTE;              /* SVGA_FIFO_MIN */
        svga_fifo[1] = SVGA_FIFO_MIN_BYTE + 16 * 1024;  /* SVGA_FIFO_MAX */
        svga_fifo[2] = SVGA_FIFO_MIN_BYTE;              /* SVGA_FIFO_NEXT */
        svga_fifo[3] = SVGA_FIFO_MIN_BYTE;              /* SVGA_FIFO_STOP */
        for (int i = 4; i < 16; i++)
            svga_fifo[i] = 0;
    }
    svga_write(SVGA_REG_CONFIG_DONE, 1);
    /* trace-based updates: any GFB write auto-refreshes the display,
     * so we don't depend on FIFO UPDATE commands */
    svga_write(SVGA_REG_TRACES, 1);
    svga_write(SVGA_REG_ENABLE, 1);
    svga_fb_start = svga_read(SVGA_REG_FB_START);
    svga_fb_off = svga_read(SVGA_REG_FB_OFFSET);
    uint32_t pitch = svga_read(SVGA_REG_BYTES_PER_LINE);
    uint32_t rb_w = svga_read(SVGA_REG_WIDTH);
    uint32_t rb_h = svga_read(SVGA_REG_HEIGHT);
    if (!svga_fb_start || pitch < mode_w * 4) {
        printf("[SVGA] FAIL: bad fb/pitch\n");
        return -1;
    }
    /* the device may clamp the mode: trust the post-enable readback */
    if (rb_w >= 640 && rb_h >= 480 && rb_w <= max_w && rb_h <= max_h)
        mode_w = rb_w, mode_h = rb_h;
    scr_mem = (volatile uint8_t *)(svga_fb_start + svga_fb_off);
    scr_w = (int)mode_w;
    scr_h = (int)mode_h;
    bpp = 4;
    svga_pitch = pitch;
    printf("[GFX] SVGA %dx%d pitch %u\n",
           (unsigned)mode_w, (unsigned)mode_h, (unsigned)pitch);
    /* tell VMware to display the full framebuffer */
    svga_fifo_update(0, 0, mode_w, mode_h);
    return 0;
}

static void gfx_set_palette(void) {
    /* 0-15: standard VGA 16 colors (6-bit DAC) */
    static const uint8_t p16[48] = {
        0, 0, 0,       0, 0, 42,     0, 42, 0,     0, 42, 42,
        42, 0, 0,      42, 0, 42,    42, 21, 0,    42, 42, 42,
        21, 21, 21,    21, 21, 63,   21, 63, 21,   21, 63, 63,
        63, 21, 21,    63, 21, 63,   63, 63, 21,   63, 63, 63
    };
    /* 16-255: custom modern dark-theme palette (6-bit DAC values) */
    uint8_t rgb[256][3];
    for (int i = 0; i < 16; i++) {
        rgb[i][0] = p16[i * 3];
        rgb[i][1] = p16[i * 3 + 1];
        rgb[i][2] = p16[i * 3 + 2];
    }
    /* 16-31: desktop gradient (deep blue-black -> blue-violet) */
    for (int i = 0; i < 16; i++) {
        rgb[16 + i][0] = (uint8_t)(i * 9 / 15);
        rgb[16 + i][1] = (uint8_t)(4 + i * 6 / 15);
        rgb[16 + i][2] = (uint8_t)(15 + i * 17 / 15);
    }
    /* 32-47: dark surface gradient (panel/card base) */
    for (int i = 0; i < 16; i++) {
        rgb[32 + i][0] = (uint8_t)(21 + i * 13 / 15);
        rgb[32 + i][1] = (uint8_t)(23 + i * 14 / 15);
        rgb[32 + i][2] = (uint8_t)(29 + i * 19 / 15);
    }
    /* 48-63: bright surface gradient (hover/active) */
    for (int i = 0; i < 16; i++) {
        rgb[48 + i][0] = (uint8_t)(34 + i * 17 / 15);
        rgb[48 + i][1] = (uint8_t)(37 + i * 18 / 15);
        rgb[48 + i][2] = (uint8_t)(48 + i * 15 / 15);
    }
    /* 64-79: accent blue -> cyan */
    for (int i = 0; i < 16; i++) {
        rgb[64 + i][0] = (uint8_t)(14 + i * 9 / 15);
        rgb[64 + i][1] = (uint8_t)(40 + i * 22 / 15);
        rgb[64 + i][2] = (uint8_t)63;
    }
    /* 80-95: purple -> pink */
    for (int i = 0; i < 16; i++) {
        rgb[80 + i][0] = (uint8_t)(38 + i * 25 / 15);
        rgb[80 + i][1] = (uint8_t)(23 + i * 9 / 15);
        rgb[80 + i][2] = (uint8_t)(63 - i * 5 / 15);
    }
    /* 96-111: teal */
    for (int i = 0; i < 16; i++) {
        rgb[96 + i][0] = (uint8_t)(16 + i * 9 / 15);
        rgb[96 + i][1] = (uint8_t)(54 + i * 8 / 15);
        rgb[96 + i][2] = (uint8_t)(40 + i * 22 / 15);
    }
    /* 112-127: orange -> yellow */
    for (int i = 0; i < 16; i++) {
        rgb[112 + i][0] = (uint8_t)63;
        rgb[112 + i][1] = (uint8_t)(26 + i * 32 / 15);
        rgb[112 + i][2] = (uint8_t)(4 + i * 9 / 15);
    }
    /* 128-143: green */
    for (int i = 0; i < 16; i++) {
        rgb[128 + i][0] = (uint8_t)(16 + i * 11 / 15);
        rgb[128 + i][1] = (uint8_t)(47 + i * 15 / 15);
        rgb[128 + i][2] = (uint8_t)(21 + i * 13 / 15);
    }
    /* 144-159: red */
    for (int i = 0; i < 16; i++) {
        rgb[144 + i][0] = (uint8_t)60;
        rgb[144 + i][1] = (uint8_t)(7 + i * 11 / 15);
        rgb[144 + i][2] = (uint8_t)(7 + i * 11 / 15);
    }
    /* 160-255: 96-step gray ramp (8 -> 63) */
    for (int i = 0; i < 96; i++) {
        uint8_t g = (uint8_t)(8 + i * 55 / 95);
        rgb[160 + i][0] = g;
        rgb[160 + i][1] = g;
        rgb[160 + i][2] = g;
    }
    /* VGA DAC (6-bit) for 8bpp modes */
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) {
        outb(0x3C9, rgb[i][0]);
        outb(0x3C9, rgb[i][1]);
        outb(0x3C9, rgb[i][2]);
    }
    /* 32bpp lookup table (8-bit components, opaque alpha) for VMware SVGA */
    for (int i = 0; i < 256; i++) {
        uint32_t r = (uint32_t)rgb[i][0] * 255 / 63;
        uint32_t g = (uint32_t)rgb[i][1] * 255 / 63;
        uint32_t b = (uint32_t)rgb[i][2] * 255 / 63;
        svga_pal[i] = 0xFF000000U | (r << 16) | (g << 8) | b;
    }
}

/* Save the 8x16 text font (VRAM plane 2) before switching to graphics.
 * Mode 13h uses chained memory writes that hit all four planes, which
 * would otherwise destroy the character generator font. */
static void gfx_save_font(void) {
    for (int i = 0; i < 8192; i++)
        font_buf[i] = vga_font8x16[i];
}

static void gfx_restore_font(void) {
    outb(0x3C4, 0x02); outb(0x3C5, 0x04); /* S2: write plane 2 only */
    outb(0x3C4, 0x04); outb(0x3C5, 0x06); /* chain4 off */
    outb(0x3CE, 0x05); outb(0x3CF, 0x00); /* GRA5: write mode 0 */
    outb(0x3CE, 0x06); outb(0x3CF, 0x04); /* GRA6: 0xA0000 64K window */
    outb(0x3CE, 0x04); outb(0x3CF, 0x02); /* GRA4: write plane 2 */
    for (int i = 0; i < 8192; i++)
        *(volatile uint8_t *)(0xA0000 + i) = font_buf[i];
    outb(0x3C4, 0x02); outb(0x3C5, 0x03); /* restore S2 map mask (text mode) */
    outb(0x3C4, 0x04); outb(0x3C5, 0x02);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x10);
    outb(0x3CE, 0x06); outb(0x3CF, 0x0E);
}

/* Stretch-fill mapping: the desktop back-buffer (GFX_W x GFX_H) is
 * stretched evenly over the whole physical panel 鈥?no letterbox. */
static void compute_layout(void) {
    /* nothing to compute: mapping is derived per-pixel from scr_w/scr_h */
}

/* Is the VGA controller a VMware SVGA?  (vendor 0x15AD) */
static int pci_vga_is_vmware(void) {
    for (int dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read_cfg(0, dev, 0, 0x00);
        if (id == 0xFFFFFFFF || id == 0)
            continue;
        uint32_t class_rev = pci_read_cfg(0, dev, 0, 0x08);
        if (((class_rev >> 24) & 0xFF) == 0x03) /* display controller */
            return (id & 0xFFFF) == 0x15AD;
    }
    return 0;
}

int gfx_enter(void) {
    if (gfx_on)
        return 0;
    gfx_save_font();
    if (pci_vga_is_vmware()) {
        if (svga_enter() == 0)
            use_svga = 1;
        else {
            printf("[GFX] SVGA failed, trying VBE\n");
            use_vbe = (vbe_enter_640x480() == 0);
        }
    } else {
        use_vbe = (vbe_enter_640x480() == 0);
        if (!use_vbe) {
            printf("[GFX] VBE failed, trying SVGA\n");
            use_svga = (svga_enter() == 0);
        }
    }
    if (use_vbe) {
        uint32_t lfb = pci_find_vga_lfb();
        scr_mem = (volatile uint8_t *)(lfb ? lfb : (uint32_t)VBE_LFB);
        scr_w = 640;
        scr_h = 480;
        bpp = 1;
        printf("[GFX] VBE LFB @ 0x%X\n", (unsigned)lfb);
    } else if (use_svga) {
        printf("[GFX] VMware SVGA mode %dx%dx%d\n", scr_w, scr_h, bpp * 8);
    } else {
        printf("[GFX] falling back to mode 13h\n");
        vga_set_mode(0x63, seq_13h, crtc_13h, gra_13h, att_13h);
        scr_mem = GFX_MEM;
        scr_w = GFX_W;
        scr_h = GFX_H;
        bpp = 1;
    }
    gfx_set_palette();
    compute_layout();
    /* native resolution: logical desktop = physical panel (1:1) */
    gfx_w = scr_w;
    gfx_h = scr_h;
    printf("[GFX] %s %dx%d, desktop %dx%d (fill)\n",
           use_vbe ? "VBE" : (use_svga ? "SVGA" : "mode13h"),
           scr_w, scr_h, GFX_W, GFX_H);
    gfx_on = 1;
    return 0;
}

static void gfx_reset_palette(void) {
    /* restore standard VGA 16-color palette for text mode */
    static const uint8_t p16[48] = {
        0, 0, 0,       0, 0, 42,     0, 42, 0,     0, 42, 42,
        42, 0, 0,      42, 0, 42,    42, 21, 0,    42, 42, 42,
        21, 21, 21,    21, 21, 63,   21, 63, 21,   21, 63, 63,
        63, 21, 21,    63, 21, 63,   63, 63, 21,   63, 63, 63
    };
    outb(0x3C8, 0);
    for (int i = 0; i < 48; i++)
        outb(0x3C9, p16[i]);
}

void gfx_leave(void) {
    if (!gfx_on)
        return;
    if (use_svga) {
        svga_write(SVGA_REG_ENABLE, 0); /* back to VGA mode */
        use_svga = 0;
    } else if (use_vbe) {
        vbe_write(VBE_DISPI_ENABLE, 0); /* back to text mode via VGA */
        use_vbe = 0;
    }
    bpp = 1;
    vga_set_mode(0x63, seq_t3, crtc_t3, gra_t3, att_t3);
    gfx_on = 0;
    gfx_restore_font();
    gfx_reset_palette();
    vga_clear();
}

void gfx_get_screen_size(int *w, int *h) {
    if (w) *w = scr_w;
    if (h) *h = scr_h;
}

int gfx_active(void) {
    return gfx_on;
}

/* map a logical desktop pixel to its full tile on the physical panel */
static void px_screen(int x, int y, uint8_t color) {
    int x0 = (int)((uint32_t)x * scr_w / GFX_W);
    int x1 = (int)((uint32_t)(x + 1) * scr_w / GFX_W);
    int y0 = (int)((uint32_t)y * scr_h / GFX_H);
    int y1 = (int)((uint32_t)(y + 1) * scr_h / GFX_H);
    if (x1 > scr_w) x1 = scr_w;
    if (y1 > scr_h) y1 = scr_h;
    if (bpp == 1) {
        for (int py = y0; py < y1; py++)
            for (int px = x0; px < x1; px++)
                scr_mem[(uint32_t)py * scr_w + (uint32_t)px] = color;
    } else {
        uint32_t c = svga_pal[color];
        uint32_t pitch = svga_pitch ? svga_pitch : (uint32_t)scr_w * 4;
        for (int py = y0; py < y1; py++) {
            volatile uint32_t *line =
                (volatile uint32_t *)(scr_mem + (uint32_t)py * pitch);
            for (int px = x0; px < x1; px++)
                line[px] = c;
        }
    }
}

void gfx_clear(uint8_t color) {
    if (!gfx_on)
        return;
    if (bpp == 1) {
        for (int i = 0; i < scr_w * scr_h; i++)
            scr_mem[i] = color;
    } else {
        uint32_t c = svga_pal[color];
        uint32_t pitch = svga_pitch ? svga_pitch : (uint32_t)scr_w * 4;
        for (int py = 0; py < scr_h; py++) {
            volatile uint32_t *line =
                (volatile uint32_t *)(scr_mem + (uint32_t)py * pitch);
            for (int px = 0; px < scr_w; px++)
                line[px] = c;
        }
    }
    if (use_svga)
        svga_fifo_update(0, 0, scr_w, scr_h);
}

/* Copy the desktop back-buffer to the screen, stretched to fill it. */
void gfx_blit(const uint8_t *fb) {
    if (bpp == 1) {
        for (int py = 0; py < scr_h; py++) {
            int sy = (int)((uint32_t)py * GFX_H / scr_h);
            const uint8_t *row = fb + (uint32_t)sy * GFX_W;
            volatile uint8_t *line = scr_mem + (uint32_t)py * scr_w;
            for (int px = 0; px < scr_w; px++) {
                int sx = (int)((uint32_t)px * GFX_W / scr_w);
                line[px] = row[sx];
            }
        }
    } else {
        uint32_t pitch = svga_pitch ? svga_pitch : (uint32_t)scr_w * 4;
        for (int py = 0; py < scr_h; py++) {
            int sy = (int)((uint32_t)py * GFX_H / scr_h);
            const uint8_t *row = fb + (uint32_t)sy * GFX_W;
            volatile uint32_t *line =
                (volatile uint32_t *)(scr_mem + (uint32_t)py * pitch);
            for (int px = 0; px < scr_w; px++) {
                int sx = (int)((uint32_t)px * GFX_W / scr_w);
                line[px] = svga_pal[row[sx]];
            }
        }
    }
    if (use_svga)
        svga_fifo_update(0, 0, scr_w, scr_h);
}

void gfx_pixel(int x, int y, uint8_t color) {
    if (!gfx_on || x < 0 || x >= GFX_W || y < 0 || y >= GFX_H)
        return;
    px_screen(x, y, color);
}

void gfx_hline(int x, int y, int w, uint8_t color) {
    if (!gfx_on || y < 0 || y >= GFX_H)
        return;
    if (x < 0) { w += x; x = 0; }
    if (x >= GFX_W || w <= 0)
        return;
    if (x + w > GFX_W) w = GFX_W - x;
    for (int i = 0; i < w; i++)
        px_screen(x + i, y, color);
}

void gfx_vline(int x, int y, int h, uint8_t color) {
    if (!gfx_on || x < 0 || x >= GFX_W)
        return;
    if (y < 0) { h += y; y = 0; }
    if (y >= GFX_H || h <= 0)
        return;
    if (y + h > GFX_H) h = GFX_H - y;
    for (int i = 0; i < h; i++)
        px_screen(x, y + i, color);
}

void gfx_draw_char(int x, int y, unsigned char ch, uint8_t fg) {
    if (!gfx_on)
        return;
    const uint8_t *glyph = font_buf + (uint32_t)ch * 32;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < GFX_W && py >= 0 && py < GFX_H)
                    px_screen(px, py, fg);
            }
        }
    }
}

void gfx_draw_string(int x, int y, const char *s, uint8_t fg) {
    if (!s)
        return;
    while (*s) {
        gfx_draw_char(x, y, (unsigned char)*s, fg);
        x += 8;
        s++;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color) {
    int x2, y2;
    if (!gfx_on || w <= 0 || h <= 0)
        return;
    x2 = x + w;
    y2 = y + h;
    if (x2 <= 0 || y2 <= 0 || x >= GFX_W || y >= GFX_H)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > GFX_W) x2 = GFX_W;
    if (y2 > GFX_H) y2 = GFX_H;
    for (int yy = y; yy < y2; yy++)
        for (int xx = x; xx < x2; xx++)
            px_screen(xx, yy, color);
}

int gfx_test(void) {
    gfx_enter();
    gfx_clear(1);                       /* deep blue background */
    gfx_fill_rect(10, 20, 40, 40, 4);   /* red block    */
    gfx_fill_rect(70, 20, 40, 40, 2);   /* green block  */
    gfx_fill_rect(130, 20, 40, 40, 9);  /* bright blue  */
    gfx_fill_rect(190, 20, 40, 40, 14); /* yellow block */
    gfx_fill_rect(250, 20, 40, 40, 15); /* white block  */
    gfx_fill_rect(0, 180, 320, 20, 8);  /* gray bar at bottom */
    for (int i = 0; i < 200; i++)
        gfx_pixel(160, i, 15);          /* vertical white line */
    puts("[GFX] demo drawn: 5 blocks, gray bar, white line\n");
    return 0;
}

static int gfx_init(void) {
    gfx_on = 0;
    return 0;
}

static Driver gfx_driver = {
    .name = "gfx",
    .type = DRV_TYPE_LCD,
    .init = gfx_init,
};

int gfx_driver_register(void) {
    return driver_register(&gfx_driver);
}

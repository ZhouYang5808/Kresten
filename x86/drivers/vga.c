/* ===== x86/drivers/vga.c: VGA driver (text mode 80x25 @ 0xB8000) ===== */
#include <driver.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <io.h>

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25

static uint8_t vga_color = 0x07;
static int vga_row = 0;
static int vga_col = 0;

static void vga_set_cursor(int row, int col) {
    uint16_t pos = (uint16_t)(row * VGA_COLS + col);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)pos);
}

static void vga_scroll(void) {
    for (int row = 1; row < VGA_ROWS; row++)
        for (int col = 0; col < VGA_COLS; col++)
            VGA_MEM[(row - 1) * VGA_COLS + col] = VGA_MEM[row * VGA_COLS + col];
    uint16_t blank = (uint16_t)(0x20 | ((uint16_t)vga_color << 8));
    for (int col = 0; col < VGA_COLS; col++)
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + col] = blank;
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) vga_col--;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
    } else {
        vga_put_char(vga_col, vga_row, c, vga_color);
        vga_col++;
    }
    if (vga_col >= VGA_COLS) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_ROWS) {
        vga_scroll();
        vga_row = VGA_ROWS - 1;
    }
    vga_set_cursor(vga_row, vga_col);
}

int vga_init(void) {
    vga_clear();
    vga_row = 0;
    vga_col = 0;
    vga_set_cursor(0, 0);
    return 0;
}

void vga_clear(void) {
    uint16_t blank = (uint16_t)(0x20 | ((uint16_t)vga_color << 8));
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = blank;
}

void vga_put_char(int x, int y, char c, uint8_t color) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS)
        return;
    VGA_MEM[y * VGA_COLS + x] = (uint16_t)((uint8_t)c | ((uint16_t)color << 8));
}

int vga_test(void) {
    vga_init();
    vga_clear();
    vga_put_char(0, 0, 'K', 0x1F);
    vga_put_char(1, 0, 'r', 0x1F);
    vga_put_char(2, 0, 'e', 0x1F);
    vga_put_char(3, 0, 's', 0x1F);
    uint16_t cell = VGA_MEM[0];
    printf("VGA test: %dx%d text mode, mem=0xB8000\n", VGA_COLS, VGA_ROWS);
    printf("  cell[0]=0x%04X (expect 0x1F4B 'K' bright blue on blue)\n",
           (unsigned)cell);
    printf("  color=0x%02X\n", (unsigned)vga_color);
    if (cell == 0x1F4B)
        puts("  VGA status: OK\n");
    else
        puts("  VGA status: MISMATCH\n");
    return 0;
}

static Driver vga_driver = {
    .name = "vga",
    .type = DRV_TYPE_VGA,
    .init = vga_init,
};

int vga_driver_register(void) {
    return driver_register(&vga_driver);
}

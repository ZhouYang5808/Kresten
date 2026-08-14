ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .text : { *(.text) *(.rodata) }
    .plugins : {
        __plugins_start = .;
        *(.plugins)
        __plugins_end = .;
    }
    .crt_compressed : { INCLUDE "crt_compressed.ld" }
    .data : { *(.data) }
    .bss : {
        _bss_start = .;
        *(.bss)
        _bss_end = .;
    }
    . = 0x80000;
    .stack : {
        _stack_start = .;
        . += 0x4000;   /* 16KB 栈 */
        _stack_end = .;
    }
    . = 0x100000;
    .lcd_fb : {
        _lcd_fb_start = .;
        . += 0x96000;  /* 640x480 16bpp 显存 */
        _lcd_fb_end = .;
    }
    /* LX busybox 镜像（2.1MB）: 放到显存之后、LX_IMG_BASE(0x400000) 之前,
     * 与 guest 内存区不重叠。0x1A0000 + 0x211C28 = 0x3B1C28 < 0x400000。 */
    . = 0x1A0000;
    .lx_img : { *(.lx_img) }
    . = ALIGN(4);
    _lx_img_end = .;
}

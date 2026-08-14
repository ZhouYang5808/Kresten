ENTRY(_start)
SECTIONS
{
    . = 0x80000000;
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
    . = 0x80200000;
    .stack : {
        _stack_start = .;
        . += 0x4000;   /* 16KB 内核栈 */
        _stack_end = .;
    }
    /* LX busybox ELF 镜像 (嵌入式, ~2MB): 0x80300000 + 0x20xxxx < 0x80800000 */
    . = 0x80300000;
    .lx_img : { *(.lx_img) }
    . = ALIGN(8);
    _lx_img_end = .;
    /* 虚拟 LCD 帧缓冲 (无真实显示设备, RAM 区) */
    . = 0x80800000;
    .lcd_fb : {
        _lcd_fb_start = .;
        . += 0x96000;  /* 640x480 16bpp */
        _lcd_fb_end = .;
    }
}
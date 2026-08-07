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
}

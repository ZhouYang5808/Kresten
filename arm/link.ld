ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .text : { *(.text) *(.rodata) *(.rodata.guard) }
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
    /* 嵌入式 .kplg 表 (ARM 无光驱, 插件从内存表加载):
     * 0x1A0000 + 0x222000 (2.2MB) = 0x3C2000 < 0x400000, 不冲突。 */
    . = 0x1A0000;
    .lx_img : { *(.lx_img) }
    . = ALIGN(4);
    _lx_img_end = .;
    /* 内核导出符号表(两遍链接, 必须最后一个 section: 表大小增长不影响
     * 其它符号地址)。0x3C0000 之前, 与 LX 区 (0x400000) 不冲突。 */
    .ksymtab : {
        __ksymtab_start = .;
        KEEP(*(.ksymtab))
        __ksymtab_end = .;
    }
}

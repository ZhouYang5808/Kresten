ENTRY(_start)
SECTIONS
{
    . = 0x80000000;
    .text : { *(.text) *(.rodata) *(.rodata.guard) }
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
    /* 嵌入式 .kplg 表 (RISC-V 无光驱, 插件从内存表加载):
     * 0x80300000 + 0x222000 (2.2MB) < 0x80800000 (lcd_fb), 不冲突。 */
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
    /* 内核导出符号表 (两遍链接, 必须最后一个 section: 表大小增长不影响
     * 其它符号地址)。0x80960000 之后, 与插件区 (0x80A00000) 不冲突。 */
    . = ALIGN(8);
    .ksymtab : {
        __ksymtab_start = .;
        KEEP(*(.ksymtab))
        __ksymtab_end = .;
    }
}
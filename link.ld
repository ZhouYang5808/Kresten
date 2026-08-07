ENTRY(_start)

SECTIONS
{
    . = 1M;

    .multiboot : {
        *(.multiboot)
    }

    .text : {
        *(.text)
        *(.rodata)
    }

    .plugins : {
        __plugins_start = .;
        *(.plugins)
        __plugins_end = .;
    }

    .crt_compressed : { INCLUDE "crt_compressed.ld" }
    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
        . = ALIGN(16);
        stack_bottom = .;
        . = . + 8192;
        stack_top = .;
    }

    /* End of the kernel image: nothing in RAM above this point is touched
     * by the kernel or by the bootloader, so it survives warm reboots. */
    . = ALIGN(4096);
    _end = .;
}

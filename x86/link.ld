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

    /* LX user image region lives at 0x400000 (ET_EXEC vaddrs), growing to
     * ~0x530000 for the current busybox build.  Skip past it so ELF loading
     * can never clobber kernel .data/.bss (incl. the 960KB framebuffer). */
    . = 0x600000;
    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }

    /* End of the kernel image: nothing in RAM above this point is touched
     * by the kernel or by the bootloader, so it survives warm reboots. */
    . = ALIGN(4096);
    _end = .;

    /* BSP stack: keep out of the LX user image region (0x400000-0xE00000)
     * so page-aligned ELF loading never clobbers the running stack. */
    . = 0xE01000;
    stack_bottom = .;
    . = . + 8192;
    stack_top = .;

    /* Installer image (5MB+): must NOT live in .rodata/.text (it would push
     * .plugins past the 0x600000 .data region -> section overlap), and it
     * does not fit after .bss (only ~2MB free before the stack).  Place it
     * above the stack, well below the VBE LFB at 0xE0000000. */
    . = 0xE04000;
    .install_img : {
        KEEP(*(.install_img))
    }
}

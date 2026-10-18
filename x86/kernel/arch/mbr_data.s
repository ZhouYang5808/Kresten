/* Embeds the assembled MBR boot sector so the installer can write it. */
.section .rodata
.global mbr_bin_start
.global mbr_bin_end
mbr_bin_start:
    .incbin "build/mbr.bin"
mbr_bin_end:

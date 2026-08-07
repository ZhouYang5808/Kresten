.section .multiboot, "a"
.align 4
.long 0x1BADB002
.long 0x00000003
.long -(0x1BADB002 + 0x00000003)

.section .text
.global _start
_start:
    mov $stack_top, %esp
    pushl %ebx            /* multiboot info pointer */
    call kernel_main
    addl $4, %esp

    cli
.loop:
    hlt
    jmp .loop

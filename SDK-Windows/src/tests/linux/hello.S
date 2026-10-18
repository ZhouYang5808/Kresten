.section .text
.globl _start
_start:
    movq $1, %rax          /* syscall write */
    movq $1, %rdi          /* fd = stdout */
    leaq msg(%rip), %rsi   /* buf */
    movq $len, %rdx        /* count */
    syscall

    movq $231, %rax        /* syscall exit_group */
    xorq %rdi, %rdi        /* status = 0 */
    syscall

msg:
    .ascii "Hello from Linux on Kresten!\n"
len = . - msg

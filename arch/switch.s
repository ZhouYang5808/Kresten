.section .text
.code32

.globl context_switch
context_switch:
    movl 4(%esp), %eax
    movl 8(%esp), %edx

    movl %ebx, 20(%eax)
    movl %esi, 16(%eax)
    movl %edi, 12(%eax)
    movl %ebp, 8(%eax)
    movl %esp, 4(%eax)
    movl (%esp), %ecx
    movl %ecx, 0(%eax)

    movl 0(%edx), %ecx
    movl 4(%edx), %esp
    movl 8(%edx), %ebp
    movl 12(%edx), %edi
    movl 16(%edx), %esi
    movl 20(%edx), %ebx

    jmpl *%ecx

.globl proc_entry_wrapper
proc_entry_wrapper:
    pushl (%esp)
    call proc_entry
    addl $4, %esp
    pushl $0
    call proc_exit

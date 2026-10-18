.set MB_MAGIC,  0x1BADB002
.set MB_FLAGS,  0x00000003
.set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECK

.section .bss
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd0:
    .skip 4096
boot_pd1:
    .skip 4096
boot_pd2:
    .skip 4096
boot_pd3:
    .skip 4096

.section .data
.align 16
gdt64:
    .quad 0                       /* null */
    .quad 0x00AF9A000000FFFF      /* code64: 0x08 */
    .quad 0x00AF92000000FFFF      /* data64: 0x10 */
gdt64_end:
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.global mb_info_save
mb_info_save:
    .quad 0

pd_bases:
    .long boot_pd0
    .long boot_pd1
    .long boot_pd2
    .long boot_pd3

.section .text
.code32
.global _start
_start:
    mov $stack_top, %esp
    movl %ebx, mb_info_save

    /* check CPUID availability */
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    xorl %ecx, %eax
    jz no_longmode

    /* CPUID.80000001H:EDX.LM (bit 29) */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_longmode
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz no_longmode

    /* identity map 0..4G with 2MiB pages */
    leal boot_pdpt, %eax
    orl $0x3, %eax
    movl %eax, boot_pml4 + 0

    leal boot_pd0, %eax
    leal boot_pd1, %edx
    leal boot_pd2, %ecx
    leal boot_pd3, %esi
    orl $0x3, %eax
    orl $0x3, %edx
    orl $0x3, %ecx
    orl $0x3, %esi
    movl %eax, boot_pdpt + 0
    movl %edx, boot_pdpt + 8
    movl %ecx, boot_pdpt + 16
    movl %esi, boot_pdpt + 24

    leal pd_bases, %ebp
    movl $0x83, %ebx              /* present|rw|ps, phys = 0 */
    movl $4, %edx                 /* 4 PDs */
2:
    movl (%ebp), %edi
    movl $512, %ecx
1:
    movl %ebx, (%edi)
    addl $0x200000, %ebx
    addl $8, %edi
    decl %ecx
    jnz 1b
    addl $4, %ebp
    decl %edx
    jnz 2b

    /* PAE */
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    /* EFER.LME */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    /* CR3 + PG */
    movl $boot_pml4, %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode

.code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    mov $stack_top, %rsp
    xor %rbp, %rbp
    movq mb_info_save(%rip), %rdi
    call kernel_main

.hlt_loop:
    cli
    hlt
    jmp .hlt_loop

no_longmode:
    mov $0x3f8, %dx
    mov $'X', %al
    out %al, %dx
1:
    cli
    hlt
    jmp 1b

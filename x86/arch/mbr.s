/* myos MBR boot sector: switches to protected mode and loads the install
 * image from LBA 1 via ATA PIO into 1MB, then jumps to its entry point.
 * Image layout (written by the installer):
 *   0: 8-byte magic "MYOSBOOT"
 *   8: u32 total image size (header + payload)
 *   12: u32 entry point
 *   16: raw kernel (objcopy -O binary of kernel.elf, starts at 1MB) */
.equ LOAD, 0x7C00

.code16
.section .text
.global _start
_start:
    cli
    movw $0x07C0, %ax
    movw %ax, %ds
    xorw %ax, %ax
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    movb %dl, drive

    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92

    lgdt gdtr
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $pmode + LOAD

.code32
pmode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x9F000, %esp

    movb $0x50, 0xB8000
    movb $0x07, 0xB8001

    movl $0x8000, %edi
    movl $1, %esi
    movl $8, %ecx
    call ata_read
    cmpl $0x534F594D, 0x8000
    jne err32
    movb $0x48, 0xB8002
    movb $0x07, 0xB8003
    movl 0x8008, %edx
    addl $511, %edx
    shrl $9, %edx
    movl $0x100000, %edi
    movl $1, %esi
    movl %edx, %ecx
    call ata_read
    movb $0x52, 0xB8004
    movb $0x07, 0xB8005

    movl 0x8008, %ecx
    subl $16, %ecx
    shrl $2, %ecx
    movl $0x100010, %esi
    movl $0x100000, %edi
3:  movl (%esi), %eax
    movl %eax, (%edi)
    addl $4, %esi
    addl $4, %edi
    decl %ecx
    jnz 3b
    movb $0x4D, 0xB8006
    movb $0x07, 0xB8007
    movl $0x2000, %edi
    xorl %eax, %eax
    movl $32, %ecx
2:  movl %eax, (%edi)
    addl $4, %edi
    decl %ecx
    jnz 2b
    movl $0x40, 0x2000
    movl $0x2080, 0x2030
    movl $24, 0x202C
    movl $20, 0x2080
    movl $0x100000, 0x2084
    movl $0, 0x2088
    movl $0x200000, 0x208C
    movl $0, 0x2090
    movl $1, 0x2094
    movl $0x2000, %ebx
    movl 0x800C, %eax
    jmp *%eax

err32:
    cli
1:  hlt
    jmp 1b

/* ata_read: esi=lba, ecx=sector count, edi=dest */
ata_read:
    pushl %ebx
    pushl %edx
1:
    testl %ecx, %ecx
    jz 8f
    movl %esi, %eax
    shrl $24, %eax
    andl $0x0F, %eax
    orl $0xE0, %eax
    movl $0x1F6, %edx
    outb %al, %dx
    movb $1, %al
    movl $0x1F2, %edx
    outb %al, %dx
    movl %esi, %eax
    movl $0x1F3, %edx
    outb %al, %dx
    movl %esi, %eax
    shrl $8, %eax
    movl $0x1F4, %edx
    outb %al, %dx
    movl %esi, %eax
    shrl $16, %eax
    movl $0x1F5, %edx
    outb %al, %dx
    movb $0x20, %al
    movl $0x1F7, %edx
    outb %al, %dx

    xorl %ebx, %ebx
2:
    movl $0x1F7, %edx
    inb %dx, %al
    testb $0x08, %al
    jnz 3f
    testb $0x01, %al
    jnz err32
    incl %ebx
    cmpl $0xFFFFFF, %ebx
    jb 2b
    jmp err32

3:
    movl $256, %ebx
    movl $0x1F0, %edx
4:
    inw %dx, %ax
    movw %ax, (%edi)
    addl $2, %edi
    decl %ebx
    jnz 4b
    incl %esi
    decl %ecx
    jmp 1b
8:
    popl %edx
    popl %ebx
    ret

.align 8
gdt:
    .long 0, 0
    .long 0x0000FFFF, 0x00CF9A00
    .long 0x0000FFFF, 0x00CF9200
gdtr:
    .word 23
    .long gdt + LOAD
drive:
    .byte 0

.org 510
.word 0xAA55

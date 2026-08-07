.section .text
.globl _start
_start:
    // 关中断
    mrs r0, cpsr
    orr r0, r0, #0xC0
    msr cpsr, r0
    
    // 设置栈指针（使用 16KB 栈的顶部）
    ldr sp, =0x44000
    
    // 清空 BSS
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
bss_loop:
    cmp r0, r1
    strcc r2, [r0], #4
    bcc bss_loop
    
    // 调用 C 的 kernel_main
    bl kernel_main
    
hang:
    b hang

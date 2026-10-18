/* kernel/lx_bridge.c (ARM): LX 子系统桥(内核侧)。
 * boot/start.s 的 undef/svc 异常入口经函数指针转到插件实现
 * (linux.kplg 提供并自注册: 排在 vm 之后加载, init 里写指针)。
 * lx_save_sp/lx_trap_stack 也在此: start.s 与插件裸汇编共用,
 * 插件侧引用自动经 kernel_symtab 解析到内核定义。 */

#include <stdint.h>

uint32_t lx_save_sp = 0;                       /* svc 时保存的内核 sp */
uint8_t lx_trap_stack[8192] __attribute__((aligned(16)));  /* svc 陷阱栈 */

/* 函数指针(初值 0): 注册前若发生 undef/svc, start.s 判 0 挂起。
 * linux.kplg 把实现函数改名 _impl 后由自己的 init 写入。 */
int (*lx_undef_dispatch)(uint32_t lr, uint32_t frame);
long (*linux_syscall_dispatch)(long num, long a1, long a2, long a3, long a4, long a5);

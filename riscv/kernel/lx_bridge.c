/* kernel/lx_bridge.c (RISC-V): LX 子系统桥(内核侧)。
 * boot/start.S 的 ecall/异常入口经函数指针转到插件实现
 * (linux.kplg 提供并自注册: 排在 vm 之后加载, init 里写指针)。
 * lx_trap_stack/lx_resume_* 也在此: start.S 与插件共用,
 * 插件侧引用自动经 kernel_symtab 解析到内核定义。 */

#include <stdint.h>

uint8_t lx_trap_stack[8192] __attribute__((aligned(16)));   /* ecall 陷阱栈 */
uint8_t lx_trap_stack_end[256] __attribute__((aligned(8))); /* 栈顶标记(+冗余) */

volatile uint64_t lx_resume_sp = 0;      /* guest 恢复点 (lx_proc.c 写) */
volatile uint64_t lx_resume_pc = 0;
volatile uint64_t lx_resume_ra = 0;
volatile uint64_t lx_resume_save[12];    /* s0..s11 (start.S 蹦床保存) */

/* 函数指针(初值 0): 注册前若发生 ecall/非法指令, start.S 判 0 挂起。
 * linux.kplg 把实现函数改名 _impl 后由自己的 init 写入。 */
void (*trap_unhandled)(long mcause, long mepc, long mstatus);
long (*linux_syscall_dispatch)(long num, long a1, long a2, long a3, long a4, long a5);
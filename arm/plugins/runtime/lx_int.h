#ifndef _LX_INT_H
#define _LX_INT_H

#include <stdint.h>

/* LX 内存布局（ARM, 无 MMU, 物理 = 虚拟）：
 *   0x0040_0000  程序镜像区（ELF32 PT_LOAD 段按 vaddr 直载; busybox 以
 *                -Wl,-Ttext-segment=0x400000 链接, 段 0x400000 / 0x61DB00）
 *   0x00E0_0000  kuser 补丁桩（get_tls / cmpxchg stub）
 *   0x0100_0000  brk 池
 *   0x0140_0000  mmap 池
 *   0x0180_0000  LX 进程主栈
 * 均落在内核镜像之上、128MB RAM（0x0800_0000）之内。 */
#define LX_IMG_BASE   0x00400000u
#define LX_IMG_END    0x00E00000u
#define LX_BRK_BASE   0x01000000u
#define LX_BRK_SIZE   (16u * 1024 * 1024)
#define LX_MMAP_BASE  0x01400000u
#define LX_MMAP_SIZE  (16u * 1024 * 1024)
#define LX_STACK_BASE 0x01800000u
#define LX_STACK_SIZE (256u * 1024)

/* kuser 补丁桩: 镜像加载后扫描修改
 *  - 指令序列 mvn r0,#0xF000; [mov lr,pc]; sub pc,r0,#31 (跳 0xffff0fe0
 *    get_tls) 改写为 mrc p15,0,r0,c13,c0,3; [mov lr,pc]; mov pc,lr,
 *    由 undef_handler 模拟 c13 读 (r0 = lx_tls_slot)。
 *  - 0xffff0fc0 字面量 (cmpxchg) 替换为 LX_STUB_CMPXCHG 桩地址。
 *  - 0xffff0fa0 字面量 (memory barrier) 替换为 LX_STUB_MEMBAR 桩地址。
 * get_tls 桩: r0 = lx_tls_slot 的值; cmpxchg 桩: r2=ptr r0=old r1=new
 *            -> r0=0/-1 (单核+中断全关, 无需锁; busybox 的 glibc 用
 *            ldr r3,[pc,#imm]; blx r3 调用, 指针在 r2 而非内核惯例 r0);
 * membar 桩: ARM926 (v5TE 单核) 无 dmb, 空操作直接返回 */
#define LX_STUB_GETTLS  (LX_IMG_END + 0x0000u)
#define LX_STUB_CMPXCHG (LX_IMG_END + 0x0020u)
#define LX_STUB_MEMBAR  (LX_IMG_END + 0x0040u)

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32Hdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32Phdr;

#define LX_PT_LOAD 1

typedef struct {
    const char *name;
    const uint8_t *elf;
    uint32_t size;
} LxTest;

/* lx_syscall.c */
extern long (*linux_syscall_dispatch)(long num, long a1, long a2, long a3, long a4, long a5);
extern int (*lx_undef_dispatch)(uint32_t lr, uint32_t frame);   /* 内核 lx_bridge.c 桥指针 */
extern uint32_t lx_save_sp;                 /* svc 时保存的内核 sp */
extern uint8_t lx_trap_stack[];             /* svc 陷阱栈 */
extern volatile uint32_t lx_resume_sp;      /* guest 恢复点 sp/pc */
extern volatile uint32_t lx_resume_pc;
extern void lx_resume_helper(void);         /* exit_group 恢复入口 */

/* lx_elf.c */
int lx_elf_load(const uint8_t *data, uint32_t size, uint32_t *entry_out, const char *name);

/* lx_syscall.c: 会话级重置 (brk/mmap 游标) */
void lx_session_reset(void);

/* lx_proc.c */
extern void lx_jump_to_entry(uint32_t entry, uint32_t rsp, uint32_t argc, uint32_t argv);
int lx_run_test(const LxTest *t, int argc, char **argv);
extern volatile uint32_t lx_entry;
extern volatile int lx_finished;

#endif
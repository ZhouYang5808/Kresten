#ifndef _LX_INT_H
#define _LX_INT_H

#include <stdint.h>

/* LX 内存布局（identity 映射，物理 = 虚拟）：
 *   0x0040_0000  程序镜像区（ELF PT_LOAD 段按 vaddr 直载）
 *   0x0100_0000  brk 池
 *   0x0140_0000  mmap 池（v0: 16MB; wine 运行时 0x1000_0000 256MB）
 *   0x0180_0000  LX 进程主栈
 *  wine 运行时（tiny ISO wine-tiny.iso + ld.so 动态链接）：
 *   0x0E00_0000  wine loader (ET_DYN) 装载基
 *   0x0E40_0000  ld-linux-x86-64.so.2 装载基
 *   0x1000_0000  mmap 池 2GB（ld.so/libc/wine *.so 由 ld.so 映射于此;
 *               glibc NOREPLACE 探测从 0x8000_0000 起向下找槽, 池需覆盖
 *               到 0x9000_0000 为止, 低于 VBE LFB 0xE000_0000） */
#define LX_IMG_BASE   0x00400000ULL
#define LX_IMG_END    0x00E00000ULL
#define LX_BRK_BASE   0x01000000ULL
#define LX_BRK_SIZE   (16ULL * 1024 * 1024)
#define LX_MMAP_BASE  0x10000000ULL
#define LX_MMAP_SIZE  (0x80000000ULL)   /* 0x1000_0000..0x9000_0000 */
#define LX_STACK_BASE 0x01800000ULL
#define LX_STACK_SIZE (256ULL * 1024)

#define LX_WINE_MAIN_BASE 0x0E000000ULL
#define LX_WINE_LDSO_BASE 0x0E400000ULL

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf64Hdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) Elf64Phdr;

#define LX_PT_LOAD 1
#define LX_PT_DYNAMIC 2

typedef struct {
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    };
} Elf64Dyn;

typedef struct {
    const char *name;
    const uint8_t *elf;
    uint32_t size;
} LxTest;

/* lx_syscall.c */
extern void linux_syscall_entry(void);
extern long linux_syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5, long a6);
/* module-internal (asm RIP-relative addressing, hidden from the dynamic
 * symbol table so -shared can bind them locally) */
__attribute__((visibility("hidden"))) extern uint64_t linux_user_rsp;
__attribute__((visibility("hidden"))) extern uint64_t linux_ret_rip;
__attribute__((visibility("hidden"))) extern uint64_t linux_ret_rflags;
__attribute__((visibility("hidden"))) extern uint8_t lx_trap_stack[];

/* lx_elf.c */
int lx_elf_load(const uint8_t *data, uint32_t size, uint64_t *entry_out,
                uint64_t base, const char *name, int relocate);
int lx_elf_interp(const uint8_t *data, uint32_t size, char *interp, uint32_t cap);

/* lx_syscall.c: wine 真实路径 -> tiny ISO 路径 (wine_iso_map 查表) */
int lx_wine_iso_path(const char *real, char *out, int cap);

/* lx_syscall.c: 会话级重置 (fd/socket/mmap 表) */
void lx_session_reset(void);

/* lx_proc.c */
extern void lx_jump_to_entry(uint64_t entry, uint64_t rsp, uint64_t argc, uint64_t argv);
void lx_thread_main(void);
int lx_run_test(const LxTest *t, int argc, char **argv);
int lx_run_wine(int argc, char **argv);
extern volatile uint64_t lx_entry;
extern volatile int lx_finished;

#endif

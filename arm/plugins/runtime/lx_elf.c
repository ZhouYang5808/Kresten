#include "lx_int.h"
#include <stdint.h>

static uint32_t lx_dst(void)
{
    return LX_IMG_BASE;
}

void lx_install_stubs(void);

/* ARM EABI: 镜像按 vaddr 直达 LX_IMG 段, 无重定位(静态链接固定地址)。 */
int lx_elf_load(const uint8_t *data, uint32_t size, uint32_t *entry_out, const char *name)
{
    const Elf32Hdr *h;
    const Elf32Phdr *ph;
    uint32_t i, end = 0;

    (void)name;
    if (!data || size < sizeof(Elf32Hdr))
        return -1;
    h = (const Elf32Hdr *)data;
    if (h->e_ident[0] != 0x7f || h->e_ident[1] != 'E' || h->e_ident[2] != 'L' ||
        h->e_ident[3] != 'F' || h->e_type != 2 || h->e_machine != 40 /* EM_ARM */)
        return -1;
    if (h->e_phoff + (uint32_t)h->e_phnum * sizeof(Elf32Phdr) > size)
        return -1;

    ph = (const Elf32Phdr *)(data + h->e_phoff);
    for (i = 0; i < h->e_phnum; i++) {
        const Elf32Phdr *p = &ph[i];
        if (p->p_type != LX_PT_LOAD)
            continue;
        if (p->p_vaddr < LX_IMG_BASE || p->p_vaddr - LX_IMG_BASE + p->p_memsz > LX_IMG_END - LX_IMG_BASE)
            return -1;
        if (p->p_offset + p->p_filesz > size)
            return -1;
        {
            const char *s = (const char *)(data + p->p_offset);
            volatile char *d = (volatile char *)(lx_dst() + (p->p_vaddr - LX_IMG_BASE));
            uint32_t n = p->p_filesz;
            while (n--)
                *d++ = *s++;
        }
        if (p->p_memsz > p->p_filesz) {
            volatile char *d = (volatile char *)(lx_dst() + (p->p_vaddr - LX_IMG_BASE) + p->p_filesz);
            uint32_t n = p->p_memsz - p->p_filesz;
            while (n--)
                *d++ = 0;
        }
        if (p->p_vaddr + p->p_memsz > end)
            end = p->p_vaddr + p->p_memsz;
    }
    if (entry_out)
        *entry_out = h->e_entry;

    /* kuser 补丁: glibc/musl 的 __aeabi_read_tp 用指令
     *   mvn r0,#0xF000; [mov lr,pc]; sub pc,r0,#31
     * 直接跳到 0xffff0fe0 (kuser get_tls); cmpxchg 用
     *   ldr rX,[pc,#imm] 载入 0xffff0fc0 字面量后 blx rX;
     * memory barrier 用 ldr r3,[pc,#0]; bx r3 载入 0xffff0fa0。
     * QEMU 系统模拟不提供 kuser 页 (读回 0, PC 滑到 0 -> 复位)。
     * 对策:
     *   a) mvn+subpc 指令对改写为 mrc p15,0,r0,c13,c0,3; mov pc,lr
     *      (undef_handler 模拟 mrc c13, r0 = lx_tls_slot);
     *   b) 0xffff0fc0 / 0xffff0fa0 字面量替换为对应桩地址。 */
    lx_install_stubs();
    {
        volatile uint32_t *w = (volatile uint32_t *)LX_IMG_BASE;
        uint32_t n = (end - LX_IMG_BASE) / 4;
        for (i = 0; i + 1 < n; i++) {
            uint32_t a = w[i], b = w[i + 1];
            if (a == 0xE3E00A0Fu) {                 /* mvn r0, #0xF000 */
                if (b == 0xE240F01Fu) {             /* sub pc, r0, #31 */
                    w[i] = 0xEE1D0F70u;             /* mrc p15,0,r0,c13,c0,3 */
                    w[i + 1] = 0xE1A0F00Eu;         /* mov pc, lr */
                } else if (b == 0xE1A0E00Fu && i + 2 < n &&
                           w[i + 2] == 0xE240F01Fu) {
                    /* mvn r0,#0xF000; mov lr,pc; sub pc,r0,#31 */
                    w[i] = 0xEE1D0F70u;             /* mrc p15,0,r0,c13,c0,3 */
                    w[i + 2] = 0xE1A0F00Eu;         /* mov pc, lr */
                }
            } else if (a == 0xffff0fc0u) {          /* kuser cmpxchg 字面量 */
                w[i] = LX_STUB_CMPXCHG;
            } else if (a == 0xffff0fa0u) {          /* kuser memory barrier 字面量 */
                w[i] = LX_STUB_MEMBAR;
            }
        }
    }
    return 0;
}

/* ARM926EJ-S (ARMv5TE) 无 TPIDRURO, get_tls 桩读 RAM 模拟槽 */
extern uint32_t lx_tls_slot;

/* 一次性安装两个 kuser 桩 (写 RAM 固定地址)。 */
void lx_install_stubs(void)
{
    volatile uint32_t *w;

    /* get_tls: 返回 lx_tls_slot 的值 (glibc __aeabi_read_tp 期望 r0 = TP) */
    w = (volatile uint32_t *)LX_STUB_GETTLS;
    w[0] = 0xe59f0004u;   /* ldr r0, [pc, #4]          -> r0 = w[3] = &lx_tls_slot */
    w[1] = 0xe5900000u;   /* ldr r0, [r0]              -> r0 = lx_tls_slot 的值 */
    w[2] = 0xe12fff1eu;   /* bx lr                     */
    w[3] = (uint32_t)(uintptr_t)&lx_tls_slot;

    w = (volatile uint32_t *)LX_STUB_CMPXCHG;
    w[0] = 0xe5923000u;      /* ldr r3,[r2]        r2 = ptr */
    w[1] = 0xe1530000u;      /* cmp r3,r0          r0 = old */
    w[2] = 0x13a000ffu;      /* movne r0,#-1 */
    w[3] = 0x11a0f00eu;      /* movne pc,lr */
    w[4] = 0xe5821000u;      /* str r1,[r2]        r1 = new */
    w[5] = 0xe3a00000u;      /* mov r0,#0 */
    w[6] = 0xe12fff1eu;      /* mov pc,lr */
    w[7] = 0xe1a00000u;      /* nop */

    /* memory barrier: ARM926 (v5TE, 单核顺序执行) 无 dmb, 空操作直接返回 */
    w = (volatile uint32_t *)LX_STUB_MEMBAR;
    w[0] = 0xe12fff1eu;      /* bx lr */
    w[1] = 0xe1a00000u;      /* nop */
}
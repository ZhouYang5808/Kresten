#include "lx_int.h"
#include <stdint.h>

/* RISC-V RV64 ELF 加载器: busybox 是 ET_EXEC, 固定链接于 0x81000000,
 * 与 LX_IMG_BASE 重合, 按 vaddr 直载 (无重定位, 无 MMU)。 */

int lx_elf_load(const uint8_t *data, uint32_t size, uint64_t *entry_out, const char *name)
{
    const Elf64Hdr *h;
    const Elf64Phdr *ph;
    uint32_t i;
    uint64_t end = 0;

    (void)name;
    if (!data || size < sizeof(Elf64Hdr))
        return -1;
    h = (const Elf64Hdr *)data;
    if (h->e_ident[0] != 0x7f || h->e_ident[1] != 'E' || h->e_ident[2] != 'L' ||
        h->e_ident[3] != 'F' || h->e_type != 2 || h->e_machine != 243 /* EM_RISCV */)
        return -1;
    if (h->e_phoff + (uint64_t)h->e_phnum * sizeof(Elf64Phdr) > size)
        return -1;

    ph = (const Elf64Phdr *)(data + h->e_phoff);
    for (i = 0; i < h->e_phnum; i++) {
        const Elf64Phdr *p = &ph[i];
        if (p->p_type != LX_PT_LOAD)
            continue;
        if (p->p_vaddr < LX_IMG_BASE || p->p_vaddr - LX_IMG_BASE + p->p_memsz > LX_IMG_END - LX_IMG_BASE)
            return -1;
        if (p->p_offset + p->p_filesz > size)
            return -1;
        {
            const char *s = (const char *)(data + p->p_offset);
            volatile char *d = (volatile char *)(uintptr_t)p->p_vaddr;
            uint64_t n = p->p_filesz;
            while (n--)
                *d++ = *s++;
        }
        if (p->p_memsz > p->p_filesz) {
            volatile char *d = (volatile char *)(uintptr_t)(p->p_vaddr + p->p_filesz);
            uint64_t n = p->p_memsz - p->p_filesz;
            while (n--)
                *d++ = 0;
        }
        if (p->p_vaddr + p->p_memsz > end)
            end = p->p_vaddr + p->p_memsz;
    }
    if (entry_out)
        *entry_out = h->e_entry;
    return 0;
}
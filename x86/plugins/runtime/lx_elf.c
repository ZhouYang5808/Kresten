#include "lx_int.h"
#include <stdio.h>
#include <string.h>

int lx_elf_load(const uint8_t *data, uint32_t size, uint64_t *entry_out, const char *name) {
    if (!data || size < sizeof(Elf64Hdr)) {
        printf("[LX] %s: ELF too small\n", name);
        return -1;
    }
    const Elf64Hdr *hdr = (const Elf64Hdr *)data;
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') {
        printf("[LX] %s: bad magic\n", name);
        return -1;
    }
    if (hdr->e_type != 2) {                     /* ET_EXEC */
        printf("[LX] %s: not ET_EXEC (type=%u)\n", name, hdr->e_type);
        return -1;
    }
    if (hdr->e_machine != 62) {                 /* EM_X86_64 */
        printf("[LX] %s: not x86-64 (machine=%u)\n", name, hdr->e_machine);
        return -1;
    }
    if (hdr->e_phentsize < sizeof(Elf64Phdr) ||
        (uint64_t)hdr->e_phnum * hdr->e_phentsize > size ||
        hdr->e_phoff > size) {
        printf("[LX] %s: bad program headers\n", name);
        return -1;
    }

    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != LX_PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) {
            printf("[LX] %s: segment outside file\n", name);
            return -1;
        }
        if (ph->p_vaddr < LX_IMG_BASE || ph->p_vaddr + ph->p_memsz > LX_IMG_END) {
            printf("[LX] %s: segment vaddr 0x%08x out of image range\n",
                   name, (unsigned)(ph->p_vaddr >> 32), (unsigned)ph->p_vaddr);
            return -1;
        }
    }

    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != LX_PT_LOAD) continue;
        /* 按页（4KB）加载：段末页内剩余部分（.text 尾部等）也要从文件拷入，
         * 否则落入未加载内存（全 0）执行会漫步到随机位置 #UD。 */
        uint64_t seg_start = ph->p_vaddr;
        uint64_t seg_end = ph->p_vaddr + ph->p_memsz;
        uint64_t page_start = seg_start & ~0xFFFULL;
        uint64_t page_end = (seg_end + 0xFFF) & ~0xFFFULL;
        uint8_t *dst = (uint8_t *)(uintptr_t)page_start;
        for (uint64_t off = 0; off < page_end - page_start; off++) {
            uint64_t addr = page_start + off;
            uint8_t val = 0;
            if (addr >= seg_start) {
                uint64_t in_seg = addr - seg_start;
                if (in_seg < ph->p_filesz && ph->p_offset + in_seg < size)
                    val = data[ph->p_offset + in_seg];
            }
            dst[off] = val;
        }
        printf("[LX] %s: [LOAD] vaddr=0x%08x%08x memsz=%u filesz=%u\n", name,
               (unsigned)(ph->p_vaddr >> 32), (unsigned)ph->p_vaddr,
               (unsigned)ph->p_memsz, (unsigned)ph->p_filesz);
    }

    printf("[LX] %s: pre-copy data=%p size=%u phnum=%u\n", name, data, (unsigned)size, hdr->e_phnum);
    if (entry_out) *entry_out = hdr->e_entry;
    printf("[LX] %s: e_entry=0x%08x%08x size=%u\n", name,
           (unsigned)(hdr->e_entry >> 32), (unsigned)hdr->e_entry, (unsigned)size);
    return 0;
}

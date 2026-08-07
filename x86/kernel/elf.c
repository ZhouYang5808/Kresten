#include <elf.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fs.h>

int elf_load(const char *path, uint32_t *entry, uint32_t *heap) {
    char *data = fs_read_file_content(path);
    if (!data) { printf("elf: '%s' not found\n", path); return -1; }

    uint32_t data_len = (uint32_t)strlen(data);
    Elf32Header *hdr = (Elf32Header *)data;

    if (data_len < sizeof(Elf32Header)) { printf("elf: truncated header\n"); return -1; }
    if (hdr->magic != ELF_MAGIC) { printf("elf: bad magic\n"); return -1; }
    if (hdr->machine != 3) { printf("elf: not i386\n"); return -1; }
    if (hdr->type != 2) { printf("elf: not executable\n"); return -1; }
    if (hdr->phentsize < sizeof(Elf32ProgramHeader) ||
        (uint32_t)hdr->phnum * hdr->phentsize > data_len ||
        hdr->phoff > data_len) { printf("elf: bad program headers\n"); return -1; }

    uint32_t max_addr = 0;
    for (int i = 0; i < hdr->phnum; i++) {
        Elf32ProgramHeader *ph = (Elf32ProgramHeader *)(data + hdr->phoff + i * hdr->phentsize);
        if (ph->type == PT_LOAD) {
            if (ph->offset + ph->filesz > data_len) { printf("elf: segment outside file\n"); return -1; }
            uint32_t end = ph->vaddr + ph->memsz;
            if (end > max_addr) max_addr = end;
        }
    }

    uint32_t heap_start = (max_addr + 0xFFF) & ~0xFFF;

    for (int i = 0; i < hdr->phnum; i++) {
        Elf32ProgramHeader *ph = (Elf32ProgramHeader *)(data + hdr->phoff + i * hdr->phentsize);
        if (ph->type == PT_LOAD) {
            uint32_t mem = (uint32_t)malloc(ph->memsz);
            if (!mem) { printf("elf: alloc failed\n"); return -1; }
            memset((void *)mem, 0, ph->memsz);
            memcpy((void *)mem, data + ph->offset, ph->filesz);
            printf("  [LOAD] vaddr=0x%08x size=%u -> heap=0x%08x\n",
                   ph->vaddr, ph->memsz, mem);
        }
    }

    if (entry) *entry = hdr->entry;
    if (heap) *heap = heap_start;
    return 0;
}

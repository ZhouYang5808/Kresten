#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

#define ELF_MAGIC 0x464C457F

typedef struct {
    uint32_t magic;
    uint8_t  arch;
    uint8_t  endian;
    uint8_t  header_ver;
    uint8_t  abi;
    uint8_t  pad[8];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) Elf32Header;

typedef struct {
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;
} __attribute__((packed)) Elf32ProgramHeader;

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

int elf_load(const char *path, uint32_t *entry, uint32_t *heap_addr);

#endif

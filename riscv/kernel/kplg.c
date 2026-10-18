/* ===== kernel/kplg.c (RISC-V): 迷你动态链接器(可加载插件 .kplg) =====
 * .kplg = ELF64 ET_DYN (RISC-V, machine=243), 由 riscv64-linux-gnu-gcc
 * -fPIC -shared -nostdlib 生成。
 * 加载流程:
 *   1. 校验 ELF64, 解析节表(.dynsym/.dynstr/.rela.*);
 *   2. 在插件区分配空间, 按 PT_LOAD 段装载;
 *   3. 应用重定位: 当前模块自身 -> 内核导出符号(kernel_symtab) ->
 *      已加载插件模块(跨插件引用, 如 vm.kplg 引 linux.kplg 的 lx_guest);
 *   4. 解析 kplg_plugin 符号(插件导出结构), 返回给注册方。
 * 重定位类型(RISC-V psABI; -fPIC 下动态符号访问全部走 GOT/PLT):
 *   R_RISCV_RELATIVE 3  R_RISCV_GLOB_DAT 6  R_RISCV_JUMP_SLOT 5
 *   R_RISCV_64 2
 * (PCREL_HI20/LO12_I 等只出现在链接器已静态解析的 GOT 定位中,
 *  不产生对符号的动态重定位。) */

#include <plugin.h>
#include <driver.h>
#include <stdio.h>
#include <string.h>
#include "kplg.h"

#define KPLG_MAX_MODS 16

static Driver *kplg_last_kdrv = 0;

/* ---- ELF64 结构 ---- */
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
} __attribute__((packed)) KElfHdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) KElfPhdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed)) KElfShdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __attribute__((packed)) KElfSym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed)) KElfRela;

#define ELF64_R_SYM(i)  ((i) >> 32)
#define ELF64_R_TYPE(i) ((uint32_t)(i) & 0xffffffffu)

#define KSH_DYNSYM 11
#define KSH_STRTAB 3
#define KSH_RELA   4

#define R_RISCV_NONE      0
#define R_RISCV_64        2
#define R_RISCV_RELATIVE  3
#define R_RISCV_JUMP_SLOT 5
#define R_RISCV_GLOB_DAT  6

typedef struct {
    uint8_t *file;
    uint32_t file_size;
    uint8_t *base;
    const KElfSym *dynsym;
    const char *dynstr;
    uint32_t dynsym_count;
    const char *name;
} KplgMod;

static KplgMod kplg_mods[KPLG_MAX_MODS];
static int kplg_mod_count = 0;

/* ---- 插件区 bump 分配 ---- */
static uint32_t kplg_cur = KPLG_AREA_BASE;

void *kplg_alloc(uint32_t size) {
    uint32_t a = (kplg_cur + 0xFFF) & ~0xFFFu;
    if (a < KPLG_AREA_BASE || a + size > KPLG_AREA_BASE + KPLG_AREA_SIZE) {
        puts("[KPLG] plugin area exhausted\n");
        return 0;
    }
    kplg_cur = a + size;
    return (void *)(uintptr_t)a;
}

/* ---- 符号解析: 当前模块自身 -> 内核导出表 -> 已加载模块 ---- */
static void *kplg_resolve_self(const KElfSym *dynsym, const char *dynstr,
                               uint32_t count, uint8_t *base, const char *name) {
    for (uint32_t i = 0; i < count; i++) {
        if (dynsym[i].st_shndx == 0) continue;              /* undefined */
        if ((dynsym[i].st_info & 0x0F) == 0) continue;      /* STB_LOCAL */
        if (strcmp(dynstr + dynsym[i].st_name, name) == 0)
            return (void *)(uintptr_t)(base + dynsym[i].st_value);
    }
    return 0;
}

static void *kplg_resolve(const char *name) {
    for (uint32_t i = 0; i < kernel_symtab_count; i++) {
        if (strcmp(kernel_symtab[i].name, name) == 0)
            return kernel_symtab[i].addr;
    }
    for (int m = 0; m < kplg_mod_count; m++) {
        const KElfSym *s = kplg_mods[m].dynsym;
        for (uint32_t i = 0; i < kplg_mods[m].dynsym_count; i++) {
            if (s[i].st_shndx == 0) continue;              /* undefined */
            if ((s[i].st_info & 0x0F) == 0) continue;      /* STB_LOCAL */
            if (strcmp(kplg_mods[m].dynstr + s[i].st_name, name) == 0)
                return (void *)(uintptr_t)(kplg_mods[m].base + s[i].st_value);
        }
    }
    return 0;
}

int kplg_load(const uint8_t *file, uint32_t size, Plugin **out) {
    if (!file || size < sizeof(KElfHdr)) {
        puts("[KPLG] file too small\n");
        return -1;
    }
    const KElfHdr *h = (const KElfHdr *)file;
    if (h->e_ident[0] != 0x7F || h->e_ident[1] != 'E' ||
        h->e_ident[2] != 'L' || h->e_ident[3] != 'F') {
        puts("[KPLG] bad magic\n");
        return -1;
    }
    if (h->e_ident[4] != 2) {                /* ELFCLASS64 */
        puts("[KPLG] not ELF64\n");
        return -1;
    }
    if (h->e_type != 3) {                    /* ET_DYN */
        printf("[KPLG] not ET_DYN (type=%u)\n", h->e_type);
        return -1;
    }
    if (h->e_machine != 243) {
        printf("[KPLG] not RISC-V (machine=%u)\n", h->e_machine);
        return -1;
    }
    if (h->e_phentsize < sizeof(KElfPhdr) || h->e_shentsize < sizeof(KElfShdr) ||
        h->e_phoff > size || h->e_shoff > size ||
        (uint64_t)h->e_phnum * h->e_phentsize > size ||
        (uint64_t)h->e_shnum * h->e_shentsize > size) {
        puts("[KPLG] bad headers\n");
        return -1;
    }

    /* 计算装载大小 + 校验段 */
    uint64_t total = 0;
    for (int i = 0; i < h->e_phnum; i++) {
        const KElfPhdr *ph = (const KElfPhdr *)(file + h->e_phoff + (uint64_t)i * h->e_phentsize);
        if (ph->p_type != 1) continue;                   /* PT_LOAD */
        if (ph->p_offset + ph->p_filesz > size) {
            puts("[KPLG] segment outside file\n");
            return -1;
        }
        uint64_t end = ph->p_vaddr + ph->p_memsz;
        if (end > total) total = end;
    }
    total = (total + 0xFFF) & ~0xFFFu;
    if (total == 0 || total > KPLG_AREA_SIZE) {
        puts("[KPLG] bad load size\n");
        return -1;
    }

    uint8_t *base = (uint8_t *)kplg_alloc((uint32_t)total);
    if (!base) return -1;

    /* 装载 PT_LOAD 段 */
    for (int i = 0; i < h->e_phnum; i++) {
        const KElfPhdr *ph = (const KElfPhdr *)(file + h->e_phoff + (uint64_t)i * h->e_phentsize);
        if (ph->p_type != 1) continue;
        uint8_t *dst = base + ph->p_vaddr;
        memset(dst, 0, ph->p_memsz);
        memcpy(dst, file + ph->p_offset, ph->p_filesz);
    }

    /* 定位 .dynsym/.dynstr/重定位节 */
    const KElfSym *dynsym = 0;
    const char *dynstr = 0;
    uint32_t dynsym_count = 0;
    int relas = 0;
    const KElfShdr *rela_sec[8];
    uint32_t rela_sec_sz[8];

    for (int i = 0; i < h->e_shnum; i++) {
        const KElfShdr *sh = (const KElfShdr *)(file + h->e_shoff + (uint64_t)i * h->e_shentsize);
        if (sh->sh_type == KSH_DYNSYM) {
            dynsym = (const KElfSym *)(file + sh->sh_offset);
            dynsym_count = (uint32_t)(sh->sh_size / sizeof(KElfSym));
            if (sh->sh_link < h->e_shnum) {
                const KElfShdr *str = (const KElfShdr *)(file + h->e_shoff + (uint64_t)sh->sh_link * h->e_shentsize);
                dynstr = (const char *)(file + str->sh_offset);
            }
        } else if (sh->sh_type == KSH_RELA) {
            if (relas < 8) {
                rela_sec[relas] = sh;
                rela_sec_sz[relas] = (uint32_t)sh->sh_size;
                relas++;
            }
        }
    }
    if (!dynsym || !dynstr || dynsym_count == 0) {
        puts("[KPLG] no .dynsym\n");
        return -1;
    }

    /* 应用重定位 */
    for (int r = 0; r < relas; r++) {
        const KElfShdr *sh = rela_sec[r];
        int n = (int)(rela_sec_sz[r] / sizeof(KElfRela));
        const KElfRela *rl = (const KElfRela *)(file + sh->sh_offset);
        for (int i = 0; i < n; i++) {
            uint64_t type = ELF64_R_TYPE(rl[i].r_info);
            /* 重定位写目标必须落在模块装载区内, 否则越界写插件区/内核 */
            if (type != R_RISCV_NONE &&
                (rl[i].r_offset >= total || rl[i].r_offset + 8 > total)) {
                printf("[KPLG] reloc offset 0x%llx out of module\n",
                       (unsigned long long)rl[i].r_offset);
                return -1;
            }
            uint64_t loc = (uint64_t)(uintptr_t)base + rl[i].r_offset;
            if (type == R_RISCV_NONE) continue;
            if (type == R_RISCV_RELATIVE) {
                *(uint64_t *)loc = (uint64_t)(uintptr_t)base + (uint64_t)rl[i].r_addend;
                continue;
            }
            uint32_t sidx = (uint32_t)ELF64_R_SYM(rl[i].r_info);
            if (sidx >= dynsym_count) {
                printf("[KPLG] bad symbol index %u\n", sidx);
                return -1;
            }
            const char *nm = dynstr + dynsym[sidx].st_name;
            void *val = kplg_resolve_self(dynsym, dynstr, dynsym_count, base, nm);
            if (!val) val = kplg_resolve(nm);
            if (!val) {
                printf("[KPLG] unresolved symbol '%s'\n", nm);
                return -1;
            }
            uint64_t v = (uint64_t)(uintptr_t)val;
            switch (type) {
            case R_RISCV_64:        *(uint64_t *)loc = v + (uint64_t)rl[i].r_addend; break;
            case R_RISCV_GLOB_DAT:
            case R_RISCV_JUMP_SLOT: *(uint64_t *)loc = v; break;
            default:
                printf("[KPLG] unsupported reloc type %llu at %llx\n",
                       (unsigned long long)type, (unsigned long long)rl[i].r_offset);
                return -1;
            }
        }
    }

    /* 注册模块(保留文件副本供跨模块符号解析)。
     * 在解析 kplg_plugin 之前注册: 无插件结构的依赖模块(如 linux.kplg
     * 只提供 lx_guest 给 vm.kplg)也必须进入符号搜索范围。 */
    if (kplg_mod_count >= KPLG_MAX_MODS) {
        puts("[KPLG] too many modules\n");
        return -1;
    }
    uint8_t *file_copy = (uint8_t *)kplg_alloc(size);
    if (!file_copy) return -1;
    memcpy(file_copy, file, size);

    KplgMod *m = &kplg_mods[kplg_mod_count++];
    m->file = file_copy;
    m->file_size = size;
    m->base = base;
    m->dynsym = (const KElfSym *)(file_copy + ((uint8_t *)dynsym - (uint8_t *)file));
    m->dynstr = (const char *)(file_copy + ((uint8_t *)dynstr - (uint8_t *)file));
    m->dynsym_count = dynsym_count;
    m->name = "(module)";

    /* 解析插件导出结构 kplg_plugin(依赖模块可以没有) */
    Plugin *plg = 0;
    for (uint32_t i = 0; i < dynsym_count; i++) {
        if (dynsym[i].st_shndx == 0) continue;
        if (strcmp(dynstr + dynsym[i].st_name, "kplg_plugin") != 0) continue;
        plg = (Plugin *)(uintptr_t)(base + dynsym[i].st_value);
        break;
    }
    if (plg) m->name = plg->name;

    /* 解析驱动导出结构 kdrv_driver(纯驱动模块, 无 kplg_plugin) */
    kplg_last_kdrv = 0;
    for (uint32_t i = 0; i < dynsym_count; i++) {
        if (dynsym[i].st_shndx == 0) continue;
        if (strcmp(dynstr + dynsym[i].st_name, "kdrv_driver") != 0) continue;
        kplg_last_kdrv = (Driver *)(uintptr_t)(base + dynsym[i].st_value);
        break;
    }

    printf("[KPLG] loaded '%s' base=%p size=%u\n", m->name,
           (void *)base, (unsigned)total);
    *out = plg;
    return 0;
}

/* 驱动装载器取回最近一次加载模块的 Driver 结构(无则为 0) */
Driver *kplg_last_driver(void) {
    return kplg_last_kdrv;
}

/* 供 plugin.c 的 plugin_list_all 显示已加载插件 */
int kplg_mod_list(int idx, const char **name, void **base) {
    if (idx < 0 || idx >= kplg_mod_count) return -1;
    *name = kplg_mods[idx].name;
    *base = kplg_mods[idx].base;
    return 0;
}

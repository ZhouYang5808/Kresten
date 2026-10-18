#include "lx_int.h"
#include <stdio.h>
#include <string.h>

/* .rela.dyn: RELATIVE (8) + IRELATIVE (37, ifunc resolver) */
static int lx_apply_rela(const uint8_t *data, uint32_t size, uint64_t loadbase) {
    const Elf64Hdr *hdr = (const Elf64Hdr *)data;
    uint64_t rela = 0, relasz = 0;
    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != LX_PT_DYNAMIC) continue;
        if (ph->p_offset + ph->p_filesz > size) return -2;
        const Elf64Dyn *dyn = (const Elf64Dyn *)(data + ph->p_offset);
        uint32_t n = ph->p_filesz / sizeof(Elf64Dyn);
        for (uint32_t k = 0; k < n; k++) {
            if (dyn[k].d_tag == 7) rela = dyn[k].d_ptr;    /* DT_RELA   */
            if (dyn[k].d_tag == 8) relasz = dyn[k].d_val;  /* DT_RELASZ */
        }
        break;
    }
    if (!relasz || !rela) return 1;
    uint32_t done = 0;
    for (uint64_t off = 0; off + 24 <= relasz; off += 24) {
        const uint8_t *r = (const uint8_t *)(uintptr_t)(loadbase + rela + off);
        uint64_t r_offset = *(const uint64_t *)(r + 0);
        uint64_t r_info   = *(const uint64_t *)(r + 8);
        int64_t  r_addend = *(const int64_t *)(r + 16);
        uint32_t type = r_info & 0xFFFFFFFFu;
        if (type == 8) {                    /* R_X86_64_RELATIVE */
            *(uint64_t *)(uintptr_t)(loadbase + r_offset) = loadbase + (uint64_t)r_addend;
            done++;
        } else if (type == 37) {            /* R_X86_64_IRELATIVE: 调 resolver */
            uint64_t (*fn)(void) = (uint64_t (*)(void))(uintptr_t)(loadbase + (uint64_t)r_addend);
            *(uint64_t *)(uintptr_t)(loadbase + r_offset) = fn();
            done++;
        }
    }
    if (done) printf("[LX] %s: RELA applied (%u relocs)\n", "elf", (unsigned)done);
    return 0;
}

/* RELR 压缩相对重定位 (ld.so 等 glibc 库用):
 * 偶 entry = 单个待重定位地址 (同时作后续位图的 base);
 * 奇 entry = 位图: bit i (i>=1) 决定 base 起第 i 个字是否重定位。 */
static int lx_apply_relr(const uint8_t *data, uint32_t size, uint64_t loadbase) {
    const Elf64Hdr *hdr = (const Elf64Hdr *)data;
    uint64_t relr = 0, relrsz = 0, relrent = 8;
    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != LX_PT_DYNAMIC) continue;      /* PT_DYNAMIC */
        if (ph->p_offset + ph->p_filesz > size) return -2;
        const Elf64Dyn *dyn = (const Elf64Dyn *)(data + ph->p_offset);
        uint32_t n = ph->p_filesz / sizeof(Elf64Dyn);
        for (uint32_t k = 0; k < n; k++) {
            switch (dyn[k].d_tag) {
            case 35: relrsz = dyn[k].d_val; break;    /* DT_RELRSZ */
            case 36: relr   = dyn[k].d_ptr; break;    /* DT_RELR   */
            case 37: relrent = dyn[k].d_val; break;   /* DT_RELRENT */
            }
        }
        break;
    }
    if (!relrsz || !relr) return 1;         /* 无 RELR */
    if (relrent != 8) return -3;
    uint64_t *rp = (uint64_t *)(uintptr_t)(loadbase + relr);
    uint64_t n = relrsz / 8;
    uint64_t done = 0;
    uint64_t *place = NULL;
    for (uint64_t k = 0; k < n; k++) {
        uint64_t e = rp[k];
        if ((e & 1) == 0) {
            place = (uint64_t *)(uintptr_t)(loadbase + e);
            *place += loadbase;
            place++;
            done++;
        } else {
            for (uint64_t *p = place, r = e >> 1; r; p++, r >>= 1)
                if (r & 1) {
                    *p += loadbase;
                    done++;
                }
            place += 63;
        }
    }
    printf("[LX] %s: RELR applied (%u relocs)\n", "elf", (unsigned)done);
    return 0;
}

/* 装载 ELF (ET_EXEC 直载 vaddr, 或 ET_DYN 装载到 base)。
 * base == 0 且 ET_DYN 时装载到 LX_IMG_BASE (busybox PIE 兼容)。
 * relocate != 0 时应用 RELR/RELA 重定位 (主程序/共享库);
 * relocate == 0 跳过 (ld.so 解释器: rtld 启动时自行重定位, 二次叠加会损坏指针)。 */
int lx_elf_load(const uint8_t *data, uint32_t size, uint64_t *entry_out,
                uint64_t base, const char *name, int relocate) {
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
    if (hdr->e_type != 2 && hdr->e_type != 3) {      /* ET_EXEC / ET_DYN */
        printf("[LX] %s: not ET_EXEC/ET_DYN (type=%u)\n", name, hdr->e_type);
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
    if (hdr->e_type == 3 && base == 0) base = LX_IMG_BASE;

    uint64_t seg_lo = ~0ULL, seg_hi = 0;
    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != LX_PT_LOAD) continue;
        if (ph->p_offset + ph->p_filesz > size) {
            printf("[LX] %s: segment outside file\n", name);
            return -1;
        }
        uint64_t lo = base + ph->p_vaddr;
        uint64_t hi = lo + ph->p_memsz;
        if (hi < lo || hi > base + 0x02000000ULL) {
            printf("[LX] %s: segment 0x%08x%08x out of base range\n",
                   name, (unsigned)(lo >> 32), (unsigned)lo);
            return -1;
        }
        if (lo < seg_lo) seg_lo = lo;
        if (hi > seg_hi) seg_hi = hi;
    }

    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != LX_PT_LOAD) continue;
        /* 按页（4KB）加载：段末页内剩余部分（.text 尾部等）也要从文件拷入，
         * 否则落入未加载内存（全 0）执行会漫步到随机位置 #UD。 */
        uint64_t seg_start = base + ph->p_vaddr;
        uint64_t seg_end = base + ph->p_vaddr + ph->p_memsz;
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
        printf("[LX] %s: [LOAD] base=0x%08x seg=0x%08x..%08x memsz=%u filesz=%u\n",
               name, (unsigned)(base >> 32), (unsigned)ph->p_vaddr,
               (unsigned)(ph->p_vaddr + ph->p_memsz),
               (unsigned)ph->p_memsz, (unsigned)ph->p_filesz);
    }

    printf("[LX] %s: pre-copy data=%p size=%u phnum=%u\n", name, data, (unsigned)size, hdr->e_phnum);
    if (entry_out) *entry_out = hdr->e_entry + base;
    printf("[LX] %s: e_entry=0x%08x%08x size=%u\n", name,
           (unsigned)(hdr->e_entry >> 32), (unsigned)(hdr->e_entry + base), (unsigned)size);
    if (relocate) {
        lx_apply_relr(data, size, base);
        lx_apply_rela(data, size, base);
    } else {
        printf("[LX] %s: relocs skipped (interp self-relocates)\n", name);
    }
    return 0;
}

/* 读 PT_INTERP; 返回 0 无 INTERP, 1 有 (路径写入 interp, cap 字节) */
int lx_elf_interp(const uint8_t *data, uint32_t size, char *interp, uint32_t cap) {
    if (!data || size < sizeof(Elf64Hdr)) return 0;
    const Elf64Hdr *hdr = (const Elf64Hdr *)data;
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') return 0;
    if (hdr->e_phentsize < sizeof(Elf64Phdr) ||
        (uint64_t)hdr->e_phnum * hdr->e_phentsize > size ||
        hdr->e_phoff > size) return 0;
    for (int i = 0; i < hdr->e_phnum; i++) {
        const Elf64Phdr *ph = (const Elf64Phdr *)
            (data + hdr->e_phoff + (uint64_t)i * hdr->e_phentsize);
        if (ph->p_type != 3) continue;      /* PT_INTERP */
        uint32_t n = ph->p_filesz < cap - 1 ? ph->p_filesz : cap - 1;
        if (ph->p_offset + n > size) return 0;
        memcpy(interp, data + ph->p_offset, n);
        interp[n] = '\0';
        return 1;
    }
    return 0;
}
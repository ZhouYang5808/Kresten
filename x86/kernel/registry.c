/* ===== x86/registry.c: registry stored in a text file (C:\REGISTRY.TXT) =====
 *
 * The registry keeps key=value pairs in memory and persists them to a plain
 * text file on the filesystem. Format (one entry per line):
 *
 *   # MyOS Registry
 *   System.Version=0.3
 *   Shell.Prompt=C:\> 
 *
 * Lines starting with '#' are comments. Every reg_set()/reg_del() rewrites
 * the file, so "type REGISTRY.TXT" always reflects the current registry.
 *
 * Persistence across warm restarts: the text is also mirrored to a RAM
 * region near the top of the highest free memory region reported by the
 * multiboot memory map (reg_persist_setup, called before reg_init),
 * guarded by a magic. Bootloaders allocate low memory, so the top of RAM
 * is not touched by them, and the kernel never allocates there, so
 * settings like Sched.Mode survive warm reboots (QEMU system_reset,
 * VMware reset). A cold power cycle clears it (no disk support yet).
 */

#include <registry.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fs.h>

#define REG_PERSIST_MAX   4092
#define REG_PERSIST_MAGIC 0x31474552  /* "REG1" */

static char *reg_keys[REG_MAX_KEYS];
static char *reg_vals[REG_MAX_KEYS];
static int reg_count = 0;
static int reg_loaded = 0;

static uint32_t persist_addr = 0;

/* Parse the multiboot v1 memory map and pick a safe persistence region:
 * the top of the highest free region above 1 MB. Called before reg_init. */
void reg_persist_setup(uint32_t mb_info) {
    persist_addr = 0;
    uint32_t best_base = 0, best_len = 0;
    if (mb_info) {
        uint32_t flags = *(uint32_t *)mb_info;
        if (flags & 0x40) {
            uint32_t mmap_addr = *(uint32_t *)(mb_info + 48);
            uint32_t mmap_end = mmap_addr + *(uint32_t *)(mb_info + 44);
            uint32_t p = mmap_addr;
            while (p + 24 <= mmap_end) {
                uint32_t size = *(uint32_t *)p;
                uint32_t base = *(uint32_t *)(p + 4);
                uint32_t len = *(uint32_t *)(p + 12);
                uint32_t type = *(uint32_t *)(p + 20);
                if (type == 1 && base >= 0x100000 && len >= 0x10000) {
                    if (base > best_base) {
                        best_base = base;
                        best_len = len;
                    }
                }
                if (size < 20) break;
                p += size + 4;
            }
        }
    }
    if (best_len >= 0x10000)
        persist_addr = (best_base + best_len / 2) & ~0xFFFu;
    if (persist_addr)
        printf("[REGISTRY] persist region @ 0x%x\n", persist_addr);
    else
        printf("[REGISTRY] no persist region (mmap unavailable)\n");
}

static char *persist_base(void) {
    return (char *)persist_addr;
}

static int persist_valid(void) {
    return *(unsigned int *)persist_base() == REG_PERSIST_MAGIC;
}

static const char *persist_text(void) {
    return persist_base() + 4;
}

static void persist_save(const char *text) {
    unsigned int *magic = (unsigned int *)persist_base();
    char *dst = persist_base() + 4;
    strncpy(dst, text, REG_PERSIST_MAX - 1);
    dst[REG_PERSIST_MAX - 1] = '\0';
    *magic = REG_PERSIST_MAGIC;
}

/* Set without touching the file (used while loading) */
static void reg_set_impl(const char *key, const char *val) {
    for (int i = 0; i < reg_count; i++) {
        if (strcmp(reg_keys[i], key) == 0) {
            free(reg_vals[i]);
            reg_vals[i] = strdup(val ? val : "");
            return;
        }
    }
    if (reg_count >= REG_MAX_KEYS) return;
    reg_keys[reg_count] = strdup(key);
    reg_vals[reg_count] = strdup(val ? val : "");
    reg_count++;
}

static void reg_parse_line(const char *line) {
    if (!line || !*line || *line == '#') return;
    const char *eq = strchr(line, '=');
    if (!eq) return;
    int kl = (int)(eq - line);
    if (kl <= 0 || kl > REG_KEY_MAX) return;
    char key[REG_KEY_MAX + 1];
    memcpy(key, line, kl);
    key[kl] = '\0';
    char val[REG_VAL_MAX + 1];
    strncpy(val, eq + 1, REG_VAL_MAX);
    val[REG_VAL_MAX] = '\0';
    int vl = strlen(val);
    while (vl > 0 && (val[vl - 1] == '\r' || val[vl - 1] == '\n'))
        val[--vl] = '\0';
    reg_set_impl(key, val);
}

void reg_init(void) {
    if (reg_loaded) return;
    reg_loaded = 1;
    reg_count = 0;

    char *content = 0;
    if (persist_valid()) {
        const char *pt = persist_text();
        if (fs_find_entry(REG_FILE))
            fs_update_file(REG_FILE, pt);
        else
            fs_create_file(REG_FILE, pt);
    }
    content = fs_read_file_content(REG_FILE);
    if (content) {
        static char parse_buf[4096];
        strncpy(parse_buf, content, 4095);
        parse_buf[4095] = '\0';
        char *line = strtok(parse_buf, "\n");
        while (line) {
            reg_parse_line(line);
            line = strtok(0, "\n");
        }
        reg_save();
        printf("[REGISTRY] loaded %d entries from %s%s\n", reg_count, REG_FILE,
               persist_valid() ? " (persisted)" : "");
    } else {
        reg_set_impl("System.Version", "0.3");
        reg_set_impl("System.Build", "20260803");
        reg_set_impl("System.Author", "MyOS");
        reg_set_impl("System.TickMs", "10");
        reg_set_impl("Shell.Prompt", "C:\\> ");
        reg_set_impl("Shell.History", "8");
        reg_set_impl("Sched.Print", "0");
        reg_set_impl("Sched.Mode", "preempt");
        reg_save();
        printf("[REGISTRY] created %s with defaults\n", REG_FILE);
    }
}

const char *reg_get(const char *key) {
    if (!key) return 0;
    for (int i = 0; i < reg_count; i++)
        if (strcmp(reg_keys[i], key) == 0)
            return reg_vals[i];
    return 0;
}

int reg_set(const char *key, const char *val) {
    if (!key || !*key) return -1;
    reg_set_impl(key, val);
    return reg_save();
}

int reg_del(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < reg_count; i++) {
        if (strcmp(reg_keys[i], key) == 0) {
            free(reg_keys[i]);
            free(reg_vals[i]);
            for (int j = i; j < reg_count - 1; j++) {
                reg_keys[j] = reg_keys[j + 1];
                reg_vals[j] = reg_vals[j + 1];
            }
            reg_count--;
            return reg_save();
        }
    }
    return -1;
}

void reg_list(void) {
    puts("\n=== Registry (C:\\REGISTRY.TXT) ===\n");
    for (int i = 0; i < reg_count; i++)
        printf("  %s = %s\n", reg_keys[i], reg_vals[i]);
    printf("\n  %d entries\n\n", reg_count);
}

static void reg_append(char *buf, int *pos, int max, const char *s) {
    int len = strlen(s);
    if (*pos + len >= max) return;
    memcpy(buf + *pos, s, len);
    *pos += len;
    buf[*pos] = '\0';
}

int reg_save(void) {
    static char buf[8192];
    int pos = 0;
    buf[0] = '\0';
    reg_append(buf, &pos, sizeof(buf), "# MyOS Registry\n");
    reg_append(buf, &pos, sizeof(buf), "# key=value, one per line\n");
    for (int i = 0; i < reg_count; i++) {
        reg_append(buf, &pos, sizeof(buf), reg_keys[i]);
        reg_append(buf, &pos, sizeof(buf), "=");
        reg_append(buf, &pos, sizeof(buf), reg_vals[i]);
        reg_append(buf, &pos, sizeof(buf), "\n");
    }
    if (fs_find_entry(REG_FILE))
        fs_update_file(REG_FILE, buf);
    else
        fs_create_file(REG_FILE, buf);
    persist_save(buf);
    return 0;
}

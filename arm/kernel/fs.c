#include <fs.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAX_DRIVES 8
#define MAX_ENTRIES 64

static DiskDrive drives[MAX_DRIVES];
static int drive_count = 0;

typedef struct {
    FileEntry entry;
    char *content;
    int owned;
} FSEntry;

static FSEntry entries[MAX_ENTRIES];
static int entry_count = 0;

/* Current working directory, format: X:\path\subpath (no trailing backslash except root "X:\") */
static char cwd[64] = "C:\\";

static void fill_now(FileDate *d) {
    d->year = 2025; d->month = 7; d->day = 26;
    d->hour = 12; d->minute = 0; d->second = 0;
}

static int add_entry(const char *name, uint8_t type, uint32_t size,
                     const char *content) {
    if (entry_count >= MAX_ENTRIES) return -1;
    FSEntry *r = &entries[entry_count++];
    strncpy(r->entry.name, name, 31);
    r->entry.name[31] = '\0';
    r->entry.size = size;
    r->entry.flags = 0;
    r->entry.type = type;
    fill_now(&r->entry.create);
    fill_now(&r->entry.modify);
    if (content) {
        r->content = strdup(content);
        r->owned = 1;
    } else {
        r->content = 0;
        r->owned = 0;
    }
    return 0;
}

/* Ensure a path has a trailing backslash, writing into dst (size 64) */
static void ensure_trailing(const char *src, char *dst) {
    strncpy(dst, src, 63);
    dst[63] = '\0';
    int len = strlen(dst);
    if (len > 0 && dst[len - 1] != '\\') {
        dst[len] = '\\';
        dst[len + 1] = '\0';
    }
}

int fs_init(void) {
    drive_count = 0;
    entry_count = 0;
    strcpy(cwd, "C:\\");

    /* Create drive C: */
    fs_mount('C', 0, 0);

    /* Create root directory as a directory entry */
    add_entry("C:\\", FS_TYPE_DIR, 0, 0);
    add_entry("C:\\README.txt", FS_TYPE_FILE, 85,
              "MyOS v1.0 - Drive-letter based OS\n"
              "Type help for commands\n");
    add_entry("C:\\version.txt", FS_TYPE_FILE, 32,
              "MyOS Kernel v0.5\nBuild: 2025-07-27\n");
    add_entry("C:\\kernel.elf", FS_TYPE_FILE, 80532, 0);
    add_entry("C:\\shell.elf", FS_TYPE_FILE, 20480, 0);
    add_entry("C:\\config.sys", FS_TYPE_FILE, 128,
              "FILES=20\nBUFFERS=10\nSHELL=shell.elf\n");
    return 0;
}

/* ---- Drive management ---- */

DiskDrive *fs_mount(char drive_letter, uint32_t base, uint32_t size) {
    if (drive_count >= MAX_DRIVES) { puts("fs: Max drives reached\n"); return 0; }
    for (int i = 0; i < drive_count; i++)
        if (drives[i].name[0] == drive_letter) return &drives[i];
    DiskDrive *d = &drives[drive_count++];
    d->name[0] = drive_letter; d->name[1] = ':'; d->name[2] = '\0';
    d->base = base; d->size = size; d->used = 0;
    return d;
}

int fs_unmount(char drive_letter) {
    for (int i = 0; i < drive_count; i++) {
        if (drives[i].name[0] == drive_letter) {
            for (int j = i; j < drive_count - 1; j++) drives[j] = drives[j + 1];
            drive_count--; return 0;
        }
    }
    return -1;
}

DiskDrive *fs_get_drive(char drive_letter) {
    for (int i = 0; i < drive_count; i++)
        if (drives[i].name[0] == drive_letter) return &drives[i];
    return 0;
}

void fs_print_drives(void) {
    puts("\n=== Mounted Drives ===\n");
    if (drive_count == 0) { puts("  No drives mounted.\n"); return; }
    for (int i = 0; i < drive_count; i++)
        printf("  %s base=0x%08x size=%d used=%d\n",
               drives[i].name, drives[i].base, drives[i].size, drives[i].used);
    putchar('\n');
}

/* ---- Raw drive read/write ---- */

int fs_read(DiskDrive *drive, uint32_t offset, void *buf, uint32_t size) {
    if (!drive || offset + size > drive->size) return -1;
    volatile uint8_t *src = (volatile uint8_t *)(drive->base + offset);
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) dst[i] = src[i];
    return (int)size;
}

int fs_write(DiskDrive *drive, uint32_t offset, const void *buf, uint32_t size) {
    if (!drive || offset + size > drive->size) return -1;
    volatile uint8_t *dst = (volatile uint8_t *)(drive->base + offset);
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < size; i++) dst[i] = src[i];
    if (offset + size > drive->used) drive->used = offset + size;
    return (int)size;
}

uint8_t fs_read_byte(DiskDrive *drive, uint32_t offset) {
    if (!drive || offset >= drive->size) return 0;
    return *(volatile uint8_t *)(drive->base + offset);
}

int fs_write_byte(DiskDrive *drive, uint32_t offset, uint8_t value) {
    if (!drive || offset >= drive->size) return -1;
    *(volatile uint8_t *)(drive->base + offset) = value;
    if (offset + 1 > drive->used) drive->used = offset + 1;
    return 0;
}

/* ---- Path resolution ---- */

/* Produce a fully-qualified path "X:\...\file" from user input.
 *
 * "C:\foo"  -> "C:\foo"
 * "\foo"    -> current_drive + ":\foo"
 * "foo"     -> cwd + "\foo"  (unless cwd is root "C:\", then "C:\foo")
 * "C:"      -> "C:\"
 * "C:foo"   -> "C:\current_dir\foo" (not standard, but for simplicity treat as "C:\foo")
 */
int fs_resolve_path(const char *path, char *out, size_t out_len) {
    if (!path || !*path) {
        strncpy(out, cwd, out_len);
        return 1;
    }

    char drv = 0;
    const char *rest = path;

    /* Check for drive letter prefix */
    if (path[0] && path[1] == ':') {
        drv = path[0];
        rest = path + 2;
    }

    char full[64] = {0};

    if (drv) {
        /* Drive explicitly given */
        if (*rest == '\\' || *rest == '\0') {
            /* "X:\..." or "X:" */
            full[0] = drv;
            full[1] = ':';
            full[2] = '\\';
            if (*rest == '\\') rest++;
            strncat(full, rest, 63 - 3);
        } else {
            /* "X:foo" -> treat as "X:\foo" for simplicity */
            full[0] = drv;
            full[1] = ':';
            full[2] = '\\';
            strncat(full, rest, 63 - 3);
        }
    } else if (*rest == '\\') {
        /* Absolute on current drive */
        char buf[64];
        strncpy(buf, cwd, 63);
        buf[63] = '\0';
        drv = buf[0];
        full[0] = drv;
        full[1] = ':';
        full[2] = '\\';
        strncat(full, rest + 1, 63 - 3);
    } else {
        /* Relative path */
        char buf[64];
        strncpy(buf, cwd, 63);
        buf[63] = '\0';
        ensure_trailing(buf, buf);
        strncat(buf, rest, 63 - strlen(buf) - 1);
        strncpy(full, buf, 63);
        full[63] = '\0';
    }

    /* Normalize: collapse dup backslashes, handle . and .. */
    char stack[16][32];
    int sp = 0;
    char drv_letter = full[0];

    char *part = strtok(full + 3, "\\");
    while (part) {
        if (strcmp(part, ".") == 0) { part = strtok(0, "\\"); continue; }
        if (strcmp(part, "..") == 0) {
            if (sp > 0) sp--;
            part = strtok(0, "\\");
            continue;
        }
        strncpy(stack[sp], part, 31);
        stack[sp][31] = '\0';
        sp++;
        if (sp >= 16) return 0;
        part = strtok(0, "\\");
    }

    out[0] = drv_letter;
    out[1] = ':';
    out[2] = '\\';
    int pos = 3;
    for (int j = 0; j < sp; j++) {
        int len = strlen(stack[j]);
        if (pos + len + 1 >= (int)out_len) return 0;
        for (int k = 0; k < len; k++) out[pos++] = stack[j][k];
        out[pos++] = '\\';
    }
    if (pos == 3) {
        /* Root - keep trailing backslash */
        out[3] = '\0';
    } else {
        out[pos - 1] = '\0';
    }
    return 1;
}

/* ---- CWD management ---- */

int fs_set_cwd(const char *path) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    const FileEntry *e = fs_find_entry(resolved);
    if (!e || e->type != FS_TYPE_DIR) return -1;
    ensure_trailing(resolved, resolved);
    int len = strlen(resolved);
    if (len > 3 && resolved[len - 1] == '\\') resolved[len - 1] = '\0';
    strncpy(cwd, resolved, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
    return 0;
}

const char *fs_get_cwd(void) { return cwd; }

/* ---- Entry lookup ---- */

const FileEntry *fs_find_entry(const char *path) {
    for (int i = 0; i < entry_count; i++)
        if (strcmp(entries[i].entry.name, path) == 0) return &entries[i].entry;
    char with_backslash[64];
    int len = strlen(path);
    if (len > 0 && path[len - 1] != '\\') {
        if (len + 1 < 64) {
            strcpy(with_backslash, path);
            with_backslash[len] = '\\';
            with_backslash[len + 1] = '\0';
            for (int i = 0; i < entry_count; i++)
                if (strcmp(entries[i].entry.name, with_backslash) == 0) return &entries[i].entry;
        }
    }
    return 0;
}

int fs_list_dir(const char *path, void (*callback)(const FileEntry *, void *), void *arg) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    /* Ensure trailing backslash for matching */
    ensure_trailing(resolved, resolved);
    int plen = strlen(resolved);
    for (int i = 0; i < entry_count; i++) {
        const char *name = entries[i].entry.name;
        if (strcmp(name, resolved) == 0) continue;
        if (strncmp(name, resolved, plen) == 0) {
            const char *rest = name + plen;
            if (strchr(rest, '\\') == 0) {
                callback(&entries[i].entry, arg);
            }
        }
    }
    return entry_count;
}

/* ---- Internal helpers ---- */

static int path_exists(const char *path) {
    return fs_find_entry(path) != 0;
}

static FSEntry *find_entry_mutable(const char *path) {
    for (int i = 0; i < entry_count; i++)
        if (strcmp(entries[i].entry.name, path) == 0) return &entries[i];
    char with_backslash[64];
    int len = strlen(path);
    if (len > 0 && path[len - 1] != '\\') {
        if (len + 1 < 64) {
            strcpy(with_backslash, path);
            with_backslash[len] = '\\';
            with_backslash[len + 1] = '\0';
            for (int i = 0; i < entry_count; i++)
                if (strcmp(entries[i].entry.name, with_backslash) == 0) return &entries[i];
        }
    }
    return 0;
}

/* ---- File operations ---- */

int fs_create_file(const char *path, const char *content) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    if (path_exists(resolved)) return -1;
    return add_entry(resolved, FS_TYPE_FILE,
                     content ? strlen(content) : 0, content);
}

int fs_delete_entry(const char *path) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].entry.name, resolved) == 0) {
            if (entries[i].owned && entries[i].content) free(entries[i].content);
            for (int j = i; j < entry_count - 1; j++) entries[j] = entries[j + 1];
            entry_count--; return 0;
        }
    }
    return -1;
}

int fs_update_file(const char *path, const char *content) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    FSEntry *e = find_entry_mutable(resolved);
    if (!e || e->entry.type != FS_TYPE_FILE) return -1;
    if (e->owned && e->content) free(e->content);
    e->content = content ? strdup(content) : 0;
    e->owned = 1;
    e->entry.size = content ? strlen(content) : 0;
    fill_now(&e->entry.modify);
    return 0;
}

int fs_mkdir(const char *path) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    if (path_exists(resolved)) return -1;
    char dirname[64];
    ensure_trailing(resolved, dirname);
    return add_entry(dirname, FS_TYPE_DIR, 0, 0);
}

int fs_rmdir(const char *path) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return -1;
    char dirpath[64];
    ensure_trailing(resolved, dirpath);
    int plen = strlen(dirpath);
    for (int i = 0; i < entry_count; i++)
        if (strncmp(entries[i].entry.name, dirpath, plen) == 0 &&
            entries[i].entry.name[plen] != '\0')
            return -2;
    return fs_delete_entry(resolved);
}

int fs_cp(const char *src, const char *dst) {
    char src_res[64], dst_res[64];
    if (!fs_resolve_path(src, src_res, sizeof(src_res))) return -1;
    if (!fs_resolve_path(dst, dst_res, sizeof(dst_res))) return -1;
    FSEntry *e = find_entry_mutable(src_res);
    if (!e || e->entry.type != FS_TYPE_FILE) return -1;
    return add_entry(dst_res, FS_TYPE_FILE,
                     e->entry.size, e->content);
}

int fs_mv(const char *src, const char *dst) {
    char src_res[64], dst_res[64];
    if (!fs_resolve_path(src, src_res, sizeof(src_res))) return -1;
    if (!fs_resolve_path(dst, dst_res, sizeof(dst_res))) return -1;
    FSEntry *e = find_entry_mutable(src_res);
    if (!e) return -1;
    if (path_exists(dst_res)) return -1;
    strncpy(e->entry.name, dst_res, 31);
    e->entry.name[31] = '\0';
    return 0;
}

char *fs_read_file_content(const char *path) {
    char resolved[64];
    if (!fs_resolve_path(path, resolved, sizeof(resolved))) return 0;
    FSEntry *e = find_entry_mutable(resolved);
    if (!e || e->entry.type != FS_TYPE_FILE) return 0;
    return e->content;
}

const FileEntry *fs_find_file(const char *name) { return fs_find_entry(name); }

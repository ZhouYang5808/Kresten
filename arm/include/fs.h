#ifndef _FS_H
#define _FS_H

#include <stddef.h>
#include <stdint.h>

#define FS_DATE_YEAR_BASE  2000
#define FS_TYPE_FILE 0
#define FS_TYPE_DIR  1

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} FileDate;

typedef struct {
    char name[32];
    uint32_t size;
    uint32_t flags;
    uint8_t  type;
    FileDate create;
    FileDate modify;
} FileEntry;

typedef struct {
    char name[4];
    uint32_t base;
    uint32_t size;
    uint32_t used;
} DiskDrive;

int fs_init(void);
DiskDrive *fs_mount(char drive_letter, uint32_t base, uint32_t size);
int fs_unmount(char drive_letter);
int fs_read(DiskDrive *drive, uint32_t offset, void *buf, uint32_t size);
int fs_write(DiskDrive *drive, uint32_t offset, const void *buf, uint32_t size);
uint8_t fs_read_byte(DiskDrive *drive, uint32_t offset);
int fs_write_byte(DiskDrive *drive, uint32_t offset, uint8_t value);
DiskDrive *fs_get_drive(char drive_letter);
void fs_print_drives(void);

int fs_set_cwd(const char *path);
const char *fs_get_cwd(void);

int fs_resolve_path(const char *path, char *out, size_t out_len);
const FileEntry *fs_find_entry(const char *name);
int fs_list_dir(const char *path, void (*callback)(const FileEntry *, void *), void *arg);

int fs_create_file(const char *path, const char *content);
int fs_delete_entry(const char *path);
int fs_update_file(const char *path, const char *content);
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
int fs_cp(const char *src, const char *dst);
int fs_mv(const char *src, const char *dst);

char *fs_read_file_content(const char *path);

#endif

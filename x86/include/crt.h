#ifndef _RUNTIME_H
#define _RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <fs.h>
#include <rtc.h>
#include <process.h>
#include <net.h>

typedef struct {
    /* stdio */
    void (*putchar)(char c);
    void (*puts)(const char *s);
    int (*printf)(const char *fmt, ...);
    int (*vprintf)(const char *fmt, va_list args);
    void (*gets)(char *buf, int max_len);
    void (*set_output_capture)(char *buf, int size);
    void (*end_output_capture)(void);

    /* stdlib */
    int (*atoi)(const char *s);
    char *(*itoa)(int num, char *buf, int base);
    int (*rand)(void);
    void (*srand)(unsigned int seed);
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    void *(*calloc)(size_t nmemb, size_t size);
    void *(*realloc)(void *ptr, size_t size);
    void (*heap_init)(void *start, size_t len);

    /* string */
    size_t (*strlen)(const char *s);
    char *(*strcpy)(char *dest, const char *src);
    char *(*strncpy)(char *dest, const char *src, size_t n);
    int (*strcmp)(const char *s1, const char *s2);
    int (*strncmp)(const char *s1, const char *s2, size_t n);
    char *(*strchr)(const char *s, int c);
    char *(*strrchr)(const char *s, int c);
    char *(*strstr)(const char *haystack, const char *needle);
    void *(*memset)(void *s, int c, size_t n);
    void *(*memcpy)(void *dest, const void *src, size_t n);
    void *(*memmove)(void *dest, const void *src, size_t n);
    char *(*strdup)(const char *s);
    char *(*strtok)(char *str, const char *delim);
    char *(*strcat)(char *dest, const char *src);
    char *(*strncat)(char *dest, const char *src, size_t n);

    /* ctype */
    int (*isdigit)(int c);
    int (*isalpha)(int c);
    int (*isspace)(int c);
    int (*isxdigit)(int c);
    int (*isalnum)(int c);
    int (*toupper)(int c);
    int (*tolower)(int c);

    /* filesystem */
    int (*fs_init)(void);
    DiskDrive *(*fs_mount)(char drive_letter, uint32_t base, uint32_t size);
    int (*fs_unmount)(char drive_letter);
    int (*fs_read)(DiskDrive *drive, uint32_t offset, void *buf, uint32_t size);
    int (*fs_write)(DiskDrive *drive, uint32_t offset, const void *buf, uint32_t size);
    uint8_t (*fs_read_byte)(DiskDrive *drive, uint32_t offset);
    int (*fs_write_byte)(DiskDrive *drive, uint32_t offset, uint8_t value);
    DiskDrive *(*fs_get_drive)(char drive_letter);
    void (*fs_print_drives)(void);
    int (*fs_set_cwd)(const char *path);
    const char *(*fs_get_cwd)(void);
    int (*fs_resolve_path)(const char *path, char *out, size_t out_len);
    const FileEntry *(*fs_find_entry)(const char *name);
    int (*fs_list_dir)(const char *path, void (*callback)(const FileEntry *, void *), void *arg);
    int (*fs_create_file)(const char *path, const char *content);
    int (*fs_delete_entry)(const char *path);
    int (*fs_update_file)(const char *path, const char *content);
    int (*fs_mkdir)(const char *path);
    int (*fs_rmdir)(const char *path);
    int (*fs_cp)(const char *src, const char *dst);
    int (*fs_mv)(const char *src, const char *dst);
    char *(*fs_read_file_content)(const char *path);

    /* environment */
    void (*env_init)(void);
    const char *(*env_get)(const char *key);
    int (*env_set)(const char *key, const char *val);
    int (*env_unset)(const char *key);
    void (*env_list)(void);
    char *(*env_expand)(const char *str);

    /* rtc */
    void (*rtc_init)(void);
    void (*rtc_read)(RTC_Time *t);

    /* editor */
    void (*editor_run)(const char *filename);

    /* process */
    void (*proc_init)(void);
    int (*proc_create)(const char *name, uint32_t entry);
    void (*proc_yield)(void);
    void (*proc_exit)(int code);
    void (*proc_list)(void);
    Process *(*proc_current)(void);

    /* elf loader */
    int (*elf_load)(const char *path, uint32_t *entry, uint32_t *heap_addr);

    /* network */
    int (*net_init)(void);
    void (*net_poll)(void);
    void (*net_ifconfig)(void);
    int (*net_ping)(const char *target, int count);
    uint32_t (*net_nslookup)(const char *domain);
    int (*net_fetch)(const char *host, int port, const char *path);
    int (*net_dhcp)(void);

    /* power */
    void (*sys_poweroff)(void);

    /* drivers */
    int (*driver_init_all)(void);
    void (*driver_list_all)(void);
#ifdef X86_ARCH
    int (*vga_init)(void);
    int (*vga_test)(void);
#else
    int (*lcd_init)(void);
    int (*lcd_test)(void);
#endif
    int (*kbd_init)(void);
    int (*kbd_test)(void);
} CRTExport;

extern CRTExport *crt;

#endif

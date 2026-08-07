#!/usr/bin/env python3
"""Generate crt_table.S — assembly with .short offsets from _crt_base.

Usage: gen_crt_table.py [arm|x86]  (default: arm)
"""

import sys

DISPLAY_ARM = ["lcd_init", "lcd_test"]
DISPLAY_X86 = ["vga_init", "vga_test"]
NET_ARM = ["net_init", "net_poll", "net_ifconfig",
           "net_ping", "net_nslookup", "net_fetch", "net_dhcp"]
NET_X86 = []

IS_X86 = len(sys.argv) >= 2 and sys.argv[1] == "x86"

FUNCTIONS = [
    # stdio
    "putchar", "puts", "printf", "vprintf",
    "gets", "set_output_capture", "end_output_capture",
    # stdlib
    "atoi", "itoa", "rand", "srand",
    "malloc", "free", "calloc", "realloc", "heap_init",
    # string
    "strlen", "strcpy", "strncpy",
    "strcmp", "strncmp", "strchr", "strrchr",
    "strstr", "memset", "memcpy", "memmove",
    "strdup", "strtok", "strcat", "strncat",
    # ctype
    "isdigit", "isalpha", "isspace", "isxdigit",
    "isalnum", "toupper", "tolower",
    # filesystem
    "fs_init", "fs_mount", "fs_unmount",
    "fs_read", "fs_write", "fs_read_byte", "fs_write_byte",
    "fs_get_drive", "fs_print_drives",
    "fs_set_cwd", "fs_get_cwd", "fs_resolve_path",
    "fs_find_entry", "fs_list_dir",
    "fs_create_file", "fs_delete_entry", "fs_update_file",
    "fs_mkdir", "fs_rmdir", "fs_cp", "fs_mv",
    "fs_read_file_content",
    # environment
    "env_init", "env_get", "env_set", "env_unset",
    "env_list", "env_expand",
    # rtc
    "rtc_init", "rtc_read",
    # editor
    "editor_run",
    # process
    "proc_init", "proc_create", "proc_yield",
    "proc_exit", "proc_list", "proc_current",
    # elf loader
    "elf_load",
    # network (arch-specific)
] + (NET_ARM if not IS_X86 else NET_X86) + [
    # power
    "sys_poweroff",
    # drivers
    "driver_init_all", "driver_list_all",
] + (DISPLAY_ARM if not IS_X86 else DISPLAY_X86) + [
    "kbd_init", "kbd_test",
]

def main():
    out = sys.stdout
    out.write('crt_compressed_start = .;\n')
    for fn in FUNCTIONS:
        out.write(f'SHORT({fn} - _crt_base);\n')
    out.write('crt_compressed_end = .;\n')

if __name__ == '__main__':
    main()

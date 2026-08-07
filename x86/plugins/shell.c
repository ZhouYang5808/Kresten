#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fs.h>
#include <io.h>
#include <plugin.h>
#include <rtc.h>
#include <env.h>
#include <ata.h>
#include <editor.h>
#include <process.h>
#include <driver.h>
#include <registry.h>
#include <gfx.h>

#define MAX_CMD_LEN 256
#define MAX_PIPES 8

void cmd_help(char *args);
void cmd_echo(char *args);
void cmd_ls(char *args);
void cmd_clear(char *args);
void cmd_reboot(char *args);
void sys_poweroff(void);
void sys_reboot(void);
void cmd_power(char *args);
void cmd_cat(char *args);
void cmd_date(char *args);
void cmd_touch(char *args);
void cmd_rm(char *args);
void cmd_write(char *args);
void cmd_plugins(char *args);
void cmd_mkdir(char *args);
void cmd_rmdir(char *args);
void cmd_cd(char *args);
void cmd_pwd(char *args);
void cmd_cp(char *args);
void cmd_mv(char *args);
void cmd_set(char *args);
void cmd_unset(char *args);
void cmd_disk(char *args);
void cmd_edit(char *args);
void cmd_ps(char *args);
void cmd_yield(char *args);
void cmd_sudo(char *args);
void cmd_power(char *args);
void cmd_drivers(char *args);
void cmd_vgatest(char *args);
void cmd_kbdtest(char *args);
void cmd_reg(char *args);
void cmd_gfx(char *args);
void execute_command(char *cmdline);

static int sudo_mode = 0;
static volatile int shell_exit = 0;

void cmd_exit(char *args);

typedef struct {
    char *name;
    void (*func)(char *args);
    char *desc;
} Command;

static Command commands[] = {
    {"help",   cmd_help,   "Show available commands"},
    {"echo",   cmd_echo,   "Echo text to screen"},
    {"dir",    cmd_ls,     "List directory"},
    {"ls",     cmd_ls,     "List directory (alias: dir)"},
    {"cls",    cmd_clear,  "Clear screen"},
    {"clear",  cmd_clear,  "Clear screen (alias: cls)"},
    {"reboot", cmd_reboot, "Reboot the system"},
    {"type",   cmd_cat,    "Display file content"},
    {"cat",    cmd_cat,    "Display file content (alias: type)"},
    {"date",   cmd_date,   "Show current date/time"},
    {"touch",  cmd_touch,  "Create an empty file"},
    {"del",    cmd_rm,     "Delete a file"},
    {"rm",     cmd_rm,     "Delete a file (alias: del)"},
    {"write",  cmd_write,  "Write content to a file"},
    {"mkdir",  cmd_mkdir,  "Create a directory"},
    {"rmdir",  cmd_rmdir,  "Remove an empty directory"},
    {"cd",     cmd_cd,     "Change current directory"},
    {"pwd",    cmd_pwd,    "Print working directory"},
    {"copy",   cmd_cp,     "Copy a file"},
    {"cp",     cmd_cp,     "Copy a file (alias: copy)"},
    {"move",   cmd_mv,     "Move or rename a file"},
    {"rename", cmd_mv,     "Rename a file"},
    {"mv",     cmd_mv,     "Move or rename a file (alias: move)"},
    {"set",    cmd_set,    "Set environment variable"},
    {"unset",  cmd_unset,  "Unset environment variable"},
    {"disk",   cmd_disk,   "Show ATA disk info"},
    {"edit",   cmd_edit,   "Line editor"},
    {"ps",     cmd_ps,     "List processes"},
    {"yield",  cmd_yield,  "Yield to other processes"},
    {"plugins", cmd_plugins, "List loaded plugins"},
    {"sudo",   cmd_sudo,   "Execute command unconditionally"},
    {"power",  cmd_power,  "Power control: power off|reboot"},
    {"drivers", cmd_drivers, "List registered device drivers"},
    {"vgatest", cmd_vgatest, "Test the VGA driver"},
    {"kbdtest", cmd_kbdtest, "Test the keyboard driver"},
    {"reg",     cmd_reg,     "Registry: reg get|set|del <key>"},
    {"gfx",     cmd_gfx,     "Graphics: gfx on|off|fill x y w h c|pixel x y c"},
    {"exit",    cmd_exit,    "Exit to desktop"},
    {NULL, NULL, NULL}
};

void cmd_exit(char *args) {
    (void)args;
    shell_exit = 1;
}

void cmd_help(char *args) {
    (void)args;
    puts("\n=== Kresten Shell ===\n");
    puts("Available commands:\n");
    for (int i = 0; commands[i].name; i++)
        printf("  %-8s %s\n", commands[i].name, commands[i].desc);
    puts("  (pipe)    cmd1 | cmd2 - Pipe output\n");
    puts("  (redir)   cmd > file - Redirect output\n");
    putchar('\n');
}

void cmd_echo(char *args) {
    if (args && *args) {
        char expanded[256];
        strncpy(expanded, args, 255);
        expanded[255] = '\0';
        char *res = env_expand(expanded);
        if (res) puts(res);
        else puts(args);
    }
    putchar('\n');
}

struct ls_ctx { int count; int total; };
static void ls_print(const FileEntry *e, void *arg) {
    struct ls_ctx *ctx = (struct ls_ctx *)arg;
    const char *name = e->name;
    const char *slash = strrchr(name, '\\');
    if (slash && slash[1]) name = slash + 1;
    if (e->type == FS_TYPE_DIR) {
        printf("  %-16s <DIR>     %04u-%02u-%02u\n",
               name, e->modify.year, e->modify.month, e->modify.day);
        ctx->count++;
    } else {
        printf("  %-16s %8d  %04u-%02u-%02u\n",
               name, e->size, e->modify.year, e->modify.month, e->modify.day);
        ctx->count++;
        ctx->total += e->size;
    }
}

void cmd_ls(char *args) {
    struct ls_ctx ctx = {0, 0};
    char *path = args && *args ? args : (char *)fs_get_cwd();
    printf("\n Directory of %s\n\n", path);
    puts("  NAME              SIZE     MODIFIED\n");
    puts("  ----------------  ------  --------------\n");
    fs_list_dir(path, ls_print, &ctx);
    printf("\n  %d entries(s)  %d bytes\n\n", ctx.count, ctx.total);
}

void cmd_clear(char *args) {
    (void)args;
    for (int i = 0; i < 50; i++) putchar('\n');
}

void cmd_reboot(char *args) {
    (void)args;
    sys_reboot();
}

void cmd_power(char *args) {
    if (!args || !*args) {
        puts("Usage: power off | reboot\n"
             "  power off     - Shut down the machine\n"
             "  power reboot  - Restart the machine\n");
        return;
    }
    char sub[16];
    strncpy(sub, args, 15);
    sub[15] = '\0';
    if (strcasecmp(sub, "off") == 0 || strcasecmp(sub, "shutdown") == 0 ||
        strcasecmp(sub, "halt") == 0 || strcasecmp(sub, "poweroff") == 0) {
        sys_poweroff();
    } else if (strcasecmp(sub, "reboot") == 0 || strcasecmp(sub, "restart") == 0) {
        cmd_reboot(NULL);
    } else {
        printf("power: unknown action '%s'\n", sub);
        puts("Usage: power off | reboot\n");
    }
}

void cmd_drivers(char *args) {
    (void)args;
    driver_list_all();
}

void cmd_vgatest(char *args) {
    (void)args;
    puts("\n========== VGA Driver Test ==========\n");
    vga_test();
    puts("========== VGA test complete ==========\n");
}

void cmd_gfx(char *args) {
    char sub[16];
    int i = 0;
    while (args && args[i] && args[i] != ' ' && i < 15) {
        sub[i] = args[i];
        i++;
    }
    sub[i] = '\0';
    while (args && args[i] == ' ') i++;

    if (strcmp(sub, "on") == 0 || sub[0] == '\0') {
        gfx_test();
    } else if (strcmp(sub, "off") == 0) {
        gfx_leave();
        puts("[GFX] back to text mode\n");
    } else if (strcmp(sub, "fill") == 0) {
        int x = atoi(args + i);
        while (args[i] && args[i] != ' ') i++;
        while (args[i] == ' ') i++;
        int y = atoi(args + i);
        while (args[i] && args[i] != ' ') i++;
        while (args[i] == ' ') i++;
        int w = atoi(args + i);
        while (args[i] && args[i] != ' ') i++;
        while (args[i] == ' ') i++;
        int h = atoi(args + i);
        while (args[i] && args[i] != ' ') i++;
        while (args[i] == ' ') i++;
        int c = atoi(args + i);
        if (gfx_active()) {
            gfx_fill_rect(x, y, w, h, (uint8_t)c);
            printf("[GFX] rect (%d,%d) %dx%d color=%d\n", x, y, w, h, c);
        } else {
            gfx_enter();
            gfx_fill_rect(x, y, w, h, (uint8_t)c);
            printf("[GFX] entered graphics, rect (%d,%d) %dx%d color=%d\n",
                   x, y, w, h, c);
        }
    } else if (strcmp(sub, "pixel") == 0) {
        int x = atoi(args + i);
        while (args[i] && args[i] != ' ') i++;
        while (args[i] == ' ') i++;
        int y = atoi(args + i);
        while (args[i] && args[i] != ' ') i++;
        while (args[i] == ' ') i++;
        int c = atoi(args + i);
        if (!gfx_active()) {
            gfx_enter();
            printf("[GFX] entered graphics, pixel (%d,%d) color=%d\n", x, y, c);
        }
        gfx_pixel(x, y, (uint8_t)c);
    } else {
        puts("Usage: gfx on | off | fill <x> <y> <w> <h> <color> | pixel <x> <y> <color>\n");
    }
}

void cmd_kbdtest(char *args) {
    (void)args;
    puts("\n========== Keyboard Driver Test ==========\n");
    kbd_test();
    puts("========== KBD test complete ==========\n");
}

void cmd_reg(char *args) {
    if (!args || !*args) { reg_list(); return; }
    char *p = args;
    while (*p && isspace(*p)) p++;
    char sub[16];
    int i = 0;
    while (*p && !isspace(*p) && i < 15) sub[i++] = *p++;
    sub[i] = '\0';
    while (*p && isspace(*p)) p++;
    char *rest = p;
    if (strcasecmp(sub, "list") == 0 || strcasecmp(sub, "dump") == 0) {
        reg_list();
        return;
    }
    if (strcasecmp(sub, "get") == 0) {
        if (!*rest) { puts("Usage: reg get <key>\n"); return; }
        char key[REG_KEY_MAX + 1];
        i = 0;
        while (*rest && !isspace(*rest) && i < REG_KEY_MAX) key[i++] = *rest++;
        key[i] = '\0';
        const char *v = reg_get(key);
        if (v) printf("%s=%s\n", key, v);
        else printf("reg: '%s' not found\n", key);
        return;
    }
    if (strcasecmp(sub, "set") == 0) {
        if (!*rest) { puts("Usage: reg set <key> <value>\n"); return; }
        char key[REG_KEY_MAX + 1];
        i = 0;
        while (*rest && !isspace(*rest) && i < REG_KEY_MAX) key[i++] = *rest++;
        key[i] = '\0';
        while (*rest && isspace(*rest)) rest++;
        if (!*rest) { puts("Usage: reg set <key> <value>\n"); return; }
        if (reg_set(key, rest) == 0) printf("  %s = %s\n", key, rest);
        else puts("reg: set failed (table full?)\n");
        return;
    }
    if (strcasecmp(sub, "del") == 0 || strcasecmp(sub, "delete") == 0) {
        if (!*rest) { puts("Usage: reg del <key>\n"); return; }
        char key[REG_KEY_MAX + 1];
        i = 0;
        while (*rest && !isspace(*rest) && i < REG_KEY_MAX) key[i++] = *rest++;
        key[i] = '\0';
        if (reg_del(key) == 0) printf("  %s deleted\n", key);
        else printf("reg: '%s' not found\n", key);
        return;
    }
    puts("Usage: reg [list|get <key>|set <key> <value>|del <key>]\n");
}

void cmd_cat(char *args) {
    if (!args || !*args) { puts("Usage: cat <filename>\n"); return; }
    char *content = fs_read_file_content(args);
    if (!content) { printf("cat: %s: No such file\n", args); return; }
    putchar('\n');
    puts(content);
    putchar('\n');
}

void cmd_date(char *args) {
    (void)args;
    RTC_Time t;
    rtc_read(&t);
    printf("\nCurrent date: %04u-%02u-%02u\n", t.year, t.month, t.day);
    printf("Current time: %02u:%02u:%02u\n", t.hour, t.minute, t.second);
}

void cmd_touch(char *args) {
    if (!args || !*args) { puts("Usage: touch <filename>\n"); return; }
    if (fs_create_file(args, "") == 0) puts("File created.\n");
    else puts("touch: failed (exists or table full)\n");
}

void cmd_rm(char *args) {
    if (!args || !*args) { puts("Usage: rm <filename>\n"); return; }
    if (fs_delete_entry(args) == 0) puts("File deleted.\n");
    else puts("rm: file not found\n");
}

void cmd_write(char *args) {
    if (!args || !*args) { puts("Usage: write <filename> <content>\n"); return; }
    char *p = args;
    while (*p && !isspace(*p)) p++;
    if (!*p) { puts("Usage: write <filename> <content>\n"); return; }
    *p++ = '\0';
    while (*p && isspace(*p)) p++;
    char resolved[64];
    if (!fs_resolve_path(args, resolved, sizeof(resolved))) { puts("write: bad path\n"); return; }
    if (fs_find_entry(resolved)) {
        if (fs_update_file(resolved, p) == 0) puts("File updated.\n");
        else puts("write: update failed\n");
    } else {
        if (fs_create_file(resolved, p) == 0) puts("File created.\n");
        else puts("write: create failed (table full)\n");
    }
}

void cmd_plugins(char *args) {
    (void)args;
    plugin_list_all();
}

void cmd_mkdir(char *args) {
    if (!args || !*args) { puts("Usage: mkdir <dirname>\n"); return; }
    if (fs_mkdir(args) == 0) puts("Directory created.\n");
    else puts("mkdir: failed (exists or table full)\n");
}

void cmd_rmdir(char *args) {
    if (!args || !*args) { puts("Usage: rmdir <dirname>\n"); return; }
    int ret = fs_rmdir(args);
    if (ret == 0) puts("Directory removed.\n");
    else if (ret == -2) puts("rmdir: directory not empty\n");
    else puts("rmdir: not found\n");
}

void cmd_cd(char *args) {
    if (!args || !*args) { puts("Usage: cd <dirname>\n"); return; }
    if (fs_set_cwd(args) != 0) puts("cd: no such directory\n");
}

void cmd_pwd(char *args) {
    (void)args;
    printf("\n%s\n\n", fs_get_cwd());
}

void cmd_cp(char *args) {
    if (!args || !*args) { puts("Usage: cp <source> <dest>\n"); return; }
    char *p = args;
    while (*p && !isspace(*p)) p++;
    if (!*p) { puts("Usage: cp <source> <dest>\n"); return; }
    *p++ = '\0';
    while (*p && isspace(*p)) p++;
    if (!*p) { puts("Usage: cp <source> <dest>\n"); return; }
    if (fs_cp(args, p) == 0) puts("File copied.\n");
    else puts("cp: failed\n");
}

void cmd_mv(char *args) {
    if (!args || !*args) { puts("Usage: mv <source> <dest>\n"); return; }
    char *p = args;
    while (*p && !isspace(*p)) p++;
    if (!*p) { puts("Usage: mv <source> <dest>\n"); return; }
    *p++ = '\0';
    while (*p && isspace(*p)) p++;
    if (!*p) { puts("Usage: mv <source> <dest>\n"); return; }
    if (fs_mv(args, p) == 0) puts("File moved.\n");
    else puts("mv: failed\n");
}

void cmd_set(char *args) {
    if (!args || !*args) { env_list(); return; }
    char *eq = strchr(args, '=');
    if (eq) {
        *eq = '\0';
        env_set(args, eq + 1);
        printf("  %s=%s\n", args, eq + 1);
        *eq = '=';
    } else {
        char var[32];
        int i = 0;
        while (*args && !isspace(*args) && i < 31) var[i++] = *args++;
        var[i] = '\0';
        const char *val = env_get(var);
        if (val) printf("%s=%s\n", var, val);
        else printf("%s: not set\n", var);
    }
}

void cmd_unset(char *args) {
    if (!args || !*args) { puts("Usage: unset <variable>\n"); return; }
    char var[32];
    int i = 0;
    while (*args && !isspace(*args) && i < 31) var[i++] = *args++;
    var[i] = '\0';
    if (env_unset(var) == 0) printf("  %s unset\n", var);
    else printf("  %s: not set\n", var);
}

void cmd_disk(char *args) {
    (void)args;
    puts("\n=== ATA Drives ===\n");
    ata_print_info();
    putchar('\n');
}

void cmd_edit(char *args) {
    if (!args || !*args) { puts("Usage: edit <filename>\n"); return; }
    char fn[64];
    strncpy(fn, args, 63);
    fn[63] = '\0';
    char *nl = strchr(fn, '\n');
    if (nl) *nl = '\0';
    editor_run(fn);
}

void cmd_ps(char *args) {
    (void)args;
    puts("\n=== Processes ===\n");
    proc_list();
    putchar('\n');
}

void cmd_yield(char *args) {
    (void)args;
    puts("Yielding...\n");
    proc_yield();
}

void cmd_sudo(char *args) {
    if (!args || !*args) { puts("sudo: missing command\n"); return; }
    sudo_mode = 1;
    execute_command(args);
    sudo_mode = 0;
}

static int run_command(char *cmdline) {
    char *p = cmdline;
    while (*p && isspace(*p)) p++;
    if (!*p) return 0;
    char cmd_name[32];
    int i = 0;
    while (*p && !isspace(*p) && i < 31) cmd_name[i++] = *p++;
    cmd_name[i] = '\0';
    while (*p && isspace(*p)) p++;
    char *args = p;
    if (!*args) args = NULL;
    for (int i = 0; commands[i].name; i++) {
        if (strcasecmp(cmd_name, commands[i].name) == 0) {
            commands[i].func(args);
            return 0;
        }
    }
    if (plugin_dispatch(cmd_name, args) == 0) return 0;
    return -1;
}

void execute_command(char *cmdline) {
    int len = strlen(cmdline);
    if (len > 0 && (cmdline[len-1] == '\n' || cmdline[len-1] == '\r'))
        cmdline[--len] = '\0';
    if (len == 0) return;

    char expanded[512];
    char *exp = env_expand(cmdline);
    strncpy(expanded, exp ? exp : cmdline, 511);
    expanded[511] = '\0';

    char *pipe_parts[MAX_PIPES];
    int npipes = 0;
    pipe_parts[npipes++] = expanded;
    for (char *q = expanded; *q && npipes < MAX_PIPES; q++) {
        if (*q == '|') {
            *q = '\0';
            pipe_parts[npipes++] = q + 1;
        }
    }

    char pipe_buf[MAX_PIPES - 1][1024];
    for (int pi = 0; pi < npipes; pi++) {
        char *part = pipe_parts[pi];
        while (*part && isspace(*part)) part++;
        if (!*part) continue;

        char redir_file[64] = {0};
        int redir_mode = 0;
        char *out = strchr(part, '>');
        char *in = strchr(part, '<');
        if (in && (!out || in < out)) {
            *in = '\0';
            char *f = in + 1;
            while (*f && isspace(*f)) f++;
            strncpy(redir_file, f, 63);
            char *sp = redir_file;
            while (*sp && !isspace(*sp)) sp++;
            *sp = '\0';
            redir_mode = 2;
        } else if (out && (!in || out < in)) {
            *out = '\0';
            char *f = out + 1;
            while (*f && isspace(*f)) f++;
            strncpy(redir_file, f, 63);
            char *sp = redir_file;
            while (*sp && !isspace(*sp)) sp++;
            *sp = '\0';
            redir_mode = 1;
        }

        if (pi > 0) {
            set_output_capture(pipe_buf[pi - 1], 1024);
            int r = run_command(part);
            end_output_capture();
            if (r != 0 && pi == npipes - 1)
                printf("%s: command not found\nType 'help' for available commands.\n", part);
        } else if (npipes > 1) {
            set_output_capture(pipe_buf[0], 1024);
            int r = run_command(part);
            end_output_capture();
            if (r != 0) {
                printf("%s: command not found\nType 'help' for available commands.\n", part);
                return;
            }
        } else if (redir_mode == 1) {
            char cap[4096];
            set_output_capture(cap, 4096);
            int r = run_command(part);
            end_output_capture();
            if (r == 0) {
                char redir_res[64];
                if (fs_resolve_path(redir_file, redir_res, sizeof(redir_res))) {
                    if (fs_find_entry(redir_res)) fs_update_file(redir_res, cap);
                    else fs_create_file(redir_res, cap);
                }
            }
        } else if (redir_mode == 2) {
            char redir_res[64];
            char *content = 0;
            if (fs_resolve_path(redir_file, redir_res, sizeof(redir_res)))
                content = fs_read_file_content(redir_res);
            if (content) {
                char newargs[128];
                char *np = part;
                while (*np && !isspace(*np)) np++;
                while (*np && isspace(*np)) np++;
                strncpy(newargs, np, 127);
                newargs[127] = '\0';
                int nlen = strlen(newargs);
                int clen = strlen(content);
                int total = nlen + clen + 2;
                char *combined = malloc(total);
                if (combined) {
                    if (nlen > 0) {
                        strcpy(combined, newargs);
                        strcat(combined, " ");
                    } else {
                        combined[0] = '\0';
                    }
                    strcat(combined, content);
                    run_command(combined);
                    free(combined);
                }
            }
        } else {
            int r = run_command(part);
            if (r != 0)
                printf("%s: command not found\nType 'help' for available commands.\n", part);
        }
    }

    for (int pi = 1; pi < npipes; pi++) {
        char *part = pipe_parts[pi];
        while (*part && isspace(*part)) part++;
        if (!*part) continue;
        char prev_out[1024];
        strncpy(prev_out, pipe_buf[pi - 1], 1023);
        prev_out[1023] = '\0';
        int plen = strlen(prev_out);
        if (plen > 0 && prev_out[plen - 1] == '\n') prev_out[--plen] = '\0';
        char merged[1152];
        strncpy(merged, part, 1023);
        merged[1023] = '\0';
        int mlen = strlen(merged);
        if (mlen + plen + 2 < 1152) {
            merged[mlen] = ' ';
            merged[mlen + 1] = '\0';
            strncat(merged, prev_out, 1152 - mlen - 2);
        }
        int r = run_command(merged);
        if (plen > 0) {
            char fn[32];
            int i;
            for (i = 0; prev_out[i] && i < 31; i++) {
                if (isspace(prev_out[i])) break;
                fn[i] = prev_out[i];
            }
            fn[i] = '\0';
            char fn_res[64];
            if (fs_resolve_path(fn, fn_res, sizeof(fn_res)) && fs_find_entry(fn_res)) {
                char *content = fs_read_file_content(fn_res);
                if (content) {
                    set_output_capture(prev_out, 1024);
                    run_command(content);
                    end_output_capture();
                }
            }
        }
        if (r != 0 && pi == npipes - 1)
            printf("%s: command not found\nType 'help' for available commands.\n", part);
    }
}

int shell_run(void) {
    shell_exit = 0;
    puts("\n");
    puts("=============================================\n");
    puts("  Kresten Command Shell v1.0\n");
    puts("  Type 'help' for available commands\n");
    puts("  Type 'exit' to return to the desktop\n");
    puts("=============================================\n\n");
    char cmdline[MAX_CMD_LEN];
    while (!shell_exit) {
        printf("%s> ", fs_get_cwd());
        gets(cmdline, MAX_CMD_LEN);
        if (shell_exit)
            break;
        execute_command(cmdline);
    }
    puts("\n[SHELL] returning to desktop\n");
    return 0;
}

int plugin_shell_init(void) {
    puts("[INFO] Shell plugin loaded (started from the desktop)\n");
    return 0;
}

int plugin_shell_cmd(char *args) {
    (void)args;
    return shell_run();
}

REGISTER_PLUGIN(shell);

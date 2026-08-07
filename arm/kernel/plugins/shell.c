#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fs.h>
#include <plugin.h>
#include <rtc.h>
#include <env.h>
#include <editor.h>
#include <process.h>
#include <net.h>
#include <driver.h>

#define MAX_CMD_LEN 256
#define MAX_PIPES 8
#define MAX_REDIR 2

void cmd_help(char *args);
void cmd_echo(char *args);
void cmd_ls(char *args);
void cmd_clear(char *args);
void cmd_reboot(char *args);
void sys_poweroff(void);
void cmd_power(char *args);
void cmd_exit(char *args);
void cmd_cat(char *args);
void cmd_date(char *args);
void cmd_touch(char *args);
void cmd_rm(char *args);
void cmd_write(char *args);
void cmd_plugins(char *args);
void cmd_mkdir(char *args);
void cmd_rmdir(char *args);
void cmd_ps(char *args);
void cmd_yield(char *args);
void cmd_cd(char *args);
void cmd_pwd(char *args);
void cmd_cp(char *args);
void cmd_mv(char *args);
void cmd_set(char *args);
void cmd_unset(char *args);
void cmd_edit(char *args);
void cmd_sudo(char *args);
void cmd_hello(char *args);
int plugin_hello_cmd(char *args);
void cmd_ifconfig(char *args);
void cmd_ping(char *args);
void cmd_nslookup(char *args);
void cmd_fetch(char *args);
void cmd_dhcp(char *args);
void cmd_nettest(char *args);
void cmd_drivers(char *args);
void cmd_lcdtest(char *args);
void cmd_kbdtest(char *args);
void execute_command(char *cmdline);

static int sudo_mode = 0;

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
    {"power",  cmd_power,  "Power control: power off|reboot"},
    {"exit",   cmd_exit,   "Exit to desktop"},
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
    {"edit",   cmd_edit,   "Line editor"},
    {"plugins", cmd_plugins, "List loaded plugins"},
    {"ps",     cmd_ps,     "List processes"},
    {"yield",  cmd_yield,  "Yield to other processes"},
    {"sudo",   cmd_sudo,   "Execute command unconditionally"},
    {"hello",  cmd_hello,  "Test the hello plugin"},
    {"ifconfig", cmd_ifconfig, "Show network interface configuration"},
    {"netstat",  cmd_ifconfig, "Show network status (alias: ifconfig)"},
    {"ping",     cmd_ping,     "Ping a host or IP (e.g. ping 10.0.2.2)"},
    {"nslookup", cmd_nslookup, "Lookup domain name IP (e.g. nslookup httpbin.org)"},
    {"fetch",    cmd_fetch,    "Fetch a web page over HTTP (e.g. fetch httpbin.org /ip)"},
    {"get",      cmd_fetch,    "Fetch a web page over HTTP (alias: fetch)"},
    {"dhcp",     cmd_dhcp,     "Request IP address via DHCP"},
    {"nettest",  cmd_nettest,  "Run network self-test"},
    {"drivers",  cmd_drivers,  "List registered device drivers"},
    {"lcdtest",  cmd_lcdtest,  "Test the LCD driver (PL110)"},
    {"kbdtest",  cmd_kbdtest,  "Test the keyboard driver (PL050)"},
    {NULL, NULL, NULL}
};

void cmd_help(char *args) {
    (void)args;
    puts("\nCommands:\n");
    for (int i = 0; commands[i].name; i++) {
        char up[32];
        int j;
        for (j = 0; commands[i].name[j] && j < 31; j++) {
            unsigned char c = commands[i].name[j];
            if (c >= 'a' && c <= 'z') c -= 32;
            up[j] = c;
        }
        up[j] = '\0';
        printf("  %-8s %s\n", up, commands[i].desc);
    }
    puts("\n  PIPE    cmd1 | cmd2 - Pipe output\n");
    puts("  REDIR   cmd > file - Redirect output\n");
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
    puts("\nRebooting system...\n");
    __asm__ volatile ("mov pc, #0x10000");
}

void cmd_power(char *args) {
    if (!args || !*args) {
        puts("Usage: power off | reboot\n"
             "  power off     - Shut down the machine\n"
             "  power reboot  - Restart the machine\n");
        return;
    }
    while (*args && isspace(*args)) args++;
    char sub[16];
    int i = 0;
    while (*args && !isspace(*args) && i < 15) sub[i++] = *args++;
    sub[i] = '\0';
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

void cmd_edit(char *args) {
    if (!args || !*args) { puts("Usage: edit <filename>\n"); return; }
    char fn[64];
    strncpy(fn, args, 63);
    fn[63] = '\0';
    char *nl = strchr(fn, '\n');
    if (nl) *nl = '\0';
    editor_run(fn);
}

void cmd_sudo(char *args) {
    if (!args || !*args) { puts("sudo: missing command\n"); return; }
    sudo_mode = 1;
    execute_command(args);
    sudo_mode = 0;
}

void cmd_hello(char *args) {
    plugin_hello_cmd(args);
}

void cmd_ifconfig(char *args) {
    (void)args;
    net_ifconfig();
}

void cmd_ping(char *args) {
    if (!args || !*args) {
        puts("Usage: ping <host_or_ip> [count]\n");
        return;
    }
    char target[64];
    int count = 4;
    char *p = args;
    int i = 0;
    while (*p && !isspace(*p) && i < 63) target[i++] = *p++;
    target[i] = '\0';
    while (*p && isspace(*p)) p++;
    if (*p) count = atoi(p);
    net_ping(target, count);
}

void cmd_nslookup(char *args) {
    if (!args || !*args) {
        puts("Usage: nslookup <domain>\n");
        return;
    }
    char domain[64];
    int i = 0;
    while (*args && !isspace(*args) && i < 63) domain[i++] = *args++;
    domain[i] = '\0';
    net_nslookup(domain);
}

void cmd_fetch(char *args) {
    if (!args || !*args) {
        puts("Usage: fetch <host> [port] [path]  (e.g. fetch 10.0.2.2 8080 /)\n");
        return;
    }
    char host[64];
    char path[128] = "/";
    int port = 80;
    char *p = args;
    int i = 0;
    while (*p && !isspace(*p) && i < 63) host[i++] = *p++;
    host[i] = '\0';
    while (*p && isspace(*p)) p++;
    if (*p) {
        if (isdigit((unsigned char)*p)) {
            port = atoi(p);
            while (*p && !isspace(*p)) p++;
            while (*p && isspace(*p)) p++;
        }
        if (*p) {
            i = 0;
            while (*p && !isspace(*p) && i < 127) path[i++] = *p++;
            path[i] = '\0';
        }
    }
    net_fetch(host, port, path);
}

void cmd_dhcp(char *args) {
    (void)args;
    net_dhcp();
}

void cmd_nettest(char *args) {
    (void)args;
    puts("\n========== Network Diagnostics ==========\n");
    puts("[1] Interface configuration:\n");
    net_ifconfig();
    puts("\n[2] Pinging gateway 10.0.2.2:\n");
    net_ping("10.0.2.2", 2);
    puts("\n[3] DNS lookup httpbin.org:\n");
    uint32_t ip = net_nslookup("httpbin.org");
    if (ip != 0) {
        puts("\n[4] HTTP fetch httpbin.org /ip:\n");
        net_fetch("httpbin.org", 80, "/ip");
    }
    puts("\n========== Diagnostics complete ==========\n");
}

void cmd_drivers(char *args) {
    (void)args;
    driver_list_all();
}

void cmd_lcdtest(char *args) {
    (void)args;
    puts("\n========== LCD Driver Test ==========\n");
    lcd_test();
    puts("========== LCD test complete ==========\n");
}

void cmd_kbdtest(char *args) {
    (void)args;
    puts("\n========== Keyboard Driver Test ==========\n");
    kbd_test();
    puts("========== KBD test complete ==========\n");
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
            if (r != 0 && pi == npipes - 1) {
                printf("%s: command not found\nType 'help' for available commands.\n", part);
            }
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
            char newargs[128];
            if (content) {
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
            if (r != 0) {
                printf("%s: command not found\nType 'help' for available commands.\n", part);
            }
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
        if (r != 0 && pi == npipes - 1) {
            printf("%s: command not found\nType 'help' for available commands.\n", part);
        }
    }
}

static volatile int shell_exit = 0;

void cmd_exit(char *args) {
    (void)args;
    shell_exit = 1;
}

int shell_run(void) {
    shell_exit = 0;
    char cmdline[MAX_CMD_LEN];
    puts("\n");
    puts("=============================================\n");
    puts("  Kresten Command Shell v1.0\n");
    puts("  Type 'help' for available commands\n");
    puts("  Type 'exit' to return to the desktop\n");
    puts("=============================================\n\n");
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

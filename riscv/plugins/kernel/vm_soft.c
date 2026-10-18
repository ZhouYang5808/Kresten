/* ===== kernel/plugins/vm_soft.c: 软件仿真模式 VM guest OS (RISC-V) =====
 * RISC-V 无 VT-x/VMX 硬件, 所以这个软件 guest 是 "kresten" guest 的正常路径。
 * 输出走 putchar (UART 串口), 便于 QEMU 测试。
 */

#include <stdio.h>
#include <string.h>
#include <keyboard.h>

void vm_set_status(const char *s);

/* ---------- 键盘: set1 scancode -> ASCII (与 asm guest 相同的映射) ---------- */
static const char digit_tbl[] = "1234567890";
static const char alpha_tbl_lo[] = "qwertyuiop\0\0\0";
static const char alpha_tbl_hi[] = "QWERTYUIOP\0\0\0";
static const char alpha2_tbl[] = "asdfghjkl\0\0\0\0";
static const char alpha3_tbl[] = "zxcvbnm,./";

static int vm_soft_shift;

static char vm_soft_kbd_getc(void) {
    if (!keyboard_scancode_pending())
        return 0;
    int sc = keyboard_poll_scancode();
    if (sc < 0)
        return 0;
    unsigned char al = (unsigned char)sc;
    if (al == 0x2A || al == 0x36) {
        vm_soft_shift = 1;
        return 0;
    }
    if (al == 0xAA || al == 0xB6) {
        vm_soft_shift = 0;
        return 0;
    }
    if (al & 0x80)
        return 0;
    switch (al) {
    case 0x1C: return '\r';
    case 0x0E: return '\b';
    case 0x39: return ' ';
    }
    if (al >= 0x02 && al <= 0x0B)
        return digit_tbl[al - 0x02];
    if (al >= 0x10 && al <= 0x1C)
        return vm_soft_shift ? alpha_tbl_hi[al - 0x10] : alpha_tbl_lo[al - 0x10];
    if (al >= 0x1E && al <= 0x26)
        return alpha2_tbl[al - 0x1E];
    if (al >= 0x2C && al <= 0x35)
        return alpha3_tbl[al - 0x2C];
    return 0;
}

/* ---------- 显示辅助 (经 putchar: UART) ---------- */
static void vm_soft_clear(void) {
    puts("\x1B[2J\x1B[1;1H");
}

static void vm_soft_backspace(void) {
    putchar('\b');
    putchar(' ');
    putchar('\b');
}

/* ---------- 命令 ---------- */
static void vm_soft_help(void) {
    puts("help        show this text\n");
    puts("clear       clear the screen\n");
    puts("echo <txt>  print text\n");
    puts("exit        stop the virtual machine\n");
}

static int vm_soft_run_cmd(const char *cmd) {
    if (!cmd[0])
        return 0;
    if (strcmp(cmd, "exit") == 0) {
        puts("bye - exiting virtual machine\n");
        vm_set_status("guest stopped (software mode)");
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        vm_soft_help();
        return 0;
    }
    if (strcmp(cmd, "clear") == 0) {
        vm_soft_clear();
        return 0;
    }
    if (strncmp(cmd, "echo", 4) == 0 &&
        (cmd[4] == 0 || cmd[4] == ' ')) {
        const char *p = cmd + 4;
        while (*p == ' ')
            p++;
        if (*p)
            printf("%s\n", p);
        return 0;
    }
    printf("unknown command: %s\n", cmd);
    return 0;
}

/* ---------- 软件 guest 会话 ---------- */
int vm_run_soft(void) {
    char cmd_buf[64];
    int cmd_len = 0;
    keyboard_flush();              /* drop keys typed before the session */
    vm_soft_shift = 0;
    vm_soft_clear();
    puts("vm: software emulation mode (no hardware virt)");
    puts("Kresten VM guest OS v1.0 - software virtual machine\n");
    puts("commands: help, clear, echo <text>, exit\n");
    printf("guest> ");
    for (;;) {
        char c = vm_soft_kbd_getc();
        if (!c)
            continue;
        if (c == '\r') {
            putchar('\n');
            cmd_buf[cmd_len] = 0;
            if (vm_soft_run_cmd(cmd_buf))
                break;
            cmd_len = 0;
            printf("guest> ");
            continue;
        }
        if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                vm_soft_backspace();
            }
            continue;
        }
        if (c == 0x1B) {                /* Esc = clear line */
            while (cmd_len > 0) {
                cmd_len--;
                vm_soft_backspace();
            }
            continue;
        }
        if (cmd_len < 63) {
            cmd_buf[cmd_len++] = c;
            putchar(c);
        }
    }
    keyboard_flush();              /* keep leftovers out of the host shell */
    return 0;
}

/* ===== plugins/vm_soft.c: software-emulation mode of the VM guest OS =====
 * Native port of scripts/vm_guest.asm for machines WITHOUT VT-x:
 * same banner, commands, screen layout and keyboard behaviour as the
 * hardware (VMX) guest. Runs in VGA text mode; output goes to 0xB8000
 * (and the serial mirror via putchar, which also makes it testable).
 * Invoked automatically by the vm plugin when CPUID.1:ECX.VMX=0. */

#include <stdio.h>
#include <string.h>
#include <keyboard.h>

void vm_set_status(const char *s);

/* ---------- keyboard: scancode set1 -> ASCII (ported from the asm guest) ---------- */
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

/* ---------- display helpers (through putchar: VGA text + serial) ---------- */
static void vm_soft_clear(void) {
    puts("\x1B[2J\x1B[1;1H");
}

static void vm_soft_backspace(void) {
    putchar('\b');
    putchar(' ');
    putchar('\b');
}

/* ---------- commands ---------- */
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

/* ---------- the software guest session ---------- */
int vm_run_soft(void) {
    char cmd_buf[64];
    int cmd_len = 0;
    keyboard_flush();              /* drop keys typed before the session */
    vm_soft_shift = 0;
    vm_soft_clear();
    puts("vm: no VT-x - software emulation mode");
    puts("Kresten VM guest OS v1.0 - VMX virtual machine (software)\n");
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

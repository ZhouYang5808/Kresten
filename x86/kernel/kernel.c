#include <stdio.h>
#include <string.h>
#include <fs.h>
#include <plugin.h>
#include <rtc.h>
#include <stdlib.h>
#include <idt.h>
#include <keyboard.h>
#include <env.h>
#include <ata.h>
#include <process.h>
#include <io.h>
#include <driver.h>
#include <registry.h>

void gdt_init(void);
int drivers_register_all(void);
int install_to_hdd(void);

void sys_poweroff(void) {
    puts("\nPowering off...\n");
    outw(0x1004, 0x3400); /* VMware ACPI PM1a_CNT: SLP_TYP=5 (S5) + SLP_EN */
    outw(0x604, 0x2000);  /* QEMU i440fx ACPI PM1a_CNT: SLP_TYP=0 (soft power off) + SLP_EN */
    for (volatile int i = 0; i < 1000000; i++);
    __asm__ volatile ("cli; hlt");
}

void sys_reboot(void) {
    puts("\nRebooting...\n");
    outb(0x64, 0xFE); /* PS/2 controller reset: works in QEMU and VMware */
    for (volatile int i = 0; i < 1000000; i++);
    outb(0xCF9, 0x06); /* reset control port hard reset: QEMU + VMware */
    for (volatile int i = 0; i < 1000000; i++);
    __asm__ volatile ("cli; hlt");
}

static void test_process(void) {
    int count = 0;
    while (1) {
        printf("[PROC:test] iteration %d\n", count++);
        for (volatile int i = 0; i < 800000; i++);
        proc_yield();
    }
}

void kernel_main(uint32_t mb_info) {
    serial_init();
    gdt_init();

    puts("[INFO] KERNEL: MyOS Kernel v1.0 x86 starting...\n");

    idt_init();
    drivers_register_all();
    driver_init_all();

    heap_init(0, 0);
    rtc_init();
    env_init();
    reg_persist_setup(mb_info);
    reg_init();
    sched_apply_registry();
    ata_init();
    if (install_to_hdd() == 0) {
        puts("[INFO] INSTALL DONE\n");
        sys_reboot();
    }
    proc_init();
    proc_create("test", (uint32_t)test_process);
    plugin_init_all();
    while (1)
        plugin_dispatch("desktop", NULL);
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <env.h>
#include <process.h>
#include <rtc.h>
#include <net.h>
#include <driver.h>
#include <plugin.h>
#include <defense.h>
#include <guard.h>

uint32_t sched_get_ticks(void);

int drivers_register_all(void);

/* 强制链接器脚本符号(_lcd_fb_start, 'A' 类型)进入内核符号表:
 * gfx/lcd .kdrv 模块在加载时经 kernel_symtab 解析该地址。 */
extern uint8_t _lcd_fb_start[];
uintptr_t lcd_fb_base __attribute__((used)) = (uintptr_t)_lcd_fb_start;

/* sifive test device @ 0x100000: 写 0x5555 = 退出 QEMU (exit 0) */
#define TEST_DEVICE 0x100000UL

void sys_poweroff(void) {
    puts("\nPowering off...\n");
    *(volatile uint32_t *)(TEST_DEVICE) = 0x5555;
    for (;;);
}


ErrorContext _error_context;

void _module_register(const char *name) {
    strncpy(_error_context.module, name, sizeof(_error_context.module) - 1);
    _error_context.module[sizeof(_error_context.module) - 1] = '\0';
}

void _save_error_context(const char *module, const char *function,
                         uint32_t pointer, const char *message) {
    strncpy(_error_context.module, module ? module : "?", sizeof(_error_context.module) - 1);
    _error_context.module[sizeof(_error_context.module) - 1] = '\0';
    strncpy(_error_context.function, function ? function : "?", sizeof(_error_context.function) - 1);
    _error_context.function[sizeof(_error_context.function) - 1] = '\0';
    _error_context.pointer = pointer;
    strncpy(_error_context.message, message ? message : "", sizeof(_error_context.message) - 1);
    _error_context.message[sizeof(_error_context.message) - 1] = '\0';
    _error_context.timestamp = sched_get_ticks();
}

void _print_error_context(void) {
    printf("\n=== Error Context ===\n");
    printf("  module: %s\n", _error_context.module);
    printf("  function: %s\n", _error_context.function);
    printf("  pointer: 0x%08x\n", _error_context.pointer);
    printf("  message: %s\n", _error_context.message);
    printf("  tick: %u\n", (unsigned)_error_context.timestamp);
}

void _panic(const char *message, const char *file, int line) {
    printf("\n[PANIC] %s (file: %s, line: %d)\n", message, file, line);
    _print_error_context();
    guard_panic(message);       /* 守护系统接管: 转储后停机 */
    for (;;);
}

static uint32_t _watchdog_deadline = 0;
void watchdog_init(uint32_t timeout_ticks) {
    _watchdog_deadline = sched_get_ticks() + timeout_ticks;
}
void watchdog_feed(void) {
    _watchdog_deadline = sched_get_ticks() + 100;
}
void watchdog_check(void) {
    if (sched_get_ticks() > _watchdog_deadline) {
        _panic("watchdog timeout", __FILE__, __LINE__);
    }
}

const char *_log_level_str(int level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default:              return "NONE";
    }
}

void kernel_main(void) {
    LOG_INFO("KERNEL", "Kresten Kernel v3.1 (RISC-V) starting...");
    LOG_INFO("KERNEL", "Assertion system ready");

    int x = 42;
    assert(x == 42);
    LOG_INFO("KERNEL", "Assertion test passed");

    guard_init();                   /* 崩溃守护: 武装 (接管异常/看门狗) */

    heap_init(0, 0);
    guard_kick();
    env_init();
    proc_init();
    guard_watch();
    guard_kick();
    drivers_register_all();
    driver_init_all();
    drv_load_all();        /* 外围驱动 .kdrv: 嵌入表 (timer/keyboard/fs 已静态注册) */
    guard_watch();
    guard_kick();
    net_init();
    guard_watch();
    guard_kick();
    plugin_init_all();
    guard_watch();
    while (1)
        plugin_dispatch("desktop", NULL);
}

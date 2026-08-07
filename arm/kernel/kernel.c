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

uint32_t sched_get_ticks(void);

int drivers_register_all(void);

void sys_poweroff(void) {
    puts("\nPowering off...\n");
    __asm__ volatile ("mov r0, #0x18; ldr r1, =0x20026; svc 0x123456");
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
    LOG_INFO("KERNEL", "Kresten Kernel v1.0 starting...");
    LOG_INFO("KERNEL", "Assertion system ready");

    int x = 42;
    assert(x == 42);
    LOG_INFO("KERNEL", "Assertion test passed");

    heap_init(0, 0);
    rtc_init();
    env_init();
    proc_init();
    drivers_register_all();
    driver_init_all();
    net_init();
    plugin_init_all();
    while (1)
        plugin_dispatch("desktop", NULL);
}

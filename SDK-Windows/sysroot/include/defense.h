#ifndef _DEFENSE_H
#define _DEFENSE_H

#include <stdio.h>
#include <stdint.h>

// ============================================
// 1. 日志系统
// ============================================
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#define LOG(level, module, fmt, ...) \
    do { \
        if (level <= LOG_LEVEL) { \
            printf("[%s] %s: " fmt "\n", _log_level_str(level), module, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_ERROR(module, fmt, ...)  LOG(LOG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)   LOG(LOG_LEVEL_WARN, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...)   LOG(LOG_LEVEL_INFO, module, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(module, fmt, ...)  LOG(LOG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)

const char *_log_level_str(int level);

// ============================================
// 2. 断言系统
// ============================================
#define assert(expr) \
    do { \
        if (!(expr)) { \
            LOG_ERROR("ASSERT", "Failed: %s, file %s, line %d", \
                      #expr, __FILE__, __LINE__); \
            _panic("Assertion failed", __FILE__, __LINE__); \
        } \
    } while (0)

#define assert_msg(expr, msg) \
    do { \
        if (!(expr)) { \
            LOG_ERROR("ASSERT", "Failed: %s, file %s, line %d: %s", \
                      #expr, __FILE__, __LINE__, msg); \
            _panic(msg, __FILE__, __LINE__); \
        } \
    } while (0)

// ============================================
// 3. 栈哨兵
// ============================================
#define STACK_SENTINEL 0xDEADBEEF

void _stack_check_init(void);
void _stack_check(void);

// ============================================
// 4. 错误现场（恢复环境）
// ============================================
typedef struct {
    // 错误信息
    char module[32];          // 出错的模块名
    char function[64];        // 出错的函数名
    uint32_t pointer;         // 出错的指针地址
    uint32_t instruction;     // 当前指令地址（PC）
    uint32_t stack_pointer;   // 栈指针（SP）
    uint32_t link_register;   // 链接寄存器（LR）
    uint32_t registers[16];   // 所有通用寄存器
    char message[128];        // 附加错误信息
    uint32_t timestamp;       // 错误发生时间（tick）
} ErrorContext;

// 全局错误上下文
extern ErrorContext _error_context;

// 保存错误现场
void _save_error_context(const char *module, const char *function,
                         uint32_t pointer, const char *message);

// 打印错误现场
void _print_error_context(void);

// 系统崩溃（死循环，保留现场）
void _panic(const char *message, const char *file, int line) __attribute__((noreturn));

// ============================================
// 5. 看门狗
// ============================================
void watchdog_init(uint32_t timeout_ticks);
void watchdog_feed(void);
void watchdog_check(void);

// ============================================
// 6. 模块注册（用于错误定位）
// ============================================
#define REGISTER_MODULE(name) \
    static void __attribute__((constructor)) _register_module_##name(void) { \
        _module_register(#name); \
    }

void _module_register(const char *name);

// ============================================
// 7. 安全的内存访问
// ============================================
#define SAFE_READ(addr, fallback) \
    ({ \
        uint32_t _val = (fallback); \
        if ((uint32_t)(addr) > 0x20000000 && (uint32_t)(addr) < 0x30000000) { \
            _val = *(volatile uint32_t *)(addr); \
        } else { \
            LOG_WARN("MEMORY", "Invalid read at 0x%08x", (uint32_t)(addr)); \
        } \
        _val; \
    })

#define SAFE_WRITE(addr, val) \
    do { \
        if ((uint32_t)(addr) > 0x20000000 && (uint32_t)(addr) < 0x30000000) { \
            *(volatile uint32_t *)(addr) = (val); \
        } else { \
            LOG_WARN("MEMORY", "Invalid write at 0x%08x", (uint32_t)(addr)); \
        } \
    } while (0)

#endif

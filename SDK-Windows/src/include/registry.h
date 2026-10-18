#ifndef _REGISTRY_H
#define _REGISTRY_H

#include <stdint.h>

#define REG_FILE       "C:\\REGISTRY.TXT"
#define REG_MAX_KEYS   64
#define REG_KEY_MAX    32
#define REG_VAL_MAX    64

void reg_persist_setup(uint32_t mb_info);
void reg_init(void);
const char *reg_get(const char *key);
int reg_set(const char *key, const char *val);
int reg_del(const char *key);
void reg_list(void);
int reg_save(void);

#endif

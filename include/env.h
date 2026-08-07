#ifndef _ENV_H
#define _ENV_H

void env_init(void);
const char *env_get(const char *key);
int env_set(const char *key, const char *val);
int env_unset(const char *key);
void env_list(void);
char *env_expand(const char *str);

#endif

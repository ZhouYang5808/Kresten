#include <env.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_ENV 32

static char *env_keys[MAX_ENV];
static char *env_vals[MAX_ENV];
static int env_count = 0;

void env_init(void) {
    env_count = 0;
    env_set("SHELL", "kresten");
    env_set("PATH", "/");
    env_set("PS1", "%s> ");
}

const char *env_get(const char *key) {
    for (int i = 0; i < env_count; i++)
        if (strcmp(env_keys[i], key) == 0)
            return env_vals[i];
    return 0;
}

int env_set(const char *key, const char *val) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_keys[i], key) == 0) {
            free(env_vals[i]);
            env_vals[i] = strdup(val ? val : "");
            return 0;
        }
    }
    if (env_count >= MAX_ENV) return -1;
    env_keys[env_count] = strdup(key);
    env_vals[env_count] = strdup(val ? val : "");
    env_count++;
    return 0;
}

int env_unset(const char *key) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_keys[i], key) == 0) {
            free(env_keys[i]); free(env_vals[i]);
            for (int j = i; j < env_count - 1; j++) {
                env_keys[j] = env_keys[j + 1];
                env_vals[j] = env_vals[j + 1];
            }
            env_count--;
            return 0;
        }
    }
    return -1;
}

void env_list(void) {
    for (int i = 0; i < env_count; i++)
        printf("%s=%s\n", env_keys[i], env_vals[i]);
}

static char expand_buf[256];

char *env_expand(const char *str) {
    int pos = 0;
    while (*str && pos < 255) {
        if (*str == '$') {
            str++;
            char key[32];
            int ki = 0;
            if (*str == '{') {
                str++;
                while (*str && *str != '}' && ki < 30) key[ki++] = *str++;
                key[ki] = '\0';
                if (*str == '}') str++;
            } else {
                while (*str && (isalnum(*str) || *str == '_') && ki < 30)
                    key[ki++] = *str++;
                key[ki] = '\0';
            }
            const char *val = env_get(key);
            if (val) {
                int vlen = strlen(val);
                int remain = 255 - pos;
                int copy = vlen < remain ? vlen : remain;
                memcpy(expand_buf + pos, val, copy);
                pos += copy;
            }
        } else {
            expand_buf[pos++] = *str++;
        }
    }
    expand_buf[pos] = '\0';
    return expand_buf;
}

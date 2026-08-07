#include <editor.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fs.h>

#define MAX_LINES 512
#define MAX_LINE_LEN 256

static char *lines[MAX_LINES];
static int line_count = 0;
static int cur_line = 0;
static char filename[64];
static int modified = 0;

static void load_file(const char *name) {
    for (int i = 0; i < line_count; i++) { free(lines[i]); lines[i] = 0; }
    line_count = 0; cur_line = 0;
    strncpy(filename, name, 63); filename[63] = '\0';
    modified = 0;
    char *content = fs_read_file_content(name);
    if (!content) {
        line_count = 1;
        lines[0] = malloc(1);
        if (lines[0]) lines[0][0] = '\0';
        cur_line = 0;
        return;
    }
    char *p = content;
    while (*p && line_count < MAX_LINES) {
        char line[MAX_LINE_LEN];
        int i = 0;
        while (*p && *p != '\n' && i < MAX_LINE_LEN - 1) line[i++] = *p++;
        line[i] = '\0';
        lines[line_count] = strdup(line);
        if (!lines[line_count]) lines[line_count] = strdup("");
        line_count++;
        if (*p == '\n') p++;
    }
    if (line_count == 0) { line_count = 1; lines[0] = strdup(""); }
    cur_line = 0;
}

static int save_file(void) {
    int total = 0;
    for (int i = 0; i < line_count; i++) total += strlen(lines[i]) + 1;
    char *buf = malloc(total + 1);
    if (!buf) return -1;
    int pos = 0;
    for (int i = 0; i < line_count; i++) {
        int len = strlen(lines[i]);
        memcpy(buf + pos, lines[i], len);
        pos += len;
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    char resolved[64];
    if (!fs_resolve_path(filename, resolved, sizeof(resolved))) return -1;
    int ret;
    if (fs_find_entry(resolved)) ret = fs_update_file(resolved, buf);
    else ret = fs_create_file(resolved, buf);
    free(buf);
    if (ret == 0) modified = 0;
    return ret;
}

static void print_line(int idx) {
    if (idx < 0 || idx >= line_count) return;
    printf("%5d  %s\n", idx + 1, lines[idx] ? lines[idx] : "");
}

static void list_lines(int from, int to) {
    if (from < 0) from = 0;
    if (to >= line_count) to = line_count - 1;
    for (int i = from; i <= to; i++) print_line(i);
}

static int insert_line(int after, const char *text) {
    if (line_count >= MAX_LINES) { puts("Buffer full\n"); return -1; }
    for (int i = line_count; i > after + 1; i--) lines[i] = lines[i - 1];
    lines[after + 1] = strdup(text ? text : "");
    line_count++;
    modified = 1;
    return after + 1;
}

static int delete_line(int idx) {
    if (idx < 0 || idx >= line_count) return -1;
    free(lines[idx]);
    for (int i = idx; i < line_count - 1; i++) lines[i] = lines[i + 1];
    line_count--;
    modified = 1;
    if (cur_line >= line_count) cur_line = line_count - 1;
    return 0;
}

static int search(const char *text, int start) {
    for (int i = start; i < line_count; i++)
        if (strstr(lines[i], text)) return i;
    for (int i = 0; i < start; i++)
        if (strstr(lines[i], text)) return i;
    return -1;
}

void editor_run(const char *name) {
    load_file(name);
    printf("\"%s\" %d lines\n", filename, line_count);
    if (cur_line < line_count) print_line(cur_line);
    while (1) {
        printf(": ");
        char cmd[128];
        gets(cmd, 128);
        char *p = cmd;
        while (*p && isspace(*p)) p++;
        if (!*p) continue;
        if (isdigit(*p)) {
            int n = 0;
            while (isdigit(*p)) n = n * 10 + (*p++ - '0');
            if (n >= 1 && n <= line_count) { cur_line = n - 1; print_line(cur_line); }
            else puts("?\n");
            while (*p && isspace(*p)) p++;
            if (!*p) continue;
        }
        switch (*p) {
        case 'q':
            if (modified) { puts("No write since last change (q! to quit)\n"); continue; }
            puts("Quitting editor\n"); return;
        case 'Q':
            puts("Quitting editor\n"); return;
        case 'w': {
            char fn[64] = {0};
            char *arg = p + 1;
            while (*arg && isspace(*arg)) arg++;
            if (*arg) strncpy(fn, arg, 63);
            else strncpy(fn, filename, 63);
            fn[63] = '\0';
            if (save_file() == 0) printf("\"%s\" %d lines written\n", fn, line_count);
            else puts("Write failed\n");
            break;
        }
        case 'p':
            print_line(cur_line);
            break;
        case 'n':
            printf("%d\n", cur_line + 1);
            break;
        case '=':
            printf("%d\n", line_count);
            break;
        case 'l':
            list_lines(0, line_count - 1);
            break;
        case 'a': {
            puts("Enter text ('.' on empty line to exit append mode):\n");
            int after = cur_line;
            while (1) {
                printf("%5d: ", after + 2);
                char line[MAX_LINE_LEN];
                gets(line, MAX_LINE_LEN);
                if (line[0] == '.' && line[1] == '\0') break;
                after = insert_line(after, line);
                if (after < 0) break;
            }
            if (after >= 0) cur_line = after;
            break;
        }
        case 'i': {
            puts("Enter text ('.' on empty line to exit insert mode):\n");
            int before = cur_line - 1;
            while (1) {
                printf("%5d: ", before + 2);
                char line[MAX_LINE_LEN];
                gets(line, MAX_LINE_LEN);
                if (line[0] == '.' && line[1] == '\0') break;
                before = insert_line(before, line);
                if (before < 0) break;
            }
            if (before >= 0) cur_line = before;
            break;
        }
        case 'd': {
            int step = 1;
            char *ap = p + 1;
            while (*ap && isspace(*ap)) ap++;
            if (isdigit(*ap)) {
                step = 0;
                while (isdigit(*ap)) step = step * 10 + (*ap++ - '0');
            }
            if (!p[1] || isspace(p[1])) {
                for (int s = 0; s < step && cur_line < line_count; s++)
                    delete_line(cur_line);
                if (cur_line >= line_count) cur_line = line_count - 1;
                if (cur_line >= 0) print_line(cur_line);
            } else goto unknown;
            break;
        }
        case 'c': {
            char *ap = p + 1;
            while (*ap && isspace(*ap)) ap++;
            if (*ap == '/') {
                ap++;
                char search_text[64];
                int si = 0;
                while (*ap && *ap != '/' && si < 63) search_text[si++] = *ap++;
                search_text[si] = '\0';
                int found = search(search_text, cur_line + 1);
                if (found >= 0) { cur_line = found; print_line(cur_line); }
                else puts("Not found\n");
            } else goto unknown;
            break;
        }
        default:
            unknown:
            printf("?\n");
            break;
        }
    }
}

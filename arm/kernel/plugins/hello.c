#include <plugin.h>
#include <stdio.h>

int plugin_hello_init(void) {
    puts("[HELLO] Plugin active! Type 'hello' to test.\n");
    return 0;
}

int plugin_hello_cmd(char *args) {
    puts("Hello from plugin!");
    if (args && *args) { puts(" Args: "); puts(args); }
    putchar('\n');
    return 0;
}

REGISTER_PLUGIN(hello);

/*
 * Plugin Template for Kresten
 *
 * A loadable plugin (.kplg) is an ELF64 shared object that exports the
 * plugin_<name>_init / plugin_<name>_cmd hooks and registers itself via
 * REGISTER_PLUGIN(<name>). Build it from a Kresten source tree with the
 * plugin flags (see PLUGIN_CFLAGS in the kernel Makefile), for example:
 *
 *   gcc -m64 -nostdlib -ffreestanding -fPIC -fno-stack-protector \
 *       -mno-red-zone -Wall -Werror -DX86_ARCH -I<tree>/include \
 *       -c plugin.c -o plugin.o
 *   ld -melf_x86_64 -shared -z max-page-size=0x1000 -o my_plugin.kplg plugin.o
 *
 * Drop my_plugin.kplg into /plugins/user/ on the ISO, or load it at runtime
 * from the shell with:  plugins load my_plugin
 */
#include <plugin.h>
#include <stdio.h>

int plugin_my_plugin_init(void) {
    puts("[MY_PLUGIN] active\n");
    return 0;
}

int plugin_my_plugin_cmd(char *args) {
    puts("my_plugin: hello from a loadable plugin!\n");
    (void)args;
    return 0;
}

REGISTER_PLUGIN(my_plugin);

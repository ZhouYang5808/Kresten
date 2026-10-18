#include <plugin.h>

/* Pass-1 stub: lets the first link resolve kplg.c's references before the
 * real table is generated.  .ksymtab is the LAST section in link.ld, so
 * replacing this stub with the real table does not move any other symbol. */
__attribute__((section(".ksymtab"), used))
const Ksym kernel_symtab[] = { { 0, 0 } };

__attribute__((section(".ksymtab"), used))
const uint32_t kernel_symtab_count = 0;

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

int main(int argc, char **argv) {
    printf("Hello from a real C program (static glibc)!\n");
    printf("argc=%d argv[0]=%s\n", argc, argc > 0 ? argv[0] : "?");
    struct utsname u;
    if (uname(&u) == 0)
        printf("uname: %s %s %s\n", u.sysname, u.release, u.machine);
    printf("time: %ld\n", (long)time(0));
    return 0;
}

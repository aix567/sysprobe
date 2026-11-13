
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

int main(void)
{
    struct utsname info;

    if (uname(&info) != 0) {
        perror("uname");
        return EXIT_FAILURE;
    }

    printf("=== SysProbe v0.1 ===\n");
    printf("System:       %s\n", info.sysname);
    printf("Node:         %s\n", info.nodename);
    printf("Kernel:       %s\n", info.release);
    printf("Architecture: %s\n", info.machine);

    return EXIT_SUCCESS;
}

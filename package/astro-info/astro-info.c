#include <stdio.h>
#include <sys/utsname.h>

int main(void)
{
    struct utsname u;

    if (uname(&u) != 0) {
        perror("uname");
        return 1;
    }

    printf("Astro OS 0.6.0\n");
    printf("Kernel: %s\n", u.release);
    printf("Architecture: %s\n", u.machine);
    printf("Hostname: %s\n", u.nodename);

    return 0;
}

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

static void print_os_name(void)
{
    char line[256];
    FILE *f = fopen("/etc/os-release", "r");

    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                line[strcspn(line, "\n")] = '\0';
                char *name = line + 12;
                size_t len = strlen(name);

                if (len >= 2 && name[0] == '"' && name[len - 1] == '"') {
                    name[len - 1] = '\0';
                    name++;
                }
                printf("%s\n", name);
                fclose(f);
                return;
            }
        }
        fclose(f);
    }
    printf("Astro OS (unknown version)\n");
}

int main(void)
{
    struct utsname u;

    if (uname(&u) != 0) {
        perror("uname");
        return 1;
    }

    print_os_name();
    printf("Kernel: %s\n", u.release);
    printf("Architecture: %s\n", u.machine);
    printf("Hostname: %s\n", u.nodename);

    return 0;
}

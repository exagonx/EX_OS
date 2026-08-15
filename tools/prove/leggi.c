#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    struct stat st;
    static char buf[65536];
    int fd; long size, total = 0, count;

    if (argc < 2) return 1;
    printf("sizeof(struct stat) = %d, offset st_size = %d\n",
           (int)sizeof(struct stat), (int)((char*)&st.st_size - (char*)&st));

    /* Esattamente come libcpp: O_RDONLY | O_NOCTTY | O_BINARY */
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif
    fd = open(argv[1], O_RDONLY | O_NOCTTY | O_BINARY, 0666);
    printf("open = %d\n", fd);
    if (fd < 0) return 1;

    if (fstat(fd, &st) != 0) { printf("fstat fallita\n"); return 1; }
    printf("st_size = %d  st_mode = 0%o  S_ISREG = %d  S_ISBLK = %d\n",
           (int)st.st_size, (unsigned)st.st_mode,
           S_ISREG(st.st_mode) ? 1 : 0, S_ISBLK(st.st_mode) ? 1 : 0);

    size = (long)st.st_size;
    while ((count = (long)read(fd, buf + total, (unsigned)(size - total))) > 0)
        total += count;
    printf("ciclo alla libcpp: total = %d (size era %d)\n", (int)total, (int)size);
    close(fd);
    return 0;
}

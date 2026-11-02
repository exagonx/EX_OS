/* tools/prove/leggitempi.c FILE BYTE_PER_READ...: how fast a file reads,
 * once per size given. To measure the disk and not `cat`, which reads 512 bytes
 * at a time. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static double adesso(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
    int i;

    for (i = 2; i < argc; i++) {
        unsigned int bs = (unsigned)atoi(argv[i]);
        char *b = malloc(bs);
        unsigned long long tot = 0;
        double t0 = adesso(), dt;
        int fd = open(argv[1], O_RDONLY), n;

        if (fd < 0 || b == NULL) { puts("leggitempi: non apro"); return 1; }
        while ((n = (int)read(fd, b, bs)) > 0) tot += (unsigned)n;
        close(fd);
        dt = adesso() - t0;
        printf("%u byte per read: %llu MB in %d ms = %d MB/s\n", bs, tot >> 20,
               (int)(dt * 1000), dt > 0 ? (int)((double)(tot >> 20) / dt) : 0);
        free(b);
    }
    return 0;
}

/* =============================================================================
 * tools/prove/shmfili.c - shm_open(SHM_ANON) from many threads at once
 *
 *     shmfili [fili] [giri] [tenute]   (8, 3000 and 64 by default)
 *
 * Each thread keeps `tenute` zones open (a ring), so the zones of the
 * process pass 64, 128, 256, 512: the libc's table of zones doubles while
 * the other threads use it - what Firefox does on a page full of images.
 *
 * What Firefox does from its threads (images, canvas, WebGL): a private zone,
 * ftruncate to its size, a dup to hand over, fstat to check it
 * (ipc/glue/SharedMemoryPlatform_posix.cpp, IsSafeToMap), close. Every thread
 * checks that fstat says the size it asked and that the memory it wrote is
 * still its own. Firefox on unsplash.com (22/11/2026) ended with "IPDL
 * protocol error: Shared memory PlatformHandle is not safe to map": an fstat
 * with a smaller size than the one asked.
 *
 * Every other round the copy is made the way Gecko clones a handle,
 * fcntl(fd, F_DUPFD_CLOEXEC, 0) (mfbt/UniquePtrExtensions.cpp): up to the libc
 * of 23/11/2026 that answered 0 for a zone, stdin, and fstat said its size.
 *
 * Exit: 0 if nothing was wrong, 1 otherwise.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int g_giri = 3000, g_tenute = 64;
static volatile int g_sbagli_fstat, g_sbagli_dati, g_falliti, g_sbagli_copia;

typedef struct { int fd; size_t lung; unsigned char *m; } Tenuta;

static void controlla(int id, int i, const Tenuta *t)
{
    struct stat st;
    int         ok = fstat(t->fd, &st) == 0 && (size_t)st.st_size >= t->lung;

    if (!ok && __sync_fetch_and_add(&g_sbagli_fstat, 1) < 5)
        printf("  filo %d giro %d: fstat dice %ld, chiesti %lu\n", id, i,
               (long)st.st_size, (unsigned long)t->lung);
    if ((t->m[0] != id + 1 || t->m[t->lung - 1] != id + 1) &&
        __sync_fetch_and_add(&g_sbagli_dati, 1) < 5)
        printf("  filo %d giro %d: la memoria e' di un altro (%u)\n", id, i, t->m[0]);
}

static void *filo(void *arg)
{
    int     id = (int)(long)arg, i;
    Tenuta *r = calloc((size_t)g_tenute, sizeof(Tenuta));

    for (i = 0; i < g_giri; i++) {
        Tenuta *t = &r[i % g_tenute];
        int     fd, d;

        if (t->m) {                     /* the oldest goes: checked, closed */
            controlla(id, i, t);
            munmap(t->m, t->lung);
            close(t->fd);
            t->m = NULL;
        }
        t->lung = 4096u * (size_t)(1 + (id * 7 + i) % 16);
        fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
        if (fd < 0) { __sync_fetch_and_add(&g_falliti, 1); continue; }
        if (ftruncate(fd, (off_t)t->lung) != 0) {
            __sync_fetch_and_add(&g_falliti, 1);
            close(fd);
            continue;
        }
        t->m = mmap(NULL, t->lung, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (t->m == MAP_FAILED) { t->m = NULL; __sync_fetch_and_add(&g_falliti, 1); close(fd); continue; }
        memset(t->m, id + 1, t->lung);
        /* the handle that goes to the other side, half of them as Gecko clones */
        d = (i & 1) ? fcntl(fd, F_DUPFD_CLOEXEC, 0) : dup(fd);
        if (d == fd || d == 0 || d == 1 || d == 2) {
            if (__sync_fetch_and_add(&g_sbagli_copia, 1) < 5)
                printf("  filo %d giro %d: la copia di %d e' %d\n", id, i, fd, d);
            if (d > 2 && d != fd) close(d);
            d = -1;
        }
        close(fd);
        if (d < 0) {
            if (!g_sbagli_copia) __sync_fetch_and_add(&g_falliti, 1);
            munmap(t->m, t->lung);
            t->m = NULL;
            continue;
        }
        t->fd = d;
        controlla(id, i, t);
    }
    for (i = 0; i < g_tenute; i++)
        if (r[i].m) { controlla(id, g_giri, &r[i]); munmap(r[i].m, r[i].lung); close(r[i].fd); }
    free(r);
    return NULL;
}

int main(int argc, char **argv)
{
    int       n = argc > 1 ? atoi(argv[1]) : 8, i;
    pthread_t t[64];

    if (n < 1 || n > 64) n = 8;
    if (argc > 2) g_giri = atoi(argv[2]);
    if (argc > 3) g_tenute = atoi(argv[3]);
    if (g_tenute < 1) g_tenute = 1;
    for (i = 0; i < n; i++) pthread_create(&t[i], NULL, filo, (void *)(long)i);
    for (i = 0; i < n; i++) pthread_join(t[i], NULL);
    printf("shmfili: %d fili x %d giri, %d tenute: %d fstat sbagliati, %d memorie altrui, %d copie sbagliate, %d falliti\n",
           n, g_giri, g_tenute, g_sbagli_fstat, g_sbagli_dati, g_sbagli_copia, g_falliti);
    return (g_sbagli_fstat || g_sbagli_dati || g_sbagli_copia || g_falliti) ? 1 : 0;
}

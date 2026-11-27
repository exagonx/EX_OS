/* =============================================================================
 * tools/prove/semprova.c - semaphores between processes (0.373)
 *
 *     semprova          the test (lines SEM <name> ...)
 *     semprova figlio   the exec'd process: opens "/semprova" by name,
 *                       waits 300 ms, posts, then 1000 posts on "/semmille"
 *
 *   nome     sem_open O_CREAT|O_EXCL, an exec'd process posts after 300 ms:
 *            sem_wait returns after 250-1500 ms
 *   stesso   the same name again in this process: the same sem_t
 *   eexist   O_CREAT|O_EXCL on it: EEXIST
 *   mille    1000 posts from the other process, 1000 waits here: all of them
 *   pshared  sem_init(pshared) in MAP_SHARED|MAP_ANONYMOUS memory, a forked
 *            child posts after 200 ms
 *   scade    sem_timedwait 200 ms with nothing: ETIMEDOUT after 200-400 ms
 *   valore   two posts: sem_getvalue 2
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>

static long ms_ora(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

int main(int argc, char **argv)
{
    sem_t          *s, *s2, *m, *p;
    struct timespec t;
    long            t0;
    pid_t           f;
    int             i, v, r, e;

    if (argc > 1 && strcmp(argv[1], "figlio") == 0) {
        s = sem_open("/semprova", 0);
        m = sem_open("/semmille", 0);
        if (s == SEM_FAILED || m == SEM_FAILED) { printf("SEM figlio-apre ERRORE %d\n", errno); return 1; }
        usleep(300000);
        sem_post(s);
        for (i = 0; i < 1000; i++) sem_post(m);
        return 0;
    }

    s = sem_open("/semprova", O_CREAT | O_EXCL, 0600, 0);
    m = sem_open("/semmille", O_CREAT | O_EXCL, 0600, 0);
    if (s == SEM_FAILED || m == SEM_FAILED) { printf("SEM crea ERRORE %d\n", errno); return 1; }
    s2 = sem_open("/semprova", 0);
    printf("SEM stesso %d\n", s2 == s);
    sem_close(s2);
    r = sem_open("/semprova", O_CREAT | O_EXCL, 0600, 0) == SEM_FAILED; e = errno;
    printf("SEM eexist %d\n", r && e == EEXIST);

    f = fork();
    if (f == 0) { execl(argv[0], argv[0], "figlio", (char *)0); _exit(99); }
    t0 = ms_ora();
    r = sem_wait(s);
    printf("SEM nome r=%d ms=%ld\n", r, ms_ora() - t0);
    for (i = 0; i < 1000; i++) {
        t.tv_sec = time(NULL) + 10; t.tv_nsec = 0;
        if (sem_timedwait(m, &t) != 0) break;
    }
    sem_getvalue(m, &v);
    printf("SEM mille presi=%d resto=%d\n", i, v);
    waitpid(f, &r, 0);

    p = (sem_t *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED || sem_init(p, 1, 0) != 0) printf("SEM pshared ERRORE %d\n", errno);
    else {
        f = fork();
        if (f == 0) { usleep(200000); sem_post(p); _exit(0); }
        t0 = ms_ora();
        r = sem_wait(p);
        printf("SEM pshared r=%d ms=%ld\n", r, ms_ora() - t0);
        waitpid(f, NULL, 0);
    }

    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_nsec += 200000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    t0 = ms_ora();
    r = sem_timedwait(s, &t); e = errno;
    printf("SEM scade etimedout=%d ms=%ld\n", r != 0 && e == ETIMEDOUT, ms_ora() - t0);

    sem_post(s); sem_post(s);
    sem_getvalue(s, &v);
    printf("SEM valore %d\n", v);
    sem_close(s); sem_close(m);
    sem_unlink("/semprova"); sem_unlink("/semmille");
    printf("SEM FINE\n");
    return 0;
}

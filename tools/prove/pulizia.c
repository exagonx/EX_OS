/* tools/prove/pulizia.c - pthread_cleanup_push: pthread_exit runs the
 * handlers, the last pushed first; pop(0) drops one, pop(1) runs it. A thread
 * that dies by pthread_exit holding a mutex unlocks it through its handler,
 * as VLC's cancelled threads do. Prints PULIZIA-OK or PULIZIA-NO. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static char ordine[16];

static void segna(void *x) { strncat(ordine, (const char *)x, 1); }
static void sblocca(void *x) { (void)x; strcat(ordine, "u"); pthread_mutex_unlock(&m); }

static void *filo(void *x)
{
    (void)x;
    pthread_mutex_lock(&m);
    pthread_cleanup_push(sblocca, NULL);
    pthread_cleanup_push(segna, "a");
    pthread_cleanup_push(segna, "z");
    pthread_cleanup_pop(0);             /* "z" dropped */
    pthread_cleanup_push(segna, "b");
    pthread_cleanup_pop(1);             /* "b" now */
    pthread_exit(NULL);                 /* then "a", then unlock */
    pthread_cleanup_pop(0);
    pthread_cleanup_pop(0);
    return NULL;
}

int main(void)
{
    pthread_t t;
    pthread_create(&t, NULL, filo, NULL);
    pthread_join(t, NULL);
    int libero = pthread_mutex_trylock(&m) == 0;
    int ok = libero && strcmp(ordine, "bau") == 0;
    printf("PULIZIA-%s ordine=%s mutex %s\n", ok ? "OK" : "NO", ordine, libero ? "libero" : "PRESO");
    return ok ? 0 : 1;
}

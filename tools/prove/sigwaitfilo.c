/* tools/prove/sigwaitfilo.c - what VLC's main does to wait for its end:
 * SIGTERM blocked, another thread sends it with pthread_kill to the main
 * thread, the main thread takes it with sigwait. Prints SIGWAIT-OK. */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static pthread_t principale;

static void *manda(void *x)
{
    (void)x;
    usleep(300000);
    int r = pthread_kill(principale, SIGTERM);
    printf("pthread_kill: %d\n", r);
    return NULL;
}

int main(void)
{
    sigset_t set;
    int sig = 0;
    pthread_t t;

    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_SETMASK, &set, NULL);
    principale = pthread_self();
    pthread_create(&t, NULL, manda, NULL);
    printf("sigwait...\n");
    int r = sigwait(&set, &sig);
    printf("SIGWAIT-%s r=%d sig=%d\n", (r == 0 && sig == SIGTERM) ? "OK" : "NO", r, sig);
    pthread_join(t, NULL);
    return 0;
}

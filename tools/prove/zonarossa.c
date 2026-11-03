/* tools/prove/zonarossa.c - a leaf function touching a new stack page through
 * the red zone, 64 bytes below rsp (as bn_mul of NSS does), in the main thread
 * and in another one. The stack pointer is set by hand: the page must be one
 * that nobody has touched yet. x86_64 only. */
#include <stdio.h>
#include <pthread.h>

static void tocca(void)
{
    unsigned long cima, nuovo, salvo;

    __asm__ volatile ("mov %%rsp, %0" : "=r"(cima));
    /* the page below the next one: new rsp 16 bytes in, the write in the page under it */
    nuovo = (cima & ~4095ul) - 2 * 4096 + 16;
    __asm__ volatile (
        "mov %%rsp, %0\n"
        "mov %1, %%rsp\n"
        "movq $0x1234, -64(%%rsp)\n"
        "mov %0, %%rsp\n"
        : "=&r"(salvo) : "r"(nuovo) : "memory");
}

static void *filo(void *a)
{
    (void)a;
    tocca();
    return (void *)1;
}

int main(void)
{
    pthread_t t;
    void *r = 0;

    tocca();
    printf("zonarossa: filo principale a posto\n");
    fflush(stdout);
    if (pthread_create(&t, 0, filo, 0) != 0) { printf("niente filo\n"); return 2; }
    pthread_join(t, &r);
    printf("zonarossa: secondo filo %s\n", r ? "a posto" : "NO");
    return r ? 0 : 1;
}

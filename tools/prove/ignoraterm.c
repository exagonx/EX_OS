/* tools/prove/ignoraterm.c - a program that ignores SIGTERM and keeps a file
 * open: with it, only the SIGKILL of segnali_arresto frees the volume
 * (tools/prova_smonta.sh).   ignoraterm FILE */
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2 || open(argv[1], O_RDWR) < 0) return 1;
    signal(SIGTERM, SIG_IGN);
    for (;;) sleep(100);
}

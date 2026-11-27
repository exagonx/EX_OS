/* =============================================================================
 * tools/prove/statfsprova.c - statfs, fstatfs and statvfs (voce 103)
 *
 *     statfsprova <path> ...
 *
 * For each path: STATFS <path> tipo=<hex> blocco=<n> blocchi=<n> liberi=<n>
 * disp=<n> inode=<n> nome=<n> ro=<0|1>, then the same from fstatfs on the
 * open file and from statvfs: they must agree (FSTATFS-UGUALE, STATVFS-UGUALE
 * or -DIVERSO). tools/prova_statfs.sh reads the lines.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        struct statfs  a, b;
        struct statvfs v;
        int            fd;

        if (statfs(argv[i], &a) != 0) { printf("STATFS %s ERRORE\n", argv[i]); continue; }
        printf("STATFS %s tipo=%lx blocco=%ld blocchi=%lu liberi=%lu disp=%lu inode=%lu nome=%ld ro=%ld\n",
               argv[i], (unsigned long)a.f_type, a.f_bsize, a.f_blocks, a.f_bfree, a.f_bavail,
               a.f_files, a.f_namelen, a.f_flags & 1);
        fd = open(argv[i], O_RDONLY);
        if (fd >= 0 && fstatfs(fd, &b) == 0)
            printf("FSTATFS %s %s\n", argv[i], a.f_type == b.f_type && a.f_blocks == b.f_blocks &&
                   a.f_bsize == b.f_bsize ? "UGUALE" : "DIVERSO");
        else
            printf("FSTATFS %s NONAPRE\n", argv[i]);
        if (fd >= 0) close(fd);
        if (statvfs(argv[i], &v) == 0)
            printf("STATVFS %s %s\n", argv[i], v.f_blocks == a.f_blocks && v.f_bsize == (unsigned long)a.f_bsize &&
                   v.f_bfree == a.f_bfree ? "UGUALE" : "DIVERSO");
    }
    return 0;
}

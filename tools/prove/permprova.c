/* =============================================================================
 * tools/prove/permprova.c - what an ordinary user can reach
 *
 *     permprova [uid [gid]]        run as root: it goes down to uid (1000)
 *
 * Measures, does not judge: each line says what the kernel allowed to a
 * user that is not root. Made for the question of 22/11/2026 (on the .253
 * the user graziano could see /root and the files of the system).
 *
 *   [SI] = allowed, [NO] = refused (errno), as Linux would answer for the
 *   same modes is written next to it: "linux: no" where Linux refuses.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static int g_falle, g_stretti;

static void riga(const char *cosa, int riuscito, int linux_si)
{
    printf("  [%s] %-48s linux: %s%s\n", riuscito ? "SI" : "NO", cosa,
           linux_si ? "si" : "no",
           (riuscito && !linux_si) ? "   <-- FALLA" :
           (!riuscito && linux_si) ? "   <-- TROPPO STRETTO" : "");
    if (riuscito && !linux_si) g_falle++;
    if (!riuscito && linux_si) g_stretti++;
}

static int elenca(const char *d)
{
    DIR *p = opendir(d);
    int n = 0;
    if (!p) return 0;
    while (readdir(p)) n++;
    closedir(p);
    return n > 0;
}

static int leggi(const char *f)
{
    char b[16];
    int fd = open(f, O_RDONLY), r;
    if (fd < 0) return 0;
    r = (int)read(fd, b, sizeof b);
    close(fd);
    return r >= 0;
}

static int scrivi(const char *f)
{
    int fd = open(f, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    (void)!write(fd, "x\n", 2);
    close(fd);
    unlink(f);
    return 1;
}

static void modo(const char *p)
{
    struct stat st;
    if (stat(p, &st) == 0)
        printf("  %-24s modo %04o uid %u gid %u\n", p, (unsigned)(st.st_mode & 07777),
               (unsigned)st.st_uid, (unsigned)st.st_gid);
    else
        printf("  %-24s stat: errno %d\n", p, errno);
}

int main(int argc, char **argv)
{
    unsigned uid = argc > 1 ? (unsigned)atoi(argv[1]) : 1000;
    unsigned gid = argc > 2 ? (unsigned)atoi(argv[2]) : uid;
    struct stat st;
    int r, altri_root = 5;      /* r-x of "others" on /root, as root sees it */

    printf("== i modi, visti da root\n");
    modo("/root"); modo("/root/nu.log"); modo("/home"); modo("/boot");
    modo("/boot/ombra"); modo("/boot/kernel.cfg"); modo("/bin");
    if (stat("/root", &st) == 0) altri_root = (int)(st.st_mode & 7);

    /* a private folder of root and a file 0644 inside: Linux reads the file
     * only if it can cross the folder */
    mkdir("/root/privata", 0700);
    chmod("/root/privata", 0700);
    { int fd = open("/root/privata/f.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) { (void)!write(fd, "segreto\n", 8); close(fd); }
      chmod("/root/privata/f.txt", 0644); }
    /* 0711: crossed, not listed - a known name inside is still reachable
     * (at the root of the disk: inside /root 0700 nothing is) */
    mkdir("/permprova.p", 0711);
    chmod("/permprova.p", 0711);
    { int fd = open("/permprova.p/noto.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) { (void)!write(fd, "noto\n", 5); close(fd); }
      chmod("/permprova.p/noto.txt", 0644); }
    /* a 1777 folder like /tmp should be, with a file of root inside */
    mkdir("/tmp/permprova.d", 01777);
    chmod("/tmp/permprova.d", 01777);
    { int fd = open("/tmp/permprova.d/di-root", O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) close(fd); }
    modo("/tmp"); modo("/tmp/permprova.d");

    if (setgid(gid) != 0 || setuid(uid) != 0) {
        printf("permprova: setuid(%u) non riuscito, errno %d (va lanciato da root)\n", uid, errno);
        return 2;
    }
    printf("== ora uid %u gid %u (getuid %u)\n", uid, gid, (unsigned)getuid());

    /* what Linux says here depends on the mode /root has (0755 before step 2,
     * 0700 after): r and x of "others" */
    riga("elencare /root", elenca("/root"), (altri_root & 5) == 5);
    riga("leggere /root/nu.log (0644)", leggi("/root/nu.log"), (altri_root & 1) != 0);
    riga("scrivere in /root (di root)", scrivi("/root/permprova.tmp"), 0);
    riga("elencare /root/privata (0700)", elenca("/root/privata"), 0);
    riga("stat di /root/privata/f.txt", stat("/root/privata/f.txt", &st) == 0, 0);
    riga("leggere /root/privata/f.txt (0644 in 0700)", leggi("/root/privata/f.txt"), 0);
    riga("cd in /root/privata", (r = chdir("/root/privata")) == 0, 0);
    if (r == 0) chdir("/");
    riga("leggere /boot/ombra (0600)", leggi("/boot/ombra"), 0);
    riga("scrivere /boot/kernel.cfg (0644)", (r = open("/boot/kernel.cfg", O_WRONLY)) >= 0, 0);
    if (r >= 0) close(r);
    riga("creare /boot/permprova.tmp", scrivi("/boot/permprova.tmp"), 0);
    riga("creare /bin/permprova.tmp", scrivi("/bin/permprova.tmp"), 0);
    riga("cancellare /boot/avvio.sh (finto: rename)", rename("/boot/avvio.sh", "/boot/avvio.sh") == 0, 0);
    riga("chmod 777 /root/privata (di root)", chmod("/root/privata", 0777) == 0, 0);
    riga("creare in /tmp", scrivi("/tmp/permprova.tmp"), 1);

    /* what must STILL work: no stricter than Linux */
    riga("elencare /permprova.p (0711)", elenca("/permprova.p"), 0);
    riga("leggere /permprova.p/noto.txt (0644 in 0711)", leggi("/permprova.p/noto.txt"), 1);
    riga("cd in /permprova.p (0711)", (r = chdir("/permprova.p")) == 0, 1);
    if (r == 0) chdir("/");
    riga("stat di /bin/sh", stat("/bin/sh", &st) == 0, 1);
    riga("elencare /bin", elenca("/bin"), 1);
    riga("cd in /home", (r = chdir("/home")) == 0, 1);
    if (r == 0) chdir("/");

    /* the sticky bit */
    riga("creare in /tmp/permprova.d (1777)", (r = open("/tmp/permprova.d/mio", O_WRONLY | O_CREAT, 0644)) >= 0, 1);
    if (r >= 0) close(r);
    riga("cancellare il mio in /tmp/permprova.d", unlink("/tmp/permprova.d/mio") == 0, 1);
    riga("cancellare quello di root in /tmp/permprova.d", unlink("/tmp/permprova.d/di-root") == 0, 0);
    riga("rinominare quello di root in /tmp/permprova.d",
         rename("/tmp/permprova.d/di-root", "/tmp/permprova.d/preso") == 0, 0);

    printf("permprova: %d falle, %d troppo stretti\n", g_falle, g_stretti);
    printf("(da root, dopo: rm -r /root/privata /permprova.p /tmp/permprova.d)\n");
    return g_falle != 0 || g_stretti != 0;
}

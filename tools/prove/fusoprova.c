/* tools/prove/fusoprova.c - the time zone of EX-OS (kernel 0.340)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     fusoprova base HOST_UTC EXT2_DIR FAT_DIR     the default zone, UTC+1 ue
 *     fusoprova zona MINUTI UE                     after date -fuso:
 *
 * One "FUSO name 0|1 detail" line per check, 0 = right; "FUSO fine N".
 * Run by tools/prova_fuso.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

static int sbagliate = 0;

static void esito(const char *nome, int male, const char *dettaglio)
{
    printf("FUSO %s %d %s\n", nome, male ? 1 : 0, dettaglio);
    if (male) sbagliate++;
}

static time_t utc(int a, int mo, int g, int h, int mi, int s)
{
    struct tm t;

    memset(&t, 0, sizeof(t));
    t.tm_year = a - 1900; t.tm_mon = mo - 1; t.tm_mday = g;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
    return timegm(&t);
}

/* localtime of an instant must give that hour, that offset, that name */
static void locale(const char *nome, time_t t, int h, int mi, int s, long off, const char *zona)
{
    struct tm r;
    char      d[160];

    localtime_r(&t, &r);
    snprintf(d, sizeof(d), "%lld -> %02d:%02d:%02d gmtoff %ld isdst %d %s (atteso %02d:%02d:%02d %ld %s)",
             (long long)t, r.tm_hour, r.tm_min, r.tm_sec, r.tm_gmtoff, r.tm_isdst,
             r.tm_zone ? r.tm_zone : "(null)", h, mi, s, off, zona);
    esito(nome, r.tm_hour != h || r.tm_min != mi || r.tm_sec != s || r.tm_gmtoff != off ||
                r.tm_isdst != (off == 7200) || r.tm_zone == NULL || strcmp(r.tm_zone, zona) != 0, d);
}

/* mktime of a local time must give that instant */
static void indietro(const char *nome, int a, int mo, int g, int h, int mi, time_t atteso)
{
    struct tm t;
    time_t    x;
    char      d[120];

    memset(&t, 0, sizeof(t));
    t.tm_year = a - 1900; t.tm_mon = mo - 1; t.tm_mday = g; t.tm_hour = h; t.tm_min = mi;
    x = mktime(&t);
    snprintf(d, sizeof(d), "%04d-%02d-%02d %02d:%02d locale -> %lld (atteso %lld)",
             a, mo, g, h, mi, (long long)x, (long long)atteso);
    esito(nome, x != atteso, d);
}

static void file_nuovo(const char *nome, const char *dir, time_t adesso)
{
    char        p[256], d[160];
    struct stat s;
    int         fd;

    snprintf(p, sizeof(p), "%s/nuovo.txt", dir);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, "ciao\n", 5) != 5 || close(fd) != 0 || stat(p, &s) != 0) {
        esito(nome, 1, p);
        return;
    }
    snprintf(d, sizeof(d), "%s mtime %lld, time() %lld", p, (long long)s.st_mtime, (long long)adesso);
    esito(nome, s.st_mtime < adesso - 5 || s.st_mtime > adesso + 5, d);
}

int main(int argc, char **argv)
{
    char d[200], z[40];

    if (argc == 5 && strcmp(argv[1], "base") == 0) {
        time_t   adesso = time(NULL), host = (time_t)atoll(argv[2]);
        struct   tm r;
        struct   timeval tv;
        struct   timezone tz;
        long     off;

        /* the clock: QEMU's CMOS in local time, time() in UTC */
        snprintf(d, sizeof(d), "time() %lld, Linux all'avvio %lld", (long long)adesso, (long long)host);
        esito("adesso", adesso < host - 10 || adesso > host + 600, d);
        localtime_r(&adesso, &r);
        off = r.tm_gmtoff;
        snprintf(d, sizeof(d), "gmtoff %ld %s", off, r.tm_zone ? r.tm_zone : "(null)");
        esito("adesso-fuso", off != 3600 && off != 7200, d);
        gettimeofday(&tv, &tz);
        snprintf(d, sizeof(d), "tz_minuteswest %d", tz.tz_minuteswest);
        esito("gettimeofday", tz.tz_minuteswest != -(int)(off / 60), d);
        tzset();
        snprintf(d, sizeof(d), "timezone %ld daylight %d %s/%s", timezone, daylight, tzname[0], tzname[1]);
        esito("tzset", timezone != -3600 || daylight != 1 || strcmp(tzname[0], "CET") || strcmp(tzname[1], "CEST"), d);

        /* the European rule at its two edges, 2026 */
        locale("inverno",          utc(2026, 1, 15, 12, 0, 0),   13, 0, 0, 3600, "CET");
        locale("estate",           utc(2026, 7, 1, 12, 0, 0),    14, 0, 0, 7200, "CEST");
        locale("marzo-prima",      utc(2026, 3, 29, 0, 59, 59),  1, 59, 59, 3600, "CET");
        locale("marzo-dopo",       utc(2026, 3, 29, 1, 0, 0),    3, 0, 0, 7200, "CEST");
        locale("ottobre-prima",    utc(2026, 10, 25, 0, 59, 59), 2, 59, 59, 7200, "CEST");
        locale("ottobre-dopo",     utc(2026, 10, 25, 1, 0, 0),   2, 0, 0, 3600, "CET");
        indietro("mktime-inverno", 2026, 1, 15, 13, 0,  utc(2026, 1, 15, 12, 0, 0));
        indietro("mktime-estate",  2026, 7, 1, 14, 0,   utc(2026, 7, 1, 12, 0, 0));
        indietro("mktime-doppia",  2026, 10, 25, 2, 30, utc(2026, 10, 25, 0, 30, 0));
        indietro("mktime-manca",   2026, 3, 29, 2, 30,  utc(2026, 3, 29, 1, 30, 0));
        {
            time_t e = utc(2026, 7, 1, 12, 0, 0);
            strftime(z, sizeof(z), "%H:%M %z %Z", localtime(&e));
            esito("strftime", strcmp(z, "14:00 +0200 CEST") != 0, z);
            strftime(z, sizeof(z), "%H:%M %z %Z", gmtime(&e));
            esito("strftime-utc", strcmp(z, "12:00 +0000 UTC") != 0, z);
        }
        /* the files: the kernel dates them in UTC, whatever the volume keeps */
        file_nuovo("ext2-nuovo", argv[3], adesso);
        file_nuovo("fat-nuovo", argv[4], adesso);
    } else if (argc == 4 && strcmp(argv[1], "zona") == 0) {
        long   min = atol(argv[2]);
        time_t e   = utc(2026, 7, 1, 12, 0, 0);
        struct tm r;

        localtime_r(&e, &r);
        snprintf(d, sizeof(d), "gmtoff %ld isdst %d %s (atteso %ld, regola %s)", r.tm_gmtoff,
                 r.tm_isdst, r.tm_zone ? r.tm_zone : "(null)", min * 60, argv[3]);
        esito("zona-nuova", r.tm_gmtoff != min * 60 + (atoi(argv[3]) ? 3600 : 0), d);
    } else {
        printf("uso: fusoprova base HOST_UTC EXT2_DIR FAT_DIR | zona MINUTI UE\n");
        return 2;
    }
    printf("FUSO fine %d\n", sbagliate);
    return sbagliate != 0;
}

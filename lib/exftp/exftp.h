/* =============================================================================
 * lib/exftp/exftp.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * exftp — the FTP and FTPS client every EX-OS program shares
 *
 * Users: /bin/ftp (the interactive ftp shell), /bin/ftpswap (keeps a
 * directory in step with a server), /bin/netspeed (-makeserver uploads its
 * PHP script) and, next, the file manager's FTP area. Compiled into each
 * of them (at 64 bit it is in libexos.a): a few kilobytes of code are not
 * worth a shared library of their own.
 *
 * ! IT STARTED IN SEPTEMBER 2026 AS ftpswap's CLIENT and became the only one
 * on 17 October 2026, when the user asked for FTPS in /bin/ftp, netspeed and
 * the file manager alike: three copies of the same three-digit dialogue
 * would be three places to get the same reply wrong.
 *
 * -----------------------------------------------------------------------------
 * WHAT IT DOES
 *
 *   - Plain FTP and explicit FTPS (AUTH TLS on the normal port, then PBSZ 0
 *     and PROT P: commands AND data encrypted). Implicit FTPS (port 990,
 *     TLS from the first byte) too, with EXFTP_TLS_IMPLICITO.
 *   - Passive data connections (EPSV first, PASV if the server does not
 *     know it) and active ones (PORT: EX-OS's TCP can listen).
 *   - ! THE PASSIVE ADDRESS IS ALWAYS THE CONTROL CONNECTION'S. The address
 *     in a 227 reply is often a private one behind the server's NAT, and
 *     FTPS checks the certificate against the host name anyway: curl and
 *     FileZilla do the same.
 *   - MLSD for listings meant for programs, LIST as the fallback (Unix and
 *     DOS layouts parsed), and the raw LIST/NLST lines for a human.
 *   - SIZE, MDTM, MFMT, CWD, CDUP, PWD, MKD, RMD, DELE, RNFR/RNTO, REST.
 *   - ASCII mode converts line ends (CR LF on the wire, LF on EX-OS).
 *   - A log hook (every line sent and received - the password masked) and
 *     a progress hook that can also stop a transfer.
 *
 * ! TLS COMES FROM exhttp, AS A FUNCTION POINTER (exhttp_tls_sopra). A
 * program that never speaks FTPS (ftpswap) does not have to link the whole
 * of exhttp and its certificate store: it passes 0 and gets plain FTP.
 *
 * ! WHAT THE TLS UNDERNEATH CANNOT DO, said here because it is where a
 * failure will be looked up: exhttp's TLS is 1.3 only, and it does not
 * resume sessions. A server that requires the data connection to reuse the
 * control connection's TLS session (vsftpd's require_ssl_reuse=YES, the
 * default there) refuses the data channel with a 522/425: the reply is put
 * in `risposta` as the server wrote it.
 *
 * ! FTP DATES ARE UTC and are returned as time_t untouched. EX-OS does not
 * know its time zone (localtime and gmtime are the same function, libc.h),
 * so compare them with a tolerance.
 *
 * ! ONE TCP RESERVATION AT A TIME. The stack delivers bytes to whoever asked
 * (IP_MSG_TCP_RICEVI), and the request is used up by each delivery. Between
 * the control and the data connection there is ALWAYS ONE request open: the
 * code reads one or the other, never both. Keep that rule when touching
 * exftp.c.
 * ============================================================================= */
#ifndef EXFTP_H
#define EXFTP_H

#include "libc.h"
#include "exhttp.h"                  /* ExHttpTrasporto: the byte stream     */

#define EXFTP_RIGA_MAX     512       /* one reply line                       */
#define EXFTP_ACC_MAX     2048       /* control channel accumulator          */
#define EXFTP_NOME_MAX     256       /* one remote file name                 */
#define EXFTP_HOST_MAX     128
#define EXFTP_ERRORE_MAX   192

/* TLS over a transport already open: exhttp_tls_sopra has this shape. */
typedef int (*ExFtpTls)(ExHttpTrasporto *t, const ExHttpTrasporto *tcp,
                        const char *host, char *errore, unsigned int max);

/* The log: `verso` is '>' for a line sent, '<' for a line received, '!' for
 * what the library itself has to say (TLS up, data connection address...).
 * The PASS argument never reaches it. */
typedef void (*ExFtpRegistro)(void *dato, int verso, const char *riga);

/* The progress of a transfer: bytes done, and the total if known (else -1).
 * Return 0 to stop the transfer (it ends with -ECANCELED). */
typedef int (*ExFtpAvanza)(void *dato, long fatti, long totale);

/* How to connect. */
#define EXFTP_CHIARO          0      /* plain FTP                            */
#define EXFTP_TLS             1      /* explicit FTPS: AUTH TLS              */
#define EXFTP_TLS_IMPLICITO   2      /* implicit FTPS: TLS at once (990)     */

typedef struct {
    ExHttpTrasporto ctrl;            /* the control connection               */
    int           aperto;            /* 1 while ctrl is up                   */
    int           dentro;            /* 1 after a successful login           */
    char          host[EXFTP_HOST_MAX];
    unsigned char ip[4];             /* the server                           */
    unsigned int  porta;
    int           passiva;           /* 1 = EPSV/PASV (default), 0 = PORT    */
    int           binario;           /* 1 = TYPE I, 0 = TYPE A               */
    int           verboso;           /* print the dialogue (old switch)      */

    int           cifrato;           /* control channel under TLS            */
    int           prot_p;            /* data channels under TLS              */
    ExFtpTls      tls;               /* 0 = no FTPS available                */
    int           no_epsv;           /* the server refused EPSV: use PASV    */

    long          riparti;           /* REST offset for the next RETR/STOR   */

    ExFtpRegistro registro;
    void         *registro_dato;
    ExFtpAvanza   avanza;
    void         *avanza_dato;

    int           codice;            /* the last reply's code                */
    char          risposta[EXFTP_RIGA_MAX];   /* its last line               */
    char          errore[EXFTP_ERRORE_MAX];   /* what failed below FTP (TLS) */

    char          acc[EXFTP_ACC_MAX];         /* lines not consumed yet      */
    unsigned int  acc_len;
} ExFtp;

/* A listing entry, from MLSD (or LIST, as well as it can be read).
 * `quando` is a UTC time_t, or -1 if the server did not say. */
typedef struct {
    char nome[EXFTP_NOME_MAX];
    int  directory;
    long dimensione;
    long quando;
} ExFtpVoce;

/* The listing hook: return 1 to go on, 0 to stop.
 * ! THE LISTING IS HANDED OVER IN PIECES and never accumulated: a directory
 * with a thousand files must not have to fit in the reader's memory. */
typedef int (*ExFtpElenca)(void *dato, const ExFtpVoce *v);

/* One raw line (LIST, NLST): return 1 to go on, 0 to stop. */
typedef int (*ExFtpRiga)(void *dato, const char *riga);

/* -----------------------------------------------------------------------------
 * Opening and closing
 *
 * exftp_prepara clears the structure and sets the defaults; the hooks and
 * `tls` are set after it and before exftp_connetti. All return 0 or -errno;
 * the reason in words is in `risposta` (the server's) or `errore` (ours).
 * --------------------------------------------------------------------------- */
void exftp_prepara(ExFtp *f);

/* Connects and reads the greeting; with EXFTP_TLS sends AUTH TLS and starts
 * TLS on the control connection (f->tls must be set). porta 0 = 21 (990
 * for EXFTP_TLS_IMPLICITO). */
int  exftp_connetti(ExFtp *f, const char *host, unsigned int porta, int modo);

/* USER/PASS; on an encrypted connection PBSZ 0 and PROT P; then TYPE I. */
int  exftp_accedi(ExFtp *f, const char *utente, const char *password);

/* exftp_prepara + exftp_connetti (plain) + exftp_accedi: the old call. */
int  exftp_apri(ExFtp *f, const char *host, unsigned int porta,
                const char *utente, const char *password);

/* QUIT and close. Safe to call twice. */
void exftp_chiudi(ExFtp *f);

/* Any command, as `quote` sends it: the reply code, or -1 if the connection
 * died. PASS goes through masked in the log. */
int  exftp_comando(ExFtp *f, const char *verbo, const char *arg);

/* Data mode: 1 binary (TYPE I), 0 text (TYPE A). */
int  exftp_binario(ExFtp *f, int si);

/* Data channel: 1 passive, 0 active. Takes effect at the next transfer. */
void exftp_passiva(ExFtp *f, int si);

/* PROT P (1) or PROT C (0) on an encrypted connection. */
int  exftp_protezione(ExFtp *f, int privata);

/* -----------------------------------------------------------------------------
 * Directories and files
 * --------------------------------------------------------------------------- */
int  exftp_pwd(ExFtp *f, char *dst, unsigned int max);
int  exftp_cd(ExFtp *f, const char *dir);
int  exftp_cdup(ExFtp *f);
int  exftp_mkd(ExFtp *f, const char *dir);      /* -EEXIST if it is there    */
int  exftp_rmd(ExFtp *f, const char *dir);
int  exftp_dele(ExFtp *f, const char *file);
int  exftp_rinomina(ExFtp *f, const char *da, const char *a);

long exftp_size(ExFtp *f, const char *file);    /* bytes, or -errno          */
long exftp_mdtm(ExFtp *f, const char *file);    /* UTC time_t, or -errno     */
int  exftp_mfmt(ExFtp *f, const char *file, long quando);

/* The listing of `dir` (0 or "" = the current one): MLSD, else LIST read as
 * well as possible. Returns the number of entries, or -errno. */
int  exftp_elenco(ExFtp *f, const char *dir, ExFtpElenca g, void *dato);

/* The raw lines of LIST or NLST (verbo), for a human to read. */
int  exftp_righe(ExFtp *f, const char *verbo, const char *arg,
                 ExFtpRiga g, void *dato);

/* Transfers. `locale` is a path on this machine. With f->riparti > 0 the
 * transfer starts there (REST): scarica appends to the local file, carica
 * skips that much of it. riparti is used up by the call. */
int  exftp_scarica(ExFtp *f, const char *remoto, const char *locale);
int  exftp_carica(ExFtp *f, const char *locale, const char *remoto);

/* APPE: the local file added at the end of the remote one. */
int  exftp_accoda(ExFtp *f, const char *locale, const char *remoto);

/* Upload from memory (netspeed's generated script). */
int  exftp_carica_mem(ExFtp *f, const void *dati, unsigned long n,
                      const char *remoto);

/* The text of an -errno from this library, for messages. */
const char *exftp_perche(int errore);

#endif /* EXFTP_H */

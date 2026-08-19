/* =============================================================================
 * bin/install/install.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Installa EX-OS su un disco rigido.
 *
 *   install <punto>        installa sul volume gia' montato in <punto>
 *
 * Esempio d'uso completo, dal floppy di avvio:
 *
 *   disk                   guarda i dispositivi: hd0, hd0p1, ...
 *   mount hd0p1 /disk      il volume dev'essere in lettura/scrittura
 *   install /disk
 *
 * -----------------------------------------------------------------------
 * L'ORDINE DI COPIA NON E' CASUALE
 *
 * STAGE2.BIN e KERNEL.BIN vengono copiati PER PRIMI. Il settore di avvio
 * non sa leggere la FAT: riceve il primo settore e la lunghezza di quei
 * due file, quindi devono essere CONTIGUI sul volume. Su un volume con
 * spazio libero contiguo — quello appena formattato, il caso normale — un
 * file scritto per primo lo e' senz'altro; scritto dopo altre venti copie,
 * puo' non esserlo piu'.
 *
 * Se nonostante tutto risultano frammentati, il kernel RIFIUTA di
 * installare l'avvio invece di scrivere una mappa che comprende cluster
 * di altri file. Vedi kernel/boot/bootinst.c.
 *
 * -----------------------------------------------------------------------
 * I NOMI SI CREANO IN MINUSCOLO, E SU ext2 NON E' UN DETTAGLIO
 *
 * Il kernel cerca "/bin/sh", "/boot/kernel.cfg", "/dev/kbd.drv": tutto
 * minuscolo. Su FAT non importa — il driver mette in maiuscolo sia cio'
 * che scrive sia cio' che cerca, quindi "BIN" e "bin" sono la stessa
 * directory. Su ext2 sono due directory diverse, e un sistema installato
 * in "BIN" non troverebbe la propria shell.
 *
 * Creare in minuscolo funziona su entrambi: su FAT il driver converte da
 * solo, su ext2 il nome resta quello che il kernel cerchera'.
 *
 * -----------------------------------------------------------------------
 * REINSTALLARE SOSTITUISCE (dal 0.148)
 *
 * Ogni file viene riscritto anche se sulla destinazione ce n'e' gia' uno,
 * kernel compreso, e riletto per controllarne la dimensione. Prima i file
 * esistenti venivano saltati: aggiornare un sistema gia' installato
 * copiava solo i file nuovi e lasciava indietro il kernel, mentre la mappa
 * dei settori veniva riscritta per quello vecchio. Il disco ripartiva con
 * la versione di prima e l'installatore diceva "completata".
 *
 * Le directory no: esistono o non esistono, e ricrearle non vuol dire
 * niente. Quel che c'e' dentro e non fa parte del sistema resta dov'e' —
 * `install` aggiorna, non azzera il volume.
 *
 * Nel resoconto: '+' creato, '~' sostituito, '!' errore.
 *
 * -----------------------------------------------------------------------
 * COSA NON FA
 *
 * Non partiziona e non formatta. Sono due operazioni che distruggono dati
 * e vanno fatte con strumenti dedicati, con l'utente che sa cosa sta
 * cancellando — non come effetto collaterale di "installa".
 * ============================================================================= */
#include "libc.h"
#include "exuser.h"

#define BLOCCO      4096
#define PERC_MAX    128

static char buf[BLOCCO];
static int  errori = 0;

/* ! SOLO PER bootverify() e bootinstall(), che sono FUNZIONI NOSTRE e
 * rendono ancora -errno. Le funzioni POSIX qui sotto — open(), mkdir() —
 * dal agosto 2026 rendono -1 e parlano per errno: quelle usano strerror(),
 * e mescolare le due convenzioni in una tabella sola darebbe messaggi
 * giusti per meta' delle chiamate. */
static const char *spiega(int e)
{
    switch (-e) {
        case 2:  return "non trovato";
        case 5:  return "errore di I/O";
        case 17: return "esiste gia'";
        case 19: return "dispositivo assente";
        case 21: return "e' una directory";
        case 22: return "argomento non valido";
        case 27: return "file troppo grande";
        case 28: return "spazio esaurito";
        case 29: return "file frammentato: ricopialo su un volume con spazio contiguo";
        case 30: return "montato in sola lettura";
        default: return "errore";
    }
}

/* Da FAT12 i nomi arrivano SEMPRE in maiuscolo: e' come il formato li
 * conserva, e l'informazione sul caso originale non esiste piu'. Copiarli
 * cosi' com'e' su ext2 — dove il caso conta — produrrebbe "/bin/SH" mentre
 * il kernel cerca "/bin/sh". Il minuscolo e' l'unica ricostruzione
 * sensata, ed e' quella che il sistema si aspetta. */
static void minuscolo(char *s)
{
    int i;
    for (i = 0; s[i]; i++)
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
}

/* Concatena "punto" + "/" + "resto" senza sforare. */
/* Vero se `s` finisce con `coda`. */
static int finisce_con(const char *s, const char *coda)
{
    unsigned int ls = (unsigned int)strlen(s), lc = (unsigned int)strlen(coda);
    return ls >= lc && strcmp(s + ls - lc, coda) == 0;
}

static void unisci(char *out, const char *a, const char *b)
{
    int i = 0, j;

    for (j = 0; a[j] && i < PERC_MAX - 2; j++) out[i++] = a[j];
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    for (j = 0; b[j] && i < PERC_MAX - 1; j++) out[i++] = b[j];
    out[i] = '\0';
}

/* Copia un file, SOSTITUENDO la destinazione se esiste. Ritorna 0, o <0.
 *
 * ! FINO AL 0.148 QUESTA FUNZIONE NON SOVRASCRIVEVA: se la destinazione
 * c'era gia', la considerava fatta e andava avanti. Sembrava prudenza —
 * "reinstallare sopra non deve fallire a meta'" — ed era invece il difetto
 * peggiore che un installatore possa avere: `install` su un sistema gia'
 * installato aggiornava i file NUOVI e lasciava indietro tutti gli altri,
 * kernel compreso. Il disco continuava ad avviare la versione vecchia
 * mentre l'installatore stampava "Installazione completata", e la mappa
 * dei settori veniva riscritta per quel kernel vecchio — cioe' il
 * risultato piu' convincente possibile di un aggiornamento che non e'
 * avvenuto.
 *
 * Ora si riscrive sempre. Un installatore che salta i file esistenti non
 * e' un installatore: e' una copia condizionale.
 *
 * La verifica finale sulla dimensione non e' pignoleria: e' cio' che
 * distingue "riscritto" da "riscritto a meta'" quando il volume si riempie
 * o il supporto ha un settore che non risponde — e senza, un kernel
 * troncato si scoprirebbe al riavvio successivo, che e' il momento in cui
 * non si puo' piu' fare niente. */
static int copia(const char *da, const char *a)
{
    int fs, fd, n, tot = 0;
    int esisteva;

    fd = open(a, O_RDONLY);
    esisteva = (fd >= 0);
    if (esisteva) close(fd);

    fs = open(da, O_RDONLY);
    if (fs < 0) {
        printf("  ! %s: %s\n", da, strerror(errno));
        errori++;
        return fs;
    }

    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        close(fs);
        printf("  ! %s: %s\n", a, strerror(errno));
        errori++;
        return fd;
    }

    while ((n = (int)read(fs, buf, BLOCCO)) > 0) {
        int scritti = 0;
        while (scritti < n) {
            int w = (int)write(fd, buf + scritti, (unsigned int)(n - scritti));
            if (w <= 0) {
                printf("  ! %s: scrittura interrotta a %d byte\n", a, tot + scritti);
                close(fs); close(fd);
                errori++;
                return -1;
            }
            scritti += w;
        }
        tot += scritti;
    }

    close(fs);
    close(fd);

    if (n < 0) { printf("  ! %s: lettura interrotta\n", da); errori++; return n; }

    /* Rilettura di controllo: la destinazione deve esistere e avere
     * esattamente i byte che ci abbiamo scritto. */
    {
        int  v = open(a, O_RDONLY);
        long dim;

        if (v < 0) {
            printf("  ! %s: riscritto ma non rileggibile: %s\n", a,
                   strerror(errno));
            errori++;
            return v;
        }
        dim = fsize(v);
        close(v);

        if (dim != (long)tot) {
            printf("  ! %s: scritti %d byte, sul volume ce ne sono %ld\n",
                   a, tot, dim);
            errori++;
            return -1;
        }
    }

    printf("  %c %s  (%d byte)\n", esisteva ? '~' : '+', a, tot);
    return 0;
}

/* =============================================================================
 * pulisci_temporanei — toglie i .new rimasti da un tentativo annullato
 *
 * ! SI CHIAMA SU OGNI USCITA ANTICIPATA. Un `.new` dimenticato occupa
 * spazio e, cosa peggiore, al tentativo successivo la copia lo troverebbe
 * gia' esistente: non un errore, ma il segno che qualcosa era andato male
 * e nessuno ha ripulito. Meglio non lasciarne.
 * ============================================================================= */
static void pulisci_temporanei(const char *punto)
{
    char b[PERC_MAX], q[PERC_MAX];

    unisci(b, punto, "boot");
    unisci(q, b, "stage2.new"); unlink(q);
    unisci(q, b, "kernel.new"); unlink(q);
}

/* =============================================================================
 * sostituisci — «<nome>.new» prende il posto di «<nome>.bin»
 *
 * ! E' L'UNICO PUNTO IN CUI SI TOCCA IL SISTEMA GIA' INSTALLATO, e ci si
 * arriva solo dopo che la verifica ha detto di si'.
 *
 * L'ordine e' cancella-poi-rinomina e non il contrario, perche' rename()
 * su EX-OS NON sostituisce la destinazione: darebbe EEXIST. Fra le due
 * operazioni c'e' un istante in cui il nome definitivo non esiste — ma il
 * contenuto e' salvo sotto il nome temporaneo, quindi il peggio che possa
 * capitare e' un disco da cui rilanciare l'installazione, non dati persi.
 *
 * ! E LA RINOMINA NON SPOSTA I DATI: e' la ragione per cui la mappa
 * verificata un attimo fa vale ancora. Una rename che copiasse
 * riallocherebbe i blocchi e la verifica non varrebbe piu' niente — era
 * cosi' fino alla 0.160, vedi lib/libc.c.
 * ============================================================================= */
static int sostituisci(const char *dir_boot, const char *base)
{
    char vecchio[PERC_MAX], nuovo[PERC_MAX], nb[64], nn[64];
    int  i, j;

    for (i = 0; base[i] && i < 50; i++) { nb[i] = base[i]; nn[i] = base[i]; }
    for (j = 0; ".bin"[j]; j++) nb[i + j] = ".bin"[j];
    nb[i + j] = '\0';
    for (j = 0; ".new"[j]; j++) nn[i + j] = ".new"[j];
    nn[i + j] = '\0';

    unisci(vecchio, dir_boot, nb);
    unisci(nuovo,   dir_boot, nn);

    unlink(vecchio);          /* se non c'era, non importa */

    if (rename(nuovo, vecchio) != 0) {
        printf("  ! %s: rinomina fallita (errno %d)\n", vecchio, errno);
        printf("    il contenuto e' ancora in %s\n", nuovo);
        errori++;
        return -1;
    }

    printf("  ~ %s  (verificato, poi sostituito)\n", vecchio);
    return 0;
}

/* Crea una directory ignorando "esiste gia'". */
static void crea_dir(const char *p)
{
    int r = mkdir(p, 0755);
    if (r == 0)                 printf("  + %s/\n", p);
    else if (errno == EEXIST)   printf("  = %s/ (gia' presente)\n", p);
    else {
        printf("  ! %s/: %s\n", p, strerror(errno));
        errori++;
    }
}

/* Copia tutto il contenuto di una directory del volume di avvio. */
/* =============================================================================
 * MODO AGGIORNAMENTO — `install -a`
 *
 * Confronta quello che c'e' sul volume con quello che c'e' sul supporto e
 * ricopia SOLO cio' che e' cambiato. Un'installazione completa riscrive
 * tutto ed e' quello che deve fare; un aggiornamento su un floppy o su un
 * disco lento non ha motivo di riscrivere trenta file identici.
 *
 * ! LA REGOLA E' "SORGENTE PIU' NUOVA", NON "DATE DIVERSE", e la
 * differenza e' quella fra uno strumento utile e uno inutile.
 *
 * Copiare un file NON ne conserva la data: quello scritto adesso ha l'ora
 * di adesso, non quella che aveva sul CD. Con la regola ingenua — «le
 * date differiscono, quindi aggiorna» — ogni file risulterebbe da
 * aggiornare a OGNI esecuzione, per sempre, anche subito dopo averlo
 * appena copiato. Confrontando invece se la sorgente e' PIU' NUOVA della
 * destinazione, un file appena copiato resta a posto finche' sul supporto
 * non ne arriva davvero uno piu' recente.
 *
 * ! LA DIMENSIONE SI CONFRONTA COMUNQUE, e viene prima: un file
 * ricopiato a meta' per un disco pieno ha la stessa data e una
 * dimensione diversa. E' anzi il caso che conta di piu', perche' e'
 * l'unico in cui il volume e' rotto senza che nessuno l'abbia detto.
 *
 * ! SE UNA DELLE DUE DATE MANCA si guarda solo la dimensione. Zero
 * significa «questo volume non tiene le date» (ISO 9660 masterizzati
 * male, volumi montati da altri sistemi): trattarlo come "1970" farebbe
 * sembrare la destinazione vecchissima e proporrebbe di riscrivere tutto.
 * ============================================================================= */
#define STATO_UGUALE    0
#define STATO_MANCANTE  1
#define STATO_DIVERSO   2

static int confronta(const char *da, const char *a)
{
    struct stat s, d;

    if (stat(a, &d) != 0) return STATO_MANCANTE;
    if (stat(da, &s) != 0) return STATO_UGUALE;   /* non c'e' nulla da copiare */

    if (s.st_size != d.st_size) return STATO_DIVERSO;

    /* Date assenti da una delle due parti: la dimensione ha gia' detto
     * quello che si poteva dire. */
    if (s.st_mtime == 0 || d.st_mtime == 0) return STATO_UGUALE;

    return (s.st_mtime > d.st_mtime) ? STATO_DIVERSO : STATO_UGUALE;
}

/* Percorre una directory confrontando. Se `applica` e' 0 conta soltanto e
 * stampa; se e' 1 copia quello che serve. */
static int scorri_confronto(const char *sorgente, const char *punto,
                            const char *dest, int applica, int *mancanti)
{
    DirEntry     voci[LISTDIR_MAX_BATCH];
    char         pdest[PERC_MAX];
    unsigned int start = 0;
    int          n, da_fare = 0;

    unisci(pdest, punto, dest);
    if (applica) crea_dir(pdest);

    while ((n = listdir_from(sorgente, voci, LISTDIR_MAX_BATCH, start)) > 0) {
        int i;

        for (i = 0; i < n; i++) {
            char da[PERC_MAX], a[PERC_MAX];
            int  st;

            if (voci[i].is_dir) continue;
            if (voci[i].name[0] == '.') continue;

            unisci(da, sorgente, voci[i].name);
            unisci(a,  pdest,    voci[i].name);
            minuscolo(a + strlen(pdest));

            st = confronta(da, a);
            if (st == STATO_UGUALE) continue;

            da_fare++;
            if (st == STATO_MANCANTE && mancanti) (*mancanti)++;

            if (applica) copia(da, a);
            else printf("  %s %s\n", (st == STATO_MANCANTE) ? "+" : "~", a);
        }

        start += (unsigned int)n;
        if (n < LISTDIR_MAX_BATCH) break;
    }

    return da_fare;
}

/* =============================================================================
 * ! QUI C'ERA installa_exwin(), tolta il 17 agosto 2026.
 *
 * Era una funzione che sapeva a memoria di /exwin: creava /exwin, copiava
 * bin/ e lib/, e creava /exwin/dev anche se vuota. Funzionava, e aveva un
 * difetto di struttura: ogni pacchetto nuovo avrebbe voluto una funzione come
 * quella, dentro l'installatore, scritta da chi l'installatore ce l'ha in
 * mano. CHI PREPARA UN PACCHETTO NON HA I SORGENTI DELL'INSTALLATORE.
 *
 * Adesso /exwin non e' un caso speciale: e' una directory nella radice del
 * supporto che non sta nell'elenco del sistema minimale, quindi
 * cerca_componenti() la trova, l'installatore la mostra e la chiede, e
 * copia_albero() la copia con tutti i suoi livelli — /exwin/dev compresa,
 * vuota o no. Il giorno che ci sara' un altro pacchetto, l'installatore non
 * andra' toccato.
 * ============================================================================= */


/* -----------------------------------------------------------------------------
 * copia_albero — come copia_dir, ma SEGUE le sottodirectory
 *
 * ! copia_dir SI FERMA A UN LIVELLO, ed e' giusto per /bin e /lib, che di
 * livelli ne hanno uno. Un componente no: /exwin non contiene file, contiene
 * bin/ lib/ dev/. Copiarlo con copia_dir darebbe una directory vuota e nessun
 * errore — il caso peggiore, perche' l'installazione sembrerebbe riuscita.
 *
 * ! LA PROFONDITA' E' LIMITATA, e non per prudenza generica: senza limite un
 * collegamento circolare (o un difetto della VFS) farebbe scendere
 * l'installatore per sempre, riempiendo il disco di copie annidate.
 * --------------------------------------------------------------------------- */
#define ALBERO_LIVELLI_MAX  6

static void copia_albero_ric(const char *sorgente, const char *pdest,
                             int aggiorna, int livello)
{
    DirEntry voci[LISTDIR_MAX_BATCH];
    unsigned int start = 0;
    int n;

    crea_dir(pdest);

    while ((n = listdir_from(sorgente, voci, LISTDIR_MAX_BATCH, start)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char da[PERC_MAX], a[PERC_MAX];

            if (voci[i].name[0] == '.') continue;

            unisci(da, sorgente, voci[i].name);
            unisci(a,  pdest,    voci[i].name);
            minuscolo(a + strlen(pdest));

            if (voci[i].is_dir) {
                if (livello + 1 >= ALBERO_LIVELLI_MAX) {
                    printf("  ! %s: troppi livelli, non scendo oltre\n", da);
                    errori++;
                    continue;
                }
                copia_albero_ric(da, a, aggiorna, livello + 1);
                continue;
            }

            /* In aggiornamento si tocca solo cio' che e' cambiato: riscrivere
             * un file identico consuma il disco e allunga l'elenco senza dire
             * niente di utile. */
            if (aggiorna && confronta(da, a) == STATO_UGUALE) continue;

            /* ! DENTRO UN COMPONENTE I PROGRAMMI STANNO IN bin/, e si
             * riconoscono dal nome della directory che li contiene: qui non si
             * sa se siamo in /exwin/bin o in /exwin/lib, ma il percorso lo
             * dice. Un pacchetto che mette i suoi programmi altrove non li
             * vedra' marcati — ed e' meglio di marcare eseguibile tutto. */
            if (copia(da, a) >= 0 && finisce_con(pdest, "/bin"))
                chmod(a, 0755);
        }
        start += (unsigned int)n;
        if (n < LISTDIR_MAX_BATCH) break;
    }
}

static void copia_albero(const char *sorgente, const char *punto,
                         const char *dest, int aggiorna)
{
    char pdest[PERC_MAX];

    unisci(pdest, punto, dest);
    copia_albero_ric(sorgente, pdest, aggiorna, 0);
}

/* =============================================================================
 * I COMPONENTI OPZIONALI — e perche' NON sono un elenco dentro l'installatore
 *
 * ! IL SISTEMA MINIMALE E' UN ELENCO CHIUSO; TUTTO IL RESTO E' OPZIONALE. Le
 * directory qui sotto sono cio' senza cui EX-OS non parte: kernel, programmi
 * di base, librerie, driver. Qualunque ALTRA directory trovata nella radice
 * del supporto di avvio e' un componente che si puo' installare o no, e
 * l'installatore la mostra e la chiede.
 *
 * E' il contrario di un elenco scritto qui dentro: aggiungere un pacchetto
 * nuovo — oggi /exwin, domani quello che sara' — vuol dire metterne la
 * directory sul supporto, e basta. CHI PREPARA UN PACCHETTO NON HA I SORGENTI
 * DELL'INSTALLATORE, ed e' la stessa ragione per cui l'elenco delle
 * applicazioni grafiche e' un file di testo e non una tabella compilata.
 *
 * ! I PUNTI DI MONTAGGIO NON SONO COMPONENTI. /cdrom non e' roba da copiare:
 * e' un altro disco visto da qui, e proporlo vorrebbe dire offrire di
 * installare il CD dentro il disco.
 * ============================================================================= */
#define COMPONENTI_MAX  16

static const char *const DIR_SISTEMA[] = {
    "bin", "boot", "lib", "dev", "drivers"
};

/* ! QUESTO ELENCO E' UN AIUTO, NON LA DIFESA. I nomi qui dentro sono quelli
 * che di solito NON sono componenti; ma il punto di montaggio lo sceglie chi
 * installa, e indovinarne il nome non funziona — vedi bersaglio_da_saltare(). */
static const char *const DIR_NON_COMPONENTI[] = {
    "cdrom", "tmp", "mnt", "disk", "home", "lost+found"
};

typedef struct {
    char nome[DIRENT_NAME_MAX];
    int  installa;
} Componente;

static Componente g_comp[COMPONENTI_MAX];
static int        g_n_comp = 0;

static int in_elenco(const char *nome, const char *const *elenco, int quanti)
{
    int i;
    for (i = 0; i < quanti; i++)
        if (strcmp(nome, elenco[i]) == 0) return 1;
    return 0;
}

/* =============================================================================
 * ! IL BERSAGLIO NON E' UN COMPONENTE DI SE STESSO
 *
 * `cerca_componenti` guarda le directory di primo livello del supporto d'avvio
 * e chiama componente tutto cio' che non e' di sistema. Ma il punto in cui si
 * sta INSTALLANDO e' anch'esso una directory di primo livello — e' li' che il
 * disco e' montato — quindi finiva nell'elenco, e `install -t` (che prende
 * tutti i componenti) COPIAVA LA DESTINAZIONE DENTRO SE STESSA:
 *
 *     ~ /disco/disco/boot/kernel.cfg
 *     ~ /disco/disco/disco/boot/kernel.cfg
 *     ~ /disco/disco/disco/disco/boot/kernel.cfg      ... e cosi' via
 *
 * 398 file inutili, sei livelli di profondita', fermati solo da
 * ALBERO_LIVELLI_MAX — che e' un tetto contro i cicli, non una scelta. Ed e'
 * anche l'origine dei «6 errori» che l'installazione dichiarava alla fine.
 *
 * ! E NON SI RIPARA ALLUNGANDO L'ELENCO DEI NOMI. In DIR_NON_COMPONENTI c'era
 * gia' «disk», che e' il punto di montaggio usato negli esempi della
 * documentazione; la ricetta di RIPRENDERE usa «/disco», e sono bastate due
 * lettere. Il nome del punto di montaggio lo sceglie chi installa, quindi
 * qualunque elenco di nomi indovinati sbaglia al primo che non c'e'.
 *
 * L'installatore il bersaglio ce l'ha in mano: si salta QUELLO.
 * ============================================================================= */
static void bersaglio_da_saltare(const char *punto, char *out, unsigned int max)
{
    unsigned int i = 0;

    out[0] = '\0';
    if (!punto) return;

    while (*punto == '/') punto++;               /* «/disco» -> «disco» */
    while (punto[i] && punto[i] != '/' && i < max - 1) { out[i] = punto[i]; i++; }
    out[i] = '\0';
    minuscolo(out);
}

static int cerca_componenti(const char *punto)
{
    DirEntry v[32];
    char     salta[DIRENT_NAME_MAX];
    int start = 0, n, i;

    g_n_comp = 0;
    bersaglio_da_saltare(punto, salta, sizeof(salta));

    while ((n = listdir_from("/", v, 32, start)) > 0) {
        for (i = 0; i < n && g_n_comp < COMPONENTI_MAX; i++) {
            char nome[DIRENT_NAME_MAX];

            if (!v[i].is_dir) continue;
            if (v[i].name[0] == '.') continue;

            strncpy(nome, v[i].name, DIRENT_NAME_MAX - 1);
            nome[DIRENT_NAME_MAX - 1] = '\0';
            minuscolo(nome);

            if (in_elenco(nome, DIR_SISTEMA,
                          (int)(sizeof DIR_SISTEMA / sizeof DIR_SISTEMA[0]))) continue;
            if (in_elenco(nome, DIR_NON_COMPONENTI,
                          (int)(sizeof DIR_NON_COMPONENTI / sizeof DIR_NON_COMPONENTI[0]))) continue;

            /* Il punto in cui stiamo installando: vedi sopra. */
            if (salta[0] && strcmp(nome, salta) == 0) continue;

            strcpy(g_comp[g_n_comp].nome, nome);
            g_comp[g_n_comp].installa = 0;
            g_n_comp++;
        }
        start += n;
        if (n < 32) break;
    }
    return g_n_comp;
}

/* Legge una riga dalla console. Rende il primo carattere, o 0 se non c'e'. */
static char chiedi(const char *domanda)
{
    char r[16];
    int  n;

    printf("%s", domanda);
    n = (int)read(0, r, sizeof(r) - 1);
    if (n < 0) n = 0;
    r[n] = '\0';
    return r[0];
}

/* `modo`: 0 = chiedi, 1 = minimale senza chiedere, 2 = tutto senza chiedere. */
static void scegli_componenti(int modo, const char *punto)
{
    int i;
    char c;

    if (cerca_componenti(punto) == 0) {
        if (modo == 0)
            printf("\nNon ci sono componenti opzionali su questo supporto:\n"
                   "si installa il sistema minimale.\n");
        return;
    }

    if (modo == 1) return;              /* minimale: nessuno */
    if (modo == 2) {                    /* tutto: tutti */
        for (i = 0; i < g_n_comp; i++) g_comp[i].installa = 1;
        return;
    }

    printf("\n===============================================================\n");
    printf(" COSA INSTALLARE\n");
    printf("===============================================================\n");
    printf("\nIl sistema minimale e' sempre incluso: kernel, /bin, /lib,\n");
    printf("i driver e l'avvio. Senza quello EX-OS non parte.\n\n");
    printf("Su questo supporto ci sono anche %d component%s in piu':\n\n",
           g_n_comp, (g_n_comp == 1) ? "e" : "i");

    for (i = 0; i < g_n_comp; i++)
        printf("    /%s\n", g_comp[i].nome);

    c = chiedi("\nInstallo solo il sistema minimale? [si/no] ");
    if (c == 's' || c == 'S') {
        printf("\nSistema minimale: nessun componente in piu'.\n");
        return;
    }

    printf("\nUno per volta:\n\n");
    for (i = 0; i < g_n_comp; i++) {
        char d[96];

        sprintf(d, "  /%s ? [si/no] ", g_comp[i].nome);
        c = chiedi(d);
        g_comp[i].installa = (c == 's' || c == 'S');
    }
}

static void installa_componenti(const char *punto, int aggiorna)
{
    int i, quanti = 0;

    for (i = 0; i < g_n_comp; i++) if (g_comp[i].installa) quanti++;
    if (quanti == 0) return;

    printf("\nComponenti\n");

    for (i = 0; i < g_n_comp; i++) {
        char sorgente[PERC_MAX];

        if (!g_comp[i].installa) continue;

        sorgente[0] = '/';
        strncpy(sorgente + 1, g_comp[i].nome, PERC_MAX - 2);
        sorgente[PERC_MAX - 1] = '\0';

        copia_albero(sorgente, punto, g_comp[i].nome, aggiorna);
    }
}

/* ! LA DOMANDA SI FA AL VOLUME, non al nome del punto di montaggio. ext2 ha
 * una directory perduta che FAT non ha: /lost+found la crea mkfs, esiste su
 * ogni volume ext2 e su nessun altro. E' il modo piu' semplice di distinguerli
 * dall'esterno, e non richiede una syscall nuova.
 *
 * ! IL PUNTO E' CHE SU FAT NON SI DEVE CREARE NIENTE. Senza proprietari,
 * /home sarebbe una directory come le altre e il login una serratura su una
 * porta senza muri: meglio non prometterlo. */
static int ext2_montato(const char *punto)
{
    char p[PERC_MAX];
    DirEntry v[4];

    unisci(p, punto, "lost+found");
    return listdir_from(p, v, 1, 0) >= 0;
}

/* =============================================================================
 * ! I PROGRAMMI NASCONO 0644, CIOE' SENZA IL BIT DI ESECUZIONE.
 *
 * ext2_create da' 0644 a qualunque file nuovo, ed e' giusto per un file di
 * testo. Ma un binario copiato qui e' un PROGRAMMA, e senza il bit x il
 * giorno che la VFS comincera' a controllarlo nessun utente normale potrebbe
 * eseguire piu' niente — e il difetto sembrerebbe del controllo, mentre
 * sarebbe di questa copia.
 *
 * ! I BIT SI METTONO ADESSO ANCHE SE NESSUNO LI GUARDA ANCORA, e non e'
 * lavoro sprecato: sono i bit che rendono possibile il controllo dopo. Il
 * contrario — accendere il controllo su un disco pieno di file senza x —
 * sarebbe un sistema installato in cui non parte niente.
 *
 * 0755: chi lo possiede (root) lo cambia, tutti lo leggono e lo eseguono.
 * E' quello che ci si aspetta da /bin su qualunque Unix.
 * ============================================================================= */
static int e_directory_di_programmi(const char *dest)
{
    return strcmp(dest, "bin")       == 0 ||
           strcmp(dest, "dev")       == 0 ||
           strcmp(dest, "drivers")   == 0 ||
           strcmp(dest, "exwin/bin") == 0;
}

static void copia_dir(const char *sorgente, const char *punto, const char *dest)
{
    DirEntry voci[LISTDIR_MAX_BATCH];
    char     pdest[PERC_MAX];
    unsigned int start = 0;
    int n;

    unisci(pdest, punto, dest);
    crea_dir(pdest);

    while ((n = listdir_from(sorgente, voci, LISTDIR_MAX_BATCH, start)) > 0) {
        int i;
        for (i = 0; i < n; i++) {
            char da[PERC_MAX], a[PERC_MAX];

            if (voci[i].is_dir) continue;          /* un livello solo */
            if (voci[i].name[0] == '.') continue;

            unisci(da, sorgente, voci[i].name);
            unisci(a,  pdest,    voci[i].name);
            minuscolo(a + strlen(pdest));   /* solo il NOME, non il punto */
            if (copia(da, a) >= 0 && e_directory_di_programmi(dest))
                chmod(a, 0755);             /* su FAT rende ENOSYS: pazienza */
        }
        start += (unsigned int)n;
        if (n < LISTDIR_MAX_BATCH) break;
    }
}

/* =============================================================================
 * I driver: non si copiano, si SCELGONO
 *
 * Fino ad agosto 2026 qui c'era copia_dir("/dev", ...), cioe' il supporto
 * di avvio riversato tale e quale sul disco. Il risultato era un /dev con
 * dentro il driver della scheda di rete che quella macchina non ha, e —
 * peggio — floppy.drv, che e' un modulo ET_DYN che spawn() rifiuta: un
 * file che nessuno puo' caricare, installato a ogni installazione.
 *
 * La scelta la fa `hwconfig -d`, che lancia ogni driver del catalogo con
 * `-i` e installa solo chi risponde di servire qui. Sta li' e non qui
 * perche' e' hwconfig il programma che guarda l'hardware: duplicarne la
 * logica darebbe due programmi che rispondono diversamente alla stessa
 * domanda, e il disco resterebbe con i driver di uno dei due.
 *
 * -y perche' il permesso di scrivere su questo disco l'utente l'ha gia'
 * dato a install: richiederlo insegna solo a rispondere di si' a occhi
 * chiusi.
 * ============================================================================= */
static int installa_driver(const char *punto)
{
    char *argv_hw[5];
    int   pid, stato = -1;

    printf("\nDriver\n");

    argv_hw[0] = "/bin/hwconfig";
    argv_hw[1] = "-d";
    argv_hw[2] = "-y";
    argv_hw[3] = (char *)punto;
    argv_hw[4] = NULL;

    pid = spawn("/bin/hwconfig", argv_hw);
    if (pid < 0) {
        printf("  ! /bin/hwconfig non si avvia (%s)\n", strerror(errno));
        printf("    I driver NON sono stati installati. Rimedio:\n");
        printf("      hwconfig -d %s\n", punto);
        return -1;
    }
    /* Oggi hwconfig gira con -y e non chiede niente, quindi non legge. Il
     * primo piano gli si cede lo stesso: il giorno che una domanda ci
     * fosse, non si presenterebbe come una risposta data da nessuno —
     * vedi il commento su console_setfg in installa_strumenti. */
    console_setfg((unsigned)pid);
    if (waitpid(pid, &stato, 0) < 0) {
        console_setfg((unsigned)getpid());
        return -1;
    }
    console_setfg((unsigned)getpid());

    if (stato != 0) {
        printf("  ! la scelta dei driver e' finita con %d\n", stato);
        return -1;
    }
    return 0;
}

/* =============================================================================
 * install -tools — gli STRUMENTI, non il sistema
 *
 * ! NON E' UNA SECONDA IMPLEMENTAZIONE: lancia /bin/toolinst e gli passa
 * gli argomenti. Le due installazioni si somigliano da fuori e non hanno
 * niente in comune dentro — qui si scrivono l'MBR, il settore di avvio e
 * la mappa dei settori del kernel, li' si copia un albero di 135 MB
 * conservandone la forma perche' gcc e fbc si cercano addosso. Scriverle
 * nello stesso file vorrebbe dire mescolare due lavori che sbagliano in
 * modi diversi.
 *
 * L'opzione sta qui perche' e' qui che la si cerca: chi ha appena dato
 * `install /disco` e vuole anche il compilatore prova `install` con
 * qualcosa, non un comando che si chiama toolinst.
 * ============================================================================= */
/* =============================================================================
 * kernel.cfg SI FONDE — non si sostituisce, e non si lascia nemmeno com'e'
 *
 * ! IL FILE E' DI CHI USA IL SISTEMA, MA CERTE VOCI SONO DEL SISTEMA, e fino a
 * oggi l'installatore vedeva solo la prima meta' della frase: se kernel.cfg
 * c'era gia' lo lasciava intatto e lo diceva. Su quasi tutte le voci e' la
 * scelta giusta — i montaggi, `verboseboot`, le variabili d'ambiente sono
 * decisioni prese di proposito, e un aggiornamento non deve disfarle.
 *
 * ! MA UNA VOCE CHE NEL FILE VECCHIO NON C'E' NON E' UNA DECISIONE DI NESSUNO:
 * e' una voce che non esisteva ancora quando quel file e' stato scritto. E
 * `login` e' esattamente quel caso, con la conseguenza peggiore possibile — il
 * kernel lancia /bin/login SOLO se la voce c'e' (kernel_main.c, PASSO 15),
 * quindi un sistema installato prima del 17 agosto 2026 e poi aggiornato si
 * riavviava CON LA RADICE ext2 E SENZA AUTENTICAZIONE, mentre due schermate
 * piu' su l'installatore stampava che «al primo avvio l'accesso sara'
 * OBBLIGATORIO». Diceva il falso, e lo diceva in favore di chi entra.
 *
 * La regola sta in due righe:
 *
 *     voce ASSENTE e necessaria  ->  si aggiunge, e si dice che si e' aggiunta
 *     voce PRESENTE ma diversa   ->  si lascia e si SUGGERISCE, perche' quella
 *                                    si' e' una decisione di chi usa il sistema
 *
 * ! E LE RIGHE COMMENTATE NON CONTANO COME VOCI. kernel.cfg e' pieno di esempi
 * spenti — «# svga = 800x600» — e scambiarne uno per una voce presente
 * vorrebbe dire non aggiungere mai la voce vera.
 * ============================================================================= */

/* Il tetto vero, e non e' scelto qui: kernel/fs/cfg.c legge 8191 byte e oltre
 * quelli le sezioni finali spariscono in silenzio — cioe' la macchina si
 * presenta senza tastiera. Un file che sforerebbe NON si scrive. */
#define CFG_MAX_BYTE    8191u
#define CFG_NOME_MAX    48

typedef struct {
    const char *sezione;
    const char *chiave;
    const char *valore;
    const char *perche;
} VoceCfg;

/* ! QUESTO E' L'ELENCO DA ALLUNGARE, e va allungato ogni volta che il kernel
 * impara a leggere una voce SENZA LA QUALE si comporta diversamente da come
 * l'installatore promette. Non ogni voce nuova: solo quelle la cui assenza e'
 * un difetto e non una preferenza. */
static const VoceCfg CFG_NECESSARIE[] = {
    { "boot", "login", "/bin/login",
      "senza, su una radice ext2 si entra SENZA autenticazione" },
    { 0, 0, 0, 0 }
};

/* Tutto il file in `dst`. Rende i byte letti, -1 se non si apre. */
static int cfg_leggi(const char *perc, char *dst, unsigned int max)
{
    int fd = open(perc, O_RDONLY), n;

    if (fd < 0) return -1;
    n = (int)read(fd, dst, (size_t)max - 1);
    close(fd);
    if (n < 0) n = 0;
    dst[n] = '\0';
    return n;
}

static const char *cfg_bianchi(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static const char *cfg_riga_dopo(const char *p)
{
    while (*p && *p != '\n') p++;
    return *p ? p + 1 : p;
}

static int cfg_uguale(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == '\0' && *b == '\0';
}

/* Copia in `out` cio' che sta prima di uno dei terminatori, senza gli spazi
 * ai bordi. Serve sia ai nomi di sezione (terminatore ']') sia alle chiavi
 * (terminatore '='). */
static void cfg_parola(const char *p, char fine, char *out, unsigned int max)
{
    unsigned int i = 0;

    p = cfg_bianchi(p);
    while (p[i] && p[i] != fine && p[i] != '\n' && p[i] != '\r' &&
           i < max - 1) { out[i] = p[i]; i++; }
    while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t')) i--;
    out[i] = '\0';
}

/* Cerca [sezione] chiave dentro `testo`. Rende:
 *
 *     1   la chiave c'e'      -> `valore` la riporta
 *     0   sezione si', chiave no -> `*coda` dice DOVE inserirla
 *    -1   la sezione non c'e' proprio
 *
 * `coda` e' lo scostamento subito dopo l'ultima riga vera della sezione: la
 * voce nuova finisce cosi' in mezzo alle altre e non dopo i commenti di
 * quella successiva. */
static int cfg_cerca(const char *testo, const char *sez, const char *chiave,
                     char *valore, unsigned int vmax, unsigned int *coda)
{
    const char *p = testo;
    const char *ultima = 0;
    int         dentro = 0;

    while (*p) {
        const char *r  = cfg_bianchi(p);
        const char *pr = cfg_riga_dopo(p);

        if (*r == '[') {
            char nome[CFG_NOME_MAX];

            if (dentro) break;              /* la sezione cercata finisce qui */
            cfg_parola(r + 1, ']', nome, sizeof(nome));
            dentro = cfg_uguale(nome, sez);
            if (dentro) ultima = pr;
            p = pr;
            continue;
        }

        if (dentro && *r != '#' && *r != '\n' && *r != '\r' && *r != '\0') {
            char k[CFG_NOME_MAX];

            cfg_parola(r, '=', k, sizeof(k));
            if (cfg_uguale(k, chiave)) {
                const char *v = r;

                while (*v && *v != '=' && *v != '\n') v++;
                if (vmax) valore[0] = '\0';
                if (*v == '=' && vmax) {
                    cfg_parola(v + 1, '\n', valore, vmax);
                }
                return 1;
            }
            ultima = pr;
        }

        p = pr;
    }

    if (dentro) {
        if (coda) *coda = (unsigned int)((ultima ? ultima : p) - testo);
        return 0;
    }
    return -1;
}

/* Infila `riga` allo scostamento `dove`. Rende 0 se non ci sta: e non farla
 * stare e' un esito, non un incidente — vedi il tetto di 8191. */
static int cfg_inserisci(char *testo, unsigned int max, unsigned int dove,
                         const char *riga)
{
    unsigned int n = (unsigned int)strlen(testo);
    unsigned int l = (unsigned int)strlen(riga);
    unsigned int i;

    if (n + l + 1 > max) return 0;
    if (dove > n) dove = n;

    for (i = n + 1; i-- > dove; ) testo[i + l] = testo[i];
    for (i = 0; i < l; i++) testo[dove + i] = riga[i];
    return 1;
}

/* La sezione non c'era: si aggiunge in fondo, con la sua voce dentro. */
static int cfg_appendi_sezione(char *testo, unsigned int max, const char *sez,
                               const char *riga)
{
    char         blocco[CFG_NOME_MAX + 8];
    unsigned int n = (unsigned int)strlen(testo);

    blocco[0] = '\n'; blocco[1] = '[';
    blocco[2] = '\0';
    strncat(blocco, sez, sizeof(blocco) - strlen(blocco) - 1);
    strncat(blocco, "]\n", sizeof(blocco) - strlen(blocco) - 1);

    if (!cfg_inserisci(testo, max, n, blocco)) return 0;
    return cfg_inserisci(testo, max, (unsigned int)strlen(testo), riga);
}

/* Le voci che il file spedito ha e quello installato no. Non si toccano: si
 * SUGGERISCONO, perche' se una voce nuova serva o no lo sa chi usa quella
 * macchina, non l'installatore. */
static int cfg_suggerisci(const char *nuovo, const char *vecchio)
{
    const char *p = nuovo;
    char        sez[CFG_NOME_MAX] = "";
    int         detti = 0;

    while (*p) {
        const char *r  = cfg_bianchi(p);
        const char *pr = cfg_riga_dopo(p);

        if (*r == '[') {
            cfg_parola(r + 1, ']', sez, sizeof(sez));
        } else if (sez[0] && *r != '#' && *r != '\n' && *r != '\r' && *r) {
            char k[CFG_NOME_MAX], v[128];

            cfg_parola(r, '=', k, sizeof(k));
            if (k[0] && cfg_cerca(vecchio, sez, k, v, sizeof(v), 0) != 1) {
                cfg_parola(r, '\n', v, sizeof(v));
                if (!detti) {
                    printf("    voci che il tuo kernel.cfg non ha ancora"
                           " (valuta se ti servono):\n");
                    detti = 1;
                }
                printf("      [%s] %s\n", sez, v);
            }
        }

        p = pr;
    }
    return detti;
}

/* Il lavoro vero. `perc` e' il kernel.cfg gia' installato. */
static void aggiorna_kernel_cfg(const char *perc)
{
    static char  testo[CFG_MAX_BYTE + 1];
    static char  spedito[CFG_MAX_BYTE + 1];
    char         valore[128];
    char         riga[192];
    char         bak[PERC_MAX];
    int          i, aggiunte = 0, stretto = 0;

    if (cfg_leggi(perc, testo, sizeof(testo)) < 0) {
        printf("  ! %s non si rilegge: lo lascio com'e'\n", perc);
        return;
    }

    for (i = 0; CFG_NECESSARIE[i].chiave; i++) {
        const VoceCfg *v = &CFG_NECESSARIE[i];
        unsigned int   coda = 0;
        int            dove;

        dove = cfg_cerca(testo, v->sezione, v->chiave,
                         valore, sizeof(valore), &coda);

        /* ! PRESENTE VUOL DIRE SCELTA, ANCHE SE E' DIVERSA DA QUELLA CHE
         * metteremmo noi: chi ha scritto `login = /bin/altro` sapeva cosa
         * stava facendo, e un aggiornamento che glielo cambia e' un
         * aggiornamento di cui non ci si puo' fidare. */
        if (dove == 1) {
            if (!cfg_uguale(valore, v->valore))
                printf("    [%s] %s = %s  (diverso dal predefinito %s:"
                       " lo lascio)\n",
                       v->sezione, v->chiave, valore, v->valore);
            continue;
        }

        riga[0] = '\0';
        strncat(riga, v->chiave, sizeof(riga) - strlen(riga) - 1);
        strncat(riga, "       = ", sizeof(riga) - strlen(riga) - 1);
        strncat(riga, v->valore, sizeof(riga) - strlen(riga) - 1);
        strncat(riga, "\n", sizeof(riga) - strlen(riga) - 1);

        if (dove == 0) {
            if (!cfg_inserisci(testo, sizeof(testo), coda, riga)) stretto = 1;
            else aggiunte++;
        } else {
            if (!cfg_appendi_sezione(testo, sizeof(testo), v->sezione, riga))
                stretto = 1;
            else aggiunte++;
        }

        if (!stretto)
            printf("    + [%s] %s = %s\n      %s\n",
                   v->sezione, v->chiave, v->valore, v->perche);
    }

    if (stretto) {
        printf("  ! kernel.cfg e' vicino al tetto di %u byte: non ci sta\n",
               CFG_MAX_BYTE);
        printf("    altro, e oltre quel tetto le sezioni finali spariscono.\n");
        printf("    Togli qualche commento e rilancia, oppure aggiungi a"
               " mano:\n");
        for (i = 0; CFG_NECESSARIE[i].chiave; i++)
            printf("      [%s] %s = %s\n", CFG_NECESSARIE[i].sezione,
                   CFG_NECESSARIE[i].chiave, CFG_NECESSARIE[i].valore);
    }

    if (aggiunte > 0) {
        /* ! IL FILE DI PRIMA SI TIENE, e non per abitudine: e' l'unico modo di
         * tornare indietro se la voce aggiunta non piace o non funziona su
         * questa macchina. Stessa regola di hwconfig. */
        /* ! UN PERCORSO TRONCATO NON E' UN PERCORSO PIU' CORTO, e qui
         * sarebbe stato pericoloso: `.../kernel.cfg` tagliato a misura piu'
         * «.bak» e' il nome di un ALTRO file, e copia() ci scriverebbe sopra
         * senza che nessuno l'abbia chiesto. Se non ci sta, la copia di
         * sicurezza non si fa e si dice — l'aggiornamento vale la pena lo
         * stesso, ma chi legge deve sapere che indietro non si torna da solo.
         * Trovato dal banco di prova sull'host, con un percorso lungo. */
        if (strlen(perc) + 4 < sizeof(bak)) {
            strcpy(bak, perc);
            strcat(bak, ".bak");
            if (copia(perc, bak) == 0)
                printf("    il file di prima e' in %s\n", bak);
            else
                printf("  ! copia di sicurezza %s non riuscita (%s)\n",
                       bak, strerror(errno));
        } else {
            printf("  ! %s e' troppo lungo per farne una copia .bak:"
                   " procedo senza\n", perc);
        }

        {
            int fd = open(perc, O_CREAT | O_WRONLY | O_TRUNC, 0644);
            int n  = (int)strlen(testo);

            if (fd < 0 || (int)write(fd, testo, (size_t)n) != n) {
                if (fd >= 0) close(fd);
                printf("  ! non riesco a riscrivere %s (%s)\n",
                       perc, strerror(errno));
                printf("    Il sistema si avviera' SENZA quelle voci.\n");
                errori++;
                return;
            }
            close(fd);
        }
        printf("  = %s aggiornato: %d voc%s aggiunt%s, il resto com'era\n",
               perc, aggiunte, aggiunte == 1 ? "e" : "i",
               aggiunte == 1 ? "a" : "e");
    } else if (!stretto) {
        printf("  = %s  (le voci necessarie ci sono gia': non lo tocco)\n",
               perc);
    }

    if (cfg_leggi("/boot/kernel.cfg", spedito, sizeof(spedito)) > 0)
        cfg_suggerisci(spedito, testo);
}

static int installa_strumenti(int argc, char **argv)
{
    char *a[16];
    int   n = 0, i, pid, stato = -1;

    a[n++] = "/bin/toolinst";
    /* Tutto cio' che segue -tools va a toolinst com'e': le sue opzioni le
     * documenta lui, e ricopiarle qui darebbe due elenchi che divergono. */
    for (i = 2; i < argc && n < 15; i++) a[n++] = argv[i];
    a[n] = NULL;

    pid = spawn("/bin/toolinst", a);
    if (pid < 0) {
        printf("install: /bin/toolinst non si avvia (%s)\n", strerror(errno));
        printf("\nGli strumenti li installa quel programma, non questo.\n");
        printf("Sta sul floppy e sul CD di EX-OS; se manca, l'immagine e'\n");
        printf("incompleta.\n");
        return 1;
    }

    /* ! LA TASTIERA VA CEDUTA AL FIGLIO, o le sue domande si rispondono
     * da sole. La shell ha dichiarato in primo piano NOI, e sys_read serve
     * solo chi e' in primo piano: senza questa riga ogni read() di
     * toolinst rende zero, e zero per chiedi_si() vuol dire «no».
     *
     * Il modo in cui si e' visto: `install -tools -n` stampava le tre
     * domande una dietro l'altra sulla stessa riga e installava il solo
     * gruppo C, senza che nessuno avesse risposto niente. Non c'era un
     * errore da nessuna parte — le scelte erano semplicemente state fatte
     * da nessuno. Vedi console_setfg in lib/include/libc.h. */
    console_setfg((unsigned)pid);
    if (waitpid(pid, &stato, 0) < 0) {
        console_setfg((unsigned)getpid());
        return 1;
    }
    console_setfg((unsigned)getpid());
    return stato;
}

int main(int argc, char **argv)
{
    char            p[PERC_MAX], q[PERC_MAX];
    BootInstallInfo info;
    int             r;
    int             aggiorna = 0;
    /* 0 = chiedi, 1 = solo il minimo, 2 = tutto senza chiedere */
    int             modo_comp = 0;

    if (argc >= 2 && strcmp(argv[1], "-tools") == 0)
        return installa_strumenti(argc, argv);

    if (argc < 2 || argc > 3) {
        printf("uso: install [-a|-m|-t] <punto di montaggio>\n");
        printf("     install -tools [opzioni] [punto di montaggio]\n\n");
        printf("Senza -a: installazione completa, riscrive tutto.\n");
        printf("Con  -a: aggiorna solo i file cambiati, e prima li elenca.\n");
        printf("Con  -m: sistema MINIMALE, nessun componente, non chiede.\n");
        printf("Con  -t: sistema e TUTTI i componenti, non chiede.\n\n");
        printf("Senza -m ne' -t l'installatore mostra i componenti trovati\n");
        printf("sul supporto e li chiede uno per uno.\n\n");
        printf("Il volume dev'essere gia' montato in lettura/scrittura:\n");
        printf("  disk                 elenca i dispositivi\n");
        printf("  mount hd0p1 /disk    monta\n");
        printf("  install /disk        installa\n");
        printf("  install -a /disk     aggiorna\n\n");
        printf("Non partiziona e non formatta: sono operazioni distruttive\n");
        printf("e vanno fatte di proposito, non come effetto collaterale.\n\n");
        printf("GLI STRUMENTI DI SVILUPPO SONO UN'ALTRA COSA. Stanno sul CD\n");
        printf("tools, non sul supporto di avvio, e si installano con:\n");
        printf("  install -tools               nel sistema in esecuzione\n");
        printf("  install -tools /disco        nel sistema montato in /disco\n");
        printf("  install -tools -n            dice cosa farebbe\n");
        printf("(e' /bin/toolinst: `toolinst -h` per tutte le opzioni)\n");
        return 1;
    }

    if (argc == 3) {
        if      (strcmp(argv[1], "-a") == 0) aggiorna  = 1;
        else if (strcmp(argv[1], "-m") == 0) modo_comp = 1;
        else if (strcmp(argv[1], "-t") == 0) modo_comp = 2;
        else {
            printf("install: opzione '%s' sconosciuta.\n", argv[1]);
            return 1;
        }
        argv[1] = argv[2];
    }

    /* =====================================================================
     * MODO AGGIORNAMENTO: prima si guarda, poi si chiede, poi si scrive.
     *
     * ! L'ELENCO VIENE PRIMA DELLA DOMANDA, e non e' cortesia: "aggiorno
     * 3 file?" e "aggiorno 47 file?" sono due decisioni diverse, e chi
     * risponde deve poter vedere QUALI prima di dire di si'. Un
     * aggiornamento che tocca tutto quando ci si aspettava un ritocco e'
     * il momento in cui ci si accorge di aver montato il volume sbagliato.
     * ===================================================================== */
    if (aggiorna) {
        int da_fare = 0, mancanti = 0;

        printf("Confronto di %s con il supporto di avvio\n", argv[1]);
        printf("  +  da creare    ~  da sostituire\n\n");

        da_fare += scorri_confronto("/bin", argv[1], "bin", 0, &mancanti);
        da_fare += scorri_confronto("/lib", argv[1], "lib", 0, &mancanti);
        /* ! /dev NON SI CONFRONTA CON IL SUPPORTO DI AVVIO. Sul disco
         * i driver sono un SOTTOINSIEME scelto — quelli che su questa
         * macchina trovano il proprio hardware — quindi ognuno che manca
         * e' una decisione, non una differenza da sanare. Elencarli come
         * «da creare» direbbe il contrario, e riportarli tutti indietro
         * disferebbe la scelta a ogni aggiornamento. Li rifa' hwconfig,
         * che e' l'unico a sapere quali servono. */
        printf("  driver: li risceglie hwconfig (sonda l'hardware)\n");

        {
            char kdest[PERC_MAX];
            int  st;

            unisci(kdest, argv[1], "boot");
            unisci(kdest, kdest, "kernel.bin");
            st = confronta("/KERNEL.BIN", kdest);
            if (st != STATO_UGUALE) {
                printf("  %s %s\n", (st == STATO_MANCANTE) ? "+" : "~", kdest);
                da_fare++;
            }
        }

        if (da_fare == 0) {
            printf("Niente da aggiornare: il volume ha gia' tutto.\n");
            return 0;
        }

        printf("\n%d file da aggiornare (%d nuovi).\n", da_fare, mancanti);
        printf("Procedo? [si/no] ");

        {
            char risposta[16];
            int  n = (int)read(0, risposta, sizeof(risposta) - 1);

            if (n < 0) n = 0;
            risposta[n] = '\0';
            if (risposta[0] != 's' && risposta[0] != 'S') {
                printf("Annullato: niente e' stato scritto.\n");
                return 0;
            }
        }
        printf("\n");
    }

    /* ! SI SCEGLIE PRIMA DI SCRIVERE, non a meta' strada. Chiedere «installo
     * anche /exwin?» dopo aver gia' sostituito il kernel vorrebbe dire che
     * rispondere «annulla» non annulla piu' niente. Qui l'unica cosa fatta e'
     * guardare cosa c'e' sul supporto. */
    scegli_componenti(modo_comp, argv[1]);

    printf(aggiorna ? "\nAggiornamento di EX-OS in %s\n"
                    : "\nInstallazione di EX-OS in %s\n", argv[1]);
    printf("  +  creato    ~  sostituito    !  errore\n\n");

    /* =====================================================================
     * 1. I DUE FILE DELL'AVVIO — e qui l'ordine e' tutto
     *
     * ! SI SCRIVONO PRIMA CON UN NOME TEMPORANEO, poi si verifica, e solo
     * alla fine prendono il nome definitivo. Fino alla 0.160 si copiava
     * direttamente sopra i vecchi, e la verifica arrivava dopo: se il
     * kernel nuovo risultava frammentato — cosa che su FAT succede appena
     * cresce di qualche KB, perche' li' la mappa ammette UN SOLO
     * intervallo — l'installatore lo diceva
     *
     *     ! installazione dell'avvio fallita: file frammentato
     *
     * ma quello che funzionava era gia' stato cancellato, il settore di
     * avvio puntava ancora alla mappa vecchia, e il disco non ripartiva
     * piu'. Nessuna via di ritorno.
     *
     * Ora: i vecchi restano intatti mentre si scrivono i nuovi (che quindi
     * finiscono nello spazio libero in coda, contiguo), si chiede al
     * kernel se sono mappabili, e SOLO SE la risposta e' si' si cancella e
     * si rinomina. ! La rinomina non sposta i dati (vedi vfs_rename), per
     * questo la mappa appena verificata vale ancora dopo.
     *
     * Se la verifica dice di no, si cancellano i temporanei e si esce: il
     * disco e' esattamente come prima, e continua a partire.
     * ===================================================================== */
    printf("Avvio (scritti a parte e verificati prima di sostituire)\n");
    unisci(p, argv[1], "boot");
    crea_dir(p);

    unisci(q, p, "stage2.new");
    if (copia("/LOADER.BIN", q) < 0) {
        printf("\nInstallazione interrotta: senza Stage 2 il disco non parte.\n");
        printf("Il sistema gia' installato NON e' stato toccato.\n");
        pulisci_temporanei(argv[1]);
        return 1;
    }

    unisci(q, p, "kernel.new");
    if (copia("/KERNEL.BIN", q) < 0) {
        printf("\nInstallazione interrotta: senza kernel il disco non parte.\n");
        printf("Il sistema gia' installato NON e' stato toccato.\n");
        pulisci_temporanei(argv[1]);
        return 1;
    }

    /* --- 1b. la verifica, mentre i vecchi sono ancora al loro posto --- */
    {
        BootInstallInfo prova;
        int v = bootverify(argv[1], "stage2.new", "kernel.new", &prova);

        if (v != 0) {
            printf("\n  ! i file nuovi non sono mappabili: %s (errore %d)\n",
                   spiega(v), v);
            printf("\nAggiornamento ANNULLATO. Il disco e' rimasto com'era e\n");
            printf("continua a partire con il sistema di prima.\n\n");
            if (v == -29) {
                printf("Il volume non ha abbastanza spazio LIBERO CONTIGUO per\n");
                printf("il kernel. Su FAT la mappa dell'avvio ammette un solo\n");
                printf("intervallo: serve un volume meno frammentato, oppure\n");
                printf("riformattare e installare da capo.\n");
            }
            pulisci_temporanei(argv[1]);
            return 1;
        }
        printf("  = verifica: %u settori in %u intervall%s — si puo' sostituire\n",
               prova.k_cnt, prova.k_next, (prova.k_next == 1) ? "o" : "i");
    }

    /* --- 1c. lo scambio: da qui in poi si tocca il sistema installato --- */
    if (sostituisci(p, "stage2") < 0 || sostituisci(p, "kernel") < 0) {
        printf("\nInstallazione interrotta durante la sostituzione.\n");
        return 1;
    }

    /* kernel.cfg: se manca si installa, se c'e' si FONDE. Il perche' della
     * fusione — e perche' «lo lascio com'e'» non bastava — sta tutto sopra
     * aggiorna_kernel_cfg(). */
    printf("\nConfigurazione\n");
    unisci(q, p, "kernel.cfg");
    {
        int f = open(q, O_RDONLY);
        if (f >= 0) {
            close(f);
            aggiorna_kernel_cfg(q);
        } else {
            copia("/boot/kernel.cfg", q);
        }
    }

    /* --- 2. il resto del sistema --- */
    printf("\nSistema\n");
    if (aggiorna) {
        scorri_confronto("/bin", argv[1], "bin", 1, 0);
        scorri_confronto("/lib", argv[1], "lib", 1, 0);
        installa_driver(argv[1]);
    } else {
        copia_dir("/bin", argv[1], "bin");
        copia_dir("/lib", argv[1], "lib");
        installa_driver(argv[1]);
    }

    /* ! I COMPONENTI DOPO IL SISTEMA, e l'ordine conta: se lo spazio finisce,
     * a mancare dev'essere /exwin, non /bin. Un sistema senza applicazioni
     * grafiche parte; un sistema senza /bin no. */
    installa_componenti(argv[1], aggiorna);

    /* =====================================================================
     * 2b. GLI UTENTI — solo su ext2, e solo perche' solo li' hanno senso
     *
     * ! FAT NON HA I PROPRIETARI, quindi su FAT non si crea niente: /home
     * sarebbe una directory come le altre, aperta a chiunque, e il login una
     * serratura su una porta senza muri. Su ext2 i proprietari ci sono, e da
     * li' in poi l'accesso e' obbligatorio.
     *
     * ! L'UTENTE LO CREA `login` AL PRIMO AVVIO, NON QUI, e non e' pigrizia:
     * per chiedere una password bisogna poterla leggere SENZA MOSTRARLA, cioe'
     * mettere la tastiera in modo raw — e l'installatore gira mentre la shell
     * possiede quella console. Chiederla qui vorrebbe dire contendersi la
     * tastiera con chi ci ha lanciati. login parte da solo, ha la console
     * tutta per se', e quel codice ce l'ha gia'.
     *
     * Quello che si fa qui e' preparare il posto e DIRLO, cosi' chi installa
     * sa cosa succedera' al riavvio invece di trovarsi una domanda inattesa.
     * ===================================================================== */
    if (ext2_montato(argv[1])) {
        char h[PERC_MAX];

        printf("\nUtenti\n");

        unisci(h, argv[1], "home");
        crea_dir(h);

        unisci(h, argv[1], "root");
        crea_dir(h);

        /* =====================================================================
         * ! I CONTI SI CREANO QUI, NON AL PRIMO AVVIO, dal 19 agosto 2026.
         *
         * Prima li creava `login` la prima volta che partiva, e il commento
         * che stava in questo punto diceva perche': per chiedere una password
         * bisogna leggerla SENZA MOSTRARLA, cioe' mettere la tastiera in modo
         * raw, e l'installatore gira mentre la shell possiede quella console.
         *
         * ! QUEL COMMENTO ERA VERO A META'. La tastiera si puo' prendere —
         * l'installatore E' gia' il processo in primo piano, ci parla per
         * chiedere «Procedo?» — e la lettura senza eco adesso sta in
         * lib/exuser, cioe' nello stesso codice che usa login. Non c'era da
         * inventare niente: c'era da spostare.
         *
         * ! E CIO' CHE MANCAVA DAVVERO ERA IL SECONDO CONTO. Lasciando fare a
         * login si otteneva UN utente solo, ed era root: da li' in poi si
         * lavorava sempre da amministratore, che e' il modo in cui un errore
         * qualunque diventa un danno qualunque. Qui se ne creano due — root
         * per riparare, e il tuo per lavorare — ed e' la ragione principale
         * per cui questo pezzo si e' spostato.
         * ===================================================================== */
        {
            char nome[EXUSER_NOME_MAX], p1[EXUSER_PASS_MAX], p2[EXUSER_PASS_MAX];
            int  fatto_root = 0, fatto_utente = 0;

            if (exuser_c_e_qualcuno(argv[1])) {
                printf("  = questo volume ha gia' un archivio utenti: lo lascio\n");
                printf("    com'e'. Per aggiungerne uno:  login -a\n");
            } else {
                exuser_prendi_console();

                printf("\n  Adesso i due conti. Il primo e' root, che serve a\n");
                printf("  riparare; il secondo e' il tuo, per lavorare.\n\n");

                /* --- root ------------------------------------------------- */
                while (!fatto_root) {
                    printf("  password di root: ");
                    if (exuser_leggi_password(p1, sizeof(p1)) < 0) {
                        printf("\n  ! La tastiera non risponde in modo raw: non posso\n");
                        printf("    chiedere una password senza mostrarla. Serve\n");
                        printf("    /dev/kbd.drv — vedi [modules] in kernel.cfg.\n");
                        printf("    I conti li creera' login al primo avvio.\n");
                        break;
                    }
                    if (p1[0] == '\0') {
                        printf("  Una password vuota lascia la macchina aperta.\n");
                        continue;
                    }
                    printf("  ripetila:         ");
                    if (exuser_leggi_password(p2, sizeof(p2)) < 0) break;
                    if (strcmp(p1, p2) != 0) {
                        printf("  Le due non coincidono.\n");
                        continue;
                    }
                    if (exuser_aggiungi(argv[1], "root", p1, 0u, 0u) != 0) {
                        printf("  ! non riesco a scrivere l'archivio utenti\n");
                        errori++;
                        break;
                    }
                    printf("  + root\n\n");
                    fatto_root = 1;
                }

                /* --- l'utente principale ---------------------------------- */
                while (fatto_root && !fatto_utente) {
                    printf("  il tuo nome utente: ");
                    if (exuser_leggi_riga(nome, sizeof(nome)) <= 0) continue;
                    if (!exuser_nome_valido(nome) || strcmp(nome, "root") == 0) {
                        printf("  Solo lettere, cifre e '_', e non «root».\n");
                        continue;
                    }
                    printf("  password:           ");
                    if (exuser_leggi_password(p1, sizeof(p1)) < 0) break;
                    if (p1[0] == '\0') {
                        printf("  Una password vuota lascia la macchina aperta.\n");
                        continue;
                    }
                    printf("  ripetila:           ");
                    if (exuser_leggi_password(p2, sizeof(p2)) < 0) break;
                    if (strcmp(p1, p2) != 0) {
                        printf("  Le due non coincidono.\n");
                        continue;
                    }

                    /* ! uid 1000, NON 0: questo conto serve a lavorare, e chi
                     * lavora da root non ha nessuna rete sotto. Per le cose da
                     * amministratore c'e' `su`. */
                    if (exuser_aggiungi(argv[1], nome, p1, 1000u, 1000u) != 0) {
                        printf("  ! non riesco a scrivere l'archivio utenti\n");
                        errori++;
                        break;
                    }

                    unisci(h, argv[1], "home");
                    strncat(h, "/", PERC_MAX - strlen(h) - 1);
                    strncat(h, nome, PERC_MAX - strlen(h) - 1);
                    crea_dir(h);
                    if (chown(h, 1000, 1000) != 0 && errno != ENOSYS)
                        printf("  ! %s creata ma non consegnata (%s)\n",
                               h, strerror(errno));

                    printf("  + %s (uid 1000), casa in %s\n", nome, h);

                    /* =====================================================
                     * ! «PRIVILEGI PARI A root» SI DA' CON UN ELENCO, NON CON
                     * UN SECONDO uid 0. Un altro conto con uid 0 sarebbe root
                     * a tutti gli effetti — stesso potere, stesso numero — e
                     * da quel momento nessuno saprebbe piu' dire CHI ha fatto
                     * cosa: nei file e nei log ci sarebbe scritto «0» e
                     * basta. Un elenco si legge, si toglie una riga, e chi si
                     * alza resta se stesso finche' non chiede.
                     *
                     * ! E SI CHIEDE, NON SI DA' PER SCONTATO: su una macchina
                     * di casa la risposta e' quasi sempre si', ma su una
                     * macchina con piu' persone non lo e', e indovinare
                     * vorrebbe dire darle a qualcuno senza dirglielo.
                     * ===================================================== */
                    if (chiedi("\n  Questo utente puo' fare le cose da root con "
                               "`sudo`,\n  usando la PROPRIA password? "
                               "[si/no] ") == 's') {
                        if (exuser_amministratore_aggiungi(argv[1], nome) == 0)
                            printf("  + %s e' amministratore\n", nome);
                        else {
                            printf("  ! non riesco a scrivere "
                                   "l'elenco degli amministratori\n");
                            errori++;
                        }
                    } else {
                        printf("  = %s non e' amministratore: `sudo` gli\n"
                               "    chiedera' la password di root.\n", nome);
                    }

                    fatto_utente = 1;
                }
            }
        }

        printf("\n  = questo volume e' ext2: ha i proprietari dei file, quindi\n");
        printf("    al primo avvio l'accesso sara' OBBLIGATORIO.\n");
    }

    /* --- 3. l'avvio vero e proprio --- */
    printf("\nSettori di avvio\n");
    r = bootinstall(argv[1], &info);
    if (r != 0) {
        printf("  ! installazione dell'avvio fallita: %s (errore %d)\n",
               spiega(r), r);
        printf("\nI file sono stati copiati, ma il disco NON e' avviabile.\n");
        return 1;
    }

    printf("  + MBR di hd%u, partizione %u marcata attiva\n",
           info.disco, info.voce);
    printf("  + settore di avvio della partizione\n");
    printf("    stage2: LBA %u, %u settori\n", info.s2_lba, info.s2_cnt);
    printf("    kernel: %u settori in %u intervall%s, dal LBA %u\n",
           info.k_cnt, info.k_next, (info.k_next == 1) ? "o" : "i", info.k_lba);
    if (info.k_next > 1) {
        printf("            (su ext2 il blocco di puntatori sta in mezzo ai\n");
        printf("             dati: il file e' contiguo a tratti, non in blocco)\n");
    }

    printf("\n");
    if (errori > 0) {
        printf("Completata con %d errori: controlla le righe con '!'.\n", errori);
        return 1;
    }

    printf("Installazione completata. Togli il floppy e riavvia.\n\n");
    printf("Ricorda: la mappa dei settori vale finche' quei file non si\n");
    printf("spostano. Se ricopi kernel o stage2 sul disco, rilancia install.\n");
    return 0;
}

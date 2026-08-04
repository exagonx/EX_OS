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

#define BLOCCO      4096
#define PERC_MAX    128

static char buf[BLOCCO];
static int  errori = 0;

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
 * ⚠️ FINO AL 0.148 QUESTA FUNZIONE NON SOVRASCRIVEVA: se la destinazione
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
        printf("  ! %s: %s\n", da, spiega(fs));
        errori++;
        return fs;
    }

    fd = open(a, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        close(fs);
        printf("  ! %s: %s\n", a, spiega(fd));
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
            printf("  ! %s: riscritto ma non rileggibile: %s\n", a, spiega(v));
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
 * ⚠️ SI CHIAMA SU OGNI USCITA ANTICIPATA. Un `.new` dimenticato occupa
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
 * ⚠️ E' L'UNICO PUNTO IN CUI SI TOCCA IL SISTEMA GIA' INSTALLATO, e ci si
 * arriva solo dopo che la verifica ha detto di si'.
 *
 * L'ordine e' cancella-poi-rinomina e non il contrario, perche' rename()
 * su EX-OS NON sostituisce la destinazione: darebbe EEXIST. Fra le due
 * operazioni c'e' un istante in cui il nome definitivo non esiste — ma il
 * contenuto e' salvo sotto il nome temporaneo, quindi il peggio che possa
 * capitare e' un disco da cui rilanciare l'installazione, non dati persi.
 *
 * ⚠️ E LA RINOMINA NON SPOSTA I DATI: e' la ragione per cui la mappa
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
    if (r == 0)        printf("  + %s/\n", p);
    else if (r == -17) printf("  = %s/ (gia' presente)\n", p);
    else {
        printf("  ! %s/: %s\n", p, spiega(r));
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
 * ⚠️ LA REGOLA E' "SORGENTE PIU' NUOVA", NON "DATE DIVERSE", e la
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
 * ⚠️ LA DIMENSIONE SI CONFRONTA COMUNQUE, e viene prima: un file
 * ricopiato a meta' per un disco pieno ha la stessa data e una
 * dimensione diversa. E' anzi il caso che conta di piu', perche' e'
 * l'unico in cui il volume e' rotto senza che nessuno l'abbia detto.
 *
 * ⚠️ SE UNA DELLE DUE DATE MANCA si guarda solo la dimensione. Zero
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
            copia(da, a);
        }
        start += (unsigned int)n;
        if (n < LISTDIR_MAX_BATCH) break;
    }
}

int main(int argc, char **argv)
{
    char            p[PERC_MAX], q[PERC_MAX];
    BootInstallInfo info;
    int             r;
    int             aggiorna = 0;

    if (argc < 2 || argc > 3) {
        printf("uso: install [-a] <punto di montaggio>\n\n");
        printf("Senza -a: installazione completa, riscrive tutto.\n");
        printf("Con  -a: aggiorna solo i file cambiati, e prima li elenca.\n\n");
        printf("Il volume dev'essere gia' montato in lettura/scrittura:\n");
        printf("  disk                 elenca i dispositivi\n");
        printf("  mount hd0p1 /disk    monta\n");
        printf("  install /disk        installa\n");
        printf("  install -a /disk     aggiorna\n\n");
        printf("Non partiziona e non formatta: sono operazioni distruttive\n");
        printf("e vanno fatte di proposito, non come effetto collaterale.\n");
        return 1;
    }

    if (argc == 3) {
        if (strcmp(argv[1], "-a") != 0) {
            printf("install: opzione '%s' sconosciuta.\n", argv[1]);
            return 1;
        }
        aggiorna = 1;
        argv[1]  = argv[2];
    }

    /* =====================================================================
     * MODO AGGIORNAMENTO: prima si guarda, poi si chiede, poi si scrive.
     *
     * ⚠️ L'ELENCO VIENE PRIMA DELLA DOMANDA, e non e' cortesia: "aggiorno
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
        da_fare += scorri_confronto("/dev", argv[1], "dev", 0, &mancanti);

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

    printf(aggiorna ? "Aggiornamento di EX-OS in %s\n"
                    : "Installazione di EX-OS in %s\n", argv[1]);
    printf("  +  creato    ~  sostituito    !  errore\n\n");

    /* =====================================================================
     * 1. I DUE FILE DELL'AVVIO — e qui l'ordine e' tutto
     *
     * ⚠️ SI SCRIVONO PRIMA CON UN NOME TEMPORANEO, poi si verifica, e solo
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
     * si rinomina. ⚠️ La rinomina non sposta i dati (vedi vfs_rename), per
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

    /* ⚠️ kernel.cfg NON si sovrascrive se c'e' gia': e' l'unico file di
     * tutto l'installatore che appartiene a chi usa il sistema e non al
     * sistema. Ci stanno dentro i montaggi automatici, verboseboot, la
     * shell, le variabili d'ambiente — cose che l'utente ha cambiato di
     * proposito e che un aggiornamento non deve riportare indietro in
     * silenzio. Se manca si installa, se c'e' si lascia e lo si dice. */
    unisci(q, p, "kernel.cfg");
    {
        int f = open(q, O_RDONLY);
        if (f >= 0) {
            close(f);
            printf("  = %s  (gia' presente: la tua configurazione resta)\n", q);
        } else {
            copia("/boot/kernel.cfg", q);
        }
    }

    /* --- 2. il resto del sistema --- */
    printf("\nSistema\n");
    if (aggiorna) {
        scorri_confronto("/bin", argv[1], "bin", 1, 0);
        scorri_confronto("/lib", argv[1], "lib", 1, 0);
        scorri_confronto("/dev", argv[1], "dev", 1, 0);
    } else {
        copia_dir("/bin", argv[1], "bin");
        copia_dir("/lib", argv[1], "lib");
        copia_dir("/dev", argv[1], "dev");
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

/* =============================================================================
 * bin/netupdate/netupdate.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * netupdate — il sistema si aggiorna dalla rete.
 *
 *     netupdate -set              scrive /boot/netupdate.cnf, chiedendo
 *     netupdate -check            guarda cosa e' cambiato sul server
 *     netupdate -install:list tes le app il cui nome contiene «tes»
 *     netupdate -install:nome     l'app, con tutto cio' che le serve
 *     netupdate -remove:nome      l'app, e le librerie che non servono ad altri
 *
 * Dall'altra parte c'e' `make netinst`, che prepara dist/netinst/: una
 * directory da appoggiare su un server FTP o HTTP, con dentro
 *
 *     versione.txt   versione, data, e l'impronta degli altri due
 *     catalogo.txt   i pacchetti: nome, cosa contengono, da cosa dipendono
 *     elenco.txt     un file per riga: percorso, byte, sha256, pacchetto
 *     file/...       l'albero, coi percorsi che avra' sul disco
 *
 * -----------------------------------------------------------------------------
 * ! SI CONFRONTA FILE PER FILE, NON PACCHETTO PER PACCHETTO
 *
 * E' la ragione per cui elenco.txt porta un'impronta per OGNI file. Se sul
 * server sono cambiati solo `ls` e `fdisk`, qui si devono proporre — e
 * scaricare — soltanto quei due. Un confronto per pacchetto farebbe
 * riscaricare l'intero sistema per due programmi, e su una linea lenta e' la
 * differenza fra un aggiornamento che si fa e uno che si rimanda per sempre.
 *
 * ! E L'IMPRONTA NON E' UN LUSSO. Serve a distinguere uno scaricamento
 * riuscito da uno interrotto a meta': un file troncato ha la data giusta, e
 * senza impronta la volta dopo non verrebbe riscaricato perche' «c'e' gia'».
 * ============================================================================= */
#include "libc.h"
#include "exhttp.h"
#include "inflate.h"

/* +0.001 a ogni modifica: `netupdate -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("netupdate", "0.010");

/* =============================================================================
 * IL FILE DI CONFIGURAZIONE
 *
 * ! STA IN /boot E NON IN $HOME, e non e' una svista. Aggiornare il sistema e'
 * una cosa della MACCHINA, non di chi ci ha fatto l'accesso: un netupdate.cnf
 * per utente vorrebbe dire che il sistema si aggiorna o no a seconda di chi
 * entra, e che l'aggiornamento automatico al primo accesso dipende da quale
 * primo accesso. In /boot c'e' gia' kernel.cfg, che e' la stessa specie di
 * cosa e si legge nello stesso momento.
 *
 * ! IL FORMATO E' QUELLO DI kernel.cfg — «chiave = valore», '#' commenta —
 * perche' e' quello che c'e'. Un secondo formato vorrebbe dire un secondo
 * lettore, e il secondo sbaglia dove il primo aveva gia' imparato.
 * ============================================================================= */
#define CNF "/boot/netupdate.cnf"

/* Il nome di servizio con cui si prova, PRIMA di chiedere qualunque cosa,
 * se in /boot si puo' scrivere davvero. Vedi boot_si_scrive(). */
#define CNF_PROVA "/boot/netupdate.prova"

#define URL_MAX   256
#define RIGA_MAX  512

/* I trasporti, nell'ordine in cui vengono proposti.
 *
 * ! HTTP E' IL PRIMO PERCHE' E' L'UNICO CHE FUNZIONA OGGI. FTPS e HTTPS
 * vogliono la TLS, e la stretta di TLS in EX-OS ha un limite noto (vedi
 * @DIF-TLS in in_lavorazione.txt): dentro un singolo passo non si respira.
 * Stanno qui perche' un campo del file di configurazione si scrive una volta
 * sola e cambiarlo dopo vuol dire migrare i file gia' scritti; ma chi li
 * sceglie oggi va avvisato, e infatti lo si avvisa. */
static const char *TRASPORTI[] = { "HTTP", "FTP", "HTTPS", "FTPS", 0 };

typedef struct {
    char tipo[8];        /* HTTP, FTP, HTTPS, FTPS                       */
    char url[URL_MAX];   /* la radice: dentro ci sono versione.txt e file/ */
    int  automatico;     /* 1 = controlla al primo accesso                */
} Config;

/* -----------------------------------------------------------------------------
 * Legge una riga da chi sta al terminale, senza il ritorno a capo.
 * Rende 0 se non c'e' piu' niente da leggere (Ctrl+D): chi chiama deve
 * distinguerlo da una riga vuota, che invece vuol dire «va bene il valore
 * proposto».
 * --------------------------------------------------------------------------- */
static int leggi_riga(char *buf, int max)
{
    int n;

    if (fgets(buf, max, stdin) == NULL) return 0;
    n = (int)strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return 1;
}

/* -----------------------------------------------------------------------------
 * config_leggi — riempie `c` dal file, e rende 0 se il file non c'e'.
 *
 * ! UNA CHIAVE CHE NON SI CONOSCE SI IGNORA, non e' un errore: un file
 * scritto da un netupdate piu' nuovo non deve rompere quello vecchio. E' la
 * stessa regola del catalogo degli strumenti, e per la stessa ragione.
 * --------------------------------------------------------------------------- */
static int config_leggi(Config *c)
{
    FILE *f;
    char  riga[RIGA_MAX];

    memset(c, 0, sizeof(*c));
    strcpy(c->tipo, "HTTP");

    f = fopen(CNF, "r");
    if (f == NULL) return 0;

    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *p = riga, *chiave, *valore, *fine;

        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        chiave = p;
        valore = strchr(p, '=');
        if (valore == NULL) continue;
        *valore++ = '\0';

        /* via gli spazi in coda alla chiave e in testa al valore */
        fine = chiave + strlen(chiave);
        while (fine > chiave && (fine[-1] == ' ' || fine[-1] == '\t')) *--fine = '\0';
        while (*valore == ' ' || *valore == '\t') valore++;
        fine = valore + strlen(valore);
        while (fine > valore && (fine[-1] == '\n' || fine[-1] == '\r' ||
                                 fine[-1] == ' '  || fine[-1] == '\t')) *--fine = '\0';

        if (strcasecmp(chiave, "tipo") == 0) {
            strncpy(c->tipo, valore, sizeof(c->tipo) - 1);
            c->tipo[sizeof(c->tipo) - 1] = '\0';
        } else if (strcasecmp(chiave, "url") == 0) {
            strncpy(c->url, valore, sizeof(c->url) - 1);
            c->url[sizeof(c->url) - 1] = '\0';
        } else if (strcasecmp(chiave, "automatico") == 0) {
            c->automatico = (valore[0] == 's' || valore[0] == 'S' ||
                             valore[0] == '1' ||
                             valore[0] == 'y' || valore[0] == 'Y');
        }
        /* ogni altra chiave: ignorata apposta, vedi sopra */
    }
    fclose(f);
    return 1;
}

/* -----------------------------------------------------------------------------
 * config_scrivi — scrive il file, coi commenti dentro.
 *
 * ! I COMMENTI SI RISCRIVONO OGNI VOLTA, e si perdono quelli di chi ha
 * modificato il file a mano. E' la stessa scelta che fa `hwconfig` con
 * kernel.cfg, e per la stessa ragione: un file generato che cerca di
 * conservare il testo altrui finisce per conservarlo sbagliato. Chi vuole
 * annotazioni sue le mette accanto, non dentro.
 * --------------------------------------------------------------------------- */
static int config_scrivi(const Config *c)
{
    FILE *f = fopen(CNF, "w");

    if (f == NULL) {
        fprintf(stderr, "netupdate: non riesco a scrivere %s (%s)\n",
                CNF, strerror(errno));
        return -1;
    }

    fprintf(f, "# =============================================================\n");
    fprintf(f, "# netupdate.cnf - da dove il sistema si aggiorna\n");
    fprintf(f, "#\n");
    fprintf(f, "# Lo scrive `netupdate -set`. Si puo' correggere a mano: una\n");
    fprintf(f, "# chiave per riga, «chiave = valore», '#' commenta.\n");
    fprintf(f, "#\n");
    fprintf(f, "# tipo        HTTP, FTP, HTTPS o FTPS\n");
    fprintf(f, "# url         la RADICE: li' dentro ci sono versione.txt,\n");
    fprintf(f, "#             catalogo.txt, elenco.txt e la directory file/\n");
    fprintf(f, "# automatico  si -> si controlla da soli al primo accesso;\n");
    fprintf(f, "#             no -> solo quando lo si chiede da riga di comando\n");
    fprintf(f, "# =============================================================\n");
    fprintf(f, "\n");
    fprintf(f, "tipo       = %s\n", c->tipo);
    fprintf(f, "url        = %s\n", c->url);
    fprintf(f, "automatico = %s\n", c->automatico ? "si" : "no");

    if (fclose(f) != 0) {
        fprintf(stderr, "netupdate: %s non si e' chiuso bene (%s)\n",
                CNF, strerror(errno));
        return -1;
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * boot_si_scrive — in /boot ci si puo' scrivere?
 *
 * ! SI CHIEDE PRIMA DI CHIEDERE, e non e' pignoleria. Da CD il sistema si
 * avvia benissimo e /boot e' in sola lettura: senza questa prova `-set`
 * faceva le tre domande, aspettava le tre risposte, e SOLO ALLORA diceva che
 * non poteva scrivere niente. E' la regola dei rifiuti di `make usb` — tutti
 * prima di cominciare, mai a lavoro fatto — e vale ancora di piu' qui, dove
 * quel che si butta via e' quel che ha appena battuto qualcuno.
 *
 * ! E LA PROVA E' UNA SCRITTURA VERA, SU UN NOME DI SERVIZIO. access() non
 * servirebbe a niente: senza permessi da controllare rende «si» per qualunque
 * file esista (sta scritto in lib/libc.c), e il solo caso che conta — il
 * montaggio in sola lettura — si scopre soltanto aprendo. Il nome di servizio
 * c'e' perche' provare su netupdate.cnf vorrebbe dire TRONCARE quello buono a
 * chi poi risponde male a una domanda e non arriva mai a riscriverlo.
 * --------------------------------------------------------------------------- */
static int boot_si_scrive(void)
{
    FILE *f = fopen(CNF_PROVA, "w");

    if (f == NULL) return 0;
    fclose(f);
    remove(CNF_PROVA);
    return 1;
}

/* =============================================================================
 * -set
 * ============================================================================= */
static int comando_set(void)
{
    Config c;
    char   riga[RIGA_MAX];
    int    c_era, i, scelta;

    if (!boot_si_scrive()) {
        fprintf(stderr, "netupdate: non posso scrivere %s (%s).\n",
                CNF, strerror(errno));
        fprintf(stderr, "           Non chiedo niente: le risposte andrebbero\n");
        fprintf(stderr, "           perse. Se questo EX-OS e' partito dal CD,\n");
        fprintf(stderr, "           /boot e' il CD: la configurazione vuole un\n");
        fprintf(stderr, "           sistema installato su disco o su chiavetta.\n");
        return 1;
    }

    c_era = config_leggi(&c);

    printf("Da dove si aggiorna questo sistema\n");
    printf("\n");
    if (c_era)
        printf("  (%s c'e' gia': Invio tiene il valore fra parentesi)\n\n", CNF);

    /* --- il trasporto ------------------------------------------------------ */
    printf("  Come ci si collega?\n");
    for (i = 0; TRASPORTI[i]; i++) {
        printf("    %d) %-5s", i + 1, TRASPORTI[i]);
        if (i == 0) printf("   il piu' semplice da servire");
        if (i == 1) printf("   se il server e' un FTP");
        if (i >= 2) printf("   ! vuole la TLS: vedi l'avviso in fondo");
        printf("\n");
    }
    printf("\n  scelta [1-%d, Invio = %s]: ", i, c.tipo);
    fflush(stdout);

    if (leggi_riga(riga, sizeof(riga)) && riga[0] != '\0') {
        scelta = atoi(riga);
        if (scelta >= 1 && scelta <= i) {
            strncpy(c.tipo, TRASPORTI[scelta - 1], sizeof(c.tipo) - 1);
            c.tipo[sizeof(c.tipo) - 1] = '\0';
        } else {
            printf("  ! «%s» non e' fra 1 e %d: tengo %s\n", riga, i, c.tipo);
        }
    }

    /* --- l'indirizzo ------------------------------------------------------- */
    printf("\n  L'indirizzo della RADICE, cioe' la directory che contiene\n");
    printf("  versione.txt, catalogo.txt, elenco.txt e file/.\n");
    printf("  Esempio:  esempio.org/exos\n");
    if (c.url[0]) printf("\n  adesso e': %s\n", c.url);
    printf("\n  indirizzo: ");
    fflush(stdout);

    if (leggi_riga(riga, sizeof(riga)) && riga[0] != '\0') {
        /* ! LO SCHEMA SI TOGLIE, NON SI TIENE. Chi scrive «http://esempio.org»
         * dice due volte la stessa cosa — il trasporto l'ha appena scelto — e
         * le due potrebbero contraddirsi. Si tiene la scelta esplicita, e
         * l'indirizzo si riduce a cio' che e': un host e un percorso. */
        char *p = riga;
        char *s = strstr(p, "://");

        if (s != NULL) p = s + 3;
        strncpy(c.url, p, sizeof(c.url) - 1);
        c.url[sizeof(c.url) - 1] = '\0';

        /* e la barra finale se ne va: i percorsi si compongono con una sola */
        i = (int)strlen(c.url);
        while (i > 0 && c.url[i-1] == '/') c.url[--i] = '\0';
    }

    if (c.url[0] == '\0') {
        printf("\n  Senza indirizzo non c'e' niente da controllare.\n");
        printf("  Non ho scritto niente.\n");
        return 1;
    }

    /* --- automatico o no --------------------------------------------------- */
    printf("\n  Controllare da soli, al primo accesso?\n");
    printf("    si  ogni volta che qualcuno entra, si guarda se c'e' di nuovo\n");
    printf("    no  solo quando si scrive `netupdate -check`\n");
    /* ! IL CAMPO SI SCRIVE, MA OGGI NESSUNO LO LEGGE, e va detto qui invece
     * che scoperto non vedendo mai un controllo partire. Perche' parta da solo
     * serve una riga in /boot/autoexec.sh; il campo c'e' perche' quel giorno
     * la risposta sia gia' scritta, non per fingere che ci sia gia'. */
    printf("    ! oggi il controllo automatico NON parte da solo: la risposta\n");
    printf("      si scrive, e chi la fara' partire e' una riga in\n");
    printf("      /boot/autoexec.sh.\n");
    printf("\n  [si/no, Invio = %s]: ", c.automatico ? "si" : "no");
    fflush(stdout);

    if (leggi_riga(riga, sizeof(riga)) && riga[0] != '\0')
        c.automatico = (riga[0] == 's' || riga[0] == 'S');

    /* --- si scrive, e si rilegge ------------------------------------------- */
    if (config_scrivi(&c) != 0) return 1;

    printf("\n%s scritto:\n", CNF);
    printf("  tipo       %s\n", c.tipo);
    printf("  url        %s\n", c.url);
    printf("  automatico %s\n", c.automatico ? "si" : "no");

    if (strcasecmp(c.tipo, "HTTPS") == 0 || strcasecmp(c.tipo, "FTPS") == 0) {
        printf("\n  ! HAI SCELTO UN TRASPORTO CIFRATO, e oggi non e' pronto.\n");
        printf("    La stretta di TLS in EX-OS ha un limite noto e `-check`\n");
        printf("    puo' fallire. Il campo e' scritto lo stesso — cambiarlo\n");
        printf("    dopo e' una riga — ma per adesso HTTP e' la strada che\n");
        printf("    funziona.\n");
    }

    printf("\n  Adesso:  netupdate -check\n");
    return 0;
}

/* =============================================================================
 * IL CATALOGO E IL REGISTRO
 *
 * Due cose diverse, e confonderle e' il primo modo di sbagliare.
 *
 *   IL CATALOGO sta sul SERVER e dice COSA ESISTE: nomi, pesi, dipendenze.
 *   Lo prepara `make netinst` a partire da tools/iso/strumenti.txt, che e'
 *   gia' un catalogo e ha gia' le chiavi che servono.
 *
 *   IL REGISTRO sta su QUESTA MACCHINA e dice COSA C'E' INSTALLATO: per ogni
 *   pacchetto la versione, da cosa dipende, e L'ELENCO DEI FILE che gli
 *   appartengono. Senza, `-remove` non sa cosa togliere e `-check` non sa
 *   cosa confrontare.
 *
 * ! UN REGISTRO CHE MENTE E' PEGGIO DI NESSUN REGISTRO, perche' `-remove`
 * cancella quel che ci trova scritto. Da qui tre regole che si vedono nel
 * codice qui sotto:
 *
 *   - si riscrive INTERO a ogni installazione, aggiornamento e rimozione:
 *     una riga aggiunta a mano a un file che nessuno rilegge e' il modo di
 *     farlo divergere in silenzio;
 *   - si scrive PRIMA di sostituire, e si sostituisce solo a scrittura
 *     finita (vedi registro_scrivi_fine): una macchina che si spegne in
 *     mezzo si ritrova senza registro, non con mezzo;
 *   - un'impronta che non si sa NON SI INVENTA: si scrive «-», che vuol dire
 *     «questo file c'e' ma non so se e' quello», e il primo -check lo
 *     riguardera'.
 *
 * ! LE DIPENDENZE STANNO NEL REGISTRO E NON SOLO NEL CATALOGO, ed e' voluto:
 * `-remove` deve funzionare SENZA RETE. Se «chi usa xlib?» si potesse
 * rispondere solo scaricando il catalogo, togliere un'applicazione su una
 * macchina scollegata vorrebbe dire indovinare, e indovinare qui vuol dire
 * portarsi via una libreria che serve a qualcun altro.
 * ============================================================================= */
#define REG      "/boot/netupdate.reg"
#define REG_NEW  "/boot/netupdate.new"

#define PACCHETTI_MAX  32
#define VUOLE_MAX       4
#define ID_MAX         24
#define ETICH_MAX      48
#define DICE_MAX       96
#define PERC_MAX      256
#define IMPR_MAX       72       /* 64 esadecimali, il terminatore e un margine */

typedef struct {
    char id[ID_MAX];            /* «cpp», dalle quadre                        */
    char nome[ETICH_MAX];       /* «C++», per gli occhi                       */
    char dice[DICE_MAX];
    char prova[PERC_MAX];       /* un percorso che, se c'e', dice che c'e'    */
    char versione[16];
    char data[32];
    /* L'archivio, quando c'e': quattro chiavi che stanno insieme o non
     * servono. `arcsrotolato` e' quanto occupa APERTO, e si legge prima di
     * scaricare: inflate() vuole tutto in memoria, e la decisione si prende
     * con quel numero in mano. */
    char archivio[PERC_MAX];
    long arcbyte;
    char arcimpronta[IMPR_MAX];
    long arcsrotolato;
    char vuole[VUOLE_MAX][ID_MAX];
    int  n_vuole;
    long mbyte;
    int  sempre;

    long n_file;                /* righe `file` lette: solo nel registro      */
    long n_ignoti;              /* di quelle, quante senza impronta           */
    int  c_e;                   /* la `prova` esiste sul disco (serve a crea) */
} Pacchetto;

/* -----------------------------------------------------------------------------
 * Il lettore del formato a blocchi. UNO SOLO PER I DUE FILE: catalogo e
 * registro hanno le stesse chiavi perche' sono la stessa specie di cosa vista
 * da due parti, e due lettori vorrebbero dire che il secondo sbaglia dove il
 * primo aveva gia' imparato.
 * --------------------------------------------------------------------------- */
static void copia_str(char *dst, const char *src, int max)
{
    int i;

    for (i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static char *ripulisci(char *s)
{
    int l;

    while (*s == ' ' || *s == '\t') s++;
    l = (int)strlen(s);
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t' ||
                     s[l-1] == '\r' || s[l-1] == '\n')) s[--l] = '\0';
    return s;
}

/* «chiave = valore» -> il valore ripulito, oppure NULL se la riga non e'
 * quella chiave. La chiave si confronta senza distinguere maiuscole. */
static char *valore_se(char *riga, const char *chiave)
{
    char *p = riga;
    int   i = 0;

    while (*p == ' ' || *p == '\t') p++;
    while (chiave[i]) {
        char c = p[i];

        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != chiave[i]) return NULL;
        i++;
    }
    p += i;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return NULL;
    p++;
    return ripulisci(p);
}

static int blocchi_leggi(const char *percorso, Pacchetto *v, int max)
{
    FILE      *f;
    char       riga[RIGA_MAX];
    Pacchetto *p = NULL;
    int        n = 0;

    f = fopen(percorso, "r");
    if (f == NULL) return -1;

    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *r = ripulisci(riga), *val;

        if (r[0] == '\0' || r[0] == '#') continue;

        if (r[0] == '[') {
            char *fine = r;

            while (*fine && *fine != ']') fine++;
            if (*fine != ']') continue;
            *fine = '\0';

            if (n >= max) {
                printf("  ! piu' di %d pacchetti: ignoro il resto\n", max);
                break;
            }
            p = &v[n++];
            memset(p, 0, sizeof(*p));
            copia_str(p->id, ripulisci(r + 1), ID_MAX);
            copia_str(p->nome, p->id, ETICH_MAX);   /* senza `nome`, l'id */
            continue;
        }

        if (p == NULL) continue;        /* chiavi prima del primo blocco */

        if ((val = valore_se(r, "nome"))     != NULL) { copia_str(p->nome, val, ETICH_MAX); continue; }
        if ((val = valore_se(r, "dice"))     != NULL) { copia_str(p->dice, val, DICE_MAX); continue; }
        if ((val = valore_se(r, "prova"))    != NULL) { copia_str(p->prova, val, PERC_MAX); continue; }
        if ((val = valore_se(r, "versione")) != NULL) { copia_str(p->versione, val, 16); continue; }
        if ((val = valore_se(r, "data"))     != NULL) { copia_str(p->data, val, 32); continue; }
        if ((val = valore_se(r, "mbyte"))    != NULL) { p->mbyte = atol(val); continue; }
        if ((val = valore_se(r, "archivio")) != NULL) { copia_str(p->archivio, val, PERC_MAX); continue; }
        if ((val = valore_se(r, "arcbyte"))  != NULL) { p->arcbyte = atol(val); continue; }
        if ((val = valore_se(r, "arcimpronta"))  != NULL) { copia_str(p->arcimpronta, val, IMPR_MAX); continue; }
        if ((val = valore_se(r, "arcsrotolato")) != NULL) { p->arcsrotolato = atol(val); continue; }
        if ((val = valore_se(r, "sempre"))   != NULL) {
            p->sempre = (val[0] == 's' || val[0] == 'S' || val[0] == '1');
            continue;
        }
        if ((val = valore_se(r, "vuole")) != NULL) {
            if (p->n_vuole < VUOLE_MAX) copia_str(p->vuole[p->n_vuole++], val, ID_MAX);
            continue;
        }
        if ((val = valore_se(r, "file")) != NULL) {
            p->n_file++;
            if (val[0] == '-' && (val[1] == ' ' || val[1] == '\t')) p->n_ignoti++;
            continue;
        }
        /* Chiave sconosciuta: si ignora. Un file scritto da un netupdate piu'
         * nuovo non deve rompere questo. */
    }
    fclose(f);
    return n;
}

static Pacchetto *cerca(Pacchetto *v, int n, const char *id)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(v[i].id, id) == 0) return &v[i];
    return NULL;
}

/* -----------------------------------------------------------------------------
 * chi_usa — «se tolgo questo, chi resta a piedi?»
 *
 * ! LE LIBRERIE CONDIVISE SI CONTANO, NON SI INDOVINANO. Se xlib serve a xapp
 * e a yapp, togliere xapp non deve portarsela via. La risposta e' questa
 * funzione, e va anche STAMPATA: chi non vede la riga «xlib resta: la usano
 * yapp, zapp» pensa che la rimozione sia fallita.
 * --------------------------------------------------------------------------- */
static int fra(char elenco[][ID_MAX], int n, const char *id);

/* `via`/`nvia` sono i pacchetti che stanno per andarsene: NON contano come
 * utilizzatori. Senza questa esclusione, `-remove` direbbe che una libreria
 * resta viva grazie a un pacchetto che sta cancellando nello stesso comando. */
static int chi_usa_salvo(Pacchetto *v, int n, const char *id,
                         char via[][ID_MAX], int nvia, char *out, int max)
{
    int i, j, quanti = 0;

    out[0] = '\0';
    for (i = 0; i < n; i++) {
        if (strcmp(v[i].id, id) == 0) continue;
        if (nvia > 0 && fra(via, nvia, v[i].id)) continue;
        for (j = 0; j < v[i].n_vuole; j++) {
            if (strcmp(v[i].vuole[j], id) != 0) continue;
            if (out[0] != '\0' && (int)strlen(out) < max - 3) strcat(out, ", ");
            if ((int)strlen(out) + (int)strlen(v[i].id) < max - 1) strcat(out, v[i].id);
            quanti++;
            break;
        }
    }
    if (quanti == 0) copia_str(out, "nessuno", max);
    return quanti;
}

static int chi_usa(Pacchetto *v, int n, const char *id, char *out, int max)
{
    return chi_usa_salvo(v, n, id, NULL, 0, out, max);
}

/* -----------------------------------------------------------------------------
 * Il registro non c'e': lo si dice una volta sola, e si dice come farlo.
 * --------------------------------------------------------------------------- */
static int niente_registro(void)
{
    printf("Non c'e' nessun registro (%s).\n", REG);
    printf("\n");
    printf("  Vuol dire che questo sistema non ha mai parlato con un server:\n");
    printf("  finche' il registro non c'e', `-check` non sa cosa confrontare\n");
    printf("  e `-remove` non sa cosa togliere.\n");
    printf("\n");
    printf("  Lo si scrive da una copia locale dell'albero che `make netinst`\n");
    printf("  pubblica — un CD, una chiavetta, una directory montata:\n");
    printf("      netupdate -registro:crea <albero>\n");
    printf("  oppure lo scrivera' il primo `netupdate -check`.\n");
    return 1;
}

/* -----------------------------------------------------------------------------
 * -registro — la tabella di cio' che c'e'
 * --------------------------------------------------------------------------- */
static int comando_registro(void)
{
    Pacchetto v[PACCHETTI_MAX];
    char      usato[256];
    int       n, i;
    long      tot = 0, ign = 0;

    n = blocchi_leggi(REG, v, PACCHETTI_MAX);
    if (n < 0) return niente_registro();
    if (n == 0) {
        printf("%s c'e' ma non nomina nessun pacchetto.\n", REG);
        printf("  E' un registro vuoto, non un registro assente: se non e'\n");
        printf("  quel che ti aspetti, riscrivilo con `-registro:crea`.\n");
        return 1;
    }

    printf("Installato su questa macchina (%s)\n\n", REG);
    printf("  %-12s %-9s %7s  %-16s %s\n",
           "PACCHETTO", "VERSIONE", "FILE", "LO USANO", "COS'E'");
    for (i = 0; i < n; i++) {
        chi_usa(v, n, v[i].id, usato, sizeof(usato));
        printf("  %-12s %-9s %7ld  %-16s %s\n",
               v[i].id,
               v[i].versione[0] ? v[i].versione : "?",
               v[i].n_file,
               strcmp(usato, "nessuno") == 0 ? "-" : usato,
               v[i].nome);
        tot += v[i].n_file;
        ign += v[i].n_ignoti;
    }

    printf("\n  %ld file in %d pacchetti", tot, n);
    if (ign > 0) printf(", di cui %ld senza impronta", ign);
    printf(".\n");
    if (ign > 0) {
        printf("  «Senza impronta» vuol dire: il file c'e' ma non si sa se e'\n");
        printf("  quello dell'elenco. Il primo `-check` lo riguardera'.\n");
    }
    printf("\n  Un pacchetto per intero:  netupdate -registro:%s\n", v[0].id);
    return 0;
}

/* -----------------------------------------------------------------------------
 * -registro:<id> — un pacchetto solo, coi suoi file
 *
 * ! I FILE SI RILEGGONO DAL FILE, NON SI TENGONO IN MEMORIA. Sono millecinque-
 * cento righe con gli strumenti: tenerle tutte vuol dire mezzo megabyte di
 * heap che su EX-OS non si restituisce (free() non rende niente al sistema).
 * Scorrere due volte un file di centoquaranta kilobyte costa molto meno.
 * --------------------------------------------------------------------------- */
static int comando_registro_uno(const char *id)
{
    Pacchetto  v[PACCHETTI_MAX];
    char       usato[256];
    FILE      *f;
    char       riga[RIGA_MAX];
    int        n, i, dentro = 0;
    Pacchetto *p;

    n = blocchi_leggi(REG, v, PACCHETTI_MAX);
    if (n < 0) return niente_registro();

    p = cerca(v, n, id);
    if (p == NULL) {
        printf("netupdate: «%s» non risulta installato.\n\n", id);
        printf("  Ci sono:");
        for (i = 0; i < n; i++) printf(" %s", v[i].id);
        printf("\n");
        return 1;
    }

    chi_usa(v, n, p->id, usato, sizeof(usato));

    printf("%s — %s\n", p->id, p->nome);
    if (p->dice[0]) printf("%s\n", p->dice);
    printf("\n");
    printf("  versione   %s\n", p->versione[0] ? p->versione : "?");
    printf("  dall'albero del %s\n", p->data[0] ? p->data : "?");
    printf("  vuole      ");
    if (p->n_vuole == 0) printf("niente\n");
    else {
        for (i = 0; i < p->n_vuole; i++) printf("%s%s", i ? ", " : "", p->vuole[i]);
        printf("\n");
    }
    printf("  lo usano   %s\n", usato);
    printf("  file       %ld", p->n_file);
    if (p->n_ignoti > 0) printf(" (%ld senza impronta)", p->n_ignoti);
    printf("\n\n");

    /* I file, dal registro, senza tenerli in memoria. */
    f = fopen(REG, "r");
    if (f == NULL) return 1;
    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *r = ripulisci(riga), *val;

        if (r[0] == '[') {
            char *fine = r;

            while (*fine && *fine != ']') fine++;
            if (*fine == ']') {
                *fine = '\0';
                dentro = (strcmp(ripulisci(r + 1), id) == 0);
            }
            continue;
        }
        if (!dentro) continue;
        if ((val = valore_se(r, "file")) == NULL) continue;

        /* «<impronta> <percorso>»: l'impronta e' un pezzo solo, il percorso
         * e' tutto il resto — un percorso puo' contenere spazi, un'impronta
         * no, e quindi si divide sul PRIMO spazio e non sull'ultimo. */
        {
            char *sp = val;

            while (*sp && *sp != ' ' && *sp != '\t') sp++;
            if (*sp == '\0') continue;
            *sp++ = '\0';
            while (*sp == ' ' || *sp == '\t') sp++;
            printf("  %-8.8s  /%s\n", val[0] == '-' ? "sconosc" : val, sp);
        }
    }
    fclose(f);
    return 0;
}

/* =============================================================================
 * -registro:crea — il registro da un albero pubblicato che si ha in casa
 *
 * ! PERCHE' ESISTE, VISTO CHE IL REGISTRO LO SCRIVERA' `-check`. Perche' una
 * macchina installata dal CD non ha mai parlato con un server, e il PRIMO
 * `-check` si troverebbe davanti un sistema intero senza sapere che cos'e':
 * o riscarica tutto, o indovina. Con un albero `make netinst` a portata di
 * mano — un CD, una chiavetta, una directory montata — il registro si scrive
 * subito e senza rete, e il primo controllo confronta invece di ricominciare.
 * E' anche esattamente la routine che `-check` chiamera' dopo aver scaricato
 * i tre file: qui la si scrive una volta e la si prova senza server.
 *
 * ! L'IMPRONTA NON SI RICALCOLA, E NON E' PIGRIZIA: sha256() della libc vuole
 * il buffer INTERO in memoria (lib/libc.c, una chiamata sola), e su un sistema
 * con 32 MB di RAM un file da 33 MB come cc1 non ci sta. Quindi l'impronta si
 * PRENDE dall'elenco, e la si scrive solo se il file sul disco ha la
 * dimensione che l'elenco dichiara. Se la dimensione non combacia il file c'e'
 * ma non e' quello: si scrive «-», che vuol dire «non lo so». Un registro che
 * dicesse un'impronta non verificata mentirebbe, e `-check` gli crederebbe.
 * ============================================================================= */
static void unisci(char *out, int max, const char *a, const char *b)
{
    int i = 0, j;

    for (j = 0; a[j] && i < max - 2; j++) out[i++] = a[j];
    if (i > 0 && out[i-1] != '/') out[i++] = '/';
    for (j = 0; b[j] && i < max - 1; j++) out[i++] = b[j];
    out[i] = '\0';
}

static int chiave_da_file(const char *percorso, const char *chiave,
                          char *out, int max)
{
    FILE *f = fopen(percorso, "r");
    char  riga[RIGA_MAX];

    out[0] = '\0';
    if (f == NULL) return -1;
    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *r = ripulisci(riga), *val;

        if (r[0] == '#' || r[0] == '\0') continue;
        if ((val = valore_se(r, chiave)) != NULL) {
            copia_str(out, val, max);
            break;
        }
    }
    fclose(f);
    return out[0] ? 0 : -1;
}

/* Rende 1 se il percorso ASSOLUTO esiste, e ne mette la dimensione in *dim. */
static int esiste(const char *percorso, long *dim)
{
    struct stat st;

    if (stat(percorso, &st) != 0) return 0;
    if (dim) *dim = (long)st.st_size;
    return 1;
}

/* Scorre elenco.txt e scrive nel registro le righe `file` del pacchetto `id`.
 * Rende quante ne ha scritte; conta a parte quelle che sul disco non ci sono
 * e quelle che ci sono con un'altra dimensione.
 *
 * ! UN GIRO DI ELENCO PER PACCHETTO, e non e' uno spreco: l'alternativa e'
 * tenere in memoria millecinquecento righe per raggrupparle, e su EX-OS la
 * memoria non torna indietro. Un file di centoquaranta kilobyte letto otto
 * volte e' meno di un megabyte di letture. */
static long file_del_pacchetto(FILE *out, const char *elenco, const char *id,
                               long *mancano, long *diversi)
{
    FILE *f = fopen(elenco, "r");
    char  riga[RIGA_MAX];
    long  scritti = 0;

    if (f == NULL) return -1;

    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *p, *sbyte, *impronta, *pac, *t;
        char  assoluto[PERC_MAX];
        long  dim = 0;

        if (riga[0] == '#') continue;

        /* percorso<TAB>byte<TAB>sha256<TAB>pacchetto */
        p = riga;
        t = strchr(p, '\t');       if (t == NULL) continue; *t = '\0'; sbyte    = t + 1;
        t = strchr(sbyte, '\t');   if (t == NULL) continue; *t = '\0'; impronta = t + 1;
        t = strchr(impronta, '\t');if (t == NULL) continue; *t = '\0'; pac      = t + 1;
        pac = ripulisci(pac);
        if (strcmp(pac, id) != 0) continue;

        unisci(assoluto, sizeof(assoluto), "/", p);
        if (!esiste(assoluto, &dim)) { (*mancano)++; continue; }

        if (dim != atol(sbyte)) {
            /* C'e', ma non e' quello dell'elenco. Si registra senza impronta:
             * il file appartiene al pacchetto — va tolto da `-remove` — ma
             * `-check` non deve credere di sapere com'e' fatto. */
            fprintf(out, "file     = - %s\n", p);
            (*diversi)++;
        } else {
            fprintf(out, "file     = %s %s\n", impronta, p);
        }
        scritti++;
    }
    fclose(f);
    return scritti;
}

static int comando_registro_crea(const char *albero)
{
    Pacchetto v[PACCHETTI_MAX];
    char      cat[PERC_MAX], ele[PERC_MAX], ver[PERC_MAX];
    char      versione[16], data[32], assoluto[PERC_MAX];
    FILE     *out;
    int       n, i, installati = 0, senza_prova = 0;
    long      tot = 0, tot_mancano = 0, tot_diversi = 0;

    if (!boot_si_scrive()) {
        fprintf(stderr, "netupdate: non posso scrivere %s (%s).\n",
                REG, strerror(errno));
        return 1;
    }

    unisci(ver, sizeof(ver), albero, "versione.txt");
    unisci(cat, sizeof(cat), albero, "catalogo.txt");
    unisci(ele, sizeof(ele), albero, "elenco.txt");

    if (chiave_da_file(ver, "versione", versione, sizeof(versione)) != 0) {
        fprintf(stderr, "netupdate: %s non c'e' o non dice la versione.\n", ver);
        fprintf(stderr, "           «%s» non e' un albero pubblicato: dentro\n", albero);
        fprintf(stderr, "           ci vogliono versione.txt, catalogo.txt,\n");
        fprintf(stderr, "           elenco.txt e file/ — li fa `make netinst`.\n");
        return 1;
    }
    if (chiave_da_file(ver, "data", data, sizeof(data)) != 0)
        copia_str(data, "?", sizeof(data));

    n = blocchi_leggi(cat, v, PACCHETTI_MAX);
    if (n <= 0) {
        fprintf(stderr, "netupdate: %s non c'e' o non nomina nessun pacchetto.\n", cat);
        return 1;
    }

    /* Chi c'e' davvero su QUESTA macchina: la chiave `prova` del catalogo, che
     * e' un percorso che se c'e' dice che il pacchetto c'e'. */
    for (i = 0; i < n; i++) {
        if (v[i].prova[0] == '\0') { senza_prova++; continue; }
        unisci(assoluto, sizeof(assoluto), "/", v[i].prova);
        v[i].c_e = esiste(assoluto, NULL);
        if (v[i].c_e) installati++;
    }

    printf("Registro da %s — sistema %s del %s\n\n", albero, versione, data);

    if (installati == 0) {
        printf("  Nessuno dei %d pacchetti del catalogo risulta installato qui:\n", n);
        printf("  nessuna delle loro `prova` esiste sul disco. Non scrivo niente,\n");
        printf("  perche' un registro vuoto direbbe una cosa falsa.\n");
        return 1;
    }

    /* ! SI SCRIVE ACCANTO, E SI SOSTITUISCE ALLA FINE. Scrivere sopra il
     * registro buono vuol dire che una macchina spenta a meta' scrittura si
     * risveglia con un registro TRONCATO — che e' un registro che mente, e
     * `-remove` cancella quel che ci trova scritto. Cosi' invece il caso
     * peggiore e' ritrovarsi senza registro, che si rifa' in un comando. */
    out = fopen(REG_NEW, "w");
    if (out == NULL) {
        fprintf(stderr, "netupdate: non riesco a scrivere %s (%s)\n",
                REG_NEW, strerror(errno));
        return 1;
    }

    fprintf(out, "# =============================================================\n");
    fprintf(out, "# netupdate.reg - cosa c'e' installato su questa macchina\n");
    fprintf(out, "#\n");
    fprintf(out, "# Lo scrive netupdate, e lo riscrive INTERO a ogni\n");
    fprintf(out, "# installazione, aggiornamento e rimozione.\n");
    fprintf(out, "#\n");
    fprintf(out, "# ! NON SI CORREGGE A MANO ALLA LEGGERA: `-remove` cancella\n");
    fprintf(out, "# dal disco i file scritti qui dentro.\n");
    fprintf(out, "#\n");
    fprintf(out, "#   nome     come si chiama, per chi legge\n");
    fprintf(out, "#   versione la versione del sistema da cui e' arrivato\n");
    fprintf(out, "#   data     la data dell'albero pubblicato da cui viene,\n");
    fprintf(out, "#            non quella di questa scrittura: serve a dire\n");
    fprintf(out, "#            CONTRO COSA e' stato confrontato l'ultima volta\n");
    fprintf(out, "#   vuole    un pacchetto che deve restare finche' c'e'\n");
    fprintf(out, "#            questo (piu' righe = piu' pacchetti)\n");
    fprintf(out, "#   file     un file che gli appartiene: «impronta percorso».\n");
    fprintf(out, "#            Impronta «-» = il file c'e' ma non si sa se e'\n");
    fprintf(out, "#            quello: il prossimo -check lo riguardera'.\n");
    fprintf(out, "# =============================================================\n");

    for (i = 0; i < n; i++) {
        long scritti, mancano = 0, diversi = 0;
        int  k;

        if (!v[i].c_e) continue;

        fprintf(out, "\n[%s]\n", v[i].id);
        fprintf(out, "nome     = %s\n", v[i].nome);
        if (v[i].dice[0]) fprintf(out, "dice     = %s\n", v[i].dice);
        fprintf(out, "versione = %s\n", versione);
        fprintf(out, "data     = %s\n", data);
        if (v[i].prova[0]) fprintf(out, "prova    = %s\n", v[i].prova);
        /* ! `sempre` VA NEL REGISTRO, non solo nel catalogo: e' `-remove` a
         * doverlo sapere, e -remove non tocca la rete. */
        if (v[i].sempre)   fprintf(out, "sempre   = si\n");
        for (k = 0; k < v[i].n_vuole; k++)
            fprintf(out, "vuole    = %s\n", v[i].vuole[k]);

        scritti = file_del_pacchetto(out, ele, v[i].id, &mancano, &diversi);
        if (scritti < 0) {
            fclose(out);
            remove(REG_NEW);
            fprintf(stderr, "netupdate: non riesco a leggere %s (%s)\n",
                    ele, strerror(errno));
            fprintf(stderr, "           il registro di prima non l'ho toccato.\n");
            return 1;
        }

        printf("  %-12s %6ld file", v[i].id, scritti);
        if (mancano) printf(", %ld che non c%s", mancano,
                            mancano == 1 ? "'e'" : "i sono");
        if (diversi) printf(", %ld divers%c dall'elenco", diversi,
                            diversi == 1 ? 'o' : 'i');
        if (scritti == 0) printf("   ! il catalogo lo dichiara e l'elenco non gli da' niente");
        printf("\n");

        tot += scritti;
        tot_mancano += mancano;
        tot_diversi += diversi;
    }

    if (fclose(out) != 0) {
        remove(REG_NEW);
        fprintf(stderr, "netupdate: %s non si e' chiuso bene: registro non scritto.\n",
                REG_NEW);
        return 1;
    }

    /* rename() di EX-OS non sostituisce la destinazione (EEXIST): il vecchio
     * si toglie prima. Fra le due righe c'e' l'unico istante in cui il
     * registro non c'e' — ed e' il caso buono, non quello cattivo. */
    remove(REG);
    if (rename(REG_NEW, REG) != 0) {
        fprintf(stderr, "netupdate: scritto %s ma non l'ho potuto rinominare (%s).\n",
                REG_NEW, strerror(errno));
        fprintf(stderr, "           Il registro e' quel file: rinominalo a mano in\n");
        fprintf(stderr, "           %s.\n", REG);
        return 1;
    }

    printf("\n%s scritto: %d pacchetti, %ld file.\n", REG, installati, tot);
    if (senza_prova)
        printf("  ! %d pacchetti del catalogo non hanno una `prova`: saltati.\n",
               senza_prova);
    if (tot_mancano)
        printf("  ! %ld file dell'elenco su questo disco non %s: fuori dal\n"
               "    registro, e `-check` %s proporra'.\n", tot_mancano,
               tot_mancano == 1 ? "c'e'" : "ci sono",
               tot_mancano == 1 ? "lo" : "li");
    if (tot_diversi)
        printf("  ! %ld file %s con un'altra dimensione: registrat%c senza\n"
               "    impronta, perche' non %s quell%c dell'elenco.\n", tot_diversi,
               tot_diversi == 1 ? "c'e' ma" : "ci sono ma",
               tot_diversi == 1 ? 'o' : 'i',
               tot_diversi == 1 ? "e'" : "sono",
               tot_diversi == 1 ? 'o' : 'i');
    printf("\n  Adesso:  netupdate -registro\n");
    return 0;
}


/* =============================================================================
 * -check — AGGIORNARE SENZA RESTARE A PIEDI
 *
 * La sequenza, e l'ordine e' la sostanza:
 *
 *   1. si legge la configurazione e ci si collega;
 *   2. si scaricano i TRE file del manifesto e SI VERIFICANO fra loro:
 *      versione.txt porta l'impronta degli altri due, ed e' li' per questo;
 *   3. si confronta l'elenco del server con il REGISTRO di questa macchina;
 *   4. si mostra cosa cambia e SI CHIEDE;
 *   5. si scarica ogni file ACCANTO al suo posto, e lo si verifica PRIMA di
 *      toccare quello che funziona;
 *   6. si sostituisce;
 *   7. kernel e caricatore per ULTIMI, e con la mappa dei settori rifatta;
 *   8. si riscrive il registro.
 *
 * ! CHI SI SPEGNE A META' DEVE RITROVARSI IL VECCHIO O IL NUOVO, MAI UN MEZZO
 * DEI DUE. Da qui: si scarica tutto prima di sostituire niente; ogni file si
 * verifica prima di prendere il posto dell'altro; il settore di avvio si tocca
 * per ultimo, quando il kernel nuovo e' gia' li' ed e' gia' stato verificato.
 *
 * ! E IL VECCHIO NON SI CANCELLA SUBITO, ed e' una conseguenza di come EX-OS
 * esegue i programmi: il caricatore ELF li paglia SU RICHIESTA e tiene
 * l'eseguibile APERTO (kernel/loader/elf.c). Cancellare il file di un
 * programma che sta girando — la shell, o netupdate stesso — vuol dire
 * togliergli da sotto le pagine che non ha ancora letto. Quindi il vecchio si
 * RINOMINA in «.old» e resta li' fino al riavvio; a toglierlo ci pensa il
 * `-check` successivo, che gira dopo, quando quei programmi non ci sono piu'.
 * ============================================================================= */
#define TMPDIR   "/tmp/netupdate"

/* ! IL TETTO LO METTE CHI SCARICA, NON IL SERVER — e' la stessa regola di
 * bin/scarica. Due megabyte tengono qualunque file del sistema: il piu' grosso
 * e' un font da 760 KB, il kernel ne pesa 258. NON tengono un file degli
 * strumenti (cc1 da solo e' 33 MB), e infatti quelli si rifiutano dicendolo:
 * exhttp_prendi vuole il corpo intero in un buffer di chi chiama, e una
 * macchina con 32 MB di RAM non puo' prometterne 33. */
#define BUF_MAX  (2u * 1024 * 1024)
static unsigned char g_buf[BUF_MAX];

/* ! IL SILENZIO E' UNA MODALITA', NON UN'OPZIONE DI STAMPA. `-auto` gira a
 * OGNI AVVIO, prima che qualcuno abbia chiesto niente: un messaggio a ogni
 * accensione — «la rete non e' pronta», «il server non risponde» — e' rumore
 * che dopo tre giorni si smette di leggere, e il giorno che dice qualcosa di
 * vero nessuno lo vede. Quando g_zitto e' acceso, netupdate parla SOLO se ha
 * una novita' da dire; tutto il resto lo scopre chi lancia `-check` a mano,
 * che e' il momento in cui ha senso raccontarlo. */
static int g_zitto = 0;

/* La scadenza del giro di rete, in colpi di clock(). Zero = nessuna.
 *
 * ! UN CONTROLLO ALL'AVVIO NON PUO' TENERE FERMA UNA MACCHINA. Senza scadenza,
 * un server irraggiungibile costerebbe il timeout del TCP a ogni accensione, e
 * quella e' la ragione per cui la gente spegne gli aggiornamenti automatici.
 * Cinque secondi: se non risponde in cinque secondi, se ne parla al prossimo
 * avvio. exhttp_attesa() chiama `respiro` durante le attese, e uno zero
 * annulla la richiesta. */
static long g_scadenza = 0;

static int respiro(void *dato)
{
    (void)dato;
    if (g_scadenza == 0) return 1;
    return clock() < g_scadenza;
}

/* Il registro caricato per il confronto. Un'arena e un indice invece di
 * millecinquecento malloc: su EX-OS la memoria non torna indietro. */
#define VOCI_MAX   2048
#define ARENA_MAX  (192u * 1024)
static char  g_arena[ARENA_MAX];
static int   g_arena_uso;
typedef struct { char *percorso; char *impronta; } Voce;
static Voce  g_voce[VOCI_MAX];
static int   g_nvoci;

/* Il verdetto su un file dell'elenco. */
enum { UGUALE = 0, CAMBIATO, DA_VERIFICARE, MAI_INSTALLATO, SCONOSCIUTO };

static char *arena_metti(const char *s)
{
    int   n = (int)strlen(s) + 1;
    char *p;

    if (g_arena_uso + n > (int)ARENA_MAX) return NULL;
    p = &g_arena[g_arena_uso];
    memcpy(p, s, (size_t)n);
    g_arena_uso += n;
    return p;
}

static int registro_carica(void)
{
    FILE *f = fopen(REG, "r");
    char  riga[RIGA_MAX];

    g_nvoci = 0;
    g_arena_uso = 0;
    if (f == NULL) return -1;

    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *r = ripulisci(riga), *val, *sp;

        if ((val = valore_se(r, "file")) == NULL) continue;

        sp = val;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        if (*sp == '\0') continue;
        *sp++ = '\0';
        while (*sp == ' ' || *sp == '\t') sp++;

        if (g_nvoci >= VOCI_MAX) {
            printf("  ! piu' di %d file nel registro: il confronto si ferma qui\n",
                   VOCI_MAX);
            break;
        }
        g_voce[g_nvoci].impronta = arena_metti(val);
        g_voce[g_nvoci].percorso = arena_metti(sp);
        if (!g_voce[g_nvoci].impronta || !g_voce[g_nvoci].percorso) {
            printf("  ! il registro non ci sta in memoria: confronto parziale\n");
            break;
        }
        g_nvoci++;
    }
    fclose(f);
    return g_nvoci;
}

static const char *registro_impronta(const char *percorso)
{
    int i;

    for (i = 0; i < g_nvoci; i++)
        if (strcmp(g_voce[i].percorso, percorso) == 0) return g_voce[i].impronta;
    return NULL;
}

/* -----------------------------------------------------------------------------
 * La rete, in due righe: un URL e un buffer.
 * --------------------------------------------------------------------------- */
static void url_componi(char *out, int max, const Config *c, const char *coda)
{
    int i = 0, j;
    const char *schema = "http://";

    for (j = 0; schema[j] && i < max - 1; j++) out[i++] = schema[j];
    for (j = 0; c->url[j] && i < max - 1; j++) out[i++] = c->url[j];
    if (coda[0] != '/' && i < max - 1) out[i++] = '/';
    for (j = 0; coda[j] && i < max - 1; j++) out[i++] = coda[j];
    out[i] = '\0';
}

/* Scarica in g_buf. Rende i byte, o -1 dicendo perche'. */
static long prendi(const char *url)
{
    ExHttpEsito e;

    if (!exhttp_prendi(url, g_buf, BUF_MAX, &e)) {
        if (!g_zitto) printf("  ! %s\n", e.errore[0] ? e.errore : "non riuscito");
        return -1;
    }
    if (e.codice != 200) {
        if (!g_zitto) printf("  ! %s: il server risponde %d\n", url, e.codice);
        return -1;
    }
    if (e.troncata) {
        if (!g_zitto) printf("  ! %s: piu' grande di %u byte, non lo posso tenere\n",
                             url, BUF_MAX);
        return -1;
    }
    return (long)e.byte;
}

static int scrivi_file(const char *dove, const unsigned char *dati, long n)
{
    int  fd = open(dove, O_WRONLY | O_CREAT | O_TRUNC);
    long fatti = 0;

    if (fd < 0) return -1;
    while (fatti < n) {
        int k = (int)write(fd, dati + fatti, (unsigned int)(n - fatti));

        if (k <= 0) break;
        fatti += k;
    }
    close(fd);
    return (fatti == n) ? 0 : -1;
}

/* L'impronta di cio' che si e' appena scaricato. Qui sha256() basta e avanza:
 * il buffer c'e' gia' tutto, per costruzione. */
static int impronta_e(const unsigned char *dati, long n, const char *attesa)
{
    char esa[65];

    sha256_esa(dati, (size_t)n, esa);
    return strcasecmp(esa, attesa) == 0;
}

/* Scarica <coda> dal server, verifica l'impronta se `attesa` non e' NULL, e lo
 * scrive in `dove`. Rende 0 se e' andata. */
static int prendi_verifica_scrivi(const Config *c, const char *coda,
                                  const char *attesa, const char *dove)
{
    char url[URL_MAX + 128];
    long n;

    url_componi(url, sizeof(url), c, coda);
    n = prendi(url);
    if (n < 0) return -1;

    if (attesa != NULL && !impronta_e(g_buf, n, attesa)) {
        if (!g_zitto) {
            printf("  ! %s non ha l'impronta che versione.txt dichiara.\n", coda);
            printf("    Il server si contraddice: non tocco niente.\n");
        }
        return -1;
    }
    if (scrivi_file(dove, g_buf, n) != 0) {
        if (!g_zitto)
            printf("  ! non riesco a scrivere %s (%s)\n", dove, strerror(errno));
        return -1;
    }
    return 0;
}

/* Le directory di un percorso, create una per una. Un file nuovo puo' stare
 * in una directory che questa macchina non ha mai avuto. */
static void crea_strada(const char *percorso)
{
    char b[PERC_MAX];
    int  i;

    for (i = 0; percorso[i] && i < PERC_MAX - 1; i++) {
        b[i] = percorso[i];
        if (percorso[i] == '/' && i > 0) {
            b[i] = '\0';
            mkdir(b, 0755);        /* EEXIST va benissimo */
            b[i] = '/';
        }
    }
}

/* Spezza una riga di elenco.txt. Rende 0 se non e' una riga buona. */
static int riga_elenco(char *riga, char **perc, long *byte,
                       char **impronta, char **pac)
{
    char *t;

    if (riga[0] == '#' || riga[0] == '\n' || riga[0] == '\0') return 0;
    *perc = riga;
    t = strchr(riga, '\t');      if (!t) return 0; *t = '\0'; *byte = atol(t + 1);
    t = strchr(t + 1, '\t');     if (!t) return 0; *impronta = t + 1;
    t = strchr(t + 1, '\t');     if (!t) return 0; *t = '\0'; *pac = ripulisci(t + 1);
    return (*perc)[0] != '\0';
}

/* -----------------------------------------------------------------------------
 * e_configurazione — i file che appartengono a QUESTA macchina
 *
 * ! UN AGGIORNAMENTO CHE SOVRASCRIVE LA CONFIGURAZIONE E' UN AGGIORNAMENTO CHE
 * ROMPE LA MACCHINA. /boot/kernel.cfg dice quali driver caricare, che tastiera
 * ha chi ci scrive, cosa montare all'avvio: e' il risultato di
 * un'installazione e di `hwconfig`, non un file di sistema uguale per tutti.
 * Il server ne pubblica una copia — quella del CD, che serve a chi installa —
 * e senza questa guardia `-check` la copiava sopra, cancellando la tastiera
 * italiana, i montaggi e i driver di quella macchina.
 *
 * ! E NON SI FONDE QUI. `install` sa fonderlo (aggiorna_kernel_cfg in
 * bin/install/install.c, con scritto perche' «lo lascio com'e'» non bastava):
 * quella logica esiste, sta li', e riscriverla qui vorrebbe dire due fusioni
 * che divergono. Quindi netupdate non tocca, e DICE che non tocca — un file
 * saltato in silenzio e' un file che qualcuno credera' aggiornato.
 * --------------------------------------------------------------------------- */
static int e_configurazione(const char *percorso)
{
    static const char *SUE[] = {
        "boot/kernel.cfg",      /* driver, tastiera, montaggi: di questa macchina */
        "boot/telnetd.cfg",     /* chi puo' entrare da telnet, e come             */
        0
    };
    int i;

    for (i = 0; SUE[i]; i++)
        if (strcmp(percorso, SUE[i]) == 0) return 1;
    return 0;
}

/* I due file dell'avvio si riconoscono dal PERCORSO e non dal pacchetto, ed e'
 * voluto: chi deve saperlo e' il codice della mappa dei settori, e quello
 * conosce /boot/kernel.bin e /boot/stage2.bin per nome — bootverify() prende
 * proprio quei nomi. Legare la cosa all'etichetta del catalogo vorrebbe dire
 * che rinominare un pacchetto cambia il modo in cui si aggiorna un kernel. */
static int e_avvio(const char *percorso)
{
    return strcmp(percorso, "boot/kernel.bin") == 0 ||
           strcmp(percorso, "boot/stage2.bin") == 0;
}

static int verdetto(const char *percorso, const char *impronta_server)
{
    const char *mia = registro_impronta(percorso);
    char        assoluto[PERC_MAX];

    if (mia == NULL) {
        unisci(assoluto, sizeof(assoluto), "/", percorso);
        return esiste(assoluto, NULL) ? SCONOSCIUTO : MAI_INSTALLATO;
    }
    if (mia[0] == '-' && mia[1] == '\0') return DA_VERIFICARE;
    return strcasecmp(mia, impronta_server) == 0 ? UGUALE : CAMBIATO;
}

static int chiedi_si(const char *domanda, int pred)
{
    char riga[RIGA_MAX];

    printf("\n  %s [si/no, Invio = %s]: ", domanda, pred ? "si" : "no");
    fflush(stdout);
    if (!leggi_riga(riga, sizeof(riga)) || riga[0] == '\0') return pred;
    return (riga[0] == 's' || riga[0] == 'S');
}

/* -----------------------------------------------------------------------------
 * sostituisci — il nuovo prende il posto del vecchio, e il vecchio non muore
 *
 * ! IL VECCHIO SI RINOMINA, NON SI CANCELLA, e la ragione e' nel caricatore:
 * i programmi si paginano SU RICHIESTA dal loro file, che resta aperto
 * (kernel/loader/elf.c). Cancellare /bin/sh mentre la shell gira vuol dire
 * togliere le pagine che quella shell non ha ancora letto. Il «.old» resta li'
 * fino al riavvio, e lo toglie il -check successivo.
 *
 * ! E RENAME NON SPOSTA I DATI (dalla 0.161, vedi lib/libc.c): e' il motivo per
 * cui la mappa dei settori verificata un attimo prima vale ancora dopo lo
 * scambio. Con una rename che copiasse, la verifica non varrebbe piu' niente.
 * --------------------------------------------------------------------------- */
static int sostituisci(const char *dest, const char *temp)
{
    char vecchio[PERC_MAX];
    int  c_era;

    snprintf(vecchio, sizeof(vecchio), "%s.old", dest);
    remove(vecchio);                       /* un .old di un giro precedente */
    c_era = (rename(dest, vecchio) == 0);  /* se dest non c'era, non importa */

    if (rename(temp, dest) != 0) {
        printf("  ! %s: non riesco a rinominare (%s)\n", dest, strerror(errno));
        if (c_era) rename(vecchio, dest);  /* rimettiamo com'era */
        return -1;
    }
    return 0;
}

/* =============================================================================
 * La fase dell'avvio, che e' l'unica che puo' lasciare a piedi una macchina
 *
 * ! L'ORDINE E' OBBLIGATORIO, e non e' prudenza: e' il patto LILO. Su ext2 il
 * kernel non e' contiguo e il settore di avvio ne contiene la MAPPA DEI
 * SETTORI. Quindi: prima si verifica che i file nuovi siano mappabili — mentre
 * i vecchi sono ancora al loro posto e la macchina parte ancora — poi si
 * scambiano i nomi, e SOLO ALLA FINE si riscrive il settore di avvio.
 * ============================================================================= */
static int fase_avvio(int kernel, int stage2)
{
    BootInstallInfo info;
    int r;

    printf("\nL'avvio\n");

    r = bootverify("/", stage2 ? "stage2.new" : NULL,
                        kernel ? "kernel.new" : NULL, &info);
    if (r != 0) {
        printf("  ! i file nuovi non sono mappabili (errore %d).\n", r);
        printf("    NON ho toccato niente: la macchina parte ancora con\n");
        printf("    quello di prima. I file scaricati restano come .new.\n");
        if (r == -29)
            printf("    -29 vuol dire frammentato: serve un volume meno pieno.\n");
        return -1;
    }
    printf("  = verifica: %u settori in %u intervall%s, si puo' sostituire\n",
           info.k_cnt, info.k_next, info.k_next == 1 ? "o" : "i");

    if (stage2 && sostituisci("/boot/stage2.bin", "/boot/stage2.new") != 0) return -1;
    if (kernel && sostituisci("/boot/kernel.bin", "/boot/kernel.new") != 0) return -1;

    r = bootinstall("/", &info);
    if (r != 0) {
        printf("  ! il settore di avvio NON e' stato riscritto (errore %d).\n", r);
        printf("    E QUESTO E' IL CASO BRUTTO: il kernel nuovo e' al suo\n");
        printf("    posto ma la mappa vecchia punta ai settori di quello\n");
        printf("    vecchio. Per tornare indietro, da un altro supporto:\n");
        printf("      rinomina /boot/kernel.bin.old in /boot/kernel.bin\n");
        printf("    oppure rilancia `install <punto>` che la mappa la rifa'.\n");
        return -1;
    }
    printf("  = settore di avvio riscritto: kernel a LBA %u, %u settori\n",
           info.k_lba, info.k_cnt);
    printf("  = il kernel di prima resta in /boot/kernel.bin.old\n");
    return 0;
}

/* -----------------------------------------------------------------------------
 * manifesto — la configurazione, la rete, e i tre file che dicono cosa c'e'
 *
 * Riempie ver/cat/ele con i percorsi LOCALI dei tre file scaricati, e
 * versione/data con quel che dice il server. Rende 0 se e' andata.
 *
 * ! LE DUE IMPRONTE DENTRO versione.txt SONO IL SUO MESTIERE. Un elenco
 * arrivato a meta' — o servito da un proxy che si e' inventato qualcosa —
 * porterebbe a confrontare impronte con niente: o si riscarica tutto, o non si
 * riscarica niente, e in tutt'e due i casi senza un errore. Se non tornano, ci
 * si ferma qui, prima di guardare un solo file.
 * --------------------------------------------------------------------------- */
static int manifesto(Config *c, char *ver, char *cat, char *ele,
                     char *versione, char *data)
{
    char h_cat[72], h_ele[72];

    if (!config_leggi(c)) {
        if (!g_zitto) {
            printf("netupdate: non so da dove aggiornare (%s non c'e').\n\n", CNF);
            printf("  Prima:  netupdate -set\n");
        }
        return 1;
    }
    if (strcasecmp(c->tipo, "HTTP") != 0) {
        if (!g_zitto) {
            printf("netupdate: la configurazione dice %s, e oggi so fare solo HTTP.\n\n",
                   c->tipo);
            printf("  FTP non c'e' ancora; HTTPS e FTPS vogliono la TLS, che ha un\n");
            printf("  limite noto. Si cambia con `netupdate -set`.\n");
        }
        return 1;
    }
    if (!boot_si_scrive()) {
        if (!g_zitto) {
            printf("netupdate: non posso scrivere in /boot (%s).\n\n", strerror(errno));
            printf("  Un sistema che gira da CD non si aggiorna: /boot e' il CD.\n");
        }
        return 1;
    }

    mkdir("/tmp", 0755);
    mkdir(TMPDIR, 0755);
    snprintf(ver, PERC_MAX, "%s/versione.txt", TMPDIR);
    snprintf(cat, PERC_MAX, "%s/catalogo.txt", TMPDIR);
    snprintf(ele, PERC_MAX, "%s/elenco.txt",   TMPDIR);

    if (!g_zitto) printf("Guardo http://%s\n\n", c->url);

    if (prendi_verifica_scrivi(c, "versione.txt", NULL, ver) != 0) {
        if (!g_zitto) {
            printf("\n  Non ho potuto leggere versione.txt. Se l'indirizzo e'\n");
            printf("  giusto e la rete c'e', controlla che il server pubblichi la\n");
            printf("  RADICE dell'albero fatto da `make netinst`.\n");
        }
        return 1;
    }
    if (chiave_da_file(ver, "versione", versione, 16) != 0) {
        if (!g_zitto) {
            printf("  ! versione.txt non dice nessuna versione: non e' un albero\n");
            printf("    pubblicato da `make netinst`.\n");
        }
        return 1;
    }
    if (chiave_da_file(ver, "data", data, 40) != 0)
        copia_str(data, "?", 40);

    if (chiave_da_file(ver, "catalogo", h_cat, sizeof(h_cat)) != 0 ||
        chiave_da_file(ver, "elenco",   h_ele, sizeof(h_ele)) != 0) {
        if (!g_zitto) {
            printf("  ! versione.txt non porta le impronte di catalogo.txt e\n");
            printf("    elenco.txt: senza, non posso sapere se quel che scarico\n");
            printf("    e' arrivato intero. Mi fermo.\n");
        }
        return 1;
    }
    if (prendi_verifica_scrivi(c, "catalogo.txt", h_cat, cat) != 0) return 1;
    if (prendi_verifica_scrivi(c, "elenco.txt",   h_ele, ele) != 0) return 1;

    if (!g_zitto) printf("  server: sistema %s del %s\n", versione, data);
    return 0;
}

/* -----------------------------------------------------------------------------
 * scarica_e_metti — un file solo: dalla rete al suo posto
 *
 * Rende  0 = installato, 1 = saltato (troppo grosso), 2 = messo da parte
 * perche' e' dell'avvio, -1 = non riuscito. Lo usano `-check` e `-install`:
 * scaricare, verificare e sostituire e' la stessa cosa in tutt'e due, e
 * scriverla due volte vorrebbe dire due ordini di operazioni che col tempo
 * divergono — cioe' due modi diversi di rompere una macchina.
 * --------------------------------------------------------------------------- */
static int scarica_e_metti(Config *c, const char *p, long byte,
                           const char *impronta, int *kernel, int *stage2)
{
    char dest[PERC_MAX], temp[PERC_MAX], coda[PERC_MAX + 8];
    char url[URL_MAX + PERC_MAX + 16];
    long n;

    /* ! UN FILE PIU' GRANDE DEL BUFFER NON SI SCARICA, E SI DICE. exhttp vuole
     * il corpo intero in memoria: e' il tetto scritto in cima. Il sistema ci
     * sta tutto; gli strumenti no, e chi aggiorna deve saperlo adesso e non
     * scoprirlo con un compilatore a meta'. */
    if (byte > (long)BUF_MAX) {
        printf("  - %s: %ld byte, piu' del tetto di %u. SALTATO.\n", p, byte, BUF_MAX);
        return 1;
    }

    snprintf(coda, sizeof(coda), "file/%s", p);
    url_componi(url, sizeof(url), c, coda);
    n = prendi(url);
    if (n < 0) return -1;

    if (!impronta_e(g_buf, n, impronta)) {
        printf("  ! %s: l'impronta non torna, NON lo installo\n", p);
        return -1;
    }

    unisci(dest, sizeof(dest), "/", p);

    if (e_avvio(p)) {
        /* I due dell'avvio si mettono da parte col nome che bootverify si
         * aspetta, e si sostituiscono per ULTIMI. */
        snprintf(temp, sizeof(temp), "/boot/%s.new",
                 strcmp(p, "boot/kernel.bin") == 0 ? "kernel" : "stage2");
        if (scrivi_file(temp, g_buf, n) != 0) {
            printf("  ! %s: non riesco a scriverlo (%s)\n", temp, strerror(errno));
            return -1;
        }
        if (strcmp(p, "boot/kernel.bin") == 0) *kernel = 1;
        else                                   *stage2 = 1;
        printf("  = %s scaricato e verificato (per ultimo)\n", p);
        return 2;
    }

    crea_strada(dest);
    snprintf(temp, sizeof(temp), "%s.new", dest);
    if (scrivi_file(temp, g_buf, n) != 0) {
        printf("  ! %s: non riesco a scriverlo (%s)\n", temp, strerror(errno));
        return -1;
    }
    if (sostituisci(dest, temp) != 0) return -1;
    printf("  + %s (%ld byte)\n", p, n);
    return 0;
}

/* Toglie i «.old» del giro precedente. Si fa ADESSO e non allora: dopo un
 * aggiornamento i programmi vecchi possono essere ancora in esecuzione, e il
 * loro file serve; a questo giro sono passati un riavvio e un accesso. */
static long pulisci_vecchi(void)
{
    char b[PERC_MAX];
    long tolti = 0;
    int  i;

    for (i = 0; i < g_nvoci; i++) {
        unisci(b, sizeof(b), "/", g_voce[i].percorso);
        if ((int)strlen(b) + 4 >= PERC_MAX) continue;
        strcat(b, ".old");
        if (remove(b) == 0) tolti++;
    }
    return tolti;
}

/* =============================================================================
 * guarda — il confronto, e nient'altro
 *
 * ! GUARDARE E AGIRE SONO DUE COSE, e tenerle separate non e' eleganza: serve
 * a `-check:guarda` (dimmi cosa cambierebbe e non toccare niente), serve a
 * `-auto` (che gira a ogni avvio e non puo' chiedere niente a nessuno), e
 * servira' a chiunque debba dire COSA FARA' prima di farlo. Una funzione sola
 * che guarda e agisce insieme obbliga ogni chiamante nuovo a copiarne meta'.
 *
 * Riempie `k` coi conti, e i tre percorsi col manifesto scaricato. Rende 0 se
 * ha potuto guardare davvero.
 * ============================================================================= */
typedef struct {
    long uguali, cambiati, verif, nuovi, scon, config;
    long byte_cambiati, byte_nuovi;
} Conti;

static int guarda(Config *c, char *ver, char *cat, char *ele,
                  char *versione, char *data, Conti *k)
{
    char  riga[RIGA_MAX];
    FILE *f;
    int   mostrati = 0;

    memset(k, 0, sizeof(*k));

    if (manifesto(c, ver, cat, ele, versione, data) != 0) return 1;

    /* --- il registro di questa macchina ----------------------------------- */
    {
        Pacchetto v[PACCHETTI_MAX];
        int       n = blocchi_leggi(REG, v, PACCHETTI_MAX);

        if (n < 0) {
            /* ! IL REGISTRO NON SI SCRIVE DI NASCOSTO. Quando parla, `-check`
             * lo crea e lo dice; quando tace — cioe' all'avvio — non tocca
             * niente e lascia decidere a chi c'e'. Un file nuovo in /boot
             * comparso durante un'accensione e' esattamente il genere di cosa
             * che nessuno collega piu' a niente. */
            if (g_zitto) return 1;
            printf("\nQui il registro non c'e' ancora: lo scrivo adesso, dal\n");
            printf("manifesto appena scaricato. E' quel che serve per sapere\n");
            printf("cosa confrontare — e da adesso in poi c'e'.\n\n");
            if (comando_registro_crea(TMPDIR) != 0) return 1;
            printf("\n");
        } else if (n > 0 && !g_zitto) {
            printf("  qui:    sistema %s\n", v[0].versione[0] ? v[0].versione : "?");
        }
    }
    if (registro_carica() < 0) {
        if (!g_zitto)
            printf("netupdate: il registro non si legge. Rifallo con -registro:crea.\n");
        return 1;
    }

    /* --- il confronto, file per file -------------------------------------- */
    f = fopen(ele, "r");
    if (f == NULL) {
        if (!g_zitto) printf("netupdate: %s sparito\n", ele);
        return 1;
    }

    if (!g_zitto) printf("\nDa aggiornare:\n");
    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *p, *impronta, *pac;
        long  byte;
        int   v;

        if (!riga_elenco(riga, &p, &byte, &impronta, &pac)) continue;
        v = verdetto(p, impronta);

        if (v == UGUALE)         { k->uguali++; continue; }
        if (v == MAI_INSTALLATO) { k->nuovi++; k->byte_nuovi += byte; continue; }
        if (e_configurazione(p)) { k->config++; continue; }

        if (v == CAMBIATO)      k->cambiati++;
        if (v == DA_VERIFICARE) k->verif++;
        if (v == SCONOSCIUTO)   k->scon++;
        k->byte_cambiati += byte;

        if (g_zitto) continue;
        if (mostrati < 12) {
            printf("    %s%s (%ld byte)\n", p,
                   v == DA_VERIFICARE ? " [da verificare]" :
                   v == SCONOSCIUTO   ? " [il registro non lo conosce]" : "",
                   byte);
            mostrati++;
        } else if (mostrati == 12) { printf("    ...\n"); mostrati++; }
    }
    fclose(f);
    if (g_zitto) return 0;

    if (mostrati == 0) printf("    niente\n");

    printf("\n  %ld uguali, %ld da aggiornare (%ld KB da scaricare)",
           k->uguali, k->cambiati + k->verif + k->scon,
           (k->byte_cambiati + 1023) / 1024);
    if (k->verif) printf(",\n  di cui %ld da verificare (impronta sconosciuta)", k->verif);
    if (k->scon)  printf(",\n  e %ld che il registro non conosce", k->scon);
    printf(".\n");
    if (k->nuovi)
        printf("  E %ld file che qui non ci sono MAI STATI (%ld KB).\n",
               k->nuovi, (k->byte_nuovi + 1023) / 1024);
    if (k->config) {
        printf("  E %ld file di CONFIGURAZIONE che il server ha divers%c e che\n",
               k->config, k->config == 1 ? 'o' : 'i');
        printf("  NON tocco: %s di questa macchina (driver, tastiera,\n",
               k->config == 1 ? "e'" : "sono");
        printf("  montaggi). La fusione con quella nuova la sa fare `install`.\n");
    }
    return 0;
}

static int comando_check(void)
{
    Config c;
    Conti  k;
    char   ver[PERC_MAX], cat[PERC_MAX], ele[PERC_MAX];
    char   versione[16], data[40];
    char   riga[RIGA_MAX];
    FILE  *f;
    long   fatti = 0, falliti = 0, saltati = 0;
    int    kernel_nuovo = 0, stage2_nuovo = 0;
    int    fai_cambiati, fai_nuovi;

    if (guarda(&c, ver, cat, ele, versione, data, &k) != 0) return 1;

    if (k.cambiati + k.verif + k.scon + k.nuovi == 0) {
        printf("\nNiente da fare");
        if (k.config) printf(" (a parte la configurazione, che e' tua)");
        printf(".\n");
        return 0;
    }

    /* ! «MANCA» E «E' DIVERSO» NON SONO LA STESSA COSA, e si chiedono a parte.
     * Un file che non c'e' puo' essere un componente che chi ha installato NON
     * HA VOLUTO — install chiede quali directory copiare — e riportarlo dentro
     * con la scusa dell'aggiornamento vuol dire riempire una macchina di roba
     * a cui il suo padrone aveva gia' detto di no. */
    fai_cambiati = (k.cambiati + k.verif + k.scon > 0)
        ? chiedi_si("Aggiorno i file cambiati?", 1) : 0;
    fai_nuovi = 0;
    if (k.nuovi > 0) {
        printf("\n  I %ld file mai installati possono essere componenti che\n", k.nuovi);
        printf("  chi ha installato non ha voluto (doc, applicazioni grafiche,\n");
        printf("  font). Aggiornare non vuol dire aggiungerli.\n");
        fai_nuovi = chiedi_si("Installo anche quelli?", 0);
    }
    if (!fai_cambiati && !fai_nuovi) {
        printf("\nNon ho toccato niente.\n");
        return 0;
    }

    /* --- 4. i .old del giro precedente ------------------------------------ */
    {
        long tolti = pulisci_vecchi();

        if (tolti > 0) printf("\n  tolti %ld file .old del giro precedente\n", tolti);
    }

    /* --- 5. si scarica, si verifica, si sostituisce ------------------------ */
    printf("\nScarico\n");
    f = fopen(ele, "r");
    if (f == NULL) { printf("netupdate: %s sparito\n", ele); return 1; }

    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *p, *impronta, *pac;
        long  byte;
        int   v;

        if (!riga_elenco(riga, &p, &byte, &impronta, &pac)) continue;
        v = verdetto(p, impronta);

        if (v == UGUALE) continue;
        if (e_configurazione(p)) continue;      /* e' di questa macchina */
        if (v == MAI_INSTALLATO && !fai_nuovi) continue;
        if (v != MAI_INSTALLATO && !fai_cambiati) continue;

        switch (scarica_e_metti(&c, p, byte, impronta, &kernel_nuovo, &stage2_nuovo)) {
        case 0:  fatti++;   break;
        case 1:  saltati++; break;
        case 2:             break;    /* messo da parte: e' dell'avvio */
        default: falliti++; break;
        }
    }
    fclose(f);

    /* --- 6. l'avvio, per ultimo ------------------------------------------- */
    if (kernel_nuovo || stage2_nuovo) {
        if (fase_avvio(kernel_nuovo, stage2_nuovo) == 0) fatti += kernel_nuovo + stage2_nuovo;
        else falliti += kernel_nuovo + stage2_nuovo;
    }

    /* --- 7. il registro dice la verita' di adesso ------------------------- */
    printf("\n%ld file aggiornati", fatti);
    if (falliti) printf(", %ld non riusciti", falliti);
    if (saltati) printf(", %ld saltati perche' troppo grossi", saltati);
    printf(".\n");

    if (fatti > 0) {
        printf("\nRiscrivo il registro, che adesso deve dire un'altra cosa.\n\n");
        comando_registro_crea(TMPDIR);
    }

    if (kernel_nuovo || stage2_nuovo)
        printf("\n! IL KERNEL E' CAMBIATO: riavvia. Quello di prima e' in\n"
               "  /boot/kernel.bin.old, e il settore di avvio punta al nuovo.\n");
    else if (fatti > 0)
        printf("\n! I programmi gia' in esecuzione stanno ancora usando i file\n"
               "  di prima, che sono li' col nome .old. Riavvia quando puoi:\n"
               "  il prossimo -check li togliera'.\n");
    return 0;
}



/* -----------------------------------------------------------------------------
 * -check:guarda — dimmi cosa cambierebbe, e non toccare niente
 *
 * ! ESISTE PERCHE' «GUARDARE» E' UNA DOMANDA LEGITTIMA. Chi amministra una
 * macchina vuole poter sapere cosa arriverebbe prima di decidere quando farlo
 * arrivare — magari non adesso, magari non da questa console. Con il solo
 * `-check` l'unico modo di saperlo e' arrivare alla domanda e rispondere no,
 * che funziona ma obbliga a fidarsi di aver risposto giusto.
 * --------------------------------------------------------------------------- */
static int comando_check_guarda(void)
{
    Config c;
    Conti  k;
    char   ver[PERC_MAX], cat[PERC_MAX], ele[PERC_MAX];
    char   versione[16], data[40];

    if (guarda(&c, ver, cat, ele, versione, data, &k) != 0) return 1;

    printf("\n");
    if (k.cambiati + k.verif + k.scon + k.nuovi == 0)
        printf("Niente da fare, e non ho toccato niente comunque.\n");
    else
        printf("Non ho toccato niente: per farlo, `netupdate -check`.\n");
    return 0;
}

/* =============================================================================
 * -auto — l'occhiata all'avvio, quella che nessuno ha chiesto
 *
 * La esegue /boot/avvio.sh a ogni accensione, subito dopo la rete. Fa qualcosa
 * SOLO se il file di configurazione dice `automatico = si`.
 *
 * ! TRE REGOLE, E SONO TUTTE E TRE «NON DISTURBARE».
 *
 *   TACE SE NON HA NOVITA'. Un programma che a ogni accensione dice «tutto a
 *   posto» insegna a non leggere quella riga, e il giorno che ne scrive
 *   un'altra non la vede nessuno.
 *
 *   TACE SE NON PUO' GUARDARE. Nessuna configurazione, rete spenta, server
 *   irraggiungibile, /boot in sola lettura: sono tutte condizioni NORMALI a
 *   un avvio, e nessuna e' un guaio da annunciare. Chi vuole la diagnosi
 *   lancia `-check` a mano, ed e' li' che netupdate racconta tutto.
 *
 *   NON TIENE FERMA LA MACCHINA. Cinque secondi di scadenza sul giro di rete:
 *   un server che non risponde non deve costare il timeout del TCP a ogni
 *   accensione. E' esattamente il motivo per cui la gente spegne gli
 *   aggiornamenti automatici.
 *
 * ! E NON INSTALLA NIENTE DA SOLO. Dice che c'e' qualcosa e come guardarlo.
 * Scaricare e sostituire file su una macchina il cui padrone non ha chiesto
 * niente — e magari non e' nemmeno davanti — e' una decisione che questo
 * programma non ha titolo di prendere.
 * ============================================================================= */
static int comando_auto(void)
{
    Config c;
    Conti  k;
    char   ver[PERC_MAX], cat[PERC_MAX], ele[PERC_MAX];
    char   versione[16], data[40];
    long   da_fare;

    if (!config_leggi(&c) || !c.automatico) return 0;   /* zitto, e basta */

    g_zitto   = 1;
    g_scadenza = clock() + 5 * CLOCKS_PER_SEC;
    exhttp_attesa(respiro, NULL);

    if (guarda(&c, ver, cat, ele, versione, data, &k) != 0) return 0;

    da_fare = k.cambiati + k.verif + k.scon;
    if (da_fare == 0 && k.nuovi == 0) return 0;

    /* L'unica riga che questo comando ha il diritto di stampare. */
    printf("netupdate: sul server c'e' qualcosa di nuovo");
    if (da_fare) printf(" (%ld file da aggiornare", da_fare);
    if (da_fare && k.nuovi) printf(", %ld mai installati)", k.nuovi);
    else if (da_fare)       printf(")");
    else                    printf(" (%ld file mai installati)", k.nuovi);
    printf(". Guarda con `netupdate -check`.\n");
    return 0;
}


/* =============================================================================
 * gzip E tar — DUE LETTORI PICCOLI SOPRA UNA COSA CHE GIA' FUNZIONA
 *
 * Un'applicazione e' una directory, e scaricarne i file uno per uno vuol dire
 * un giro di rete per ognuno. Con un archivio se ne fa uno solo.
 *
 * ! E IL PEZZO DIFFICILE C'ERA GIA'. lib/eximg/inflate.c fa DEFLATE (RFC 1951)
 * ed e' in uso da mesi: lo usano PNG, GIF e i font. gzip e' un'intestazione di
 * dieci byte, un flusso DEFLATE e otto byte in coda (CRC32 e ISIZE); tar e'
 * un'intestazione di 512 byte per file, scritta in ottale ASCII. Quindi qui
 * non si e' portato dentro zlib: si sono scritti due lettori piccoli sopra una
 * cosa che gia' funzionava, e nessun sorgente esterno e' entrato nel sistema.
 *
 * ! STANNO IN QUESTO PROGRAMMA E NON IN UNA LIBRERIA, per adesso: l'unico che
 * li usa e' netupdate. Il giorno che serviranno a un secondo — un comando
 * `tar` per aprire i sorgenti di qualcun altro e' il candidato ovvio — si
 * spostano, e quello e' il momento giusto per farlo: una libreria con un solo
 * utente e' una libreria di cui non si conosce ancora la forma.
 *
 * ! LA MEMORIA E' IL LIMITE, ED E' DICHIARATA PRIMA. inflate() vuole il buffer
 * d'uscita INTERO dal chiamante e non fa streaming (lib/eximg/inflate.h, ed e'
 * voluto: su EX-OS free() non restituisce niente al sistema). Quindi un
 * archivio si apre TUTTO IN MEMORIA, e il catalogo dice quanto occupa aperto
 * — `arcsrotolato` — proprio perche' si possa decidere PRIMA di scaricarlo.
 * ============================================================================= */
/* CRC32 di gzip (polinomio 0xEDB88320), a bit e senza tabella: la tabella
 * sarebbe un kilobyte per far risparmiare qualche decimo di secondo su un
 * archivio che si scarica in molto di piu'. */
static unsigned long crc32_gz(const unsigned char *d, long n)
{
    unsigned long c = 0xFFFFFFFFul;
    long i;
    int  k;

    for (i = 0; i < n; i++) {
        c ^= d[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320ul & (unsigned long)(-(long)(c & 1)));
    }
    return c ^ 0xFFFFFFFFul;
}

static unsigned long quattro_byte(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* Apre un gzip. Alloca il buffer d'uscita e lo mette in *fuori: chi chiama non
 * lo libera, perche' questo programma finisce subito dopo — e su EX-OS free()
 * non restituirebbe comunque niente al sistema.
 *
 * Rende 0, oppure -1 dicendo perche'. */
static int gzip_apri(const unsigned char *d, long n,
                     unsigned char **fuori, long *quanti)
{
    long          i = 10;
    unsigned long isize, crc_letto, crc_fatto;
    unsigned int  prodotti = 0;
    unsigned char flg;

    if (n < 20 || d[0] != 0x1f || d[1] != 0x8b) {
        printf("  ! non e' un gzip (i primi due byte non tornano)\n");
        return -1;
    }
    if (d[2] != 8) {
        printf("  ! gzip con metodo %d: so leggere solo DEFLATE\n", d[2]);
        return -1;
    }
    flg = d[3];

    if (flg & 0x04) {                       /* FEXTRA */
        if (i + 2 > n) return -1;
        i += 2 + (long)d[i] + ((long)d[i+1] << 8);
    }
    if (flg & 0x08) while (i < n && d[i++] != 0) { }    /* FNAME    */
    if (flg & 0x10) while (i < n && d[i++] != 0) { }    /* FCOMMENT */
    if (flg & 0x02) i += 2;                             /* FHCRC    */
    if (i >= n - 8) {
        printf("  ! gzip troncato: l'intestazione si mangia tutto\n");
        return -1;
    }

    /* ! LA MISURA STA IN CODA, ED E' L'UNICA CHE C'E'. Gli ultimi quattro byte
     * sono ISIZE, la dimensione da aperto: senza, non si saprebbe quanto
     * allocare, e inflate() non sa dirlo prima di provarci. */
    isize     = quattro_byte(d + n - 4);
    crc_letto = quattro_byte(d + n - 8);

    if (isize == 0 || isize > 64ul * 1024 * 1024) {
        printf("  ! l'archivio dice di occupare %lu byte aperto: non ci credo\n",
               isize);
        return -1;
    }

    *fuori = (unsigned char *)malloc((size_t)isize);
    if (*fuori == NULL) {
        printf("  ! non c'e' memoria per aprirlo (%lu byte)\n", isize);
        return -1;
    }

    if (inflate(d + i, (unsigned int)(n - i - 8), *fuori,
                (unsigned int)isize, &prodotti) != 0) {
        printf("  ! l'archivio non si apre: DEFLATE si e' fermato\n");
        return -1;
    }
    if ((unsigned long)prodotti != isize) {
        printf("  ! aperto ne da' %u e ne dichiara %lu: non e' intero\n",
               prodotti, isize);
        return -1;
    }

    crc_fatto = crc32_gz(*fuori, (long)isize);
    if (crc_fatto != crc_letto) {
        printf("  ! il CRC32 non torna: l'archivio e' rovinato\n");
        return -1;
    }

    *quanti = (long)isize;
    return 0;
}

/* -----------------------------------------------------------------------------
 * tar: intestazioni da 512 byte in ottale ASCII
 * --------------------------------------------------------------------------- */
#define TAR_BLOCCO 512

static long ottale(const unsigned char *p, int max)
{
    long v = 0;
    int  i;

    for (i = 0; i < max && (p[i] == ' ' || p[i] == '0'); i++) { }
    for (; i < max && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (p[i] - '0');
    return v;
}

/* La somma di controllo dell'intestazione: tutti i 512 byte, con il campo
 * della somma contato come otto spazi. E' l'unica verifica che il formato
 * offra, e costa otto righe: si fa. */
static int tar_somma_torna(const unsigned char *h)
{
    long somma = 0, dichiarata = ottale(h + 148, 8);
    int  i;

    for (i = 0; i < TAR_BLOCCO; i++)
        somma += (i >= 148 && i < 156) ? ' ' : h[i];
    return somma == dichiarata;
}

/* Scorre l'archivio aperto e mette ogni file al suo posto. Rende quanti ne ha
 * messi; conta a parte quelli che non ce l'hanno fatta. */
static long tar_estrai(unsigned char *d, long n, long *falliti)
{
    char dest[PERC_MAX], temp[PERC_MAX];
    long i = 0, messi = 0;

    while (i + TAR_BLOCCO <= n) {
        const unsigned char *h = d + i;
        long  dim;
        int   tipo;
        char  nome[256];

        if (h[0] == 0) break;               /* i blocchi di zeri: e' finita */

        if (!tar_somma_torna(h)) {
            printf("  ! l'intestazione a %ld non ha la somma giusta: mi fermo\n", i);
            (*falliti)++;
            return messi;
        }

        /* ustar: il nome puo' essere spezzato in `prefix` e `name`. */
        nome[0] = '\0';
        if (h[345] != 0) {
            copia_str(nome, (const char *)h + 345, 156 > (int)sizeof(nome) ?
                      (int)sizeof(nome) : 156);
            if (nome[0]) strcat(nome, "/");
        }
        {
            char corto[101];

            copia_str(corto, (const char *)h + 0, 101);
            if ((int)strlen(nome) + (int)strlen(corto) < (int)sizeof(nome))
                strcat(nome, corto);
        }

        dim  = ottale(h + 124, 12);
        tipo = h[156];
        i   += TAR_BLOCCO;

        if (tipo == '5') {                  /* una directory */
            unisci(dest, sizeof(dest), "/", nome);
            mkdir(dest, 0755);
            continue;
        }
        if (tipo != '0' && tipo != '\0') {
            /* Collegamenti, dispositivi, roba di GNU: l'albero pubblicato non
             * ne contiene, e installare cio' che non si e' capito e' peggio
             * che saltarlo dicendolo. */
            printf("  - %s: tipo '%c', saltato\n", nome, tipo ? tipo : '?');
            i += (dim + TAR_BLOCCO - 1) / TAR_BLOCCO * TAR_BLOCCO;
            continue;
        }
        if (i + dim > n) {
            printf("  ! %s: l'archivio finisce prima del file\n", nome);
            (*falliti)++;
            return messi;
        }

        unisci(dest, sizeof(dest), "/", nome);
        crea_strada(dest);
        snprintf(temp, sizeof(temp), "%s.new", dest);

        if (scrivi_file(temp, d + i, dim) != 0) {
            printf("  ! %s: non riesco a scriverlo (%s)\n", temp, strerror(errno));
            (*falliti)++;
        } else if (sostituisci(dest, temp) != 0) {
            (*falliti)++;
        } else {
            printf("  + %s (%ld byte)\n", nome, dim);
            messi++;
        }

        i += (dim + TAR_BLOCCO - 1) / TAR_BLOCCO * TAR_BLOCCO;
    }
    return messi;
}

/* =============================================================================
 * -install e -remove — UNA APPLICAZIONE PER VOLTA, CON CIO' CHE LE SERVE
 *
 * ! LE DIPENDENZE SI CHIUDONO IN CATENA, NON A UN PASSO. Se fbsrc vuole build
 * e build vuole base, chiedere fbsrc vuol dire prendere tutt'e tre: fermarsi
 * al primo livello installerebbe qualcosa che non parte, e la scoperta
 * arriverebbe alla prima esecuzione invece che adesso.
 *
 * ! E ALLA RIMOZIONE SI GUARDA DALL'ALTRA PARTE: non «da cosa dipende questo»
 * ma «chi dipende da questo». Sono due domande diverse sullo stesso grafo, e
 * confonderle vuol dire o portarsi via una libreria che serve a qualcun altro,
 * o lasciare in giro roba che non serve piu' a nessuno.
 *
 * ! -remove NON TOCCA LA RETE. Tutto quel che gli serve — chi possiede cosa,
 * chi dipende da chi — sta nel registro, ed e' la ragione per cui il registro
 * si porta dentro le `vuole` invece di andarsele a rileggere dal catalogo.
 * Una macchina scollegata deve poter disinstallare.
 * ============================================================================= */

/* Mette in `fuori` l'id e tutto cio' che vuole, in catena. Rende quanti.
 * L'ordine e' quello in cui vanno installati: le dipendenze prima. */
static int catena_vuole(Pacchetto *v, int n, const char *id,
                        char fuori[][ID_MAX], int max)
{
    int quanti = 0, i, k, giro;

    copia_str(fuori[quanti++], id, ID_MAX);

    /* Si passa e ripassa finche' non si aggiunge piu' niente: la catena e'
     * corta (una manciata di pacchetti) e cosi' non serve la ricorsione, che
     * su un catalogo scritto male sarebbe infinita. Un ciclo fra due `vuole`
     * qui non e' un guaio: si fermano tutt'e due dentro l'elenco. */
    for (giro = 0; giro < PACCHETTI_MAX; giro++) {
        int aggiunti = 0;

        for (i = 0; i < quanti; i++) {
            Pacchetto *p = cerca(v, n, fuori[i]);

            if (p == NULL) continue;
            for (k = 0; k < p->n_vuole; k++) {
                int j, c_e = 0;

                for (j = 0; j < quanti; j++)
                    if (strcmp(fuori[j], p->vuole[k]) == 0) { c_e = 1; break; }
                if (c_e) continue;
                if (quanti >= max) return quanti;
                copia_str(fuori[quanti++], p->vuole[k], ID_MAX);
                aggiunti++;
            }
        }
        if (!aggiunti) break;
    }

    /* Le dipendenze prima: si rovescia, perche' sono state aggiunte dopo. */
    for (i = 0; i < quanti / 2; i++) {
        char t[ID_MAX];

        copia_str(t, fuori[i], ID_MAX);
        copia_str(fuori[i], fuori[quanti - 1 - i], ID_MAX);
        copia_str(fuori[quanti - 1 - i], t, ID_MAX);
    }
    return quanti;
}

static int fra(char elenco[][ID_MAX], int n, const char *id)
{
    int i;

    for (i = 0; i < n; i++)
        if (strcmp(elenco[i], id) == 0) return 1;
    return 0;
}

/* -----------------------------------------------------------------------------
 * -install:list — cosa c'e' sul server, e cosa c'e' gia' qui
 * --------------------------------------------------------------------------- */
static int comando_install_list(const char *pezzo)
{
    Config    c;
    Pacchetto v[PACCHETTI_MAX], r[PACCHETTI_MAX];
    char      ver[PERC_MAX], cat[PERC_MAX], ele[PERC_MAX];
    char      versione[16], data[40];
    int       n, nr, i, mostrati = 0;

    if (manifesto(&c, ver, cat, ele, versione, data) != 0) return 1;

    n = blocchi_leggi(cat, v, PACCHETTI_MAX);
    if (n <= 0) {
        printf("  ! il catalogo non nomina nessun pacchetto.\n");
        return 1;
    }
    nr = blocchi_leggi(REG, r, PACCHETTI_MAX);
    if (nr < 0) nr = 0;

    printf("\n  %-12s %-11s %5s  %s\n", "PACCHETTO", "STATO", "MB", "COS'E'");
    for (i = 0; i < n; i++) {
        if (pezzo != NULL && pezzo[0] != '\0' &&
            strstr(v[i].id, pezzo) == NULL && strstr(v[i].nome, pezzo) == NULL)
            continue;
        printf("  %-12s %-11s %5ld  %s\n", v[i].id,
               cerca(r, nr, v[i].id) ? "installato" : "no",
               v[i].mbyte, v[i].nome);
        if (v[i].dice[0]) printf("  %-12s %-11s %5s  %s\n", "", "", "", v[i].dice);
        mostrati++;
    }

    if (mostrati == 0) {
        printf("  (nessuno: «%s» non compare in nessun nome)\n",
               pezzo ? pezzo : "");
        return 1;
    }
    printf("\n  %d pacchett%s. Per installarne uno:  netupdate -install:<nome>\n",
           mostrati, mostrati == 1 ? "o" : "i");
    return 0;
}

/* -----------------------------------------------------------------------------
 * Un pacchetto in un giro solo, quando si puo'.
 *
 * Rende 1 se il pacchetto e' stato messo a posto dall'archivio, 0 se
 * l'archivio non c'e' o non ci sta (e allora si va file per file), -1 se
 * l'archivio c'era e non ha funzionato — e anche in quel caso si ripiega,
 * perche' un pacchetto a meta' e' peggio di un giro di rete in piu'.
 *
 * ! L'IMPRONTA SI CONTROLLA PRIMA DI APRIRLO, non dopo. Aprire un archivio
 * vuol dire allocare quanto dice lui e fidarsi delle sue intestazioni: se i
 * byte non sono quelli che il catalogo dichiara, non c'e' motivo di guardarci
 * dentro. E l'impronta dell'archivio vale per TUTTI i file che contiene: e' il
 * modo in cui il giro solo mantiene la stessa promessa del file per file.
 * --------------------------------------------------------------------------- */
static int prova_archivio(Config *c, Pacchetto *q, long *fatti, long *falliti)
{
    unsigned char *aperto = NULL;
    char           url[URL_MAX + PERC_MAX + 16];
    long           n, quanti = 0, messi, f_qui = 0;

    if (q == NULL || q->archivio[0] == '\0' || q->arcbyte <= 0) return 0;

    if (q->arcbyte > (long)BUF_MAX) {
        printf("  - %s: l'archivio e' %ld byte, piu' del tetto: vado file per file\n",
               q->id, q->arcbyte);
        return 0;
    }

    printf("  = %s: un archivio da %ld KB (%ld KB aperto), un giro solo\n",
           q->id, (q->arcbyte + 1023) / 1024, (q->arcsrotolato + 1023) / 1024);

    url_componi(url, sizeof(url), c, q->archivio);
    n = prendi(url);
    if (n < 0) return -1;

    if (q->arcimpronta[0] && !impronta_e(g_buf, n, q->arcimpronta)) {
        printf("  ! l'archivio non ha l'impronta che il catalogo dichiara.\n");
        return -1;
    }

    if (gzip_apri(g_buf, n, &aperto, &quanti) != 0) return -1;

    messi = tar_estrai(aperto, quanti, &f_qui);
    *fatti   += messi;
    *falliti += f_qui;

    if (f_qui > 0) {
        printf("  ! %ld file dell'archivio non sono andati a posto.\n", f_qui);
        return -1;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * -install:<pacchetto> — l'app e tutto cio' che le serve
 * --------------------------------------------------------------------------- */
static int comando_install(const char *id)
{
    Config     c;
    Pacchetto  v[PACCHETTI_MAX], r[PACCHETTI_MAX];
    Pacchetto *p;
    char       ver[PERC_MAX], cat[PERC_MAX], ele[PERC_MAX];
    char       versione[16], data[40], riga[RIGA_MAX];
    char       scelti[PACCHETTI_MAX][ID_MAX], da_fare[PACCHETTI_MAX][ID_MAX];
    FILE      *f;
    int        n, nr, i, nscelti, nda = 0, gia = 0;
    long       n_file = 0, byte = 0, troppo_grossi = 0;
    long       fatti = 0, falliti = 0, saltati = 0;
    int        kernel_nuovo = 0, stage2_nuovo = 0;

    if (manifesto(&c, ver, cat, ele, versione, data) != 0) return 1;

    n = blocchi_leggi(cat, v, PACCHETTI_MAX);
    if (n <= 0) { printf("  ! il catalogo non nomina nessun pacchetto.\n"); return 1; }

    p = cerca(v, n, id);
    if (p == NULL) {
        printf("\nnetupdate: «%s» non c'e' nel catalogo di questo server.\n\n", id);
        printf("  Per vedere cosa c'e':  netupdate -install:list\n");
        return 1;
    }

    nr = blocchi_leggi(REG, r, PACCHETTI_MAX);
    if (nr < 0) {
        printf("\n  Il registro non c'e': lo scrivo adesso, o non saprei cosa\n");
        printf("  hai gia'.\n\n");
        if (comando_registro_crea(TMPDIR) != 0) return 1;
        nr = blocchi_leggi(REG, r, PACCHETTI_MAX);
        if (nr < 0) return 1;
    }

    /* --- la catena, e chi di quella catena c'e' gia' ---------------------- */
    nscelti = catena_vuole(v, n, id, scelti, PACCHETTI_MAX);
    for (i = 0; i < nscelti; i++) {
        if (cerca(r, nr, scelti[i]) != NULL) { gia++; continue; }
        copia_str(da_fare[nda++], scelti[i], ID_MAX);
    }

    printf("\n%s — %s\n", p->id, p->nome);
    if (p->dice[0]) printf("%s\n", p->dice);

    if (nscelti > 1) {
        printf("\n  Vuole, in catena:");
        for (i = 0; i < nscelti; i++)
            if (strcmp(scelti[i], id) != 0) printf(" %s", scelti[i]);
        printf("\n");
    }
    if (gia > 0) {
        printf("  Gia' qui:");
        for (i = 0; i < nscelti; i++)
            if (cerca(r, nr, scelti[i]) != NULL) printf(" %s", scelti[i]);
        printf("\n");
    }

    if (nda == 0) {
        printf("\nC'e' gia' tutto: %s e cio' che vuole sono installati.\n", id);
        printf("Per vedere se sono AGGIORNATI:  netupdate -check\n");
        return 0;
    }

    /* --- quanto pesa, prima di cominciare --------------------------------- */
    if (registro_carica() < 0) return 1;

    f = fopen(ele, "r");
    if (f == NULL) { printf("netupdate: %s sparito\n", ele); return 1; }
    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *fp, *impronta, *pac;
        long  fb;

        if (!riga_elenco(riga, &fp, &fb, &impronta, &pac)) continue;
        if (!fra(da_fare, nda, pac)) continue;
        if (verdetto(fp, impronta) == UGUALE) continue;
        n_file++;
        byte += fb;
        if (fb > (long)BUF_MAX) troppo_grossi++;
    }
    fclose(f);

    printf("\n  Da installare:");
    for (i = 0; i < nda; i++) printf(" %s", da_fare[i]);
    printf("\n  %ld file, %ld KB sul disco.\n", n_file, (byte + 1023) / 1024);

    /* Quali arrivano in un giro solo, e quali no: si dice PRIMA, perche' su
     * una linea lenta e' la differenza fra un minuto e mezz'ora. */
    for (i = 0; i < nda; i++) {
        Pacchetto *q = cerca(v, n, da_fare[i]);

        if (q != NULL && q->archivio[0] && q->arcbyte > 0 &&
            q->arcbyte <= (long)BUF_MAX)
            printf("  %s: archivio da %ld KB, un giro di rete solo.\n",
                   q->id, (q->arcbyte + 1023) / 1024);
    }

    if (n_file == 0) {
        printf("\n  Il catalogo li dichiara e l'elenco non gli da' nessun file:\n");
        printf("  non c'e' niente da scaricare. Il server e' incoerente.\n");
        return 1;
    }
    /* ! IL TETTO SI DICE PRIMA, NON A META' STRADA. Chi chiede il compilatore
     * deve sapere ADESSO che cc1 da solo e' piu' grande di quel che questo
     * programma sa scaricare, e non ritrovarsi un pacchetto a meta'. */
    if (troppo_grossi > 0) {
        printf("\n  ! %ld di quei file sono piu' grandi del tetto di %u byte e\n",
               troppo_grossi, BUF_MAX);
        printf("    NON si possono scaricare cosi': il pacchetto resterebbe a\n");
        printf("    meta'. Per gli strumenti grossi la strada e' il CD\n");
        printf("    (`toolinst`), finche' non ci sara' un lettore a pezzi.\n");
    }

    if (!chiedi_si("Procedo?", troppo_grossi == 0)) {
        printf("\nNon ho toccato niente.\n");
        return 0;
    }

    /* --- si scarica: un pacchetto per volta, in un giro se si puo' --------- */
    printf("\nScarico\n");
    for (i = 0; i < nda; i++) {
        Pacchetto *q = cerca(v, n, da_fare[i]);

        if (prova_archivio(&c, q, &fatti, &falliti) == 1) continue;

        /* ! IL RIPIEGO NON E' UN'ECCEZIONE, E' L'ALTRA META' DEL PIANO. Il
         * sistema di base non ha archivio per costruzione — `-check` deve
         * poter prendere i due file cambiati e non i centosettanta — e gli
         * alberi grossi non ce l'hanno perche' in memoria non ci starebbero.
         * Le due strade convivono, e questo ciclo e' quella lunga. */
        f = fopen(ele, "r");
        if (f == NULL) { printf("netupdate: %s sparito\n", ele); return 1; }
        while (fgets(riga, sizeof(riga), f) != NULL) {
            char *fp, *impronta, *pac;
            long  fb;

            if (!riga_elenco(riga, &fp, &fb, &impronta, &pac)) continue;
            if (strcmp(pac, da_fare[i]) != 0) continue;
            if (verdetto(fp, impronta) == UGUALE) continue;

            switch (scarica_e_metti(&c, fp, fb, impronta, &kernel_nuovo, &stage2_nuovo)) {
            case 0:  fatti++;   break;
            case 1:  saltati++; break;
            case 2:             break;
            default: falliti++; break;
            }
        }
        fclose(f);
    }

    if (kernel_nuovo || stage2_nuovo) fase_avvio(kernel_nuovo, stage2_nuovo);

    printf("\n%ld file installati", fatti);
    if (falliti) printf(", %ld non riusciti", falliti);
    if (saltati) printf(", %ld saltati perche' troppo grossi", saltati);
    printf(".\n");

    if (fatti > 0) {
        printf("\nRiscrivo il registro.\n\n");
        comando_registro_crea(TMPDIR);
    }
    return (falliti || saltati) ? 1 : 0;
}

/* -----------------------------------------------------------------------------
 * Il registro senza certi pacchetti: si copia saltandoli, e si sostituisce
 * alla fine. Stesso patto di -registro:crea — il caso peggiore e' restare
 * senza registro, non con mezzo registro che sembra intero.
 * --------------------------------------------------------------------------- */
static int registro_togli(char via[][ID_MAX], int nvia)
{
    FILE *in, *out;
    char  riga[RIGA_MAX], copia[RIGA_MAX];
    int   salta = 0;

    in = fopen(REG, "r");
    if (in == NULL) return -1;
    out = fopen(REG_NEW, "w");
    if (out == NULL) { fclose(in); return -1; }

    while (fgets(riga, sizeof(riga), in) != NULL) {
        copia_str(copia, riga, sizeof(copia));
        {
            char *r = ripulisci(copia);

            if (r[0] == '[') {
                char *fine = r;

                while (*fine && *fine != ']') fine++;
                if (*fine == ']') {
                    *fine = '\0';
                    salta = fra(via, nvia, ripulisci(r + 1));
                }
            }
        }
        if (!salta) fputs(riga, out);
    }
    fclose(in);
    if (fclose(out) != 0) { remove(REG_NEW); return -1; }

    remove(REG);
    if (rename(REG_NEW, REG) != 0) return -1;
    return 0;
}

/* Cancella dal disco i file dei pacchetti indicati, leggendoli dal registro.
 * Toglie anche i «.old»: un vecchio che sopravvive al suo pacchetto e' spazio
 * occupato da qualcosa che nessuno sa piu' cosa sia. */
static long cancella_file(char via[][ID_MAX], int nvia, long *non_tolti)
{
    FILE *f = fopen(REG, "r");
    char  riga[RIGA_MAX], b[PERC_MAX];
    long  tolti = 0;
    int   dentro = 0;

    if (f == NULL) return -1;

    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *r = ripulisci(riga), *val, *sp;

        if (r[0] == '[') {
            char *fine = r;

            while (*fine && *fine != ']') fine++;
            if (*fine == ']') {
                *fine = '\0';
                dentro = fra(via, nvia, ripulisci(r + 1));
            }
            continue;
        }
        if (!dentro) continue;
        if ((val = valore_se(r, "file")) == NULL) continue;

        sp = val;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        if (*sp == '\0') continue;
        *sp++ = '\0';
        while (*sp == ' ' || *sp == '\t') sp++;

        unisci(b, sizeof(b), "/", sp);
        if (remove(b) == 0) tolti++;
        else                (*non_tolti)++;

        if ((int)strlen(b) + 4 < PERC_MAX) { strcat(b, ".old"); remove(b); }
    }
    fclose(f);
    return tolti;
}

/* -----------------------------------------------------------------------------
 * -remove:<pacchetto> — l'app, e le librerie che non servono a nessun altro
 *
 * ! SI GUARDA CHI DIPENDE DA QUESTO, non da cosa dipende questo. E cio' che
 * resta si NOMINA: chi non vede la riga «xlib resta: la usano yapp, zapp»
 * pensa che la rimozione sia fallita, e la rifa'.
 * --------------------------------------------------------------------------- */
static int comando_remove(const char *id)
{
    Pacchetto  r[PACCHETTI_MAX];
    Pacchetto *p;
    char       via[PACCHETTI_MAX][ID_MAX], usato[256];
    int        nr, nvia = 0, i, k, giro;
    long       n_file = 0, tolti, non_tolti = 0;

    nr = blocchi_leggi(REG, r, PACCHETTI_MAX);
    if (nr < 0) return niente_registro();

    p = cerca(r, nr, id);
    if (p == NULL) {
        printf("netupdate: «%s» non risulta installato.\n\n", id);
        printf("  Ci sono:");
        for (i = 0; i < nr; i++) printf(" %s", r[i].id);
        printf("\n");
        return 1;
    }
    if (p->sempre) {
        printf("netupdate: «%s» non si toglie: %s\n\n", id, p->nome);
        printf("  E' segnato «sempre» nel catalogo, e vuol dire che senza di\n");
        printf("  lui questa macchina non e' piu' una macchina.\n");
        return 1;
    }

    chi_usa(r, nr, id, usato, sizeof(usato));
    if (strcmp(usato, "nessuno") != 0) {
        printf("netupdate: «%s» non si puo' togliere: lo usano %s.\n\n", id, usato);
        printf("  Togli prima quelli, o resterebbero a meta'.\n");
        return 1;
    }

    copia_str(via[nvia++], id, ID_MAX);

    /* Gli orfani: le sue `vuole` che, tolto lui, non le usa piu' nessuno. Si
     * ripassa finche' non se ne aggiungono altri — togliere una libreria puo'
     * rendere orfana quella sotto. */
    for (giro = 0; giro < PACCHETTI_MAX; giro++) {
        int aggiunti = 0;

        for (i = 0; i < nvia; i++) {
            Pacchetto *q = cerca(r, nr, via[i]);

            if (q == NULL) continue;
            for (k = 0; k < q->n_vuole; k++) {
                Pacchetto *dip = cerca(r, nr, q->vuole[k]);

                if (dip == NULL || dip->sempre) continue;
                if (fra(via, nvia, dip->id)) continue;
                if (chi_usa_salvo(r, nr, dip->id, via, nvia, usato, sizeof(usato)))
                    continue;                      /* serve ancora a qualcuno */
                if (nvia >= PACCHETTI_MAX) break;
                copia_str(via[nvia++], dip->id, ID_MAX);
                aggiunti++;
            }
        }
        if (!aggiunti) break;
    }

    printf("Tolgo %s — %s\n", p->id, p->nome);
    for (i = 0; i < nvia; i++) {
        Pacchetto *q = cerca(r, nr, via[i]);

        if (q == NULL) continue;
        n_file += q->n_file;
        if (i > 0) printf("  porta via anche %s (%s): non lo usa piu' nessuno\n",
                          q->id, q->nome);
    }

    /* E quel che RESTA, con chi lo tiene in vita. DIRLO E' PARTE DEL LAVORO:
     * chi non vede la riga «base resta: la usano cpp, fb» pensa che la
     * rimozione sia fallita, e la rifa'.
     *
     * ! SI GUARDANO LE `vuole` DI TUTTA LA CATENA CHE SE NE VA, non solo del
     * pacchetto chiesto. La prima versione guardava solo quelle di fbsrc, e
     * quindi non diceva NIENTE di base — che e' voluta da build, cioe' dal
     * pacchetto che se ne andava insieme. Proprio la riga che serviva. */
    {
        int detti[PACCHETTI_MAX], ndetti = 0, w;

        for (i = 0; i < nvia; i++) {
            Pacchetto *q = cerca(r, nr, via[i]);

            for (k = 0; q != NULL && k < q->n_vuole; k++) {
                Pacchetto *dip = cerca(r, nr, q->vuole[k]);
                int        gia_detto = 0;

                if (dip == NULL || fra(via, nvia, dip->id)) continue;
                for (w = 0; w < ndetti; w++)
                    if (detti[w] == (int)(dip - r)) gia_detto = 1;
                if (gia_detto) continue;
                if (ndetti < PACCHETTI_MAX) detti[ndetti++] = (int)(dip - r);

                chi_usa_salvo(r, nr, dip->id, via, nvia, usato, sizeof(usato));
                printf("  %s RESTA: %s\n", dip->id,
                       dip->sempre ? "e' del sistema" : usato);
            }
        }
    }

    printf("\n  %ld file da cancellare.\n", n_file);
    if (!chiedi_si("Procedo?", 0)) {
        printf("\nNon ho toccato niente.\n");
        return 0;
    }

    /* ! PRIMA I FILE, POI IL REGISTRO. Al contrario, una macchina che si
     * spegne in mezzo si ritroverebbe i file sul disco e nessuno che sa piu'
     * a chi appartengono: roba che non si puo' piu' togliere. Cosi' invece il
     * caso peggiore e' un registro che promette file che non ci sono, e
     * `-check` lo dice al primo giro. */
    tolti = cancella_file(via, nvia, &non_tolti);
    if (tolti < 0) { printf("netupdate: il registro non si legge.\n"); return 1; }

    if (registro_togli(via, nvia) != 0) {
        printf("\n! %ld file cancellati, MA IL REGISTRO NON E' STATO RISCRITTO.\n",
               tolti);
        printf("  Dice ancora che ci sono. Rifallo con:\n");
        printf("      netupdate -registro:crea <albero>\n");
        return 1;
    }

    printf("\n%ld file cancellati", tolti);
    if (non_tolti) printf(", %ld non c'erano piu'", non_tolti);
    printf(". Il registro non li nomina piu'.\n");
    return 0;
}

/* =============================================================================
 * L'uso
 * ============================================================================= */
static void uso(void)
{
    printf("uso: netupdate -set                     da dove ci si aggiorna\n");
    printf("     netupdate -check                   cosa e' cambiato sul server\n");
    printf("     netupdate -check:guarda            lo stesso, senza toccare niente\n");
    printf("     netupdate -auto                    l'occhiata dell'avvio: zitta,\n");
    printf("                                        e solo se automatico = si\n");
    printf("     netupdate -registro                cosa c'e' installato qui\n");
    printf("     netupdate -registro:<pacchetto>    uno solo, coi suoi file\n");
    printf("     netupdate -registro:crea <albero>  il registro da una copia locale\n");
    printf("     netupdate -install:list <pezzo>    le app il cui nome lo contiene\n");
    printf("     netupdate -install:<app>           l'app e cio' che le serve\n");
    printf("     netupdate -remove:<app>            l'app e le librerie di nessun altro\n");
    printf("\n");
    printf("Due file, e sono cose diverse:\n");
    printf("  %s  da dove ci si aggiorna, lo scrive -set\n", CNF);
    printf("  %s  cosa c'e' installato, lo scrive netupdate\n", REG);
}

int main(int argc, char **argv)
{
    if (argc < 2) { uso(); return 1; }

    if (strcmp(argv[1], "-set") == 0)  return comando_set();

    if (strcmp(argv[1], "-registro") == 0) return comando_registro();

    if (strncmp(argv[1], "-registro:", 10) == 0) {
        const char *coda = argv[1] + 10;

        /* ! «crea» E' UNA PAROLA RISERVATA DOPO I DUE PUNTI, come «list» lo e'
         * per -install. Un pacchetto che si chiamasse cosi' non si potrebbe
         * guardare: e' il prezzo, ed e' scritto qui perche' chi un giorno
         * pubblichera' un pacchetto «crea» sappia dove sbatte. */
        if (strcmp(coda, "crea") == 0) {
            if (argc < 3) {
                printf("uso: netupdate -registro:crea <albero>\n\n");
                printf("  <albero> e' una copia locale di cio' che sta sul\n");
                printf("  server: dentro ci stanno versione.txt, catalogo.txt,\n");
                printf("  elenco.txt e file/: e' quel che fa `make netinst`.\n");
                return 1;
            }
            return comando_registro_crea(argv[2]);
        }
        if (coda[0] == '\0') return comando_registro();
        return comando_registro_uno(coda);
    }

    if (strcmp(argv[1], "-check") == 0) return comando_check();

    /* ! «guarda» E' UNA PAROLA RISERVATA DOPO I DUE PUNTI, come «crea» per
     * -registro e «list» per -install. */
    if (strcmp(argv[1], "-check:guarda") == 0) return comando_check_guarda();

    if (strcmp(argv[1], "-auto") == 0) return comando_auto();

    if (strncmp(argv[1], "-install:", 9) == 0) {
        const char *coda = argv[1] + 9;

        /* ! «list» E' UNA PAROLA RISERVATA DOPO I DUE PUNTI, come «crea» per
         * -registro. Sta scritto qui perche' chi un giorno pubblichera' un
         * pacchetto che si chiama «list» sappia dove sbatte. */
        if (strcmp(coda, "list") == 0)
            return comando_install_list(argc > 2 ? argv[2] : "");
        if (coda[0] == '\0') { uso(); return 1; }
        return comando_install(coda);
    }

    if (strncmp(argv[1], "-remove:", 8) == 0) {
        const char *coda = argv[1] + 8;

        if (coda[0] == '\0') { uso(); return 1; }
        return comando_remove(coda);
    }

    if (strncmp(argv[1], "-install", 8) == 0 ||
        strncmp(argv[1], "-remove", 7) == 0) {
        /* ! DICHIARATO, NON DIMENTICATO. Queste due non ci sono ancora, e
         * dirlo cosi' costa una riga: un comando che accetta un'opzione e non
         * fa niente e' peggio di uno che la rifiuta, perche' chi lo usa crede
         * di aver installato. Il lavoro e' in in_lavorazione.txt, @NET-APP. */
        printf("netupdate: «%s» vuole i due punti e un nome.\n\n", argv[1]);
        printf("           netupdate -install:list [pezzo di nome]\n");
        printf("           netupdate -install:<pacchetto>\n");
        printf("           netupdate -remove:<pacchetto>\n");
        return 2;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        uso();
        return 0;
    }

    printf("netupdate: non conosco «%s»\n\n", argv[1]);
    uso();
    return 1;
}

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

/* +0.001 a ogni modifica: `netupdate -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("netupdate", "0.005");

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
static int chi_usa(Pacchetto *v, int n, const char *id, char *out, int max)
{
    int i, j, quanti = 0;

    out[0] = '\0';
    for (i = 0; i < n; i++) {
        if (strcmp(v[i].id, id) == 0) continue;
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
        if (mancano) printf(", %ld che non ci sono", mancano);
        if (diversi) printf(", %ld diversi dall'elenco", diversi);
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
        printf("  ! %ld file dell'elenco non ci sono su questo disco: non li ho\n"
               "    scritti nel registro. `-check` li proporra'.\n", tot_mancano);
    if (tot_diversi)
        printf("  ! %ld file ci sono con un'altra dimensione: registrati senza\n"
               "    impronta. Non sono quelli dell'elenco.\n", tot_diversi);
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
        printf("  ! %s\n", e.errore[0] ? e.errore : "non riuscito");
        return -1;
    }
    if (e.codice != 200) {
        printf("  ! %s: il server risponde %d\n", url, e.codice);
        return -1;
    }
    if (e.troncata) {
        printf("  ! %s: piu' grande di %u byte, non lo posso tenere\n",
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
        printf("  ! %s non ha l'impronta che versione.txt dichiara.\n", coda);
        printf("    Il server si contraddice: non tocco niente.\n");
        return -1;
    }
    if (scrivi_file(dove, g_buf, n) != 0) {
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
        printf("      rinomina /boot/kernel.old in /boot/kernel.bin\n");
        printf("    oppure rilancia `install <punto>` che la mappa la rifa'.\n");
        return -1;
    }
    printf("  = settore di avvio riscritto: kernel a LBA %u, %u settori\n",
           info.k_lba, info.k_cnt);
    printf("  = il kernel di prima resta in /boot/kernel.old\n");
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

static int comando_check(void)
{
    Config c;
    char   ver[PERC_MAX], cat[PERC_MAX], ele[PERC_MAX];
    char   versione[16], data[40], h_cat[72], h_ele[72];
    char   riga[RIGA_MAX], dest[PERC_MAX], temp[PERC_MAX], coda[PERC_MAX + 8];
    FILE  *f;
    long   n_uguali = 0, n_cambiati = 0, n_verif = 0, n_nuovi = 0, n_scon = 0;
    long   n_config = 0;
    long   byte_cambiati = 0, byte_nuovi = 0;
    long   fatti = 0, falliti = 0, saltati = 0;
    int    mostrati = 0, kernel_nuovo = 0, stage2_nuovo = 0;
    int    fai_cambiati, fai_nuovi;

    if (!config_leggi(&c)) {
        printf("netupdate: non so da dove aggiornare (%s non c'e').\n\n", CNF);
        printf("  Prima:  netupdate -set\n");
        return 1;
    }
    if (strcasecmp(c.tipo, "HTTP") != 0) {
        printf("netupdate: la configurazione dice %s, e oggi so fare solo HTTP.\n\n",
               c.tipo);
        printf("  FTP non c'e' ancora; HTTPS e FTPS vogliono la TLS, che ha un\n");
        printf("  limite noto. Si cambia con `netupdate -set`.\n");
        return 1;
    }
    if (!boot_si_scrive()) {
        printf("netupdate: non posso scrivere in /boot (%s).\n\n", strerror(errno));
        printf("  Un sistema che gira da CD non si aggiorna: /boot e' il CD.\n");
        return 1;
    }

    mkdir("/tmp", 0755);
    mkdir(TMPDIR, 0755);
    snprintf(ver, sizeof(ver), "%s/versione.txt", TMPDIR);
    snprintf(cat, sizeof(cat), "%s/catalogo.txt", TMPDIR);
    snprintf(ele, sizeof(ele), "%s/elenco.txt",   TMPDIR);

    printf("Guardo http://%s\n\n", c.url);

    /* --- 1. il manifesto, e la sua coerenza ------------------------------- */
    if (prendi_verifica_scrivi(&c, "versione.txt", NULL, ver) != 0) {
        printf("\n  Non ho potuto leggere versione.txt. Se l'indirizzo e'\n");
        printf("  giusto e la rete c'e', controlla che il server pubblichi la\n");
        printf("  RADICE dell'albero fatto da `make netinst`.\n");
        return 1;
    }
    if (chiave_da_file(ver, "versione", versione, sizeof(versione)) != 0) {
        printf("  ! versione.txt non dice nessuna versione: non e' un albero\n");
        printf("    pubblicato da `make netinst`.\n");
        return 1;
    }
    if (chiave_da_file(ver, "data", data, sizeof(data)) != 0)
        copia_str(data, "?", sizeof(data));

    /* ! LE DUE IMPRONTE DENTRO versione.txt SONO IL SUO MESTIERE. Un elenco
     * scaricato a meta' — o servito da un proxy che si e' inventato qualcosa —
     * porterebbe a confrontare impronte con niente, cioe' a riscaricare tutto
     * o a non riscaricare niente, e in tutt'e due i casi senza un errore. */
    if (chiave_da_file(ver, "catalogo", h_cat, sizeof(h_cat)) != 0 ||
        chiave_da_file(ver, "elenco",   h_ele, sizeof(h_ele)) != 0) {
        printf("  ! versione.txt non porta le impronte di catalogo.txt e\n");
        printf("    elenco.txt: senza, non posso sapere se quel che scarico\n");
        printf("    e' arrivato intero. Mi fermo.\n");
        return 1;
    }
    if (prendi_verifica_scrivi(&c, "catalogo.txt", h_cat, cat) != 0) return 1;
    if (prendi_verifica_scrivi(&c, "elenco.txt",   h_ele, ele) != 0) return 1;

    printf("  server: sistema %s del %s\n", versione, data);

    /* --- 2. il registro di questa macchina -------------------------------- */
    {
        Pacchetto v[PACCHETTI_MAX];
        int       n = blocchi_leggi(REG, v, PACCHETTI_MAX);

        if (n < 0) {
            printf("\nQui il registro non c'e' ancora: lo scrivo adesso, dal\n");
            printf("manifesto appena scaricato. E' quel che serve per sapere\n");
            printf("cosa confrontare — e da adesso in poi c'e'.\n\n");
            if (comando_registro_crea(TMPDIR) != 0) return 1;
            printf("\n");
        } else if (n > 0) {
            printf("  qui:    sistema %s\n", v[0].versione[0] ? v[0].versione : "?");
        }
    }
    if (registro_carica() < 0) {
        printf("netupdate: il registro non si legge. Rifallo con -registro:crea.\n");
        return 1;
    }

    /* --- 3. il confronto, file per file ----------------------------------- */
    f = fopen(ele, "r");
    if (f == NULL) { printf("netupdate: %s sparito\n", ele); return 1; }

    printf("\nDa aggiornare:\n");
    while (fgets(riga, sizeof(riga), f) != NULL) {
        char *p, *impronta, *pac;
        long  byte;
        int   v;

        if (!riga_elenco(riga, &p, &byte, &impronta, &pac)) continue;
        v = verdetto(p, impronta);

        if (v == UGUALE)         { n_uguali++; continue; }
        if (v == MAI_INSTALLATO) { n_nuovi++; byte_nuovi += byte; continue; }

        if (e_configurazione(p)) {
            n_config++;
            continue;
        }

        if (v == CAMBIATO)      n_cambiati++;
        if (v == DA_VERIFICARE) n_verif++;
        if (v == SCONOSCIUTO)   n_scon++;
        byte_cambiati += byte;

        if (mostrati < 12) {
            printf("    %s%s (%ld byte)\n", p,
                   v == DA_VERIFICARE ? " [da verificare]" :
                   v == SCONOSCIUTO   ? " [il registro non lo conosce]" : "",
                   byte);
            mostrati++;
        } else if (mostrati == 12) { printf("    ...\n"); mostrati++; }
    }
    fclose(f);

    if (mostrati == 0) printf("    niente\n");

    printf("\n  %ld uguali, %ld da aggiornare (%ld KB da scaricare)",
           n_uguali, n_cambiati + n_verif + n_scon, (byte_cambiati + 1023) / 1024);
    if (n_verif) printf(",\n  di cui %ld da verificare (impronta sconosciuta)", n_verif);
    if (n_scon)  printf(",\n  e %ld che il registro non conosce", n_scon);
    printf(".\n");
    if (n_nuovi)
        printf("  E %ld file che qui non ci sono MAI STATI (%ld KB).\n",
               n_nuovi, (byte_nuovi + 1023) / 1024);
    if (n_config) {
        printf("  E %ld file di CONFIGURAZIONE, diversi dal server e NON toccati:\n",
               n_config);
        printf("  sono di questa macchina (driver, tastiera, montaggi). Per\n");
        printf("  fonderli con quelli nuovi c'e' `install`, che sa farlo.\n");
    }

    if (n_cambiati + n_verif + n_scon + n_nuovi == 0) {
        printf("\nNiente da fare");
        if (n_config) printf(" (a parte la configurazione, che e' tua)");
        printf(".\n");
        return 0;
    }

    /* ! «MANCA» E «E' DIVERSO» NON SONO LA STESSA COSA, e si chiedono a parte.
     * Un file che non c'e' puo' essere un componente che chi ha installato NON
     * HA VOLUTO — install chiede quali directory copiare — e riportarlo dentro
     * con la scusa dell'aggiornamento vuol dire riempire una macchina di roba
     * a cui il suo padrone aveva gia' detto di no. */
    fai_cambiati = (n_cambiati + n_verif + n_scon > 0)
        ? chiedi_si("Aggiorno i file cambiati?", 1) : 0;
    fai_nuovi = 0;
    if (n_nuovi > 0) {
        printf("\n  I %ld file mai installati possono essere componenti che\n", n_nuovi);
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
        long  byte, n;
        int   v;

        if (!riga_elenco(riga, &p, &byte, &impronta, &pac)) continue;
        v = verdetto(p, impronta);

        if (v == UGUALE) continue;
        if (e_configurazione(p)) continue;      /* e' di questa macchina */
        if (v == MAI_INSTALLATO && !fai_nuovi) continue;
        if (v != MAI_INSTALLATO && !fai_cambiati) continue;

        /* ! UN FILE PIU' GRANDE DEL BUFFER NON SI SCARICA, E SI DICE. exhttp
         * vuole il corpo intero in memoria: e' il tetto scritto in cima. Il
         * sistema ci sta tutto; gli strumenti no, e chi aggiorna deve saperlo
         * adesso e non scoprirlo con un compilatore a meta'. */
        if (byte > (long)BUF_MAX) {
            printf("  - %s: %ld byte, piu' del tetto di %u. SALTATO.\n",
                   p, byte, BUF_MAX);
            saltati++;
            continue;
        }

        snprintf(coda, sizeof(coda), "file/%s", p);
        {
            char url[URL_MAX + PERC_MAX + 16];

            url_componi(url, sizeof(url), &c, coda);
            n = prendi(url);
        }
        if (n < 0) { falliti++; continue; }

        if (!impronta_e(g_buf, n, impronta)) {
            printf("  ! %s: l'impronta non torna, NON lo installo\n", p);
            falliti++;
            continue;
        }

        unisci(dest, sizeof(dest), "/", p);
        if (e_avvio(p)) {
            /* I due dell'avvio si mettono da parte col nome che bootverify si
             * aspetta, e si sostituiscono per ULTIMI. */
            snprintf(temp, sizeof(temp), "/boot/%s.new",
                     strcmp(p, "boot/kernel.bin") == 0 ? "kernel" : "stage2");
            if (scrivi_file(temp, g_buf, n) != 0) {
                printf("  ! %s: non riesco a scriverlo (%s)\n", temp, strerror(errno));
                falliti++;
                continue;
            }
            if (strcmp(p, "boot/kernel.bin") == 0) kernel_nuovo = 1;
            else                                   stage2_nuovo = 1;
            printf("  = %s scaricato e verificato (per ultimo)\n", p);
            continue;
        }

        crea_strada(dest);
        snprintf(temp, sizeof(temp), "%s.new", dest);
        if (scrivi_file(temp, g_buf, n) != 0) {
            printf("  ! %s: non riesco a scriverlo (%s)\n", temp, strerror(errno));
            falliti++;
            continue;
        }
        if (sostituisci(dest, temp) != 0) { falliti++; continue; }
        printf("  + %s (%ld byte)\n", p, n);
        fatti++;
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
               "  /boot/kernel.old, e il settore di avvio punta al nuovo.\n");
    else if (fatti > 0)
        printf("\n! I programmi gia' in esecuzione stanno ancora usando i file\n"
               "  di prima, che sono li' col nome .old. Riavvia quando puoi:\n"
               "  il prossimo -check li togliera'.\n");
    return 0;
}

/* =============================================================================
 * L'uso
 * ============================================================================= */
static void uso(void)
{
    printf("uso: netupdate -set                     da dove ci si aggiorna\n");
    printf("     netupdate -check                   cosa e' cambiato sul server\n");
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

    if (strncmp(argv[1], "-install", 8) == 0 ||
        strncmp(argv[1], "-remove", 7) == 0) {
        /* ! DICHIARATO, NON DIMENTICATO. Queste due non ci sono ancora, e
         * dirlo cosi' costa una riga: un comando che accetta un'opzione e non
         * fa niente e' peggio di uno che la rifiuta, perche' chi lo usa crede
         * di aver installato. Il lavoro e' in in_lavorazione.txt, @NET-APP. */
        printf("netupdate: «%s» non c'e' ancora.\n", argv[1]);
        printf("           Il sistema si aggiorna (-check); le applicazioni\n");
        printf("           una per una non ancora.\n");
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

/* =============================================================================
 * exwin/bin/filemgr/filemgr.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il file manager, a due aree
 *
 *     /exwin/bin/filemgr [DIRECTORY]
 *
 *     +---------------------------------------------------------------+
 *     | File   Comandi   Info                                         |
 *     +----------------+----------------------------------------------+
 *     | Cartelle       |[Nome ^][Tipo][Dimensione][Data]              |
 *     +----------------+----------------------------------------------+
 *     | - /            | bin         <DIR>          -  2026-09-16 18:10|
 *     |   + bin        | dev         <DIR>          -  2026-09-16 18:10|
 *     |   - exwin      | leggimi.txt <FILE>      1024  2026-09-14 09:02|
 *     |     + bin      |                                              |
 *     +----------------+----------------------------------------------+
 *     | /exwin  -  4 voci                                             |
 *     +---------------------------------------------------------------+
 *
 * ! LE COLONNE SI ORDINANO CLICCANDO L'INTESTAZIONE. Un clic sceglie la
 * colonna, un secondo clic sulla stessa rovescia il verso, e la freccia
 * nell'etichetta dice qual e' — perche' «per che cosa e' ordinato» e «in che
 * verso» sono due domande, e la seconda senza indizi si risponde indovinando.
 *
 * ! E IL VERSO SE LO RICORDA OGNI COLONNA PER CONTO SUO. Chi ordina per data
 * vuole il piu' recente in cima, chi ordina per nome vuole la A: un verso solo
 * per tutte costringerebbe a due clic ogni volta che si cambia colonna.
 *
 * ! LA DATA E' QUELLA DI MODIFICA, e quella di creazione non c'e'. Non e' una
 * scelta di questo programma: la voce di directory che FAT, ext2 e ISO 9660
 * consegnano al VFS tiene UNA coppia data/ora, e `struct stat` infatti pone
 * st_ctime e st_atime uguali a st_mtime, dichiarandolo. Mostrare lo stesso
 * numero sotto due intestazioni diverse sarebbe peggio che mostrarne uno.
 *
 * ! A SINISTRA C'E' DOVE SI E', A DESTRA COSA C'E'. Con una lista sola le due
 * domande si rispondono a turno: per sapere dov'e' un file bisogna risalire, e
 * risalendo si perde di vista il file. E' la ragione per cui ogni file manager
 * mai scritto ha due aree, e non e' una questione di gusto.
 *
 * ! L'ALBERO NON E' UN CONTROLLO NUOVO DEL TOOLKIT, E' UNA LISTA CON DENTRO
 * L'INDENTAZIONE. Un «controllo albero» vorrebbe dire nodi, figli, un modello
 * da tenere aggiornato e un disegno tutto suo dentro exwin.so — cioe' un pezzo
 * di toolkit che UNA sola applicazione usa. Qui l'albero e' un vettore di nodi
 * in ORDINE DI VISUALIZZAZIONE: espandere vuol dire infilare i figli subito
 * dopo il padre, chiudere vuol dire toglierli. La lista non sa che sia un
 * albero, e non deve saperlo.
 *
 * ! I FILE SI SEGNANO CON LA BARRA SPAZIATRICE, e un comando agisce sui
 * segnati — o, se non ce n'e' nessuno, sulla riga dove sta il cursore. E'
 * UNA regola sola, non due modi di lavorare: chi non ha mai premuto la barra
 * continua a usare il programma come prima senza accorgersi che e' cambiato.
 *
 * ! E IL SEGNO NON E' UN CONTROLLO NUOVO DEL TOOLKIT. Una lista che sappia
 * tenere piu' righe scelte vorrebbe dire un modello di selezione, un disegno
 * suo e un'ABI in piu' da tenere ferma dentro exwin.so, per una cosa che UNA
 * sola applicazione usa. Qui il segno e' un carattere nella colonna 0 della
 * riga — che la riga la impagina gia' questo programma. Stessa scelta
 * dell'albero, che e' una lista con dentro l'indentazione.
 *
 * ! E IL PERCORSO DI UN NODO SI RICOSTRUISCE ALL'INDIETRO, senza puntatori al
 * padre. Con l'inserimento e la rimozione in mezzo al vettore, un indice del
 * padre sarebbe da correggere in tutti i nodi ogni volta — cioe' il difetto
 * che aspetta. Il livello, invece, non cambia mai: risalire cercando il primo
 * nodo di livello minore e' O(n) e non si puo' sbagliare.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `filemgr -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.006"
EX_VERSIONE("filemgr", VERSIONE_APP);

#define VOCI_MAX    512
#define NODI_MAX    128
#define PERC_MAX    192
#define PROFONDITA  8       /* quanto in giu' vanno copia ricorsiva e ricerca */

#define FIN_W       740
#define FIN_H       440
#define MENU_H      20
#define BASSO       24
#define ALBERO_W    210
#define INTEST_H     18     /* la fascia dei pulsanti sopra le due aree */

/* =============================================================================
 * LE COLONNE DELL'ELENCO, IN CARATTERI
 *
 * ! SONO L'UNICA DEFINIZIONE DEL FORMATO, e da loro discendono SIA la riga SIA
 * i pulsanti dell'intestazione. Scritte due volte — una nello sprintf e una
 * nelle coordinate dei pulsanti — si scollerebbero alla prima colonna
 * allargata, e il sintomo sarebbe un'intestazione che indica la colonna
 * sbagliata: cioe' una bugia, non un disallineamento estetico.
 *
 * ! LA LISTA DEL TOOLKIT DISEGNA A PASSO FISSO di 8 pixel (LISTA_CAR_W in
 * lib/exwin/exwin.c) e tiene 64 byte per riga (LISTA_TESTO_MAX). Il totale qui
 * sotto e' 62 caratteri piu' lo zero: ci sta, e non di misura per caso —
 * FIN_W e ALBERO_W sono stati scelti perche' ci stesse.
 *
 *     ' ' nome(24) ' ' tipo(6) ' ' dimensione(12) ' ' data(10) ' ' ora(5)
 *
 * ! LA COLONNA «dimensione» E' PIU' LARGA DEL NUMERO PIU' LUNGO, e non per il
 * numero: e' per l'INTESTAZIONE. Con dieci caratteri il pulsante era largo
 * esattamente quanto «Dimensione», e la freccia del verso — due caratteri in
 * piu' — finiva tagliata. L'indicazione del verso spariva proprio sulla
 * colonna appena scelta.
 * ============================================================================= */
/* ! LA COLONNA 0 E' IL SEGNO, ED ERA GIA' LI' A NON FAR NIENTE: la riga
 * comincia con uno spazio (C_NOME_X vale 1) perche' il nome non tocchi il
 * bordo. Il segno ci sta dentro senza spostare niente, senza togliere un
 * carattere al nome e senza che l'intestazione debba saperlo. */
#define C_SEGNO_X    0
#define C_NOME_X     1
#define C_NOME_W    24
#define C_TIPO_X    26
#define C_TIPO_W     6
#define C_DIM_X     33
#define C_DIM_W     12
#define C_DATA_X    46
#define C_DATA_W    10
#define C_ORA_X     57
#define C_ORA_W      5
#define RIGA_CAR    (C_ORA_X + C_ORA_W)     /* 62 */

/* Quanto e' larga l'area dell'elenco, in pixel, perche' le 62 colonne ci
 * stiano: due margini da 4 come li mette la lista, piu' il testo. */
#define ELENCO_W    (RIGA_CAR * 8 + 8 + LISTA_ICONE_W)

/* ! LO SPAZIO DELLE ICONE SI RISERVA SEMPRE, anche quando le icone non ci
 * sono. La corsia della lista e' larga due caratteri (lib/exwin: le righe si
 * indentano solo se almeno una ha l'icona), e l'elenco deve poter mostrare le
 * sue sessantadue colonne in tutt'e due i casi: senza questi sedici pixel, il
 * giorno che le icone vengono installate l'ultima colonna finirebbe fuori dal
 * bordo. Sedici pixel di margine a destra quando non servono si notano meno. */
#define LISTA_ICONE_W  16

/* ! LE COORDINATE DELL'INTESTAZIONE ESCONO DALLE STESSE COSTANTI DELLE RIGHE,
 * e stanno qui perche' servono in DUE posti: a crearla e a spostarla quando la
 * lista apre la corsia delle icone. C_NOME_X e compagni sono in caratteri; la
 * lista disegna a passo di 8 pixel e lascia 4 pixel di margine a sinistra.
 *
 * `dal` e' il carattere dove comincia la colonna, `al` quello dove comincia la
 * prossima (RIGA_CAR per l'ultima): cosi' i pulsanti si toccano senza
 * sovrapporsi e senza lasciare buchi. */
#define INT_T0          (ALBERO_W + 10 + 4)
#define INT_X(dal)      (INT_T0 + ((dal) - 1) * 8)
#define INT_W(dal, al)  (((al) - (dal) + 1) * 8)
#define INT_Y           (MENU_H + 3)

#define ID_ALBERO    1
#define ID_ELENCO    2

#define ID_APRI      10
#define ID_SU        11
#define ID_AGGIORNA  12
#define ID_ESCI      13

/* ! «Copia» E «Copia directory» ERANO DUE VOCI E ADESSO SONO UNA. Con i segni
 * un gruppo puo' contenere file E cartelle insieme: due comandi che si
 * rifiutano a vicenda («e' una directory: usa l'altro») non avrebbero saputo
 * cosa fare di un gruppo misto. Copia guarda cosa ha davanti, voce per voce. */
#define ID_COPIA     20
#define ID_SPOSTA    21
#define ID_CERCA     22
#define ID_CANCELLA  23
#define ID_SEGNA     24
#define ID_SEGNA_TUTTI  25
#define ID_SEGNA_NIENTE 26
#define ID_NUOVA_DIR    27

#define ID_ISTRUZIONI 30
#define ID_INFO       31

/* I pulsanti dell'intestazione: uno per colonna ordinabile. */
#define ID_ORD_NOME   40
#define ID_ORD_TIPO   41
#define ID_ORD_DIM    42
#define ID_ORD_DATA   43

#define ORD_NOME  0
#define ORD_TIPO  1
#define ORD_DIM   2
#define ORD_DATA  3

/* -----------------------------------------------------------------------------
 * L'albero, a sinistra
 *
 * ! IL VETTORE E' GIA' L'ORDINE IN CUI SI VEDE. Non c'e' una struttura ad
 * albero da percorrere per disegnarla: c'e' l'elenco delle righe visibili, e
 * il livello di ognuna. E' la stessa rappresentazione che usa la lista del
 * toolkit — una riga di testo per riga — quindi fra il modello e cio' che si
 * vede non c'e' niente da tenere d'accordo.
 * --------------------------------------------------------------------------- */
typedef struct {
    char          nome[DIRENT_NAME_MAX];
    unsigned char liv;          /* 0 = la radice */
    unsigned char aperto;       /* 1 = i figli sono qui sotto */
} Nodo;

static Nodo         g_nodo[NODI_MAX];
static unsigned int g_nodi = 0;

/* =============================================================================
 * L'elenco di destra
 *
 * ! ADESSO SI TIENE TUTTO, NON PIU' SOLO IL FLAG DI DIRECTORY. Prima qui
 * c'erano il nome e «e' una cartella?», e la dimensione viveva in uno `static`
 * dentro leggi(): bastava, perche' la riga si costruiva una volta sola e poi
 * era la lista del toolkit a possederla. Da quando le colonne si possono
 * RIORDINARE cliccando, l'elenco va ricostruito senza tornare sul disco — e
 * per farlo i dati devono stare da qualche parte che non sia la stringa gia'
 * impaginata. Rileggere la directory a ogni clic sarebbe un accesso al disco
 * per un'operazione che non cambia niente di quel che c'e' sul disco.
 * ============================================================================= */
static unsigned char g_dir_flag[VOCI_MAX];
static char          g_nome[VOCI_MAX][DIRENT_NAME_MAX];
static unsigned int  g_dim[VOCI_MAX];
static time_t        g_data[VOCI_MAX];
static unsigned int  g_voci = 0;

/* La colonna su cui si ordina, e in che verso. Il verso e' per COLONNA e non
 * uno solo per tutte: chi ordina per data si aspetta il piu' recente in cima,
 * chi ordina per nome si aspetta la A — e ricordarsi il verso di ognuna
 * significa che tornare su una colonna la ritrova come la si era lasciata. */
static int g_ord = ORD_NOME;
static int g_giu[4] = { 0, 0, 0, 1 };   /* 1 = dal piu' grande al piu' piccolo */

static ExFinestra g_int_nome, g_int_tipo, g_int_dim, g_int_data;

/* Di quanto l'intestazione e' gia' spostata: vedi intestazione_incolonna(). */
static unsigned int g_int_margine = 0;

static void intestazione_incolonna(void);

/* ! DOPO UNA RICERCA I NOMI SONO PERCORSI INTERI, e va segnato: senza questo
 * l'Invio su un risultato cercherebbe il file dentro la directory corrente,
 * che e' proprio quella in cui non sta. */
static int g_da_ricerca = 0;

/* I segni, uno per voce. Vedi la testata: e' un carattere nella riga, non un
 * controllo nuovo del toolkit. */
static unsigned char g_segno[VOCI_MAX];

static char       g_dir[PERC_MAX] = "/";
static ExFinestra g_f, g_stato, g_albero, g_elenco, g_menu;
static char       g_avviso[120] = "";

/* =============================================================================
 * I percorsi
 *
 * ! UNA FUNZIONE SOLA CHE ATTACCA UN NOME A UNA DIRECTORY, e non uno strcat
 * scritto a mano ogni volta. Le due cose da sbagliare sono sempre le stesse —
 * la barra doppia quando la directory e' «/», e il traboccamento — e scritte
 * in sei punti si sbagliano in almeno uno.
 * ============================================================================= */
/* =============================================================================
 * Una riga a colonne
 *
 * ! SI SCRIVE DENTRO UNA RIGA GIA' PIENA DI SPAZI, invece di concatenare pezzi.
 * Concatenando, una colonna piu' larga del previsto spinge a destra tutte
 * quelle dopo — cioe' rompe l'allineamento con l'intestazione proprio nella
 * riga dove il dato e' interessante. Scrivendo a POSIZIONE, una colonna che
 * sfora si tronca e le altre restano dove l'intestazione promette.
 *
 * `a_destra` allinea a destra dentro la colonna: e' per i numeri, dove le
 * unita' incolonnate sono l'unico modo di confrontare due misure con l'occhio.
 *
 * ! E TRONCA, NON TRABOCCA, ed e' una lezione gia' pagata da questo file: qui
 * c'era uno `sprintf(riga, "[%s]", nome)` su un buffer di 80 byte, e un nome
 * di file puo' essere lungo 255. Scriveva oltre la fine dello stack. Non e'
 * mai successo perche' i nomi di prova sono corti — che e' il modo in cui
 * questi difetti restano nascosti per mesi. Il `[nome]` fra quadre e' sparito
 * con la colonna «tipo», che dice `<DIR>` a lettere; il limite di lunghezza,
 * che era il vero difetto, adesso lo impone la colonna.
 * ============================================================================= */
static void campo(char *riga, int x, int w, const char *testo, int a_destra)
{
    int l = (int)strlen(testo);
    int i, da = 0;

    if (l > w) l = w;
    if (a_destra) da = w - l;
    for (i = 0; i < l; i++) riga[x + da + i] = testo[i];
}

/* ! DI UN PERCORSO SI TIENE LA CODA, NON LA TESTA. Nei risultati di una
 * ricerca il nome E' il percorso intero, e i primi ventisei caratteri di
 * «/exwin/bin/...» sono uguali per tutti: troncare da destra darebbe una
 * colonna di righe identiche. Quel che distingue sta in fondo. */
static const char *coda(const char *s, int w)
{
    int l = (int)strlen(s);

    return (l > w) ? s + (l - w) : s;
}

/* "AAAA-MM-GG" e "HH:MM", o dei trattini. Stessa regola di /bin/ls, e per la
 * stessa ragione: un filesystem che non tiene le date darebbe 1970-01-01, che
 * sembra una data vera e non lo e'. */
static void data_ora(char *gg, unsigned int ng, char *hh, unsigned int nh,
                     time_t t)
{
    struct tm *tm = (t != 0) ? localtime(&t) : 0;

    if (tm == 0 || strftime(gg, ng, "%Y-%m-%d", tm) == 0) {
        strncpy(gg, "----------", ng - 1); gg[ng - 1] = '\0';
        strncpy(hh, "--:--",      nh - 1); hh[nh - 1] = '\0';
        return;
    }
    if (strftime(hh, nh, "%H:%M", tm) == 0) {
        strncpy(hh, "--:--", nh - 1); hh[nh - 1] = '\0';
    }
}

static void unisci(char *out, unsigned int max, const char *dir, const char *nome)
{
    unsigned int l;

    strncpy(out, dir, max - 1);
    out[max - 1] = '\0';
    l = (unsigned int)strlen(out);

    if (l == 0 || out[l - 1] != '/') {
        if (l + 1 < max) { out[l] = '/'; out[l + 1] = '\0'; }
    }
    strncat(out, nome, max - strlen(out) - 1);
}

static void percorso_nodo(int i, char *out, unsigned int max)
{
    int catena[NODI_MAX];
    int n = 0, k;
    int liv;

    out[0] = '\0';
    if (i < 0 || i >= (int)g_nodi) { strcpy(out, "/"); return; }

    /* ! SI RISALE CERCANDO IL PRIMO NODO DI LIVELLO MINORE, all'indietro. E'
     * corretto perche' i figli stanno SEMPRE subito dopo il padre: il primo
     * nodo di livello n-1 che si incontra tornando indietro e' per forza il
     * padre, e non ci sono puntatori da tenere aggiornati. */
    liv = (int)g_nodo[i].liv;
    catena[n++] = i;
    for (k = i - 1; k >= 0 && liv > 0 && n < NODI_MAX; k--)
        if ((int)g_nodo[k].liv == liv - 1) { catena[n++] = k; liv--; }

    for (k = n - 1; k >= 0; k--) {
        if (g_nodo[catena[k]].nome[0] == '\0') continue;   /* la radice */
        unisci(out, max, out, g_nodo[catena[k]].nome);
    }
    if (out[0] == '\0') strcpy(out, "/");
}

/* =============================================================================
 * Espandere e chiudere
 * ============================================================================= */
static void albero_chiudi(int i)
{
    int j = i + 1;

    if (i < 0 || i >= (int)g_nodi) return;

    while (j < (int)g_nodi && g_nodo[j].liv > g_nodo[i].liv) j++;
    if (j > i + 1) {
        memmove(&g_nodo[i + 1], &g_nodo[j],
                (unsigned int)((int)g_nodi - j) * sizeof(Nodo));
        g_nodi -= (unsigned int)(j - i - 1);
    }
    g_nodo[i].aperto = 0;
}

static void albero_espandi(int i)
{
    char      perc[PERC_MAX];
    DirEntry  v[8];
    int       start = 0, n, k, ins;
    unsigned char liv;

    if (i < 0 || i >= (int)g_nodi || g_nodo[i].aperto) return;

    percorso_nodo(i, perc, sizeof(perc));
    liv = (unsigned char)(g_nodo[i].liv + 1);
    ins = i + 1;

    /* ! IL BLOCCO E' DA OTTO, NON DA SEDICI. Un DirEntry sono 264 byte: un
     * blocco da sedici sono 4 KB di stack per chiamata, e questa funzione la
     * chiama anche chi copia una directory intera scendendo di livello in
     * livello. Otto bastano e costano la meta'. */
    while ((n = listdir_from(perc, v, 8, start)) > 0) {
        for (k = 0; k < n && g_nodi < NODI_MAX; k++) {
            if (!v[k].is_dir) continue;
            if (v[k].name[0] == '.' &&
                (v[k].name[1] == '\0' ||
                 (v[k].name[1] == '.' && v[k].name[2] == '\0'))) continue;

            memmove(&g_nodo[ins + 1], &g_nodo[ins],
                    (g_nodi - (unsigned int)ins) * sizeof(Nodo));
            g_nodi++;

            memset(&g_nodo[ins], 0, sizeof(Nodo));
            strncpy(g_nodo[ins].nome, v[k].name, DIRENT_NAME_MAX - 1);
            g_nodo[ins].nome[DIRENT_NAME_MAX - 1] = '\0';
            g_nodo[ins].liv = liv;
            ins++;
        }
        start += n;
        if (n < 8) break;
    }
    g_nodo[i].aperto = 1;
}

/* Riempie la lista di sinistra con i nodi, indentati. */
static void albero_mostra(void)
{
    unsigned int i;
    unsigned int scelta = ex_lista_scelta(g_albero);

    ex_lista_svuota(g_albero);

    for (i = 0; i < g_nodi; i++) {
        char riga[80];
        unsigned int k, p = 0;

        for (k = 0; k < g_nodo[i].liv && p + 2 < sizeof(riga); k++) {
            riga[p++] = ' '; riga[p++] = ' ';
        }
        /* ! IL SEGNO DICE SE C'E' ALTRO SOTTO, e non se il nodo E' una
         * directory: sono tutte directory. «+» vuol dire «non l'ho ancora
         * guardata dentro», «-» vuol dire «e' aperta». Chi apre una directory
         * vuota vede il segno cambiare e nessun figlio comparire, che e'
         * l'unica risposta onesta: era vuota. */
        if (p + 2 < sizeof(riga)) {
            riga[p++] = g_nodo[i].aperto ? '-' : '+';
            riga[p++] = ' ';
        }
        riga[p] = '\0';
        strncat(riga, g_nodo[i].nome[0] ? g_nodo[i].nome : "/",
                sizeof(riga) - strlen(riga) - 1);
        ex_lista_aggiungi(g_albero, riga);
    }

    if (scelta < g_nodi) ex_lista_scegli(g_albero, scelta);
}

/* =============================================================================
 * L'elenco di destra
 * ============================================================================= */
/* Due voci si scambiano di posto: tutte e quattro le colonne insieme, o le
 * righe si mescolano fra loro.
 *
 * ! E IL SEGNO VIAGGIA CON LA RIGA, che e' la ragione per cui sta scritto qui
 * e non in un elenco a parte. Un segno legato alla POSIZIONE si sposterebbe da
 * solo al primo clic sull'intestazione: si segnano tre file, si ordina per
 * data, e sono segnati altri tre — senza che niente lo dica. */
static void scambia(unsigned int a, unsigned int b)
{
    char         n[DIRENT_NAME_MAX];
    unsigned int d;
    time_t       t;
    unsigned char f;

    strcpy(n, g_nome[a]); strcpy(g_nome[a], g_nome[b]); strcpy(g_nome[b], n);
    d = g_dim[a];      g_dim[a]      = g_dim[b];      g_dim[b]      = d;
    t = g_data[a];     g_data[a]     = g_data[b];     g_data[b]     = t;
    f = g_dir_flag[a]; g_dir_flag[a] = g_dir_flag[b]; g_dir_flag[b] = f;
    f = g_segno[a];    g_segno[a]    = g_segno[b];    g_segno[b]    = f;
}

/* Confronto di nomi che non guarda maiuscole e minuscole.
 *
 * ! SERVE DAVVERO, E NON E' UN VEZZO: su FAT i nomi corti arrivano in
 * MAIUSCOLO e su ext2 come sono stati scritti. Con uno strcmp nudo, in una
 * directory mista tutti i nomi maiuscoli finirebbero prima di tutti i
 * minuscoli — un ordine alfabetico che alfabetico non e', e che cambia secondo
 * il filesystem invece che secondo i nomi. */
static int nome_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        int cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;

        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}

/* Rende <0 se `a` va prima di `b` secondo la colonna scelta. */
static int prima_di(unsigned int a, unsigned int b)
{
    int r = 0;

    /* ! LE DIRECTORY RESTANO SEMPRE IN CIMA, qualunque colonna si scelga, e
     * non e' l'ordinamento a deciderlo: e' la ragione per cui esistono le due
     * aree. A sinistra c'e' dove si e', a destra cosa c'e' — e le cartelle in
     * cui si puo' ENTRARE sono una terza cosa, che sparsa fra cento file non
     * si trova piu'. L'unica colonna che le mescola e' «Tipo», dove separarle
     * e' esattamente quel che si e' chiesto cliccando. */
    if (g_dir_flag[a] != g_dir_flag[b])
        return g_giu[ORD_TIPO] && g_ord == ORD_TIPO
               ? (int)g_dir_flag[a] - (int)g_dir_flag[b]
               : (int)g_dir_flag[b] - (int)g_dir_flag[a];

    switch (g_ord) {
    case ORD_DIM:
        /* ! FRA DUE DIRECTORY NON SI ORDINA PER DIMENSIONE, ed e' una
         * conseguenza di averla nascosta. La colonna mostra un trattino —
         * perche' il numero che i filesystem tengono li' non e' quanto pesa il
         * contenuto — ma il numero c'e' lo stesso, e ordinandoci sopra le
         * cartelle uscivano in un ordine che chi guarda non puo' spiegare:
         * dieci righe con lo stesso trattino, mescolate. Fra due trattini si
         * ordina per nome, che e' l'unica cosa che si vede. */
        if (g_dir_flag[a] && g_dir_flag[b]) { r = 0; break; }
        r = (g_dim[a] < g_dim[b]) ? -1 : (g_dim[a] > g_dim[b]) ? 1 : 0;
        break;
    case ORD_DATA:
        r = (g_data[a] < g_data[b]) ? -1 : (g_data[a] > g_data[b]) ? 1 : 0;
        break;
    default:
        break;
    }

    /* ! A PARITA' DI COLONNA SI ORDINA PER NOME, e non e' un di piu': in /bin
     * decine di file hanno la stessa data al minuto, perche' li ha scritti la
     * stessa `make`. Senza questo, due elenchi della stessa directory
     * potrebbero uscire in ordine diverso — e un elenco che cambia da solo fa
     * credere che sia cambiato il disco. */
    if (r == 0 && g_ord != ORD_NOME) {
        int n = nome_cmp(g_nome[a], g_nome[b]);

        if (n != 0) return n;           /* ! il verso NON si applica qui */
    }
    if (g_ord == ORD_NOME || r == 0) r = nome_cmp(g_nome[a], g_nome[b]);

    return g_giu[g_ord] ? -r : r;
}

/* ! UNA SELEZIONE A INSERIMENTO, E VA BENISSIMO. Le voci sono al massimo
 * VOCI_MAX = 512 e l'ordinamento si rifa' solo a un clic dell'utente: il
 * quadrato di 512 e' un quarto di milione di confronti, che su questa macchina
 * sono meno di quanto costa ridisegnare la lista. Un quicksort qui sarebbe
 * codice in piu' da rileggere per un tempo che nessuno misurerebbe. */
static void ordina(void)
{
    unsigned int i, j;

    for (i = 0; i + 1 < g_voci; i++)
        for (j = i + 1; j < g_voci; j++)
            if (prima_di(j, i) < 0) scambia(i, j);
}

/* Dai vettori alla lista del toolkit. La chiamano leggi(), la ricerca e ogni
 * clic sull'intestazione: e' l'unico posto dove una riga si impagina. */
/* Sposta i quattro pulsanti dell'intestazione di quanto la lista tiene per le
 * icone.
 *
 * ! SI CHIEDE ALLA LISTA, NON SI SA. Quanto sia larga una corsia di icone e'
 * roba del toolkit: scriverlo qui vorrebbe dire una costante copiata, e una
 * copia sbagliata il giorno che cambia. ex_lista_margine() rende 0 quando
 * nessuna riga ha un'icona — cioe' quando i file delle icone non sono
 * installati — e allora l'intestazione resta dov'e' sempre stata.
 *
 * ! E SI SPOSTA SOLO QUANDO CAMBIA. Questa funzione la chiama ogni riempimento
 * dell'elenco: muovere quattro controlli a ogni cambio di directory
 * vorrebbe dire quattro ridisegni per niente. */
static void intestazione_incolonna(void)
{
    unsigned int m = ex_lista_margine(g_elenco);
    int          d;

    if (m == g_int_margine) return;
    g_int_margine = m;
    d = (int)m;

    if (g_int_nome) ex_sposta(g_int_nome, INT_X(C_NOME_X) + d, INT_Y);
    if (g_int_tipo) ex_sposta(g_int_tipo, INT_X(C_TIPO_X) + d, INT_Y);
    if (g_int_dim)  ex_sposta(g_int_dim,  INT_X(C_DIM_X)  + d, INT_Y);
    if (g_int_data) ex_sposta(g_int_data, INT_X(C_DATA_X) + d, INT_Y);
}

/* =============================================================================
 * LE ICONE PER TIPO DI FILE
 *
 * ! IL TIPO SI DECIDE DAL NOME, E NON E' UN BUON MODO — e' l'unico che ci sia
 * senza aprire ogni file dell'elenco. Aprire cento file per decidere cento
 * icone vorrebbe dire cento letture a ogni cambio di directory, e su un
 * dischetto si sentirebbe. L'estensione mente (un .txt puo' contenere un PNG),
 * ma mente su un'ICONA: il danno e' un disegno sbagliato, non un file aperto
 * male.
 *
 * ! I FILE DELLE ICONE LI METTE CHI LI DISEGNA, in /exwin/icon/tipi/, col nome
 * che si legge qui sotto. Se non ci sono, l'elenco resta com'e' sempre stato:
 * il file manager deve funzionare anche il giorno prima che le icone esistano.
 * E' la stessa regola del pannello di exide.
 * ============================================================================= */
#define TIPI_N  7

static const char *const g_tipo_nome[TIPI_N] = {
    "cartella", "programma", "testo", "immagine", "archivio", "sorgente", "file"
};

/* Aperte una volta sola, alla prima directory che se ne serve. */
static ExIcona g_tipo_ic[TIPI_N];
static int     g_tipi_cercati = 0;

static void tipi_apri(void)
{
    static const char *const dove[] = {
        "/exwin/icon/tipi/",
        "/cdrom/exwin/icon/tipi/"
    };
    char p[PERC_MAX];
    int  t, d;

    if (g_tipi_cercati) return;
    g_tipi_cercati = 1;

    for (t = 0; t < TIPI_N; t++)
        for (d = 0; d < 2; d++) {
            if (snprintf(p, sizeof(p), "%s%s.ico", dove[d], g_tipo_nome[t])
                >= (int)sizeof(p)) continue;
            g_tipo_ic[t] = ex_icona_apri(p);
            if (g_tipo_ic[t]) break;
        }
}

/* L'estensione, minuscola, o "" se non ce n'e'. */
static void estensione(const char *nome, char *out, unsigned int max)
{
    const char *p = strrchr(nome, '.');
    unsigned int k = 0;

    out[0] = '\0';
    if (!p || p == nome) return;        /* ".profilo" non ha estensione */

    for (p++; *p && k + 1 < max; p++, k++)
        out[k] = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
    out[k] = '\0';
}

static int tipo_di(unsigned int i)
{
    char e[8];

    if (g_dir_flag[i]) return 0;        /* cartella */

    estensione(g_nome[i], e, sizeof(e));

    if (!strcmp(e, "txt") || !strcmp(e, "md")  || !strcmp(e, "cfg") ||
        !strcmp(e, "cnf") || !strcmp(e, "log") || !strcmp(e, "dis")) return 2;
    if (!strcmp(e, "bmp") || !strcmp(e, "png") || !strcmp(e, "jpg") ||
        !strcmp(e, "jpeg")|| !strcmp(e, "gif") || !strcmp(e, "ico")) return 3;
    if (!strcmp(e, "zip") || !strcmp(e, "gz")  || !strcmp(e, "tar")) return 4;
    if (!strcmp(e, "c")   || !strcmp(e, "h")   || !strcmp(e, "s")   ||
        !strcmp(e, "bas") || !strcmp(e, "sh")  || !strcmp(e, "py"))  return 5;

    /* ! UN PROGRAMMA NON HA UN'ESTENSIONE, SU QUESTO SISTEMA, e si riconosce
     * da dove sta: /bin, /dev e /exwin/bin. E' una regola grossolana e lo
     * resta finche' non ci sara' un modo di chiedere al VFS «questo e'
     * eseguibile?» — che oggi non c'e'. */
    if (!strncmp(g_dir, "/bin", 4) || !strncmp(g_dir, "/dev", 4) ||
        !strncmp(g_dir, "/exwin/bin", 10)) return 1;

    return 6;                           /* file e basta */
}

static void elenco_mostra(void)
{
    unsigned int i;

    ex_lista_svuota(g_elenco);
    tipi_apri();

    for (i = 0; i < g_voci; i++) {
        char riga[RIGA_CAR + 1];
        char gg[16], hh[8], dim[16];
        int  k;

        for (k = 0; k < RIGA_CAR; k++) riga[k] = ' ';
        riga[RIGA_CAR] = '\0';

        if (g_segno[i]) riga[C_SEGNO_X] = '*';

        campo(riga, C_NOME_X, C_NOME_W,
              g_da_ricerca ? coda(g_nome[i], C_NOME_W) : g_nome[i], 0);
        campo(riga, C_TIPO_X, C_TIPO_W,
              g_dir_flag[i] ? "<DIR>" : "<FILE>", 0);

        /* ! UNA DIRECTORY NON HA UNA DIMENSIONE DA MOSTRARE. Il numero che i
         * filesystem tengono li' e' la misura della voce sul disco — zero su
         * FAT — non quanto pesa il contenuto. Un trattino dice «la domanda non
         * si applica»; uno zero direbbe il falso. Stessa scelta di /bin/ls. */
        if (g_dir_flag[i]) strcpy(dim, "-");
        else               sprintf(dim, "%u", g_dim[i]);
        campo(riga, C_DIM_X, C_DIM_W, dim, 1);

        data_ora(gg, sizeof(gg), hh, sizeof(hh), g_data[i]);
        campo(riga, C_DATA_X, C_DATA_W, gg, 0);
        campo(riga, C_ORA_X,  C_ORA_W,  hh, 0);

        if (ex_lista_aggiungi(g_elenco, riga))
            ex_lista_icona(g_elenco, i, g_tipo_ic[tipo_di(i)]);
    }

    /* ! L'INTESTAZIONE SEGUE LE RIGHE. Se c'e' almeno un'icona la lista
     * indenta tutte le righe di due caratteri, e quattro pulsanti che
     * restassero fermi indicherebbero la colonna sbagliata. Si chiede alla
     * lista quanto ha tenuto: cosi' vale sia con le icone sia senza. */
    intestazione_incolonna();
}

static void leggi(const char *percorso)
{
    DirEntry v[8];
    int start = 0, n, i;
    unsigned int quante = 0;

    g_da_ricerca = 0;

    /* ! I SEGNI SONO DI QUESTE RIGHE, e queste righe stanno per essere
     * sostituite. Tenerli vorrebbe dire che entrando in un'altra directory
     * restano segnate le posizioni 2 e 5 — cioe' due file che nessuno ha mai
     * scelto, e su cui il prossimo comando agirebbe. */
    memset(g_segno, 0, sizeof(g_segno));

    while ((n = listdir_from(percorso, v, 8, start)) > 0) {
        for (i = 0; i < n && quante < VOCI_MAX; i++) {
            char        perc[PERC_MAX];
            struct stat st;

            if (v[i].name[0] == '.' && v[i].name[1] == '\0') continue;
            strncpy(g_nome[quante], v[i].name, DIRENT_NAME_MAX - 1);
            g_nome[quante][DIRENT_NAME_MAX - 1] = '\0';
            g_dim[quante]      = v[i].size;
            g_dir_flag[quante] = v[i].is_dir;

            /* ! LA DATA COSTA UNA stat() PER VOCE, e la voce di directory non
             * la porta: DirEntry ha nome, misura e «e' una cartella», e
             * basta. Allargarla vorrebbe dire toccare una struttura che
             * attraversa l'ABI della syscall ed e' duplicata a mano in tre
             * posti — cioe' ricostruire il bersaglio per una colonna. Qui il
             * prezzo si puo' pagare: una directory si legge quando qualcuno
             * ci entra, non in un ciclo.
             *
             * ! E SE stat() FALLISCE LA VOCE RESTA. Ce l'ha appena data la
             * directory: farla sparire perche' non se ne conosce la data
             * farebbe credere che il file non ci sia. La data diventa zero, e
             * zero si stampa con dei trattini. */
            unisci(perc, sizeof(perc), percorso, v[i].name);
            g_data[quante] = (stat(perc, &st) == 0) ? st.st_mtime : 0;

            quante++;
        }
        start += n;
        if (n < 8) break;
    }

    g_voci = quante;
    ordina();
    elenco_mostra();
}

/* Ridisegna la finestra intera. Definita piu' sotto, insieme allo stato: qui
 * serve perche' un'etichetta cambiata non si ridisegna da sola. */
static void ridisegna(void);

/* =============================================================================
 * L'intestazione cliccabile
 *
 * ! SONO QUATTRO PULSANTI, NON UN CONTROLLO NUOVO DEL TOOLKIT. E' la stessa
 * scelta dell'albero a sinistra, che e' «una lista con dentro l'indentazione»
 * e non un controllo albero: un'intestazione di colonne dentro exwin.so
 * vorrebbe dire un modello di colonne, un disegno suo, e la larghezza da
 * tenere d'accordo con la lista — cioe' un pezzo di toolkit che UNA sola
 * applicazione usa. Quattro pulsanti allineati alle colonne fanno la stessa
 * cosa con quel che c'e' gia', e un pulsante si sa gia' che si preme.
 *
 * ! E LA FRECCIA STA NELL'ETICHETTA, non in un disegno accanto. Chi guarda un
 * elenco ordinato deve poter rispondere a due domande — «per che cosa?» e «in
 * che verso?» — e la seconda senza indizi si risponde solo leggendo i dati e
 * indovinando. La freccia costa due caratteri.
 * ============================================================================= */
static void intestazione_aggiorna(void)
{
    static const char *nomi[4] = { "Nome", "Tipo", "Dimensione", "Data" };
    ExFinestra         q[4];
    int                i;

    q[0] = g_int_nome; q[1] = g_int_tipo; q[2] = g_int_dim; q[3] = g_int_data;

    for (i = 0; i < 4; i++) {
        char t[24];

        if (!q[i]) continue;
        if (i == g_ord) sprintf(t, "%s %s", nomi[i], g_giu[i] ? "v" : "^");
        else            sprintf(t, "%s", nomi[i]);
        ex_testo_metti(q[i], t);
    }
}

/* Un clic sull'intestazione. Sulla colonna gia' scelta ROVESCIA il verso; su
 * un'altra ci si sposta tenendo il verso che quella colonna aveva.
 *
 * ! IL RIDISEGNO ALLA FINE NON E' DI TROPPO, ED E' COSTATO UNA PROVA. La lista
 * si riempie da se' — ex_lista_svuota() e ex_lista_aggiungi() ridisegnano — ma
 * ex_testo_metti() su un CONTROLLO cambia solo la stringa: e' ex_titolo(), che
 * avvisa il server soltanto per una finestra di primo livello. Senza questa
 * riga l'elenco si riordinava davvero e la freccia restava dov'era: cioe' la
 * cosa peggiore, un'indicazione che dice il falso invece di non dire niente.
 * (E' lo stesso contratto che stato_aggiorna() seguiva gia', chiamata da
 * dentro ridisegna() e mai da sola.) */
static void ordina_per(int colonna)
{
    if (colonna == g_ord) g_giu[colonna] = !g_giu[colonna];
    else                  g_ord = colonna;

    intestazione_aggiorna();
    ordina();
    elenco_mostra();
    ridisegna();
}

/* Definita piu' sotto, insieme al gruppo su cui agiscono i comandi. */
static unsigned int quanti_segnati(void);

static void stato_aggiorna(void)
{
    char s[220];
    unsigned int segnati = quanti_segnati();

    /* ! QUANTE VOCI SONO SEGNATE VA SCRITTO, e non basta l'asterisco nella
     * riga: un segno messo in cima a un elenco lungo, dopo aver scorso, non
     * si vede piu'. E il comando dopo agisce su quello, non su cio' che si
     * ha davanti agli occhi. */
    if (g_avviso[0])   sprintf(s, "%s  -  %s", g_dir, g_avviso);
    else if (segnati)  sprintf(s, "%s  -  %u voci, %u segnate",
                               g_dir, g_voci, segnati);
    else               sprintf(s, "%s  -  %u voci", g_dir, g_voci);

    ex_testo_metti(g_stato, s);
}

static void ridisegna(void)
{
    stato_aggiorna();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);
}

/* =============================================================================
 * Andare da qualche parte
 *
 * ! UNA SOLA FUNZIONE PER \xabADESSO SIAMO QUI\xbb, chiamata dall'albero, dall'elenco
 * e da «Su». Tre strade che portano nello stesso posto e non passano dalla
 * stessa funzione si scollegano appena una delle tre cambia — e la prima a
 * cambiare sara' quella che qualcuno usa meno, quindi il difetto si vedra' per
 * ultimo.
 * ============================================================================= */
static void vai(const char *percorso)
{
    strncpy(g_dir, percorso, PERC_MAX - 1);
    g_dir[PERC_MAX - 1] = '\0';
    leggi(g_dir);
}

/* Trova nell'albero il nodo che corrisponde a `perc`, espandendo per strada.
 * Rende -1 se non ci si arriva. */
static int albero_apri_fino_a(const char *perc)
{
    int  i = 0;                 /* si parte dalla radice */
    char pezzo[DIRENT_NAME_MAX];
    unsigned int p = 0, k;

    while (perc[p] == '/') p++;

    while (perc[p]) {
        unsigned int q = 0;

        while (perc[p] && perc[p] != '/' && q < sizeof(pezzo) - 1)
            pezzo[q++] = perc[p++];
        pezzo[q] = '\0';
        while (perc[p] == '/') p++;
        if (!pezzo[0]) break;

        albero_espandi(i);

        /* Fra i figli diretti di `i` si cerca quello che si chiama cosi'. */
        {
            int trovato = -1;

            for (k = (unsigned int)i + 1; k < g_nodi; k++) {
                if (g_nodo[k].liv <= g_nodo[i].liv) break;
                if (g_nodo[k].liv == g_nodo[i].liv + 1 &&
                    strcmp(g_nodo[k].nome, pezzo) == 0) { trovato = (int)k; break; }
            }
            if (trovato < 0) return -1;
            i = trovato;
        }
    }
    return i;
}

/* =============================================================================
 * Aprire un file: lo si passa all'editor
 *
 * ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA: su un sistema installato l'albero
 * sta in /exwin, avviando dal CD sta sotto /cdrom. Stessa regola del program
 * manager, per la stessa ragione.
 * ============================================================================= */
static void apri_file(const char *percorso)
{
    static const char *editori[2] = {
        "/exwin/bin/edit", "/cdrom/exwin/bin/edit"
    };
    static char copia[PERC_MAX];
    char *argv[3];
    int i;

    strncpy(copia, percorso, PERC_MAX - 1);
    copia[PERC_MAX - 1] = '\0';

    argv[1] = copia;
    argv[2] = 0;

    for (i = 0; i < 2; i++) {
        argv[0] = (char *)editori[i];
        if (spawn_ex(editori[i], argv, 0, 0, 0) >= 0) {
            sprintf(g_avviso, "aperto con l'editor: %s", copia);
            return;
        }
    }
    strcpy(g_avviso, "l'editor non si trova: /exwin/bin/edit");
}

/* =============================================================================
 * Copiare
 *
 * ! IL BUFFER E' UNO SOLO E STA QUI FUORI, non nello stack: copia_albero()
 * scende di livello in livello, e un buffer da un kilobyte per chiamata
 * sarebbe un kilobyte per ogni directory di profondita'. Non e' rientrante, e
 * non deve esserlo: qui c'e' un solo copiatore per volta.
 * ============================================================================= */
static char g_buf[1024];

static int copia_file(const char *da, const char *a)
{
    int fa, fb, n;

    fa = open(da, O_RDONLY, 0);
    if (fa < 0) return 0;

    fb = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fb < 0) { close(fa); return 0; }

    while ((n = (int)read(fa, g_buf, sizeof(g_buf))) > 0)
        if ((int)write(fb, g_buf, (unsigned int)n) != n) {
            close(fa); close(fb);
            return 0;
        }

    close(fa);
    close(fb);
    return 1;
}

/* Rende quanti file ha copiato, o -1 se qualcosa e' andato storto. */
static int copia_albero(const char *da, const char *a, int giu)
{
    DirEntry v[8];
    int start = 0, n, i, fatti = 0;

    /* ! IL TETTO ALLA PROFONDITA' NON E' PRUDENZA ESAGERATA. Questa funzione
     * ricorre, e ogni livello si porta dietro il suo blocco di DirEntry: senza
     * un tetto, una directory profonda o — peggio — un anello nel filesystem
     * finirebbe lo stack. Fermarsi dicendolo e' meglio che morire. */
    if (giu > PROFONDITA) return -1;

    if (mkdir(a, 0755) < 0 && errno != EEXIST) return -1;

    while ((n = listdir_from(da, v, 8, start)) > 0) {
        for (i = 0; i < n; i++) {
            char sorg[PERC_MAX], dest[PERC_MAX];
            int r;

            if (v[i].name[0] == '.' &&
                (v[i].name[1] == '\0' ||
                 (v[i].name[1] == '.' && v[i].name[2] == '\0'))) continue;

            unisci(sorg, sizeof(sorg), da, v[i].name);
            unisci(dest, sizeof(dest), a,  v[i].name);

            if (v[i].is_dir) {
                r = copia_albero(sorg, dest, giu + 1);
                if (r < 0) return -1;
                fatti += r;
            } else {
                if (!copia_file(sorg, dest)) return -1;
                fatti++;
            }
        }
        start += n;
        if (n < 8) break;
    }
    return fatti;
}

/* =============================================================================
 * IL GRUPPO SU CUI AGISCE UN COMANDO
 *
 * ! LA REGOLA E' UNA SOLA: i segnati, e se non c'e' nessun segno la riga dove
 * sta il cursore. Non sono due modi di lavorare da tenere a mente — «con la
 * selezione» e «senza» — ma uno che degenera bene nell'altro: chi non preme
 * mai la barra spaziatrice trova il programma di ieri.
 * ============================================================================= */
static unsigned int g_set[VOCI_MAX];
static unsigned int g_set_n = 0;

/* Il percorso intero della voce i. Dopo una ricerca il nome E' gia' il
 * percorso: vedi g_da_ricerca. */
static int voce_percorso(unsigned int i, char *out, unsigned int max)
{
    if (i >= g_voci) return 0;

    if (g_da_ricerca) {
        strncpy(out, g_nome[i], max - 1);
        out[max - 1] = '\0';
    } else {
        unisci(out, max, g_dir, g_nome[i]);
    }
    return 1;
}

/* L'ultimo pezzo di un percorso: e' il nome che la voce prende nella
 * destinazione. Serve per i risultati di una ricerca, dove il nome e' un
 * percorso intero e copiarlo cosi' com'e' darebbe una destinazione assurda. */
static const char *base(const char *perc)
{
    const char *b = perc;

    while (*perc) { if (*perc == '/') b = perc + 1; perc++; }
    return b;
}

static unsigned int quanti_segnati(void)
{
    unsigned int i, n = 0;

    for (i = 0; i < g_voci; i++) if (g_segno[i]) n++;
    return n;
}

static void set_costruisci(void)
{
    unsigned int i, s;

    g_set_n = 0;
    for (i = 0; i < g_voci; i++)
        if (g_segno[i] && g_set_n < VOCI_MAX) g_set[g_set_n++] = i;
    if (g_set_n > 0) return;

    s = ex_lista_scelta(g_elenco);
    if (s < g_voci) g_set[g_set_n++] = s;
}

/* =============================================================================
 * Le tre domande che una copia ricorsiva fa e una copia singola no
 * ============================================================================= */

/* ! «DENTRO SE STESSO» SI GUARDA PRIMA DI COMINCIARE, NON A META'. Copiare /a
 * dentro /a/b vuol dire creare /a/b/a, poi /a/b/a/b, e avanti cosi' finche' il
 * disco non e' pieno: il tetto di PROFONDITA' ferma la ricorsione, ma intanto
 * i file sono gia' stati scritti e vanno tolti a mano. Il controllo costa uno
 * strncmp e si fa quando non e' ancora successo niente. */
static int e_dentro(const char *fuori, const char *dentro)
{
    unsigned int l = (unsigned int)strlen(fuori);

    if (strncmp(fuori, dentro, l) != 0) return 0;
    return dentro[l] == '/' || dentro[l] == '\0';
}

/* Quanti file e quanti byte c'e' sotto un percorso. ! LA DOMANDA PRIMA DI
 * CANCELLARE LA FA QUESTA: «sei sicuro?» non e' una domanda — chi la legge sa
 * gia' di aver premuto Canc, e quel che non sa e' che dietro una directory
 * chiusa ci sono centoquaranta file.
 *
 * ! SOTTO PROFONDITA' LIVELLI IL CONTO E' PER DIFETTO, e lo e' per la stessa
 * ragione per cui la cancellazione li' si ferma dicendolo: la funzione ricorre
 * e lo stack ha una fine. Un conto per difetto davanti a una domanda e' meno
 * grave di uno stack finito in mezzo a una cancellazione. */
static void conta(const char *perc, int e_dir, int giu,
                  unsigned int *nfile, unsigned int *nbyte)
{
    DirEntry v[8];
    int start = 0, n, i;

    if (!e_dir) {
        struct stat st;

        (*nfile)++;
        if (stat(perc, &st) == 0) *nbyte += (unsigned int)st.st_size;
        return;
    }
    if (giu > PROFONDITA) return;

    while ((n = listdir_from(perc, v, 8, start)) > 0) {
        for (i = 0; i < n; i++) {
            char sotto[PERC_MAX];

            if (v[i].name[0] == '.' &&
                (v[i].name[1] == '\0' ||
                 (v[i].name[1] == '.' && v[i].name[2] == '\0'))) continue;

            unisci(sotto, sizeof(sotto), perc, v[i].name);
            conta(sotto, v[i].is_dir, giu + 1, nfile, nbyte);
        }
        start += n;
        if (n < 8) break;
    }
}

/* Cancella un file, o una directory con tutto quel che ha dentro. Rende
 * quante voci ha tolto, o -1.
 *
 * ! SI RILEGGE SEMPRE DALL'INIZIO, e non e' spreco: `listdir_from` ha un
 * indice di partenza, e cancellare una voce fa scalare tutte quelle dopo. Un
 * ciclo che avanzasse l'indice salterebbe un file ogni due — e il sintomo
 * sarebbe una directory che non si vuota e un rmdir che fallisce, cioe' un
 * guasto che sembra del filesystem. Si rilegge finche' si fanno progressi. */
static int cancella_albero(const char *perc, int e_dir, int giu)
{
    DirEntry v[8];
    int n, i, tolti = 0;

    if (!e_dir) return (unlink(perc) == 0) ? 1 : -1;
    if (giu > PROFONDITA) return -1;

    for (;;) {
        int avanzato = 0;

        n = listdir_from(perc, v, 8, 0);
        if (n <= 0) break;

        for (i = 0; i < n; i++) {
            char sotto[PERC_MAX];
            int  r;

            if (v[i].name[0] == '.' &&
                (v[i].name[1] == '\0' ||
                 (v[i].name[1] == '.' && v[i].name[2] == '\0'))) continue;

            unisci(sotto, sizeof(sotto), perc, v[i].name);
            r = cancella_albero(sotto, v[i].is_dir, giu + 1);
            if (r < 0) return -1;
            tolti += r;
            avanzato = 1;
        }
        /* Restano solo «.» e «..»: la directory e' vuota. */
        if (!avanzato) break;
    }

    if (rmdir(perc) != 0) return -1;
    return tolti + 1;
}

/* =============================================================================
 * Copiare sopra un file che c'e' gia'
 *
 * ! IL VFS TRONCA LA DESTINAZIONE QUANDO L'APRE, e questa e' una lezione gia'
 * pagata altrove: una copia che fallisce a meta' su un file che esisteva non
 * lascia il vecchio file, lascia mezzo file nuovo. Cioe' il caso in cui si
 * perde qualcosa e' proprio quello in cui si aveva gia' una copia buona.
 *
 * Quindi si scrive accanto, con un nome temporaneo, e solo quando la copia e'
 * finita per intero si sostituisce. ! E LA SOSTITUZIONE E' unlink PIU' rename,
 * non rename da solo: la nostra rename() NON sovrascrive la destinazione
 * (EEXIST, vedi libc.h), e non e' un capriccio — e' il prezzo della garanzia
 * che i blocchi non si spostino.
 *
 * ! IL TEMPORANEO E' IN OTTO PUNTO TRE E MAIUSCOLO, e sta nella STESSA
 * directory della destinazione. Nella stessa perche' rename() non attraversa
 * i montaggi; in otto punto tre perche' su FAT i nomi lunghi, da noi, sono in
 * sola lettura — un temporaneo che non si puo' creare renderebbe impossibile
 * copiare proprio sui dischetti.
 * ============================================================================= */
static int copia_file_sicura(const char *da, const char *a, int *mezzo)
{
    struct stat st;
    char tmp[PERC_MAX];
    int  i;

    *mezzo = 0;

    /* Se la destinazione non c'e', non c'e' niente da proteggere. */
    if (stat(a, &st) != 0) return copia_file(da, a);

    strncpy(tmp, a, PERC_MAX - 1);
    tmp[PERC_MAX - 1] = '\0';
    i = (int)strlen(tmp);
    while (i > 0 && tmp[i - 1] != '/') i--;
    if ((unsigned int)i + 10 >= PERC_MAX) return 0;
    tmp[i] = '\0';
    strcat(tmp, "FMTMP.TMP");

    if (!copia_file(da, tmp)) { unlink(tmp); return 0; }
    if (unlink(a) != 0)       { unlink(tmp); return 0; }
    if (rename(tmp, a) != 0) {
        /* ! QUI IL VECCHIO NON C'E' PIU' E IL NUOVO HA UN ALTRO NOME. E' la
         * sola finestra in cui questa strada e' peggio di una scrittura
         * diretta, ed e' larga una rename() dentro la stessa directory. Chi
         * lo legge sa dove sono i suoi byte, che e' il minimo dovuto. */
        *mezzo = 1;
        return 0;
    }
    return 1;
}

/* =============================================================================
 * Copiare e spostare
 * ============================================================================= */
/* Rende 1 se ha fatto, 0 se non c'era niente da fare, e un numero negativo se
 * e' andata storta: -1 la copia, -2 dentro se stesso, -3 copiato ma non
 * tolto, -4 la sostituzione lasciata a meta'. */
static int fai_uno(const char *sorg, int e_dir, const char *dest, int sposta,
                   unsigned int *nfile)
{
    int r, mezzo = 0;

    if (strcmp(sorg, dest) == 0) return 0;
    if (e_dir && e_dentro(sorg, dest)) return -2;

    /* ! SPOSTARE DENTRO LA STESSA DIRECTORY NON MUOVE UN BYTE, e provarlo per
     * primo non e' un'ottimizzazione: rename() non sposta i blocchi, quindi
     * non puo' fallire a meta' lasciando due copie mezze scritte. Fuori dalla
     * directory la nostra rename() rende ENOSYS (vedi libc.h) e allora non
     * resta che copiare e cancellare — che e' un'altra cosa, e infatti puo'
     * fallire in mezzo. */
    if (sposta && rename(sorg, dest) == 0) { (*nfile)++; return 1; }

    if (e_dir) {
        r = copia_albero(sorg, dest, 0);
        if (r < 0) return -1;
        *nfile += (unsigned int)r;
    } else {
        if (!copia_file_sicura(sorg, dest, &mezzo)) return mezzo ? -4 : -1;
        (*nfile)++;
    }

    /* ! L'ORIGINALE SI TOGLIE SOLO A COPIA FINITA. Uno spostamento e' una
     * copia piu' una cancellazione, e l'ordine fra le due e' tutta la
     * differenza fra «e' andata male» e «l'ho perso». */
    if (sposta && cancella_albero(sorg, e_dir, 0) < 0) return -3;

    return 1;
}

/* La destinazione di una voce del gruppo: il percorso battuto quando la voce
 * e' una sola, altrimenti quel percorso piu' il nome della voce. */
static void destinazione_di(unsigned int i, const char *dest, int uno_solo,
                            char *out, unsigned int max)
{
    if (uno_solo) {
        strncpy(out, dest, max - 1);
        out[max - 1] = '\0';
    } else {
        unisci(out, max, dest, base(g_nome[i]));
    }
}

static void comando_copia(int sposta)
{
    char dest[PERC_MAX], sorg[PERC_MAX], d[PERC_MAX];
    struct stat st;
    unsigned int i, nfile = 0, esistono = 0, fatte = 0;
    int uno_solo, falliti = 0, saltati = 0;

    set_costruisci();
    if (g_set_n == 0) {
        strcpy(g_avviso, "non c'e' niente di scelto a destra");
        return;
    }
    uno_solo = (g_set_n == 1);

    /* ! LA DESTINAZIONE ARRIVA GIA' SCRITTA. Con una voce sola si parte dal
     * suo percorso, perche' quasi sempre si vuole lo stesso nome in un'altra
     * directory e riscrivere un nome lungo e' il modo piu' facile di
     * sbagliarlo. Con piu' voci quel nome non esiste: si parte da dove si e'.
     *
     * ! E CON PIU' VOCI LA DESTINAZIONE E' UNA DIRECTORY CHE ESISTE. Dieci
     * file non possono diventare un file solo, e creare la directory per conto
     * nostro vorrebbe dire indovinare cosa si e' battuto — un nome nuovo o un
     * nome sbagliato. Meglio dirlo. */
    if (uno_solo) voce_percorso(g_set[0], dest, sizeof(dest));
    else {
        strncpy(dest, g_dir, PERC_MAX - 1);
        dest[PERC_MAX - 1] = '\0';
    }

    if (!ex_dlg_salva(dest, PERC_MAX)) {
        strcpy(g_avviso, sposta ? "spostamento annullato" : "copia annullata");
        return;
    }

    /* ! UNA VOCE SOLA SU UNA DIRECTORY CHE ESISTE VUOL DIRE «DENTRO», come su
     * ogni cp da cinquant'anni: chi sceglie /tmp nel dialogo non sta chiedendo
     * di sostituire /tmp con il suo file — cosa che oltretutto non si puo'
     * fare. Da qui in giu' e' un gruppo come un altro. */
    if (uno_solo && stat(dest, &st) == 0 && S_ISDIR(st.st_mode)) uno_solo = 0;

    if (!uno_solo && (stat(dest, &st) != 0 || !S_ISDIR(st.st_mode))) {
        sprintf(g_avviso, "%u voci vogliono una directory che esiste, e %s non lo e'",
                g_set_n, dest);
        return;
    }

    /* ! SI CHIEDE UNA VOLTA SOLA, PRIMA, E NON UNA PER FILE. Con venti file
     * una domanda per ognuno e' una domanda a cui si risponde senza leggere —
     * cioe' peggio di non chiedere. Il conto si fa con una stat per voce, che
     * costa quanto leggere la directory. */
    for (i = 0; i < g_set_n; i++) {
        destinazione_di(g_set[i], dest, uno_solo, d, sizeof(d));
        if (stat(d, &st) == 0) esistono++;
    }
    if (esistono > 0) {
        char t[200];

        sprintf(t, "Nella destinazione %u voci esistono gia'.  "
                   "Le sostituisco con quelle scelte?", esistono);
        if (!ex_dlg_conferma("Esiste gia'", t, "Sostituisci", "Annulla")) {
            strcpy(g_avviso, "annullato: non ho toccato niente");
            return;
        }
    }

    for (i = 0; i < g_set_n; i++) {
        int r;

        voce_percorso(g_set[i], sorg, sizeof(sorg));
        destinazione_di(g_set[i], dest, uno_solo, d, sizeof(d));

        r = fai_uno(sorg, g_dir_flag[g_set[i]], d, sposta, &nfile);
        if (r == 1) { fatte++; continue; }
        if (r == 0) { saltati++; continue; }
        if (r == -2) {
            sprintf(g_avviso, "%s sta dentro se stesso: non la tocco", sorg);
            falliti++;
            continue;
        }
        if (r == -3) {
            sprintf(g_avviso, "copiato %s ma l'originale non si cancella: %s",
                    sorg, strerror(errno));
            falliti++;
            continue;
        }
        if (r == -4) {
            sprintf(g_avviso, "ATTENZIONE: %s e' nel file FMTMP.TMP accanto "
                              "alla destinazione", sorg);
            falliti++;
            continue;
        }
        if (r < 0) falliti++;
    }

    /* ! SPOSTANDO SI CONTANO LE VOCI, COPIANDO I FILE, e non e' pignoleria:
     * uno spostamento dentro la stessa directory e' una rename(), che di file
     * non ne tocca nessuno — «spostati 0 file» sarebbe una bugia su un lavoro
     * riuscito. Le voci sono quel che l'utente ha scelto, e quelle si contano
     * in tutt'e due i casi. */
    if (falliti == 0 && saltati == 0) {
        if (sposta) sprintf(g_avviso, "spostate %u voci in %s", fatte, dest);
        else        sprintf(g_avviso, "copiati %u file in %s", nfile, dest);
    } else if (!g_avviso[0]) {
        sprintf(g_avviso, "%s: %u voci fatte, %d non riuscite, %d saltate",
                sposta ? "spostamento" : "copia", fatte, falliti, saltati);
    }

    leggi(g_dir);
}

/* =============================================================================
 * Cancellare
 * ============================================================================= */
static void comando_cancella(void)
{
    char perc[PERC_MAX], t[220];
    unsigned int i, nfile = 0, nbyte = 0, tolti = 0;
    int falliti = 0;

    set_costruisci();
    if (g_set_n == 0) {
        strcpy(g_avviso, "non c'e' niente di scelto a destra");
        return;
    }

    for (i = 0; i < g_set_n; i++) {
        voce_percorso(g_set[i], perc, sizeof(perc));
        conta(perc, g_dir_flag[g_set[i]], 0, &nfile, &nbyte);
    }

    /* ! LA DOMANDA DICE QUANTI E QUANTO, e con una voce sola dice anche QUALE.
     * Un conto senza nome davanti a un file solo fa fermare a chiedersi
     * «quale?», ed e' proprio il momento in cui si preme Invio per scoprirlo. */
    if (g_set_n == 1) {
        voce_percorso(g_set[0], perc, sizeof(perc));
        sprintf(t, "Cancello %s: %u file, %u byte.  Non si torna indietro.",
                base(perc), nfile, nbyte);
    } else {
        sprintf(t, "Cancello %u voci scelte: %u file, %u byte.  "
                   "Non si torna indietro.", g_set_n, nfile, nbyte);
    }

    if (!ex_dlg_conferma("Cancella", t, "Cancella", "Annulla")) {
        strcpy(g_avviso, "cancellazione annullata");
        return;
    }

    for (i = 0; i < g_set_n; i++) {
        int r;

        voce_percorso(g_set[i], perc, sizeof(perc));
        r = cancella_albero(perc, g_dir_flag[g_set[i]], 0);
        if (r < 0) falliti++;
        else       tolti += (unsigned int)r;
    }

    if (falliti == 0) sprintf(g_avviso, "cancellate %u voci", tolti);
    else sprintf(g_avviso, "cancellate %u voci, %d non si sono potute togliere: %s",
                 tolti, falliti, strerror(errno));

    leggi(g_dir);
}

/* =============================================================================
 * A new directory
 *
 * ! IT IS BORN UNDER THE ONE PICKED IN THE TREE, NOT UNDER THE ONE SHOWN ON THE
 * RIGHT, and it was asked for that way for a reason that only shows when the
 * two differ: after a search the list on the right shows hits scattered all
 * over the disk, and "in here" no longer means anything. The tree, instead,
 * always points at ONE place.
 *
 * ! AND THE PLACE IS WRITTEN IN THE QUESTION. Creating a directory is about to
 * change the disk: seeing the path in the window, before typing the name, is
 * the difference between creating it where you meant to and finding out later.
 *
 * ! THE DIALOG IS NOT NEW: ex_dlg_riga is exactly this window - title,
 * question, one box, two buttons - and it is the one "Cerca" uses.
 * ============================================================================= */
static void comando_nuova_dir(void)
{
    unsigned int s = ex_lista_scelta(g_albero);
    char  dove[PERC_MAX], perc[PERC_MAX], domanda[PERC_MAX + 64];
    static char nome[DIRENT_NAME_MAX] = "";

    if (s >= g_nodi) {
        strcpy(g_avviso, "scegli prima una cartella nell'albero a sinistra");
        return;
    }
    percorso_nodo((int)s, dove, sizeof(dove));

    sprintf(domanda, "Il nome della cartella nuova, dentro %s:", dove);
    /* ! AND THE BUTTON SAYS "CREA", not "Va bene". On 22 September 2026 it said
     * "Va bene" because the captions of ex_dlg_riga were fixed, and it was
     * written here that this would change the day that file was touched for
     * something else: that day came (ex_dlg_chiedi). A button that names the
     * action is read without re-reading the question. */
    if (!ex_dlg_chiedi("Nuova directory", domanda, "Crea",
                       nome, sizeof(nome))) {
        strcpy(g_avviso, "non ho creato niente");
        return;
    }
    if (!nome[0]) {
        strcpy(g_avviso, "non hai scritto nessun nome");
        return;
    }

    unisci(perc, sizeof(perc), dove, nome);

    if (mkdir(perc, 0755) != 0) {
        /* ! "IT ALREADY EXISTS" IS SAID, NOT SWALLOWED: whoever typed a name
         * that is taken wants to know at once, not when they walk into it. */
        sprintf(g_avviso, "non creata: %s", strerror(errno));
        return;
    }

    sprintf(g_avviso, "creata %s", perc);

    /* The tree has one child more: that node is closed and reopened, which is
     * the shortest way to read it again. And the list on the right is reread
     * only if it is looking at that very directory. */
    if (g_nodo[s].aperto) { albero_chiudi((int)s); albero_espandi((int)s); }
    albero_mostra();
    ex_lista_scegli(g_albero, s);

    if (strcmp(dove, g_dir) == 0) leggi(g_dir);
}

/* =============================================================================
 * I segni
 * ============================================================================= */
static void segna_toggle(void)
{
    unsigned int s = ex_lista_scelta(g_elenco);

    if (s >= g_voci) return;

    g_segno[s] = (unsigned char)!g_segno[s];
    elenco_mostra();

    /* ! E IL CURSORE SCENDE DI UNA RIGA, come su ogni file manager da
     * trent'anni: segnare dieci file di fila dev'essere un gesto ripetuto
     * dieci volte, non dieci gesti da due tasti. */
    ex_lista_scegli(g_elenco, (s + 1 < g_voci) ? s + 1 : s);
}

static void segna_tutti(int si)
{
    unsigned int s = ex_lista_scelta(g_elenco);
    unsigned int i;

    for (i = 0; i < g_voci; i++) g_segno[i] = (unsigned char)(si ? 1 : 0);

    /* elenco_mostra() svuota la lista, e con lei la riga scelta: si rimette
     * dov'era, o il cursore salterebbe in cima a ogni «segna tutto». */
    elenco_mostra();
    if (s < g_voci) ex_lista_scegli(g_elenco, s);
}

/* =============================================================================
 * Cercare
 *
 * ! I RISULTATI VANNO NELL'AREA DI DESTRA, non in una finestra nuova. Un
 * elenco di risultati e' un elenco di file: farne un posto a parte vorrebbe
 * dire un secondo modo di aprirli, di copiarli e di guardarli. Cosi' invece un
 * risultato si apre con l'Invio come qualunque altra voce — e per far tornare
 * l'elenco vero basta scegliere una directory a sinistra.
 * ============================================================================= */
static void cerca_giu(const char *dir, const char *pezzo, int giu,
                      unsigned int *quanti)
{
    DirEntry v[8];
    int start = 0, n, i;

    if (giu > PROFONDITA || *quanti >= VOCI_MAX) return;

    while ((n = listdir_from(dir, v, 8, start)) > 0) {
        for (i = 0; i < n && *quanti < VOCI_MAX; i++) {
            char perc[PERC_MAX];

            if (v[i].name[0] == '.' &&
                (v[i].name[1] == '\0' ||
                 (v[i].name[1] == '.' && v[i].name[2] == '\0'))) continue;

            unisci(perc, sizeof(perc), dir, v[i].name);

            if (strstr(v[i].name, pezzo) != 0) {
                struct stat st;

                strncpy(g_nome[*quanti], perc, DIRENT_NAME_MAX - 1);
                g_nome[*quanti][DIRENT_NAME_MAX - 1] = '\0';
                g_dir_flag[*quanti] = v[i].is_dir;
                g_dim[*quanti]      = v[i].size;
                g_data[*quanti]     = (stat(perc, &st) == 0) ? st.st_mtime : 0;
                (*quanti)++;
            }

            if (v[i].is_dir) cerca_giu(perc, pezzo, giu + 1, quanti);
        }
        start += n;
        if (n < 8) break;
    }
}

static void comando_cerca(void)
{
    static char pezzo[64] = "";
    unsigned int quanti = 0;

    if (!ex_dlg_riga("Cerca", "Parte del nome da cercare, qui sotto:",
                     pezzo, sizeof(pezzo))) {
        strcpy(g_avviso, "ricerca annullata");
        return;
    }
    if (!pezzo[0]) {
        strcpy(g_avviso, "non hai scritto niente da cercare");
        return;
    }

    /* Stessa ragione di leggi(): i segni sono delle righe che c'erano, e
     * queste sono righe nuove. */
    memset(g_segno, 0, sizeof(g_segno));

    cerca_giu(g_dir, pezzo, 0, &quanti);

    g_voci = quanti;
    g_da_ricerca = 1;

    /* ! I RISULTATI SI ORDINANO COME L'ELENCO, con la colonna che l'utente ha
     * scelto: una ricerca che rende quaranta file non e' meno bisognosa di
     * ordine di una directory che ne ha quaranta. E le cartelle restano in
     * cima anche qui, perche' li' si puo' entrare. */
    ordina();
    elenco_mostra();
    sprintf(g_avviso, "%s: %u trovati sotto %s", pezzo, quanti, g_dir);
}

/* =============================================================================
 * Le scelte
 * ============================================================================= */
/* ! IL SEGNO STA IN UNA COLONNA CHE SI SA CONTARE. La riga la costruisce
 * albero_mostra(): due spazi per livello, poi «+» o «-», poi uno spazio, poi
 * il nome. Quindi il segno di un nodo di livello n e' nella colonna 2n, e lo
 * spazio subito dopo nella 2n+1 — che si accetta anche lui, perche' un bersaglio
 * di otto pixel si manca. Se albero_mostra() cambiasse indentazione, questa
 * dovrebbe cambiare con lei: sono le due meta' della stessa convenzione. */
static int sul_segno(unsigned int nodo, int col)
{
    int c;

    if (col < 0 || nodo >= g_nodi) return 0;      /* -1 = venuto da tastiera */
    c = (int)g_nodo[nodo].liv * 2;
    return col == c || col == c + 1;
}

/* `apri` = si e' chiesto di aprire (Invio o doppio clic); `col` = la colonna
 * del clic dentro la riga, -1 se il comando viene dalla tastiera. */
static void scegli_albero(int apri, int col)
{
    unsigned int s = ex_lista_scelta(g_albero);
    char perc[PERC_MAX];
    int  segno;

    if (s >= g_nodi) return;

    segno = sul_segno(s, col);

    percorso_nodo((int)s, perc, sizeof(perc));
    vai(perc);

    /* ! IL CLIC MOSTRA, L'APERTURA ESPANDE. Sono due desideri diversi e vanno
     * distinti: chi scorre l'albero con le frecce vuole vedere il contenuto
     * cambiare a destra senza che l'albero gli si apra sotto le mani, e chi
     * batte Invio o fa doppio clic ha chiesto proprio di scendere.
     *
     * ! E IL SEGNO E' LA TERZA VIA, quella che ci si aspetta guardandolo. Un
     * «+» disegnato accanto a una directory dice «qui sotto c'e' dell'altro»:
     * chi ce lo vede lo preme, e prima di oggi non succedeva niente — il segno
     * era un disegno e basta. Premerlo apre SENZA doppio clic, che e' il senso
     * di averlo messo li'. */
    if (!apri && !segno) return;

    if (g_nodo[s].aperto) albero_chiudi((int)s);
    else                  albero_espandi((int)s);
    albero_mostra();
    ex_lista_scegli(g_albero, s);
}

static void scegli_elenco(int apri)
{
    char         perc[PERC_MAX];
    unsigned int s = ex_lista_scelta(g_elenco);

    if (!apri) return;
    if (!voce_percorso(s, perc, sizeof(perc))) return;

    if (!g_dir_flag[s]) { apri_file(perc); return; }

    /* Una directory scelta a destra si apre a destra E si apre a sinistra:
     * sono la stessa directory, e vederla in un posto solo vorrebbe dire un
     * albero che dice una cosa e un elenco che ne dice un'altra. */
    vai(perc);
    {
        int nodo = albero_apri_fino_a(perc);

        if (nodo >= 0) {
            albero_espandi(nodo);
            albero_mostra();
            ex_lista_scegli(g_albero, (unsigned int)nodo);
        } else {
            albero_mostra();
        }
    }
}

/* =============================================================================
 * La disposizione, che cambia con la finestra
 * ============================================================================= */
static void disponi(int w, int h)
{
    int alt = h - MENU_H - 4 - BASSO;

    if (alt < 40) alt = 40;

    ex_sposta(g_albero, 4, MENU_H + 4);
    ex_misura(g_albero, ALBERO_W, alt);

    ex_sposta(g_elenco, ALBERO_W + 10, MENU_H + 4);
    ex_misura(g_elenco, w - ALBERO_W - 14, alt);

    ex_sposta(g_stato, 6, h - 22);
    ex_misura(g_stato, w - 12, 16);
}

static void istruzioni(void)
{
    ex_dlg_avviso("Istruzioni",
                  "A sinistra l'albero, a destra il contenuto.  Le frecce "
                  "scelgono, Tab passa da un'area all'altra, Invio o doppio "
                  "clic espande una directory o apre un file.  Il segno + "
                  "dell'albero si preme con un clic solo.  F10 apre i menu.  "
                  "LA BARRA SPAZIATRICE SEGNA la riga e scende di una: Copia "
                  "(Ctrl+C), Sposta (Ctrl+X) e Cancella (Canc) agiscono su "
                  "tutte le righe segnate, e se non ce n'e' nessuna sulla "
                  "riga dov'e' il cursore.  Vale per i file e per le cartelle "
                  "intere.  Con piu' di una voce la destinazione dev'essere "
                  "una directory che esiste.  Prima di sostituire e prima di "
                  "cancellare si viene avvisati, con quanti file e quanti "
                  "byte.  Cerca guarda sotto la directory corrente.  "
                  "L'elenco ha quattro colonne - nome, tipo, dimensione e "
                  "data - e la fascia di pulsanti sopra le ordina: un clic "
                  "sceglie la colonna, un secondo clic sulla stessa rovescia "
                  "il verso, e la freccia nell'etichetta dice qual e'.  Le "
                  "cartelle restano in cima qualunque colonna si scelga, "
                  "tranne quando si ordina per Tipo.  La data e' quella di "
                  "MODIFICA: quella di creazione i filesystem non la tengono.");
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "File manager", VERSIONE_APP,
                 "Il file manager di EX-OS, sul toolkit ExWin.  L'albero non "
                 "e' un controllo nuovo: e' una lista con dentro "
                 "l'indentazione, e i nodi stanno in un vettore nell'ordine "
                 "in cui si vedono.  E l'intestazione delle colonne non e' un "
                 "controllo nuovo: sono quattro pulsanti allineati alle "
                 "colonne della riga, che escono dalle stesse costanti.");
    ex_dlg_avviso("Informazioni su", t);
}

/* =============================================================================
 * La procedura
 * ============================================================================= */
static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_COMANDO:
        g_avviso[0] = '\0';

        /* ! DALLE LISTE ARRIVA ANCHE COME, non solo COSA: lp dice se si e'
         * chiesto di APRIRE — Invio o doppio clic — e in quale COLONNA della
         * riga e' caduto il clic. Senza il primo, un clic per guardare e un
         * Invio per entrare sarebbero indistinguibili, e l'albero si aprirebbe
         * sotto le dita di chi voleva solo dare un'occhiata; senza la seconda,
         * il «+» dell'albero resterebbe un disegno da guardare. */
        if (wp == ID_ALBERO) { scegli_albero(EX_APRIRE(lp), EX_COL(lp)); break; }
        if (wp == ID_ELENCO) { scegli_elenco(EX_APRIRE(lp)); break; }

        if (wp == ID_APRI)     { scegli_elenco(1); break; }
        if (wp == ID_AGGIORNA) { leggi(g_dir);     break; }
        if (wp == ID_ESCI)     { ex_esci(0); return 0; }
        if (wp == ID_SU) {
            char su[PERC_MAX];
            int i = (int)strlen(g_dir);

            strncpy(su, g_dir, PERC_MAX - 1);
            su[PERC_MAX - 1] = '\0';
            while (i > 1 && su[i - 1] != '/') i--;
            if (i > 1) i--;
            if (i == 0) i = 1;
            su[i] = '\0';
            vai(su);
            {
                int nodo = albero_apri_fino_a(su);
                if (nodo >= 0) ex_lista_scegli(g_albero, (unsigned int)nodo);
            }
            break;
        }

        if (wp == ID_COPIA)    { comando_copia(0);    break; }
        if (wp == ID_SPOSTA)   { comando_copia(1);    break; }
        if (wp == ID_CANCELLA) { comando_cancella();  break; }
        if (wp == ID_CERCA)    { comando_cerca();     break; }

        if (wp == ID_SEGNA)        { segna_toggle();  break; }
        if (wp == ID_SEGNA_TUTTI)  { segna_tutti(1);  break; }
        if (wp == ID_SEGNA_NIENTE) { segna_tutti(0);  break; }
        if (wp == ID_NUOVA_DIR)    { comando_nuova_dir(); break; }

        if (wp == ID_ORD_NOME) { ordina_per(ORD_NOME); break; }
        if (wp == ID_ORD_TIPO) { ordina_per(ORD_TIPO); break; }
        if (wp == ID_ORD_DIM)  { ordina_per(ORD_DIM);  break; }
        if (wp == ID_ORD_DATA) { ordina_per(ORD_DATA); break; }

        if (wp == ID_ISTRUZIONI) { istruzioni();   break; }
        if (wp == ID_INFO)       { informazioni(); break; }
        return 0;

    case EXM_TASTO:
        /* ! Tab PASSA DA UN'AREA ALL'ALTRA, e lo fa gia' il toolkit: qui non
         * c'e' niente da scrivere. Resta questo ramo perche' le scorciatoie
         * dei comandi sono dell'applicazione — un menu non cattura i tasti. */
        if (wp & KBD_MOD_CTRL) {
            unsigned int c = wp & KBD_KEY_MASK;

            if (c == 'c' || c == 'C') { comando_copia(0); break; }
            if (c == 'x' || c == 'X') { comando_copia(1); break; }
            if (c == 'f' || c == 'F') { comando_cerca();  break; }
            if (c == 'q' || c == 'Q') { ex_esci(0); return 0; }
        }

        /* ! LA BARRA E IL TASTO CANC VALGONO SOLO SE IL FUOCO E' SULL'ELENCO.
         * Nell'albero non c'e' niente da segnare e non c'e' niente da
         * cancellare: rubare li' due tasti vorrebbe dire che la barra, che in
         * una lista scorre, smette di farlo in meta' finestra e nessuno sa
         * perche'. */
        if (ex_fuoco_chi(g_f) == g_elenco) {
            unsigned int c = wp & KBD_KEY_MASK;

            if (c == ' ')       { segna_toggle();    break; }
            if (c == KBD_K_DEL) { comando_cancella(); break; }
        }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_MISURA:
        disponi(EX_X(lp), EX_Y(lp));
        break;

    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    if (argc >= 2) {
        strncpy(g_dir, argv[1], PERC_MAX - 1);
        g_dir[PERC_MAX - 1] = '\0';
    }

    g_f = ex_crea("finestra", "File manager",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("filemgr: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Apri\tInvio",   ID_APRI);
    ex_menu_voce(g_menu, "File", "Su",            ID_SU);
    ex_menu_voce(g_menu, "File", "Aggiorna",      ID_AGGIORNA);
    ex_menu_voce(g_menu, "File", "-",             0);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q",  ID_ESCI);

    ex_menu_voce(g_menu, "Comandi", "Nuova directory",  ID_NUOVA_DIR);
    ex_menu_voce(g_menu, "Comandi", "-",                0);
    ex_menu_voce(g_menu, "Comandi", "Copia\tCtrl+C",    ID_COPIA);
    ex_menu_voce(g_menu, "Comandi", "Sposta\tCtrl+X",   ID_SPOSTA);
    ex_menu_voce(g_menu, "Comandi", "Cancella\tCanc",   ID_CANCELLA);
    ex_menu_voce(g_menu, "Comandi", "-",                0);
    ex_menu_voce(g_menu, "Comandi", "Segna\tSpazio",    ID_SEGNA);
    ex_menu_voce(g_menu, "Comandi", "Segna tutto",      ID_SEGNA_TUTTI);
    ex_menu_voce(g_menu, "Comandi", "Nessun segno",     ID_SEGNA_NIENTE);
    ex_menu_voce(g_menu, "Comandi", "-",                0);
    ex_menu_voce(g_menu, "Comandi", "Cerca\tCtrl+F",    ID_CERCA);

    ex_menu_voce(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);

    {
        /* ! LE COORDINATE DEI PULSANTI ESCONO DALLE STESSE COSTANTI DELLA
         * RIGA. C_NOME_X e compagni sono in caratteri; la lista disegna a
         * passo di 8 pixel e lascia 4 pixel di margine a sinistra. Moltiplicare
         * qui e' l'unico modo perche' l'intestazione indichi davvero la colonna
         * che nomina: due serie di numeri si scollano alla prima modifica. */
        const int y = INT_Y;

        ex_crea("etichetta", "Cartelle", EX_FIGLIO,
                8, y + 2, ALBERO_W - 8, 14, g_f, 0, 0);

        g_int_nome = ex_crea("pulsante", "Nome", EX_FIGLIO,
                             INT_X(C_NOME_X), y,
                             INT_W(C_NOME_X, C_TIPO_X), INTEST_H,
                             g_f, ID_ORD_NOME, 0);
        g_int_tipo = ex_crea("pulsante", "Tipo", EX_FIGLIO,
                             INT_X(C_TIPO_X), y,
                             INT_W(C_TIPO_X, C_DIM_X), INTEST_H,
                             g_f, ID_ORD_TIPO, 0);
        g_int_dim  = ex_crea("pulsante", "Dimensione", EX_FIGLIO,
                             INT_X(C_DIM_X), y,
                             INT_W(C_DIM_X, C_DATA_X), INTEST_H,
                             g_f, ID_ORD_DIM, 0);
        /* ! DATA E ORA SONO DUE COLONNE E UN PULSANTE SOLO: sono lo stesso
         * numero scritto in due pezzi, e chi ordina «per ora» senza la data
         * mescolerebbe due giorni diversi. */
        g_int_data = ex_crea("pulsante", "Data", EX_FIGLIO,
                             INT_X(C_DATA_X), y,
                             INT_W(C_DATA_X, RIGA_CAR), INTEST_H,
                             g_f, ID_ORD_DATA, 0);
    }

    g_albero = ex_crea("lista", "", EX_FIGLIO,
                       4, MENU_H + 4 + INTEST_H, ALBERO_W,
                       FIN_H - MENU_H - 4 - INTEST_H - BASSO,
                       g_f, ID_ALBERO, 0);
    g_elenco = ex_crea("lista", "", EX_FIGLIO,
                       ALBERO_W + 10, MENU_H + 4 + INTEST_H, ELENCO_W,
                       FIN_H - MENU_H - 4 - INTEST_H - BASSO,
                       g_f, ID_ELENCO, 0);
    if (!g_albero || !g_elenco) {
        printf("filemgr: non riesco a creare le due aree\n");
        return 1;
    }
    intestazione_aggiorna();

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    /* La radice: il nodo senza nome, da cui discende ogni percorso. */
    memset(&g_nodo[0], 0, sizeof(Nodo));
    g_nodi = 1;
    albero_espandi(0);

    {
        int nodo = albero_apri_fino_a(g_dir);

        albero_mostra();
        if (nodo >= 0) ex_lista_scegli(g_albero, (unsigned int)nodo);
    }

    leggi(g_dir);

    /* ! IL FUOCO ALL'ALBERO, ESPLICITAMENTE. Senza andrebbe al primo controllo
     * creato che lo accetta — che e' comunque l'albero, ma per caso: il giorno
     * che si aggiunge un pulsante prima, le frecce smetterebbero di muovere
     * qualcosa senza che nessuno abbia toccato l'albero. */
    ex_fuoco(g_albero);

    ridisegna();
    printf("filemgr: %s, %u voci\n", g_dir, g_voci);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* =============================================================================
 * QUELLO CHE MANCA, DICHIARATO
 *
 * ! NIENTE RINOMINA. Spostare dentro la stessa directory con un altro nome
 * e' gia' una rinomina — fai_uno() prova rename() per primo proprio per
 * questo — ma passa da un dialogo di salvataggio, che per cambiare due
 * lettere a un nome e' la strada lunga. Una voce «Rinomina» che chieda il
 * solo nome e' mezz'ora di lavoro, ed e' l'unica delle quattro che manca.
 *
 * ! NON SI TORNA INDIETRO DA UNA CANCELLAZIONE. Non c'e' cestino, e la difesa
 * e' tutta nella domanda che si fa prima: percio' la domanda dice quanti file
 * e quanti byte, e non «sei sicuro?».
 *
 * ! NON SI VEDE A CHE PUNTO E'. Copiando, spostando o cancellando una
 * directory grossa la finestra resta ferma finche' non ha finito: il ciclo dei
 * messaggi e' fermo dentro la ricorsione. Per farlo si dovrebbe lavorare un
 * pezzo per giro del ciclo, e allora servirebbe uno stato del lavoro in corso
 * — cioe' l'unica parte di questo compito che e' davvero un lavoro grosso.
 *
 * ! E IL CONTO DAVANTI ALLA DOMANDA SI FERMA A PROFONDITA' LIVELLI, come la
 * cancellazione: sotto, il numero e' per difetto. Un conto per difetto davanti
 * a una domanda e' meno grave di uno stack finito in mezzo a un lavoro.
 *
 * ! LA RICERCA SCENDE AL MASSIMO DI OTTO LIVELLI e si ferma a 512 risultati.
 * Sono due tetti dichiarati, non due limiti scoperti dopo: la funzione ricorre
 * e ogni livello si porta dietro il suo blocco di DirEntry.
 *
 * ! E L'ALBERO TIENE 128 NODI IN TUTTO, aperti insieme. Espandendo mezzo disco
 * si smette di aggiungerne: e' un vettore, non una lista, e un vettore ha una
 * fine.
 * ============================================================================= */

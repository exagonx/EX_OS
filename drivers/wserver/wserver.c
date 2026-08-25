/* =============================================================================
 * drivers/wserver/wserver.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il server a finestre — gradino 1 di DIREZIONE.md
 *
 *     /exwin/bin/wserver        compone le finestre, muove il puntatore
 *     /exwin/bin/wserver -v     dice tutto quello che fa
 *
 * ! GIRA IN RING 3, ED E' IL PUNTO. La direttiva 2 di DIREZIONE.md dice che
 * la grafica sta in spazio utente: quando questo processo muore, muore lui.
 * Kernel, scheduler, console seriale e tastiera restano vivi, e lo schermo si
 * rimette con /bin/testo — che si digita alla cieca ed e' la rete di
 * sicurezza costruita apposta prima di scrivere questo file.
 *
 * ! NON E' UN DRIVER, E DAL 19 AGOSTO 2026 NON LO SEMBRA NEMMENO PIU'. Si
 * chiamava /dev/wserver.drv per una ragione sola — mmio_map() e' riservata
 * agli eseguibili caricati da un file *.drv — e quella ragione e' caduta il 17
 * agosto, quando il framebuffer ha smesso di passare da mmio_map: adesso c'e'
 * fb_map(), che non prende argomenti, mappa SOLO il framebuffer e non chiede
 * nessun privilegio. Questo file non guida nessuna periferica e non registra
 * un servizio di driver: il nome era un lasciapassare, non una descrizione.
 *
 * ! ED E' QUEL NOME CHE TENEVA LA GRAFICA FUORI DALLA MULTIUTENZA. /dev e' di
 * root, quindi un utente normale non poteva eseguire il server — e il nome di
 * servizio PER UTENTE qui accanto in win_proto.h («root registra wserver,
 * chiunque altro <uid>:wserver») descriveva una situazione che non si poteva
 * verificare. Misurato il 19 agosto, con un utente vero:
 *
 *     uid=1000(tizio)
 *     exwin
 *     ELF: '/dev/wserver.drv' non eseguibile da questo utente (err=-13)
 *
 * ! E LA RIPARAZIONE SBAGLIATA ERA A PORTATA DI MANO: allargare i permessi di
 * quel file. Avrebbe dato a chiunque `is_driver`, cioe' ioport_bind e
 * dma_alloc — la capacita' larga al posto di quella stretta. Si e' tolto il
 * nome, non la barriera.
 *
 * -----------------------------------------------------------------------------
 * ! NON DISEGNA IL CONTENUTO DELLE FINESTRE, LO COMPONE
 *
 * Ogni finestra e' una zona di memoria condivisa che il CLIENT riempie di
 * pixel. Qui dentro si mette il bordo, la barra del titolo, si impilano nel
 * giusto ordine e si copia nel framebuffer. Le tre ragioni stanno in testa a
 * win_proto.h, e la piu' importante e' che un client che sbaglia a disegnare
 * rovina la propria finestra e non lo schermo.
 *
 * ! ED E' ANCHE PERCHE' I FORMATI DI IMMAGINE NON SONO QUI. Un
 * decodificatore JPG o PNG e' migliaia di righe che leggono dati venuti da
 * fuori: nel server sarebbe un difetto di tutte le applicazioni insieme. Sta
 * nella libreria del client, che disegna nella propria zona — e il server non
 * sa nemmeno che esistano le immagini.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "win_proto.h"

/* +0.001 a ogni modifica: `wserver -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("wserver", "0.001");

#define FINESTRE_MAX    16
#define BARRA_H         20

/* =============================================================================
 * ! IL BORDO E' DI DUE PIXEL, E OGNUNO DEI DUE HA UN MESTIERE. Fino al 18
 * agosto 2026 era uno solo, di un grigio quasi nero, e una finestra sembrava
 * un rettangolo ritagliato invece di un oggetto posato sulla scrivania.
 *
 *     il pixel di FUORI    sporge: luce sopra e a sinistra, ombra sotto e a
 *                          destra. E' il telaio che si alza dallo sfondo
 *     il pixel di DENTRO   rientra, e disegna il buco in cui sta l'area del
 *                          client: cosi' il contenuto sembra INCASSATO nel
 *                          telaio invece che appoggiato sopra
 *
 * ! LA LUCE VIENE DA SOPRA A SINISTRA, SEMPRE — la stessa convenzione del
 * toolkit (vedi EX_LUCE in exwin.h). Se il telaio e i controlli dentro la
 * prendessero da due parti diverse, uno dei due sembrerebbe premuto.
 * ============================================================================= */
#define BORDO           2

/* =============================================================================
 * LA PRESA PER RIDIMENSIONARE — l'angolo in basso a destra
 *
 * ! LA PRESA STA DENTRO L'AREA DEL CLIENT, E SE NE PRENDE UN ANGOLO. E' la
 * cosa scomoda di questa scelta e va detta invece che nascosta: il bordo e'
 * spesso UN pixel, e un pixel non si acchiappa col mouse. Allargare il bordo
 * per farci stare una presa vorrebbe dire rifare l'aritmetica della cornice
 * dappertutto — e sottrarre comunque quello spazio al client, solo su tutti e
 * quattro i lati invece che in un angolo.
 *
 * Quindi la presa si disegna SOPRA i pixel del client, dopo averli copiati, e
 * la si prende solo alle finestre che hanno chiesto WIN_ST_RIDIM: chi non la
 * vuole non perde niente.
 *
 * ! E SI RIDIMENSIONA A RILASCIO, NON MENTRE SI TRASCINA. Ogni cambio di
 * misura e' una zona condivisa nuova da creare, riempire e consegnare al
 * client: farlo a ogni movimento del mouse vorrebbe dire decine di zone al
 * secondo e altrettanti messaggi a un client che non fa in tempo a leggerli.
 * Mentre si trascina si disegna il contorno di dove andra' a finire, che e'
 * quello che facevano tutti quando la memoria costava — e per la stessa
 * ragione, che qui vale ancora.
 * ============================================================================= */
#define PRESA           14      /* il lato del quadrato che si acchiappa */
#define MIN_W           80      /* sotto questa non e' piu' una finestra */
#define MIN_H           48
#define MISURA_TENTATIVI 10     /* quante volte si ripete WIN_MSG_MISURATA */

/* I colori, in ARGB. Sono pochi e stanno qui: una scrivania che cambia
 * aspetto non deve voler dire cercarli sparsi nel file. */
#define C_SFONDO        0x00204060
/* ! LA BARRA ATTIVA NON E' DELLO STESSO BLU DI EX_BLU, e dal 18 agosto 2026
 * nemmeno per un pixel. Erano identici, e la coincidenza costava due cose: una
 * riga scelta in una lista e la barra del titolo si confondevano a colpo
 * d'occhio, e — peggio — nessuno guardando una fotografia dello schermo poteva
 * dire quale blu fosse quale. Le prove che misurano le finestre contando i
 * pixel (tools/misura_finestre.py) hanno bisogno di un colore che voglia dire
 * UNA cosa sola. */
#define C_BARRA_ATT     0x001E4D7D
#define C_BARRA_INA     0x00808080
#define C_TELAIO        0x00C0C0C0  /* il grigio del telaio, come il pannello */
#define C_LUCE          0x00FFFFFF
#define C_OMBRA         0x00000000
#define C_TITOLO        0x00FFFFFF
#define C_TITOLO_INA    0x00E0E0E0
#define C_CHIUDI        0x00C04040
#define C_CLIENT        0x00C0C0C0
#define C_PRESA         0x00404040
#define C_CONTORNO      0x00FFFF80

extern const unsigned char font8x16[256 * 16];

typedef struct {
    unsigned int usata;
    unsigned int id;
    unsigned int pid;
    unsigned int x, y, w, h;        /* l'area del CLIENT, senza cornice */
    unsigned int stile;
    char         titolo[WIN_TITOLO_LEN];
    unsigned int zona_virt;         /* i pixel del client, ARGB */
    unsigned int zona_byte;
    unsigned int giro;              /* quante volte ha cambiato zona */
    /* ! LA NOTIZIA DEL CAMBIO SI RIPETE FINCHE' NON ARRIVA, e questo e' il
     * contatore dei tentativi. Un evento perso e' un clic perso; un
     * WIN_MSG_MISURATA perso e' una finestra che disegna per sempre in una
     * zona che nessuno guarda piu' — cioe' un rettangolo congelato che
     * risponde ai tasti. Sono due cose diverse e vanno trattate diversamente. */
    unsigned int da_dire;
} Finestra;

static Finestra g_fin[FINESTRE_MAX];
static unsigned int g_ordine[FINESTRE_MAX];     /* dal fondo alla cima */
static unsigned int g_n_ordine = 0;
static unsigned int g_prossimo_id = 1;
static unsigned int g_verboso = 0;
static unsigned int g_cascata = 0;      /* quante finestre ha gia' posato */

/* Il nome con cui ci si registra: «wserver» per root, «<uid>:wserver» per
 * chiunque altro. Vedi win_nome_servizio() in win_proto.h. */
static char g_servizio[32];

/* Lo schermo */
static unsigned char *g_fb = 0;
static unsigned int g_fb_passo = 0, g_fb_w = 0, g_fb_h = 0, g_fb_bit = 0;

/* Il puntatore */
static int g_px = 0, g_py = 0;
static unsigned int g_bottoni = 0, g_bottoni_prec = 0;

/* Il trascinamento in corso */
static int g_trascino = -1;             /* indice, -1 = nessuno */
static int g_tr_dx = 0, g_tr_dy = 0;

/* ! IL FUOCO SI DICHIARA QUI E SI GESTISCE PIU' SOTTO, perche' lo guarda anche
 * il compositore: e' il fuoco a decidere quale barra del titolo si disegna
 * attiva. Vedi il commento in componi(). */
static int g_fuoco = -1;        /* indice della finestra che riceve i tasti */

/* Il ridimensionamento in corso: mentre dura si disegna solo il contorno, e la
 * misura vera si da' al rilascio. Il perche' sta accanto a #define PRESA. */
static int g_ridim = -1;                /* indice, -1 = nessuno */
static unsigned int g_rw = 0, g_rh = 0; /* la misura che si sta scegliendo */

/* =============================================================================
 * ! IL MOVIMENTO SI MANDA SOLO CON UN BOTTONE PREMUTO, E SOLO A CHI HA PRESO
 * IL BOTTONE GIU'. Sono due limiti insieme, e ognuno toglie un guaio:
 *
 *   - mandare OGNI movimento vorrebbe dire riempire la mailbox di un client
 *     fermo — e' profonda quattro messaggi — per una cosa che quasi nessuna
 *     applicazione guarda. Col bottone premuto invece qualcuno sta facendo
 *     qualcosa apposta, e il flusso finisce quando alza il dito;
 *   - mandarlo a CHI STA SOTTO IL PUNTATORE vorrebbe dire che chi trascina la
 *     selezione fuori dalla propria finestra la vede finire in un'altra. Il
 *     trascinamento appartiene a chi l'ha cominciato, dall'inizio alla fine.
 *
 * ! E IL BOTTONE SU VA ALLO STESSO, ANCHE FUORI DALLA SUA FINESTRA. Prima si
 * mandava solo se il puntatore era ancora sopra una finestra: chi premeva
 * dentro e rilasciava fuori non riceveva mai il «su», e restava a trascinare
 * per sempre una selezione che nessuno aveva chiuso.
 * ============================================================================= */
static int g_giu_su = -1;               /* chi ha ricevuto il bottone giu' */
static int g_px_prec = 0, g_py_prec = 0;

/* ! SI RICOMPONE SOLO QUANDO QUALCOSA E' CAMBIATO, e non e' un'ottimizzazione
 * prematura: e' una CORREZIONE. Ricomponendo a ogni giro, 480000 pixel per
 * fotogramma tenevano il processo occupato tanto a lungo che una richiesta di
 * un client restava in mailbox per secondi — e il client si arrendeva. Il
 * sintomo era «il server a finestre non risponde» da un'applicazione avviata
 * in sfondo, mentre la stessa in primo piano funzionava: perche' li' la shell
 * era ferma e lasciava tutta la CPU al server.
 *
 * ! ED E' IL GENERE DI DIFETTO CHE LA PROVA COMODA NON VEDE. Provando a mano,
 * in primo piano, andava. */
/* =============================================================================
 * QUANTO SCHERMO SI RIFA'
 *
 * ! IL PREDEFINITO E' «TUTTO», E VA TENUTO COSI'. Una regione sporca sbagliata
 * per difetto lascia pixel vecchi sullo schermo — un difetto che non si vede
 * dove e' stato fatto e che si manifesta come «ogni tanto resta un pezzo di
 * finestra». Ogni ragione per ricomporre che non sappia dire ESATTAMENTE cosa
 * ha cambiato dichiara tutto lo schermo, e paga quello che pagava prima.
 *
 * ! SI STRINGE UN CASO SOLO, ED E' QUELLO CHE COSTA: il movimento del
 * puntatore. Il puntatore e' otto pixel per dodici e si muove in continuazione;
 * ridipingere 800x600 per spostarlo di due pixel era il grosso del lavoro di
 * questo server. Tutto il resto — una finestra che si aggiorna, una che si
 * sposta, una che nasce — passa ancora da «tutto», e restringerlo e' un lavoro
 * a se' che va fatto un caso per volta guardando i pixel.
 *
 * ! E LE DUE POSIZIONI VANNO SPORCATE TUTT'E DUE: dove il puntatore ERA (per
 * cancellarlo) e dove E' (per disegnarlo). Sporcare solo la seconda lascia una
 * scia — lo stesso difetto che il cursore della console aveva per un'altra
 * ragione.
 * ============================================================================= */
static unsigned int g_sporco = 1;       /* c'e' qualcosa da rifare */
static unsigned int g_sp_tutto = 1;     /* ...e non si sa cosa: tutto */
static unsigned int g_sp_x = 0, g_sp_y = 0, g_sp_x1 = 0, g_sp_y1 = 0;

static void sporca_tutto(void)
{
    g_sporco = 1;
    g_sp_tutto = 1;
}

static void sporca(int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;

    if (w <= 0 || h <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    if (!g_sporco || (!g_sp_tutto && g_sp_x1 == g_sp_x)) {
        g_sp_x = (unsigned int)x;  g_sp_y = (unsigned int)y;
        g_sp_x1 = (unsigned int)x1; g_sp_y1 = (unsigned int)y1;
    } else if (!g_sp_tutto) {
        if ((unsigned int)x  < g_sp_x)  g_sp_x  = (unsigned int)x;
        if ((unsigned int)y  < g_sp_y)  g_sp_y  = (unsigned int)y;
        if ((unsigned int)x1 > g_sp_x1) g_sp_x1 = (unsigned int)x1;
        if ((unsigned int)y1 > g_sp_y1) g_sp_y1 = (unsigned int)y1;
    }
    g_sporco = 1;
}

/* Il rettangolo del puntatore, con un pixel d'aria intorno. */
static void sporca_puntatore(int x, int y)
{
    sporca(x - 1, y - 1, 8 + 2, 12 + 2);
}


/* =============================================================================
 * MMX NEL COMPOSITORE — otto byte per volta invece di quattro
 *
 * ! IL COMPOSITORE E' L'UNICO POSTO DEL SISTEMA DOVE LA LARGHEZZA DELLA COPIA
 * SI VEDE. A 800x600x32 un fotogramma sono 1,83 MB scritti nel framebuffer, e
 * si riscrive tutto a ogni cambiamento: e' gia' stato il difetto che teneva il
 * server occupato tanto da far scadere le richieste dei client. Ovunque altro
 * in EX-OS si copiano decine di byte e non conta niente.
 *
 * ! I REGISTRI MMX SONO QUELLI DELL'x87, e per questo si puo' fare: il kernel
 * salva lo stato FPU al cambio di contesto (fnsave/fxsave, con commutazione
 * pigra via CR0_TS), quindi copre anche MMX. Senza quel salvataggio, due
 * processi che usassero MMX si calpesterebbero i registri a vicenda.
 *
 * ! E SI CHIAMA emms ALLA FINE DI OGNI GIRO. Dopo un'istruzione MMX i registri
 * x87 restano marcati «in uso»: la prima istruzione in virgola mobile che
 * arriva dopo — anche in un ALTRO processo, se lo scheduler entra prima —
 * trova uno stack che non e' suo. E' l'errore classico di MMX, e non da'
 * nessun sintomo finche' qualcuno non usa la virgola mobile.
 *
 * ! SI CONTROLLA A RUNTIME E SI RIPIEGA. La CPU di base dichiarata ha MMX, ma
 * un Pentium liscio no: senza il controllo, su quella macchina il server
 * morirebbe con un'istruzione non valida invece di andare piu' piano.
 * ============================================================================= */
static int g_mmx = 0;

static int mmx_c_e(void)
{
    unsigned int a = 0, b = 0, c = 0, d = 0;

    /* CPUID funzione 1: il bit 23 di EDX dice MMX. Che CPUID stessa esista e'
     * garantito dalla CPU di base (Pentium): sotto quella non si arriva
     * nemmeno qui, perche' il resto del sistema e' compilato per lei. */
    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(1));
    return (d & (1u << 23)) != 0;
}

/* Riempie `n` pixel a 32 bit col colore `c`. */
static void mmx_riempi32(unsigned int *d, unsigned int n, unsigned int c)
{
    unsigned int coppie = n >> 1;

    if (coppie) {
        __asm__ __volatile__(
            "movd      %2, %%mm0\n\t"
            "punpckldq %%mm0, %%mm0\n\t"   /* mm0 = colore:colore */
            "1:\n\t"
            "movq      %%mm0, (%0)\n\t"
            "addl      $8, %0\n\t"
            "decl      %1\n\t"
            "jnz       1b\n\t"
            "emms"
            : "+r"(d), "+r"(coppie)
            : "rm"(c)
            : "memory", "cc");
    }

    /* Il pixel dispari in coda, se c'e'. */
    if (n & 1u) *d = c;
}

/* Copia `n` pixel a 32 bit. */
static void mmx_copia32(unsigned int *d, const unsigned int *s, unsigned int n)
{
    unsigned int coppie = n >> 1;

    if (coppie) {
        __asm__ __volatile__(
            "1:\n\t"
            "movq      (%1), %%mm0\n\t"
            "movq      %%mm0, (%0)\n\t"
            "addl      $8, %0\n\t"
            "addl      $8, %1\n\t"
            "decl      %2\n\t"
            "jnz       1b\n\t"
            "emms"
            : "+r"(d), "+r"(s), "+r"(coppie)
            :
            : "memory", "cc");
    }

    if (n & 1u) *d = *s;
}

/* -----------------------------------------------------------------------------
 * Lo schermo, un pixel per volta
 *
 * ! LA CONVERSIONE STA QUI E SOLO QUI. E' l'unico posto del sistema che sa se
 * lo schermo e' a 16, 24 o 32 bit: i client disegnano sempre in ARGB a 32 e
 * non lo sanno. Un toolkit che dovesse conoscere il formato dello schermo
 * avrebbe sei strade da provare invece di una.
 * --------------------------------------------------------------------------- */
/* =============================================================================
 * IL RITAGLIO — si ricompone solo cio' che e' cambiato
 *
 * ! IL RITAGLIO STA NELLE DUE PRIMITIVE, NON NEI CHIAMANTI, ed e' l'unica
 * disposizione che non si puo' dimenticare: `px()` e `riempi()` sono le sole
 * due strade per arrivare al framebuffer, quindi tutto cio' che disegna —
 * cornici, prese, contorni, il puntatore — eredita il ritaglio senza sapere
 * che esiste. Metterlo in componi() vorrebbe dire ricordarselo a ogni funzione
 * nuova, e prima o poi qualcuno non se lo ricorda.
 *
 * ! LA COPIA DELLA ZONA DEL CLIENT E' L'ECCEZIONE, e ce l'ha per forza: non
 * passa dalle primitive perche' copia righe intere con MMX, ed e' proprio
 * quella la ragione per cui e' veloce. Li' il ritaglio si applica a mano, e
 * c'e' un commento che lo dice.
 * ============================================================================= */
static unsigned int g_clip_x = 0, g_clip_y = 0;
static unsigned int g_clip_w = 0, g_clip_h = 0;   /* w=0 vuol dire «tutto» */

static void clip_tutto(void)
{
    g_clip_x = 0; g_clip_y = 0;
    g_clip_w = 0; g_clip_h = 0;
}

static void clip_metti(unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h)
{
    g_clip_x = x; g_clip_y = y;
    g_clip_w = w; g_clip_h = h;
}

static int clip_dentro_y(unsigned int y)
{
    if (g_clip_w == 0) return 1;
    return y >= g_clip_y && y < g_clip_y + g_clip_h;
}

static void px(unsigned int x, unsigned int y, unsigned int c)
{
    unsigned char *p;

    if (x >= g_fb_w || y >= g_fb_h) return;
    if (g_clip_w != 0 &&
        (x < g_clip_x || x >= g_clip_x + g_clip_w ||
         y < g_clip_y || y >= g_clip_y + g_clip_h)) return;

    p = g_fb + y * g_fb_passo + x * (g_fb_bit >> 3);

    if (g_fb_bit == 32) {
        *(unsigned int *)p = c;
    } else if (g_fb_bit == 24) {
        p[0] = (unsigned char)(c);
        p[1] = (unsigned char)(c >> 8);
        p[2] = (unsigned char)(c >> 16);
    } else {    /* 16 bit, 5-6-5 */
        unsigned int v = (((c >> 16) & 0xF8) << 8) |
                         (((c >> 8)  & 0xFC) << 3) |
                         (( c        & 0xF8) >> 3);
        *(unsigned short *)p = (unsigned short)v;
    }
}

/* ! LA STRADA VELOCE A 32 BIT NON E' UN LUSSO: riempire lo sfondo di
 * 800x600 con una chiamata di funzione per pixel sono 480000 chiamate a
 * fotogramma, ed e' quanto bastava a far scadere le richieste dei client.
 * Qui si scrive una riga per volta, e la strada generale resta per gli altri
 * formati. */
static void riempi(unsigned int x, unsigned int y, unsigned int w,
                   unsigned int h, unsigned int c)
{
    unsigned int i, j;

    /* Il ritaglio si applica QUI, una volta, e non dentro il ciclo: e' la
     * stessa ragione per cui il confine dello schermo si taglia qui sopra. */
    if (g_clip_w != 0) {
        unsigned int x1 = x + w, y1 = y + h;
        unsigned int cx1 = g_clip_x + g_clip_w, cy1 = g_clip_y + g_clip_h;

        if (x  < g_clip_x) x = g_clip_x;
        if (y  < g_clip_y) y = g_clip_y;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
        if (x1 <= x || y1 <= y) return;
        w = x1 - x;
        h = y1 - y;
    }

    if (g_fb_bit == 32) {
        if (x >= g_fb_w || y >= g_fb_h) return;
        if (x + w > g_fb_w) w = g_fb_w - x;
        if (y + h > g_fb_h) h = g_fb_h - y;

        for (j = 0; j < h; j++) {
            unsigned int *p = (unsigned int *)(g_fb + (y + j) * g_fb_passo + x * 4);

            if (g_mmx) { mmx_riempi32(p, w, c); continue; }
            for (i = 0; i < w; i++) p[i] = c;
        }
        return;
    }

    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            px(x + i, y + j, c);
}

static void scrivi(unsigned int x, unsigned int y, const char *s, unsigned int c)
{
    unsigned int i;

    for (i = 0; s[i]; i++) {
        const unsigned char *g = &font8x16[(unsigned char)s[i] * 16];
        unsigned int r, b;

        for (r = 0; r < 16; r++)
            for (b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b)) px(x + i * 8 + b, y + r, c);
    }
}

/* -----------------------------------------------------------------------------
 * Comporre
 *
 * ! SI RIDISEGNA TUTTO, E PER ADESSO VA BENE. Un compositore serio ridisegna
 * solo cio' che e' cambiato, e ci arrivera': ma la lista delle regioni sporche
 * e' la struttura piu' facile da sbagliare di un server grafico, e sbagliarla
 * si vede come pezzi di finestra che restano sullo schermo dopo che la
 * finestra se n'e' andata. Prima che sia giusto, poi che sia veloce.
 * --------------------------------------------------------------------------- */
/* =============================================================================
 * ! GLI ANGOLI APPARTENGONO ALLA LUCE, e non e' un dettaglio da lasciare al
 * caso: le due righe si incontrano in due angoli, e chi ci arriva per ultimo
 * decide come si vede lo spigolo. Disegnando prima l'ombra e poi la luce
 * restano chiari l'angolo in alto a destra e quello in basso a sinistra — che
 * e' come si vede uno spigolo illuminato da sopra a sinistra. Al contrario si
 * ottiene un rettangolo che sembra ritagliato male.
 *
 * La stessa aritmetica sta in bordo3d() dentro lib/exwin/exwin.c: sono due
 * processi diversi e non c'e' modo di condividerla, ma e' la stessa regola —
 * e quando una si tocca va toccata anche l'altra, o telaio e controlli
 * prenderebbero la luce da due parti.
 * ============================================================================= */
static void bordo3d(unsigned int x, unsigned int y, unsigned int w,
                    unsigned int h, unsigned int sopra, unsigned int sotto)
{
    if (w == 0 || h == 0) return;

    riempi(x, y + h - 1, w, 1, sotto);
    riempi(x + w - 1, y, 1, h, sotto);
    riempi(x, y, w, 1, sopra);
    riempi(x, y, 1, h, sopra);
}

static void rilievo(unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    bordo3d(x, y, w, h, C_LUCE, C_OMBRA);
}

static void incavo(unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    bordo3d(x, y, w, h, C_OMBRA, C_LUCE);
}

static void cornice(const Finestra *f, unsigned int attiva)
{
    unsigned int alta = (f->stile & WIN_ST_TITOLO) ? BARRA_H : 0;
    unsigned int bx = f->x - BORDO;
    unsigned int by = f->y - BORDO - alta;
    unsigned int bw = f->w + BORDO * 2;
    unsigned int bh = f->h + BORDO * 2 + alta;

    if (f->stile & WIN_ST_BORDO) {
        /* Il telaio: prima il grigio dappertutto, poi i due anelli. Riempire
         * tutto e ridisegnarci sopra costa meno che ritagliare quattro strisce
         * — e i pixel di mezzo li copre comunque la zona del client. */
        riempi(bx, by, bw, BORDO, C_TELAIO);
        riempi(bx, by + bh - BORDO, bw, BORDO, C_TELAIO);
        riempi(bx, by, BORDO, bh, C_TELAIO);
        riempi(bx + bw - BORDO, by, BORDO, bh, C_TELAIO);

        rilievo(bx, by, bw, bh);                        /* il telaio sporge */
        incavo(bx + 1, by + 1, bw - 2, bh - 2);         /* il dentro rientra */
    }

    if (f->stile & WIN_ST_TITOLO) {
        unsigned int ty = f->y - alta;

        riempi(f->x, ty, f->w, alta, attiva ? C_BARRA_ATT : C_BARRA_INA);
        rilievo(f->x, ty, f->w, alta);
        scrivi(f->x + 5, ty + (alta - 16) / 2, f->titolo,
               attiva ? C_TITOLO : C_TITOLO_INA);

        /* ! IL PULSANTE DI CHIUSURA E' UN OGGETTO, NON UN QUADRATO ROSSO.
         * Sporge come un pulsante e ha dentro un quadratino: e' cosi' che si
         * riconosce come una cosa da premere invece che come una decorazione
         * della barra. */
        if (f->stile & WIN_ST_CHIUDI) {
            unsigned int cx = f->x + f->w - BARRA_H + 1;
            unsigned int cy = ty + 2;
            unsigned int cl = BARRA_H - 4;

            riempi(cx, cy, cl, cl, C_TELAIO);
            rilievo(cx, cy, cl, cl);
            riempi(cx + 4, cy + 4, cl - 8, cl - 8, C_CHIUDI);
            incavo(cx + 4, cy + 4, cl - 8, cl - 8);
        }
    }
}

/* ! TRE SEGNI IN DIAGONALE, come li fa chiunque, e non un'icona: e' l'unica
 * forma che si riconosce a 14 pixel di lato senza spiegazioni. */
static void presa(const Finestra *f)
{
    unsigned int bx = f->x + f->w - PRESA;
    unsigned int by = f->y + f->h - PRESA;
    unsigned int i, k;

    /* ! ANCHE LA PRESA SPORGE, e per la stessa ragione del pulsante di
     * chiusura: un disegno sopra i pixel del client sembra una macchia
     * dell'applicazione, un oggetto in rilievo sembra una cosa da tirare. */
    riempi(bx, by, PRESA, PRESA, C_TELAIO);
    rilievo(bx, by, PRESA, PRESA);

    for (k = 0; k < 2; k++) {
        unsigned int off = 4 + k * 4;

        for (i = 1; i + off < PRESA - 1; i++)
            px(bx + i + off, by + PRESA - 1 - i, C_PRESA);
    }
}

/* Il contorno di dove la finestra andra' a finire. Si disegna per ultimo,
 * sopra tutto: e' l'unica cosa che deve restare visibile qualunque sia la
 * finestra che ci sta sotto. */
static void contorno(unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    riempi(x, y, w, 1, C_CONTORNO);
    riempi(x, y + h - 1, w, 1, C_CONTORNO);
    riempi(x, y, 1, h, C_CONTORNO);
    riempi(x + w - 1, y, 1, h, C_CONTORNO);
}

static void componi(void)
{
    unsigned int k, i, j;

    riempi(0, 0, g_fb_w, g_fb_h, C_SFONDO);

    for (k = 0; k < g_n_ordine; k++) {
        Finestra *f = &g_fin[g_ordine[k]];
        const unsigned int *src;

        if (!f->usata || !(f->stile & WIN_ST_VISIBILE)) continue;

        /* ! «ATTIVA» VUOL DIRE «HA IL FUOCO», NON «E' L'ULTIMA DELLA PILA», e
         * fino al 18 agosto 2026 qui c'era la seconda. Sembrava la stessa cosa
         * e non lo era: la barra delle applicazioni ha WIN_ST_SOPRA, quindi
         * sta SEMPRE in cima — e quindi NESSUNA finestra normale risultava mai
         * attiva. Le barre del titolo erano tutte grigie, e non si vedeva
         * quale finestra avrebbe ricevuto i tasti.
         *
         * Il fuoco lo sa gia' il server, e lo sa bene: salta lo sfondo e le
         * finestre «sopra» apposta (vedi prende_fuoco_da_solo). */
        cornice(f, (int)g_ordine[k] == g_fuoco);

        /* ! I PIXEL SI LEGGONO DALLA ZONA DEL CLIENT SENZA FIDARSI DELLA
         * MISURA CHE IL CLIENT CREDE DI AVERE: il ciclo va sui numeri che
         * abbiamo noi. La zona e' condivisa, quindi il client puo' averci
         * scritto qualunque cosa — ma non puo' cambiarne la dimensione. */
        src = (const unsigned int *)f->zona_virt;
        if (!src) continue;

        if (g_fb_bit == 32 && f->x < g_fb_w && f->y < g_fb_h) {
            unsigned int ww = (f->x + f->w > g_fb_w) ? g_fb_w - f->x : f->w;
            unsigned int hh = (f->y + f->h > g_fb_h) ? g_fb_h - f->y : f->h;
            unsigned int x0 = 0;

            /* ! QUI IL RITAGLIO SI FA A MANO, e il perche' sta accanto a
             * clip_metti(): questa copia non passa dalle primitive apposta —
             * va per righe intere con MMX, ed e' quello che la rende veloce.
             * Si tagliano le colonne una volta prima del ciclo e le righe
             * dentro, che e' lo stesso lavoro che farebbe px() ma per riga
             * invece che per pixel. */
            if (g_clip_w != 0) {
                unsigned int cx1 = g_clip_x + g_clip_w;
                unsigned int fx1 = f->x + ww;

                if (f->x + ww <= g_clip_x || f->x >= cx1) ww = 0;
                else {
                    if (f->x < g_clip_x) x0 = g_clip_x - f->x;
                    if (fx1 > cx1) fx1 = cx1;
                    ww = fx1 - (f->x + x0);
                }
            }

            for (j = 0; ww && j < hh; j++) {
                unsigned int *d;
                const unsigned int *sr;

                if (!clip_dentro_y(f->y + j)) continue;

                d  = (unsigned int *)(g_fb + (f->y + j) * g_fb_passo
                                      + (f->x + x0) * 4);
                sr = src + j * f->w + x0;

                if (g_mmx) { mmx_copia32(d, sr, ww); continue; }
                for (i = 0; i < ww; i++) d[i] = sr[i];
            }
        } else {
            for (j = 0; j < f->h; j++)
                for (i = 0; i < f->w; i++)
                    px(f->x + i, f->y + j, src[j * f->w + i]);
        }

        /* ! DOPO I PIXEL DEL CLIENT, E QUINDI SOPRA: se si disegnasse con la
         * cornice, la copia della zona la cancellerebbe subito. E' il prezzo
         * dichiarato di avere la presa dentro l'area del client. */
        if (f->stile & WIN_ST_RIDIM) presa(f);
    }

    /* Il contorno di un ridimensionamento in corso, sopra tutte le finestre. */
    if (g_ridim >= 0 && g_fin[g_ridim].usata) {
        const Finestra *f = &g_fin[g_ridim];
        unsigned int by = f->y - BORDO -
                          ((f->stile & WIN_ST_TITOLO) ? BARRA_H : 0);

        contorno(f->x - BORDO, by, g_rw + BORDO * 2,
                 g_rh + BORDO * 2 + ((f->stile & WIN_ST_TITOLO) ? BARRA_H : 0));
    }

    /* Il puntatore, una freccia semplice disegnata a mano. */
    {
        int a, b;
        for (a = 0; a < 12; a++)
            for (b = 0; b <= a && b < 8; b++)
                if (a < 10 || b < 4)
                    px((unsigned int)(g_px + b), (unsigned int)(g_py + a),
                       (b == 0 || b == a || a == 11) ? 0x00000000 : 0x00FFFFFF);
    }
}

/* -----------------------------------------------------------------------------
 * Le finestre
 * --------------------------------------------------------------------------- */
static int trova_id(unsigned int id)
{
    int i;
    for (i = 0; i < FINESTRE_MAX; i++)
        if (g_fin[i].usata && g_fin[i].id == id) return i;
    return -1;
}

/* =============================================================================
 * IL FUOCO NON E' L'ORDINE DI DISEGNO — corretto il 17 agosto 2026
 *
 * Fino a oggi il tasto andava a `g_ordine[g_n_ordine - 1]`, cioe' alla
 * finestra disegnata per ultima, con il commento «la finestra in cima, che e'
 * il fuoco». Era vero finche' l'ordine di disegno dipendeva solo da chi si era
 * portato davanti.
 *
 * Ha smesso di esserlo il 14 agosto, con WIN_ST_SOPRA: in_cima() rimette in
 * fondo all'ordine — cioe' in cima allo schermo — tutte le finestre «sopra»,
 * qualunque cosa sia appena salita. E la barra delle applicazioni e' «sopra».
 *
 * ! QUINDI DA QUEL GIORNO LA BARRA SI PRENDEVA OGNI TASTO, e nessuna finestra
 * poteva riceverne uno finche' il program manager era acceso. L'editor
 * sembrava sordo: non arrivava niente, ne' le lettere ne' Ctrl+S. Il difetto
 * non era nell'editor ne' in WIN_ST_SOPRA — era in questa funzione, che
 * decideva DUE cose mentre il suo nome ne prometteva una.
 *
 * Adesso il fuoco e' una variabile sua. Chi sale prende il fuoco; le finestre
 * «sopra» e lo sfondo restano dove devono stare a schermo senza portarselo
 * via, e lo prendono solo se ci si clicca dentro davvero.
 * ============================================================================= */


/* Chi puo' tenere il fuoco per conto suo, cioe' senza esserci stato messo da
 * un clic: non lo sfondo (non e' una finestra con cui si parla) e non la barra
 * (c'e' sempre, e vincerebbe sempre). */
static int prende_fuoco_da_solo(int idx)
{
    return !(g_fin[idx].stile & (WIN_ST_SFONDO | WIN_ST_SOPRA));
}

/* Il fuoco quando quello di prima se n'e' andato: la finestra normale piu' in
 * alto. Se non ce n'e' nessuna, nessun fuoco — meglio di uno a caso. */
static void fuoco_ricalcola(void)
{
    int k;

    g_fuoco = -1;
    for (k = (int)g_n_ordine - 1; k >= 0; k--) {
        int idx = (int)g_ordine[k];
        if (g_fin[idx].usata && prende_fuoco_da_solo(idx)) { g_fuoco = idx; return; }
    }
}

/* La finestra modale di un processo, se ne ha una aperta, o -1.
 *
 * ! SI CERCA DALLA CIMA, cosi' con due dialoghi annidati — «salva?» che apre
 * «sovrascrivo?» — risponde l'ultimo aperto, che e' quello che sta chiedendo.
 *
 * ! E LA RICERCA E' PER PROCESSO E NON PER FINESTRA PADRE, perche' il server
 * non sa niente di parentele: dal suo punto di vista un'applicazione e' un
 * pid con delle finestre. Legare il blocco alla parentela vorrebbe dire
 * portare l'albero delle finestre dentro il protocollo per usarlo qui, e qui
 * soltanto. */
static int modale_di(unsigned int pid)
{
    int k;

    for (k = (int)g_n_ordine - 1; k >= 0; k--) {
        int idx = (int)g_ordine[k];

        if (g_fin[idx].usata && (g_fin[idx].stile & WIN_ST_MODALE) &&
            g_fin[idx].pid == pid)
            return idx;
    }
    return -1;
}

static void in_cima(int idx)
{
    unsigned int k, j = 0;

    /* ! IL FUOCO SI DA' QUI, PRIMA DEL RIORDINO, e non si legge dall'ordine
     * dopo: e' proprio il riordino a mentire. Chi si porta davanti prende i
     * tasti anche se poi lo schermo lo disegna sotto alla barra. */
    if (g_fin[idx].usata && !(g_fin[idx].stile & WIN_ST_SFONDO)) g_fuoco = idx;

    /* =====================================================================
     * ! UNA FINESTRA «SOPRA» GIA' IN PILA NON SI RIALZA, E FRA LORO L'ORDINE
     * LO DECIDE CHI E' NATO PRIMA.
     *
     * Segnalato cosi': «data e ora nella barra spariscono appena faccio
     * clic». Non sparivano: finivano SOTTO. La barra delle applicazioni e
     * l'orologio sono tutt'e due WIN_ST_SOPRA e si sovrappongono per
     * disegno — l'angolo destro della barra E' dell'orologio, che e' un
     * processo a parte. Al primo clic sulla barra questa funzione la portava
     * davanti, e siccome il riordino qui sotto tiene l'ordine RELATIVO fra le
     * «sopra», da quel momento la barra copriva l'orologio per sempre.
     *
     *     ordine: 0 1 2      l'orologio (2) sopra la barra (1)
     *     ordine: 0 2 1      dopo un clic sulla barra
     *
     * ! E IL FUOCO SI DA' LO STESSO, sopra: chi clicca la barra deve poterci
     * scrivere. Quello che non deve cambiare e' CHI COPRE CHI.
     *
     * Una finestra «sopra» NUOVA passa di qui e viene aggiunta normalmente —
     * e' cosi' che il menu di avvio nasce davanti alla barra. La regola vale
     * solo per chi in pila c'e' gia'.
     * ===================================================================== */
    if (g_fin[idx].stile & WIN_ST_SOPRA) {
        for (k = 0; k < g_n_ordine; k++)
            if (g_ordine[k] == (unsigned int)idx) return;
    }

    for (k = 0; k < g_n_ordine; k++)
        if (g_ordine[k] != (unsigned int)idx) g_ordine[j++] = g_ordine[k];

    /* ! LO SFONDO NON SALE MAI. E' cio' che lo rende uno sfondo invece di una
     * finestra come le altre, ed e' anche il motivo per cui un'immagine di
     * scrivania non ha bisogno di un meccanismo suo. */
    if (g_fin[idx].stile & WIN_ST_SFONDO) {
        for (k = j; k > 0; k--) g_ordine[k] = g_ordine[k - 1];
        g_ordine[0] = (unsigned int)idx;
    } else {
        g_ordine[j] = (unsigned int)idx;
    }
    g_n_ordine = j + 1;

    /* ! E QUELLE «SOPRA» RESTANO SOPRA, anche a chi e' appena salito. Senza,
     * la barra delle applicazioni finirebbe coperta dalla prima finestra che
     * si porta davanti — e con una finestra a schermo intero non ci sarebbe
     * piu' modo di arrivare al menu. */
    {
        unsigned int q, alto = 0;
        unsigned int tmp[FINESTRE_MAX];

        for (q = 0; q < g_n_ordine; q++)
            if (!(g_fin[g_ordine[q]].stile & WIN_ST_SOPRA)) tmp[alto++] = g_ordine[q];
        for (q = 0; q < g_n_ordine; q++)
            if (g_fin[g_ordine[q]].stile & WIN_ST_SOPRA) tmp[alto++] = g_ordine[q];
        for (q = 0; q < g_n_ordine; q++) g_ordine[q] = tmp[q];
    }
}

/* Chi c'e' sotto il puntatore, dalla cima al fondo. -1 = nessuno.
 * `dove`: 0 = area del client, 1 = barra del titolo, 2 = pulsante chiudi. */
static int sotto(int x, int y, unsigned int *dove)
{
    int k;

    for (k = (int)g_n_ordine - 1; k >= 0; k--) {
        Finestra *f = &g_fin[g_ordine[k]];
        int by, bh;

        if (!f->usata || !(f->stile & WIN_ST_VISIBILE)) continue;

        if (x >= (int)f->x && x < (int)(f->x + f->w) &&
            y >= (int)f->y && y < (int)(f->y + f->h)) {
            *dove = 0;

            /* ! LA PRESA VINCE SUL CLIENT, e per questo la finestra deve
             * averla chiesta: un angolo dell'area che non consegna piu' i clic
             * a chi ci ha messo un pulsante sarebbe un difetto, se non fosse
             * stata l'applicazione stessa a domandarlo. */
            if ((f->stile & WIN_ST_RIDIM) &&
                x >= (int)(f->x + f->w - PRESA) &&
                y >= (int)(f->y + f->h - PRESA)) *dove = 3;

            return (int)g_ordine[k];
        }

        if (!(f->stile & WIN_ST_TITOLO)) continue;

        by = (int)f->y - BARRA_H;
        bh = BARRA_H;
        if (x >= (int)f->x && x < (int)(f->x + f->w) && y >= by && y < by + bh) {
            *dove = 1;
            if ((f->stile & WIN_ST_CHIUDI) &&
                x >= (int)(f->x + f->w - BARRA_H)) *dove = 2;
            return (int)g_ordine[k];
        }
    }
    return -1;
}

static void manda_evento(const Finestra *f, unsigned int tipo,
                         int x, int y, unsigned int bottoni, unsigned int tasto)
{
    WinEvento e;

    e.id      = f->id;
    e.tipo    = tipo;
    e.x       = (unsigned int)(x - (int)f->x);
    e.y       = (unsigned int)(y - (int)f->y);
    e.bottoni = bottoni;
    e.tasto   = tasto;

    /* ! L'ORA SI PRENDE QUI, DOVE L'EVENTO NASCE. Prenderla dove viene letto
     * misurerebbe anche il tempo che il client ha passato a fare altro: vedi
     * il commento su WinEvento in win_proto.h. */
    e.tempo   = uptime_ms();

    /* ! SE IL CLIENT NON RACCOGLIE, NON SI INSISTE. La mailbox e' profonda
     * quattro messaggi: un client fermo la riempirebbe, e da li' in poi ogni
     * ipc_send fallirebbe. Un evento perso e' meno grave di un server che si
     * blocca su un client che non risponde piu'. */
    (void)ipc_send(f->pid, WIN_MSG_EVENTO, &e, sizeof(e));
}

/* -----------------------------------------------------------------------------
 * Il mouse
 *
 * ! SI CERCA PRIMA IL SERVIZIO DEDICATO, POI IL PS/2, ed e' la regola gia'
 * scritta in kbd_proto.h: se c'e' un driver che fa SOLO il mouse vuol dire che
 * qualcuno l'ha avviato apposta. Cosi' lo stesso server funziona con PS/2,
 * seriale, UHCI e xHCI senza sapere quale sia.
 * --------------------------------------------------------------------------- */
/* -----------------------------------------------------------------------------
 * La tastiera
 *
 * ! NON SI RIFANNO LE MAPPE: IL SERVIZIO 'kbd' CONSEGNA GIA' TRADOTTO. In modo
 * raw un KBD_MSG_KEY porta il carattere nei bit 0..15, i tasti speciali da
 * 0x100 in su e i modificatori nei bit 16..18 — passati per la disposizione
 * scelta con `keymap`, che si cambia a caldo. Un server che leggesse scancode
 * grezzi si porterebbe dietro una seconda copia di keymaps.h, e le due
 * divergerebbero. E' la stessa regola del driver USB, applicata dall'altro
 * lato: li' si MANDANO scancode a chi serve 'kbd', qui si CONSUMA cio' che
 * 'kbd' ha gia' tradotto.
 *
 * ! IL MODO RAW E' PER CONSOLE, NON GLOBALE, ed e' cio' che rende la cosa
 * possibile: mettere in raw la propria console non tocca quella dove gira la
 * shell, che continua a ricevere righe intere con l'eco.
 *
 * ! MA PRENDERE LA TASTIERA E' UNA DECISIONE CHE RIGUARDA UN ALTRO PROGRAMMA,
 * quindi non si fa di nascosto. Se il server e' in PRIMO PIANO sulla propria
 * console, la tastiera e' sua per definizione. Se e' in sfondo non la tocca —
 * o lascerebbe muta la shell che sta usando quella stessa console — a meno che
 * non gliela si dia esplicitamente con -t.
 * --------------------------------------------------------------------------- */
static int g_kbd_pid = -1;
static unsigned int g_console = 0;
static unsigned int g_mio_pid = 0;
static unsigned int g_raw = 0;          /* la nostra console e' in raw */
static unsigned int g_forza_tastiera = 0;
static unsigned int g_key_chiesta = 0;  /* c'e' una READKEY in volo */
static unsigned int g_visibile = 1;     /* la nostra console e' quella a video */

static void kbd_modo(unsigned int raw)
{
    KbdSetMode m;

    if (g_kbd_pid < 0 || g_raw == raw) return;

    m.modo    = raw ? KBD_MODE_RAW : KBD_MODE_COOKED;
    m.console = g_console;
    if (ipc_send((unsigned int)g_kbd_pid, KBD_MSG_SETMODE, &m, sizeof(m)) < 0)
        return;

    g_raw = raw;
    g_key_chiesta = 0;
    if (g_verboso)
        printf("wserver: console %u in modo %s\n", g_console,
               raw ? "raw" : "cooked");
}

/* Decide a ogni giro se la tastiera e' nostra, e chiede il prossimo tasto. */
static void kbd_giro(void)
{
    ConsoleInfo ci;
    unsigned int nostra;

    if (console_info(&ci) != 0) return;

    /* ! LO SCHERMO SI COMPONE SOLO SE LA NOSTRA CONSOLE E' QUELLA A VIDEO.
     * La console di testo del kernel disegna nello STESSO framebuffer: se si
     * commuta con Alt+Fn e noi continuassimo a comporre, i due si
     * scriverebbero addosso a vicenda — e chi guarda vedrebbe finestre e
     * prompt sovrapposti senza capire di chi sia la colpa. */
    {
        unsigned int ora = (ci.visibile == g_console);

        /* ! TORNANDO VISIBILI SI RIDIPINGE TUTTO, e non e' un di piu': mentre
         * eravamo nascosti la console di TESTO ha scritto nello stesso
         * framebuffer. Senza questo, Alt+Fn e ritorno lascerebbe lo schermo
         * com'era — cioe' il prompt della shell sopra le finestre — finche'
         * qualcosa non cambia per conto suo. */
        if (ora && !g_visibile) sporca_tutto();
        g_visibile = ora;
    }

    if (g_kbd_pid < 0) return;
    if (!g_visibile) { kbd_modo(0); return; }

    nostra = g_forza_tastiera || (ci.fg == g_mio_pid);
    kbd_modo(nostra);

    if (g_raw && !g_key_chiesta) {
        unsigned int c = g_console;
        if (ipc_send((unsigned int)g_kbd_pid, KBD_MSG_READKEY, &c, sizeof(c)) >= 0)
            g_key_chiesta = 1;
    }
}

/* Il tasto va alla finestra col FUOCO, che non e' quella disegnata per ultima:
 * vedi il blocco sopra in_cima(). */
static void kbd_tasto(unsigned int k)
{
    g_key_chiesta = 0;

    if (g_n_ordine == 0) return;
    if (g_fuoco < 0 || !g_fin[g_fuoco].usata) fuoco_ricalcola();
    if (g_fuoco < 0) return;

    /* ! I TASTI SEGUONO LA STESSA REGOLA DEI CLIC, e dimenticarlo sarebbe il
     * modo piu' facile di fare un modale finto: si blocca il mouse, si prova
     * col mouse, sembra fatto — e intanto nel testo dietro si continua a
     * scrivere. Qui non si buttano: si dirottano alla modale, che e' l'unica
     * che possa farci qualcosa (Invio, Esc). */
    {
        int md = modale_di(g_fin[g_fuoco].pid);

        if (md >= 0) g_fuoco = md;
    }

    {
        Finestra *f = &g_fin[g_fuoco];
        WinEvento e;

        if (!f->usata) return;

        e.id      = f->id;
        e.tipo    = WIN_EV_TASTO;
        e.x = 0; e.y = 0;
        e.bottoni = 0;
        e.tasto   = k;          /* gia' tradotto: NON e' uno scancode */
        e.tempo   = uptime_ms();
        (void)ipc_send(f->pid, WIN_MSG_EVENTO, &e, sizeof(e));
    }
}

static int g_mouse_pid = -1;

static void mouse_trova(void)
{
    /* ! IL MESSAGGIO E' LO STESSO PER TUTT'E DUE, e cambia solo a chi si
     * manda: MOUSE_MSG_LEGGI lo capisce sia un driver dedicato sia il driver
     * della tastiera, che il mouse PS/2 lo serve dallo stesso 8042. */
    g_mouse_pid = ipc_lookup(MOUSE_SERVICE_NAME);
    if (g_mouse_pid >= 0) return;

    g_mouse_pid = ipc_lookup(KBD_SERVICE_NAME);
}

/* ! CHIEDE E BASTA: LA RISPOSTA LA RACCOGLIE CHI LEGGE LA MAILBOX.
 *
 * Qui c'era una ipc_recv_timeout() sua, e leggeva QUALUNQUE messaggio
 * trovasse — compresa la richiesta di un client che stava chiedendo una
 * finestra, che finiva interpretata come uno stato del mouse e spariva. Il
 * sintomo era un'applicazione che una volta su tre diceva «il server non
 * risponde», e non somigliava per niente a «qualcuno mi ruba i messaggi».
 *
 * ! LA REGOLA E' UNA SOLA: LA MAILBOX LA LEGGE UN POSTO SOLO. Due funzioni
 * che pescano dalla stessa coda si rubano i messaggi a vicenda, e chi perde
 * dipende da quale delle due e' arrivata prima — cioe' dal caso. */
/* ! UNA RICHIESTA PER VOLTA, E NON E' EDUCAZIONE: E' CORRETTEZZA.
 *
 * Chiedendo lo stato del mouse a ogni giro senza aspettare la risposta, le
 * risposte si accumulavano nella NOSTRA mailbox — che e' profonda quattro
 * messaggi. Piena quella, ogni ipc_send verso di noi falliva, compresa la
 * consegna di un tasto. E il servizio 'kbd', quando la consegna di un tasto
 * fallisce, RIMETTE LA CONSOLE IN COOKED:
 *
 *     c->keyreader_pid = 0;
 *     kbd_set_mode(n, KBD_MODE_COOKED);
 *
 * Da li' i tasti tornavano alla shell. Il sintomo era «il server prende il
 * modo raw ma i tasti vanno alla shell» — e la causa non era nel modo raw
 * ne' nella tastiera: era che ci eravamo riempiti la posta da soli. */
static unsigned int g_mouse_chiesto = 0;

static void mouse_chiedi(void)
{
    unsigned int attendi = 0;

    if (g_mouse_pid < 0 || g_mouse_chiesto) return;
    if (ipc_send((unsigned int)g_mouse_pid, MOUSE_MSG_LEGGI,
                 &attendi, sizeof(attendi)) >= 0)
        g_mouse_chiesto = 1;
}

static void mouse_stato(const MouseStato *s)
{
    /* ! UN CAMBIO DI BOTTONI PUO' CAMBIARE QUALUNQUE COSA — alzare una
     * finestra, aprire un menu — e quello che cambia lo decidono i client:
     * qui non si sa, quindi si dichiara tutto. Il movimento invece si sa
     * esattamente cos'e'. */
    if (s->bottoni != g_bottoni) sporca_tutto();

    if (s->dx || s->dy) sporca_puntatore(g_px, g_py);   /* dov'era */

    g_px += s->dx;
    g_py += s->dy;
    if (g_px < 0) g_px = 0;
    if (g_py < 0) g_py = 0;
    if (g_px >= (int)g_fb_w) g_px = (int)g_fb_w - 1;
    if (g_py >= (int)g_fb_h) g_py = (int)g_fb_h - 1;

    if (s->dx || s->dy) sporca_puntatore(g_px, g_py);   /* e dov'e' */

    g_bottoni = s->bottoni;
}

/* Definita fra le richieste dei client: e' la stessa cosa, chiesta col mouse
 * invece che con un messaggio. */
static void ridimensiona(int idx, unsigned int nw, unsigned int nh);

static void mouse_agisci(void)
{
    unsigned int giu = (g_bottoni & MOUSE_BTN_SIN) &&
                       !(g_bottoni_prec & MOUSE_BTN_SIN);
    unsigned int su  = !(g_bottoni & MOUSE_BTN_SIN) &&
                        (g_bottoni_prec & MOUSE_BTN_SIN);
    unsigned int dove = 0;
    int idx;

    if (g_trascino >= 0) {
        if (su) {
            /* ! LA POSIZIONE NUOVA SI DICE AL CLIENT, UNA VOLTA, AL RILASCIO.
             * Dirla a ogni pixel sarebbe un messaggio per movimento del mouse,
             * per una cosa che serve solo quando ci si e' fermati. */
            WinRegione w;

            memset(&w, 0, sizeof(w));
            w.id = g_fin[g_trascino].id;
            w.x  = g_fin[g_trascino].x;
            w.y  = g_fin[g_trascino].y;
            (void)ipc_send(g_fin[g_trascino].pid, WIN_MSG_POSTA, &w, sizeof(w));
            g_trascino = -1;
        } else {
            g_fin[g_trascino].x = (unsigned int)(g_px - g_tr_dx);
            g_fin[g_trascino].y = (unsigned int)(g_py - g_tr_dy);
            sporca_tutto();
        }
        g_bottoni_prec = g_bottoni;
        return;
    }

    /* ! MENTRE SI TIRA L'ANGOLO SI MUOVE SOLO UN CONTORNO. La misura vera
     * arriva al rilascio, e una volta sola: il perche' sta accanto a
     * #define PRESA. */
    if (g_ridim >= 0) {
        int idx2 = g_ridim;
        int nw = g_px - (int)g_fin[idx2].x + 1;
        int nh = g_py - (int)g_fin[idx2].y + 1;

        if (nw < MIN_W) nw = MIN_W;
        if (nh < MIN_H) nh = MIN_H;
        if (g_fin[idx2].x + (unsigned int)nw > g_fb_w)
            nw = (int)(g_fb_w - g_fin[idx2].x);
        if (g_fin[idx2].y + (unsigned int)nh > g_fb_h)
            nh = (int)(g_fb_h - g_fin[idx2].y);

        if ((unsigned int)nw != g_rw || (unsigned int)nh != g_rh) sporca_tutto();
        g_rw = (unsigned int)nw;
        g_rh = (unsigned int)nh;

        if (su) {
            g_ridim = -1;
            ridimensiona(idx2, g_rw, g_rh);
        }
        g_bottoni_prec = g_bottoni;
        return;
    }

    /* Il trascinamento dentro l'area di un client: si consegna a chi ha preso
     * il bottone giu', finche' non lo rilascia. */
    if (!giu && !su && (g_bottoni & MOUSE_BTN_SIN) && g_giu_su >= 0 &&
        (g_px != g_px_prec || g_py != g_py_prec)) {
        if (g_fin[g_giu_su].usata)
            manda_evento(&g_fin[g_giu_su], WIN_EV_MOUSE_MOSSO,
                         g_px, g_py, g_bottoni, 0);
        g_px_prec = g_px;
        g_py_prec = g_py;
    }

    if (su && g_giu_su >= 0) {
        if (g_fin[g_giu_su].usata)
            manda_evento(&g_fin[g_giu_su], WIN_EV_MOUSE_SU,
                         g_px, g_py, g_bottoni, 0);
        g_giu_su = -1;
        g_bottoni_prec = g_bottoni;
        return;
    }

    idx = sotto(g_px, g_py, &dove);
    if (idx < 0) { g_bottoni_prec = g_bottoni; return; }

    /* ! IL CLIC SU UNA FINESTRA BLOCCATA NON SI PERDE IN SILENZIO: porta
     * davanti la modale. Buttarlo e basta darebbe un'applicazione che non
     * risponde e nessun indizio sul perche' — e la finestra che aspetta la
     * risposta potrebbe essere finita sotto un'altra. */
    {
        int md = modale_di(g_fin[idx].pid);

        if (md >= 0 && md != idx) {
            if (giu) { in_cima(md); sporca_tutto(); }
            g_bottoni_prec = g_bottoni;
            return;
        }
    }

    if (giu) {
        in_cima(idx);
        sporca_tutto();

        if (dove == 2) {
            manda_evento(&g_fin[idx], WIN_EV_CHIUDI, g_px, g_py, 0, 0);
        } else if (dove == 3) {
            g_ridim = idx;
            g_rw = g_fin[idx].w;
            g_rh = g_fin[idx].h;
        } else if (dove == 1) {
            /* ! SOLO LA BARRA TRASCINA, e non tutta la finestra: trascinare
             * dall'area del client vorrebbe dire che un'applicazione non puo'
             * mai ricevere un clic. */
            g_trascino = idx;
            g_tr_dx = g_px - (int)g_fin[idx].x;
            g_tr_dy = g_py - (int)g_fin[idx].y;
        } else {
            g_giu_su  = idx;
            g_px_prec = g_px;
            g_py_prec = g_py;
            manda_evento(&g_fin[idx], WIN_EV_MOUSE_GIU, g_px, g_py,
                         g_bottoni, 0);
        }
    }

    g_bottoni_prec = g_bottoni;
}

/* -----------------------------------------------------------------------------
 * Le richieste dei client
 * --------------------------------------------------------------------------- */
static void crea(unsigned int pid, const WinCrea *c)
{
    WinCreata r;
    ShmZona   z;
    int i;

    memset(&r, 0, sizeof(r));

    for (i = 0; i < FINESTRE_MAX; i++) if (!g_fin[i].usata) break;
    if (i == FINESTRE_MAX) {
        (void)ipc_send(pid, WIN_MSG_CREATA, &r, sizeof(r));
        return;
    }

    /* ! LA MISURA SI STRINGE, NON SI RIFIUTA. Una finestra piu' grande dello
     * schermo e' una richiesta ragionevole scritta da chi non sa quanto e'
     * grande lo schermo; rifiutarla vorrebbe dire che ogni applicazione deve
     * chiedere prima. La misura CONCESSA torna indietro nella risposta, come
     * fa gia' shm_apri con i byte. */
    r.larghezza = c->larghezza ? c->larghezza : 100;
    r.altezza   = c->altezza   ? c->altezza   : 100;
    if (r.larghezza > g_fb_w) r.larghezza = g_fb_w;
    if (r.altezza   > g_fb_h) r.altezza   = g_fb_h;

    memset(&z, 0, sizeof(z));
    win_nome_zona(z.nome, g_prossimo_id, 0);
    z.byte = r.larghezza * r.altezza * 4;
    z.flag = SHM_CREA;

    if (shm_apri(&z) != 0) {
        (void)ipc_send(pid, WIN_MSG_CREATA, &r, sizeof(r));
        return;
    }

    g_fin[i].usata     = 1;
    g_fin[i].id        = g_prossimo_id++;
    g_fin[i].pid       = pid;
    g_fin[i].x         = c->x;
    g_fin[i].y         = c->y;

    /* ! «METTILA TU»: LA CASCATA. Il perche' sta accanto a WIN_XY_AUTO in
     * win_proto.h — due copie dello stesso programma che nascono nello stesso
     * punto sembrano una copia sola.
     *
     * ! IL PASSO E' L'ALTEZZA DELLA BARRA DEL TITOLO, e non un numero a caso:
     * e' esattamente quanto serve perche' della finestra di sotto resti
     * visibile la barra — cioe' il suo nome e il modo di riportarla davanti.
     * Uno scostamento piu' piccolo darebbe una pila in cui non si legge
     * nessun titolo, uno piu' grande esaurirebbe lo schermo in tre finestre. */
    if (c->x == WIN_XY_AUTO || c->y == WIN_XY_AUTO) {
        unsigned int passo = BARRA_H + BORDO;
        unsigned int px_ = BORDO + g_cascata * passo;
        unsigned int py_ = BARRA_H + BORDO + g_cascata * passo;

        /* Quando la prossima non ci starebbe piu' si ricomincia da capo: e'
         * quello che fa qualunque scrivania, e l'alternativa — finestre con la
         * barra fuori dallo schermo — e' una finestra che non si sposta piu'. */
        if (px_ + r.larghezza + BORDO > g_fb_w ||
            py_ + r.altezza   + BORDO > g_fb_h) {
            g_cascata = 0;
            px_ = BORDO;
            py_ = BARRA_H + BORDO;
        }
        g_cascata++;

        g_fin[i].x = px_;
        g_fin[i].y = py_;
    }
    g_fin[i].w         = r.larghezza;
    g_fin[i].h         = r.altezza;
    g_fin[i].stile     = c->stile;
    g_fin[i].zona_virt = z.virt;
    g_fin[i].zona_byte = z.byte;
    memcpy(g_fin[i].titolo, c->titolo, WIN_TITOLO_LEN);
    g_fin[i].titolo[WIN_TITOLO_LEN - 1] = '\0';

    /* Il client parte con un'area di un colore noto invece che con la
     * memoria che c'era: una finestra nuova piena di spazzatura sembra un
     * difetto del client. */
    {
        unsigned int *p = (unsigned int *)z.virt;
        unsigned int n = r.larghezza * r.altezza, k;
        for (k = 0; k < n; k++) p[k] = C_CLIENT;
    }

    in_cima(i);
    sporca_tutto();

    r.id    = g_fin[i].id;
    r.byte  = z.byte;
    r.passo = r.larghezza * 4;
    r.giro  = 0;
    r.x     = g_fin[i].x;
    r.y     = g_fin[i].y;

    if (g_verboso)
        printf("wserver: finestra %u per il PID %u, %ux%u '%s'\n",
               r.id, pid, r.larghezza, r.altezza, g_fin[i].titolo);

    (void)ipc_send(pid, WIN_MSG_CREATA, &r, sizeof(r));
}

/* =============================================================================
 * RIDIMENSIONARE — la sola cosa che una zona condivisa non sa fare da se'
 *
 * ! UNA ZONA CONDIVISA NON SI ALLARGA, E QUESTA E' TUTTA LA DIFFICOLTA'. Le
 * pagine sono mappate in due spazi di indirizzi diversi, a due indirizzi
 * diversi; allungarle vorrebbe dire trovare spazio libero DOPO di esse in
 * tutt'e due, il che non si puo' garantire. Quindi non si allarga: si crea la
 * zona nuova, ci si porta dentro cio' che c'era, e si lascia morire la vecchia
 * quando l'ultimo dei due l'ha chiusa.
 *
 * ! E LA STRETTA DI MANO E' ORDINATA PERCHE' NESSUNO RESTA SENZA PIXEL. La
 * successione e' questa, e ogni passo ha un perche':
 *
 *   1. il server crea la zona nuova (nome col giro successivo) e la riempie;
 *   2. il server CHIUDE la sua vecchia e comincia a comporre dalla nuova. La
 *      vecchia non muore: il client la tiene ancora aperta;
 *   3. il server manda WIN_MSG_MISURATA;
 *   4. il client apre la nuova, chiude la vecchia — che a questo punto muore —
 *      si ridisegna e manda WIN_MSG_AGGIORNA con la misura che ha adesso;
 *   5. quella misura e' la RICEVUTA: il server smette di ripetere il messaggio.
 *
 * ! IL PASSO 5 NON E' ZELO. Un evento del mouse perso e' un clic perso; un
 * WIN_MSG_MISURATA perso e' un client che disegna per sempre dentro una zona
 * che il server non guarda piu' — una finestra congelata che pero' risponde ai
 * tasti, cioe' il difetto piu' difficile da leggere che ci sia. E la mailbox e'
 * profonda quattro messaggi: perderlo non e' un'ipotesi teorica.
 * ============================================================================= */
static void dire_misura(Finestra *f)
{
    WinCreata r;

    memset(&r, 0, sizeof(r));
    r.id        = f->id;
    r.byte      = f->zona_byte;
    r.passo     = f->w * 4;
    r.larghezza = f->w;
    r.altezza   = f->h;
    r.giro      = f->giro;
    r.x         = f->x;
    r.y         = f->y;

    (void)ipc_send(f->pid, WIN_MSG_MISURATA, &r, sizeof(r));
}

static void ridimensiona(int idx, unsigned int nw, unsigned int nh)
{
    Finestra     *f = &g_fin[idx];
    ShmZona       z;
    unsigned int *nuovo, *vecchio;
    unsigned int  cw, ch, i, j;

    if (idx < 0 || !f->usata) return;

    if (nw < MIN_W) nw = MIN_W;
    if (nh < MIN_H) nh = MIN_H;
    if (nw > g_fb_w) nw = g_fb_w;
    if (nh > g_fb_h) nh = g_fb_h;
    if (nw == f->w && nh == f->h) return;

    memset(&z, 0, sizeof(z));
    win_nome_zona(z.nome, f->id, f->giro + 1);
    z.byte = nw * nh * 4;
    z.flag = SHM_CREA;

    /* ! SE LA ZONA NUOVA NON SI PUO' AVERE NON SUCCEDE NIENTE: la finestra
     * resta della misura di prima, con i suoi pixel dov'erano. E' l'unico modo
     * di fallire che non lascia niente a meta'. */
    if (shm_apri(&z) != 0) {
        if (g_verboso)
            printf("wserver: niente zona da %ux%u: la finestra %u resta com'e'\n",
                   nw, nh, f->id);
        return;
    }

    nuovo   = (unsigned int *)z.virt;
    vecchio = (unsigned int *)f->zona_virt;

    /* ! CIO' CHE C'ERA SI PORTA DIETRO, e il resto prende il colore di
     * partenza. Il client ridisegnera' tutto appena legge il messaggio, ma fra
     * il cambio e il suo ridisegno passa qualche fotogramma: senza la copia si
     * vedrebbe un lampo di grigio a ogni ridimensionamento. */
    {
        unsigned int n = nw * nh, k;
        for (k = 0; k < n; k++) nuovo[k] = C_CLIENT;
    }

    cw = (nw < f->w) ? nw : f->w;
    ch = (nh < f->h) ? nh : f->h;
    if (vecchio)
        for (j = 0; j < ch; j++)
            for (i = 0; i < cw; i++)
                nuovo[j * nw + i] = vecchio[j * f->w + i];

    if (f->zona_virt) shm_chiudi((void *)f->zona_virt);

    f->zona_virt = z.virt;
    f->zona_byte = z.byte;
    f->w         = nw;
    f->h         = nh;
    f->giro++;
    f->da_dire   = MISURA_TENTATIVI;

    if (g_verboso)
        printf("wserver: la finestra %u diventa %ux%u (giro %u)\n",
               f->id, nw, nh, f->giro);

    dire_misura(f);
    sporca_tutto();
}

static void distruggi(int idx)
{
    unsigned int k, j = 0;

    if (idx < 0) return;

    if (g_fin[idx].zona_virt) shm_chiudi((void *)g_fin[idx].zona_virt);
    memset(&g_fin[idx], 0, sizeof(Finestra));

    for (k = 0; k < g_n_ordine; k++)
        if (g_ordine[k] != (unsigned int)idx) g_ordine[j++] = g_ordine[k];
    g_n_ordine = j;
    if (g_trascino == idx) g_trascino = -1;
    if (g_ridim == idx) g_ridim = -1;
    if (g_giu_su == idx) g_giu_su = -1;

    /* ! CHI SE NE VA SI PORTA VIA IL FUOCO, e va ridato a qualcuno. Senza,
     * g_fuoco resterebbe l'indice di uno slot azzerato: i tasti finirebbero a
     * una finestra che non c'e' piu' — cioe' da nessuna parte, e chiudere un
     * editor renderebbe muto quello rimasto aperto. */
    if (g_fuoco == idx) fuoco_ricalcola();

    sporca_tutto();
}

/* -----------------------------------------------------------------------------
 * Le finestre dei client morti
 *
 * ! UN'APPLICAZIONE CHE ESCE NON DICE NIENTE AL SERVER, e non c'e' modo di
 * obbligarla: se muore per un errore non ha nemmeno la possibilita' di dirlo.
 * La sua finestra resta disegnata dov'era, con dentro l'ultima cosa che aveva
 * scritto — e chi la guarda ci clicca sopra e non capisce perche' non risponde.
 * Trovato provando il dialogo modale: dopo Ctrl+Q la shell dice «terminato» e
 * il rettangolo bianco dell'editor resta sullo schermo.
 *
 * ! SI CHIEDE AL KERNEL CHI E' VIVO, NON SI ASPETTA CHE ipc_send FALLISCA.
 * Aspettare l'errore vuol dire accorgersene solo quando c'e' qualcosa da
 * mandare, cioe' quando qualcuno clicca sul fantasma: la finestra sparirebbe
 * al primo clic, che e' PEGGIO di non farla sparire — sembrerebbe che il clic
 * abbia fatto qualcosa.
 *
 * ! GLI ZOMBIE CONTANO COME MORTI. Un processo uscito e non ancora raccolto dal
 * padre resta nell'elenco: e' li' solo perche' qualcuno legga il suo codice di
 * uscita, non ha piu' niente sullo schermo da difendere.
 *
 * ! E SE L'ELENCO NON CI STA, NON SI RACCOGLIE NIENTE. Un elenco troncato vuol
 * dire pid vivi che non risultano, cioe' finestre VIVE distrutte: il danno
 * peggiore possibile qui. Fra il non pulire e il pulire in base a un elenco di
 * cui non ci si fida, si non pulisce.
 * --------------------------------------------------------------------------- */
#define PROC_ZOMBIE     4
#define PID_TRACCIATI   64

static void raccogli_morti(void)
{
    ProcInfo     v[PROCINFO_MAX_BATCH];
    unsigned int vivi[PID_TRACCIATI];
    unsigned int n_vivi = 0, start = 0;
    int n, i, k;

    for (;;) {
        n = procinfo(v, PROCINFO_MAX_BATCH, start);
        if (n <= 0) break;

        for (i = 0; i < n; i++) {
            if (v[i].state == PROC_ZOMBIE) continue;
            if (n_vivi >= PID_TRACCIATI) return;    /* elenco troncato: basta */
            vivi[n_vivi++] = v[i].pid;
        }
        start += (unsigned int)n;
        if (n < PROCINFO_MAX_BATCH) break;
    }

    if (n_vivi == 0) return;        /* non ho saputo niente: non tocco niente */

    for (i = 0; i < FINESTRE_MAX; i++) {
        int vivo = 0;

        if (!g_fin[i].usata) continue;

        for (k = 0; k < (int)n_vivi; k++)
            if (vivi[k] == g_fin[i].pid) { vivo = 1; break; }

        if (!vivo) {
            if (g_verboso)
                printf("wserver: il pid %u non c'e' piu', tolgo la sua finestra\n",
                       g_fin[i].pid);
            distruggi(i);
        }
    }
}

/* Rende 1 se ha servito un messaggio, 0 se la coda era vuota. */
static int servi_messaggio(void)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];

    if (ipc_recv_timeout(&meta, buf, sizeof(buf), 5) < 0) return 0;

    /* La risposta del mouse arriva nella stessa coda di tutto il resto: e'
     * qui che si distingue, e da nessun'altra parte. */
    if (meta.tipo == KBD_MSG_KEY) {
        unsigned int k = 0;
        if (meta.len >= sizeof(k)) { memcpy(&k, buf, sizeof(k)); kbd_tasto(k); }
        else g_key_chiesta = 0;
        return 1;
    }

    if (meta.tipo == MOUSE_MSG_STATO) {
        MouseStato s;
        g_mouse_chiesto = 0;
        if (meta.len >= sizeof(s)) {
            memcpy(&s, buf, sizeof(s));
            mouse_stato(&s);
            mouse_agisci();
        }
        return 1;
    }

    switch (meta.tipo) {
    case WIN_MSG_CREA:
        if (meta.len >= sizeof(WinCrea)) crea(meta.sender_pid, (WinCrea *)buf);
        break;

    /* =====================================================================
     * ! SI SPEGNE LA SCRIVANIA, e non e' la crocetta di una finestra.
     *
     * Fino a oggi «Esci» dal program manager chiudeva solo la scrivania e
     * lasciava acceso il server: la ragione scritta in pm.c era che il server
     * e' di chi lo ha avviato e poteva avere altre finestre aperte. Vera in
     * astratto; in pratica lasciava una macchina con la grafica accesa e
     * nessuno dentro, e per spegnerla davvero bisognava sapere cosa uccidere.
     *
     * ! ALLE APPLICAZIONI SI CHIEDE, NON LE SI UCCIDE. Va a ognuna lo stesso
     * WIN_EV_CHIUDI della crocetta — un messaggio che sanno gia' gestire —
     * cosi' chi ha qualcosa da salvare fa in tempo. Poi si aspetta: chi se ne
     * va libera la sua finestra, e `raccogli_morti` se ne accorge.
     *
     * ! E SI ASPETTA UN TEMPO DICHIARATO, non «finche' non sono uscite tutte».
     * Un'applicazione bloccata non deve poter tenere accesa la grafica per
     * sempre: dopo il tempo si va avanti lo stesso, e chi resta se lo porta
     * via la fine del server.
     * ===================================================================== */
    case WIN_MSG_SPEGNI: {
        int n, giri;

        log_seriale("wserver: spegnimento chiesto");

        /* Nell'elenco del server ci sono SOLO finestre di primo livello: i
         * controlli del toolkit vivono dentro il client e non si registrano
         * qui. Quindi ognuna di queste e' un'applicazione da avvisare. */
        for (n = 0; n < FINESTRE_MAX; n++)
            if (g_fin[n].usata)
                manda_evento(&g_fin[n], WIN_EV_CHIUDI, 0, 0, 0, 0);

        for (giri = 0; giri < 100; giri++) {
            int vive = 0;

            for (n = 0; n < 16 && servi_messaggio(); n++) { }
            raccogli_morti();

            for (n = 0; n < FINESTRE_MAX; n++)
                if (g_fin[n].usata) vive++;
            if (vive == 0) break;

            usleep(20000);
        }

        console_grafica(2);     /* la console non e' piu' della grafica */
        modo_testo();
        log_seriale("wserver: spento");
        exit(0);
    }

    case WIN_MSG_DISTRUGGI: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        /* ! SI CONTROLLA CHE SIA SUA. Senza, un processo qualunque potrebbe
         * chiudere le finestre di un altro conoscendone il numero — e i
         * numeri sono piccoli e consecutivi. */
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) distruggi(idx);
        break;
    }

    case WIN_MSG_SPOSTA: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) {
            g_fin[idx].x = w->x;
            g_fin[idx].y = w->y;
            sporca_tutto();
        }
        break;
    }

    /* ! LA MISURA LA PUO' CHIEDERE ANCHE IL CLIENT, e non solo il mouse:
     * un'applicazione che carica un documento piu' grande sa quanto spazio le
     * serve meglio di chi guarda. Passa dalla stessa funzione del
     * trascinamento — due strade per la stessa cosa vorrebbero dire due modi
     * di sbagliarla.
     *
     * ! E QUI NON SI GUARDA WIN_ST_RIDIM: quel bit dice se il SERVER puo'
     * cambiare la misura sotto i piedi al client. Chiederla da soli e'
     * un'altra cosa, e chi la chiede sa gia' come rispondersi. */
    case WIN_MSG_MISURA: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid &&
            w->larghezza && w->altezza)
            ridimensiona(idx, w->larghezza, w->altezza);
        break;
    }

    case WIN_MSG_TITOLO: {
        WinTitolo *t = (WinTitolo *)buf;
        int idx;
        if (meta.len < sizeof(WinTitolo)) break;
        idx = trova_id(t->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) {
            memcpy(g_fin[idx].titolo, t->titolo, WIN_TITOLO_LEN);
            g_fin[idx].titolo[WIN_TITOLO_LEN - 1] = '\0';
            sporca_tutto();
        }
        break;
    }

    case WIN_MSG_PRIMO: {
        WinRegione *w = (WinRegione *)buf;
        int idx;
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid) { in_cima(idx); sporca_tutto(); }
        break;
    }

    case WIN_MSG_AGGIORNA: {
        WinRegione *w = (WinRegione *)buf;
        int idx;

        sporca_tutto();
        /* Il client ha finito di disegnare. Componendo a ogni giro non c'e'
         * niente da fare: resta nel protocollo perche' quando ci sara' la
         * lista delle regioni sporche sara' QUESTO il messaggio che la
         * riempie, e cambiare il protocollo dopo costa piu' che prevederlo.
         *
         * ! MA LA MISURA CHE PORTA DENTRO E' GIA' UTILE OGGI: e' la ricevuta
         * di WIN_MSG_MISURATA. Un client che disegna dichiarando la misura
         * NUOVA ha per forza aperto la zona nuova — non c'e' altro modo di
         * saperla — e quindi il messaggio e' arrivato e non va ripetuto. Non
         * serve un messaggio in piu' per dire una cosa che si sa gia'. */
        if (meta.len < sizeof(WinRegione)) break;
        idx = trova_id(w->id);
        if (idx >= 0 && g_fin[idx].pid == meta.sender_pid &&
            w->larghezza == g_fin[idx].w && w->altezza == g_fin[idx].h)
            g_fin[idx].da_dire = 0;
        break;
    }

    default:
        break;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
    VideoInfo v;
    MmioZona  m;
    int i, chiesta = -1;

    /* =========================================================================
     * ! «-i» SI RISPONDE E SI ESCE, PRIMA DI QUALUNQUE ALTRA COSA.
     *
     * `hwconfig -d` sceglie quali driver installare provandoli uno per uno con
     * `-i`: si aspetta che ognuno dica cosa fa e ritorni. Fino al 17 agosto
     * 2026 wserver non conosceva quel flag, lo ignorava, e PARTIVA PER DAVVERO
     * — e un server non esce mai. L'installazione dal CD si fermava li', dopo
     * aver gia' sostituito kernel e stage2: un disco a meta', e un log che
     * finiva con «wserver: entro nel ciclo» invece che con un errore.
     *
     * Non si e' mai visto installando dal floppy, che nel suo catalogo
     * wserver.drv non ce l'ha. E' bastato che l'installazione dal CD diventasse
     * una cosa che si prova.
     * ========================================================================= */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            printf("wserver: il server a finestre di EX-OS.\n");
            printf("         Nessuna periferica propria: usa il framebuffer\n");
            printf("         che ha impostato Stage 2. Serve solo a chi vuole\n");
            printf("         l'interfaccia grafica - si avvia con  exwin\n");
            return 0;
        }
    }

    /* ! CHI SIAMO E DOVE SIAMO, PRIMA DI DIRE QUALUNQUE COSA. Il primo
     * messaggio con lo schermo citava g_console mentre era ancora zero, e
     * diceva «console 0» di un processo che girava sulla 1: un numero
     * sbagliato dentro una diagnostica costa piu' di nessun numero, perche'
     * lo si crede. */
    {
        ConsoleInfo ci;

        g_mio_pid = (unsigned int)getpid();
        if (console_info(&ci) == 0) g_console = ci.mia;
    }

    log_seriale("wserver: sono partito");

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'v') g_verboso = 1;
        if (argv[i][0] == '-' && argv[i][1] == 't') g_forza_tastiera = 1;
        if (argv[i][0] == '-' && argv[i][1] == 'c' && i + 1 < argc)
            chiesta = atoi(argv[++i]);
    }

    /* ! -c N: SI RIPARTE SU UN'ALTRA CONSOLE, E POI SI ESCE.
     *
     * E' il modo di stare su una console tutta propria mentre su un'altra
     * continua a girare una shell — con Alt+Fn si passa dall'una all'altra.
     * Una console non si puo' cambiare a se stessi: la si sceglie quando un
     * processo NASCE, ed e' per questo che serve rilanciarsi.
     *
     * ! E SI CONTROLLA DI NON ESSERCI GIA', o si rinascerebbe per sempre. */
    {
        ConsoleInfo ci;

        /* ! E L'AMBIENTE SI PASSA, o rinascere qui lo BUTTA VIA. `envp` nullo
         * non vuol dire «eredita»: nel kernel (syscall_impl.c) vuol dire
         * ambiente VUOTO, e al figlio resta solo il blocco [env] di
         * kernel.cfg. Siccome tutta la scrivania nasce da qui, l'effetto era
         * che un utente entrato come `tizio` — con `HOME=/home/tizio` messo da
         * login — vedeva `HOME=/` in ogni applicazione grafica, cioe' nessun
         * programma della scrivania sapeva dov'era la casa di chi lo stava
         * usando. Si e' visto quando il browser ha provato a tenersi la cache
         * in $HOME/.app/browser/cache e se l'e' ritrovata nella radice. */
        if (chiesta >= 0 && console_info(&ci) == 0 && ci.mia != (unsigned int)chiesta) {
            int pid = spawn_su_console(argv[0], argv, environ, 0, 0, chiesta);

            if (pid < 0) {
                printf("wserver: non riesco a nascere sulla console %d\n", chiesta);
                return 1;
            }
            printf("wserver: riparto sulla console %d (PID %d); "
                   "con Alt+F%d ci si va\n", chiesta, pid, chiesta + 1);
            return 0;
        }
    }

    if (video_info(&v) != 0) {
        log_seriale("wserver: video_info non risponde");
        printf("wserver: video_info non risponde\n");
        return 1;
    }
    if (v.larghezza == 0 || v.fisico == 0) {
        log_seriale("wserver: lo schermo e' in modo TESTO");
        printf("wserver: lo schermo e' in modo TESTO.\n");
        printf("         Scegli una risoluzione e riavvia:  /dev/svga.drv 800x600\n");
        return 1;
    }

    /* =====================================================================
     * ! SI CHIEDE fb_map(), NON PIU' mmio_map(), dal 17 agosto 2026.
     *
     * mmio_map mappa un indirizzo fisico QUALUNQUE scelto da chi chiama, e per
     * questo e' riservata a root: con i registri di un dispositivo si arriva
     * al DMA, e col DMA a tutta la RAM. Finche' bastava chiamarsi `.drv` per
     * ottenerla, un utente normale che eseguiva un driver possedeva la
     * macchina — i permessi sui file accanto a una porta che non chiedeva
     * niente.
     *
     * fb_map non prende argomenti: il framebuffer lo sa il kernel. Chi chiama
     * non puo' sbagliare indirizzo e chi attacca non puo' sceglierne uno. E'
     * anche cio' che permette a QUESTO server di girare senza privilegi —
     * cioe' alla grafica di esistere in multiutenza.
     * ===================================================================== */
    (void)m;
    g_fb = (unsigned char *)fb_map();
    if (g_fb == 0) {
        log_seriale("wserver: fb_map del framebuffer rifiutata");
        printf("wserver: non riesco a mappare il framebuffer (%s).\n",
               strerror(errno));
        printf("         Serve un modo grafico: /dev/svga.drv 800x600\n");
        return 1;
    }

    /* ! SI GUARDA UNA VOLTA SOLA, ALL'AVVIO. Chiedere CPUID a ogni riga
     * costerebbe piu' di quanto MMX faccia risparmiare. */
    g_mmx = mmx_c_e();

    /* ! «-nommx» SPEGNE LA STRADA VELOCE, e non e' un'opzione per curiosi: la
     * strada di ripiego esiste per le macchine senza MMX, e su una macchina
     * CON MMX non verrebbe mai eseguita. Un codice che non si esegue mai e' un
     * codice di cui non si sa se funziona — e questo flag e' anche il modo di
     * provare che le due strade disegnano lo STESSO schermo, confrontando due
     * fotografie byte per byte. */
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-nommx") == 0) g_mmx = 0;

    g_fb_passo = v.passo;
    g_fb_w     = v.larghezza;
    g_fb_h     = v.altezza;
    g_fb_bit   = v.bit;

    /* ! SULLA SERIALE, non con printf: questo processo gira su una console
     * sua, e un messaggio scritto li' non lo legge nessuno. E' lo stesso
     * motivo per cui esiste SYS_LOG. */
    log_seriale(g_mmx ? "wserver: MMX attivo, otto byte per volta"
                      : "wserver: MMX assente, quattro byte per volta");
    printf("wserver: schermo %ux%u a %u bit, framebuffer fisico 0x%x\n",
           g_fb_w, g_fb_h, g_fb_bit, v.fisico);
    {
        /* ! ANCHE SULLA SERIALE, perche' se giriamo su una console non
         * visibile la riga qui sopra non la legge nessuno. */
        char m[120];
        sprintf(m, "wserver: schermo %ux%u a %u bit, fb 0x%x, console %u",
                g_fb_w, g_fb_h, g_fb_bit, v.fisico, g_console);
        log_seriale(m);
    }

    win_nome_servizio(g_servizio, sizeof(g_servizio));

    if (ipc_register(g_servizio) < 0) {
        printf("wserver: ipc_register('%s') fallita - ce n'e' gia' uno?\n",
               g_servizio);
        return 1;
    }

    {
        g_kbd_pid = ipc_lookup(KBD_SERVICE_NAME);
        if (g_kbd_pid < 0)
            printf("wserver: !  servizio 'kbd' assente: niente tastiera\n");
        else if (!g_forza_tastiera)
            printf("wserver: la tastiera e' mia solo in primo piano "
                   "(console %u); con -t la prendo comunque\n", g_console);
    }

    mouse_trova();
    if (g_mouse_pid < 0)
        printf("wserver: !  nessun mouse: le finestre non si potranno muovere\n");

    g_px = (int)g_fb_w / 2;
    g_py = (int)g_fb_h / 2;

    printf("wserver: servizio '%s' attivo\n", g_servizio);
    /* ! DA QUI IN AVANTI QUESTA CONSOLE E' DELLA GRAFICA, e il kernel lo sa:
     * il driver di tastiera glielo chiede per decidere se Alt+F<ultima> porti
     * da qualche parte. Prima di adesso non lo era davvero — il framebuffer
     * poteva ancora non essere mappato — e dirlo prima avrebbe aperto la porta
     * su una stanza non ancora arredata.
     *
     * ! E NON SERVE DISDIRLA MORENDO MALE: il kernel ricontrolla che chi l'ha
     * presa sia ancora vivo, quindi un wserver ucciso libera la console da
     * solo. La si lascia comunque all'uscita pulita, perche' una cosa che si
     * puo' dire subito non si fa scoprire a qualcun altro. */
    console_grafica(1);

    log_seriale("wserver: servizio attivo, entro nel ciclo");

    /* ! IL CICLO NON ASPETTA A LUNGO SU NIENTE. Ha tre cose da fare — leggere
     * il mouse, servire i client, ricomporre — e fermarsi a lungo su una vuol
     * dire non fare le altre. Quando ci sara' la lista delle regioni sporche
     * questo diventera' un poll() vero su FD_IPC piu' il mouse, che e'
     * esattamente cio' per cui SYS_POLL esiste. */
    for (;;) {
        static unsigned int giri = 0;
        int n;

        mouse_chiedi();
        kbd_giro();

        /* ! SI SVUOTA LA CODA, NON SE NE PRENDE UNO PER GIRO. Lasciarci
         * dentro dei messaggi vuol dire tenerla piena, e una mailbox piena
         * fa fallire chi ci scrive — che qui vuol dire perdere i tasti e,
         * peggio, farsi rimettere la console in cooked dal servizio 'kbd'.
         * Il tetto c'e' perche' un client impazzito non ci tenga fermi. */
        for (n = 0; n < 16 && servi_messaggio(); n++) { }

        /* ! UNA VOLTA AL SECONDO, NON A OGNI GIRO. Il giro e' di 20 ms e
         * procinfo e' una syscall che copia una tabella: farla cinquanta volte
         * al secondo per una cosa che cambia quando un'applicazione si chiude
         * sarebbe spendere sempre per accorgersi prima di qualcosa che non ha
         * fretta. */
        if (++giri >= 50) { giri = 0; raccogli_morti(); }

        /* ! LA NOTIZIA DEL CAMBIO DI ZONA SI RIPETE FINCHE' NON ARRIVA, ogni
         * dieci giri — cioe' cinque volte al secondo, non cinquanta: la
         * mailbox del client la si riempirebbe con la cura stessa. Il perche'
         * per esteso sta sopra ridimensiona(). */
        if ((giri % 10) == 0) {
            for (n = 0; n < FINESTRE_MAX; n++) {
                if (!g_fin[n].usata || !g_fin[n].da_dire) continue;

                g_fin[n].da_dire--;
                if (g_fin[n].da_dire) dire_misura(&g_fin[n]);
                else if (g_verboso)
                    printf("wserver: la finestra %u non ha mai preso la misura "
                           "nuova: smetto di dirglielo\n", g_fin[n].id);
            }
        }

        if (g_sporco && g_visibile) {
            /* ! IL RITAGLIO SI RIMETTE A «TUTTO» SUBITO DOPO, e non e' una
             * cortesia: qualunque cosa disegni fuori da componi() — e un
             * giorno ce ne sara' una — troverebbe altrimenti un ritaglio
             * lasciato li' da un movimento del mouse, e sparirebbe. */
            if (!g_sp_tutto && g_sp_x1 > g_sp_x && g_sp_y1 > g_sp_y)
                clip_metti(g_sp_x, g_sp_y, g_sp_x1 - g_sp_x, g_sp_y1 - g_sp_y);

            componi();

            clip_tutto();
            g_sporco = 0;
            g_sp_tutto = 0;
            g_sp_x = g_sp_x1 = 0;
            g_sp_y = g_sp_y1 = 0;
        }
        usleep(20000);
    }
}

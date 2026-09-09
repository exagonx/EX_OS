/* =============================================================================
 * bin/automount/automount.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SI INFILA UNA CHIAVETTA E I FILE CI SONO
 *
 *     automount &        resta a guardare: monta quel che compare
 *     automount -1       una passata sola, e se ne va
 *     automount -v       dice anche quel che non fa
 *
 * Dove finiscono le cose:
 *
 *     /USB/DRIVE0        una chiavetta senza tabella delle partizioni
 *     /USB/HDD0p1        la prima partizione del primo disco
 *     /USB/HDD0p2        la seconda
 *     /USB/HDD1p1        e il disco dopo ricomincia da capo
 *
 * -----------------------------------------------------------------------------
 * ! NON SORVEGLIA L'USB: SORVEGLIA I DISPOSITIVI A BLOCCHI
 *
 * Chiedere al driver USB «e' arrivato qualcosa?» avrebbe voluto dire un
 * sorvegliante per ogni controller — quattro, fra UHCI, OHCI, EHCI e xHCI — e
 * uno nuovo per ogni bus che verra'. Invece l'elenco dei dispositivi a blocchi
 * ce l'ha gia' il kernel e si legge con blkinfo(): un secondo di sonno, un
 * confronto con l'elenco di prima, e chi e' comparso si monta.
 *
 * Cosi' vale per una chiavetta come per un disco di rete o per un'immagine
 * montata da un processo, senza sapere niente di nessuno dei due. E' la stessa
 * ragione per cui la cucitura sta in kernel/block/blkr3.c e non dentro un
 * driver USB.
 *
 * -----------------------------------------------------------------------------
 * ! ZERO PARTIZIONI NON E' UN ERRORE, ED E' IL CASO PIU' COMUNE
 *
 * Quasi tutte le chiavette sono un filesystem che comincia al settore 0 — i
 * lettori di dischetti USB lo sono sempre. La regola quindi non e' «se ha una
 * partizione sola», e':
 *
 *     nessuna tabella  ->  il volume e' tutto il dispositivo  ->  DRIVE<n>
 *     una tabella      ->  ogni partizione leggibile          ->  HDD<n>p<m>
 *
 * ! E IL NUMERO SEGUE IL DISPOSITIVO, NON LA PARTIZIONE. «HDD0, HDD1, HDD2»
 * numerati per partizione funziona finche' i dischi sono uno: al secondo, le
 * partizioni dei due si mescolano nella stessa numerazione e non si sa piu' di
 * chi e' quale. HDD0p1 e' la stessa forma di hd0p1, che `disk` e `fdisk`
 * stampano gia'.
 *
 * -----------------------------------------------------------------------------
 * ! CIO' CHE NON SI SA LEGGERE SI SALTA IN SILENZIO
 *
 * Un disco esterno di Windows ha quasi sempre una partizione NTFS, che EX-OS
 * non legge. Un errore rosso a ogni inserimento insegna solo a non guardare i
 * messaggi: si dice a bassa voce (-v) e si va avanti con le altre.
 *
 * -----------------------------------------------------------------------------
 * ! QUEL CHE NON FA, DETTO SUBITO
 *
 * Smonta quel che SPARISCE dall'elenco, cioe' quando il driver che lo serviva
 * se ne va. Una chiavetta sfilata di brutto NON sparisce: i driver USB di oggi
 * non guardano le porte mentre servono un disco, quindi il dispositivo resta
 * li' e ogni accesso rende errore finche' non si smonta a mano. Chiudere anche
 * quel caso vuol dire far sorvegliare le porte ai driver dei controller, ed e'
 * lavoro loro, non di questo file.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `automount -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("automount", "0.001");

#define TIPO_PART     3
#define TIPO_RING3    5

#define RADICE        "/USB"
#define MAX_SEGUITI   8      /* dispositivi serviti seguiti insieme */
#define MAX_MONTAGGI  4      /* punti per dispositivo: quattro primarie */
#define PERIODO_MS    1000

typedef struct {
    char nome[BLKINFO_NOME_MAX];              /* "usb0" */
    int  usato;
    int  visto;                               /* c'e' ancora in questo giro */
    int  numero;                              /* il suo posto: DRIVE<n>/HDD<n> */
    int  n_punti;
    char punto[MAX_MONTAGGI][32];
} Seguito;

static Seguito g_seg[MAX_SEGUITI];
static int     g_verboso = 0;
static int     g_radice_ok = 0;

static void aiuto(void)
{
    printf("automount - monta da solo le chiavette e i dischi USB\n\n");
    printf("  automount &     resta a guardare (un giro al secondo)\n");
    printf("  automount -1    una passata sola\n");
    printf("  automount -v    dice anche quel che salta e perche'\n\n");
    printf("  %s/DRIVE0   una chiavetta senza tabella delle partizioni\n", RADICE);
    printf("  %s/HDD0p1   la prima partizione del primo disco\n", RADICE);
    printf("\nVa lanciato da root, come `mount`. Il posto giusto e'\n");
    printf("/boot/avvio.sh, dopo i driver: e' li' che sta la rete, e per la\n");
    printf("stessa ragione - la catena vuole un ordine.\n");
}

/* La directory su cui si monta dev'esserci. Su un sistema installato la si
 * crea; da CD la radice e' di sola lettura e /USB dev'essere gia' nell'immagine
 * (ci pensa il Makefile). Rende 1 se c'e'. */
static int radice_pronta(void)
{
    struct stat st;

    if (stat(RADICE, &st) == 0) return 1;

    if (mkdir(RADICE, 0755) == 0) {
        printf("automount: creata %s\n", RADICE);
        return 1;
    }

    printf("automount: %s non c'e' e non riesco a crearla.\n", RADICE);
    printf("           Se la radice e' un CD e' normale: quella directory va\n");
    printf("           messa nell'immagine, non creata a macchina accesa.\n");
    return 0;
}

/* Il primo numero libero: due chiavette insieme fanno DRIVE0 e DRIVE1. */
static int numero_libero(void)
{
    int n, i;

    for (n = 0; n < MAX_SEGUITI; n++) {
        int preso = 0;
        for (i = 0; i < MAX_SEGUITI; i++)
            if (g_seg[i].usato && g_seg[i].numero == n) preso = 1;
        if (!preso) return n;
    }
    return 0;
}

static Seguito *trova(const char *nome)
{
    int i;
    for (i = 0; i < MAX_SEGUITI; i++)
        if (g_seg[i].usato && strcmp(g_seg[i].nome, nome) == 0) return &g_seg[i];
    return NULL;
}

/* Monta `dev` su `punto` e se ci riesce lo annota. */
static void monta(Seguito *s, const char *dev, const char *punto)
{
    int r;

    if (s->n_punti >= MAX_MONTAGGI) return;

    r = mount(dev, punto, 0);
    if (r == 0) {
        strncpy(s->punto[s->n_punti], punto, sizeof(s->punto[0]) - 1);
        s->punto[s->n_punti][sizeof(s->punto[0]) - 1] = '\0';
        s->n_punti++;
        printf("automount: %s -> %s\n", dev, punto);
        return;
    }

    /* ! UN FILESYSTEM CHE NON SAPPIAMO LEGGERE NON E' UN GUASTO. NTFS, ext4,
     * una partizione di scambio: si saltano. Il rumore lo si fa solo con -v. */
    if (g_verboso || (r != -EINVAL && r != -EIO))
        printf("automount: %s su %s: %s\n", dev, punto,
               (r == -EINVAL) ? "filesystem che non so leggere, salto"
                              : "non si monta");
}

/* L'elenco dei dispositivi a blocchi, tutto in una volta. Rende quanti. */
static int elenco(BlkInfo *b, int max)
{
    unsigned int start = 0;
    int quanti = 0, n;

    for (;;) {
        if (quanti >= max) break;
        n = blkinfo(b + quanti, 8, start);
        if (n <= 0) break;
        quanti += n;
        start  += (unsigned int)n;
        if (n < 8) break;
    }
    return quanti;
}

/* Un dispositivo servito da un driver e' comparso. */
static void arrivato(const char *nome)
{
    Seguito *s = NULL;
    BlkInfo  dopo[16];
    char     punto[32];
    int      i, n, quanti;

    for (i = 0; i < MAX_SEGUITI; i++) if (!g_seg[i].usato) { s = &g_seg[i]; break; }
    if (s == NULL) {
        printf("automount: troppi dispositivi insieme, %s lo salto\n", nome);
        return;
    }

    /* ! IL NUMERO SI SCEGLIE PRIMA DI OCCUPARE LO SLOT. Segnandolo usato e
     * poi cercando il primo libero, questo stesso slot risultava gia' preso
     * con il numero 0 — che memset() aveva appena azzerato — e la PRIMA
     * chiavetta finiva in DRIVE1. Un difetto che si vede solo guardando il
     * nome del punto di montaggio, perche' funziona tutto lo stesso. */
    n = numero_libero();

    memset(s, 0, sizeof(*s));
    s->usato  = 1;
    s->visto  = 1;
    s->numero = n;
    strncpy(s->nome, nome, sizeof(s->nome) - 1);

    /* ! LA SCANSIONE VA CHIESTA DA QUI, e non la puo' fare il driver: lui
     * leggerebbe il proprio disco e si aspetterebbe da solo. Vedi blkr3.h. */
    n = blk_scansiona(nome);

    if (n < 0) {
        printf("automount: %s: non riesco a leggerne la tabella (%d)\n", nome, n);
        s->usato = 0;
        return;
    }

    if (n == 0) {
        /* Nessuna tabella: il volume e' tutto il dispositivo. */
        snprintf(punto, sizeof(punto), "%s/DRIVE%d", RADICE, s->numero);
        monta(s, nome, punto);
    } else {
        /* ! L'ELENCO SI RILEGGE, e questa e' costata una prova. Le partizioni
         * NON esistevano quando abbiamo guardato: le ha appena create
         * blk_scansiona(), un attimo fa. Cercarle nella copia di prima vuol
         * dire non trovarne nessuna e dire «niente da montare» su un disco
         * partizionato benissimo. */
        quanti = elenco(dopo, 16);

        for (i = 0; i < quanti; i++) {
            const char *p = dopo[i].nome;
            unsigned int l = (unsigned int)strlen(nome);

            if (dopo[i].tipo != TIPO_PART) continue;
            if (strncmp(p, nome, l) != 0 || p[l] != 'p') continue;

            snprintf(punto, sizeof(punto), "%s/HDD%dp%s", RADICE, s->numero,
                     p + l + 1);
            monta(s, p, punto);
        }
    }

    if (s->n_punti == 0) {
        printf("automount: %s: niente da montare\n", nome);
        /* Si tiene comunque in elenco: cosi' non si riprova ogni secondo. */
    }
}

/* Un dispositivo e' sparito: il driver che lo serviva se n'e' andato. */
static void sparito(Seguito *s)
{
    int i;

    for (i = 0; i < s->n_punti; i++) {
        if (umount(s->punto[i]) == 0) printf("automount: smontato %s\n", s->punto[i]);
        else printf("automount: %s non si smonta (qualcuno ci sta dentro?)\n",
                    s->punto[i]);
    }

    printf("automount: %s non c'e' piu'\n", s->nome);
    s->usato = 0;
}

/* Un giro: rilegge l'elenco e agisce sulle differenze. */
static void giro(void)
{
    BlkInfo b[16];
    int     quanti, i;

    /* L'elenco intero, in una volta: confrontarlo a pezzi vorrebbe dire
     * decidere «e' sparito» su meta' della verita'. */
    quanti = elenco(b, 16);

    for (i = 0; i < MAX_SEGUITI; i++) g_seg[i].visto = 0;

    for (i = 0; i < quanti; i++) {
        Seguito *s;

        if (b[i].tipo != TIPO_RING3) continue;

        s = trova(b[i].nome);
        if (s != NULL) {
            /* ! UN DISPOSITIVO GUASTO VALE COME SPARITO. Il driver che lo
             * serviva e' morto mentre era montato: il kernel tiene lo slot
             * occupato apposta (o il prossimo dispositivo lo riuserebbe sotto
             * un montaggio aperto), quindi dall'elenco non se ne va. Ma li'
             * dentro non si legge piu' niente, e lasciarlo montato vuol dire
             * un /USB/DRIVE0 che risponde «errore di I/O» per sempre. */
            if (!b[i].guasto) s->visto = 1;
            else if (g_verboso) printf("automount: %s e' guasto\n", b[i].nome);
            continue;
        }

        /* Comparso guasto: non lo si monta nemmeno. */
        if (b[i].guasto) continue;

        printf("automount: e' comparso %s (%u settori)\n",
               b[i].nome, b[i].settori_lo);
        arrivato(b[i].nome);
    }

    for (i = 0; i < MAX_SEGUITI; i++)
        if (g_seg[i].usato && !g_seg[i].visto) sparito(&g_seg[i]);
}

int main(int argc, char **argv)
{
    int una_sola = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) { g_verboso = 1; continue; }
        if (strcmp(argv[i], "-1") == 0) { una_sola = 1; continue; }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            aiuto();
            return 0;
        }
        printf("automount: non conosco '%s'. Prova -h.\n", argv[i]);
        return 1;
    }

    memset(g_seg, 0, sizeof(g_seg));

    g_radice_ok = radice_pronta();
    if (!g_radice_ok) return 1;

    if (una_sola) { giro(); return 0; }

    printf("automount: guardo i dispositivi. Le chiavette finiscono in %s.\n",
           RADICE);

    for (;;) {
        giro();
        usleep(PERIODO_MS * 1000);
    }
}

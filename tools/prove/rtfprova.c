/* =============================================================================
 * tools/prove/rtfprova.c — lib/exrtf sull'host (@RTF, 30 settembre 2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cc -I lib/exrtf -o /tmp/rtfprova tools/prove/rtfprova.c lib/exrtf/rtf.c
 *     /tmp/rtfprova [file.rtf ...]
 *     /tmp/rtfprova -scrivi nostro.rtf     (l'esempio di WordPad, riscritto da noi)
 *
 * Senza argomenti: le prove qui dentro. Con dei file: li legge e stampa testo
 * e pezzi, per guardare un RTF vero (di WordPad, di LibreOffice).
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "exrtf.h"

static char          g_testo[1 << 20];
static ExRtfPezzo    g_pezzi[20000];
static unsigned char g_all[20000];
static char          g_out[1 << 20];
static int g_no = 0, g_n = 0;

static void ok(int c, const char *cosa)
{
    g_n++;
    printf("%s %2d %s\n", c ? "ok" : "NO", g_n, cosa);
    if (!c) g_no++;
}

static void leggi(ExRtfDoc *d, const char *rtf)
{
    exrtf_prepara(d, g_testo, sizeof(g_testo), g_pezzi, 20000, g_all, 20000);
    exrtf_leggi(d, rtf, (unsigned int)strlen(rtf));
}

/* The style of the byte at `pos` of the text. */
static const ExRtfStile *stile_a(const ExRtfDoc *d, unsigned int pos)
{
    unsigned int i;
    for (i = 0; i < d->pezzi_n; i++)
        if (pos >= d->pezzi[i].inizio && pos < d->pezzi[i].inizio + d->pezzi[i].lung)
            return &d->pezzi[i].stile;
    return 0;
}

static void mostra(const ExRtfDoc *d)
{
    unsigned int i;
    printf("testo: [%s]\n", d->testo);
    for (i = 0; i < d->pezzi_n && i < 40; i++) {
        const ExRtfStile *s = &d->pezzi[i].stile;
        printf("  pezzo %u: %u+%u fam %u f %u corpo %u %s%s%s col %06X [%.*s]\n", i,
               d->pezzi[i].inizio, d->pezzi[i].lung, s->famiglia, s->font, s->corpo,
               s->grassetto ? "B" : "", s->corsivo ? "I" : "", s->sottolineato ? "U" : "",
               s->colore, (int)d->pezzi[i].lung, d->testo + d->pezzi[i].inizio);
    }
    for (i = 0; i < d->par_n; i++) printf("  paragrafo %u: allineamento %u\n", i, d->allinea[i]);
}

static const char *WORDPAD =
    "{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat\\deflang1040"
    "{\\fonttbl{\\f0\\fnil\\fcharset0 Calibri;}{\\f1\\fswiss\\fcharset0 Arial;}"
    "{\\f2\\fmodern Courier New;}}\n"
    "{\\colortbl ;\\red255\\green0\\blue0;\\red0\\green0\\blue255;}\n"
    "{\\*\\generator Riched20 10.0.19041}\\viewkind4\\uc1 \n"
    "\\pard\\sa200\\sl276\\slmult1\\qc\\f0\\fs22\\lang16 Titolo \\b grassetto\\b0  e \\i corsivo\\i0\\par\n"
    "\\pard\\sa200\\sl276\\slmult1\\cf1\\ul rosso sottolineato\\ulnone\\cf0  poi \\f1\\fs28 Arial 14\\par\n"
    "\\f2 perch\\'e8 caff\\u232?\\par\n"
    "}\n";

static const char *LIBRE =
    "{\\rtf1\\ansi\\deff3\\adeflang1025\n"
    "{\\fonttbl{\\f0\\froman\\fprq2\\fcharset0 Times New Roman;}{\\f3\\fswiss Liberation Sans;}}\n"
    "{\\colortbl;\\red0\\green0\\blue0;\\red0\\green128\\blue0;}\n"
    "{\\stylesheet{\\s0\\snext0 Default Paragraph Style;}}\n"
    "{\\*\\generator LibreOffice/7.4.7.2$Linux_X86_64}{\\info{\\creatim\\yr2026\\mo9}}\n"
    "\\pard\\plain \\s0\\qr{\\f3\\fs24\\cf2 verde a destra}\\par\n"
    "\\pard\\plain \\s0\\qj{\\f3\\fs24 giustificato \\{graffe\\} e \\\\}\\par}\n";

int main(int argc, char **argv)
{
    static ExRtfDoc d, e;
    const ExRtfStile *s;
    unsigned int n, i;
    int j;

    if (argc > 2 && strcmp(argv[1], "-scrivi") == 0) {   /* the WordPad sample, written by us */
        FILE *f = fopen(argv[2], "wb");
        leggi(&d, WORDPAD);
        n = exrtf_scrivi(&d, g_out, sizeof(g_out));
        if (!f || !n) return 1;
        fwrite(g_out, 1, n, f); fclose(f);
        return 0;
    }
    if (argc > 1) {
        for (j = 1; j < argc; j++) {
            FILE *f = fopen(argv[j], "rb");
            static char buf[1 << 20];
            unsigned int m;
            if (!f) { printf("%s: non si apre\n", argv[j]); continue; }
            m = (unsigned int)fread(buf, 1, sizeof(buf) - 1, f); buf[m] = 0; fclose(f);
            exrtf_prepara(&d, g_testo, sizeof(g_testo), g_pezzi, 20000, g_all, 20000);
            printf("=== %s: %s\n", argv[j], exrtf_leggi(&d, buf, m) ? "letto" : "NON e' RTF");
            mostra(&d);
        }
        return 0;
    }

    printf("--- WordPad ---\n");
    leggi(&d, WORDPAD);
    ok(strcmp(d.testo, "Titolo grassetto e corsivo\nrosso sottolineato poi Arial 14\nperch\xC3\xA8 caff\xC3\xA8\n") == 0,
       "il testo, con le lettere accentate da \\'e8 e da \\u232 (senza il ? di riserva)");
    ok(d.par_n >= 3 && d.allinea[0] == EXRTF_CENTRO && d.allinea[1] == EXRTF_SINISTRA,
       "il primo paragrafo e' centrato, il secondo no (\\pard lo rimette)");
    s = stile_a(&d, (unsigned int)(strstr(d.testo, "grassetto") - d.testo));
    ok(s && s->grassetto && !s->corsivo && s->corpo == 11, "\\b: grassetto, corpo 11 da \\fs22");
    s = stile_a(&d, (unsigned int)(strstr(d.testo, "corsivo") - d.testo));
    ok(s && s->corsivo && !s->grassetto, "\\i e \\b0");
    s = stile_a(&d, (unsigned int)(strstr(d.testo, "rosso") - d.testo));
    ok(s && s->sottolineato && s->colore == 0xFF0000, "\\ul e \\cf1 dalla tabella dei colori");
    s = stile_a(&d, (unsigned int)(strstr(d.testo, "Arial") - d.testo));
    ok(s && s->famiglia == EXRTF_SANS && s->corpo == 14 && !s->sottolineato && s->colore == 0,
       "\\f1 (fswiss) e' sans, \\fs28 e' 14");
    ok(d.font_n == 3 && strcmp(d.font_nome[0], "Calibri") == 0 &&
       d.font_fam[2] == EXRTF_MONO, "la tabella dei caratteri: nomi e famiglie");
    ok(strstr(d.testo, "Riched20") == 0, "\\*\\generator e' saltato");

    printf("--- LibreOffice ---\n");
    leggi(&d, LIBRE);
    ok(strcmp(d.testo, "verde a destra\ngiustificato {graffe} e \\\n") == 0,
       "stylesheet e info saltati, \\{ \\} \\\\ letti");
    ok(d.allinea[0] == EXRTF_DESTRA && d.allinea[1] == EXRTF_GIUSTO, "\\qr e \\qj");
    s = stile_a(&d, 0);
    ok(s && s->colore == 0x008000 && s->famiglia == EXRTF_SANS && s->corpo == 12,
       "\\cf2 verde, \\f3 sans, \\fs24 dentro un gruppo");

    printf("--- scrivere e rileggere ---\n");
    leggi(&d, WORDPAD);
    n = exrtf_scrivi(&d, g_out, sizeof(g_out));
    ok(n > 0 && exrtf_e_rtf(g_out, n), "si scrive, e quel che esce e' RTF");
    {
        static char          t2[1 << 20];
        static ExRtfPezzo    p2[20000];
        static unsigned char a2[20000];
        int stessi = 1;

        exrtf_prepara(&e, t2, sizeof(t2), p2, 20000, a2, 20000);
        exrtf_leggi(&e, g_out, n);
        ok(strcmp(e.testo, d.testo) == 0, "rileggendo, lo stesso testo");
        for (i = 0; i < d.testo_n && stessi; i++) {
            const ExRtfStile *x = stile_a(&d, i), *y = stile_a(&e, i);
            if (!x || !y || x->grassetto != y->grassetto || x->corsivo != y->corsivo ||
                x->sottolineato != y->sottolineato || x->corpo != y->corpo ||
                x->colore != y->colore || x->famiglia != y->famiglia) stessi = 0;
        }
        ok(stessi, "rileggendo, lo stesso stile carattere per carattere");
        ok(e.allinea[0] == d.allinea[0] && e.allinea[1] == d.allinea[1], "e gli stessi allineamenti");
    }
    {
        ExRtfDoc v; static char t3[256]; static ExRtfPezzo p3[16]; static unsigned char a3[16];
        ExRtfStile st;
        exrtf_prepara(&v, t3, sizeof(t3), p3, 16, a3, 16);
        exrtf_stile_base(&st);
        exrtf_aggiungi(&v, "a{b}\\c", 6, &st);
        n = exrtf_scrivi(&v, g_out, sizeof(g_out));
        ok(strstr(g_out, "a\\{b\\}\\\\c") != 0, "graffe e barre si scrivono protette");
    }
    ok(!exrtf_e_rtf("ciao", 4), "un testo semplice non e' RTF");

    printf("--- modificare (tappa 2) ---\n");
    {
        ExRtfDoc v; static char t4[256]; static ExRtfPezzo p4[32]; static unsigned char a4[16];
        ExRtfStile base, gr, x;
        int buoni = 1;

        exrtf_prepara(&v, t4, sizeof(t4), p4, 32, a4, 16);
        exrtf_stile_base(&base);
        gr = base; gr.grassetto = 1;
        exrtf_inserisci(&v, 0, "ciao mondo", 10, &base);
        exrtf_inserisci(&v, 5, "bel ", 4, &gr);
        ok(strcmp(v.testo, "ciao bel mondo") == 0 && v.pezzi_n == 3 &&
           v.pezzi[1].inizio == 5 && v.pezzi[1].lung == 4 && v.pezzi[1].stile.grassetto,
           "inserire in mezzo con un altro stile: tre pezzi");
        exrtf_inserisci(&v, 9, "!", 1, &base);
        ok(strcmp(v.testo, "ciao bel !mondo") == 0 && v.pezzi_n == 3 && v.pezzi[2].lung == 6,
           "inserire con lo stile del vicino: si unisce, nessun pezzo nuovo");
        exrtf_cancella(&v, 4, 9);
        ok(strcmp(v.testo, "ciao!mondo") == 0 && v.pezzi_n == 1 && v.pezzi[0].lung == 10,
           "cancellare il pezzo in grassetto: i due vicini tornano uno");

        exrtf_allinea(&v, 0, 0, EXRTF_CENTRO);
        exrtf_inserisci(&v, 4, "\n", 1, &base);
        ok(v.par_n == 2 && v.allinea[0] == EXRTF_CENTRO && v.allinea[1] == EXRTF_CENTRO,
           "a capo in un paragrafo centrato: due paragrafi centrati");
        exrtf_allinea(&v, 6, 6, EXRTF_DESTRA);
        ok(v.allinea[0] == EXRTF_CENTRO && v.allinea[1] == EXRTF_DESTRA &&
           exrtf_paragrafo(&v, 4) == 0 && exrtf_paragrafo(&v, 5) == 1,
           "allineare il secondo solo; il '\\n' sta nel paragrafo che chiude");
        exrtf_cancella(&v, 4, 5);
        ok(v.par_n == 1 && v.allinea[0] == EXRTF_CENTRO && strcmp(v.testo, "ciao!mondo") == 0,
           "togliere l'a capo: resta il primo allineamento");

        exrtf_cambia(&v, 2, 7, EXRTF_C_CORSIVO, 1);
        ok(v.pezzi_n == 3 && v.pezzi[1].inizio == 2 && v.pezzi[1].lung == 5 && v.pezzi[1].stile.corsivo,
           "corsivo su una parte: il pezzo si spezza in tre");
        exrtf_cambia(&v, 0, 10, EXRTF_C_CORSIVO, 0);
        ok(v.pezzi_n == 1 && !v.pezzi[0].stile.corsivo, "tolto su tutto: torna uno solo");
        exrtf_cambia(&v, 0, 4, EXRTF_C_COLORE, 0xFF0000);
        exrtf_cambia(&v, 0, 4, EXRTF_C_CORPO, 20);
        ok(exrtf_stile_a(&v, 1, &x) && x.colore == 0xFF0000 && x.corpo == 20 &&
           exrtf_stile_a(&v, 10, &x) && x.colore == 0 && x.corpo == 12,
           "colore e corpo; alla fine del testo lo stile e' quello dell'ultimo byte");

        /* the runs always cover the text, in order, with no hole */
        for (i = 0, n = 0; i < v.pezzi_n; i++) {
            if (v.pezzi[i].inizio != n || v.pezzi[i].lung == 0) buoni = 0;
            n += v.pezzi[i].lung;
        }
        ok(buoni && n == v.testo_n, "i pezzi coprono il testo senza buchi");
    }
    {
        /* a family chosen in a document read from a file: the table grows */
        exrtf_prepara(&d, g_testo, sizeof(g_testo), g_pezzi, 20000, g_all, 20000);
        leggi(&d, LIBRE);
        n = d.font_n;
        exrtf_cambia(&d, 0, 5, EXRTF_C_FAMIGLIA, EXRTF_MONO);
        s = stile_a(&d, 0);
        ok(d.font_n == n + 1 && s && s->famiglia == EXRTF_MONO &&
           strcmp(d.font_nome[s->font], "Liberation Mono") == 0,
           "monospaziato in un documento senza: la tabella prende Liberation Mono");
        n = exrtf_scrivi(&d, g_out, sizeof(g_out));
        ok(n && strstr(g_out, "\\fmodern Liberation Mono;"), "e salvando la si scrive");
    }
    {
        /* full buffers: less is done, nothing breaks */
        ExRtfDoc v; static char t5[8]; static ExRtfPezzo p5[4]; static unsigned char a5[2];
        ExRtfStile st;
        exrtf_prepara(&v, t5, sizeof(t5), p5, 4, a5, 2);
        exrtf_stile_base(&st);
        n = exrtf_inserisci(&v, 0, "a\nb\nc", 5, &st);
        ok(n == 3 && strcmp(v.testo, "a\nb") == 0 && v.par_n == 2 && v.troncato,
           "senza posto per un paragrafo in piu', il testo si ferma a quell'a capo");
    }

    if (g_no) { printf("\n--- un RTF scritto:\n%s\n", g_out); }
    printf("rtfprova: %d NO su %d\n", g_no, g_n);
    return g_no ? 1 : 0;
}

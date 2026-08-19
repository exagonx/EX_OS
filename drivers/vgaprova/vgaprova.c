/* =============================================================================
 * drivers/vgaprova/vgaprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ROMPE LO SCHERMO APPOSTA — lo strumento di misura della rete di sicurezza
 *
 *     /dev/vgaprova.drv        mette la scheda in 320x200 grafico e ci resta
 *
 * ! NON E' UN DRIVER, E' UNO STRUMENTO DI PROVA. Si chiama `.drv` e sta in
 * /dev perche' e' l'unico modo di avere le porte I/O: dal 13 agosto 2026
 * `ioport_bind` la puo' chiamare solo un eseguibile con quel nome. Non guida
 * nessun dispositivo e non registra nessun servizio.
 *
 * ! A COSA SERVE. `vga_ripristina_testo()` nel kernel esiste per rimettere lo
 * schermo quando un server grafico muore lasciando la scheda in modalita'
 * grafica. Senza qualcosa che produca quella situazione, quel codice sarebbe
 * scritto e mai eseguito — e un codice di emergenza mai eseguito e' un codice
 * che non si sa se funziona, che e' la stessa cosa di non averlo. Questo
 * programma e' il guasto, riproducibile a comando.
 *
 * La prova si legge dalla RISOLUZIONE, non dai pixel: 320x200 mentre e' rotto,
 * 720x400 dopo `testo`. E' un numero, non un'impressione.
 *
 * ! NON DISEGNA NIENTE, e non e' una mancanza: la memoria video a 0xA0000 non
 * e' mappata in ring 3 e non deve esserlo. Quello che resta sullo schermo e'
 * cio' che c'era prima, riletto come pixel — cioe' esattamente la spazzatura
 * che si vedrebbe davvero se un server morisse qui.
 * ============================================================================= */

#include "libc.h"

#define VGA_MISC_W      0x3C2
#define VGA_SEQ_IDX     0x3C4
#define VGA_SEQ_DAT     0x3C5
#define VGA_GC_IDX      0x3CE
#define VGA_GC_DAT      0x3CF
#define VGA_CRTC_IDX    0x3D4
#define VGA_CRTC_DAT    0x3D5
#define VGA_AC_IDX      0x3C0
#define VGA_STATO       0x3DA

#define PORTA_BASE      0x3C0
#define PORTE_N         0x1B        /* 0x3C0..0x3DA */

/* Modo 13h: 320x200, 256 colori, un byte per pixel. E' il modo grafico piu'
 * semplice che esista sulla VGA, ed e' scelto per questo: se la prova
 * fallisse, il difetto non deve poter essere nel modo che la prova imposta. */
static const unsigned char m13_misc = 0x63;

static const unsigned char m13_seq[5] = {
    0x03, 0x01, 0x0F, 0x00, 0x0E
};

static const unsigned char m13_crtc[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF
};

static const unsigned char m13_gc[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
};

static const unsigned char m13_ac[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

static void fuori(unsigned int porta, unsigned int val)
{
    ioport_out(porta, val);
}

int main(int argc, char **argv)
{
    int rc;
    unsigned int i;

    /* =====================================================================
     * ! `-i` DICE DI NO, E DEVE DIRLO SENZA TOCCARE NIENTE.
     *
     * `hwconfig -d` sonda ogni *.drv del catalogo lanciandolo con `-i`, e
     * `install` chiama hwconfig. Questo file gli argomenti non li guardava
     * nemmeno: veniva eseguito PER DAVVERO durante ogni installazione — con
     * lo schermo commutato in 320x200 nel mezzo — e usciva con 0, che per la
     * sonda vuol dire «servo qui». Risultato: /dev/vgaprova.drv finiva
     * installato su ogni sistema, e la console si spegneva mentre si copiava.
     *
     * ! E LA RISPOSTA GIUSTA E' NO, non «si'»: questo non e' un driver. Non
     * guida niente e non serve nessuna periferica — e' una PROVA, che si
     * lancia a mano per vedere che un programma in ring 3 puo' riprogrammare
     * la VGA. Un catalogo di driver non deve portarselo dietro.
     *
     * E' lo stesso difetto che aveva wserver.drv, e la stessa riparazione.
     * ===================================================================== */
    for (i = 1; i < (unsigned int)argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] != 'i') continue;

        printf("vgaprova: non sono un driver, sono una prova da lanciare a\n");
        printf("          mano: non installarmi.\n");
        return 1;
    }

    rc = ioport_bind(PORTA_BASE, PORTE_N);
    if (rc < 0) {
        printf("vgaprova: ioport_bind(0x%x, %d) fallita (%d)\n",
               PORTA_BASE, PORTE_N, rc);
        printf("          mi chiamo *.drv? il varco e' quello.\n");
        return 1;
    }

    printf("vgaprova: metto la scheda in 320x200 grafico.\n");
    printf("          da qui in poi lo schermo non si legge piu':\n");
    printf("          il comando da battere alla cieca e'  testo\n");

    /* Un attimo, cosi' le righe qui sopra si vedono davvero prima che lo
     * schermo diventi illeggibile. Senza, il messaggio che spiega come
     * uscirne sparirebbe insieme allo schermo. */
    usleep(400000);

    fuori(VGA_SEQ_IDX, 0x00); fuori(VGA_SEQ_DAT, 0x01);   /* reset */
    fuori(VGA_MISC_W, m13_misc);

    for (i = 1; i < 5; i++) { fuori(VGA_SEQ_IDX, i); fuori(VGA_SEQ_DAT, m13_seq[i]); }
    fuori(VGA_SEQ_IDX, 0x00); fuori(VGA_SEQ_DAT, m13_seq[0]);

    /* Il lucchetto dei primi otto registri del CRTC sta nel registro 0x11:
     * senza toglierlo, le scritture a 0..7 non hanno effetto e non danno
     * errore. Stessa trappola documentata in kernel/arch/x86/vga_modo3.c. */
    fuori(VGA_CRTC_IDX, 0x11);
    fuori(VGA_CRTC_DAT, (unsigned int)(ioport_in(VGA_CRTC_DAT) & 0x7F));

    for (i = 0; i < 25; i++) { fuori(VGA_CRTC_IDX, i); fuori(VGA_CRTC_DAT, m13_crtc[i]); }
    for (i = 0; i < 9;  i++) { fuori(VGA_GC_IDX, i);   fuori(VGA_GC_DAT, m13_gc[i]); }

    (void)ioport_in(VGA_STATO);
    for (i = 0; i < 21; i++) {
        (void)ioport_in(VGA_STATO);
        fuori(VGA_AC_IDX, i);
        fuori(VGA_AC_IDX, m13_ac[i]);
    }
    (void)ioport_in(VGA_STATO);
    fuori(VGA_AC_IDX, 0x20);

    /* Si esce SUBITO, e questo e' il punto della prova: il processo che ha
     * rotto lo schermo non c'e' piu'. Chi rimette il testo non puo' contare
     * su di lui. */
    return 0;
}

/* =============================================================================
 * kernel/arch/x86/vga_modo3.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * RIMETTERE IL MODO TESTO SENZA IL BIOS — la rete di sicurezza della grafica
 *
 * Gradino 0 punto 4 di DIREZIONE.md, e va scritto PRIMA del primo server
 * grafico, non dopo: e' cio' che rende sopportabile il primo server grafico
 * che va in crash, e il primo andra' in crash.
 *
 * -----------------------------------------------------------------------------
 * ! IL PROBLEMA, E PERCHE' NON BASTA «RIAVVIARE IL SERVER»
 *
 * Una modalita' video si imposta con INT 10h, cioe' con il BIOS, cioe' in modo
 * REALE: l'unico che puo' farlo e' Stage 2, prima del passaggio a modo
 * protetto. Dopo, quella porta e' chiusa.
 *
 * Se il server grafico muore con la scheda in modalita' grafica, il sistema e'
 * vivo — kernel, scheduler, seriale e tastiera funzionano — ma lo schermo
 * resta congelato su cio' che c'era. Un comando digitato alla cieca viene
 * eseguito e non se ne vede l'effetto. Senza questo file, l'unica uscita e' il
 * riavvio, e «alla cieca» sarebbe letterale e definitivo.
 *
 * -----------------------------------------------------------------------------
 * ! STA NEL KERNEL, ED E' UNA DELLE POCHISSIME ECCEZIONI ALLA DIRETTIVA 1
 *
 * La regola dice che ogni cosa nuova nasce in ring 3. Questa no, per la stessa
 * ragione per cui c'e' l'ATA: deve funzionare QUANDO IL SERVER E' MORTO, e un
 * processo che risponde solo finche' un altro processo e' vivo non e' una rete
 * di sicurezza. Il kernel e' l'unico che c'e' sempre.
 *
 * -----------------------------------------------------------------------------
 * ! I VALORI NON SONO STATI INVENTATI, E NON SI TOCCANO A ORECCHIO
 *
 * Le cinque tabelle qui sotto sono lo stato del modo 3 (80x25, 720x400, 16
 * colori) sui registri VGA, e sono quelle canoniche dell'hardware IBM VGA che
 * ogni scheda dal 1987 in poi implementa. Un valore sbagliato non da' un
 * errore: da' uno schermo storto, o nero, o un temporizzatore fuori
 * specifica — cioe' esattamente la situazione da cui si stava cercando di
 * uscire, senza piu' un modo di uscirne.
 *
 * -----------------------------------------------------------------------------
 * ! IL CARATTERE VA RICARICATO, e questo e' il pezzo che si dimentica
 *
 * In modo testo i disegni dei caratteri NON stanno nella ROM: stanno nel piano
 * 2 della memoria video, dove il BIOS li copia all'avvio. Una modalita'
 * grafica ci scrive sopra i propri pixel. Rimettere i registri del modo 3
 * senza ricaricare il carattere da' 80x25 di simboli casuali — uno schermo che
 * risponde e non si legge, che e' quasi peggio di uno spento.
 *
 * Il carattere ce l'abbiamo gia': `font8x16`, lo stesso che la console
 * disegna nel framebuffer quando la grafica c'e'. Un carattere solo per le due
 * strade, quindi lo schermo si legge uguale prima e dopo.
 *
 * -----------------------------------------------------------------------------
 * ! E VA RIMESSA LA TAVOLOZZA, per la stessa ragione
 *
 * I 16 colori del testo non sono valori RGB: sono INDICI, che il controllore
 * degli attributi manda al DAC. Una modalita' grafica riscrive il DAC con la
 * propria tavolozza. Senza rimetterlo si ottiene testo leggibile in colori
 * arbitrari — spesso nero su nero.
 *
 * -----------------------------------------------------------------------------
 * ! IL LIMITE, DICHIARATO: VBE NON SI SPEGNE CON I REGISTRI VGA
 *
 * Su una scheda in una modalita' VESA lineare non basta riscrivere i registri
 * VGA: la modalita' VBE ha registri suoi, e finche' non la si spegne la scheda
 * resta li'. Sulle schede Bochs/QEMU/VirtualBox — cioe' quelle su cui EX-OS
 * gira oggi — si spegne dalla finestra a porte 0x1CE/0x1CF, e questo file lo
 * fa. La finestra non si riconosce da un elenco di identificativi noti — quello
 * era vero per Bochs e QEMU e falso per VirtualBox, e il perche' sta sopra
 * vbe_spegni() — ma chiedendole di rispondere.
 *
 * ! E QUANDO NON RISPONDE LO DICE, sulla seriale, a livello WARN: e' l'unico
 * modo di sapere perche' lo schermo e' diventato illeggibile, visto che lo
 * schermo non puo' piu' dirlo.
 *
 * Su ferro vero l'equivalente e' l'interfaccia in modo protetto di VBE 2.0,
 * che EX-OS non ha. Percio': su hardware reale con una VESA attiva questo
 * codice rimette i registri VGA e puo' non bastare. E' scritto qui invece che
 * scoperto dopo.
 * ============================================================================= */

#include "kernel.h"
#include "vga.h"

extern const uint8_t font8x16[256 * 16];

/* --- Porte ---------------------------------------------------------------- */
#define VGA_MISC_W      0x3C2    /* Miscellaneous Output, scrittura */
#define VGA_SEQ_IDX     0x3C4
#define VGA_SEQ_DAT     0x3C5
#define VGA_DAC_MASK    0x3C6
#define VGA_DAC_WIDX    0x3C8
#define VGA_DAC_DATA    0x3C9
#define VGA_GC_IDX      0x3CE
#define VGA_GC_DAT      0x3CF
#define VGA_CRTC_IDX    0x3D4
#define VGA_CRTC_DAT    0x3D5
#define VGA_AC_IDX      0x3C0    /* indice E dato: si alternano */
#define VGA_AC_LEGGI    0x3C1
#define VGA_STATO       0x3DA    /* leggerlo azzera il flip-flop del 0x3C0 */

/* La finestra a porte delle schede Bochs/QEMU/VirtualBox (VBE «DISPI»). */
#define DISPI_IDX       0x01CE
#define DISPI_DAT       0x01CF
#define DISPI_ID        0
#define DISPI_ENABLE    4

/* L'identificativo che si SCRIVE per vedere se la finestra risponde: e' il
 * primo della serie, quello che ogni versione dell'interfaccia accetta. Vedi
 * vbe_spegni(): non e' il valore che ci si aspetta di leggere, e' quello che
 * si usa per fare la domanda. */
#define DISPI_ID_PROVA  0xB0C0

/* --- Lo stato del modo 3 sui registri ------------------------------------- */
static const uint8_t m3_misc = 0x67;

static const uint8_t m3_seq[5] = {
    0x03, 0x00, 0x03, 0x00, 0x02
};

static const uint8_t m3_crtc[25] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};

static const uint8_t m3_gc[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
};

/* I primi sedici sono la mappa colore->indice DAC del testo: 0..7 e 0x38..0x3F.
 * Non e' una scelta, e' cio' che il BIOS lascia e cio' che il resto del
 * sistema si aspetta. */
static const uint8_t m3_ac[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

/* I 16 colori del testo, in valori DAC a 6 bit. Sono gli stessi che vga.c
 * usa in RGB per disegnare nel framebuffer: un colore solo per le due
 * strade. */
static const uint8_t m3_dac[16][3] = {
    {  0,  0,  0 }, {  0,  0, 42 }, {  0, 42,  0 }, {  0, 42, 42 },
    { 42,  0,  0 }, { 42,  0, 42 }, { 42, 21,  0 }, { 42, 42, 42 },
    { 21, 21, 21 }, { 21, 21, 63 }, { 21, 63, 21 }, { 21, 63, 63 },
    { 63, 21, 21 }, { 63, 21, 63 }, { 63, 63, 21 }, { 63, 63, 63 }
};

/* --- La scheda VBE, se e' una di quelle che sappiamo spegnere -------------- */
static void vbe_spegni(void)
{
    uint16_t id, riletto;

    /* ! LA FINESTRA E' A SEDICI BIT, INDICE E DATO, e leggerla a byte NON
     * funziona: 0x1CF e' una porta a 16 bit, e 0x1D0 non ne e' la meta' alta —
     * e' un'altra porta. La prima stesura di questa funzione lo faceva, non
     * riconosceva l'identificativo, usciva senza toccare niente, e il
     * ripristino da una VESA 800x600 lasciava lo schermo dov'era: risoluzione
     * invariata e nessun errore da nessuna parte. L'ha preso lo screendump,
     * non il codice. */
    port_outw(DISPI_IDX, DISPI_ID);
    id = port_inw(DISPI_DAT);

    /* =====================================================================
     * ! L'IDENTIFICATIVO SI SCRIVE E SI RILEGGE, NON SI CONFRONTA CON UN
     * ELENCO.
     *
     * Prima si accettavano solo i valori da 0xB0C0 a 0xB0C5, quelli di
     * Bochs e di QEMU. Ma quel registro non dice «che scheda sono»: dice
     * quale VERSIONE dell'interfaccia e' stata concordata, e chi la concorda
     * e' l'ultimo che ci ha scritto — di norma il BIOS video della scheda,
     * mentre imposta la modalita'. Chi ne concordasse una fuori
     * dall'intervallo — VirtualBox ne ha di proprie, 0xBE00..0xBE03 — farebbe
     * leggere qui un numero legittimo e non riconosciuto: si usciva senza
     * spegnere niente, e poi si riscrivevano i registri VGA SOPRA una VESA
     * ancora accesa. Non uno schermo fermo: uno schermo che rotola.
     *
     * ! MISURATO, E SU VBoxVGA NON SUCCEDEVA: l'identificativo cadeva dentro
     * l'intervallo vecchio e il ripristino gia' funzionava. L'elenco si toglie
     * perche' non c'era ragione di fidarsene, non perche' lo si sia visto
     * tradire. (I colori casuali del 26 agosto 2026 venivano da tutt'altro:
     * dal PMM — vedi il Passo 1 di pmm_init.)
     *
     * ! UN ELENCO DI NUMERI NOTI E' SEMPRE INCOMPLETO, e lo si scopre solo
     * sulla macchina che non c'era nell'elenco. La domanda giusta non e'
     * «che numero c'e' scritto» ma «c'e' qualcuno che risponde»: si scrive
     * l'identificativo piu' vecchio, quello che ogni versione accetta, e lo
     * si rilegge. Se torna indietro uguale la finestra c'e', qualunque
     * scheda sia e qualunque versione avesse concordato prima. Se non c'e'
     * nessuno — su una macchina senza queste porte si legge 0xFFFF — non
     * puo' tornare indietro niente, e non si tocca altro.
     *
     * ! E DECLASSARE LA VERSIONE QUI NON COSTA NULLA: la riga dopo la
     * modalita' estesa si spegne, e la prossima a concordarne una sara' di
     * nuovo il BIOS video, al riavvio.
     * ===================================================================== */
    port_outw(DISPI_IDX, DISPI_ID);
    port_outw(DISPI_DAT, DISPI_ID_PROVA);
    port_outw(DISPI_IDX, DISPI_ID);
    riletto = port_inw(DISPI_DAT);

    if (riletto != DISPI_ID_PROVA) {
        /* ! LO DICE FORTE, non a livello informativo. Quando questa riga si
         * stampa lo schermo sta per diventare illeggibile, e questa e'
         * l'unica frase che spiega perche'. Va letta sulla seriale: e' la
         * sola cosa che qui continua a funzionare. */
        klog(LOG_WARN, "VGA: nessuna finestra VBE DISPI a 0x1CE "
             "(id 0x%04x, riletto 0x%04x): se la scheda e' in VESA lo "
             "schermo restera' illeggibile", id, riletto);
        return;
    }

    port_outw(DISPI_IDX, DISPI_ENABLE);
    port_outw(DISPI_DAT, 0x0000);

    klog(LOG_INFO, "VGA: VBE DISPI spenta (l'identificativo era 0x%04x)", id);
}

/* --- Il carattere, dentro il piano 2 -------------------------------------- */
static void carattere_ricarica(void)
{
    volatile uint8_t *piano = (volatile uint8_t *)0xA0000;
    uint32_t c, r;

    /* Si apre il piano 2 in accesso lineare a 0xA0000. Sono sei registri e
     * l'ordine conta: il reset del sequenziatore va tenuto mentre si cambia
     * la modalita' di memoria. */
    port_outb(VGA_SEQ_IDX, 0x00); port_outb(VGA_SEQ_DAT, 0x01);  /* reset */
    port_outb(VGA_SEQ_IDX, 0x02); port_outb(VGA_SEQ_DAT, 0x04);  /* solo piano 2 */
    port_outb(VGA_SEQ_IDX, 0x04); port_outb(VGA_SEQ_DAT, 0x07);  /* lineare */
    port_outb(VGA_SEQ_IDX, 0x00); port_outb(VGA_SEQ_DAT, 0x03);  /* fine reset */

    port_outb(VGA_GC_IDX, 0x04); port_outb(VGA_GC_DAT, 0x02);    /* leggi piano 2 */
    port_outb(VGA_GC_IDX, 0x05); port_outb(VGA_GC_DAT, 0x00);
    port_outb(VGA_GC_IDX, 0x06); port_outb(VGA_GC_DAT, 0x00);    /* finestra a 0xA0000 */

    /* ! IL PASSO E' 32 BYTE, NON 16. Ogni disegno occupa un'area fissa da 32
     * byte qualunque sia l'altezza del carattere: un font 8x16 ne riempie 16 e
     * lascia gli altri. Scriverli di seguito darebbe i caratteri sfalsati a
     * partire dal secondo, cioe' testo illeggibile che sembra un guasto della
     * memoria video. */
    for (c = 0; c < 256; c++) {
        for (r = 0; r < 16; r++)
            piano[c * 32 + r] = font8x16[c * 16 + r];
        for (r = 16; r < 32; r++)
            piano[c * 32 + r] = 0;
    }

    /* Si richiude: piani 0 e 1 in pari/dispari, finestra a 0xB8000. E' lo
     * stato in cui il modo testo si aspetta di trovare la scheda. */
    port_outb(VGA_SEQ_IDX, 0x00); port_outb(VGA_SEQ_DAT, 0x01);
    port_outb(VGA_SEQ_IDX, 0x02); port_outb(VGA_SEQ_DAT, 0x03);
    port_outb(VGA_SEQ_IDX, 0x04); port_outb(VGA_SEQ_DAT, 0x03);
    port_outb(VGA_SEQ_IDX, 0x00); port_outb(VGA_SEQ_DAT, 0x03);

    port_outb(VGA_GC_IDX, 0x04); port_outb(VGA_GC_DAT, 0x00);
    port_outb(VGA_GC_IDX, 0x05); port_outb(VGA_GC_DAT, 0x10);
    port_outb(VGA_GC_IDX, 0x06); port_outb(VGA_GC_DAT, 0x0E);
}

/* --- La tavolozza --------------------------------------------------------- */
static void tavolozza_ricarica(void)
{
    uint32_t i;

    port_outb(VGA_DAC_MASK, 0xFF);

    /* I sedici colori vanno agli indici che il controllore degli attributi
     * nomina: 0..7 e 0x38..0x3F. Scriverli in 0..15 darebbe una tavolozza
     * giusta a cui nessuno guarda. */
    for (i = 0; i < 8; i++) {
        port_outb(VGA_DAC_WIDX, (uint8_t)i);
        port_outb(VGA_DAC_DATA, m3_dac[i][0]);
        port_outb(VGA_DAC_DATA, m3_dac[i][1]);
        port_outb(VGA_DAC_DATA, m3_dac[i][2]);
    }
    for (i = 8; i < 16; i++) {
        port_outb(VGA_DAC_WIDX, (uint8_t)(0x38 + (i - 8)));
        port_outb(VGA_DAC_DATA, m3_dac[i][0]);
        port_outb(VGA_DAC_DATA, m3_dac[i][1]);
        port_outb(VGA_DAC_DATA, m3_dac[i][2]);
    }
}

/* =============================================================================
 * vga_hw_modo3 — rimette la scheda in 80x25, testo, 16 colori
 *
 * Non tocca la memoria video ne' la console: rimette solo l'hardware. Chi
 * chiama deve poi ridisegnare, e lo fa vga_ripristina_testo() in vga.c.
 * ========================================================================== */
void vga_hw_modo3(void)
{
    uint32_t i;

    /* Prima si esce dalla VESA, se ci si e' dentro: cambiare i registri VGA
     * mentre la VBE e' accesa non porta via da nessuna parte. */
    vbe_spegni();

    /* Sequenziatore in reset per tutto il tempo del cambio: i registri di
     * temporizzazione non si cambiano mentre il pennello sta disegnando. */
    port_outb(VGA_SEQ_IDX, 0x00); port_outb(VGA_SEQ_DAT, 0x01);

    /* Il Miscellaneous Output decide, fra l'altro, SE IL CRTC RISPONDE a
     * 0x3D4 (colore) o a 0x3B4 (mono). Va scritto prima di parlare al CRTC,
     * o si scriverebbe su porte che non rispondono. */
    port_outb(VGA_MISC_W, m3_misc);

    for (i = 1; i < 5; i++) {
        port_outb(VGA_SEQ_IDX, (uint8_t)i);
        port_outb(VGA_SEQ_DAT, m3_seq[i]);
    }
    port_outb(VGA_SEQ_IDX, 0x00); port_outb(VGA_SEQ_DAT, m3_seq[0]);

    /* ! I PRIMI OTTO REGISTRI DEL CRTC SONO PROTETTI DA UN LUCCHETTO, e il
     * lucchetto sta nel registro 0x11 — cioe' dentro l'insieme che si sta per
     * scrivere. Senza toglierlo prima, le scritture a 0..7 non hanno effetto
     * e non danno errore: si ottiene una temporizzazione mista fra il modo
     * vecchio e quello nuovo, che e' il modo piu' facile di avere uno schermo
     * nero credendo di aver fatto tutto. */
    port_outb(VGA_CRTC_IDX, 0x11);
    port_outb(VGA_CRTC_DAT, (uint8_t)(port_inb(VGA_CRTC_DAT) & 0x7F));

    for (i = 0; i < 25; i++) {
        port_outb(VGA_CRTC_IDX, (uint8_t)i);
        port_outb(VGA_CRTC_DAT, m3_crtc[i]);
    }

    for (i = 0; i < 9; i++) {
        port_outb(VGA_GC_IDX, (uint8_t)i);
        port_outb(VGA_GC_DAT, m3_gc[i]);
    }

    /* ! IL CONTROLLORE DEGLI ATTRIBUTI HA UNA PORTA SOLA per indice e dato, e
     * un flip-flop interno che decide quale dei due sta ricevendo. Lo si
     * azzera LEGGENDO 0x3DA. Se qualcuno l'ha lasciato a meta', il primo
     * valore finisce nel registro sbagliato e tutti gli altri slittano. */
    (void)port_inb(VGA_STATO);
    for (i = 0; i < 21; i++) {
        (void)port_inb(VGA_STATO);
        port_outb(VGA_AC_IDX, (uint8_t)i);
        port_outb(VGA_AC_IDX, m3_ac[i]);
    }

    carattere_ricarica();
    tavolozza_ricarica();

    /* Il bit 0x20 riaccende l'uscita video: finche' e' spento lo schermo resta
     * nero anche con tutto il resto giusto. E' l'ultima cosa apposta — si
     * accende quando la scheda e' pronta, non prima. */
    (void)port_inb(VGA_STATO);
    port_outb(VGA_AC_IDX, 0x20);
}

/* =============================================================================
 * drivers/sis/ponte_tab.h — il ponte video, letto dal ferro
 *
 * ! GENERATO da tools/ponte2tab.py da due referti presi con `sonda.drv -ponte`.
 * Non si modifica a mano.
 *
 * ! E' LA META' CHE MANCAVA. I banchi VGA riportano il testo in un riquadro
 * giusto e leggibile; e' il ponte a portare quel riquadro sul pannello, e
 * finche' resta impostato per 800x600 il riquadro resta un riquadro con
 * intorno quello che c'era prima.
 *
 * ! IL RUMORE QUI NON E' MISURATO, e va detto. Sui banchi VGA ci sono voluti
 * sei referti per scoprire che trentatre registri cambiano da soli; del ponte
 * ce n'e' uno per stato. Quattro sono gia' sospetti perche' cambiano anche
 * fra due letture dello stesso stato, e sono segnati riga per riga.
 * ============================================================================= */

typedef struct {
    unsigned char parte;    /* 1..5 */
    unsigned char indice;
    unsigned char valore;
} SisPonte;

/* Generato da tools/ponte2tab.py: NON si modifica a mano. */
/* Generato da tools/ponte2tab.py: NON si modifica a mano. */
/* Bersaglio: sonda/PONTET.TXT   Partenza: sonda/PONTEG.TXT */

static const SisPonte ponte_testo[] = {
/* Scartati perche' si rileggono costanti, cioe' non sono
 * banchi indicizzati: P5 */
    { 1, 0x00, 0x10 },
    { 1, 0x02, 0x44 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x04, 0x4f },
    { 1, 0x05, 0x4f },
    { 1, 0x07, 0x71 },
    { 1, 0x0a, 0x3f },
    { 1, 0x0b, 0x0c },
    { 1, 0x0c, 0x98 },
    { 1, 0x0d, 0x89 },
    { 1, 0x0e, 0x8f },
    { 1, 0x10, 0x8f },
    { 1, 0x17, 0x21 },
    { 1, 0x30, 0x00 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x31, 0x08 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x32, 0x00 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x33, 0x82 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x34, 0x00 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x46, 0x00 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x47, 0x00 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x4a, 0x7f },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x4b, 0x7f },   /* la scheda non lo accetta: serve lo sblocco */
    { 2, 0x2e, 0x19 },
    { 2, 0x45, 0x40 },
    { 2, 0x46, 0x5d },
    { 4, 0x03, 0x4e },   /* la scheda non lo accetta: serve lo sblocco */
    { 4, 0x14, 0x40 },
    { 4, 0x15, 0x19 },
    { 4, 0x16, 0xef },
    { 4, 0x17, 0x9d },
    { 4, 0x19, 0x20 },
    { 4, 0x1a, 0x15 },
    { 4, 0x1b, 0x56 },
    { 4, 0x1d, 0x7f },
    { 4, 0x1e, 0x26 },
};
#define PONTE_TESTO_N ((int)(sizeof(ponte_testo) / sizeof(ponte_testo[0])))

/* 34 registri su 640. */

/* Generato da tools/ponte2tab.py: NON si modifica a mano. */
/* Bersaglio: sonda/PONTEG.TXT   Partenza: sonda/PONTET.TXT */

static const SisPonte ponte_grafica[] = {
/* Scartati perche' si rileggono costanti, cioe' non sono
 * banchi indicizzati: P5 */
    { 1, 0x00, 0x11 },
    { 1, 0x02, 0x6e },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x04, 0x63 },
    { 1, 0x05, 0x63 },
    { 1, 0x07, 0x73 },
    { 1, 0x0a, 0xf1 },
    { 1, 0x0b, 0x2c },
    { 1, 0x0c, 0x63 },
    { 1, 0x0d, 0x84 },
    { 1, 0x0e, 0x57 },
    { 1, 0x10, 0x57 },
    { 1, 0x17, 0x01 },
    { 1, 0x30, 0x04 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x31, 0x81 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x32, 0x09 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x33, 0x81 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x34, 0x40 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x46, 0x76 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x47, 0x19 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x4a, 0x63 },   /* la scheda non lo accetta: serve lo sblocco */
    { 1, 0x4b, 0x63 },   /* la scheda non lo accetta: serve lo sblocco */
    { 2, 0x2e, 0xbf },
    { 2, 0x45, 0x48 },
    { 2, 0x46, 0x5e },
    { 4, 0x03, 0x0e },   /* la scheda non lo accetta: serve lo sblocco */
    { 4, 0x14, 0x2a },
    { 4, 0x15, 0x1a },
    { 4, 0x16, 0xff },
    { 4, 0x17, 0x6b },
    { 4, 0x19, 0x30 },
    { 4, 0x1a, 0x20 },
    { 4, 0x1b, 0x00 },
    { 4, 0x1d, 0x1f },
    { 4, 0x1e, 0x36 },
};
#define PONTE_GRAFICA_N ((int)(sizeof(ponte_grafica) / sizeof(ponte_grafica[0])))

/* 34 registri su 640. */

/* =============================================================================
 * drivers/kbd/keymaps.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Disposizioni di tastiera: dallo scancode al carattere.
 *
 * Ogni disposizione e' fatta di QUATTRO tabelle da 128 caratteri, indicizzate
 * dallo scancode (set 1). L'indice e' il TASTO FISICO, sempre lo stesso;
 * cambia solo cosa ci sta scritto sopra.
 *
 *      normale     il tasto da solo
 *      shift       con Shift
 *      altgr       con AltGr (il tasto Alt di DESTRA)
 *      altgr_sh    con AltGr e Shift insieme
 *
 * -----------------------------------------------------------------------------
 * ! SENZA AltGr UNA TASTIERA ITALIANA NON SCRIVE C
 *
 * La quarta tabella sembra un lusso finche' non si prova a scrivere una
 * funzione. Su una tastiera italiana:
 *
 *      @  e' AltGr+ò          [  e' AltGr+e`
 *      #  e' AltGr+a`         ]  e' AltGr++
 *      {  e' AltGr+Shift+e`   }  e' AltGr+Shift++
 *
 * Le graffe stanno SOLO su AltGr+Shift. Una disposizione che si ferma a
 * tre tabelle da' una tastiera con cui non si puo' aprire un blocco — e
 * questo sistema ci porta dentro un editor di testo e un compilatore C.
 * Ecco perche' le tabelle sono quattro e non tre.
 *
 * -----------------------------------------------------------------------------
 * ! LE TABELLE SONO IN UNICODE (kbd.drv 0.007, 30 ottobre 2026)
 *
 * Fino alla 0.006 qui c'erano i byte della CP437, quelli che la VGA in modo
 * testo disegna: la `à` era 0x85. Il simbolo dell'euro (AltGr+E) e il §
 * (Shift+ù) non si potevano scrivere: l'euro in CP437 non c'e', e il § sta
 * a 0x15, in mezzo ai codici di controllo. Ora ogni casella e' il codice
 * Unicode del carattere (unsigned short: bastano i primi 65536), e il
 * driver lo traduce in CP437 solo dove serve un byte:
 *
 *      la console di testo e chi legge i tasti alla vecchia maniera
 *          ricevono il byte CP437 come prima (la `à` e' ancora 0x85);
 *          un carattere che in CP437 non c'e' (l'euro) li' non scrive
 *          niente, come prima;
 *      chi legge l'evento lungo (KbdTasto, ExWin) riceve l'Unicode, e per
 *          l'euro il codice KBD_K_UNICODE (kbd_proto.h).
 *
 * ! IL § NON SI DA' ALLA CONSOLE come byte 0x15: e' un codice di
 * controllo (Ctrl+U), e un tasto che lo manda darebbe un carattere che il
 * driver non eca (vedi eco_visibile in kbd.c), invisibile dentro il
 * comando. Va solo a chi legge l'evento lungo, come l'euro.
 *
 * -----------------------------------------------------------------------------
 * ! QUANTO SONO ATTENDIBILI QUESTE TABELLE
 *
 * `us` e `it` sono verificate tasto per tasto. Le altre sono scritte
 * dalla disposizione nota e provate solo per i tasti che cambiano
 * posizione rispetto a US (le lettere di AZERTY e QWERTZ, i segni della
 * fila dei numeri): sono utilizzabili, ma chi ha quella tastiera davanti
 * e trova un tasto sbagliato ha trovato un difetto vero, non un limite.
 *
 * Non ci sono TASTI MORTI. Su una tastiera francese o tedesca `^` e `¨`
 * normalmente non scrivono nulla finche' non si preme la vocale dopo:
 * qui scrivono sé stessi subito. Farli funzionare vuol dire uno stato in
 * piu' nel driver e una tabella di combinazioni per disposizione; per ora
 * si dichiara che non ci sono, invece di farli sembrare rotti.
 * ============================================================================= */

#ifndef KEYMAPS_H
#define KEYMAPS_H

/* Lettere accentate e segni, in Unicode (fino alla 0.006 erano CP437). */
#define K_agrave    0x00E0  /* à */
#define K_egrave    0x00E8  /* è */
#define K_eacute    0x00E9  /* é */
#define K_igrave    0x00EC  /* ì */
#define K_ograve    0x00F2  /* ò */
#define K_ugrave    0x00F9  /* ù */
#define K_ccedil    0x00E7  /* ç */
#define K_Ccedil    0x00C7  /* Ç */
#define K_gradi     0x00B0  /* ° */
#define K_sterlina  0x00A3  /* £ */
#define K_udier     0x00FC  /* ü */
#define K_Udier     0x00DC  /* Ü */
#define K_odier     0x00F6  /* ö */
#define K_Odier     0x00D6  /* Ö */
#define K_adier     0x00E4  /* ä */
#define K_Adier     0x00C4  /* Ä */
#define K_eszett    0x00DF  /* ß */
#define K_ntilde    0x00F1  /* ñ */
#define K_Ntilde    0x00D1  /* Ñ */
#define K_escl_rov  0x00A1  /* ¡ */
#define K_int_rov   0x00BF  /* ¿ */
#define K_ordm      0x00BA  /* º */
#define K_ordf      0x00AA  /* ª */
#define K_non       0x00AC  /* ¬ */
#define K_micro     0x00B5  /* µ */
/* not in CP437, or where CP437 has a control code: only the long event */
#define K_euro      0x20AC  /* € */
#define K_parag     0x00A7  /* § */
#define K_sup2      0x00B2  /* ² (CP437 has it, at 0xFD) */
#define K_sup3      0x00B3  /* ³ */
#define K_valuta    0x00A4  /* ¤ */

#define KEYMAP_N_TASTI  128

typedef struct {
    const char    *nome;        /* "it", "fr", ... */
    const char    *descrizione; /* per l'elenco */
    unsigned short normale[KEYMAP_N_TASTI];     /* Unicode, see above */
    unsigned short shift[KEYMAP_N_TASTI];
    unsigned short altgr[KEYMAP_N_TASTI];
    unsigned short altgr_sh[KEYMAP_N_TASTI];
} Keymap;

/* Le righe seguono lo scancode set 1:
 *   0x02..0x0D  fila dei numeri, fino ai due tasti a destra dello 0
 *   0x10..0x1B  fila di q, fino ai due tasti dopo la p
 *   0x1E..0x28  fila di a, fino ai due tasti dopo la l
 *   0x29        il tasto a sinistra dell'1
 *   0x2B        il tasto sopra Invio (o a sinistra, secondo la tastiera)
 *   0x2C..0x35  fila di z, fino al tasto prima di Shift destro
 *   0x56        il tasto in piu' delle tastiere a 102 tasti
 */

static const Keymap g_keymaps[] = {

/* ---------------------------------------------------------------------------
 * US — QWERTY americana. E' la disposizione con cui il driver e' nato, e
 * resta il ripiego: e' l'unica in cui ogni segno che serve a programmare
 * sta su un tasto senza AltGr.
 * --------------------------------------------------------------------------- */
{ "us", "Stati Uniti (QWERTY)",
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', '-','=','\b','\t','q','w','e','r',
    't', 'y','u','i','o','p','[',']','\n', 0,
    'a', 's','d','f','g','h','j','k','l',';',
    '\'','`', 0, '\\','z','x','c','v','b','n',
    'm', ',','.','/', 0, '*', 0, ' ' },
  {   0,  27, '!','@','#','$','%','^','&','*',
    '(',')', '_','+','\b','\t','Q','W','E','R',
    'T', 'Y','U','I','O','P','{','}','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',':',
    '"', '~', 0, '|','Z','X','C','V','B','N',
    'M', '<','>','?', 0, '*', 0, ' ' },
  { 0 }, { 0 }
},

/* ---------------------------------------------------------------------------
 * IT — italiana. Verificata tasto per tasto.
 *
 * ! Le graffe stanno su AltGr+Shift, e sono l'unico modo di scriverle:
 * vedi il commento di testa. `\` e `|` stanno sul tasto a sinistra
 * dell'1, dove su US c'e' il backtick.
 * --------------------------------------------------------------------------- */
{ "it", "Italiana (QWERTY)",
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', '\'',K_igrave,'\b','\t','q','w','e','r',
    't', 'y','u','i','o','p',K_egrave,'+','\n', 0,
    'a', 's','d','f','g','h','j','k','l',K_ograve,
    K_agrave,'\\', 0, K_ugrave,'z','x','c','v','b','n',
    'm', ',','.','-', 0, '*', 0, ' ' },
  {   0,  27, '!','"',K_sterlina,'$','%','&','/','(',
    ')','=', '?','^','\b','\t','Q','W','E','R',
    'T', 'Y','U','I','O','P',K_eacute,'*','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',K_ccedil,
    K_gradi,'|', 0, K_parag,'Z','X','C','V','B','N',
    'M', ';',':','_', 0, '*', 0, ' ' },
  /* AltGr: le parentesi quadre, la chiocciola, il cancelletto, l'euro. */
  {   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,K_euro,0,
      0,   0,  0,  0,  0,  0,'[',']',  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,'@',
    '#',   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0 },
  /* AltGr+Shift: le graffe, e basta. */
  {   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,'{','}',  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0 }
},

/* ---------------------------------------------------------------------------
 * FR — francese AZERTY. Le lettere cambiano posto (a/q, z/w, m), e la
 * fila dei numeri e' invertita: i numeri stanno su Shift.
 *
 * ! Su questa disposizione TUTTI i segni che servono a programmare
 * stanno su AltGr. Senza la terza tabella una tastiera francese non
 * scrive nemmeno una parentesi graffa.
 * --------------------------------------------------------------------------- */
{ "fr", "Francese (AZERTY)",
  {   0,  27, '&',K_eacute,'"','\'','(','-',K_egrave,'_',
    K_ccedil,K_agrave, ')','=','\b','\t','a','z','e','r',
    't', 'y','u','i','o','p','^','$','\n', 0,
    'q', 's','d','f','g','h','j','k','l','m',
    K_ugrave,K_sup2, 0, '*','w','x','c','v','b','n',
    ',', ';',':','!', 0, '*', 0, ' ' },
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', K_gradi,'+','\b','\t','A','Z','E','R',
    'T', 'Y','U','I','O','P',0,K_sterlina,'\n', 0,
    'Q', 'S','D','F','G','H','J','K','L','M',
    '%', 0, 0, K_micro,'W','X','C','V','B','N',
    '?', '.','/',K_parag, 0, '*', 0, ' ' },
  {   0,   0,  0,'~','#','{','[','|','`','\\',
    '^', '@', ']','}',  0,  0,  0,  0,K_euro,0,
      0,   0,  0,  0,  0,  0,  0,K_valuta,0, 0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0 },
  { 0 }
},

/* ---------------------------------------------------------------------------
 * DE — tedesca QWERTZ. La y e la z sono scambiate, e le graffe stanno su
 * AltGr dei numeri 7, 8, 9, 0.
 * --------------------------------------------------------------------------- */
{ "de", "Tedesca (QWERTZ)",
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', K_eszett,0,'\b','\t','q','w','e','r',
    't', 'z','u','i','o','p',K_udier,'+','\n', 0,
    'a', 's','d','f','g','h','j','k','l',K_odier,
    K_adier,'^', 0, '#','y','x','c','v','b','n',
    'm', ',','.','-', 0, '*', 0, ' ' },
  {   0,  27, '!','"',K_parag,'$','%','&','/','(',
    ')','=', '?','`','\b','\t','Q','W','E','R',
    'T', 'Z','U','I','O','P',K_Udier,'*','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',K_Odier,
    K_Adier,K_gradi, 0, '\'','Y','X','C','V','B','N',
    'M', ';',':','_', 0, '*', 0, ' ' },
  {   0,   0,  0,K_sup2,K_sup3,0,0,0,'{','[',
    ']', '}', '\\',0,  0,  0,'@',  0,K_euro,0,
      0,   0,  0,  0,  0,  0,  0,'~',  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
    K_micro,0,  0,  0,  0,  0,  0,  0 },
  { 0 }
},

/* ---------------------------------------------------------------------------
 * ES — spagnola. La n con la tilde sta dove US ha il punto e virgola.
 * --------------------------------------------------------------------------- */
{ "es", "Spagnola (QWERTY)",
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', '\'',K_escl_rov,'\b','\t','q','w','e','r',
    't', 'y','u','i','o','p','`','+','\n', 0,
    'a', 's','d','f','g','h','j','k','l',K_ntilde,
    0, K_ordm, 0, K_ccedil,'z','x','c','v','b','n',
    'm', ',','.','-', 0, '*', 0, ' ' },
  {   0,  27, '!','"',K_ordm,'$','%','&','/','(',
    ')','=', '?',K_int_rov,'\b','\t','Q','W','E','R',
    'T', 'Y','U','I','O','P','^','*','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',K_Ntilde,
    0, K_ordf, 0, K_Ccedil,'Z','X','C','V','B','N',
    'M', ';',':','_', 0, '*', 0, ' ' },
  {   0,   0,'|','@','#','~',K_non,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,K_euro,0,
      0,   0,  0,  0,  0,  0,'[',']',  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,'}',  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0 },
  {   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,'{','}',  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0 }
},

/* ---------------------------------------------------------------------------
 * UK — britannica. Quasi identica a US: cambiano le virgolette, la
 * sterlina e il tasto in piu' accanto a Shift.
 * --------------------------------------------------------------------------- */
{ "uk", "Regno Unito (QWERTY)",
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', '-','=','\b','\t','q','w','e','r',
    't', 'y','u','i','o','p','[',']','\n', 0,
    'a', 's','d','f','g','h','j','k','l',';',
    '\'','`', 0, '#','z','x','c','v','b','n',
    'm', ',','.','/', 0, '*', 0, ' ' },
  {   0,  27, '!','"',K_sterlina,'$','%','^','&','*',
    '(',')', '_','+','\b','\t','Q','W','E','R',
    'T', 'Y','U','I','O','P','{','}','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',':',
    '@', K_non, 0, '~','Z','X','C','V','B','N',
    'M', '<','>','?', 0, '*', 0, ' ' },
  /* AltGr: l'euro sul 4 */
  {   0,   0,  0,  0,  0,K_euro },
  { 0 }
},

};

#define KEYMAP_N  ((int)(sizeof(g_keymaps) / sizeof(g_keymaps[0])))

/* ! IL TASTO 0x56 STA FUORI DALLE RIGHE QUI SOPRA, perche' le tabelle
 * sono scritte per righe fino a 0x39 (lo spazio) e riempirle fino a 0x56
 * con zeri renderebbe illeggibile ogni disposizione. E' il tasto in piu'
 * delle tastiere europee a 102 tasti, quello fra Shift sinistro e la Z: ci
 * sta sempre `<` e `>`, e sulle disposizioni che lo prevedono anche `\` e
 * `|` su AltGr. Si tratta qui, una volta per tutte. */
#define KEYMAP_TASTO_102    0x56

static unsigned short keymap_102(const Keymap *k, int shift, int altgr)
{
    /* US e UK: su US quel tasto non esiste, su UK c'e' ma porta \ e |. */
    if (k->nome[0] == 'u' && k->nome[1] == 's') return 0;
    if (k->nome[0] == 'u' && k->nome[1] == 'k') return (unsigned short)(shift ? '|' : '\\');

    if (altgr) return (unsigned short)'|';
    return (unsigned short)(shift ? '>' : '<');
}

#endif /* KEYMAPS_H */

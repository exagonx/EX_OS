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
 * Ogni disposizione e' fatta di QUATTRO tabelle da 128 byte, indicizzate
 * dallo scancode (set 1). L'indice e' il TASTO FISICO, sempre lo stesso;
 * cambia solo cosa ci sta scritto sopra.
 *
 *      normale     il tasto da solo
 *      shift       con Shift
 *      altgr       con AltGr (il tasto Alt di DESTRA)
 *      altgr_sh    con AltGr e Shift insieme
 *
 * -----------------------------------------------------------------------------
 * ⚠️ SENZA AltGr UNA TASTIERA ITALIANA NON SCRIVE C
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
 * ⚠️ LE LETTERE ACCENTATE SONO BYTE DELLA CODE PAGE 437
 *
 * La VGA in modo testo disegna i glifi della CP437: la `à` e' il byte
 * 0x85, non `\xC3\xA0` di UTF-8. Qui si emette il byte che la VGA
 * disegna, perche' e' l'unico che si vede.
 *
 * Conseguenza dichiarata: quei byte finiscono anche nei NOMI DI FILE e
 * nel testo salvato. Chi legge quei file su Linux vede caratteri diversi,
 * perche' li' la stessa posizione vuol dire un'altra cosa. Non e' un
 * difetto di questa tabella: e' che EX-OS non ha una codifica di sistema,
 * e sceglierne una e' una decisione piu' grande di una disposizione di
 * tastiera.
 *
 * ⚠️ DOVE UN CARATTERE NON ESISTE IN CP437 SI EMETTE ZERO, cioe' il tasto
 * non fa niente. E' il caso del § italiano: in CP437 sta alla posizione
 * 0x15, che e' dentro l'intervallo dei codici di controllo. Emetterlo
 * darebbe un byte che il driver non eca (vedi eco_visibile in kbd.c) e
 * che finirebbe invisibile dentro il comando. Meglio un tasto muto di un
 * carattere fantasma.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ QUANTO SONO ATTENDIBILI QUESTE TABELLE
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

/* Lettere accentate e segni, come li disegna la VGA (CP437). */
#define K_agrave    0x85    /* à */
#define K_egrave    0x8A    /* è */
#define K_eacute    0x82    /* é */
#define K_igrave    0x8D    /* ì */
#define K_ograve    0x95    /* ò */
#define K_ugrave    0x97    /* ù */
#define K_ccedil    0x87    /* ç */
#define K_Ccedil    0x80    /* Ç */
#define K_gradi     0xF8    /* ° */
#define K_sterlina  0x9C    /* £ */
#define K_udier     0x81    /* ü */
#define K_Udier     0x9A    /* Ü */
#define K_odier     0x94    /* ö */
#define K_Odier     0x99    /* Ö */
#define K_adier     0x84    /* ä */
#define K_Adier     0x8E    /* Ä */
#define K_eszett    0xE1    /* ß */
#define K_ntilde    0xA4    /* ñ */
#define K_Ntilde    0xA5    /* Ñ */
#define K_escl_rov  0xAD    /* ¡ */
#define K_int_rov   0xA8    /* ¿ */
#define K_ordm      0xA7    /* º */
#define K_ordf      0xA6    /* ª */
#define K_non       0xAA    /* ¬ */
#define K_micro     0xE6    /* µ */

#define KEYMAP_N_TASTI  128

typedef struct {
    const char   *nome;         /* "it", "fr", ... */
    const char   *descrizione;  /* per l'elenco */
    unsigned char normale[KEYMAP_N_TASTI];
    unsigned char shift[KEYMAP_N_TASTI];
    unsigned char altgr[KEYMAP_N_TASTI];
    unsigned char altgr_sh[KEYMAP_N_TASTI];
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
 * ⚠️ Le graffe stanno su AltGr+Shift, e sono l'unico modo di scriverle:
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
    K_gradi,'|', 0, 0,'Z','X','C','V','B','N',
    'M', ';',':','_', 0, '*', 0, ' ' },
  /* AltGr: le parentesi quadre, la chiocciola, il cancelletto. */
  {   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
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
 * ⚠️ Su questa disposizione TUTTI i segni che servono a programmare
 * stanno su AltGr. Senza la terza tabella una tastiera francese non
 * scrive nemmeno una parentesi graffa.
 * --------------------------------------------------------------------------- */
{ "fr", "Francese (AZERTY)",
  {   0,  27, '&',K_eacute,'"','\'','(','-',K_egrave,'_',
    K_ccedil,K_agrave, ')','=','\b','\t','a','z','e','r',
    't', 'y','u','i','o','p','^','$','\n', 0,
    'q', 's','d','f','g','h','j','k','l','m',
    K_ugrave,0, 0, '*','w','x','c','v','b','n',
    ',', ';',':','!', 0, '*', 0, ' ' },
  {   0,  27, '1','2','3','4','5','6','7','8',
    '9','0', K_gradi,'+','\b','\t','A','Z','E','R',
    'T', 'Y','U','I','O','P',0,K_sterlina,'\n', 0,
    'Q', 'S','D','F','G','H','J','K','L','M',
    '%', 0, 0, K_micro,'W','X','C','V','B','N',
    '?', '.','/',0, 0, '*', 0, ' ' },
  {   0,   0,  0,'~','#','{','[','|','`','\\',
    '^', '@', ']','}',  0,  0,  0,  0,  0,  0,
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
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
  {   0,  27, '!','"',K_ordm,'$','%','&','/','(',
    ')','=', '?','`','\b','\t','Q','W','E','R',
    'T', 'Z','U','I','O','P',K_Udier,'*','\n', 0,
    'A', 'S','D','F','G','H','J','K','L',K_Odier,
    K_Adier,K_gradi, 0, '\'','Y','X','C','V','B','N',
    'M', ';',':','_', 0, '*', 0, ' ' },
  {   0,   0,  0,  0,  0,  0,  0,  0,'{','[',
    ']', '}', '\\',0,  0,  0,'@',  0,  0,  0,
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
      0,   0,  0,  0,  0,  0,  0,  0,  0,  0,
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
  { 0 }, { 0 }
},

};

#define KEYMAP_N  ((int)(sizeof(g_keymaps) / sizeof(g_keymaps[0])))

/* ⚠️ IL TASTO 0x56 STA FUORI DALLE RIGHE QUI SOPRA, perche' le tabelle
 * sono scritte per righe fino a 0x39 (lo spazio) e riempirle fino a 0x56
 * con zeri renderebbe illeggibile ogni disposizione. E' il tasto in piu'
 * delle tastiere europee a 102 tasti, quello fra Shift sinistro e la Z: ci
 * sta sempre `<` e `>`, e sulle disposizioni che lo prevedono anche `\` e
 * `|` su AltGr. Si tratta qui, una volta per tutte. */
#define KEYMAP_TASTO_102    0x56

static unsigned char keymap_102(const Keymap *k, int shift, int altgr)
{
    /* US e UK: su US quel tasto non esiste, su UK c'e' ma porta \ e |. */
    if (k->nome[0] == 'u' && k->nome[1] == 's') return 0;
    if (k->nome[0] == 'u' && k->nome[1] == 'k') return (unsigned char)(shift ? '|' : '\\');

    if (altgr) return (unsigned char)'|';
    return (unsigned char)(shift ? '>' : '<');
}

#endif /* KEYMAPS_H */

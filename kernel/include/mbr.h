/* =============================================================================
 * kernel/include/mbr.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Lettura, VALIDAZIONE e scrittura della tabella delle partizioni MBR.
 *
 * La scrittura e' arrivata dopo, ed e' il motivo per cui la lettura era
 * stata fatta prima: mbr_scrivi() non ha una propria idea di cosa sia una
 * tabella sana, applica alla PROPOSTA gli stessi identici controlli che
 * mbr_leggi() applica a cio' che trova sul disco (mbr_valida). Un errore
 * in lettura fa vedere numeri sbagliati; lo stesso errore in scrittura
 * rende irraggiungibili i dati di un disco — e avere due elenchi di
 * controlli separati, uno per leggere e uno per scrivere, significa che
 * prima o poi divergono e la scrittura accetta cio' che la lettura
 * segnala come rotto.
 *
 * COSA VIENE CONTROLLATO, e perche' ognuno di questi e' un guasto reale:
 *
 *   firma 0x55AA        la sua assenza significa che il settore 0 non e'
 *                       un MBR: interpretarlo come tale produce partizioni
 *                       inventate da byte casuali.
 *   MBR protettivo      un disco GPT ha una finta tabella MBR con una sola
 *                       voce di tipo 0xEE che copre tutto. Trattarla come
 *                       una vera partizione e' il modo classico per
 *                       distruggere un disco GPT.
 *   oltre la fine       una partizione che dichiara di finire dopo
 *                       l'ultimo settore del disco. Capita davvero quando
 *                       la tabella e' stata scritta con una capacita'
 *                       diversa da quella attuale — per esempio con una
 *                       HPA attiva, poi rimossa, o viceversa.
 *   sovrapposizioni     due partizioni che condividono settori. Scriverci
 *                       dentro corrompe l'altra, in silenzio.
 *   flag di avvio       deve essere 0x00 o 0x80. Qualunque altro valore
 *                       indica una tabella danneggiata, non una scelta.
 *   catena estesa       gli EBR formano una lista concatenata. Un puntatore
 *                       che torna indietro la rende CIRCOLARE, e un
 *                       parser ingenuo ci gira dentro per sempre.
 *   somma nulla         una voce con dimensione 0 ma tipo non nullo.
 *   settore 0           una partizione che comincia dall'LBA 0 contiene
 *                       l'MBR stesso: il primo filesystem che ci scrive
 *                       dentro cancella la tabella che lo descrive.
 *
 * In LETTURA il modulo NON corregge nulla di tutto questo: lo segnala.
 * Correggere automaticamente una tabella sospetta significherebbe
 * decidere al posto dell'utente su dati che potrebbero essere
 * recuperabili.
 *
 * In SCRITTURA invece rifiuta: una tabella che presenti una qualunque di
 * queste anomalie non arriva mai al disco. La differenza e' che una
 * tabella gia' scritta e' un fatto da diagnosticare, una tabella proposta
 * e' una decisione ancora reversibile.
 * ============================================================================= */

#ifndef MBR_H
#define MBR_H

#include "kernel.h"

#define MBR_MAX_PART        16   /* 4 primarie + logiche, tetto pratico */

/* Schema rilevato sul settore 0 */
#define PT_SCHEMA_NESSUNO   0    /* niente di riconoscibile */
#define PT_SCHEMA_MBR       1    /* tabella MBR valida */
#define PT_SCHEMA_GPT       2    /* MBR protettivo: il disco e' GPT */

/* Anomalie (maschera di bit) */
#define PT_PROB_FIRMA       0x0001  /* manca 0x55AA */
#define PT_PROB_OLTRE_FINE  0x0002  /* partizione oltre l'ultimo settore */
#define PT_PROB_SOVRAPP     0x0004  /* partizioni sovrapposte */
#define PT_PROB_CATENA      0x0008  /* catena estesa ciclica o rotta */
#define PT_PROB_BOOTFLAG    0x0010  /* flag di avvio diverso da 0x00/0x80 */
#define PT_PROB_GPT         0x0020  /* MBR protettivo: non modificabile qui */
#define PT_PROB_VUOTA       0x0040  /* tipo non nullo ma dimensione zero */
#define PT_PROB_TROPPE_EXT  0x0080  /* piu' di una estesa fra le primarie */
#define PT_PROB_TRONCATA    0x0100  /* piu' partizioni di quante ne stiano */
#define PT_PROB_SETTORE0    0x0200  /* partizione che comincia dall'LBA 0 */

typedef struct {
    uint8_t  attiva;        /* 0x80 = avviabile */
    uint8_t  tipo;          /* byte di tipo (0x06 FAT16, 0x0C FAT32, ...) */
    uint8_t  logica;        /* 1 se dentro la partizione estesa */
    /* Numero della partizione nella convenzione universale: 1-4 sono gli
     * SLOT delle primarie (anche se qualcuno e' vuoto), le logiche
     * partono da 5 in ordine di catena. NON e' l'indice nell'array: se
     * sul disco sono usati gli slot 1 e 3, questo campo vale 1 e 3, non
     * 1 e 2. Serve a far coincidere i nomi dei dispositivi (hd0p3) con
     * cio' che dicono fdisk e Linux — altrimenti chi partiziona con uno
     * strumento esterno e poi monta qui monterebbe la partizione
     * sbagliata credendo di aver capito il nome. */
    uint8_t  numero;
    uint64_t inizio;        /* LBA assoluto del primo settore */
    uint64_t settori;       /* lunghezza in settori */
} Partizione;

typedef struct {
    int         schema;                  /* PT_SCHEMA_* */
    uint32_t    problemi;                /* maschera PT_PROB_* */
    int         n;                       /* partizioni trovate */
    Partizione  p[MBR_MAX_PART];
} TabellaPartizioni;

/* Legge e valida la tabella del disco `indice` (indice ATA).
 * Ritorna 0 se il settore 0 e' stato letto, <0 se nemmeno quello.
 * Lo schema e le anomalie stanno dentro `out` — un ritorno 0 NON
 * significa "tabella sana", significa "sono riuscito a guardarla". */
int mbr_leggi(int indice, TabellaPartizioni *out);

/* Critica un INSIEME di partizioni — che venga dal disco o da una
 * proposta non fa differenza — e ritorna la maschera PT_PROB_* delle
 * anomalie trovate. 0 significa "nessuna".
 *
 * E' il cuore condiviso fra lettura e scrittura, ed e' condiviso di
 * proposito: vedi il commento in testa al file. */
uint32_t mbr_valida(const Partizione *p, int n, uint64_t disco_settori);

/* =============================================================================
 * Scrive le quattro voci PRIMARIE della tabella del disco `indice`.
 *
 * `voci[i]` descrive lo SLOT i+1; tipo 0x00 significa slot libero. Si
 * passano sempre e comunque tutti e quattro: una tabella e' un oggetto
 * unico e scriverla una voce alla volta produrrebbe stati intermedi
 * incoerenti su disco, che e' esattamente cio' che si sta cercando di
 * evitare.
 *
 * Le partizioni LOGICHE non si toccano: questa funzione non scrive EBR.
 * Riscrivere l'estesa senza riscriverne la catena e' possibile, e
 * spostarla o rimpicciolirla lascia le logiche irraggiungibili — per
 * questo la proposta viene rifiutata se cambia una voce estesa che sul
 * disco contiene gia' delle logiche.
 *
 * Cosa GARANTISCE, e nessun chiamante puo' aggirarlo:
 *
 *   1. si riscrivono SOLO i byte 446..511 (tabella + firma). I 446 byte
 *      del codice di avvio restano quelli che erano — cioe' quelli che
 *      ci ha messo boot_installa(), che a sua volta non tocca mai la
 *      tabella. Le due invarianti sono complementari: partizionare non
 *      cancella l'avvio, installare l'avvio non cancella la tabella, e
 *      i due comandi si possono dare in qualunque ordine.
 *
 *      UNICA ECCEZIONE, e va detta: se il settore 0 NON ha la firma
 *      0x55AA, i 446 byte vengono AZZERATI. Aggiungere la firma a un
 *      settore che non ce l'aveva significa dire al BIOS di eseguire
 *      quei byte, che fino a un attimo prima nessuno eseguiva; lasciarli
 *      com'erano vuol dire far saltare la macchina dentro spazzatura.
 *
 *   2. la proposta passa da mbr_valida() e viene rifiutata in blocco se
 *      ha una qualunque anomalia. Nessuna scrittura parziale.
 *
 *   3. un disco GPT non viene toccato mai.
 *
 * `problemi`, se non NULL, riceve la maschera PT_PROB_* del rifiuto: chi
 * chiama puo' dire QUALE controllo non e' passato invece di un generico
 * "non valida".
 *
 * Ritorna 0, oppure un errno negativo (-EINVAL proposta rifiutata,
 * -ENODEV disco assente, -EPERM disco GPT, -EBUSY l'estesa contiene
 * logiche e la proposta la sposta, -EIO errore di I/O).
 * ============================================================================= */
int mbr_scrivi(int indice, const Partizione *voci, int n, uint32_t *problemi);

#endif /* MBR_H */

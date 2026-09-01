/* =============================================================================
 * drivers/audio/audio_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL PROTOCOLLO DEL SERVIZIO "audio" — uno solo per TUTTE le schede
 *
 * Lo parlano /dev/sb.drv (Sound Blaster e AWE), /dev/es1371.drv (Sound
 * Blaster PCI 128), /dev/ac97.drv e /dev/hdaudio.drv (Realtek). Lo
 * includono i programmi che suonano. Un gioco che vuole un flusso PCM non
 * deve sapere quale scheda c'e' dentro la macchina, e SOPRATTUTTO non deve
 * essere ricompilato il giorno che se ne aggiunge una.
 *
 * -----------------------------------------------------------------------------
 * ! IL SUONO NON PASSA DAI MESSAGGI IPC, E QUESTA E' LA DECISIONE CENTRALE
 *
 * Un messaggio IPC porta 1536 byte. Un flusso a 44100 Hz, 16 bit, stereo
 * sono 176400 byte al secondo: 115 messaggi al secondo solo per non
 * balbettare, ognuno con una syscall, una copia nella mailbox del driver e
 * un cambio di contesto. E la mailbox e' profonda QUATTRO messaggi
 * (IPC_MAILBOX_DEPTH): al quinto il mittente aspetta. Il suono sarebbe
 * scandito dallo scheduler invece che dal quarzo della scheda.
 *
 * Quindi i campioni stanno in una zona di MEMORIA CONDIVISA — un anello che
 * il client riempie e il driver svuota — e l'IPC porta soltanto quello che
 * succede di rado: apri, via, ferma, volume, un messaggio MIDI.
 *
 * ! NEL PERCORSO CALDO NON C'E' NEMMENO UN MESSAGGIO. Il client scrive i
 * campioni e alza `scritto`; il driver, a ogni interrupt della scheda,
 * legge `scritto` e alza `suonato`. Nessuno dei due chiama l'altro. Un
 * gioco che va a 60 fotogrammi al secondo fa 60 scritture in memoria, non
 * 60 syscall — ed e' esattamente la ragione per cui un motore audio scritto
 * cosi' regge mentre uno scritto a messaggi no.
 *
 * -----------------------------------------------------------------------------
 * ! I DUE CONTATORI SONO MONOTONI, E NON SONO INDICI
 *
 * `scritto` e `suonato` contano i byte TOTALI passati da quando la zona e'
 * stata aperta; l'indice dentro l'anello e' il resto della divisione per
 * `byte`. Due indici che girano non distinguono l'anello pieno dall'anello
 * vuoto — in tutti e due i casi testa e coda coincidono — e la differenza
 * fra le due condizioni e' fra il silenzio e un rumore bianco a tutto
 * volume. Con i contatori monotoni:
 *
 *     pieno   = scritto - suonato          (byte che aspettano di suonare)
 *     libero  = byte - (scritto - suonato) (byte che il client puo' scrivere)
 *
 * e le due formule non si confondono mai. Il traboccamento a 32 bit non e'
 * un problema: la sottrazione fra due unsigned che traboccano insieme rende
 * comunque la distanza giusta.
 *
 * ! CHI SCRIVE COSA — la regola che tiene in piedi tutto senza serrature.
 * Il client scrive SOLO `scritto`, il driver scrive SOLO `suonato`. Nessuno
 * dei due tocca il campo dell'altro, quindi non serve un lucchetto: su i386
 * la scrittura di una parola allineata e' atomica, e ognuno legge un valore
 * che e' o quello di prima o quello di dopo, mai una via di mezzo.
 *
 * ! E L'ORDINE NON SI PUO' INVERTIRE. Prima si scrivono i CAMPIONI, poi si
 * alza `scritto`. Alzarlo prima vuol dire dire al driver che c'e' del suono
 * dove c'e' ancora la roba di due giri fa, e si sente.
 * ============================================================================= */

#ifndef AUDIO_PROTO_H
#define AUDIO_PROTO_H

/* Il nome registrato con ipc_register(). ! UNO SOLO PER TUTTE LE SCHEDE:
 * il secondo driver audio che parte non si registra e muore dicendolo,
 * invece di contendersi i campioni con il primo. */
#define AUDIO_SERVIZIO      "audio"

/* La zona di memoria condivisa dell'anello. Il nome sta qui e non e'
 * scelto dal driver: un client che non riesce a parlare col driver deve
 * poterla comunque nominare per dire che non c'e'. */
#define AUDIO_ZONA          "audio"

/* =============================================================================
 * I MESSAGGI
 * ========================================================================== */
/* Client -> driver */
#define AUDIO_MSG_INFO      0x0A01  /* «che scheda sei?»            (nessun dato) */
#define AUDIO_MSG_APRI      0x0A02  /* AudioFormato: prepara l'anello */
#define AUDIO_MSG_VIA       0x0A03  /* comincia a suonare           (nessun dato) */
#define AUDIO_MSG_FERMA     0x0A04  /* smetti, l'anello resta       (nessun dato) */
#define AUDIO_MSG_CHIUDI    0x0A05  /* smetti e lascia la zona      (nessun dato) */
#define AUDIO_MSG_VOLUME    0x0A06  /* AudioVolume */
#define AUDIO_MSG_MIDI      0x0A07  /* byte MIDI grezzi, fino a IPC_MSG_MAX_DATA */
#define AUDIO_MSG_STATO     0x0A08  /* «a che punto sei?»           (nessun dato) */
#define AUDIO_MSG_PROVA     0x0A09  /* AudioProva: la prova di collaudo */

/* Driver -> client */
#define AUDIO_MSG_INFO_R    0x0A81  /* AudioInfo */
#define AUDIO_MSG_ESITO     0x0A82  /* AudioEsito: risposta ad APRI e a PROVA */
#define AUDIO_MSG_STATO_R   0x0A83  /* AudioStato */
#define AUDIO_MSG_PROVA_R   0x0A84  /* AudioProvaEsito */

/* =============================================================================
 * COSA SA FARE UNA SCHEDA — i bit di AudioInfo.capacita
 *
 * ! SI DICHIARA CIO' CHE IL DRIVER SA GUIDARE, NON CIO' CHE IL SILICIO HA.
 * Una AWE32 ha il sintetizzatore EMU8000 a bordo; finche' nessuno scrive il
 * codice che ci carica i campioni, AUDIO_CAP_MIDI_ONDA resta spento e chi
 * chiede MIDI viene servito dall'OPL. La differenza fra «la scheda ce l'ha»
 * e «il sistema sa usarlo» e' l'unica che conta per chi chiama.
 * ========================================================================== */
#define AUDIO_CAP_PCM8      0x0001  /* campioni a 8 bit senza segno */
#define AUDIO_CAP_PCM16     0x0002  /* campioni a 16 bit con segno */
#define AUDIO_CAP_STEREO    0x0004
#define AUDIO_CAP_MIDI_FM   0x0008  /* sintesi FM: OPL2/OPL3, o l'emulazione */
#define AUDIO_CAP_MIDI_UART 0x0010  /* MPU-401 in modo UART: c'e' un sintetizzatore esterno */
#define AUDIO_CAP_MIDI_ONDA 0x0020  /* sintesi a tavola d'onde (EMU8000, AC'97 software) */
#define AUDIO_CAP_MIXER     0x0040  /* il volume si puo' cambiare */
#define AUDIO_CAP_REGISTRA  0x0080  /* sa anche registrare (nessun driver, per ora) */

/* Non c'e' nessuna scheda, o non se n'e' trovata */
#define AUDIO_DMA_NESSUNO   0xFF

/* =============================================================================
 * AudioInfo — la carta d'identita' della scheda
 *
 * ! I CAMPI base/irq/dma8/dma16 CI SONO APPOSTA, e non sono un dettaglio da
 * driver. Sono cio' che `audio -i` deve poter scrivere in kernel.cfg, e cio'
 * che un DOS box o un gioco portato si aspetta nella variabile BLASTER. Un
 * driver che li tenesse per se' costringerebbe l'utente a ritrovarli a mano.
 * ========================================================================== */
typedef struct {
    char         nome[32];      /* «Sound Blaster 16», «AWE64», «Realtek ALC» */
    char         bus[8];        /* «ISA» oppure «PCI» */
    unsigned int capacita;      /* AUDIO_CAP_* */
    unsigned int rate_min;      /* Hz */
    unsigned int rate_max;
    unsigned int base;          /* porta base ISA; 0 se la scheda e' PCI/MMIO */
    unsigned int irq;
    unsigned int dma8;          /* AUDIO_DMA_NESSUNO se non ne usa */
    unsigned int dma16;
    unsigned int mpu_base;      /* MPU-401, 0 se assente */
    unsigned int fm_base;       /* OPL2/OPL3, 0 se assente */
    unsigned int dsp_versione;  /* (maggiore << 8) | minore, 0 se non e' una SB */
} AudioInfo;

/* =============================================================================
 * AudioFormato — cosa si vuole suonare
 *
 * ! IL DRIVER PUO' RISPONDERE CON ALTRI NUMERI, e chi chiama deve leggerli.
 * Una SB 1.0 non fa lo stereo e si ferma a 22050 Hz; l'anello viene aperto
 * comunque, con il formato PIU' VICINO che la scheda sa suonare, e i numeri
 * veri tornano dentro AudioEsito. Un client che riempie l'anello con il
 * formato che ha CHIESTO invece che con quello che ha OTTENUTO produce
 * rumore alla velocita' sbagliata — che e' il modo peggiore di sbagliare,
 * perche' sembra un guasto della scheda.
 * ========================================================================== */
typedef struct {
    unsigned int rate;          /* Hz */
    unsigned int canali;        /* 1 = mono, 2 = stereo */
    unsigned int bit;           /* 8 oppure 16 */
} AudioFormato;

/* =============================================================================
 * AudioEsito — la risposta ad AUDIO_MSG_APRI
 * ========================================================================== */
typedef struct {
    int          esito;         /* 0, oppure -errno */
    AudioFormato formato;       /* quello CONCESSO, vedi sopra */
    char         zona[16];      /* il nome della zona shm (AUDIO_ZONA) */
    unsigned int zona_byte;     /* quanto misura tutta la zona */
    unsigned int anello_byte;   /* quanti byte di campioni ci stanno */
    unsigned int dati_off;      /* dove cominciano i campioni dentro la zona */
} AudioEsito;

typedef struct {
    unsigned int percento;      /* 0..100 */
} AudioVolume;

/* =============================================================================
 * AudioStato — a che punto e' la riproduzione
 * ========================================================================== */
#define AUDIO_ST_CHIUSO     0
#define AUDIO_ST_PRONTO     1   /* anello aperto, non ancora avviato */
#define AUDIO_ST_SUONA      2
#define AUDIO_ST_FERMO      3   /* avviato e poi fermato: l'anello resta */

typedef struct {
    unsigned int stato;         /* AUDIO_ST_* */
    unsigned int scritto;       /* copia dei contatori dell'anello */
    unsigned int suonato;
    unsigned int sottoflussi;   /* quante volte il driver ha trovato l'anello vuoto */
    unsigned int ms;            /* millisecondi suonati da AUDIO_MSG_VIA */
} AudioStato;

/* =============================================================================
 * AudioProva — il collaudo, chiesto al driver invece che al client
 *
 * ! LA PROVA STA NEL DRIVER, e la ragione e' che deve funzionare quando NON
 * c'e' ancora niente di installato. `audio -i` gira sulla macchina appena
 * accesa la prima volta: non ci sono file .wav, non c'e' un file .mid, non
 * c'e' un mixer da configurare. Se la prova avesse bisogno di dati sul disco
 * non si potrebbe fare proprio nel momento in cui serve — cioe' subito dopo
 * aver trovato la scheda, per sapere se il driver che si sta per scrivere in
 * kernel.cfg suona davvero.
 *
 * Il driver genera da solo il tono, la nota MIDI e il flusso, e RISPONDE se
 * l'hardware ha confermato: DMA partito, contatore avanzato, interrupt
 * arrivato. Non e' «ho scritto nei registri e non e' esploso» — quello lo
 * sa gia' chi ha scritto nei registri.
 * ========================================================================== */
#define AUDIO_PROVA_PCM8    1   /* un tono a 8 bit, DMA a 8 bit */
#define AUDIO_PROVA_PCM16   2   /* un tono a 16 bit stereo, DMA a 16 bit */
#define AUDIO_PROVA_MIDI    3   /* una nota: MPU-401 se c'e', altrimenti FM */
#define AUDIO_PROVA_FLUSSO  4   /* l'anello riempito a rate, come farebbe un gioco */

typedef struct {
    unsigned int quale;         /* AUDIO_PROVA_* */
    unsigned int ms;            /* quanto deve durare; 0 = il driver decide */
    unsigned int muta;          /* 1 = fai tutto ma a volume zero */
} AudioProva;

/* =============================================================================
 * AudioProvaEsito — cosa ha risposto l'HARDWARE, non cosa ha fatto il driver
 *
 * ! I TRE NUMERI IN MEZZO SONO LA PROVA, e il campo `esito` da solo non lo
 * sarebbe. «E' andata bene» detto da chi ha scritto nei registri vuol dire
 * soltanto che le scritture non sono fallite. Questi tre invece vengono da
 * fuori:
 *
 *   irq        quanti interrupt ha alzato la scheda. Zero significa che il
 *              DMA non e' mai partito, o che l'IRQ e' quello sbagliato — ed
 *              e' il guasto piu' comune su una scheda ISA.
 *   avanzato   di quanto e' sceso il contatore del controller DMA. Zero
 *              significa che la scheda non ha letto un byte.
 *   sottoflussi quante volte il driver e' arrivato tardi. Diverso da zero
 *              in una prova, dove i campioni sono gia' tutti pronti, dice
 *              che la macchina non tiene il passo del formato chiesto.
 * ========================================================================== */
typedef struct {
    unsigned int quale;         /* AUDIO_PROVA_*, ripetuto: le risposte possono accavallarsi */
    int          esito;         /* 0, oppure -errno */
    unsigned int irq;
    unsigned int avanzato;
    unsigned int sottoflussi;
    unsigned int ms;            /* quanto e' durata davvero */
    char         nota[64];      /* una frase per chi legge, mai vuota */
} AudioProvaEsito;

/* =============================================================================
 * L'ANELLO — la struttura che sta in cima alla zona di memoria condivisa
 *
 * Disposizione della zona:
 *
 *     +0            AudioAnello (questa struttura)
 *     +dati_off     i campioni, `byte` in tutto, che girano
 *
 * `dati_off` e' una pagina intera e non sizeof(AudioAnello): l'intestazione
 * la scrivono in due, i campioni li scrive uno solo, e tenerli in pagine
 * diverse vuol dire che il giorno che le pagine si potranno portare su
 * disco (vedi DIREZIONE.md, terza direttiva) quella dei campioni si potra'
 * bloccare da sola.
 * ========================================================================== */
#define AUDIO_ANELLO_MAGIA  0x4558414Du   /* "EXAM" — EX-OS Audio Memory */

typedef struct {
    unsigned int magia;         /* AUDIO_ANELLO_MAGIA: dice che la zona e' nostra */
    unsigned int byte;          /* quanti byte di campioni ci stanno */
    unsigned int dati_off;      /* dove cominciano, dall'inizio della zona */

    unsigned int rate;          /* il formato CONCESSO, ripetuto qui perche' */
    unsigned int canali;        /* chi si attacca alla zona possa leggerlo */
    unsigned int bit;           /* senza chiedere niente a nessuno */

    /* ! LO SCRIVE SOLO IL CLIENT */
    unsigned int scritto;       /* byte totali immessi (monotono) */

    /* ! LI SCRIVE SOLO IL DRIVER */
    unsigned int suonato;       /* byte totali consumati (monotono) */
    unsigned int sottoflussi;   /* quante volte non c'era niente da suonare */
    unsigned int stato;         /* AUDIO_ST_* */
} AudioAnello;

/* Quanto grande e' l'anello: mezzo secondo a 44100/16/stereo, arrotondato
 * alla potenza di due. ! DEV'ESSERE UNA POTENZA DI DUE perche' l'indice si
 * ricava con un AND e non con una divisione: la divisione a 32 bit dentro
 * il ciclo di riempimento, chiamato a ogni interrupt, si vede. */
#define AUDIO_ANELLO_BYTE   65536
#define AUDIO_DATI_OFF      4096
#define AUDIO_ZONA_BYTE     (AUDIO_DATI_OFF + AUDIO_ANELLO_BYTE)

/* Lo spazio libero e quello occupato, scritti una volta sola perche' non
 * se ne scrivano tre versioni diverse in tre programmi. */
#define AUDIO_PIENO(a)      ((a)->scritto - (a)->suonato)
#define AUDIO_LIBERO(a)     ((a)->byte - ((a)->scritto - (a)->suonato))

#endif /* AUDIO_PROTO_H */

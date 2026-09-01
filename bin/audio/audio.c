/* =============================================================================
 * bin/audio/audio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL COMANDO AUDIO — trova la scheda, la collauda, la mette in kernel.cfg
 *
 *     audio            dice se il suono e' acceso e su quale scheda
 *     audio -i         cerca, prova, collauda e installa. E' il comando che
 *                      si da' UNA VOLTA, la prima
 *     audio -c         rifa' solo il collaudo, sul driver gia' attivo
 *     audio -t         stampa la tabella delle schede riconosciute
 *     audio -v 70      volume, da 0 a 100
 *     audio file.wav   suona un file WAV
 *     audio -m file.mid  suona un file MIDI
 *
 * -----------------------------------------------------------------------------
 * ! COSA VUOL DIRE «-i» E PERCHE' NON SI FIDA DI AVER CARICATO IL DRIVER
 *
 * `spawn()` torna un PID appena il processo esiste, molto prima che il driver
 * abbia trovato la scheda, scoperto l'IRQ, preso il buffer DMA e registrato
 * il servizio. Un `-i` che dicesse «installato» a quel punto direbbe una cosa
 * vera e inutile: tutto cio' che interessa — la scheda non c'e', l'IRQ e' di
 * qualcun altro, il DMA non muove un byte — succede DOPO, e il driver muore
 * stampando il proprio motivo mentre `audio` e' gia' uscito contento. E'
 * esattamente la trappola che `netdetect -c` documenta da agosto.
 *
 * Quindi `-i` fa quattro cose in fila, e ognuna puo' dire di no:
 *
 *   1. cerca      chiede al bus PCI cosa c'e', e per l'ISA prova le sonde
 *   2. carica     avvia il driver e ASPETTA che il servizio compaia
 *   3. COLLAUDA   fa suonare la scheda e legge cosa risponde l'hardware:
 *                 interrupt arrivati, contatore DMA che avanza, il MIDI che
 *                 accetta byte. Se non suona, non si installa.
 *   4. installa   scrive la riga in [modules] di kernel.cfg
 *
 * ! IL COLLAUDO E' IL PUNTO, e non e' un abbellimento. Un driver audio che si
 * carica senza suonare e' il guasto normale, non quello raro: gli indirizzi
 * ISA si sovrappongono, l'IRQ e' condiviso, il canale DMA e' di un altro. Un
 * sistema che scrive in kernel.cfg un driver mai provato consegna all'utente
 * un silenzio che si manifesta al riavvio dopo, lontano dal comando che lo ha
 * causato.
 * ============================================================================= */

#include "libc.h"
#include "audio.h"
#include "audio_proto.h"
#include "pci_proto.h"

/* +0.001 a ogni modifica: `audio -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("audio", "0.001");

#define ATTESA_MS       2000

/* ! QUINDICI SECONDI, E NON QUATTRO. Un driver audio, prima di registrare il
 * servizio, deve: essere letto da un CD-ROM ATAPI, sondare fino a sette
 * indirizzi ISA, scoprire IRQ e canale DMA (che su una scheda vecchia vuol
 * dire provocare un interrupt e provare i canali uno per uno), prendere il
 * buffer DMA e sondare MPU-401 e OPL. Con quattro secondi `audio -i` si
 * arrendeva mentre il driver stava ancora lavorando, e stampava «non ha
 * registrato il servizio» un istante prima che lo registrasse: il messaggio
 * piu' fuorviante possibile, perche' descrive un guasto che non c'e'. */
#define ATTESA_DRIVER   15000

/* =============================================================================
 * Dialogo col servizio audio
 * ========================================================================== */
static int chiedi(int pid, unsigned int tipo, const void *dati, unsigned int len,
                  unsigned int risposta, void *out, unsigned int out_len,
                  unsigned int attesa_ms)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           giri;

    if (ipc_send(pid, tipo, dati, len) < 0) return -1;
    if (!risposta) return 0;

    /* ! SI CONTROLLA CHI HA RISPOSTO. ipc_recv consegna il prossimo messaggio
     * della cassetta, non «la risposta alla mia domanda». */
    for (giri = 0; giri < 8; giri++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), attesa_ms) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo != risposta) continue;
        if (meta.len < out_len) return -1;
        memcpy(out, buf, out_len);
        return 0;
    }
    return -1;
}

static int prendi_info(int pid, AudioInfo *info)
{
    return chiedi(pid, AUDIO_MSG_INFO, 0, 0,
                  AUDIO_MSG_INFO_R, info, sizeof(*info), ATTESA_MS);
}

static void stampa_info(const AudioInfo *i)
{
    printf("  scheda    %s (%s)\n", i->nome, i->bus);
    if (i->base) printf("  porta     0x%x\n", i->base);
    printf("  IRQ       %u\n", i->irq);
    if (i->dma8 != AUDIO_DMA_NESSUNO || i->dma16 != AUDIO_DMA_NESSUNO) {
        printf("  DMA       ");
        if (i->dma8  != AUDIO_DMA_NESSUNO) printf("%u a 8 bit", i->dma8);
        if (i->dma8  != AUDIO_DMA_NESSUNO &&
            i->dma16 != AUDIO_DMA_NESSUNO) printf(", ");
        if (i->dma16 != AUDIO_DMA_NESSUNO) printf("%u a 16 bit", i->dma16);
        printf("\n");
    }
    if (i->dsp_versione)
        printf("  DSP       %u.%02u\n", i->dsp_versione >> 8, i->dsp_versione & 0xFF);
    if (i->mpu_base) printf("  MIDI      MPU-401 a 0x%x\n", i->mpu_base);
    if (i->fm_base)  printf("  FM        OPL a 0x%x\n", i->fm_base);
    printf("  formati   %u-%u Hz", i->rate_min, i->rate_max);
    if (i->capacita & AUDIO_CAP_PCM8)   printf(", 8 bit");
    if (i->capacita & AUDIO_CAP_PCM16)  printf(", 16 bit");
    if (i->capacita & AUDIO_CAP_STEREO) printf(", stereo");
    printf("\n");
}

/* =============================================================================
 * IL COLLAUDO — quattro prove, e il giudizio lo da' il driver
 * ========================================================================== */
static int una_prova(int pid, unsigned int quale, const char *titolo,
                     unsigned int ms, int muta)
{
    AudioProva      p;
    AudioProvaEsito e;

    memset(&p, 0, sizeof(p));
    p.quale = quale;
    p.ms    = ms;
    p.muta  = muta ? 1 : 0;

    printf("  %-22s ", titolo);
    fflush(stdout);

    /* L'attesa e' generosa: la prova SUONA, quindi dura quanto dura. */
    if (chiedi(pid, AUDIO_MSG_PROVA, &p, sizeof(p),
               AUDIO_MSG_PROVA_R, &e, sizeof(e), ms + 4000) < 0) {
        printf("[nessuna risposta dal driver]\n");
        return -1;
    }

    if (e.esito == 0) printf("ok");
    else              printf("NO");

    if (e.irq || e.avanzato)
        printf("   (%u interrupt, DMA mosso %u volte", e.irq, e.avanzato);
    else if (e.ms)
        printf("   (%u ms", e.ms);
    else
        printf("   (");
    if (e.sottoflussi) printf(", %u sottoflussi", e.sottoflussi);
    printf(")\n");

    if (e.nota[0]) printf("                         %s\n", e.nota);
    return e.esito;
}

static int collauda(int pid, const AudioInfo *info, int muta)
{
    int guasti = 0;

    printf("\nCollaudo:\n");

    if (info->capacita & AUDIO_CAP_PCM8)
        if (una_prova(pid, AUDIO_PROVA_PCM8, "WAV PCM 8 bit mono", 500, muta) < 0)
            guasti++;

    if (info->capacita & AUDIO_CAP_PCM16)
        if (una_prova(pid, AUDIO_PROVA_PCM16, "WAV PCM 16 bit stereo", 500, muta) < 0)
            guasti++;

    if (info->capacita & (AUDIO_CAP_MIDI_FM | AUDIO_CAP_MIDI_UART |
                          AUDIO_CAP_MIDI_ONDA)) {
        if (una_prova(pid, AUDIO_PROVA_MIDI, "MIDI", 800, muta) < 0) guasti++;
    } else {
        printf("  %-22s [la scheda non ne ha uno]\n", "MIDI");
    }

    /* ! LA QUARTA PROVA E' QUELLA CHE CONTA PER I GIOCHI: l'anello riempito a
     * pezzi mentre suona, come fa un motore audio vero. Le prime tre possono
     * riuscire su una macchina che poi balbetta appena il flusso e' continuo,
     * perche' li' il buffer era gia' tutto pronto in partenza. */
    if (una_prova(pid, AUDIO_PROVA_FLUSSO, "flusso continuo", 1500, muta) < 0)
        guasti++;

    return guasti;
}

/* =============================================================================
 * La ricerca: prima il bus PCI, poi le sonde ISA
 * ========================================================================== */
static int pci_chiedi(int pid_pci, unsigned int ordinale, unsigned int classe,
                      PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           giri;

    memset(&r, 0, sizeof(r));
    r.ordinale    = ordinale;
    r.classe      = (unsigned short)classe;
    r.sottoclasse = PCI_QUALUNQUE;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send(pid_pci, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

    for (giri = 0; giri < 8; giri++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
        if ((int)meta.sender_pid != pid_pci) continue;
        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
    }
    return -1;
}

static int c_e_il_file(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* =============================================================================
 * Dove sta davvero il driver
 *
 * ! LA TABELLA DICE «/dev/sb.drv», E NON SEMPRE E' LI'. Su un sistema avviato
 * dal CD quel percorso e' giusto: la radice E' il CD. Su un sistema installato
 * su disco, se il driver e' stato copiato, pure. Ma su una macchina avviata da
 * FLOPPY con il CD nel lettore — che e' come si installa, ed e' proprio il
 * momento in cui si da' `audio -i` — /dev contiene i driver che stanno in
 * 1.44 MB e basta: il resto e' sotto /cdrom.
 *
 * Cercare in due posti invece che in uno e' la differenza fra «audio -i
 * funziona quando serve» e «audio -i funziona dopo, quando il suono lo si e'
 * gia' acceso a mano».
 * ========================================================================== */
static const char *risolvi(const char *p, char *buf, unsigned int max)
{
    if (c_e_il_file(p)) return p;

    snprintf(buf, max, "/cdrom%s", p);
    if (c_e_il_file(buf)) return buf;

    /* La RISERVA dei driver sul CD: la stessa da cui pesca `hwconfig -d`. */
    {
        const char *nome = strrchr(p, '/');
        snprintf(buf, max, "/cdrom/drivers%s", nome ? nome : p);
        if (c_e_il_file(buf)) return buf;
    }

    return 0;
}

/* Riempie `driver` con il percorso da provare. Rende 1 se ha trovato qualcosa
 * da provare, 0 se non c'e' niente. */
static int cerca_candidati(char driver[4][64], char modello[4][64])
{
    int n = 0, pid_pci, ord;

    /* --- PCI: si CHIEDE, e la risposta e' un fatto --- */
    pid_pci = ipc_lookup("pci");
    if (pid_pci > 0) {
        /* Classe 0x04 = multimedia. La sottoclasse distingue AC'97 (0x01) da
         * HD Audio (0x03), ma il driver lo dice gia' la tabella. */
        for (ord = 0; ord < 8 && n < 4; ord++) {
            PciDispositivo d;
            const AudioScheda *s;

            if (pci_chiedi(pid_pci, (unsigned int)ord, 0x04, &d) != 1) break;

            s = audio_riconosci(d.venditore, d.dispositivo);
            if (!s) {
                printf("  %04x:%04x  scheda audio non riconosciuta\n",
                       d.venditore, d.dispositivo);
                continue;
            }
            if (!s->driver) {
                printf("  %04x:%04x  %s - driver da scrivere\n",
                       d.venditore, d.dispositivo, s->modello);
                continue;
            }
            {
                char        alt[80];
                const char *dove = risolvi(s->driver, alt, sizeof(alt));

                if (!dove) {
                    printf("  %04x:%04x  %s - manca %s su questo supporto\n",
                           d.venditore, d.dispositivo, s->modello, s->driver);
                    continue;
                }
                printf("  %04x:%04x  %s\n", d.venditore, d.dispositivo, s->modello);
                strncpy(driver[n], dove, 63); driver[n][63] = 0;
            }
            strncpy(modello[n], s->modello, 63); modello[n][63] = 0;
            n++;
        }
    } else {
        printf("  (il server PCI non e' attivo: le schede PCI non si vedono.\n");
        printf("   Si accende con  /dev/pci.drv &)\n");
    }

    /* --- ISA: si PROVA, e provare e' un atto --- */
    {
        int i;
        for (i = 0; i < audio_isa_quanti() && n < 4; i++) {
            const AudioIsa *a = audio_isa(i);
            char            alt[80];
            const char     *dove = risolvi(a->driver, alt, sizeof(alt));

            if (!dove) continue;
            strncpy(driver[n], dove, 63); driver[n][63] = 0;
            strncpy(modello[n], a->modello, 63); modello[n][63] = 0;
            n++;
        }
    }

    return n;
}

/* =============================================================================
 * kernel.cfg — si modifica la riga, non si riscrive il file
 *
 * ! DIVERSO DA `hwconfig`, CHE LO RIGENERA PER INTERO, e la differenza e'
 * voluta: hwconfig guarda tutta la macchina e ha titolo per riscrivere tutto,
 * `audio -i` guarda una scheda sola. Un comando che riscrivesse il file
 * intero per aggiungere una riga porterebbe via le scelte fatte a mano da chi
 * quel file lo ha aperto — e in kernel.cfg le scelte a mano sono la regola,
 * non l'eccezione.
 * ========================================================================== */
#define CFG_MAX 8192

static int cfg_scrivi_modulo(const char *driver)
{
    static char vecchio[CFG_MAX];
    static char nuovo[CFG_MAX];
    FILE *f;
    unsigned int n = 0, i, len;
    char riga[160];
    int  in_modules = 0, scritto = 0, ha_modules = 0;

    f = fopen("/boot/kernel.cfg", "rb");
    if (!f) {
        printf("audio: non riesco a leggere /boot/kernel.cfg\n");
        printf("       Su un sistema avviato da CD o da floppy e' normale:\n");
        printf("       il driver si installa dopo aver installato il sistema.\n");
        return -1;
    }
    n = (unsigned int)fread(vecchio, 1, CFG_MAX - 1, f);
    fclose(f);
    vecchio[n] = 0;

    /* Copia riga per riga; dentro [modules] sostituisce la voce `audio`. */
    nuovo[0] = 0;
    i = 0;
    while (i < n) {
        unsigned int j = i, k = 0;
        while (j < n && vecchio[j] != '\n') j++;
        len = j - i;
        if (len > sizeof(riga) - 2) len = sizeof(riga) - 2;
        memcpy(riga, vecchio + i, len);
        riga[len] = 0;
        i = j + 1;

        /* Salta le righe vuote iniziali del confronto */
        while (riga[k] == ' ' || riga[k] == '\t') k++;

        if (riga[k] == '[') {
            in_modules = (strncmp(riga + k, "[modules]", 9) == 0);

            if (in_modules && !scritto) {
                char r2[200];

                /* ! LA RIGA VA SUBITO DOPO L'INTESTAZIONE, non in fondo alla
                 * sezione. In fondo vuol dire «dopo l'ultima riga qualunque»,
                 * e le ultime righe di [modules] sono un blocco di commenti
                 * che parla della sezione DOPO: la voce finiva in mezzo a
                 * spiegazioni dei montaggi, dove chi rilegge il file non
                 * penserebbe mai di cercarla. */
                ha_modules = 1;
                strncat(nuovo, riga, CFG_MAX - 1 - strlen(nuovo));
                strncat(nuovo, "\n", CFG_MAX - 1 - strlen(nuovo));
                snprintf(r2, sizeof(r2),
                         "# Il suono: lo ha scritto `audio -i` dopo averlo"
                         " collaudato.\n"
                         "audio       = %s\n", driver);
                strncat(nuovo, r2, CFG_MAX - 1 - strlen(nuovo));
                scritto = 1;
                continue;
            }
            if (in_modules) ha_modules = 1;
        } else if (in_modules && strncmp(riga + k, "audio", 5) == 0) {
            /* La voce c'era gia': si sostituisce e non si duplica. */
            char r2[160];
            snprintf(r2, sizeof(r2), "audio       = %s\n", driver);
            strncat(nuovo, r2, CFG_MAX - 1 - strlen(nuovo));
            scritto = 1;
            continue;
        }

        strncat(nuovo, riga, CFG_MAX - 1 - strlen(nuovo));
        strncat(nuovo, "\n", CFG_MAX - 1 - strlen(nuovo));
    }

    if (!scritto) {
        char r2[256];
        if (!ha_modules)
            strncat(nuovo, "\n[modules]\n", CFG_MAX - 1 - strlen(nuovo));
        snprintf(r2, sizeof(r2),
                 "# Il suono: messo qui da `audio -i` dopo averlo collaudato.\n"
                 "audio       = %s\n", driver);
        strncat(nuovo, r2, CFG_MAX - 1 - strlen(nuovo));
    }

    if (strlen(nuovo) >= CFG_MAX - 1) {
        printf("audio: kernel.cfg supererebbe gli 8191 byte. Non lo tocco.\n");
        return -1;
    }

    /* ! IL FILE PRECEDENTE NON SI PERDE, stessa regola di hwconfig. */
    f = fopen("/boot/kernel.cfg.bak", "wb");
    if (f) { fwrite(vecchio, 1, n, f); fclose(f); }

    f = fopen("/boot/kernel.cfg", "wb");
    if (!f) {
        /* ! IL CASO NORMALE, NON UN GUASTO: da CD o da floppy la radice e' in
         * sola lettura, e il file c'e' ma non si tocca. Dirlo con un errore
         * secco farebbe cercare un permesso o un disco pieno; il fatto vero e'
         * che l'installazione permanente si fa su un sistema installato. */
        printf("audio: /boot/kernel.cfg non si puo' riscrivere - questa radice\n");
        printf("       e' in sola lettura (CD o floppy). Il driver ADESSO e'\n");
        printf("       acceso e funziona; per averlo a ogni avvio bisogna prima\n");
        printf("       installare il sistema su disco, e poi ridare `audio -i`.\n");
        return -1;
    }
    fwrite(nuovo, 1, strlen(nuovo), f);
    fclose(f);
    return 0;
}

/* =============================================================================
 * porta_a_casa — il driver dev'essere DOVE la macchina lo cerchera'
 *
 * ! SCRIVERE «/cdrom/dev/sb.drv» IN kernel.cfg E' UNA TRAPPOLA A SCOPPIO
 * RITARDATO, e va evitata invece che spiegata. Il kernel avvia le voci di
 * [modules] al PASSO 14b; i montaggi di [mount] vengono dopo. Quel percorso
 * quindi NON ESISTE nel momento in cui il kernel lo cerca — e anche se
 * esistesse, basterebbe togliere il CD dal lettore perche' il suono sparisse
 * senza che niente lo colleghi al disco che manca.
 *
 * Allora il driver si COPIA dove starebbe di casa — /dev/ della radice — e in
 * kernel.cfg va quel percorso. Sono trentasette kilobyte: ci stanno anche su
 * un floppy, che di spazio libero ne ha novecento.
 *
 * Se la copia non riesce — radice in sola lettura, cioe' un avvio da CD — si
 * ritorna il percorso di partenza e chi chiama lo dira'. Non e' un fallimento
 * della copia: e' un supporto su cui non si installa niente, e lo si scopre
 * comunque un istante dopo, quando kernel.cfg non si lascia riscrivere.
 * ========================================================================== */
static const char *porta_a_casa(const char *da, char *buf, unsigned int max)
{
    static char blocco[8192];
    FILE *in, *out;
    const char *nome = strrchr(da, '/');
    size_t n;

    if (!nome) return da;

    snprintf(buf, max, "/dev%s", nome);
    if (strcmp(buf, da) == 0) return da;        /* e' gia' li' */
    if (c_e_il_file(buf)) return buf;           /* c'e' gia' una copia */

    in = fopen(da, "rb");
    if (!in) return da;

    out = fopen(buf, "wb");
    if (!out) { fclose(in); return da; }

    while ((n = fread(blocco, 1, sizeof(blocco), in)) > 0) {
        if (fwrite(blocco, 1, n, out) != n) {
            fclose(in); fclose(out);
            /* Una copia a meta' e' peggio di nessuna copia: il kernel
             * proverebbe a caricarla e il messaggio parlerebbe di ELF. */
            remove(buf);
            return da;
        }
    }
    fclose(in);
    fclose(out);

    printf("Copiato %s in %s\n", da, buf);
    return buf;
}

/* =============================================================================
 * -i
 * ========================================================================== */
static int installa(int muta)
{
    char driver[4][64], modello[4][64];
    int  n, i, pid;

    pid = ipc_lookup(AUDIO_SERVIZIO);
    if (pid > 0) {
        AudioInfo info;
        printf("Il servizio audio e' gia' attivo.\n");
        if (prendi_info(pid, &info) == 0) stampa_info(&info);
        printf("\nPer rifare solo il collaudo:  audio -c\n");
        return 0;
    }

    printf("Cerco una scheda audio...\n");
    n = cerca_candidati(driver, modello);
    if (n == 0) {
        printf("\nNessun driver da provare.\n");
        printf("Le schede riconosciute si elencano con  audio -t\n");
        return 1;
    }

    for (i = 0; i < n; i++) {
        char *argv[2];
        AudioInfo info;
        int guasti;

        printf("\nProvo %s (%s)\n", driver[i], modello[i]);

        argv[0] = driver[i];
        argv[1] = 0;
        if (spawn(driver[i], argv) < 0) {
            printf("  non si avvia.\n");
            continue;
        }

        /* ! SI ASPETTA IL SERVIZIO, NON IL PROCESSO. Vedi il commento in
         * testa: il PID non dice niente su cio' che il driver ha trovato. */
        pid = ipc_attendi(AUDIO_SERVIZIO, ATTESA_DRIVER);
        if (pid <= 0) {
            printf("  il driver non ha registrato il servizio: guardare cosa\n");
            printf("  ha stampato qui sopra, e' li' che c'e' il motivo.\n");
            continue;
        }

        if (prendi_info(pid, &info) < 0) {
            printf("  il servizio c'e' ma non risponde. Lo lascio stare.\n");
            continue;
        }
        printf("\n");
        stampa_info(&info);

        guasti = collauda(pid, &info, muta);
        if (guasti > 0) {
            printf("\n%d prove non riuscite: questa scheda non si installa.\n",
                   guasti);
            /* Il driver resta acceso: chi vuole insistere puo' provare a mano
             * con le opzioni, invece di ripartire da zero. */
            continue;
        }

        printf("\nTutte le prove riuscite.\n");
        {
            char        casa[80];
            const char *dove = porta_a_casa(driver[i], casa, sizeof(casa));

            if (cfg_scrivi_modulo(dove) == 0) {
                printf("Scritto in /boot/kernel.cfg:  audio = %s\n", dove);
                printf("Dal prossimo avvio il suono c'e' senza dare comandi.\n");
            } else if (strncmp(dove, "/cdrom", 6) == 0) {
                /* ! SI DICE ANCHE QUANDO NON SI PUO' FARE NIENTE. Chi ha
                 * appena sentito la scheda suonare deve sapere che il suono
                 * NON tornera' da solo al prossimo avvio, e perche'. */
                printf("Il driver e' rimasto sul CD: al prossimo avvio andra'\n");
                printf("riacceso a mano, o ridato `audio -i` da un sistema\n");
                printf("installato su disco.\n");
            }
        }
        return 0;
    }

    printf("\nNessuno dei driver provati ha superato il collaudo.\n");
    return 1;
}

/* =============================================================================
 * Suonare un file WAV
 *
 * ! SI LEGGE L'INTESTAZIONE, NON SI INDOVINA. Un .wav e' un contenitore RIFF:
 * i pezzi hanno un nome e una lunghezza, e fra "fmt " e "data" ce ne possono
 * stare altri (LIST, fact, INFO). Saltare a un offset fisso funziona con i
 * file scritti dal programma con cui si e' provato, e con nessun altro.
 * ========================================================================== */
static unsigned int leggi32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned int leggi16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static int suona_wav(const char *percorso)
{
    FILE *f;
    unsigned char cap[12], pezzo[8], fmt[16];
    AudioFormato  formato;
    AudioEsito    esito;
    ShmZona       z;
    AudioAnello  *anello;
    unsigned char *dati;
    unsigned int  resta = 0, pid;
    int           trovato_fmt = 0;

    pid = (unsigned int)audio_richiedi();
    if ((int)pid <= 0) return 1;

    f = fopen(percorso, "rb");
    if (!f) { printf("audio: %s non si apre\n", percorso); return 1; }

    if (fread(cap, 1, 12, f) != 12 ||
        memcmp(cap, "RIFF", 4) != 0 || memcmp(cap + 8, "WAVE", 4) != 0) {
        printf("audio: %s non e' un file WAV\n", percorso);
        fclose(f);
        return 1;
    }

    memset(&formato, 0, sizeof(formato));

    for (;;) {
        unsigned int len;

        if (fread(pezzo, 1, 8, f) != 8) break;
        len = leggi32(pezzo + 4);

        if (memcmp(pezzo, "fmt ", 4) == 0) {
            if (len < 16 || fread(fmt, 1, 16, f) != 16) break;
            if (leggi16(fmt) != 1) {
                printf("audio: %s e' compresso (formato %u): so suonare solo\n",
                       percorso, leggi16(fmt));
                printf("       il PCM non compresso.\n");
                fclose(f);
                return 1;
            }
            formato.canali = leggi16(fmt + 2);
            formato.rate   = leggi32(fmt + 4);
            formato.bit    = leggi16(fmt + 14);
            trovato_fmt = 1;
            if (len > 16) fseek(f, (long)(len - 16), SEEK_CUR);
        } else if (memcmp(pezzo, "data", 4) == 0) {
            resta = len;
            break;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);   /* i pezzi sono pari */
        }
    }

    if (!trovato_fmt || resta == 0) {
        printf("audio: %s non ha un pezzo 'data' leggibile\n", percorso);
        fclose(f);
        return 1;
    }

    printf("%s: %u Hz, %u bit, %s, %u byte\n", percorso, formato.rate,
           formato.bit, (formato.canali == 2) ? "stereo" : "mono", resta);

    if (chiedi((int)pid, AUDIO_MSG_APRI, &formato, sizeof(formato),
               AUDIO_MSG_ESITO, &esito, sizeof(esito), ATTESA_MS) < 0 ||
        esito.esito != 0) {
        printf("audio: il driver non apre questo formato (%d)\n", esito.esito);
        fclose(f);
        return 1;
    }

    if (esito.formato.rate   != formato.rate ||
        esito.formato.canali != formato.canali ||
        esito.formato.bit    != formato.bit) {
        /* ! SI DICE. Il driver ha il diritto di stringere il formato — vedi
         * AudioFormato in audio_proto.h — ma un file suonato a una frequenza
         * diversa da quella con cui e' stato inciso e' udibilmente storto, e
         * chi ascolta deve sapere che e' un limite della scheda e non del
         * file. */
        printf("audio: la scheda non fa questo formato; suono a %u Hz, %u bit, %s\n",
               esito.formato.rate, esito.formato.bit,
               (esito.formato.canali == 2) ? "stereo" : "mono");
    }

    memset(&z, 0, sizeof(z));
    strcpy(z.nome, esito.zona);
    z.byte = esito.zona_byte;
    z.flag = 0;                     /* la zona la crea il driver */
    if (shm_apri(&z) < 0) {
        printf("audio: non riesco ad attaccarmi alla zona '%s'\n", esito.zona);
        fclose(f);
        return 1;
    }
    anello = (AudioAnello *)z.virt;
    dati   = (unsigned char *)z.virt + anello->dati_off;

    /* Il primo riempimento, poi si parte. */
    {
        int partito = 0;

        while (resta > 0 || AUDIO_PIENO(anello) > 0) {
            unsigned int libero = AUDIO_LIBERO(anello);
            unsigned int testa, tratto, letti;

            if (libero > 512 && resta > 0) {
                testa  = anello->scritto & (anello->byte - 1);
                tratto = anello->byte - testa;          /* fino alla fine */
                if (tratto > libero) tratto = libero;
                if (tratto > resta)  tratto = resta;

                letti = (unsigned int)fread(dati + testa, 1, tratto, f);
                if (letti == 0) { resta = 0; }
                else {
                    /* ! I CAMPIONI PRIMA, IL CONTATORE DOPO. La regola sta in
                     * audio_proto.h e non ha eccezioni. */
                    anello->scritto += letti;
                    resta -= letti;
                }
            }

            if (!partito && (AUDIO_PIENO(anello) > anello->byte / 2 || resta == 0)) {
                ipc_send((unsigned int)pid, AUDIO_MSG_VIA, 0, 0);
                partito = 1;
            }

            if (partito && AUDIO_LIBERO(anello) < 512) usleep(5000);
            if (!partito && resta == 0) break;
        }

        /* La coda: si aspetta che il driver abbia consumato tutto. */
        while (partito && AUDIO_PIENO(anello) > 0) usleep(5000);
        usleep(100000);     /* l'ultima meta' di buffer del DMA */
    }

    ipc_send((unsigned int)pid, AUDIO_MSG_CHIUDI, 0, 0);
    shm_chiudi((void *)z.virt);
    fclose(f);
    return 0;
}

/* =============================================================================
 * Suonare un file MIDI (Standard MIDI File, formato 0 e 1)
 *
 * ! NON E' UN SEQUENCER, ed e' dichiarato: legge le tracce, le fonde per
 * tempo e manda i byte al driver, che li gira alla MPU-401 o li traduce per
 * l'OPL. Non ha una coda a priorita', non recupera se il sistema rallenta, e
 * i cambi di tempo li applica al prossimo evento. Per un file di prova e per
 * la musica di un gioco basta; per suonare uno spartito serio no, e allora la
 * strada e' un demone che tiene il tempo da solo.
 * ========================================================================== */
static unsigned int vlq(const unsigned char *p, unsigned int *usati)
{
    unsigned int v = 0, n = 0;

    do {
        v = (v << 7) | (p[n] & 0x7F);
    } while ((p[n++] & 0x80) && n < 4);

    *usati = n;
    return v;
}

static int suona_mid(const char *percorso)
{
    static unsigned char file[65536];
    FILE *f;
    unsigned int n, pos, divisione, tracce, i;
    unsigned int pid;

    pid = (unsigned int)audio_richiedi();
    if ((int)pid <= 0) return 1;

    f = fopen(percorso, "rb");
    if (!f) { printf("audio: %s non si apre\n", percorso); return 1; }
    n = (unsigned int)fread(file, 1, sizeof(file), f);
    fclose(f);

    if (n < 22 || memcmp(file, "MThd", 4) != 0) {
        printf("audio: %s non e' un file MIDI\n", percorso);
        return 1;
    }

    tracce    = (file[10] << 8) | file[11];
    divisione = (file[12] << 8) | file[13];
    if (divisione & 0x8000) {
        printf("audio: %s usa il tempo in fotogrammi SMPTE: non lo so leggere\n",
               percorso);
        return 1;
    }

    printf("%s: %u tracce, %u impulsi per semiminima\n",
           percorso, tracce, divisione);

    /* ! SI SUONA UNA TRACCIA PER VOLTA, e va detto perche' e' un limite vero:
     * un file di formato 1 ha la melodia su una traccia e l'accompagnamento
     * su un'altra, e suonarle in fila invece che insieme le fa sentire tutte
     * ma non fa la musica. Fonderle vuol dire una coda ordinata per tempo,
     * cioe' il sequencer che questo comando dichiara di non essere. */
    pos = 8 + ((unsigned int)(file[4] << 24 | file[5] << 16 |
                              file[6] << 8  | file[7]));

    for (i = 0; i < tracce && pos + 8 <= n; i++) {
        unsigned int lung, fine, tempo_us = 500000;   /* 120 semiminime/minuto */
        unsigned char stato = 0;

        if (memcmp(file + pos, "MTrk", 4) != 0) break;
        lung = (unsigned int)(file[pos+4] << 24 | file[pos+5] << 16 |
                              file[pos+6] << 8  | file[pos+7]);
        pos += 8;
        fine = pos + lung;
        if (fine > n) fine = n;

        while (pos < fine) {
            unsigned int usati, delta, ev;
            unsigned char msg[3];

            delta = vlq(file + pos, &usati);
            pos += usati;
            if (pos >= fine) break;

            if (delta) {
                /* microsecondi = delta * (us per semiminima) / (impulsi per
                 * semiminima).
                 *
                 * ! SPEZZATA IN QUOZIENTE E RESTO, e non fatta a 64 bit. Il
                 * prodotto diretto trabocca su una pausa lunga, e la versione
                 * a 64 bit chiamerebbe __udivdi3, che qui non c'e': si
                 * collega senza libgcc. Cosi' invece resta tutto a 32 bit e
                 * non perde niente — il resto e' minore del divisore per
                 * definizione, quindi il secondo prodotto e' piccolo. */
                unsigned int us = delta * (tempo_us / divisione) +
                                  (delta * (tempo_us % divisione)) / divisione;
                while (us > 0) {
                    unsigned int fetta = (us > 100000u) ? 100000u : us;
                    usleep(fetta);
                    us -= fetta;
                }
            }

            ev = file[pos];

            if (ev == 0xFF) {                       /* meta-evento */
                unsigned int tipo = file[pos + 1], l, u2;
                pos += 2;
                l = vlq(file + pos, &u2);
                pos += u2;
                if (tipo == 0x51 && l == 3)
                    tempo_us = (file[pos] << 16) | (file[pos+1] << 8) | file[pos+2];
                pos += l;
                if (tipo == 0x2F) break;            /* fine traccia */
                continue;
            }

            if (ev == 0xF0 || ev == 0xF7) {         /* sysex: si salta */
                unsigned int l, u2;
                pos++;
                l = vlq(file + pos, &u2);
                pos += u2 + l;
                continue;
            }

            if (ev & 0x80) { stato = (unsigned char)ev; pos++; }
            if (stato == 0) { pos++; continue; }

            msg[0] = stato;
            if ((stato & 0xF0) == 0xC0 || (stato & 0xF0) == 0xD0) {
                msg[1] = file[pos++];
                ipc_send(pid, AUDIO_MSG_MIDI, msg, 2);
            } else {
                msg[1] = file[pos++];
                msg[2] = file[pos++];
                ipc_send(pid, AUDIO_MSG_MIDI, msg, 3);
            }
        }

        pos = fine;
    }

    /* Tutte le note giu' su tutti i canali: un file che finisce con una nota
     * accesa la lascerebbe suonare per sempre. */
    for (i = 0; i < 16; i++) {
        unsigned char spegni[3];
        spegni[0] = (unsigned char)(0xB0 | i);
        spegni[1] = 123;                        /* all notes off */
        spegni[2] = 0;
        ipc_send(pid, AUDIO_MSG_MIDI, spegni, 3);
    }
    return 0;
}

/* =============================================================================
 * Stato, tabella, uso
 * ========================================================================== */
static int stato(void)
{
    int pid = ipc_lookup(AUDIO_SERVIZIO);
    AudioInfo  info;
    AudioStato s;

    if (pid <= 0) {
        printf("Suono: spento.\n\n");
        /* ! SE LA MACCHINA E' APPENA PARTITA, «spento» PUO' ESSERE FALSO. Il
         * kernel avvia le voci di [modules] insieme alla shell e non aspetta:
         * un driver audio che deve leggere il proprio eseguibile dal supporto,
         * sondare le porte e scoprire IRQ e DMA ci mette qualche secondo, e in
         * quei secondi questo comando direbbe che il suono non c'e' mentre sta
         * per esserci. Meglio dirlo che far cercare un guasto che non c'e'. */
        if (uptime_ms() < 20000)
            printf("  (la macchina e' appena partita: se in kernel.cfg c'e' una\n"
                   "   voce `audio`, il driver puo' essere ancora in avvio -\n"
                   "   riprovare fra qualche secondo)\n\n");
        printf("  audio -i    cerca la scheda, la collauda e la installa\n");
        return 1;
    }

    printf("Suono: acceso.\n");
    if (prendi_info(pid, &info) == 0) stampa_info(&info);

    if (chiedi(pid, AUDIO_MSG_STATO, 0, 0,
               AUDIO_MSG_STATO_R, &s, sizeof(s), ATTESA_MS) == 0) {
        static const char *nomi[4] = { "chiuso", "pronto", "suona", "fermo" };
        printf("  stato     %s", nomi[s.stato & 3]);
        if (s.sottoflussi) printf(" (%u sottoflussi)", s.sottoflussi);
        printf("\n");
    }
    return 0;
}

static void tabella(void)
{
    int i;

    printf("Schede PCI riconosciute:\n\n");
    printf("  ven:disp   modello                                    driver\n");
    printf("  ---------  -----------------------------------------  -------------\n");
    for (i = 0; i < audio_schede_note(); i++) {
        const AudioScheda *s = audio_scheda(i);
        printf("  %04x:%04x  %-41s  %s\n", s->venditore, s->dispositivo,
               s->modello, s->driver ? s->driver : "(da scrivere)");
    }

    printf("\nSchede ISA: non si enumerano, si provano -\n\n");
    for (i = 0; i < audio_isa_quanti(); i++) {
        const AudioIsa *a = audio_isa(i);
        printf("  %-13s %s\n", a->driver, a->modello);
    }
    printf("\n! Un codec Realtek (ALC...) non e' una scheda: sta su un\n");
    printf("  controller Intel o VIA, ed e' quello ad avere il driver.\n");
}

static void uso(void)
{
    printf("uso: audio [comando]\n\n");
    printf("  (senza argomenti)   dice se il suono e' acceso e su quale scheda\n");
    printf("  -i                  cerca la scheda, la collauda e la installa\n");
    printf("  -i -muto            come sopra, ma le prove non si sentono\n");
    printf("  -c                  rifa' il collaudo sul driver gia' attivo\n");
    printf("  -t                  la tabella delle schede riconosciute\n");
    printf("  -v 0..100           il volume\n");
    printf("  <file>.wav          suona un file WAV (PCM non compresso)\n");
    printf("  -m <file>.mid       suona un file MIDI\n");
}

int main(int argc, char **argv)
{
    int i, muto = 0;

    for (i = 1; i < argc; i++) if (strcmp(argv[i], "-muto") == 0) muto = 1;

    if (argc < 2) return stato();

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        uso();
        return 0;
    }
    if (strcmp(argv[1], "-t") == 0) { tabella(); return 0; }
    if (strcmp(argv[1], "-i") == 0) return installa(muto);

    if (strcmp(argv[1], "-c") == 0) {
        int pid = audio_richiedi();
        AudioInfo info;
        if (pid <= 0) return 1;
        if (prendi_info(pid, &info) < 0) {
            printf("audio: il servizio non risponde\n");
            return 1;
        }
        stampa_info(&info);
        return collauda(pid, &info, muto) ? 1 : 0;
    }

    if (strcmp(argv[1], "-v") == 0) {
        AudioVolume v;
        int pid = audio_richiedi();
        if (pid <= 0) return 1;
        if (argc < 3) { printf("uso: audio -v 0..100\n"); return 1; }
        v.percento = (unsigned int)atoi(argv[2]);
        if (v.percento > 100) v.percento = 100;
        ipc_send((unsigned int)pid, AUDIO_MSG_VOLUME, &v, sizeof(v));
        printf("volume: %u\n", v.percento);
        return 0;
    }

    if (strcmp(argv[1], "-m") == 0) {
        if (argc < 3) { printf("uso: audio -m file.mid\n"); return 1; }
        return suona_mid(argv[2]);
    }

    if (argv[1][0] == '-') { uso(); return 1; }

    return suona_wav(argv[1]);
}

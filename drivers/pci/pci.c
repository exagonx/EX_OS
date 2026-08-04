/* =============================================================================
 * drivers/pci/pci.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Server PCI di EX-OS (/dev/pci.drv) — PROCESSO RING3.
 *
 * Enumera il bus PCI e risponde alla domanda che i driver di scheda
 * hanno bisogno di fare: «dov'è il dispositivo di classe rete, su quali
 * porte risponde, su quale IRQ». Segue lo schema già usato da kbd.drv:
 * eseguibile ET_EXEC statico, nessuna istruzione privilegiata, tutto
 * l'hardware passa dal kernel.
 *
 * -----------------------------------------------------------------------------
 * PERCHE' IN USERSPACE
 *
 * L'enumerazione PCI è codice che legge tabelle scritte da terzi — BIOS,
 * firmware di schede, emulatori — e che quindi incontra prima o poi un
 * valore che non ci si aspettava. Dentro il kernel un ciclo che non
 * termina su un ponte mal formato è una macchina bloccata; qui è un
 * processo da far ripartire.
 *
 * Non è un discorso teorico: il ponte PCI-PCI dichiara il proprio bus
 * secondario in un byte del suo spazio di configurazione, e niente
 * impedisce a quel byte di puntare al bus da cui si è arrivati. Il
 * codice qui sotto tiene una mappa dei bus già visitati proprio per
 * questo (vedi g_bus_visto).
 *
 * -----------------------------------------------------------------------------
 * DUE MODI DI ESECUZIONE
 *
 *   pci.drv          registra il servizio "pci" e serve i client
 *   pci.drv -l       enumera, stampa, esce — nessuna registrazione
 *
 * Il secondo modo esiste per non dover scrivere un `lspci` separato che
 * duplicherebbe l'enumerazione: la seconda copia diverge dalla prima e
 * poi si passa il tempo a chiedersi quale delle due dice il vero.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ MECCANISMO DI CONFIGURAZIONE #1 E ACCESSI A 32 BIT
 *
 * CONFIG_ADDRESS (0xCF8) va scritto con UN accesso a 32 bit. Non è una
 * preferenza: un accesso a byte o word verso 0xCF8..0xCFB non viene
 * riconosciuto dal ponte come ciclo di configurazione e finisce sul bus
 * come normale I/O — e su molti chipset 0xCF9 è il registro di reset
 * della piattaforma, quindi scrivere l'indirizzo in quattro pezzi ha
 * ottime probabilità di riavviare la macchina. Per questo il kernel ha
 * dovuto crescere SYS_IOPORT_IN32/OUT32 (vedi kernel/include/syscall.h):
 * senza, questo file non si poteva scrivere in userspace, punto.
 *
 * ⚠️ 0xFFFFFFFF NON E' UN ERRORE. Quando in uno slot non c'è niente, il
 * ponte non risponde e il bus resta alto: si legge 0xFFFFFFFF. È il modo
 * NORMALE di scoprire uno slot vuoto, ed è anche il motivo per cui
 * ioport_in32() non può restituire il valore letto come valore di
 * ritorno — sarebbe -1, indistinguibile da un errore.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"

/* =============================================================================
 * Porte del meccanismo di configurazione #1
 *
 * Range rivendicato con ioport_bind(): 0xCF8..0xCFF. Il kernel rifiuta
 * con -EPERM ogni accesso fuori di qui, quindi un errore in questo file
 * non può toccare il controller ATA o il PIC.
 * ============================================================================= */
#define PCI_CFG_ADDR    0xCF8
#define PCI_CFG_DATA    0xCFC
#define PCI_PORT_BASE   PCI_CFG_ADDR
#define PCI_PORT_COUNT  8       /* 0xCF8..0xCFF */

/* Offset nello spazio di configurazione (intestazione comune) */
#define OFF_VENDITORE   0x00    /* word venditore, word dispositivo */
#define OFF_COMANDO     0x04    /* word comando, word stato */
#define OFF_REVISIONE   0x08    /* rev, prog-IF, sottoclasse, classe */
#define OFF_TIPO_INTEST 0x0C    /* cache line, latency, header type, BIST */
#define OFF_BAR0        0x10    /* sei BAR consecutivi */
#define OFF_BUS_SEC     0x18    /* solo sui ponti: primario, secondario, subord. */
#define OFF_IRQ         0x3C    /* linea IRQ, pin IRQ */

#define INTEST_MULTIFUNZIONE 0x80  /* bit 7 del tipo di intestazione */
#define INTEST_TIPO_MASK     0x7F
#define INTEST_NORMALE       0x00  /* dispositivo: sei BAR */
#define INTEST_PONTE         0x01  /* ponte PCI-PCI: due BAR */

/* Bit basso del BAR: 1 = spazio I/O, 0 = spazio di memoria */
#define BAR_E_IO        0x01
#define BAR_MASK_IO     0xFFFFFFFCu
#define BAR_MASK_MEM    0xFFFFFFF0u

/* Quanti dispositivi teniamo. Una macchina reale del 2026 ne ha qualche
 * decina; oltre questo numero si smette di aggiungerne e si dice quanti
 * ne mancano, invece di scrivere fuori dall'array e scoprirlo altrove. */
#define MAX_DISPOSITIVI 64

static PciDispositivo g_disp[MAX_DISPOSITIVI];
static int            g_n_disp   = 0;
static int            g_scartati = 0;   /* trovati ma non entrati nell'array */

/* Bus già visitati: 256 bit. Serve contro i ponti che dichiarano un bus
 * secondario già visto — vedi il commento di testa. */
static unsigned char  g_bus_visto[32];

static int bus_gia_visto(unsigned int bus)
{
    return (g_bus_visto[bus >> 3] >> (bus & 7)) & 1;
}

static void bus_segna(unsigned int bus)
{
    g_bus_visto[bus >> 3] |= (unsigned char)(1u << (bus & 7));
}

/* =============================================================================
 * Accesso allo spazio di configurazione
 * ============================================================================= */

/* Compone il valore di CONFIG_ADDRESS. Bit 31 = abilitazione; l'offset
 * viene troncato al multiplo di 4 perché il ciclo di configurazione è
 * sempre di una dword — i due bit bassi selezionano il byte DENTRO la
 * dword, e vanno usati sulla porta dati, non qui. */
static unsigned int cfg_indirizzo(unsigned int bus, unsigned int slot,
                                  unsigned int fn, unsigned int off)
{
    return 0x80000000u
         | ((bus  & 0xFF) << 16)
         | ((slot & 0x1F) << 11)
         | ((fn   & 0x07) << 8)
         | (off & 0xFC);
}

/* Legge una dword. Ritorna 0xFFFFFFFF anche in caso di errore della
 * syscall: è lo stesso valore che si legge da uno slot vuoto, quindi
 * chi chiama tratta i due casi allo stesso modo — «non c'è niente da
 * leggere qui». */
static unsigned int cfg_leggi(unsigned int bus, unsigned int slot,
                              unsigned int fn, unsigned int off)
{
    unsigned int v = 0xFFFFFFFFu;

    if (ioport_out32(PCI_CFG_ADDR, cfg_indirizzo(bus, slot, fn, off)) != 0)
        return 0xFFFFFFFFu;
    if (ioport_in32(PCI_CFG_DATA, &v) != 0)
        return 0xFFFFFFFFu;
    return v;
}

/* Scrive una word. Usata SOLO per il registro comando: vedi pci_proto.h
 * per il perché non esiste una scrittura generica.
 *
 * ⚠️ SI SCRIVE UNA WORD, NON UNA DWORD, E LA DIFFERENZA CONTA. A offset
 * 0x04 la dword contiene comando (basso) e STATO (alto), e i bit di
 * stato si azzerano scrivendoci 1. Un lettura-modifica-riscrittura della
 * dword intera rimetterebbe indietro i bit di stato appena letti,
 * cancellando errori che qualcun altro non ha ancora visto. Con una
 * scrittura a 16 bit sulla metà bassa lo stato non viene toccato. */
static int cfg_scrivi16(unsigned int bus, unsigned int slot, unsigned int fn,
                        unsigned int off, unsigned int val)
{
    if (ioport_out32(PCI_CFG_ADDR, cfg_indirizzo(bus, slot, fn, off)) != 0)
        return -1;
    return ioport_out16(PCI_CFG_DATA + (off & 2), val & 0xFFFF);
}

/* =============================================================================
 * Enumerazione
 * ============================================================================= */

static void scansiona_bus(unsigned int bus);   /* ricorsiva coi ponti */

/* Registra una funzione trovata. Ritorna il puntatore alla voce, o NULL
 * se l'array è pieno. */
static PciDispositivo *registra(unsigned int bus, unsigned int slot,
                                unsigned int fn, unsigned int id)
{
    PciDispositivo *d;
    unsigned int    v;
    int             i, n_bar;

    if (g_n_disp >= MAX_DISPOSITIVI) { g_scartati++; return NULL; }

    d = &g_disp[g_n_disp++];
    memset(d, 0, sizeof(*d));

    d->venditore   = (unsigned short)(id & 0xFFFF);
    d->dispositivo = (unsigned short)(id >> 16);
    d->bus         = (unsigned char)bus;
    d->slot        = (unsigned char)slot;
    d->funzione    = (unsigned char)fn;

    v = cfg_leggi(bus, slot, fn, OFF_REVISIONE);
    d->revisione   = (unsigned char)(v & 0xFF);
    d->interfaccia = (unsigned char)((v >> 8)  & 0xFF);
    d->sottoclasse = (unsigned char)((v >> 16) & 0xFF);
    d->classe      = (unsigned char)((v >> 24) & 0xFF);

    v = cfg_leggi(bus, slot, fn, OFF_IRQ);
    d->irq_linea   = (unsigned char)(v & 0xFF);

    /* Un ponte ha due BAR, non sei: leggerne sei significherebbe
     * interpretare come indirizzi i numeri di bus e le finestre di
     * inoltro, che stanno esattamente lì. */
    v = cfg_leggi(bus, slot, fn, OFF_TIPO_INTEST);
    n_bar = (((v >> 16) & INTEST_TIPO_MASK) == INTEST_PONTE) ? 2 : 6;

    for (i = 0; i < n_bar; i++) {
        unsigned int bar = cfg_leggi(bus, slot, fn, OFF_BAR0 + (unsigned)i * 4);

        if (bar == 0 || bar == 0xFFFFFFFFu) continue;

        if (bar & BAR_E_IO) {
            d->bar[i]    = bar & BAR_MASK_IO;
            d->bar_io[i] = 1;
        } else {
            d->bar[i]    = bar & BAR_MASK_MEM;
            d->bar_io[i] = 0;
            /* Un BAR di memoria a 64 bit occupa DUE voci: la seconda
             * contiene i 32 bit alti dell'indirizzo, non un altro BAR.
             * Saltarla evita di annunciare un dispositivo a un
             * indirizzo che è in realtà mezzo puntatore. */
            if (((bar >> 1) & 3) == 2) i++;
        }
    }

    return d;
}

/* Esamina una singola funzione. Se è un ponte, scende sul bus dietro. */
static void scansiona_funzione(unsigned int bus, unsigned int slot, unsigned int fn)
{
    unsigned int    id = cfg_leggi(bus, slot, fn, OFF_VENDITORE);
    PciDispositivo *d;

    if ((id & 0xFFFF) == 0xFFFF) return;   /* niente in questo slot */

    d = registra(bus, slot, fn, id);
    if (d == NULL) return;

    if (d->classe == PCI_CLASSE_PONTE && d->sottoclasse == PCI_SOTTO_PCI_PCI) {
        unsigned int v   = cfg_leggi(bus, slot, fn, OFF_BUS_SEC);
        unsigned int sec = (v >> 8) & 0xFF;

        if (!bus_gia_visto(sec)) scansiona_bus(sec);
    }
}

static void scansiona_bus(unsigned int bus)
{
    unsigned int slot;

    if (bus_gia_visto(bus)) return;
    bus_segna(bus);

    for (slot = 0; slot < 32; slot++) {
        unsigned int id, tipo, fn;

        id = cfg_leggi(bus, slot, 0, OFF_VENDITORE);
        if ((id & 0xFFFF) == 0xFFFF) continue;

        scansiona_funzione(bus, slot, 0);

        /* Le funzioni 1..7 si leggono SOLO se il bit multifunzione è
         * alzato. Su un dispositivo a funzione singola le altre sette
         * sono alias della zero: leggerle comunque non è più prudente,
         * è il modo di annunciare otto volte la stessa scheda. */
        tipo = (cfg_leggi(bus, slot, 0, OFF_TIPO_INTEST) >> 16) & 0xFF;
        if (!(tipo & INTEST_MULTIFUNZIONE)) continue;

        for (fn = 1; fn < 8; fn++) scansiona_funzione(bus, slot, fn);
    }
}

/* Enumerazione completa, da zero. Il bus 0 c'è sempre; gli altri si
 * raggiungono solo scendendo dai ponti, che è più economico e più
 * corretto che provare tutti i 256 (su una macchina con più host bridge
 * la forza bruta annuncia dispositivi che non sono raggiungibili). */
static void enumera(void)
{
    g_n_disp   = 0;
    g_scartati = 0;
    memset(g_bus_visto, 0, sizeof(g_bus_visto));

    scansiona_bus(0);
}

/* =============================================================================
 * Ricerca
 * ============================================================================= */

static int corrisponde(const PciDispositivo *d, const PciRichiesta *r)
{
    if (r->classe      != PCI_QUALUNQUE && d->classe      != r->classe)      return 0;
    if (r->sottoclasse != PCI_QUALUNQUE && d->sottoclasse != r->sottoclasse) return 0;
    if (r->venditore   != PCI_QUALUNQUE && d->venditore   != r->venditore)   return 0;
    if (r->dispositivo != PCI_QUALUNQUE && d->dispositivo != r->dispositivo) return 0;
    return 1;
}

/* Ritorna l'ordinale-esimo dispositivo che corrisponde, o NULL. */
static const PciDispositivo *cerca(const PciRichiesta *r)
{
    unsigned int trovati = 0;
    int i;

    for (i = 0; i < g_n_disp; i++) {
        if (!corrisponde(&g_disp[i], r)) continue;
        if (trovati == r->ordinale) return &g_disp[i];
        trovati++;
    }
    return NULL;
}

/* Cerca per posizione sul bus. Serve alle operazioni che arrivano dopo
 * una ricerca: il client rimanda bus/slot/funzione, non un indice che
 * una riscansione potrebbe aver spostato. */
static const PciDispositivo *cerca_posizione(unsigned int bus, unsigned int slot,
                                             unsigned int fn)
{
    int i;

    for (i = 0; i < g_n_disp; i++) {
        if (g_disp[i].bus == bus && g_disp[i].slot == slot &&
            g_disp[i].funzione == fn) return &g_disp[i];
    }
    return NULL;
}

/* =============================================================================
 * Operazioni sul registro comando
 *
 * ⚠️ SOLO SU DISPOSITIVI CHE ABBIAMO ENUMERATO. Il controllo non è
 * pignoleria: senza, un client potrebbe mandare bus/slot/funzione
 * arbitrari e accendere il bus mastering su qualcosa che non ha mai
 * visto — per esempio su un dispositivo dietro un ponte che non abbiamo
 * scansionato, cioè su hardware di cui nessuno qui sa niente.
 * ============================================================================= */
static int cambia_comando(const PciAzione *a, int accendi, PciEsito *esito)
{
    const PciDispositivo *d = cerca_posizione(a->bus, a->slot, a->funzione);
    unsigned int          cmd, nuovo;
    unsigned int          bit = a->bit & (PCI_ABIL_IO | PCI_ABIL_MEMORIA |
                                          PCI_ABIL_BUSMASTER);

    esito->comando = 0;

    if (d == NULL)          { esito->codice = -ENODEV; return -1; }
    if (bit != a->bit)      { esito->codice = -EINVAL; return -1; }
    if (bit == 0)           { esito->codice = -EINVAL; return -1; }

    cmd   = cfg_leggi(a->bus, a->slot, a->funzione, OFF_COMANDO) & 0xFFFF;
    nuovo = accendi ? (cmd | bit) : (cmd & ~bit);

    if (nuovo != cmd) {
        if (cfg_scrivi16(a->bus, a->slot, a->funzione, OFF_COMANDO, nuovo) != 0) {
            esito->codice = -EIO;
            return -1;
        }
    }

    /* Si rilegge invece di fidarsi: alcuni bit sono di sola lettura su
     * certi dispositivi, e dire «acceso» quando il bit non si è alzato
     * manderebbe il driver a cercare un guasto dove non c'è. */
    esito->comando = cfg_leggi(a->bus, a->slot, a->funzione, OFF_COMANDO) & 0xFFFF;
    esito->codice  = 0;
    return 0;
}

/* =============================================================================
 * Modo elenco (pci.drv -l)
 * ============================================================================= */

static const char *nome_classe(unsigned char c, unsigned char s)
{
    switch (c) {
    case 0x00: return "non classificato";
    case 0x01: return "controller di memoria di massa";
    case 0x02: return (s == 0x00) ? "Ethernet" : "controller di rete";
    case 0x03: return "controller video";
    case 0x04: return "multimediale";
    case 0x05: return "controller di memoria";
    case 0x06: return (s == 0x00) ? "host bridge"
                    : (s == 0x01) ? "ISA bridge"
                    : (s == 0x04) ? "ponte PCI-PCI" : "ponte";
    case 0x07: return "comunicazione";
    case 0x08: return "periferica di sistema";
    case 0x09: return "periferica di ingresso";
    case 0x0C: return (s == 0x03) ? "controller USB" : "bus seriale";
    default:   return "sconosciuto";
    }
}

static void stampa_elenco(void)
{
    int i, j;

    printf("PCI: %d dispositivi\n\n", g_n_disp);

    for (i = 0; i < g_n_disp; i++) {
        const PciDispositivo *d = &g_disp[i];

        printf("%02x:%02x.%d  %04x:%04x  %s\n",
               d->bus, d->slot, d->funzione,
               d->venditore, d->dispositivo,
               nome_classe(d->classe, d->sottoclasse));
        printf("           classe %02x.%02x rev %02x", d->classe,
               d->sottoclasse, d->revisione);
        if (d->irq_linea != 0xFF && d->irq_linea != 0)
            printf("  IRQ %u", d->irq_linea);
        printf("\n");

        for (j = 0; j < 6; j++) {
            if (d->bar[j] == 0) continue;
            printf("           BAR%d %s 0x%x\n", j,
                   d->bar_io[j] ? "I/O   " : "memoria", d->bar[j]);
        }
    }

    if (g_scartati > 0) {
        printf("\n⚠️  %d dispositivi trovati oltre il limite di %d e non elencati.\n",
               g_scartati, MAX_DISPOSITIVI);
    }
}

/* =============================================================================
 * Loop di servizio
 *
 * Un solo punto di attesa, come in kbd.drv: ipc_recv(). Non c'è nessun
 * contesto interrupt, quindi nessuna variabile di questo file è toccata
 * da due flussi diversi e non serve nessun `volatile`.
 * ============================================================================= */
static void servi(void)
{
    IpcMessage    meta;
    unsigned char payload[IPC_MSG_MAX_DATA];

    for (;;) {
        if (ipc_recv(&meta, payload, sizeof(payload)) < 0) continue;

        switch (meta.tipo) {

        case PCI_MSG_ELENCA:
        case PCI_MSG_CERCA: {
            PciRichiesta          r;
            const PciDispositivo *d;

            if (meta.len < sizeof(r)) {
                ipc_send(meta.sender_pid, PCI_MSG_FINE, NULL, 0);
                break;
            }
            memcpy(&r, payload, sizeof(r));

            /* PCI_MSG_ELENCA è PCI_MSG_CERCA senza filtri: si forzano a
             * PCI_QUALUNQUE invece di avere due percorsi che possono
             * divergere. */
            if (meta.tipo == PCI_MSG_ELENCA) {
                r.classe = r.sottoclasse = PCI_QUALUNQUE;
                r.venditore = r.dispositivo = PCI_QUALUNQUE;
            }

            d = cerca(&r);
            if (d == NULL) ipc_send(meta.sender_pid, PCI_MSG_FINE, NULL, 0);
            else ipc_send(meta.sender_pid, PCI_MSG_DISPOSITIVO, d, sizeof(*d));
            break;
        }

        case PCI_MSG_LEGGI: {
            PciAzione a;
            PciValore v;

            v.valore = 0xFFFFFFFFu;
            if (meta.len >= sizeof(a)) {
                memcpy(&a, payload, sizeof(a));
                if (cerca_posizione(a.bus, a.slot, a.funzione) != NULL)
                    v.valore = cfg_leggi(a.bus, a.slot, a.funzione, a.offset);
            }
            ipc_send(meta.sender_pid, PCI_MSG_VALORE, &v, sizeof(v));
            break;
        }

        case PCI_MSG_ABILITA:
        case PCI_MSG_DISABILITA: {
            PciAzione a;
            PciEsito  e;

            e.codice  = -EINVAL;
            e.comando = 0;
            if (meta.len >= sizeof(a)) {
                memcpy(&a, payload, sizeof(a));
                cambia_comando(&a, meta.tipo == PCI_MSG_ABILITA, &e);
            }
            ipc_send(meta.sender_pid, PCI_MSG_ESITO, &e, sizeof(e));
            break;
        }

        case PCI_MSG_RISCANSIONE: {
            PciEsito e;

            enumera();
            e.codice  = g_n_disp;
            e.comando = 0;
            ipc_send(meta.sender_pid, PCI_MSG_ESITO, &e, sizeof(e));
            break;
        }

        default:
            /* Messaggio che non conosciamo: si risponde comunque, perché
             * un client che resta in ipc_recv per sempre è un processo
             * bloccato che nessuno riesce più a collegare a questa riga. */
            ipc_send(meta.sender_pid, PCI_MSG_FINE, NULL, 0);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    int elenca_ed_esci = (argc > 1 && strcmp(argv[1], "-l") == 0);
    int rc;

    if (argc > 1 && !elenca_ed_esci) {
        printf("uso: pci.drv        avvia il servizio PCI\n");
        printf("     pci.drv -l     elenca i dispositivi ed esce\n");
        return 1;
    }

    rc = ioport_bind(PCI_PORT_BASE, PCI_PORT_COUNT);
    if (rc < 0) {
        printf("pci: ioport_bind(0x%x, %d) fallita (%d) — esco\n",
               PCI_PORT_BASE, PCI_PORT_COUNT, rc);
        return 1;
    }

    enumera();

    if (elenca_ed_esci) {
        stampa_elenco();
        return 0;
    }

    if (g_n_disp == 0) {
        /* Nessun dispositivo non vuol dire per forza «macchina senza
         * PCI»: vuol dire che nessuno ha risposto. Si registra lo stesso
         * il servizio — un client che chiede avrà una risposta chiara
         * («nessun dispositivo») invece di un ipc_lookup che fallisce e
         * lo lascia a indovinare se il server manca o è rotto. */
        printf("pci: nessun dispositivo risponde sul bus 0\n");
    }

    rc = ipc_register(PCI_SERVIZIO);
    if (rc < 0) {
        printf("pci: ipc_register('%s') fallita (%d) — esco\n", PCI_SERVIZIO, rc);
        return 1;
    }

    printf("pci: servizio '%s' attivo, %d dispositivi\n", PCI_SERVIZIO, g_n_disp);
    servi();
    return 0;
}

/* =============================================================================
 * bin/netdetect/netdetect.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Riconosce le schede di rete presenti e dice quale driver serve.
 *
 *   netdetect        elenca le schede Ethernet e il driver di ciascuna
 *   netdetect -c     carica il driver della prima scheda che sa guidare
 *   netdetect -t     stampa la tabella dei modelli riconosciuti
 *
 * È il client del server PCI (/dev/pci.drv): non tocca l'hardware, fa una
 * domanda e legge la risposta. Se il server non è avviato lo dice e
 * spiega come avviarlo, invece di lasciare un errore numerico.
 *
 * -----------------------------------------------------------------------------
 * PERCHE' UNA TABELLA E NON UNA CATENA DI `if`
 *
 * L'associazione scheda -> driver è un dato, non una decisione: cambia
 * ogni volta che si aggiunge un driver e non cambia mai per altri motivi.
 * Tenerla in un array significa che aggiungere una scheda è una riga in
 * più in un punto solo, e che l'elenco si può stampare (`-t`) — cosa che
 * con una catena di `if` non si può fare senza riscriverla a mano, e
 * quindi senza che prima o poi le due versioni si contraddicano.
 *
 * -----------------------------------------------------------------------------
 * ! `-c` NON SI FIDA DI AVER LANCIATO IL DRIVER: CONTROLLA CHE SIA VIVO
 *
 * `spawn()` torna un PID appena il processo è stato creato, molto prima
 * che il driver abbia trovato la scheda, letto la PROM e registrato il
 * servizio. Un `-c` che stampasse «caricato» a quel punto direbbe una
 * cosa vera e inutile: il caso che interessa — la scheda non risponde,
 * l'IRQ è di qualcun altro, la firma della PROM è sbagliata — succede
 * TUTTO dopo, e il driver muore stampando il proprio errore mentre
 * netdetect è già uscito dicendo che è andata bene.
 *
 * Perciò dopo lo spawn si aspetta che il nome del servizio compaia nel
 * registro IPC. Se non compare entro il tempo previsto, il messaggio dice
 * di guardare cosa ha stampato il driver — che è l'unico posto dove sta
 * scritto il motivo vero.
 *
 * -----------------------------------------------------------------------------
 * ! LE SCHEDE ISA NON SI VEDONO DA QUI, E NON E' UN DIFETTO DI QUESTO FILE
 *
 * Una NE2000 ISA non ha spazio di configurazione: non si enumera. E non
 * si sonda nemmeno a tentativi, né qui né nel driver: per riconoscerla
 * bisogna scrivere sulla sua porta di reset, e se a quell'indirizzo c'è
 * un'altra scheda le si è appena scritto addosso. La lista dei «soliti
 * indirizzi» (0x300, 0x280, 0x320...) è esattamente la lista di quelli
 * che si contendevano tutte le schede ISA.
 *
 * Perciò l'indirizzo va dichiarato:  /dev/ne2k.drv -p 0x300 -q 3
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "net_proto.h"
#include "rete.h"

/* -----------------------------------------------------------------------------
 * ! LA TABELLA DEI MODELLI NON STA PIU' QUI, dal 24 agosto 2026: sta in
 * lib/rete.c, e questo file la legge con rete_riconosci() e rete_scheda().
 *
 * Ci stava, con accanto il commento «duplicarla darebbe due elenchi che
 * divergono al primo driver nuovo». E' andata proprio cosi', ma senza copia:
 * e' stato scritto /dev/e1000.drv e la riga 8086:100E e' rimasta a «driver da
 * scrivere», perche' chi scriveva un driver non aveva motivo di aprire il
 * sorgente di netdetect. Su QEMU — dove quella scheda e' la predefinita —
 * `netdetect -c` diceva che il driver non c'era mentre stava nel CD accanto.
 *
 * Il secondo lettore, adesso, e' `hwconfig`: deve scrivere in kernel.cfg il
 * driver giusto per la scheda trovata, cosi' la rete si accende all'avvio
 * invece che a ogni accesso. Con la tabella dentro un programma non poteva.
 * --------------------------------------------------------------------------- */

/* -----------------------------------------------------------------------------
 * Dialogo col server PCI
 *
 * ! SI CONTROLLA CHI HA RISPOSTO. ipc_recv consegna il prossimo messaggio
 * della mailbox, non «la risposta alla mia domanda»: se qualcun altro ci
 * scrive nel frattempo, prenderemmo il suo messaggio per la risposta del
 * server e leggeremmo i suoi byte come un PciDispositivo. I messaggi che
 * non vengono dal server si scartano.
 *
 * E si usa la versione con scadenza: se il server muore fra la domanda e
 * la risposta, un ipc_recv senza timeout lascerebbe questo programma
 * fermo per sempre, e chi lo ha lanciato non saprebbe nemmeno perché.
 * --------------------------------------------------------------------------- */
#define ATTESA_MS 2000

static int chiedi(int pid_pci, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           tentativi;

    r.ordinale    = ordinale;
    r.classe      = PCI_CLASSE_RETE;
    r.sottoclasse = PCI_SOTTO_ETHERNET;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send(pid_pci, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;

    for (tentativi = 0; tentativi < 8; tentativi++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
        if ((int)meta.sender_pid != pid_pci) continue;   /* non è la nostra risposta */

        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
        return -1;
    }
    return -1;
}

/* Vero se il file del driver esiste davvero su questo supporto.
 *
 * ! SERVE PERCHE' LA TABELLA DICE COSA SERVIREBBE, NON COSA C'E'. Il
 * nome /dev/pcnet.drv sta in tabella perche' quella scheda si guida cosi';
 * il file compare quando quel driver e' scritto e sta sul supporto da cui
 * si e' avviati. Dire «driver disponibile» leggendo solo la tabella
 * significa promettere qualcosa che non c'e', e mandare chi legge a
 * cercare un guasto nella scheda. */
static int driver_presente(const char *driver)
{
    struct stat st;

    if (driver == NULL) return 0;
    return stat(driver, &st) == 0;
}

/* -----------------------------------------------------------------------------
 * Caricamento del driver
 * --------------------------------------------------------------------------- */
#define ATTESA_AVVIO_MS  4000
#define PASSO_MS          100

/* Aspetta che il driver registri il proprio servizio. Il nome lo si
 * ricava dal driver stesso? No: lo si conosce, ed è "rete0" per
 * definizione — la prima interfaccia di rete del sistema. */
static int attendi_servizio(const char *nome)
{
    int trascorso;

    for (trascorso = 0; trascorso < ATTESA_AVVIO_MS; trascorso += PASSO_MS) {
        if (ipc_lookup(nome) > 0) return 1;
        usleep(PASSO_MS * 1000);
    }
    return 0;
}

static int carica(const char *driver)
{
    char *const argv[] = { (char *)driver, NULL };
    int   pid;

    /* Si guarda prima se il file c'è: «/dev/ne2k.drv non esiste su questo
     * supporto» e «il driver è partito e non ha trovato la scheda» sono
     * due guasti diversi, e uno spawn fallito li confonderebbe in un
     * numero solo. */
    if (!driver_presente(driver)) {
        printf("netdetect: %s non c'e' su questo supporto.\n", driver);
        printf("           I driver di rete stanno sul CD di EX-OS.\n");
        return 1;
    }

    pid = spawn(driver, argv);
    if (pid < 0) {
        printf("netdetect: impossibile avviare %s (%d)\n", driver, pid);
        return 1;
    }

    if (!attendi_servizio(NET_SERVIZIO_0)) {
        printf("\nnetdetect: %s e' partito (PID %d) ma non ha registrato\n",
               driver, pid);
        printf("           il servizio '%s'. Il motivo l'ha stampato lui\n",
               NET_SERVIZIO_0);
        printf("           qui sopra: e' l'unico posto dove sta scritto.\n");
        return 1;
    }

    printf("\n%s attivo: il servizio '%s' risponde.\n", driver, NET_SERVIZIO_0);
    return 0;
}

static void stampa_tabella(void)
{
    int i;

    printf("Modelli riconosciuti (%d):\n\n", rete_schede_note());
    for (i = 0; i < rete_schede_note(); i++) {
        const ReteScheda *n = rete_scheda(i);
        const char       *d = n->driver;

        printf("  %04x:%04x  %-44s %s%s\n",
               n->venditore, n->dispositivo, n->modello,
               d ? d : "(driver da scrivere)",
               (d && !driver_presente(d)) ? "  [assente]" : "");
    }
    printf("\nUna NE2000 ISA non compare qui: non ha spazio di configurazione,\n");
    printf("quindi non si enumera. Non si cerca nemmeno a tentativi — se a\n");
    printf("quell'indirizzo c'e' un'altra scheda le si scrive addosso — e va\n");
    printf("percio' indicata a mano:\n\n");
    printf("  /dev/ne2k.drv -p 0x300 -q 3\n");
}

/* Prima porta I/O dichiarata dalla scheda: è quella che il driver dovrà
 * rivendicare con ioport_bind(). Una scheda che risponde solo in memoria
 * non ne ha, e va detto invece di stampare 0. */
static unsigned int prima_porta_io(const PciDispositivo *d)
{
    int i;

    for (i = 0; i < 6; i++)
        if (d->bar_io[i] && d->bar[i] != 0) return d->bar[i];
    return 0;
}

int main(int argc, char **argv)
{
    PciDispositivo d;
    unsigned int   n = 0;
    int            pid_pci, esito, gestibili = 0, carica_dopo = 0;
    const char    *da_caricare = NULL;

    if (argc > 1 && strcmp(argv[1], "-t") == 0) { stampa_tabella(); return 0; }

    if (argc > 1 && strcmp(argv[1], "-c") == 0) carica_dopo = 1;

    if (argc > 1 && !carica_dopo) {
        printf("uso: netdetect        elenca le schede di rete e il loro driver\n");
        printf("     netdetect -c     carica il driver della prima scheda gestibile\n");
        printf("     netdetect -t     stampa la tabella dei modelli riconosciuti\n");
        return 1;
    }

    pid_pci = rete_richiedi(PCI_SERVIZIO);
    if (pid_pci <= 0) return 1;

    for (;;) {
        esito = chiedi(pid_pci, n, &d);

        if (esito < 0) {
            printf("netdetect: il servizio '%s' non ha risposto.\n", PCI_SERVIZIO);
            return 1;
        }
        if (esito == 0) break;

        {
            const ReteScheda *s  = rete_riconosci(d.venditore, d.dispositivo);
            unsigned int      io = prima_porta_io(&d);

            printf("%02x:%02x.%d  %04x:%04x  %s\n",
                   d.bus, d.slot, d.funzione, d.venditore, d.dispositivo,
                   s ? s->modello : "modello sconosciuto");

            if (io != 0) printf("           porte I/O da 0x%x", io);
            else         printf("           solo spazio di memoria");
            if (d.irq_linea != 0 && d.irq_linea != 0xFF)
                printf(", IRQ %u", d.irq_linea);
            printf("\n");

            if (s == NULL) {
                printf("           driver: nessuno. Serve il numero %04x:%04x\n",
                       d.venditore, d.dispositivo);
                printf("                   nella tabella di lib/rete.c.\n");
            } else if (s->driver == NULL) {
                printf("           driver: da scrivere per questo modello.\n");
            } else if (!driver_presente(s->driver)) {
                printf("           driver: %s — non presente su questo supporto\n",
                       s->driver);
            } else {
                printf("           driver: %s\n", s->driver);
                if (da_caricare == NULL) da_caricare = s->driver;
                gestibili++;
            }
        }

        n++;
    }

    if (n == 0) {
        printf("netdetect: nessun controller Ethernet sul bus PCI.\n");
        printf("           Una scheda ISA non si vede da qui: vedi netdetect -t.\n");
        return 1;
    }

    printf("\n%u %s, %d con un driver presente e utilizzabile.\n",
           n, (n == 1) ? "scheda trovata" : "schede trovate", gestibili);

    if (carica_dopo) {
        if (da_caricare == NULL) {
            printf("\nnetdetect: nessuna delle schede trovate ha un driver.\n");
            return 1;
        }
        return carica(da_caricare);
    }

    return (gestibili > 0) ? 0 : 1;
}

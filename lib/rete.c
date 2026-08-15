/* =============================================================================
 * lib/rete.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Stato della catena di rete e istruzioni per accenderla. Il perché sta
 * in lib/include/rete.h.
 * ============================================================================= */

#include "libc.h"
#include "rete.h"
#include "pci_proto.h"
#include "net_proto.h"
#include "ip_proto.h"

typedef struct {
    const char *nome;       /* che cos'è, per chi legge */
    const char *servizio;   /* nome nel registro IPC, NULL = si controlla altrove */
    const char *comando;    /* come si accende */
} Anello;

static const Anello g_catena[RETE_PASSI] = {
    { "bus PCI",        PCI_SERVIZIO,   "/dev/pci.drv &" },
    { "scheda di rete", NET_SERVIZIO_0, "netdetect -c"   },
    { "stack IP",       IP_SERVIZIO,    "/dev/ip.drv &"  },
    { "indirizzo",      NULL,           "dhcp"           },
};

/* L'ultimo anello non è un servizio: è una CONFIGURAZIONE. Si controlla
 * chiedendo allo stack se ha un indirizzo diverso da zero.
 *
 * ! SI USA UNA SCADENZA BREVE. Questa funzione viene chiamata quando
 * qualcosa già non va, e un ipc_recv senza scadenza trasformerebbe «lo
 * stack è impallato» in «il comando che doveva spiegartelo è impallato
 * anche lui». */
static int indirizzo_configurato(int pid_ip)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    IpStato       s;
    int           i;

    if (pid_ip <= 0) return 0;
    if (ipc_send(pid_ip, IP_MSG_STATO, NULL, 0) < 0) return 0;

    for (i = 0; i < 8; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 1000) < 0) return 0;
        if ((int)meta.sender_pid != pid_ip) continue;
        if (meta.tipo != IP_MSG_STATO_R) continue;
        if (meta.len < sizeof(s)) return 0;

        memcpy(&s, buf, sizeof(s));
        return (s.cfg.ip[0] | s.cfg.ip[1] | s.cfg.ip[2] | s.cfg.ip[3]) != 0;
    }
    return 0;
}

/* Riempie `fatto[]` con lo stato di ogni anello. */
static void esamina(int *fatto)
{
    int pid_ip = 0, i;

    for (i = 0; i < RETE_PASSI; i++) fatto[i] = 0;

    for (i = 0; i < RETE_PASSI; i++) {
        if (g_catena[i].servizio == NULL) continue;
        if (ipc_lookup(g_catena[i].servizio) > 0) {
            fatto[i] = 1;
            if (i == RETE_PASSO_STACK) pid_ip = ipc_lookup(IP_SERVIZIO);
        }
    }

    if (fatto[RETE_PASSO_STACK])
        fatto[RETE_PASSO_INDIRIZZO] = indirizzo_configurato(pid_ip);
}

int rete_primo_mancante(void)
{
    int fatto[RETE_PASSI], i;

    esamina(fatto);
    for (i = 0; i < RETE_PASSI; i++) if (!fatto[i]) return i;
    return RETE_PASSI;
}

void rete_istruzioni(void)
{
    int fatto[RETE_PASSI], i, primo = RETE_PASSI;

    esamina(fatto);
    for (i = 0; i < RETE_PASSI; i++) if (!fatto[i]) { primo = i; break; }

    if (primo == RETE_PASSI) {
        printf("La rete e' pronta: bus, scheda, stack e indirizzo ci sono.\n");
        printf("`ipcfg` mostra la configurazione, `ipcfg -r` la tabella ARP.\n");
        return;
    }

    printf("\nStato della catena di rete:\n\n");
    for (i = 0; i < RETE_PASSI; i++) {
        printf("  %-8s %-16s %s\n",
               fatto[i] ? "[ok]" : "[manca]",
               g_catena[i].nome,
               g_catena[i].comando);
    }

    /* ! SI INDICA UN PASSO SOLO. Elencare tutti i comandi mancanti
     * sembra piu' utile e non lo e': ognuno serve al successivo, quindi
     * lanciarli tutti insieme fa fallire quelli dopo il primo per un
     * motivo diverso da quello vero, e chi legge insegue l'errore
     * sbagliato. */
    printf("\nIl prossimo passo e':\n\n    %s\n\n", g_catena[primo].comando);

    if (primo == RETE_PASSO_SCHEDA) {
        printf("Se `netdetect -c` non trova niente, `netdetect` da solo\n");
        printf("elenca le schede viste e dice quale driver servirebbe.\n\n");
    }
    if (primo == RETE_PASSO_INDIRIZZO) {
        printf("Senza un server DHCP l'indirizzo si mette a mano:\n");
        printf("    ipcfg -a 192.168.1.10 -m 255.255.255.0 -g 192.168.1.1\n\n");
    }
}

/* =============================================================================
 * ! SI ASPETTA UN POCO, E NON E' UNA CORTESIA
 *
 * Un servizio lanciato con '&' esiste come processo molto prima di essere
 * REGISTRATO: deve trovare l'hardware, inizializzarlo e solo allora
 * chiamare ipc_register(). Fra i due momenti passano decine di
 * millisecondi.
 *
 * Chi scrive uno script — un autoexec, per esempio — lancia il servizio e
 * usa il comando successivo subito dopo. Senza questa attesa quel comando
 * trova il registro vuoto e stampa le istruzioni per accendere una cosa
 * che si sta gia' accendendo. E' esattamente quello che e' successo alla
 * prima prova di /boot/autoexec.sh: `dhcp` falliva perche' `/dev/ip.drv &`
 * era partito due righe sopra.
 *
 * Due secondi sono un compromesso: abbastanza perche' un servizio che sta
 * partendo faccia in tempo, abbastanza poco da non far sembrare bloccato
 * un comando su una macchina dove quel servizio non c'e' proprio.
 * ============================================================================= */
#define ATTESA_SERVIZIO_MS  2000
#define PASSO_MS             100

int rete_richiedi(const char *servizio)
{
    int pid, trascorso;

    for (trascorso = 0; ; trascorso += PASSO_MS) {
        pid = ipc_lookup(servizio);
        if (pid > 0) return pid;
        if (trascorso >= ATTESA_SERVIZIO_MS) break;
        usleep(PASSO_MS * 1000);
    }

    printf("Il servizio '%s' non e' attivo.\n", servizio);
    rete_istruzioni();
    return pid;
}

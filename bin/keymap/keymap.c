/* =============================================================================
 * bin/keymap/keymap.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Sceglie la disposizione della tastiera.
 *
 *     keymap              quale c'e' adesso, e quali si possono avere
 *     keymap it           passa a quella italiana, subito
 *     keymap -p           stampa la riga da mettere in kernel.cfg
 *
 * -----------------------------------------------------------------------------
 * ! CAMBIA SUBITO, MA NON PER SEMPRE
 *
 * Questo comando parla con /dev/kbd.drv: il cambio vale finche' la
 * macchina resta accesa. Per renderlo permanente va scritto in
 * kernel.cfg, e `keymap -p` stampa la riga esatta da metterci — oppure lo
 * fa `hwconfig`, che quel file lo scrive tutto.
 *
 * La divisione e' voluta. Chi si accorge di avere la disposizione
 * sbagliata se ne accorge DIGITANDO, e in quel momento ha una tastiera
 * che scrive i caratteri sbagliati: deve poter rimediare con un comando
 * corto, non aprendo un editor su un file di configurazione.
 *
 * -----------------------------------------------------------------------------
 * ! LA VIA D'USCITA E' IL RIAVVIO, NON UN COMANDO
 *
 * Avevo scritto qui che `keymap it` si puo' digitare con qualunque
 * disposizione. E' falso, e la prova lo ha mostrato subito: fra QWERTY e
 * QWERTY (us, it, es, uk) le lettere non si muovono, ma su AZERTY la `a`
 * e la `m` cambiano posto e su QWERTZ si scambiano `y` e `z`. Da una
 * francese, battendo i tasti dove stanno su una americana, esce
 * `keyq,p` — cioe' proprio nel caso per cui questo comando esiste, il
 * comando non si riesce a scrivere.
 *
 * La via d'uscita c'e' lo stesso, ed e' migliore perche' non richiede di
 * indovinare niente: **il cambio a caldo non e' permanente**. Chi sceglie
 * la disposizione sbagliata riavvia, e torna quella di kernel.cfg. E'
 * una proprieta' del progetto, non un ripiego: e' il motivo per cui
 * questo comando non scrive su nessun file.
 *
 * Chi vuole cambiarla per davvero passa da `hwconfig`, che il file lo
 * scrive tutto e conserva quello che trova.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"

static int chiedi(unsigned int tipo, const void *payload, unsigned int len,
                  KbdMapInfo *info)
{
    int           pid = ipc_lookup(KBD_SERVICE_NAME);
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           i;

    if (pid <= 0) {
        printf("keymap: il servizio '%s' non e' attivo.\n", KBD_SERVICE_NAME);
        printf("        La tastiera la sta servendo il ripiego interno al\n");
        printf("        kernel, che ha solo la disposizione americana.\n");
        printf("        Si accende dichiarandolo in /boot/kernel.cfg:\n\n");
        printf("            [modules]\n            kbd = /dev/kbd.drv\n");
        return -1;
    }

    if (ipc_send(pid, tipo, payload, len) < 0) {
        printf("keymap: il servizio non accetta messaggi\n");
        return -1;
    }

    for (i = 0; i < 8; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) {
            printf("keymap: nessuna risposta dal servizio\n");
            return -1;
        }
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo != KBD_MSG_MAPINFO) continue;
        if (meta.len < sizeof(*info)) return -1;

        memcpy(info, buf, sizeof(*info));
        return 0;
    }
    return -1;
}

static void uso(void)
{
    printf("uso: keymap [nome | -p]\n\n");
    printf("  keymap        mostra quella attiva e l'elenco\n");
    printf("  keymap it     passa a quella italiana, subito\n");
    printf("  keymap -p     stampa la riga per /boot/kernel.cfg\n\n");
    printf("Il cambio vale finche' la macchina resta accesa. Per renderlo\n");
    printf("permanente serve la riga in kernel.cfg — o `hwconfig`, che\n");
    printf("quel file lo scrive tutto.\n\n");
    printf("Se sbagli disposizione e non riesci piu' a scrivere il comando\n");
    printf("per tornare indietro: RIAVVIA. Il cambio non e' permanente, e\n");
    printf("all'accensione torna quella di kernel.cfg.\n");
}

int main(int argc, char **argv)
{
    KbdMapInfo info;

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 ||
                      strcmp(argv[1], "-help") == 0 ||
                      strcmp(argv[1], "--help") == 0)) {
        uso();
        return 0;
    }

    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        if (chiedi(KBD_MSG_GETMAP, NULL, 0, &info) != 0) return 1;
        printf("Da mettere in /boot/kernel.cfg, sezione [kernel]:\n\n");
        printf("    keymap = %s\n", info.attiva);
        return 0;
    }

    if (argc >= 2) {
        KbdSetMap s;

        memset(&s, 0, sizeof(s));
        strncpy(s.nome, argv[1], sizeof(s.nome) - 1);

        if (chiedi(KBD_MSG_SETMAP, &s, sizeof(s), &info) != 0) return 1;

        if (info.esito != 0) {
            printf("keymap: '%s' non la conosco.\n", argv[1]);
            printf("        Ce ne sono %u: %s\n", info.n, info.elenco);
            return 1;
        }
        printf("Disposizione: %s (%s)\n", info.attiva, info.descrizione);
        printf("Vale da adesso. Per renderla permanente: `keymap -p`.\n");
        return 0;
    }

    if (chiedi(KBD_MSG_GETMAP, NULL, 0, &info) != 0) return 1;

    printf("Disposizione attiva: %s (%s)\n\n", info.attiva, info.descrizione);
    printf("Se ne conoscono %u: %s\n\n", info.n, info.elenco);
    printf("  keymap it     per cambiarla adesso\n");
    printf("  keymap -p     per la riga da mettere in kernel.cfg\n\n");
    printf("! Non ci sono i tasti morti: su una tastiera francese o\n");
    printf("   tedesca ^ e \" scrivono se' stessi invece di aspettare la\n");
    printf("   vocale. Le lettere accentate si vedono come le disegna la\n");
    printf("   VGA (code page 437), e con quei byte finiscono nei file.\n");
    return 0;
}

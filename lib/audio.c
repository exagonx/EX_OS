/* =============================================================================
 * lib/audio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La tabella delle schede audio e la sonda del servizio. Il perche' di
 * ognuna delle due sta in lib/include/audio.h.
 * ============================================================================= */

#include "libc.h"
#include "audio.h"
#include "audio_proto.h"

/* =============================================================================
 * ! I NUMERI SONO QUELLI DEL SILICIO, NON QUELLI SULLA SCATOLA. Una «Sound
 * Blaster PCI 128» e' un Ensoniq ES1370 o ES1371 con un adesivo Creative
 * sopra: Creative aveva comprato Ensoniq nel 1998 e ha venduto per anni lo
 * stesso chip con tre nomi diversi. Chi cerca il proprio modello nella
 * colonna di destra lo trova con il nome che gli hanno venduto; chi guarda i
 * numeri trova quello vero.
 * ========================================================================== */
static const AudioScheda g_schede[] = {
    /* --- Ensoniq / Creative: la famiglia «Sound Blaster PCI» ------------- */
    { 0x1274, 0x5000, "Ensoniq ES1370 (Sound Blaster PCI 64)",  "/dev/es1371.drv" },
    { 0x1274, 0x1371, "Ensoniq ES1371 (Sound Blaster PCI 128)", "/dev/es1371.drv" },
    { 0x1274, 0x5880, "Creative CT5880 (Sound Blaster 128 PCI)","/dev/es1371.drv" },
    { 0x1102, 0x8938, "Ectiva EV1938 (Sound Blaster 128)",      "/dev/es1371.drv" },

    /* --- Creative EMU10K1: le Live! e Audigy. Altro silicio, altro driver. */
    { 0x1102, 0x0002, "Creative EMU10K1 (Sound Blaster Live!)",  0 },
    { 0x1102, 0x0004, "Creative CA0102 (Sound Blaster Audigy)",  0 },

    /* --- Controller AC'97: sopra ci sta quasi sempre un codec Realtek ---- */
    { 0x8086, 0x2415, "Intel 82801AA AC'97",                    "/dev/ac97.drv" },
    { 0x8086, 0x2425, "Intel 82801AB AC'97",                    "/dev/ac97.drv" },
    { 0x8086, 0x2445, "Intel 82801BA AC'97",                    "/dev/ac97.drv" },
    { 0x8086, 0x2485, "Intel 82801CA AC'97",                    "/dev/ac97.drv" },
    { 0x8086, 0x24C5, "Intel 82801DB AC'97",                    "/dev/ac97.drv" },
    { 0x8086, 0x24D5, "Intel 82801EB AC'97",                    "/dev/ac97.drv" },
    { 0x8086, 0x266E, "Intel 82801FB AC'97",                    "/dev/ac97.drv" },
    { 0x1106, 0x3058, "VIA VT82C686 AC'97",                     "/dev/ac97.drv" },
    { 0x10DE, 0x01B1, "nVidia nForce AC'97",                    "/dev/ac97.drv" },

    /* --- Controller HD Audio: il posto dove sta un ALC di oggi ----------- */
    { 0x8086, 0x2668, "Intel HD Audio (ICH6)",                  "/dev/hdaudio.drv" },
    { 0x8086, 0x27D8, "Intel HD Audio (ICH7)",                  "/dev/hdaudio.drv" },
    { 0x8086, 0x284B, "Intel HD Audio (ICH8)",                  "/dev/hdaudio.drv" },
    { 0x8086, 0x293E, "Intel HD Audio (ICH9)",                  "/dev/hdaudio.drv" },
    { 0x8086, 0x3B56, "Intel HD Audio (PCH)",                   "/dev/hdaudio.drv" },
    { 0x1002, 0x4383, "ATI/AMD SB600 HD Audio",                 "/dev/hdaudio.drv" },
    { 0x10DE, 0x0371, "nVidia MCP55 HD Audio",                  "/dev/hdaudio.drv" },
};

#define SCHEDE_N ((int)(sizeof(g_schede) / sizeof(g_schede[0])))

const AudioScheda *audio_riconosci(unsigned short venditore,
                                   unsigned short dispositivo)
{
    int i;

    for (i = 0; i < SCHEDE_N; i++) {
        if (g_schede[i].venditore == venditore &&
            g_schede[i].dispositivo == dispositivo) return &g_schede[i];
    }
    return 0;
}

const AudioScheda *audio_scheda(int i)
{
    if (i < 0 || i >= SCHEDE_N) return 0;
    return &g_schede[i];
}

int audio_schede_note(void) { return SCHEDE_N; }

/* =============================================================================
 * L'elenco ISA: quello che si prova, non quello che si chiede
 * ========================================================================== */
static const AudioIsa g_isa[] = {
    { "/dev/sb.drv", "Sound Blaster (1.x, 2.0, Pro, 16, AWE32, AWE64)" },
};

/* ! L'ELENCO ISA HA UNA VOCE SOLA, e non e' una dimenticanza. Provare vuol
 * dire SCRIVERE su porte che potrebbero essere di un'altra scheda: si fa solo
 * dove la sonda e' sicura. Il DSP della Sound Blaster risponde 0xAA a una
 * sequenza che nessun altro chip imita, e i sette indirizzi provati sono
 * quelli che la Creative ha usato e nessun altro. Una Gravis Ultrasound o una
 * Windows Sound System non hanno una sonda cosi', e aggiungerle qui vorrebbe
 * dire scrivere alla cieca su mezzo spazio I/O per trovare una scheda che
 * quasi nessuno ha. */

#define ISA_N ((int)(sizeof(g_isa) / sizeof(g_isa[0])))

const AudioIsa *audio_isa(int i)
{
    if (i < 0 || i >= ISA_N) return 0;
    return &g_isa[i];
}

int audio_isa_quanti(void) { return ISA_N; }

/* =============================================================================
 * audio_richiedi — il servizio c'e', o si dice cosa fare
 * ========================================================================== */
int audio_richiedi(void)
{
    int pid = ipc_lookup(AUDIO_SERVIZIO);

    if (pid > 0) return pid;

    printf("Il servizio audio non e' attivo.\n\n");
    printf("  audio -i     cerca la scheda, prova il driver e lo mette in\n");
    printf("               kernel.cfg perche' riparta da solo al prossimo avvio\n\n");
    return -1;
}

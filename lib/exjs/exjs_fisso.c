/* =============================================================================
 * lib/exjs/exjs_fisso.c — il motore JavaScript quando ce n'e' uno solo
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A 32 bit il navigatore sceglie il motore a macchina accesa: exjs_stub.c
 * carica exjs.so oppure quickjs.so, due librerie condivise con le stesse
 * funzioni. A 64 bit le librerie sono collegate DENTRO il programma (non c'e'
 * ancora il caricatore delle librerie condivise), e due motori con gli stessi
 * nomi non convivono: c'e' ExJs e basta. Queste due funzioni sono cio' che
 * dello stub resta - la scelta non c'e', e lo si dice.
 *
 * Non fa parte della costruzione a 32 bit: la usa tools/costruisci-utente64.sh.
 * ============================================================================= */
void exjs_motore(int quickjs) { (void)quickjs; }    /* si chiede, non si ottiene */
int  exjs_motore_ora(void)    { return 0; }         /* 0 = ExJs */

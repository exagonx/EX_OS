/* =============================================================================
 * tools/iso/prova-cpp1.cpp
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il programma piu' piccolo che dica qualcosa a cc1plus, da compilare
 * DENTRO EX-OS:
 *
 *     cc1plus /prova-cpp1.cpp -O2 -o /prova.s
 *
 * ! NESSUN #include, E NON E' PIGRIZIA — e' la stessa regola di
 * tools/iso/prova-cc1.c. Un solo `#include <vector>` farebbe due domande
 * insieme: «cc1plus funziona?» e «trova gli header di libstdc++?». La
 * seconda oggi ha risposta NO — sul CD ci sono gli header della libc, non
 * quelli del C++ — ed e' un problema di percorsi, che si affronta dopo e
 * da solo. Mescolarle vorrebbe dire non sapere a quale delle due un
 * fallimento ha detto no.
 *
 * ! NIENTE new, delete, ECCEZIONI NE' DISTRUTTORI VIRTUALI: tirerebbero
 * dentro operator new e la macchina delle eccezioni, cioe' meta'
 * libstdc++. Qui si prova il COMPILATORE, non la libreria.
 *
 * Cosa c'e' dentro, scelto per far lavorare i pezzi che distinguono il C++
 * dal C:
 *
 *   - una funzione VIRTUALE, cioe' la vtable e la chiamata indiretta;
 *   - un TEMPLATE, cioe' l'istanziazione;
 *   - un riferimento e un metodo const.
 *
 * L'uscita e' 42, e si controlla a mano: la derivata risponde 7, il
 * template moltiplica per 6.
 * ============================================================================= */

struct Base {
    virtual int valore() const { return 1; }
};

struct Derivata : Base {
    int valore() const override { return 7; }
};

template <typename T>
static T moltiplica(const T &a, const T &b) { return a * b; }

/* ! `static const` con inizializzazione COSTANTE: il puntatore alla
 * vtable si conosce a tempo di compilazione, quindi non serve nessun
 * costruttore dinamico all'avvio — che vorrebbe dire .init_array e
 * __cxa_atexit, cioe' un'altra domanda ancora. */
static const Derivata d;

int main()
{
    const Base &b = d;
    return moltiplica(b.valore(), 6);
}

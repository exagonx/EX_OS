/* =============================================================================
 * tools/iso/prova-cpp.cpp
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * La libreria standard del C++ dentro EX-OS.
 *
 *     i386-exos-g++ -O2 -o provacpp prova-cpp.cpp
 *
 * ! UNA RIGA SOLA, E CON g++. Fino al 3 agosto 2026 qui c'era scritto il
 * contrario: si compilava con g++ e si collegava con gcc, perche' -lstdc++
 * per i386-exos non esisteva. Adesso esiste, e con lei l'unica cosa che
 * distingue questo bersaglio da uno qualunque e' sparita.
 *
 * -----------------------------------------------------------------------------
 * COSA PROVA, IN ORDINE DI QUANTO E' DIFFICILE
 *
 *   1. template e contenitori   -> li risolve il compilatore, non serve
 *                                  niente a runtime
 *   2. std::string              -> serve l'allocatore: `operator new`,
 *                                  cioe' libsupc++ sopra la nostra malloc
 *   3. new/delete virtuale      -> serve il *deleting destructor*, che
 *                                  chiama `operator delete`
 *   4. le ECCEZIONI             -> ! e' il pezzo piu' grosso: __cxa_throw,
 *                                  lo svolgimento dello stack, le tabelle
 *                                  .eh_frame e i descrittori di tipo. Se
 *                                  questa riga stampa, la meta' bassa di
 *                                  libstdc++ funziona per intero.
 *
 * ! CHE LE ECCEZIONI FUNZIONINO NON ERA SCONTATO. Lo svolgimento dello
 * stack legge le tabelle .eh_frame prodotte dal compilatore e le percorre
 * a runtime: e' l'unica parte del C++ che ha bisogno che il PROGRAMMA
 * CARICATO IN MEMORIA sia esattamente come il collegatore l'ha
 * descritto. Con il caricamento su richiesta di EX-OS (le pagine arrivano
 * al primo accesso) e' anche una prova indiretta che quel meccanismo e'
 * corretto.
 * ============================================================================= */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

/* Ereditarieta' e funzioni virtuali: la vtable la costruisce il
 * compilatore. Il distruttore E' virtuale — fino a ieri non poteva
 * esserlo, perche' il *deleting destructor* che GCC genera chiama
 * `operator delete` e quella stava in una libreria che non c'era. */
struct Forma {
    virtual ~Forma() {}
    virtual const char *nome() const = 0;
    virtual double area() const = 0;
};

struct Cerchio : Forma {
    double r;
    explicit Cerchio(double raggio) : r(raggio) {}
    const char *nome() const override { return "cerchio"; }
    double area() const override { return M_PI * r * r; }
};

struct Quadrato : Forma {
    double l;
    explicit Quadrato(double lato) : l(lato) {}
    const char *nome() const override { return "quadrato"; }
    double area() const override { return l * l; }
};

/* ! I risultati in virgola mobile si moltiplicano per mille e si
 * stampano come interi: la printf di EX-OS non formatta i double. Vedi
 * prova-mat.c. */
static long mille(double v)
{
    return (long)(v * 1000.0 + (v < 0 ? -0.5 : 0.5));
}

int main()
{
    std::printf("La libreria standard del C++ dentro EX-OS\n\n");

    /* 1. Contenitori e algoritmi. */
    std::vector<int> v{5, 3, 9, 1, 7};
    std::sort(v.begin(), v.end());
    std::printf("  vector+sort :");
    for (int x : v) std::printf(" %d", x);
    std::printf("\n");

    /* 2. std::string: qui sotto c'e' `operator new`, cioe' la nostra
     *    malloc vista da libsupc++. */
    std::string s = "std::string ";
    s += "concatenata";
    std::printf("  string      : \"%s\" (%d caratteri)\n",
                s.c_str(), (int)s.size());

    /* 3. Polimorfismo con distruttore virtuale, allocato sullo heap. */
    std::vector<Forma *> forme;
    forme.push_back(new Cerchio(2.0));
    forme.push_back(new Quadrato(3.0));
    for (Forma *f : forme) {
        std::printf("  %-11s : area = %ld (x1000)\n", f->nome(), mille(f->area()));
    }
    for (Forma *f : forme) delete f;    /* deleting destructor virtuale */

    /* 4. Le eccezioni: il pezzo che ha bisogno di piu' cose funzionanti. */
    try {
        throw std::runtime_error("lanciata e ripresa");
    } catch (const std::exception &e) {
        std::printf("  eccezione   : %s\n", e.what());
    }

    /* E una che attraversa piu' livelli di stack, perche' e' li' che lo
     * svolgimento fa il suo lavoro invece di essere un salto. */
    try {
        std::vector<int> vuoto;
        (void)vuoto.at(10);             /* std::out_of_range dal profondo */
        std::printf("  ! at(10) non ha lanciato: SBAGLIATO\n");
    } catch (const std::out_of_range &) {
        std::printf("  out_of_range : presa da dentro la libreria\n");
    }

    std::printf("\nLa libreria standard risponde.\n");
    return 0;
}

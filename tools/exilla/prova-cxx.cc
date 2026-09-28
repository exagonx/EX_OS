// =============================================================================
// tools/exilla/prova-cxx.cc — il C++ di Mozilla, in piccolo, dentro EX-OS
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tappa 0 di Exilla (28 settembre 2026). Gecko e' C++ compilato come lo
// compila Mozilla: -fno-exceptions -fno-rtti. Prima di portarne un solo file
// si guarda che le cose che usa ad OGNI riga funzionino sopra la libc di
// EX-OS, eseguendole, non solo compilandole:
//
//   1. i COSTRUTTORI DEGLI OGGETTI GLOBALI — Gecko ne ha migliaia (i moduli si
//      registrano cosi'). Se l'avvio di EX-OS non li chiama, il programma parte
//      con gli oggetti vuoti e non lo dice nessuno;
//   2. new e delete, e le funzioni virtuali;
//   3. std::string, std::vector, std::map, std::sort (libstdc++.a del bersaglio);
//   4. i distruttori globali all'uscita.
//
// Ogni riga stampa OK o NO; l'ultima dice quante NO.
// =============================================================================
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static int g_no = 0;

static void ok(const char *cosa, bool va)
{
    printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
    if (!va) g_no++;
}

// 1. un oggetto globale col costruttore: deve essere gia' pronto in main()
struct Registro {
    int valore;
    std::string nome;
    Registro() : valore(42), nome("registrato") {}
    ~Registro() { printf("  [OK]  distruttore globale chiamato all'uscita\n"); }
};
static Registro g_registro;

// 2. virtuali
struct Forma { virtual int lati() const = 0; virtual ~Forma() {} };
struct Triangolo : Forma { int lati() const override { return 3; } };
struct Quadrato  : Forma { int lati() const override { return 4; } };

int main()
{
    printf("prova-cxx: il C++ di Exilla dentro EX-OS\n");

    ok("costruttore globale chiamato prima di main",
       g_registro.valore == 42 && g_registro.nome == "registrato");

    {
        std::vector<Forma *> forme;
        forme.push_back(new Triangolo);
        forme.push_back(new Quadrato);
        int somma = 0;
        for (Forma *f : forme) somma += f->lati();
        for (Forma *f : forme) delete f;
        ok("new, delete e funzioni virtuali", somma == 7);
    }

    {
        std::string s = "Exilla";
        s += " su ";
        s += "EX-OS";
        ok("std::string", s == "Exilla su EX-OS" && s.size() == 15);
    }

    {
        std::vector<int> v;
        for (int i = 0; i < 1000; i++) v.push_back((i * 7919) % 1000);
        std::sort(v.begin(), v.end());
        bool ordinato = true;
        for (size_t i = 1; i < v.size(); i++) if (v[i - 1] > v[i]) ordinato = false;
        ok("std::vector e std::sort (mille numeri)", ordinato && v.front() == 0 && v.back() == 999);
    }

    {
        std::map<std::string, int> m;
        m["uno"] = 1; m["due"] = 2; m["tre"] = 3;
        ok("std::map", m.size() == 3 && m["due"] == 2 && m.begin()->first == "due");
    }

    printf("prova-cxx: %d NO\n", g_no);
    return g_no;
}

/* =============================================================================
 * lib/exwin/exwin.hpp
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExWin per C++ — un involucro sottile, e sottile di proposito
 *
 * ! NON C'E' UN SECONDO TOOLKIT QUI DENTRO. Questo file avvolge le stesse
 * funzioni C: nessuna gerarchia di classi, nessun conteggio dei riferimenti,
 * nessun std::. Un involucro che aggiungesse un proprio modello a oggetti
 * sarebbe una seconda cosa da tenere allineata alla prima, e le due
 * divergerebbero — mentre qui, se ex_crea() cambia, questo file non compila
 * piu' e lo si scopre subito.
 *
 * ! E NON SERVONO ECCEZIONI NE' RTTI. Una finestra che non si crea rende una
 * maniglia nulla, come in C: valido() lo dice. Cosi' l'involucro si usa anche
 * dove il runtime C++ e' ridotto all'osso.
 *
 *     #include <exwin.hpp>
 *
 *     static long proc(ExFinestra f, unsigned m, unsigned wp, long lp) { ... }
 *
 *     int main() {
 *         ExWin::Finestra f("Prova", 100, 100, 320, 200, proc);
 *         ExWin::Pulsante ok(f, "OK", 20, 140, 80, 24, ID_OK);
 *         return ExWin::gira();
 *     }
 * ============================================================================= */

#ifndef EXWIN_HPP
#define EXWIN_HPP

#include "exwin.h"

namespace ExWin {

/* La maniglia con un po' di comodita' intorno. Non possiede niente: e' la
 * libreria C a tenere gli oggetti, esattamente come in C. */
class Oggetto {
public:
    Oggetto() : h(0) {}
    explicit Oggetto(ExFinestra f) : h(f) {}

    bool       valido() const { return h != 0; }
    ExFinestra maniglia() const { return h; }
    operator ExFinestra() const { return h; }

    void testo(const char *s)   { ex_testo_metti(h, s); }
    const char *testo() const   { return ex_testo_prendi(h); }
    void mostra(bool v = true)  { ex_mostra(h, v ? 1 : 0); }
    void sposta(int x, int y)   { ex_sposta(h, x, y); }

protected:
    ExFinestra h;
};

class Finestra : public Oggetto {
public:
    Finestra(const char *titolo, int x, int y, int w, int h_,
             ExProcedura proc,
             unsigned int stile = EX_TITOLO | EX_BORDO | EX_CHIUDI)
    {
        h = ex_crea("finestra", titolo, stile, x, y, w, h_, 0, 0, proc);
    }

    ~Finestra() { if (h) ex_distruggi(h); }

    void titolo(const char *s) { ex_titolo(h, s); }
    void aggiorna()            { ex_aggiorna(h); }
    void riempi(int x, int y, int w, int h_, unsigned int c)
                               { ex_riempi(h, x, y, w, h_, c); }
    void scrivi(int x, int y, const char *s, unsigned int c)
                               { ex_scrivi(h, x, y, s, c); }
    bool immagine(const char *percorso, int x = 0, int y = 0)
                               { return ex_immagine(h, percorso, x, y) != 0; }

private:
    /* ! NON SI COPIA. Due Finestra con la stessa maniglia vorrebbero dire due
     * distruttori sulla stessa finestra: la seconda ex_distruggi() lavorerebbe
     * su una maniglia gia' libera. */
    Finestra(const Finestra &);
    Finestra &operator=(const Finestra &);
};

/* I controlli. Sono tutti la stessa chiamata con una classe diversa, ed e'
 * il motivo per cui aggiungerne uno non tocca questo file piu' di una riga. */
#define EXWIN_CONTROLLO(Nome, classe)                                         \
    class Nome : public Oggetto {                                             \
    public:                                                                   \
        Nome(ExFinestra padre, const char *titolo, int x, int y,              \
             int w, int h_, unsigned int id = 0)                              \
        { h = ex_crea(classe, titolo, EX_FIGLIO, x, y, w, h_, padre, id, 0); } \
    }

EXWIN_CONTROLLO(Pulsante,     "pulsante");
EXWIN_CONTROLLO(Etichetta,    "etichetta");
EXWIN_CONTROLLO(Testo,        "testo");
EXWIN_CONTROLLO(Riquadro,     "riquadro");
EXWIN_CONTROLLO(Separatore,   "separatore");
EXWIN_CONTROLLO(Intestazione, "intestazione");

#undef EXWIN_CONTROLLO

/* Il ciclo dei messaggi, come in C ma in una riga. */
inline int gira()
{
    ExMsg m;
    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

inline void esci(int codice = 0) { ex_esci(codice); }

} /* namespace ExWin */

#endif /* EXWIN_HPP */

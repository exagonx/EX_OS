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
 * divergerebbero — mentre qui, se ex_create() cambia, questo file non compila
 * piu' e lo si scopre subito.
 *
 * ! E NON SERVONO ECCEZIONI NE' RTTI. Una finestra che non si crea rende una
 * maniglia nulla, come in C: valido() lo dice. Cosi' l'involucro si usa anche
 * dove il runtime C++ e' ridotto all'osso.
 *
 *     #include <exwin.hpp>
 *
 *     static long proc(ExWindow f, unsigned m, unsigned wp, long lp) { ... }
 *
 *     int main() {
 *         ExWin::Window f("Prova", 100, 100, 320, 200, proc);
 *         ExWin::Button ok(f, "OK", 20, 140, 80, 24, ID_OK);
 *         return ExWin::run();
 *     }
 * ============================================================================= */

#ifndef EXWIN_HPP
#define EXWIN_HPP

#include "exwin.h"

namespace ExWin {

/* The handle with a little convenience around it. It owns nothing: the C
 * library keeps the objects, exactly as in C. */
class Object {
public:
    Object() : h(0) {}
    explicit Object(ExWindow f) : h(f) {}

    bool        valid() const  { return h != 0; }
    ExWindow    handle() const { return h; }
    operator ExWindow() const  { return h; }

    void        set_text(const char *s) { ex_set_text(h, s); }
    const char *text() const            { return ex_get_text(h); }
    void        show(bool v = true)     { ex_show(h, v ? 1 : 0); }
    void        move(int x, int y)      { ex_move(h, x, y); }
    /* h_ and not h: the member is already called that, and is the handle. */
    void        resize(int w, int h_)   { ex_resize(h, w, h_); }
    void        draw_raised(int x, int y, int w, int h_) { ex_draw_raised(h, x, y, w, h_); }
    void        draw_sunken(int x, int y, int w, int h_) { ex_draw_sunken(h, x, y, w, h_); }

    /* The Italian names of before 3 October 2026, deprecated. */
    bool        valido() const           { return valid(); }
    ExWindow    maniglia() const         { return h; }
    void        testo(const char *s)     { set_text(s); }
    const char *testo() const            { return text(); }
    void        mostra(bool v = true)    { show(v); }
    void        sposta(int x, int y)     { move(x, y); }
    void        misura(int w, int h_)    { resize(w, h_); }
    void        rilievo(int x, int y, int w, int h_) { draw_raised(x, y, w, h_); }
    void        incavo(int x, int y, int w, int h_)  { draw_sunken(x, y, w, h_); }

protected:
    ExWindow h;
};

class Window : public Object {
public:
    Window(const char *title, int x, int y, int w, int h_,
           ExWindowProc proc,
           unsigned int style = EX_CAPTION | EX_BORDER | EX_CLOSEBOX)
    {
        h = ex_create("window", title, style, x, y, w, h_, 0, 0, proc);
    }

    ~Window() { if (h) ex_destroy(h); }

    void set_title(const char *s) { ex_set_title(h, s); }
    void update()                 { ex_update(h); }
    void fill_rect(int x, int y, int w, int h_, unsigned int c)
                                  { ex_fill_rect(h, x, y, w, h_, c); }
    void draw_text(int x, int y, const char *s, unsigned int c)
                                  { ex_draw_text(h, x, y, s, c); }
    bool draw_image(const char *path, int x = 0, int y = 0)
                                  { return ex_draw_image(h, path, x, y) != 0; }

    /* The Italian names, deprecated. */
    void titolo(const char *s)    { set_title(s); }
    void aggiorna()               { update(); }
    void riempi(int x, int y, int w, int h_, unsigned int c) { fill_rect(x, y, w, h_, c); }
    void scrivi(int x, int y, const char *s, unsigned int c) { draw_text(x, y, s, c); }
    bool immagine(const char *p, int x = 0, int y = 0)       { return draw_image(p, x, y); }

private:
    /* ! NOT COPYABLE. Two Window objects with the same handle would mean two
     * destructors on the same window: the second ex_destroy() would work on
     * a handle already freed. */
    Window(const Window &);
    Window &operator=(const Window &);
};

/* The controls. They are all the same call with a different class, which is
 * why adding one does not touch this file by more than a line. */
#define EXWIN_CONTROL(Name, cls)                                              \
    class Name : public Object {                                              \
    public:                                                                   \
        Name(ExWindow parent, const char *title, int x, int y,                \
             int w, int h_, unsigned int id = 0)                              \
        { h = ex_create(cls, title, EX_CHILD, x, y, w, h_, parent, id, 0); } \
    }

EXWIN_CONTROL(Button,    "button");
EXWIN_CONTROL(Label,     "label");
EXWIN_CONTROL(TextBox,   "textbox");
EXWIN_CONTROL(Frame,     "frame");
EXWIN_CONTROL(Separator, "separator");
EXWIN_CONTROL(Header,    "header");

#undef EXWIN_CONTROL

/* The message loop, as in C but in one line. */
inline int run()
{
    ExMsg m;
    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}

inline void quit(int code = 0) { ex_quit(code); }

/* The Italian names of before 3 October 2026, deprecated. */
typedef Object    Oggetto;
typedef Window    Finestra;
typedef Button    Pulsante;
typedef Label     Etichetta;
typedef TextBox   Testo;
typedef Frame     Riquadro;
typedef Separator Separatore;
typedef Header    Intestazione;
inline int  gira()               { return run(); }
inline void esci(int codice = 0) { quit(codice); }

} /* namespace ExWin */

#endif /* EXWIN_HPP */

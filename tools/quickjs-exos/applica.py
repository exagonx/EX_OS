#!/usr/bin/env python3
"""Adatta un albero dei sorgenti QuickJS a EX-OS.

    python3 tools/quickjs-exos/applica.py quickjs
    python3 tools/quickjs-exos/applica.py quickjs --togli

PERCHE' UNO SCRIPT E NON UNA PATCH, e perche' le modifiche non stanno
dentro quickjs/. L'albero di QuickJS non e' in questo repository (vedi
.gitignore: sono cinque megabyte di codice di terzi che cambiano a monte
con i loro tempi), quindi cio' che ne cambiamo va conservato a parte. Una
patch a contesto scade in giorni — basta che qualcuno tocchi una riga
vicina; una sostituzione di stringhe esatte sopravvive a tutto cio' che
non tocca proprio quelle righe, e quando invece le tocca lo DICE invece
di applicarsi a meta'. E' la stessa scelta, e lo stesso file, di
tools/gcc-exos/applica.py.

E' IDEMPOTENTE: rilanciarlo su un albero gia' adattato non fa danni e
riporta "gia' presente" per ogni pezzo.

--- QUANTO POCO SERVE, ED E' LA NOTIZIA -------------------------------

Cinque modifiche — quattro in cutils.h e una in quickjs.c — tutte dello
stesso genere: dire a QuickJS che questo sistema non e' Linux nelle cinque
cose in cui non lo e'. Il motore vero — quickjs.c, libregexp.c, libunicode.c, dtoa.c —
compila per EX-OS **senza toccare una riga**. Non era scontato e vale la
pena scriverlo: chi riprende questo lavoro deve sapere che il grosso non
e' far compilare QuickJS, e' l'adattatore verso l'interfaccia di exjs.h.

--- LICENZE ------------------------------------------------------------

QuickJS e quickjs-ng sono distribuiti sotto licenza MIT (Fabrice Bellard,
Charlie Gordon, Ben Noordhuis, Saul Ibarra Corretge). La MIT non chiede
di marcare i file modificati, ma questo script lo fa lo stesso: chi
guarda un albero adattato deve sapere in dieci secondi che non e' quello
di upstream, e dove sta scritto perche'.

EX-OS di suo e' GPL-2.0-or-later. La MIT e' compatibile: il risultato
combinato si distribuisce sotto GPL, e i file di QuickJS restano MIT con
il loro copyright — che percio' NON si tocca.

Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
SPDX-License-Identifier: GPL-2.0-or-later
"""

import os
import sys
import time

QUI = os.path.dirname(os.path.abspath(__file__))

# Ogni voce: (file relativo all'albero, testo originale, testo nuovo).
#
# Il testo originale e' abbastanza lungo da essere unico nel file: una
# stringa corta rischierebbe di combaciare dove non c'entra, e in un
# groviglio di #if una sostituzione nel posto sbagliato non da' un errore
# di sintassi — da' un motore che compila e si comporta da un'altra parte.
MODIFICHE = [
    # =========================================================================
    # 1. pthread.h — EX-OS non ce l'ha, e non serve
    #
    # ! I THREAD DI QUICKJS SERVONO A Atomics.wait E AI WORKER, cioe' a cose
    # che un browser di EX-OS non ha e non avra' presto. cutils.h e' gia'
    # preparato a farne a meno (vedi JS_HAVE_THREADS qui sotto): l'unica cosa
    # che manca e' non includere l'header di una libreria che non esiste.
    #
    # ! E errno.h RESTA INCLUSO, che e' il motivo per cui la guardia sta
    # attorno alla riga e non attorno al blocco: quello ce l'abbiamo, e
    # QuickJS lo usa davvero.
    # =========================================================================
    (
        "cutils.h",
        "#if !defined(_WIN32) && !defined(EMSCRIPTEN) && !defined(__wasi__) && !defined(__DJGPP)\n"
        "#include <errno.h>\n"
        "#include <pthread.h>\n"
        "#endif",

        "#if !defined(_WIN32) && !defined(EMSCRIPTEN) && !defined(__wasi__) && !defined(__DJGPP)\n"
        "#include <errno.h>\n"
        "#if !defined(__EXOS__)   /* EX-OS non ha pthread: vedi JS_HAVE_THREADS */\n"
        "#include <pthread.h>\n"
        "#endif\n"
        "#endif",
    ),

    # =========================================================================
    # 2. Atomics — non esiste senza thread
    #
    # ! CONFIG_ATOMICS NON SI SPEGNE DA SOLO QUANDO I THREAD NON CI SONO, e il
    # sintomo e' quello di un motore che non compila per un motivo che sembra
    # un altro: `unknown type name 'js_cond_t'` a pagina 62607, cioe' dentro
    # Atomics.wait, che una variabile di condizione ce l'ha per forza.
    #
    # ! E LA LISTA ESISTE GIA', con dentro DJGPP, WASI ed Emscripten per la
    # STESSA ragione. Ci si aggiunge EX-OS invece di inventare un
    # interruttore nuovo: `Atomics` senza thread e senza SharedArrayBuffer
    # non e' una funzione a meta', e' una funzione che non ha senso.
    # =========================================================================
    (
        "quickjs.c",
        "#if !defined(__TINYC__) && !defined(EMSCRIPTEN) && !defined(__wasi__) "
        "&& !__STDC_NO_ATOMICS__ && !defined(__DJGPP)",

        "#if !defined(__TINYC__) && !defined(EMSCRIPTEN) && !defined(__wasi__) "
        "&& !__STDC_NO_ATOMICS__ && !defined(__DJGPP) && !defined(__EXOS__)",
    ),

    # =========================================================================
    # 3. JS_HAVE_THREADS a zero
    #
    # E' l'interruttore che QuickJS mette a disposizione apposta: con zero,
    # tutto il blocco js_mutex_/js_cond_/js_thread_ non viene nemmeno
    # compilato. Si aggiunge EX-OS all'elenco di chi non ha thread, accanto a
    # Emscripten, WASI e DJGPP.
    # =========================================================================
    (
        "cutils.h",
        "#if defined(EMSCRIPTEN) || defined(__wasi__) || defined(__DJGPP)\n"
        "\n"
        "#define JS_HAVE_THREADS 0",

        "#if defined(EMSCRIPTEN) || defined(__wasi__) || defined(__DJGPP) || defined(__EXOS__)\n"
        "\n"
        "#define JS_HAVE_THREADS 0",
    ),

    # =========================================================================
    # 4. L'orologio monotono: gettimeofday, non clock_gettime
    #
    # ! LA LIBC DI EX-OS HA gettimeofday E NON clock_gettime, ed e' una scelta
    # dichiarata: il tempo lo tiene il PIT a 10 ms, e una funzione che
    # promette i nanosecondi darebbe una precisione che non c'e'. QuickJS ha
    # gia' esattamente questo ramo per DJGPP — si aggiunge EX-OS a quello
    # invece di scriverne uno nuovo.
    # =========================================================================
    (
        "cutils.h",
        "static inline uint64_t js__hrtime_ns(void) {\n"
        "#ifdef __DJGPP\n",

        "static inline uint64_t js__hrtime_ns(void) {\n"
        "#if defined(__DJGPP) || defined(__EXOS__)\n",
    ),

    # =========================================================================
    # 5. js_exepath — qui non c'e' /proc
    #
    # Il ramo Linux legge /proc/self/exe con readlink. EX-OS non ha ne' l'uno
    # ne' l'altro, e il -1 e' gia' previsto da QuickJS come risposta legittima
    # (lo usa per trovare i propri moduli, che qui non ci sono).
    # =========================================================================
    (
        "cutils.h",
        "#elif defined(__linux__) || defined(__GNU__)\n"
        "static inline int js_exepath(char *buffer, size_t *size) {\n"
        "    ssize_t n;",

        "#elif defined(__EXOS__)\n"
        "/* EX-OS non ha /proc: il -1 e' una risposta prevista. */\n"
        "static inline int js_exepath(char *buffer, size_t *size) {\n"
        "    (void)buffer; (void)size;\n"
        "    return -1;\n"
        "}\n"
        "#elif defined(__linux__) || defined(__GNU__)\n"
        "static inline int js_exepath(char *buffer, size_t *size) {\n"
        "    ssize_t n;",
    ),
]


def marca_modifica(percorso):
    """Una riga in TESTA che dice che il file non e' quello di upstream.

    La MIT non lo impone; lo impone il buon senso di chi tornera' qui fra sei
    mesi con un albero aggiornato e dovra' capire in dieci secondi cosa
    guarda. In coda a duemila righe non la vedrebbe nessuno."""
    testo = open(percorso, encoding="utf-8", errors="surrogateescape").read()
    if "Modificato per EX-OS" in testo:
        return

    riga = ("/* Modificato per EX-OS il %s: le modifiche sono descritte in\n"
            "   tools/quickjs-exos/applica.py del progetto EX-OS.  Il copyright\n"
            "   e la licenza MIT di questo file restano quelli di sopra.  */\n"
            % time.strftime("%Y-%m-%d"))

    righe = testo.splitlines(keepends=True)
    righe.insert(0, riga)
    open(percorso, "w", encoding="utf-8", errors="surrogateescape").writelines(righe)


def applica(albero):
    problemi = 0

    for rel, vecchio, nuovo in MODIFICHE:
        percorso = os.path.join(albero, rel)
        if not os.path.isfile(percorso):
            print("  [ASSENTE] %s" % rel)
            problemi += 1
            continue

        testo = open(percorso, encoding="utf-8", errors="surrogateescape").read()

        if nuovo in testo:
            print("  [gia' presente] %s" % rel)
            continue

        if testo.count(vecchio) != 1:
            print("  [NON APPLICABILE] %s: il testo di riferimento compare %d "
                  "volte invece di una" % (rel, testo.count(vecchio)))
            print("     upstream ha cambiato quelle righe: va aggiornato "
                  "tools/quickjs-exos/applica.py")
            problemi += 1
            continue

        open(percorso, "w", encoding="utf-8", errors="surrogateescape").write(
            testo.replace(vecchio, nuovo, 1))
        marca_modifica(percorso)
        print("  [applicato] %s" % rel)

    return problemi


def togli(albero):
    """Riporta l'albero com'era: serve a rigenerare le modifiche da capo dopo
    un aggiornamento di upstream, senza riscaricare niente."""
    for rel, vecchio, nuovo in MODIFICHE:
        percorso = os.path.join(albero, rel)
        if not os.path.isfile(percorso):
            continue
        testo = open(percorso, encoding="utf-8", errors="surrogateescape").read()
        if nuovo not in testo:
            print("  [non c'era] %s" % rel)
            continue
        testo = testo.replace(nuovo, vecchio, 1)
        righe = [r for r in testo.splitlines(keepends=True)
                 if "Modificato per EX-OS" not in r
                 and "tools/quickjs-exos/applica.py" not in r
                 and "e la licenza MIT di questo file restano" not in r]
        open(percorso, "w", encoding="utf-8", errors="surrogateescape").writelines(righe)
        print("  [tolto] %s" % rel)
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    albero = sys.argv[1]
    smonta = "--togli" in sys.argv[2:]

    if not os.path.isfile(os.path.join(albero, "quickjs.c")):
        print("applica: '%s' non sembra un albero di QuickJS "
              "(manca quickjs.c)" % albero)
        return 1

    print("%s l'adattamento a EX-OS in %s"
          % ("Tolgo" if smonta else "Applico", albero))
    problemi = togli(albero) if smonta else applica(albero)

    if problemi:
        print("\n%d pezzi non applicati: l'albero NON e' pronto." % problemi)
        return 1

    print("\nFatto. Adesso si compila con -D__EXOS__ ; vedi il leggimi.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

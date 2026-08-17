/* =============================================================================
 * lib/libc_avvio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'AVVIO DI UN PROGRAMMA — l'unico pezzo di libc che NON puo' essere condiviso
 *
 * ! PERCHE' STA IN UN FILE SUO, dal 17 agosto 2026. Queste tre funzioni
 * toccano vettori che appartengono al BINARIO in cui si trovano:
 *
 *     main                 lo definisce il programma
 *     __init_array_*       i costruttori globali DEL PROGRAMMA
 *     __fini_array_*       i suoi distruttori
 *
 * Dentro una libc condivisa `main` non esisterebbe nemmeno (il collegamento
 * fallirebbe), e i vettori sarebbero quelli della LIBRERIA — vuoti. I
 * costruttori degli oggetti globali del programma non girerebbero mai, e
 * nessuno lo direbbe: un programma C++ partirebbe con gli oggetti globali non
 * costruiti.
 *
 * ! SI USA IN TRE MODI, MA E' SCRITTO UNA VOLTA SOLA:
 *
 *     lib/libc.c lo INCLUDE in fondo   -> i programmi con la libc statica non
 *                                         cambiano di una virgola
 *     libc.so lo esclude               -> con -DEXOS_LIBC_SO
 *     lo stub della libc condivisa     -> lo compila per conto suo
 *
 * Includere un .c e' insolito, e la ragione e' precisa: la libc viene
 * compilata da una ventina di regole diverse del Makefile, e una definizione
 * SEPARATA avrebbe voluto dire aggiungere un oggetto a tutte e venti. Una
 * dimenticata sarebbe stata un programma senza costruttori globali — di nuovo
 * in silenzio.
 * ============================================================================= */

/* =============================================================================
 * _libc_start — chiamata da lib/start.S dopo aver letto argc/argv
 *               dallo stack iniziale costruito da sys_spawn.
 *
 * I programmi che usano la libc implementano main(int argc, char **argv).
 * lib/start.S fornisce _start (weak, assembly puro) che legge argc/argv
 * da [esp+4]/[esp+8] prima di qualunque prolog GCC, poi chiama qui.
 *
 * libc.c non include libc.h (è self-contained): dichiariamo main() qui.
 * Da GCC 14 la chiamata implicita è un errore, non più un warning.
 *
 * ! TRE ARGOMENTI, E IL TERZO E' COSTATO UN PAGE FAULT A 0x00000000.
 *
 * La terza forma di main() — `main(argc, argv, envp)` — non e' nello
 * standard C ma esiste su ogni Unix, e il codice di terzi la usa. GNU make
 * la usa: la PRIMA cosa che fa dopo l'avvio e' scorrere `envp` per portarsi
 * dentro le variabili d'ambiente.
 *
 * Fino ad agosto 2026 qui c'era `main(argc, argv)` e la chiamata passava due
 * argomenti. Un main dichiarato con tre ne trovava due sullo stack e per il
 * terzo leggeva quello che c'era: `make --version` moriva su
 *
 *     [FAULT] page fault a 0x00000000 (protezione, lettura, EIP=0x08000450)
 *
 * cioe' alla seconda istruzione del suo ciclo sull'ambiente, prima di aver
 * stampato una riga.
 *
 * ! E NON ROMPE CHI NE DICHIARA DUE. La convenzione di chiamata di i386 e'
 * cdecl: gli argomenti li mette e li toglie il CHIAMANTE, quindi passarne
 * uno in piu' a una funzione che ne legge due e' esattamente cio' che fa
 * ogni Unix — la libc non sa quale delle due forme il programma abbia
 * scritto, e non ha bisogno di saperlo.
 * ============================================================================= */
int main(int argc, char **argv, char **envp);

/* ! DICHIARATO QUI E NON INCLUDENDO libc.h. Questo file si compila in due
 * contesti: dentro libc.c (che la libc se la dichiara da se') e da solo, nello
 * stub della libc condivisa. Tirare dentro libc.h nel primo caso vorrebbe dire
 * far scontrare due dichiarazioni della stessa cosa. */
void     exit(int codice);
char  ***__environ_dove(void);
void     __libc_distruttori_registra(void (*f)(void));

/* =============================================================================
 * ! IL GANCIO CHE AGGANCIA LA libc CONDIVISA, e perche' e' weak.
 *
 * Con la libc collegata staticamente non c'e' niente da agganciare, e questa
 * versione — vuota — e' quella che vince. Un programma collegato alla libc
 * CONDIVISA porta invece lo stub, che ne definisce una forte: quella risolve i
 * 311 nomi prima che venga chiamata qualunque altra cosa.
 *
 * ! DEV'ESSERE LA PRIMA RIGA DI _libc_start, prima perfino di toccare environ:
 * ogni funzione della libc, in modo condiviso, e' un salto attraverso un
 * puntatore che fino a qui vale zero.
 * ============================================================================= */
__attribute__((weak)) void __libc_ponti_avvia(void) { }

/* =============================================================================
 * ! I COSTRUTTORI GLOBALI, che fino ad agosto 2026 NON VENIVANO CHIAMATI
 *
 * In C++ un oggetto dichiarato a livello di file ha un costruttore che
 * deve girare PRIMA di main(). Il compilatore mette il puntatore a quella
 * funzione nella sezione `.init_array`, e sta al codice di avvio
 * percorrerla. La nostra non lo faceva: ogni oggetto globale di ogni
 * programma C++ restava con i byte che si trovava.
 *
 * NON SI VEDEVA, ed e' il motivo per cui e' rimasto li' tanto: i
 * programmi di EX-OS sono in C e non hanno costruttori globali, e la
 * prova di libstdc++ (bin/iso/prova-cpp.cpp) costruisce i propri oggetti
 * DENTRO main. Il primo programma vero a inciamparci e' stato cc1, che
 * ne ha 57:
 *
 *     [ 4] .init_array  INIT_ARRAY  0a1be000  0000e4  (228 byte = 57 voci)
 *
 * Fra quei 57 c'e' `static object_allocator<et_occ> et_occurrences`, il
 * pool da cui la foresta ET prende i nodi. Mai costruito, allocate()
 * restituiva NULL, e cc1 moriva con un fault a 0x00000004 dentro
 * et_splay() — cioe' `occ->parent` su un puntatore nullo. Il guasto
 * sembrava un difetto di GCC ed era il nostro codice di avvio.
 *
 * ! I SIMBOLI SONO `weak` PERCHE' POSSONO NON ESISTERE. Li definisce il
 * linker script, e i nostri (bin/<prog>/<prog>.ld) non lo fanno — non
 * serve, quei programmi non hanno costruttori. Un simbolo weak non
 * definito vale zero, i due estremi coincidono e il ciclo non gira
 * nemmeno una volta. Dichiararli forti farebbe fallire il link di ogni
 * programma di EX-OS.
 *
 * ! L'ORDINE E' QUELLO DELL'ARRAY, IN AVANTI. Per .init_array e'
 * l'ordine giusto; per .fini_array la specifica dice ALL'INDIETRO, e
 * distruggere nell'ordine di costruzione invece che al contrario
 * significa distruggere un oggetto mentre un altro, costruito dopo, lo
 * sta ancora usando.
 *
 * ! .preinit_array PRIMA DI TUTTO. Ci finiscono le funzioni che devono
 * girare prima di qualunque costruttore — le usa il codice di
 * strumentazione. Sono quasi sempre zero, e costano tre righe.
 * ============================================================================= */
/* ! QUESTO BLOCCO STA PRIMA DI __attribute__((weak, noreturn)), e non e'
 * indifferente: quell'attributo appartiene a _libc_start, ed e' scritto
 * sulla riga PRECEDENTE alla funzione. Infilando del codice in mezzo —
 * come ho fatto alla prima stesura — l'attributo si attacca alla prima
 * dichiarazione che trova (il typedef qui sotto, con tanto di avviso
 * «weak attribute ignored») e _libc_start perde sia weak sia noreturn,
 * in silenzio. */
typedef void (*FunzioneInit)(void);

extern FunzioneInit __preinit_array_start[] __attribute__((weak));
extern FunzioneInit __preinit_array_end[]   __attribute__((weak));
extern FunzioneInit __init_array_start[]    __attribute__((weak));
extern FunzioneInit __init_array_end[]      __attribute__((weak));
extern FunzioneInit __fini_array_start[]    __attribute__((weak));
extern FunzioneInit __fini_array_end[]      __attribute__((weak));

static void esegui_costruttori(void)
{
    FunzioneInit *p;

    for (p = __preinit_array_start; p != __preinit_array_end; p++)
        if (*p) (*p)();

    for (p = __init_array_start; p != __init_array_end; p++)
        if (*p) (*p)();
}

/* Chiamata da exit(). Vedi il commento sopra: all'indietro, non in
 * avanti. */
void _libc_distruttori(void)
{
    FunzioneInit *p;

    /* &a[0] e non a: confrontare due array direttamente e' un confronto
     * fra indirizzi che il compilatore segnala, perche' quasi sempre chi
     * lo scrive voleva confrontare il CONTENUTO. Qui gli indirizzi sono
     * proprio quello che serve. */
    if (&__fini_array_start[0] == &__fini_array_end[0]) return;

    for (p = __fini_array_end; p != __fini_array_start; ) {
        p--;
        if (*p) (*p)();
    }
}

__attribute__((weak, noreturn))
void _libc_start(int argc, char **argv, char **envp)
{
    /* ! PRIMA DI TUTTO IL RESTO. Con la libc condivisa, da qui in poi ogni
     * chiamata passa per un puntatore che prima di questa riga vale zero.
     * Staticamente non fa niente. */
    __libc_ponti_avvia();

    /* ! E SUBITO DOPO, CHI DISTRUGGERA' GLI OGGETTI GLOBALI. exit() sta nella
     * libreria e non puo' trovare il __fini_array di questo programma: glielo
     * si dice. Staticamente e' la stessa funzione, e registrarla non cambia
     * niente. */
    __libc_distruttori_registra(_libc_distruttori);

    /* L'ambiente del processo e' quello che il padre ha passato. Puo'
     * essere vuoto — succede al primo processo, che lo spawn lo fa il
     * kernel — e in quel caso getenv() ripiega sulla sezione [env] di
     * /boot/kernel.cfg: vedi getenv(). */
    *__environ_dove() = envp;

    /* ! DOPO environ E PRIMA DI main. Un costruttore globale puo'
     * chiamare getenv(); farlo girare prima che environ sia impostato
     * gli darebbe un ambiente vuoto invece di quello vero. */
    esegui_costruttori();

    exit(main(argc, argv, envp));
    for (;;);
}

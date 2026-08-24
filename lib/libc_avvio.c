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

/* La versione dello strumento: la dichiara EX_VERSIONE() in libc.h. Deboli
 * apposta — vedi il blocco su `-v` piu' sotto. */
extern const char *const __ex_nome     __attribute__((weak));
extern const char *const __ex_versione __attribute__((weak));

/* strcmp a mano: qui non si puo' ancora chiamare la libc. Rende 1 se uguali. */
static int confronta(const char *a, const char *b)
{
    unsigned int i = 0;

    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/* write(2) diretta: qui non si puo' ancora chiamare la libc. */
static void scrivi_versione(const char *s)
{
    unsigned int n = 0;

    if (s == 0) return;
    while (s[n]) n++;
    __asm__ volatile ("int $0x80" :: "a"(4), "b"(1), "c"(s), "d"(n)
                      : "memory");
}

/* ! DICHIARATO QUI E NON INCLUDENDO libc.h. Questo file si compila in due
 * contesti: dentro libc.c (che la libc se la dichiara da se') e da solo, nello
 * stub della libc condivisa. Tirare dentro libc.h nel primo caso vorrebbe dire
 * far scontrare due dichiarazioni della stessa cosa. */
void     exit(int codice);
char  ***__environ_dove(void);
void     __libc_distruttori_registra(void (*f)(void));
void     __libc_canarino_avvia(void);
unsigned int uptime_ms(void);
int      getpid(void);

/* =============================================================================
 * IL CANARINO DELLO STACK — e perche' sta QUI e non nella libc condivisa
 *
 * ! __stack_chk_guard E' UNA VARIABILE, E LE VARIABILI NON PASSANO DAI PONTI.
 * E' la stessa trappola di errno: il compilatore la nomina direttamente in
 * ogni funzione protetta, e se stesse nella libreria condivisa ogni programma
 * ne avrebbe una PROPRIA, non inizializzata — cioe' un canarino con un valore
 * costante scritto nel binario, che chi riscrive lo stack rimette a posto.
 *
 * Il canarino appartiene al PROGRAMMA, come `main` e i costruttori globali. Sta
 * in questo file per la stessa ragione per cui ci sta _libc_start.
 *
 * ! SERVE PERCHE' SU QUESTO HARDWARE NON C'E' IL BIT NX, e non ci sara'
 * nemmeno col Pentium MMX: il non-execute arriva con PAE e in pratica solo dai
 * Pentium 4 in poi. Una pagina di dati resta eseguibile, quindi uno stack
 * riscritto e' codice che parte. Il canarino non impedisce l'overflow: lo fa
 * ACCORGERE, e costa qualche istruzione per funzione senza chiedere niente al
 * processore.
 *
 * ! IL VALORE NON E' UNA COSTANTE. Si compone all'avvio da cio' che il sistema
 * ha di variabile — i millisecondi dall'accensione e il PID. Non e' casualita'
 * crittografica: su questa macchina non c'e' una sorgente di entropia, ed e'
 * scritto qui perche' non lo si creda.
 *
 * ! E IL BYTE BASSO E' ZERO, di proposito: e' la convenzione di ogni
 * implementazione, e ferma gli overflow fatti con le funzioni di stringa —
 * una strcpy() si arresta sullo zero e non riesce a ricopiare il canarino
 * oltre di se'.
 * ============================================================================= */
unsigned long __stack_chk_guard = 0x00AA55C3ul;

void __libc_canarino_avvia(void)
{
    unsigned long v = (unsigned long)uptime_ms();

    v ^= ((unsigned long)getpid() << 16);
    v  = (v << 8) ^ 0x5A3C0000ul;
    __stack_chk_guard = v & ~0xFFul;
}

void __stack_chk_fail(void)
{
    /* ! NON SI TORNA, E NON SI PROVA A RIPARARE. Se il canarino e' rotto,
     * l'indirizzo di ritorno accanto a lui puo' essere gia' quello di chi
     * attacca: qualunque cosa si faccia dopo — perfino una printf, che passa
     * per puntatori a funzione — potrebbe essere la sua. Si scrive col
     * numero di syscall a mano e si esce. */
    static const char m[] =
        "\n*** stack rotto: una scrittura e' andata oltre un buffer locale.\n"
        "    Il programma viene fermato qui: proseguire vorrebbe dire tornare\n"
        "    a un indirizzo che non e' piu' quello giusto.\n";

    __asm__ volatile ("int $0x80" :: "a"(4), "b"(2), "c"(m), "d"(sizeof(m) - 1)
                      : "memory");
    __asm__ volatile ("int $0x80" :: "a"(1), "b"(134) : "memory");
    for (;;) { }
}

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

    /* ! IL CANARINO SI COMPONE PRIMA DI QUALUNQUE FUNZIONE CHE LO USI. Una
     * funzione protetta chiamata prima di questa riga confronterebbe il valore
     * iniziale con quello iniziale — passerebbe sempre, e la difesa non ci
     * sarebbe. Vedi __libc_canarino_avvia() in libc.c. */
    __libc_canarino_avvia();

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

    /* =====================================================================
     * ! `-v` LA RISPONDE L'AVVIO, NON IL PROGRAMMA
     *
     * Ogni strumento dichiara la propria versione con EX_VERSIONE() e non
     * guarda argv: la domanda «che versione sei?» e' identica per tutti, e
     * cinquantaquattro copie della stessa riga sono cinquantaquattro occasioni
     * di scriverla in modo diverso — una stampa il nome, un'altra no, una esce
     * con 0 e un'altra con 1, tre non la scrivono nella pagina d'uso.
     *
     * ! I DUE SIMBOLI SONO DEBOLI, e chi non li definisce non ha `-v`: un
     * simbolo debole non definito vale zero, quindi il controllo qui sotto e'
     * un confronto con zero. E' cio' che permette a `make`, `gcc` e `fbc` —
     * che si collegano a questa libc e hanno un `--version` loro — di non
     * accorgersi di niente.
     *
     * ! L'OPZIONE E' `-version`, PER ESTESO, e non `-v`. Una lettera sola era
     * gia' presa da tre programmi con tre significati diversi — «parla di
     * piu'» in sshd e telnetd, «visualizza il file» in textline — e una regola
     * che vale per tutti tranne tre non e' una regola. Con la parola intera
     * non si sovrappone a niente, e chi la legge in uno script capisce cosa
     * chiede senza andare a vedere il programma.
     *
     * ! E SI RISPONDE SOLO SE E' L'UNICO ARGOMENTO. `prog -version` e' una
     * domanda sulla versione; `prog -version qualcosa` e' un comando
     * malformato, e rispondergli vorrebbe dire far finta che sia una domanda.
     *
     * ! SI SCRIVE CON write(), NON CON printf(). Qui siamo prima di main e, in
     * un programma collegato alla libc condivisa, prima che i ponti possano
     * essere stati risolti: una printf sarebbe un salto a un puntatore che
     * potrebbe valere ancora zero. Sono due stringhe e uno spazio.
     * ===================================================================== */
    if (&__ex_versione != 0 && &__ex_nome != 0 && argc == 2 &&
        argv[1] != 0 && confronta(argv[1], "-version")) {
        scrivi_versione(__ex_nome);
        scrivi_versione(" ");
        scrivi_versione(__ex_versione);
        scrivi_versione("\n");
        exit(0);
    }

    exit(main(argc, argv, envp));
    for (;;);
}

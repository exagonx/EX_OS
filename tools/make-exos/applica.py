#!/usr/bin/env python3
# =============================================================================
# tools/make-exos/applica.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
#     applica.py <albero-make> [--togli]
#
# Mette (o toglie) nell'albero di GNU make cio' che serve a costruirlo per
# i386-exos. Gemello di tools/binutils-exos/applica.py, e con lo stesso
# marcatore per file: c'e' -> applicato, non c'e' -> da applicare.
#
# -----------------------------------------------------------------------------
# LE MODIFICHE, E PERCHE' SONO SOLO DUE
#
#   config/config.sub   'exos' entra fra i sistemi operativi ammessi, o
#                       configure risponde «Invalid configuration» prima
#                       ancora di provare a compilare qualcosa.
#
#   job.c               come si lancia un comando senza fork().
#
# ! LA SECONDA E' TUTTO IL PORTING, e sta in trenta righe perche' EX-OS ha
# esattamente la chiamata che serve. `child_execute_job` di GNU make e', per
# intero:
#
#     pid = vfork();  se sono il figlio: dup2 dei tre descrittori, exec.
#
# cioe' la definizione a parole di spawn_ex(percorso, argv, envp, redirezioni).
# Non e' un ripiego e non e' piu' lento: e' la stessa cosa detta in una
# chiamata invece che in cinque, e senza duplicare uno spazio di
# indirizzamento per buttarlo via un'istruzione dopo.
#
# ! NON SI USA IL RAMO __MSDOS__, che pure esisteva ed e' la strada che un
# sistema senza fork sembra dover prendere. Quel ramo esegue il comando
# DENTRO child_execute_job con `spawnvpe(P_WAIT, ...)` e finge poi di avere
# un figlio da raccogliere (`++dead_children; child->pid = dos_pid++`).
# Funziona, e costa il parallelismo: con -j2 i due lavori si aspetterebbero
# a vicenda in fila. EX-OS ha spawn e waitpid veri, quindi il figlio e' un
# figlio vero e reap_children resta quello di sempre, WNOHANG compreso.
#
# -----------------------------------------------------------------------------
# COSA NON SERVE, e vale la pena dirlo
#
#   niente pex, niente libiberty  make non li usa: ha job.c e basta.
#   niente modifiche ai segnali   il configure incrociato li dichiara
#                                 assenti (vedi prepara-make.sh) e make ha
#                                 gia' i rami senza.
#   niente glob                   make si porta il proprio (glob/), che
#                                 usa opendir/readdir: EX-OS le ha.
#
# LICENZA. GNU make e' GPLv3+: le modifiche non possono che esserlo, e questo
# script marca ogni file toccato con la dichiarazione di modifica e la data
# che la GPLv3 §5(a) richiede a chi distribuisce una versione modificata.
# EX-OS resta GPL-2.0-or-later — vedi tools/gcc-exos/leggimi.md.
# =============================================================================

import datetime
import os
import sys

MARCA = "Modificato per il bersaglio i386-exos di EX-OS"

# ! IL MARCATORE E' UNA STRINGA CHE ESISTE SOLO NELLO STATO APPLICATO, e non
# e' ridondante rispetto al confronto «il testo nuovo c'e' gia'?». Queste sono
# INSERZIONI: il testo di partenza resta dentro quello di arrivo, quindi quel
# confronto direbbe «da fare» anche a cose fatte, e si applicherebbe due volte.
# Vedi la nota estesa in tools/binutils-exos/applica.py, dove il difetto e'
# stato trovato.
MODIFICHE = [
    (
        "config/config.sub",
        "| -exos* \\",
        "\t      | -chorusos* | -chorusrdb* | -cegcc* \\\n",
        "\t      | -chorusos* | -chorusrdb* | -cegcc* \\\n"
        "\t      | -exos* \\\n",
    ),

    # --- job.c: il figlio si crea gia' fatto ------------------------------
    #
    # Si sostituisce il CORPO di child_execute_job, non il ramo #elif che lo
    # sceglie: quel ramo — `!_AMIGA && !__MSDOS__ && !VMS` — e' gia' quello
    # giusto per noi, e infilarci dentro un terzo sistema vorrebbe dire
    # rendere illeggibile una catena di #ifdef che e' gia' lunga sei sistemi.
    #
    # ! IL `#ifdef __EXOS__` STA DENTRO LA FUNZIONE. Cosi' il testo di
    # partenza resta visibile accanto al nostro, e chi legge questo file fra
    # un anno vede in una schermata sola che cosa fa GNU make su Unix e che
    # cosa fa qui — che e' l'unica documentazione che una modifica a un
    # sorgente di terzi puo' davvero avere.
    (
        "job.c",
        "__EXOS__",
        """  pid = vfork();
  if (pid != 0)
    return pid;
""",
        """#ifdef __EXOS__
  /* =========================================================================
   * EX-OS: il figlio nasce gia' con i suoi descrittori.
   *
   * ! NON C'E' fork(), e la mancanza e' voluta: duplicare uno spazio di
   * indirizzamento per sostituirlo un'istruzione dopo vuol dire copy-on-write,
   * cioe' pagine condivise con un conteggio di riferimenti, per poi buttare
   * via tutto. spawn_ex() prende il percorso, argv, l'ambiente e l'elenco dei
   * descrittori da montare, e rende un PID: e' esattamente quello che le
   * quindici righe qui sotto fanno in cinque chiamate.
   *
   * ! IL FIGLIO E' UN FIGLIO VERO. Il ramo __MSDOS__ di questa stessa
   * funzione — la strada che un sistema senza fork sembra dover prendere —
   * esegue il comando qui dentro con P_WAIT e poi finge di avere qualcuno da
   * raccogliere. Costa il parallelismo: con -j2 i lavori si metterebbero in
   * fila. Qui reap_children resta quella di sempre, WNOHANG compreso.
   *
   * ! SI RIDIRIGE SOLO CIO' CHE E' DAVVERO DIVERSO. Un descrittore che punta
   * gia' dove deve non produce nessuna azione: il figlio lo eredita. Le azioni
   * sono al massimo tre e SPAWN_MAX_AZIONI e' quattro, quindi non c'e' un caso
   * in cui non ci stiano.
   * ========================================================================= */
  {
    SpawnRedir redir[3];
    int        n = 0;

    if (fdin != FD_STDIN)
      { redir[n].fd = FD_STDIN;  redir[n].flags = 0;
        redir[n].percorso = NULL; redir[n].fd_padre = fdin;  ++n; }
    if (fdout != FD_STDOUT)
      { redir[n].fd = FD_STDOUT; redir[n].flags = 0;
        redir[n].percorso = NULL; redir[n].fd_padre = fdout; ++n; }
    if (fderr != FD_STDERR)
      { redir[n].fd = FD_STDERR; redir[n].flags = 0;
        redir[n].percorso = NULL; redir[n].fd_padre = fderr; ++n; }

    /* spawn_ex cerca da se' nel PATH quando argv[0] non ha barre, come
       execvp: e' la stessa regola che exec_command si aspetta piu' sotto. */
    return spawn_ex (argv[0], argv, envp, redir, n);
  }
#else
  pid = vfork();
  if (pid != 0)
    return pid;
#endif
""",
    ),

    # Le due variabili che il ramo EX-OS non usa. Senza questo, `-Wall -Werror`
    # — che make attiva da se' quando il compilatore e' GCC — ferma la
    # compilazione su `r` e `pid` non usati, e il messaggio parla di due
    # variabili invece che di un ramo condizionale.
    (
        "job.c",
        "(void) r; (void) pid;",
        """  int r;
  int pid;
  int fdin = good_stdin ? FD_STDIN : get_bad_stdin ();""",
        """  int r;
  int pid;
  int fdin = good_stdin ? FD_STDIN : get_bad_stdin ();

#ifdef __EXOS__
  (void) r; (void) pid;   /* il ramo EX-OS piu' sotto non li usa */
#endif""",
    ),

    # L'header della libc di EX-OS, per SpawnRedir e spawn_ex. Va DOPO gli
    # include di make: makeint.h tira dentro config.h, e senza quello i
    # #ifdef HAVE_* di libc.h non sarebbero ancora definiti.
    (
        "job.c",
        "#include <libc.h>   /* EX-OS",
        '#include "os.h"\n',
        '#include "os.h"\n'
        "\n"
        "#ifdef __EXOS__\n"
        "#include <libc.h>   /* EX-OS: spawn_ex, SpawnRedir */\n"
        "#endif\n",
    ),

    # --- main.c: --output-sync si rifiuta, non si ignora -------------------
    #
    # make si costruisce con -DNO_OUTPUT_SYNC (vedi prepara-make.sh): la
    # sincronizzazione dell'output fra lavori paralleli e' costruita sui
    # LOCK DI RECORD di fcntl — struct flock, F_SETLKW — e la fcntl di EX-OS
    # conosce solo F_GETFL/F_SETFL. Non e' una riga da aggiungere: sono i
    # lock POSIX sui file, cioe' un pezzo di kernel.
    #
    # ! MA UPSTREAM, IN QUEL CASO, ACCETTA L'OPZIONE E LA IGNORA
    # (`output_sync = OUTPUT_SYNC_NONE;` e via). Un'opzione accettata e
    # ignorata e' una promessa: chi scrive `make -j4 --output-sync=target` in
    # uno script lo fa proprio perche' l'output mescolato non gli serve, e si
    # ritrova esattamente cio' che voleva evitare, senza un avviso. Qui lo si
    # dice.
    (
        "main.c",
        "--output-sync non e' disponibile",
        "#ifdef NO_OUTPUT_SYNC\n"
        "  output_sync = OUTPUT_SYNC_NONE;\n",
        "#ifdef NO_OUTPUT_SYNC\n"
        "  /* EX-OS: costruito senza sincronizzazione dell'output (i lock di\n"
        "     record di fcntl non ci sono). Si DICE, invece di accettare\n"
        "     l'opzione e non farne niente.  */\n"
        "  if (output_sync_option && !streq (output_sync_option, \"none\"))\n"
        "    ON (fatal, NILF,\n"
        "        _(\"--output-sync non e' disponibile in questa build (%s)\"),\n"
        "        output_sync_option);\n"
        "  output_sync = OUTPUT_SYNC_NONE;\n",
    ),

    # --- commands.c: kill() su un sistema che non consegna segnali --------
    #
    # `fatal_error_signal` e' il gestore che, quando make riceve un Ctrl-C o
    # un SIGTERM, ammazza i figli e cancella il bersaglio lasciato a meta'.
    # Su EX-OS non viene eseguito mai — nessuno consegna segnali — ma va
    # COMPILATO, e chiama kill(), che qui non esiste.
    #
    # ! NON SI TOGLIE LA FUNZIONE, si da' un kill() che dice la verita'. I
    # suoi due usi sono diversi e vanno risposti diversamente:
    #
    #     kill (c->pid, SIGTERM)     ammazza un figlio -> non si puo' fare,
    #                                ENOSYS. Non c'e' una syscall per farlo,
    #                                e uno stub che rende 0 direbbe a make che
    #                                i figli sono stati fermati mentre girano
    #                                ancora.
    #     kill (getpid (), sig)      rimanda il segnale a se' stesso, per
    #                                morire del segnale giusto -> e' raise(),
    #                                che la libc ha davvero.
    #
    # Cosi' le due righe di make restano leggibili accanto all'originale, e
    # il giorno che EX-OS avesse una syscall per terminare un processo si
    # cambia questa funzione e basta.
    (
        "commands.c",
        "EX-OS: nessun segnale da consegnare",
        '#include "commands.h"\n',
        '#include "commands.h"\n'
        "\n"
        "#ifdef __EXOS__\n"
        "/* EX-OS: nessun segnale da consegnare a un altro processo, e nessuna\n"
        "   syscall per terminarlo. A se' stessi ci si puo' mandare un segnale,\n"
        "   ed e' raise().  */\n"
        "static int kill (pid_t pid, int sig)\n"
        "{\n"
        "  if (pid == getpid ())\n"
        "    return raise (sig);\n"
        "  errno = ENOSYS;\n"
        "  return -1;\n"
        "}\n"
        "#endif\n",
    ),

    # --- arscan.c: <ar.h> non c'e', ma il formato si', ed e' quello -------
    #
    # make sa aggiornare i membri di una libreria (`libfb.a(file.o)` come
    # bersaglio) e per farlo legge le intestazioni dell'archivio, che
    # descrive <ar.h>. EX-OS quell'header non ce l'ha.
    #
    # ! NON SI DISATTIVA LA FUNZIONE (-DNO_ARCHIVES) e non si aggiunge un
    # header alla libc. Upstream ha GIA' un ramo per i sistemi in cui <ar.h>
    # manca e il formato e' quello di tutti — lo usa per Android e BeOS — e
    # ci si accoda: sono le stesse cinque righe di struttura, mantenute da
    # loro. Il formato e' proprio quello giusto, perche' gli archivi su EX-OS
    # li scrive l'`ar` dei binutils, che scrive SysV come dappertutto.
    (
        "arscan.c",
        "!defined (__EXOS__)",
        "# if !defined (__ANDROID__) && !defined (__BEOS__)\n",
        "# if !defined (__ANDROID__) && !defined (__BEOS__) \\\n"
        "     && !defined (__EXOS__)\n",
    ),

    # --- <pwd.h>: non c'e' un registro degli utenti ------------------------
    #
    # Due file espandono `~utente` e per farlo includono <pwd.h> e chiamano
    # getpwnam: glob/glob.c (i caratteri jolly) e read.c (i percorsi dentro
    # un makefile). EX-OS non ha ne' l'header ne' la funzione, e non e' una
    # mancanza da colmare: non c'e' /etc/passwd, non ci sono utenti con una
    # directory personale, e inventarne uno vorrebbe dire far espandere
    # `~tizio` in un percorso che non esiste.
    #
    # ! SI DA' UNA getpwnam CHE RENDE NULL, invece di togliere il codice che
    # la chiama. E' la stessa risposta che darebbe un sistema vero per un
    # utente che non c'e', e tutti e due i file la gestiscono gia': lasciano
    # `~tizio` com'e' scritto. Togliendo i rami si sarebbe ottenuta la stessa
    # cosa in piu' righe di modifica, e con due pezzi di codice di terzi in
    # meno da poter confrontare con l'originale.
    #
    # ! LO STESSO PEZZO DI CODICE DUE VOLTE, ed e' voluto. Metterlo in un
    # header condiviso vorrebbe dire aggiungere un file all'albero di make e
    # una directory al percorso di ricerca di glob/, che si costruisce per
    # conto suo con un Makefile suo. Quattro righe ripetute costano meno di
    # una dipendenza fra due sottoprogetti che upstream tiene separati.
    #
    # `~` da solo continua a funzionare: viene da getenv("HOME"), che la
    # shell imposta.
    (
        "read.c",
        "EX-OS: nessun registro degli utenti",
        "#ifndef _AMIGA\n"
        "#ifndef VMS\n"
        "#include <pwd.h>\n"
        "#else\n"
        "struct passwd *getpwnam (char *name);\n"
        "#endif\n"
        "#endif\n",
        "#ifndef _AMIGA\n"
        "#if defined (__EXOS__)\n"
        "/* EX-OS: nessun registro degli utenti. Vedi la nota gemella in\n"
        "   glob/glob.c — la stessa risposta di un sistema vero per un utente\n"
        "   che non c'e', cioe' `~tizio' resta com'e' scritto.  */\n"
        "struct passwd { char *pw_name; char *pw_dir; };\n"
        "static struct passwd *getpwnam (const char *nome)\n"
        "{ (void) nome; return (struct passwd *) 0; }\n"
        "static char *getlogin (void) { return (char *) 0; }\n"
        "#elif !defined (VMS)\n"
        "#include <pwd.h>\n"
        "#else\n"
        "struct passwd *getpwnam (char *name);\n"
        "#endif\n"
        "#endif\n",
    ),
    (
        "glob/glob.c",
        "EX-OS: nessun registro degli utenti",
        "#if !defined _AMIGA && !defined VMS && !defined WINDOWS32\n"
        "# include <pwd.h>\n"
        "#endif\n",
        "#if !defined _AMIGA && !defined VMS && !defined WINDOWS32 \\\n"
        "    && !defined __EXOS__\n"
        "# include <pwd.h>\n"
        "#endif\n"
        "\n"
        "#ifdef __EXOS__\n"
        "/* EX-OS: nessun registro degli utenti, quindi nessuna directory\n"
        "   personale da cercare. Rendere NULL e' la risposta giusta — e' cio'\n"
        "   che un sistema vero risponde per un utente che non esiste — e glob.c\n"
        "   la sa gia' leggere: lascia `~tizio' com'e' scritto. `~' da solo\n"
        "   passa da getenv(\"HOME\") e continua a funzionare.  */\n"
        "struct passwd { char *pw_name; char *pw_dir; };\n"
        "static struct passwd *getpwnam (const char *nome)\n"
        "{ (void) nome; return (struct passwd *) 0; }\n"
        "char *getlogin (void) { return (char *) 0; }\n"
        "#endif\n",
    ),
]

# File nostri da copiare nell'albero. Per ora nessuno: il porting di make sta
# tutto nelle sostituzioni qui sopra. La lista resta perche' e' la stessa
# struttura di binutils-exos, e il giorno che servisse un sorgente intero si
# aggiunge qui invece di inventare un secondo meccanismo.
FILE_NOSTRI = []


def marca_file(percorso):
    """Aggiunge la dichiarazione di modifica GPLv3 §5(a), una volta sola."""
    with open(percorso, "r", encoding="utf-8", errors="surrogateescape") as f:
        testo = f.read()
    if MARCA in testo:
        return
    oggi = datetime.date.today().isoformat()

    # Il commento va scritto nella sintassi del file: `#` per gli script di
    # shell, `/* */` per il C. Un `#` in testa a job.c sarebbe una direttiva
    # di preprocessore malformata, cioe' un errore di compilazione al posto
    # di una nota di licenza.
    if percorso.endswith(".c") or percorso.endswith(".h"):
        riga = "/* %s (%s) */\n" % (MARCA, oggi)
    else:
        riga = "# %s (%s)\n" % (MARCA, oggi)

    if testo.startswith("#!"):
        fine = testo.index("\n") + 1
        testo = testo[:fine] + riga + testo[fine:]
    else:
        testo = riga + testo

    with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(testo)


def copia_file_nostri(albero, togli):
    qui = os.path.dirname(os.path.abspath(__file__))
    fatte = saltate = 0

    for nome, relativo in FILE_NOSTRI:
        origine = os.path.join(qui, nome)
        destino = os.path.join(albero, relativo)

        if togli:
            if os.path.exists(destino):
                os.remove(destino)
                print("  - %s" % relativo)
                fatte += 1
            else:
                print("  = %s (gia' a posto)" % relativo)
                saltate += 1
            continue

        if not os.path.exists(origine):
            print("  ! manca il sorgente %s" % origine)
            return None

        with open(origine, "rb") as f:
            nuovo = f.read()

        vecchio = None
        if os.path.exists(destino):
            with open(destino, "rb") as f:
                vecchio = f.read()

        if vecchio == nuovo:
            print("  = %s (gia' a posto)" % relativo)
            saltate += 1
            continue

        with open(destino, "wb") as f:
            f.write(nuovo)
        print("  %s %s" % ("+" if vecchio is None else "~", relativo))
        fatte += 1

    return (fatte, saltate)


def applica(albero, togli):
    fatte = saltate = 0

    for relativo, marcatore, prima, dopo in MODIFICHE:
        percorso = os.path.join(albero, relativo)
        if not os.path.exists(percorso):
            print("  ! manca: %s" % relativo)
            return 1

        with open(percorso, "r", encoding="utf-8", errors="surrogateescape") as f:
            testo = f.read()

        applicato = marcatore in testo
        if applicato != togli:
            print("  = %s (gia' a posto: %s)" % (relativo, marcatore.strip()))
            saltate += 1
            continue

        da, a = (dopo, prima) if togli else (prima, dopo)

        if testo.count(da) != 1:
            print("  ! %s: il testo di riferimento compare %d volte invece di una."
                  % (relativo, testo.count(da)))
            print("    Upstream ha toccato proprio quelle righe: va aggiornato")
            print("    questo script, non forzata la sostituzione.")
            return 1

        with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(testo.replace(da, a))
        if not togli:
            marca_file(percorso)
        print("  %s %s (%s)" % ("-" if togli else "+", relativo, marcatore.strip()))
        fatte += 1

    r = copia_file_nostri(albero, togli)
    if r is None:
        return 1
    fatte += r[0]
    saltate += r[1]

    print("\n%s: %d modifiche, %d gia' a posto."
          % ("Rimozione" if togli else "Applicazione", fatte, saltate))
    return 0


def main():
    if len(sys.argv) < 2:
        print("uso: applica.py <albero-make> [--togli]")
        return 1
    albero = sys.argv[1]
    togli = "--togli" in sys.argv[2:]

    if not os.path.isdir(albero):
        print("Non e' una directory: %s" % albero)
        return 1
    if not os.path.exists(os.path.join(albero, "job.c")):
        print("Non sembra un albero di GNU make: %s" % albero)
        return 1

    print("%s il bersaglio i386-exos in %s\n"
          % ("Tolgo" if togli else "Applico", albero))
    return applica(albero, togli)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# =============================================================================
# tools/freebasic-exos/bersaglio-exos-110.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
#     bersaglio-exos-110.py <albero-freebasic-1.10> [--togli]
#
# Insegna al COMPILATORE FreeBASIC 1.10.1 il bersaglio `exos`. E' il gemello
# di bersaglio-exos.py, che fa la stessa cosa sulla 1.07.3, e ne condivide
# formato e regole: terne (file, cerca, metti), sostituzione idempotente,
# e ci si ferma appena un'ancora non combacia.
#
# -----------------------------------------------------------------------------
# ! PERCHE' DUE SCRIPT E NON UNO CON DEGLI `if`
#
# Delle 26 modifiche della 1.07.3, su un albero 1.10.1 se ne agganciano 14.
# Le altre 12 no. Due di quelle 12 sono cadute per strada — una perche'
# upstream l'ha resa inutile, una perche' era gia' coperta da un'altra —
# e le 10 che restano puntano a righe che upstream ha riscritto: la tabella dei
# bersagli ha un campo in piu', l'enum ha voci nuove in fondo, e
# `emit_x86.bas` ha smesso di confrontare il bersaglio in linea per farlo una
# volta sola in una variabile.
#
# Un solo script con dei rami per versione avrebbe due difetti: le ancore
# vecchie e nuove si somigliano abbastanza da poter combaciare nel posto
# sbagliato, e il giorno che si porta una terza versione i rami diventano
# tre. Due file separati si leggono in diff uno contro l'altro, che e' come
# si capisce cosa e' cambiato fra una versione e l'altra.
#
# ! LE 14 CHE REGGONO NON SONO RICOPIATE: si importano da bersaglio-exos.py.
# Cosi' correggerne una la corregge per tutte e due le versioni — ed e' il
# caso di quelle su inc/crt/, che descrivono la NOSTRA libc e non hanno
# niente a che vedere con la versione di FreeBASIC.
#
# -----------------------------------------------------------------------------
# LE DIFFERENZE CHE CONTANO RISPETTO ALLA 1.07.3
#
#   enum FB_COMPTARGET     finiva con NETBSD, ora finisce con JS
#   tabella targetinfo     ha FB_TARGETOPT_RETURNINFLTS in piu'
#   nomi dei triplet       hanno dragonfly, solaris, js
#   ldcline                dopo il caso LINUX non c'e' piu' `end select`
#   emit_x86.bas           usa `islinux = (...)` invece di confronti sparsi
#
# ! E UNA SCELTA CHE LA 1.07 NON DOVEVA FARE: l'UNWIND.
#
# La 1.10 emette direttive `.cfi_*` quando `FB_COMPOPT_UNWINDINFO` e' acceso,
# e il makefile di FreeBASIC compila il compilatore con `-e`, che lo accende.
# Nella 1.10 quel controllo passa dalla stessa variabile `islinux` che serve a
# `.size` e `.type` — quindi comprendere exos in `islinux` e basta
# accenderebbe ANCHE l'unwind, di rimbalzo e senza averlo chiesto.
#
# Qui le due cose si separano: `islinux` comprende exos (servono `.size` e
# `.type`), `hasunwind` resta di Linux soltanto. La ragione e' un fatto
# verificato, non una preferenza: la 1.07.3 non ha NESSUNA emissione di
# unwind — zero occorrenze in emit_x86.bas — e nel bootstrap exos gia'
# generato non c'e' un solo `.cfi_startproc`. Accenderlo qui vorrebbe dire
# produrre `.eh_frame` che la runtime di EX-OS non consuma e che il suo crt0
# non registra: sezioni che nessuno legge, nel migliore dei casi.
#
# Chi un domani vorra' le eccezioni su EX-OS accendera' quella riga di
# proposito, avendo prima messo la macchina che serve a farle funzionare.
#
# LICENZA. Il compilatore FreeBASIC e' GPL v2. L'elenco per esteso di cosa
# viene cambiato, con il perche' di ogni punto, sta in
# tools/freebasic-exos/MODIFICHE-FBC.md (italiano) e MODIFICHE-FBC.en.md
# (inglese).
# =============================================================================

import importlib.util
import os
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))


# =============================================================================
# LE 14 CHE REGGONO ANCHE SULLA 1.10.1
#
# Prese TALI E QUALI da bersaglio-exos.py, per indice. Se upstream un giorno
# tocca una di quelle righe, l'ancora non combacia piu' e applica() si ferma
# dicendo quale: e' il caso di spostare quella voce da qui a RIFATTE.
# =============================================================================
INVARIATE = [1, 4, 7, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20, 21]


def carica_invariate():
    p = os.path.join(QUI, "bersaglio-exos.py")
    sp = importlib.util.spec_from_file_location("_b107", p)
    mod = importlib.util.module_from_spec(sp)
    sys.modules["_b107"] = mod
    try:
        sp.loader.exec_module(mod)
    except SystemExit:
        pass
    return [tuple(mod.MODIFICHE[i]) for i in INVARIATE]


# =============================================================================
# LE 10 RIFATTE SUI PUNTI NUOVI DELLA 1.10.1
#
# Terne (file, cerca, metti), come nella 1.07. Una quarta voce facoltativa
# dice quante occorrenze ci si aspetta, quando non e' una.
# =============================================================================
RIFATTE = [

    # --- fb.bi: il bersaglio entra nell'enum ------------------------------
    #
    # ! IN FONDO, NON IN MEZZO. Il valore numerico di questi elementi finisce
    # dentro i .asm del bootstrap: infilarne uno a meta' rinumera tutti quelli
    # che seguono, e un bootstrap gia' generato comincia a parlare di un
    # bersaglio per un altro.
    #
    # Nella 1.07.3 l'ultimo era NETBSD; qui e' JS.
    (
        "src/compiler/fb.bi",
        "\tFB_COMPTARGET_JS\n"
        "\tFB_COMPTARGETS\n"
        "end enum\n",
        "\tFB_COMPTARGET_JS\n"
        "\t'' EX-OS.\n"
        "\t'' !! IN FONDO, NON IN MEZZO: il valore numerico di queste voci\n"
        "\t'' finisce dentro i .asm del bootstrap, e infilarne una a meta'\n"
        "\t'' rinumera tutte quelle che seguono.\n"
        "\tFB_COMPTARGET_EXOS\n"
        "\tFB_COMPTARGETS\n"
        "end enum\n",
    ),

    # --- fb.bas: la riga della tabella targetinfo -------------------------
    #
    # ! IL TRATTINO BASSO DAVANTI AL COMMENTO NON E' DECORAZIONE. Dentro un
    # inizializzatore di tabella la riga logica prosegue, e un commento nudo
    # cade dove fbc si aspetta un'espressione: risponde «Expected expression,
    # found '''» nominando una riga che non e' quella sbagliata.
    #
    # ! RISPETTO ALLA 1.07 LE VOCI VICINE HANNO UN CAMPO IN PIU',
    # FB_TARGETOPT_RETURNINFLTS. Per exos NON si mette: l'ABI e' quella di
    # Linux x86 a 32 bit, dove i float tornano sullo stack dell'x87 e non nei
    # registri interi. Copiarlo dalle voci a 64 bit qui accanto darebbe valori
    # di ritorno sbagliati sui float, e senza nessun errore.
    (
        "src/compiler/fb.bas",
        "\t( _\n"
        '\t\t@"js", _\n'
        "\t\tFB_DATATYPE_ULONG, _   '' wchar\n"
        "\t\tFB_FUNCMODE_CDECL, _\n"
        "\t\tFB_FUNCMODE_STDCALL_MS, _\n"
        "\t\t0   or FB_TARGETOPT_UNIX _\n"
        "\t\t\tor FB_TARGETOPT_CALLEEPOPSHIDDENPTR _\n"
        "\t\t\tor FB_TARGETOPT_RETURNINREGS _\n"
        "\t) _\n"
        "}\n",
        "\t( _\n"
        '\t\t@"js", _\n'
        "\t\tFB_DATATYPE_ULONG, _   '' wchar\n"
        "\t\tFB_FUNCMODE_CDECL, _\n"
        "\t\tFB_FUNCMODE_STDCALL_MS, _\n"
        "\t\t0   or FB_TARGETOPT_UNIX _\n"
        "\t\t\tor FB_TARGETOPT_CALLEEPOPSHIDDENPTR _\n"
        "\t\t\tor FB_TARGETOPT_RETURNINREGS _\n"
        "\t), _\n"
        "\t_ '' EX-OS: ELF32 i386, la stessa ABI di linux-x86. Le differenze\n"
        "\t_ '' cominciano al collegamento, non nella forma dei dati.\n"
        "\t_ ''\n"
        "\t_ '' !! IL TRATTINO BASSO DAVANTI AL COMMENTO E' OBBLIGATORIO:\n"
        "\t_ '' dentro un inizializzatore la riga logica prosegue, e un\n"
        "\t_ '' commento nudo cade dove fbc aspetta un'espressione.\n"
        "\t_ ''\n"
        "\t_ '' !! NIENTE FB_TARGETOPT_RETURNINFLTS: su x86 a 32 bit i float\n"
        "\t_ '' tornano sullo stack dell'x87. Copiarlo dalle voci a 64 bit qui\n"
        "\t_ '' sopra darebbe valori di ritorno sbagliati, e senza errore.\n"
        "\t( _\n"
        '\t\t@"exos", _\n'
        "\t\tFB_DATATYPE_ULONG, _   '' wchar\n"
        "\t\tFB_FUNCMODE_CDECL, _\n"
        "\t\tFB_FUNCMODE_STDCALL_MS, _\n"
        "\t\t0   or FB_TARGETOPT_UNIX _\n"
        "\t\t\tor FB_TARGETOPT_CALLEEPOPSHIDDENPTR _\n"
        "\t\t\tor FB_TARGETOPT_STACKALIGN16 _\n"
        "\t\t\tor FB_TARGETOPT_ELF _\n"
        "\t) _\n"
        "}\n",
    ),

    # --- fbc.bas: i nomi riconosciuti da -target --------------------------
    (
        "src/compiler/fbc.bas",
        '\t(@"xbox"       , FB_COMPTARGET_XBOX     )  _\n}',
        '\t(@"xbox"       , FB_COMPTARGET_XBOX     ), _\n'
        '\t(@"exos"       , FB_COMPTARGET_EXOS     )  _\n}',
    ),

    # --- fbc.bas: quale emulazione dare a ld ------------------------------
    #
    # ! NELLA 1.10 DOPO IL CASO LINUX NON C'E' PIU' `end select`: segue
    # DARWIN. L'ancora della 1.07 finiva proprio li', ed e' una delle dodici
    # che non combaciano piu'.
    (
        "src/compiler/fbc.bas",
        "\tcase FB_COMPTARGET_LINUX\n"
        "\t\tselect case( fbGetCpuFamily( ) )\n"
        "\t\tcase FB_CPUFAMILY_X86\n"
        "\t\t\tldcline += \"-m elf_i386 \"\n"
        "\t\tcase FB_CPUFAMILY_X86_64\n"
        "\t\t\tldcline += \"-m elf_x86_64 \"\n"
        "\t\tcase FB_CPUFAMILY_ARM\n"
        "\t\t\tldcline += \"-m armelf_linux_eabi \"\n"
        "\t\tend select\n"
        "\tcase FB_COMPTARGET_DARWIN\n",
        "\tcase FB_COMPTARGET_LINUX\n"
        "\t\tselect case( fbGetCpuFamily( ) )\n"
        "\t\tcase FB_CPUFAMILY_X86\n"
        "\t\t\tldcline += \"-m elf_i386 \"\n"
        "\t\tcase FB_CPUFAMILY_X86_64\n"
        "\t\t\tldcline += \"-m elf_x86_64 \"\n"
        "\t\tcase FB_CPUFAMILY_ARM\n"
        "\t\t\tldcline += \"-m armelf_linux_eabi \"\n"
        "\t\tend select\n"
        "\t'' EX-OS e' x86 a 32 bit e basta: niente select da fare.\n"
        "\tcase FB_COMPTARGET_EXOS\n"
        "\t\tldcline += \"-m elf_i386 \"\n"
        "\tcase FB_COMPTARGET_DARWIN\n",
    ),

    # --- fbc.bas: i flag di collegamento per exos -------------------------
    (
        "src/compiler/fbc.bas",
        "\tcase FB_COMPTARGET_XBOX\n"
        "\t\tldcline += \" -nostdlib --file-alignment 0x20 "
        "--section-alignment 0x20 -shared\"\n",
        "\tcase FB_COMPTARGET_EXOS\n"
        "\t\t'' !! COLLEGAMENTO STATICO, E NON E' UNA PREFERENZA: EX-OS non ha\n"
        "\t\t'' un caricatore dinamico, quindi niente -dynamic-linker (che\n"
        "\t\t'' infatti exos non prende, non stando nel gruppo di linux) e\n"
        "\t\t'' niente .so.\n"
        "\t\t''\n"
        "\t\t'' -e _start   l'ingresso lo definisce crt0.o. E' anche il\n"
        "\t\t''             predefinito di ld per ELF, ma dirlo costa una\n"
        "\t\t''             parola e toglie una dipendenza da un default.\n"
        "\t\t'' 0x08000000  l'indirizzo a cui EX-OS carica i programmi, lo\n"
        "\t\t''             stesso dei linker script di bin/*.ld. Il\n"
        "\t\t''             predefinito di ld per elf_i386 e' 0x08048000, e\n"
        "\t\t''             non e' quello.\n"
        "\t\tldcline += \" -static -e _start -Ttext-segment=0x08000000\"\n"
        "\n"
        "\tcase FB_COMPTARGET_XBOX\n"
        "\t\tldcline += \" -nostdlib --file-alignment 0x20 "
        "--section-alignment 0x20 -shared\"\n",
    ),

    # --- fbc.bas: le librerie predefinite ---------------------------------
    (
        "src/compiler/fbc.bas",
        "\tcase FB_COMPTARGET_NETBSD\n"
        "\t\tfbcAddDefLib( \"gcc\" )\n",
        "\tcase FB_COMPTARGET_EXOS\n"
        "\t\t'' !! NIENTE pthread, dl, ncurses o tinfo. La runtime per EX-OS\n"
        "\t\t'' e' costruita senza thread (vedi src/rtlib/exos/) e senza\n"
        "\t\t'' terminfo: chiederle qui darebbe un collegamento che fallisce\n"
        "\t\t'' su librerie inesistenti, con un messaggio che parla di simboli\n"
        "\t\t'' mai visti invece che della loro assenza.\n"
        "\t\t''\n"
        "\t\t'' L'ordine e' quello che serve a un link statico: libfb chiama\n"
        "\t\t'' la libc, la libc chiama libgcc.\n"
        "\t\tfbcAddDefLib( \"c\" )\n"
        "\t\tfbcAddDefLib( \"m\" )\n"
        "\t\tfbcAddDefLib( \"gcc\" )\n"
        "\n"
        "\tcase FB_COMPTARGET_NETBSD\n"
        "\t\tfbcAddDefLib( \"gcc\" )\n",
    ),

    # --- fbc.bas: non si chiede se il linker e' gold ----------------------
    #
    # ! Nella 1.10 questa condizione ha due righe in piu' (SOLARIS e JS) e
    # rientri fatti di TABULAZIONI, non di spazi come nella 1.07. E' il
    # motivo per cui l'ancora vecchia non combaciava.
    #
    # Si tocca solo l'ultima riga: il resto della condizione — compreso il
    # `-T fbextra.x` che ne dipende — resta come upstream lo scrive, perche'
    # a exos quello script di collegamento serve come a Linux.
    (
        "src/compiler/fbc.bas",
        "\t\tif( fbGetOption( FB_COMPOPT_OBJINFO ) and _\n"
        "\t\t\t(fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_DARWIN) and _\n"
        "\t\t\t(fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_SOLARIS) and _\n"
        "\t\t\t( fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_JS ) and _\n"
        "\t\t\t(not fbcIsUsingGoldLinker( )) ) then\n",
        "\t\t'' !! SU EX-OS NON SI CHIEDE SE E' gold, SI SA CHE NON LO E'.\n"
        "\t\t'' fbcIsUsingGoldLinker() lancia `ld --version` e ne legge la\n"
        "\t\t'' prima riga: dentro EX-OS quella strada passa da popen(), cioe'\n"
        "\t\t'' da /bin/sh, per rispondere a una domanda la cui risposta e'\n"
        "\t\t'' gia' nota. Un processo in piu' a ogni collegamento, per niente.\n"
        "\t\tif( fbGetOption( FB_COMPOPT_OBJINFO ) and _\n"
        "\t\t\t(fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_DARWIN) and _\n"
        "\t\t\t(fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_SOLARIS) and _\n"
        "\t\t\t( fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_JS ) and _\n"
        "\t\t\t((fbGetOption( FB_COMPOPT_TARGET ) = FB_COMPTARGET_EXOS) orelse _\n"
        "\t\t\t (not fbcIsUsingGoldLinker( ))) ) then\n",
    ),

    # --- inc/crt/stdlib.bi: NON SI TOCCA PIU', E NON E' UNA DIMENTICANZA --
    #
    # Sulla 1.07.3 questo file smistava su `__FB_LINUX__` e bisognava
    # aggiungergli un ramo `__FB_EXOS__`. Nella 1.10 upstream l'ha riscritto
    # per smistare su `__FB_UNIX__`, che exos SODDISFA GIA': la voce exos
    # della tabella targetinfo accende FB_TARGETOPT_UNIX, e symb-define.bas
    # ne ricava __FB_UNIX__ (riga 1346).
    #
    # Percio' exos pesca crt/unix/stdlib.bi — e li' dentro c'e' esattamente
    # cio' che c'e' nel nostro crt-exos/stdlib.bi: la sola `mkstemp`.
    # Aggiungere un ramo per includere il nostro darebbe lo stesso identico
    # risultato, con un file in piu' da tenere allineato.
    #
    # Le 25 modifiche di questo script sono percio' una in meno delle 26
    # della 1.07: non ne manca una, ne e' diventata inutile una.

    # --- emit_x86.bas: la sezione .rodata NON sta qui ---------------------
    #
    # La scelta fra .rodata e .data per le costanti la fa gia' la modifica 21
    # delle INVARIATE: la sua ancora della 1.07 combacia tale e quale sulla
    # 1.10, perche' quelle due righe upstream non le ha toccate. Ripeterla
    # qui la farebbe risultare «gia' a posto» e basta, ma sarebbe una voce da
    # tenere allineata in due posti.

    # --- emit_x86.bas: islinux nell'epilogo di procedura -------------------
    (
        "src/compiler/emit_x86.bas",
        "\tislinux = ( env.clopt.target = FB_COMPTARGET_LINUX )\n"
        "\t' don't do anything for naked functions, except the .size at the end\n",
        "\t'' EX-OS: serve il .size in fondo, come su Linux.\n"
        "\tislinux = ( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _\n"
        "\t            (env.clopt.target = FB_COMPTARGET_EXOS) )\n"
        "\t' don't do anything for naked functions, except the .size at the end\n",
    ),

    # --- emit_x86.bas: e il hasunwind CHE STA DENTRO QUELL'EPILOGO --------
    #
    # ! QUESTA VOCE ESISTE PERCHE' MI ERA SFUGGITA, e la prova l'ha presa.
    # I punti che guardano l'unwind sono TRE, non due:
    #
    #   riga 1144  hasunwind proprio, gia' solo-Linux    -> exos spento, ok
    #   riga 1238  hasunwind = islinux andalso ...       <- QUESTO
    #   riga 7609  islinux + hasunwind insieme           -> sistemato sotto
    #
    # Il secondo sta DENTRO il corpo dell'epilogo, dopo l'ancora della voce
    # qui sopra, e quindi non veniva toccato: allargando `islinux` ereditava
    # l'unwind di rimbalzo. Il risultato non era codice «con un po' di CFI in
    # piu'», era codice CHE NON SI ASSEMBLA:
    #
    #   u_exos.asm:26: Error: CFI instruction used without previous
    #                         .cfi_startproc
    #
    # perche' il prologo (1144, gia' spento per exos) NON apriva la regione e
    # l'epilogo la chiudeva lo stesso. Due meta' di un interruttore girate in
    # senso opposto.
    (
        "src/compiler/emit_x86.bas",
        "\t\thasunwind = islinux andalso fbGetOption( FB_COMPOPT_UNWINDINFO )\n",
        "\t\t'' EX-OS: unwind NO — vedi il commento in testa a questo script.\n"
        "\t\t'' !! NON si scrive `islinux andalso ...`: `islinux` ora comprende\n"
        "\t\t'' exos, e il .cfi_restore qui sotto uscirebbe senza il\n"
        "\t\t'' .cfi_startproc del prologo, che invece resta di Linux.\n"
        "\t\thasunwind = ( env.clopt.target = FB_COMPTARGET_LINUX ) andalso _\n"
        "\t\t            fbGetOption( FB_COMPOPT_UNWINDINFO )\n",
    ),

    # --- emit_x86.bas: islinux nell'epilogo, SENZA accendere l'unwind -----
    #
    # ! E' LA MODIFICA PIU' DELICATA DI TUTTE. Nella 1.10 una sola
    # variabile governa due cose: le direttive `.size`/`.type` (che a exos
    # servono) e l'emissione delle `.cfi_*` (che a exos NON serve, e che la
    # 1.07 non produceva affatto). Comprendendo exos in `islinux` e basta si
    # otterrebbe l'unwind di rimbalzo — e il makefile compila il compilatore
    # con `-e`, che accende FB_COMPOPT_UNWINDINFO.
    #
    # Percio' `hasunwind` viene slegato e continua a guardare solo Linux.
    (
        "src/compiler/emit_x86.bas",
        "\tislinux = ( env.clopt.target = FB_COMPTARGET_LINUX )\n"
        "\thasunwind = islinux andalso fbGetOption( FB_COMPOPT_UNWINDINFO )\n",
        "\t'' EX-OS: .size e .type si', unwind NO.\n"
        "\t'' !! `hasunwind` viene SLEGATO da `islinux`: la 1.07 non emetteva\n"
        "\t'' nessuna .cfi_* e il bootstrap exos gia' generato non ne contiene\n"
        "\t'' una. Le eccezioni su EX-OS vogliono macchina che oggi non c'e';\n"
        "\t'' produrre .eh_frame che nessuno consuma sarebbe peso e nient'altro.\n"
        "\tislinux = ( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _\n"
        "\t            (env.clopt.target = FB_COMPTARGET_EXOS) )\n"
        "\thasunwind = ( env.clopt.target = FB_COMPTARGET_LINUX ) andalso _\n"
        "\t            fbGetOption( FB_COMPOPT_UNWINDINFO )\n",
    ),

    # --- emit_x86.bas: gli attributi delle sezioni ctor/dtor --------------
    #
    # Sono DUE punti identici byte per byte (costruttori e distruttori),
    # quindi si sostituiscono tutti e due in una volta. Distinguerli
    # vorrebbe dire allargare l'ancora fino a righe che non c'entrano, e
    # un'ancora piu' larga e' un'ancora che si rompe prima.
    (
        "src/compiler/emit_x86.bas",
        "\t\t\tif( env.clopt.target = FB_COMPTARGET_LINUX ) then\n"
        '\t\t\t\tostr += ", " + QUOTE + "aw" + QUOTE + ", @progbits"\n',
        "\t\t\t'' EX-OS: stesso ELF di Linux, stessi attributi.\n"
        "\t\t\tif( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _\n"
        "\t\t\t    (env.clopt.target = FB_COMPTARGET_EXOS) ) then\n"
        '\t\t\t\tostr += ", " + QUOTE + "aw" + QUOTE + ", @progbits"\n',
        2,
    ),
]


def copia_header(albero, togli):
    """I .bi della nostra libc: inc/crt/exos/ e inc/crt/sys/exos/."""
    coppie = [
        (os.path.join(QUI, "crt-exos"), os.path.join(albero, "inc", "crt", "exos")),
        (os.path.join(QUI, "crt-exos", "sys"),
         os.path.join(albero, "inc", "crt", "sys", "exos")),
    ]
    for origine, destino in coppie:
        if togli:
            if os.path.isdir(destino):
                shutil.rmtree(destino)
                print("  - %s" % os.path.relpath(destino, albero))
            continue
        if not os.path.isdir(origine):
            print("  ! manca %s" % origine, file=sys.stderr)
            return 1
        os.makedirs(destino, exist_ok=True)
        n = 0
        for f in sorted(os.listdir(origine)):
            s = os.path.join(origine, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(destino, f))
                n += 1
        print("  + %s (%d file)" % (os.path.relpath(destino, albero), n))
    return 0


def applica(albero, togli):
    modifiche = carica_invariate() + [tuple(m) for m in RIFATTE]
    fatte = 0
    gia = 0

    for voce in modifiche:
        rel, cerca, metti = voce[0], voce[1], voce[2]
        attese = voce[3] if len(voce) > 3 else 1
        percorso = os.path.join(albero, rel)

        if not os.path.isfile(percorso):
            print("  ! manca %s" % rel)
            return 1

        with open(percorso, encoding="utf-8", errors="surrogateescape") as fh:
            testo = fh.read()

        # ! SI GUARDA SEMPRE `metti`, IN TUTTE E DUE LE DIREZIONI, e mai
        # `cerca`. Sembra piu' naturale chiedersi «c'e' gia' il testo che
        # voglio ottenere?» — cioe' `cerca` quando si toglie — ma NON
        # FUNZIONA: quasi tutte queste modifiche INSERISCONO attorno al testo
        # cercato invece di sostituirlo, quindi dopo averle applicate `cerca`
        # e' ancora li' dentro.
        #
        # Il caso che lo dimostra e' `#else` contro `#elseif`: il primo e' un
        # prefisso del secondo, e una rimozione che si fidi di `cerca` si
        # convince di aver gia' finito. Il risultato non e' un errore ma un
        # albero tolto A META', che si compila lo stesso e sbaglia altrove.
        if togli:
            if metti not in testo:
                gia += 1
                continue
            da, a = metti, cerca
        else:
            if metti in testo:
                gia += 1
                continue
            da, a = cerca, metti

        if testo.count(da) != attese:
            # ! MENO O PIU' DEL PREVISTO SONO ENTRAMBI UN ERRORE, e vanno
            # detti. Meno: il sorgente e' cambiato sotto e la modifica NON e'
            # stata applicata — proseguire darebbe un albero a meta'. Piu':
            # non si sa quale sia quello giusto, e sceglierne uno a caso e'
            # peggio che fermarsi.
            #
            # ! Dentro un COMPILATORE una sostituzione finita nel posto
            # sbagliato non da' un errore: da' codice generato diverso da
            # quello che si crede, e lo si scopre molto piu' a valle.
            print("  ! %s: trovato %d volte invece di %d"
                  % (rel, testo.count(da), attese))
            print("    il testo cercato comincia con:")
            print("      %s" % da.strip().splitlines()[0][:70])
            print("    Va aggiornato questo script, non forzata la sostituzione.")
            return 1

        with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(testo.replace(da, a))
        fatte += 1

    if copia_header(albero, togli) != 0:
        return 1

    verso = "tolto" if togli else "messo"
    print("  %d modifiche applicate, %d gia' a posto (%s)" % (fatte, gia, verso))
    return 0


def main():
    if len(sys.argv) < 2:
        print("uso: bersaglio-exos-110.py <albero-freebasic-1.10> [--togli]")
        return 1
    albero = sys.argv[1]
    togli = "--togli" in sys.argv[2:]

    if not os.path.isdir(albero):
        print("Non e' una directory: %s" % albero, file=sys.stderr)
        return 1
    if not os.path.isfile(os.path.join(albero, "src", "compiler", "fbc.bas")):
        print("Non sembra un albero di FreeBASIC: %s" % albero, file=sys.stderr)
        return 1

    # ! SI CONTROLLA LA VERSIONE PRIMA DI TOCCARE QUALSIASI COSA. Questo
    # script conosce le righe della 1.10; su un albero 1.07 le ancore
    # combacerebbero IN PARTE, e un albero modificato a meta' e' peggio di
    # uno non modificato — il primo si compila lo stesso.
    vm = os.path.join(albero, "version.mk")
    if os.path.isfile(vm):
        testo = open(vm, encoding="utf-8", errors="surrogateescape").read()
        if "1.10" not in testo:
            print("!  %s non e' una 1.10: per la 1.07 usa bersaglio-exos.py"
                  % albero, file=sys.stderr)
            print("    (%s)" % testo.strip().replace("\n", " "), file=sys.stderr)
            return 1

    print("%s il bersaglio exos in %s"
          % ("Tolgo" if togli else "Applico", albero))
    return applica(albero, togli)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# =============================================================================
# tools/freebasic-exos/bersaglio-exos.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
"""Aggiunge il bersaglio `exos` al COMPILATORE FreeBASIC.

    python3 tools/freebasic-exos/bersaglio-exos.py <albero-fb> [--togli]

E' il gemello di applica.py, che invece tocca la RUNTIME (src/rtlib/). I due
lavori sono separati perche' rispondono a due domande diverse:

    applica.py          come gira la runtime SU EX-OS
    bersaglio-exos.py   come fbc COLLEGA un programma PER EX-OS

Fino ad agosto 2026 esisteva solo il primo, e `fbc` restava convinto di
produrre per Linux: compilava e assemblava bene — EX-OS condivide con Linux
l'ABI ELF32 i386 — e poi al link chiedeva `crt1.o` e
`-dynamic-linker /lib/ld-linux.so.2`, che qui non esistono. Usciva con 1
prima ancora di stampare la riga `linking:`, e il sintomo faceva cercare il
guasto nell'assemblatore, che aveva finito benissimo.

-----------------------------------------------------------------------------
!! COSA CAMBIA E COSA NO

`exos` e' identico a `linux` in tutto cio' che riguarda il CODICE: stessa
ABI, stesso ELF32 i386, stesse direttive .type/.size/.rodata. Cambia SOLO il
link. Percio' qui dentro:

  - nelle tabelle e nel generatore di codice, `exos` si accoda a `linux`;
  - in hLinkFiles(), `exos` ha un ramo suo e NON entra nei gruppi di `linux`.

!! ir-gas64.bas NON SI TOCCA, ed e' una scelta. Sono 28 confronti con
FB_COMPTARGET_LINUX, tutti nel backend a 64 bit: EX-OS e' a 32 bit e quel
file non viene nemmeno attraversato. Toccarlo vorrebbe dire modificare
codice che nessuna prova esercita.

!! symb-struct.bas NON SI TOCCA, stessa ragione: i suoi due confronti stanno
dentro `if fbIs64Bit()`.

-----------------------------------------------------------------------------
!! DUE MACRO CHE SI SOMIGLIANO E NON C'ENTRANO NIENTE

    __FB_UNIX__   lato COMPILATORE. Lo definisce symb-define.bas dai flag del
                  bersaglio, e serve a fbc per scegliere exec() invece di
                  shell() quando lancia as e ld (fbc.bas:401). Per `exos` LO
                  VOGLIAMO.
    HOST_UNIX     lato RUNTIME, un #define C di src/rtlib/. Tira dentro
                  termios, i segnali, i thread e _FILE_OFFSET_BITS=64, e di
                  quei quattro EX-OS non ha niente. NON lo vogliamo, e
                  infatti applica.py non lo definisce.

Confonderle costa una giornata.

-----------------------------------------------------------------------------
!! __FB_EXOS__ NON VA DICHIARATO DA NESSUNA PARTE

symb-define.bas:271 lo compone da solo:

    symbAddDefine( "__FB_" + ucase( *env.target.id ) + "__", NULL, 0 )

Basta che l'id del bersaglio sia "exos". E' quello che fa funzionare il
`#elseif defined(__FB_EXOS__)` in fb.bi quando si compila fbc per EX-OS.

-----------------------------------------------------------------------------
Licenza: il compilatore FreeBASIC e' GPL v2 e queste modifiche stanno dentro
i suoi file, quindi sono GPL v2 come il resto di src/compiler/. La runtime
(src/rtlib/) e' un'altra cosa: LGPL con eccezione di collegamento, vedi
applica.py.
"""

import os
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))

# (percorso relativo, testo da cercare, testo da metterci)
MODIFICHE = [

    # -------------------------------------------------------------------------
    # 1. L'enum dei bersagli
    # -------------------------------------------------------------------------
    (
        "src/compiler/fb.bi",
        """	FB_COMPTARGET_NETBSD
	FB_COMPTARGETS
end enum""",
        """	FB_COMPTARGET_NETBSD
	'' EX-OS.
	'' !! IN FONDO, NON IN MEZZO. Il valore numerico di questi elementi
	'' finisce dentro i .asm del bootstrap: infilarne uno a meta' rinumera
	'' tutti quelli che seguono, e un bootstrap gia' generato comincia a
	'' parlare di un bersaglio per un altro.
	FB_COMPTARGET_EXOS
	FB_COMPTARGETS
end enum""",
    ),

    # -------------------------------------------------------------------------
    # 2. Il bersaglio predefinito quando fbc gira SU EX-OS
    # -------------------------------------------------------------------------
    (
        "src/compiler/fb.bi",
        """#elseif defined(__FB_NETBSD__)
const FB_HOST_EXEEXT        = ""
const FB_HOST_PATHDIV       = "/"
const FB_DEFAULT_TARGET     = FB_COMPTARGET_NETBSD
#else""",
        """#elseif defined(__FB_NETBSD__)
const FB_HOST_EXEEXT        = ""
const FB_HOST_PATHDIV       = "/"
const FB_DEFAULT_TARGET     = FB_COMPTARGET_NETBSD
#elseif defined(__FB_EXOS__)
'' __FB_EXOS__ lo definisce symb-define.bas dall'id del bersaglio: non c'e'
'' niente da dichiarare, basta compilare con -target exos.
const FB_HOST_EXEEXT        = ""
const FB_HOST_PATHDIV       = "/"
const FB_DEFAULT_TARGET     = FB_COMPTARGET_EXOS
#else""",
    ),

    # -------------------------------------------------------------------------
    # 3. targetinfo(): l'ABI. Identica a linux, e l'ordine deve corrispondere
    #    all'enum, quindi in fondo anche qui.
    # -------------------------------------------------------------------------
    (
        "src/compiler/fb.bas",
        """	( _
		@"netbsd", _
		FB_DATATYPE_ULONG, _
		FB_FUNCMODE_CDECL, _
		FB_FUNCMODE_STDCALL_MS, _
		0	or FB_TARGETOPT_UNIX _
			or FB_TARGETOPT_CALLEEPOPSHIDDENPTR _
			or FB_TARGETOPT_RETURNINREGS _
			or FB_TARGETOPT_ELF _
	) _
}""",
        """	( _
		@"netbsd", _
		FB_DATATYPE_ULONG, _
		FB_FUNCMODE_CDECL, _
		FB_FUNCMODE_STDCALL_MS, _
		0	or FB_TARGETOPT_UNIX _
			or FB_TARGETOPT_CALLEEPOPSHIDDENPTR _
			or FB_TARGETOPT_RETURNINREGS _
			or FB_TARGETOPT_ELF _
	), _
	_ '' EX-OS: gli stessi valori di linux, riga per riga. L'ABI e' quella
	_ '' e non c'e' niente da inventare - e' ELF32 i386 come Linux, e le
	_ '' differenze cominciano al link. FB_TARGETOPT_UNIX qui vuol dire
	_ '' __FB_UNIX__ per il compilatore, non HOST_UNIX per la runtime.
	_ ''
	_ '' !! IL TRATTINO BASSO DAVANTI AL COMMENTO NON E' DECORAZIONE.
	_ '' Dentro un inizializzatore di tabella la riga logica prosegue, e un
	_ '' commento nudo cade dove fbc si aspetta un'espressione: risponde
	_ '' "Expected expression, found '''" e nomina una riga che non e'
	_ '' quella sbagliata. La forma buona e' gia' usata qui accanto, nella
	_ '' tabella dei bersagli.
	( _
		@"exos", _
		FB_DATATYPE_ULONG, _
		FB_FUNCMODE_CDECL, _
		FB_FUNCMODE_STDCALL_MS, _
		0	or FB_TARGETOPT_UNIX _
			or FB_TARGETOPT_CALLEEPOPSHIDDENPTR _
			or FB_TARGETOPT_STACKALIGN16 _
			or FB_TARGETOPT_ELF _
	) _
}""",
    ),

    # -------------------------------------------------------------------------
    # 4. I nomi riconosciuti nelle triplette GNU
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	(@"xbox"   , FB_COMPTARGET_XBOX   )  _
}""",
        """	(@"xbox"   , FB_COMPTARGET_XBOX   ), _
	(@"exos"   , FB_COMPTARGET_EXOS   )  _
}""",
    ),

    # -------------------------------------------------------------------------
    # 5. Cosa accetta -target
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	(@"openbsd", FB_COMPTARGET_OPENBSD, FB_DEFAULT_CPUTYPE       )  _
}""",
        """	(@"openbsd", FB_COMPTARGET_OPENBSD, FB_DEFAULT_CPUTYPE       ), _
	_ '' EX-OS gira solo su x86 a 32 bit, come dos e xbox: si puo' sempre
	_ '' dare per scontata l'architettura.
	(@"exos"   , FB_COMPTARGET_EXOS   , FB_DEFAULT_CPUTYPE_X86   )  _
}""",
    ),

    # -------------------------------------------------------------------------
    # 6. hLinkFiles(): l'emulazione di ld
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	case FB_COMPTARGET_LINUX
		select case( fbGetCpuFamily( ) )
		case FB_CPUFAMILY_X86
			ldcline += "-m elf_i386 "
		case FB_CPUFAMILY_X86_64
			ldcline += "-m elf_x86_64 "
		case FB_CPUFAMILY_ARM
			ldcline += "-m armelf_linux_eabi "
		end select
	end select""",
        """	case FB_COMPTARGET_LINUX
		select case( fbGetCpuFamily( ) )
		case FB_CPUFAMILY_X86
			ldcline += "-m elf_i386 "
		case FB_CPUFAMILY_X86_64
			ldcline += "-m elf_x86_64 "
		case FB_CPUFAMILY_ARM
			ldcline += "-m armelf_linux_eabi "
		end select
	case FB_COMPTARGET_EXOS
		ldcline += "-m elf_i386 "
	end select""",
    ),

    # -------------------------------------------------------------------------
    # 7. Il ramo dell'eseguibile: statico, entry, indirizzo di carico
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	case FB_COMPTARGET_XBOX
		ldcline += " -nostdlib --file-alignment 0x20 --section-alignment 0x20 -shared"

	end select""",
        """	case FB_COMPTARGET_EXOS
		'' !! COLLEGAMENTO STATICO, E NON E' UNA PREFERENZA: EX-OS non ha
		'' un caricatore dinamico, quindi niente -dynamic-linker (che
		'' infatti `exos` non prende, non stando nel gruppo di linux
		'' qui sopra) e niente .so.
		''
		'' -e _start   l'ingresso lo definisce crt0.o. E' anche il
		''             predefinito di ld per ELF, ma dirlo costa una
		''             parola e toglie una dipendenza da un default.
		'' 0x08000000  l'indirizzo a cui EX-OS carica i programmi: e' lo
		''             stesso dei linker script di bin/*.ld. Il
		''             predefinito di ld per elf_i386 e' 0x08048000, e
		''             non e' quello.
		ldcline += " -static -e _start -Ttext-segment=0x08000000"

	case FB_COMPTARGET_XBOX
		ldcline += " -nostdlib --file-alignment 0x20 --section-alignment 0x20 -shared"

	end select""",
    ),

    # -------------------------------------------------------------------------
    # 8. Gli oggetti di avvio: crt0.o e basta
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	case FB_COMPTARGET_XBOX
		'' link with crt0.o (C runtime init)
		ldcline += hFindLib( "crt0.o" )

	end select""",
        """	case FB_COMPTARGET_XBOX
		'' link with crt0.o (C runtime init)
		ldcline += hFindLib( "crt0.o" )

	case FB_COMPTARGET_EXOS
		'' !! crt0.o, NON crt1.o. Il ramo di linux qui sopra chiede
		'' crt1.o + crti.o + crtbegin.o, e in coda crtend.o + crtn.o:
		'' sono gli oggetti di avvio della glibc, e da noi non esistono.
		'' EX-OS ha un crt0.o solo, che fa tutto quello che serve.
		''
		'' Era questa la riga che faceva fallire il link: hFindLib() non
		'' trovava crt1.o, chiamava errReportEx() e si usciva PRIMA di
		'' stampare "linking:".
		ldcline += hFindLib( "crt0.o" )

	end select""",
    ),

    # -------------------------------------------------------------------------
    # 9. Le librerie predefinite
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	case FB_COMPTARGET_NETBSD
		'' TODO""",
        """	case FB_COMPTARGET_EXOS
		'' !! NIENTE pthread, dl, ncurses o tinfo. La runtime per EX-OS
		'' e' costruita senza thread (vedi src/rtlib/exos/) e senza
		'' terminfo: chiederle qui vorrebbe dire un link che fallisce su
		'' librerie che non esistono, e il messaggio parlerebbe di
		'' simboli mai visti invece che della loro assenza.
		''
		'' L'ordine e' quello che serve a un link statico: libfb chiama
		'' la libc, la libc chiama libgcc.
		fbcAddDefLib( "c" )
		fbcAddDefLib( "m" )
		fbcAddDefLib( "gcc" )

	case FB_COMPTARGET_NETBSD
		'' TODO""",
    ),

    # -------------------------------------------------------------------------
    # 10. Il controllo "e' il linker gold?" non si fa su EX-OS
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """		if( fbGetOption( FB_COMPOPT_OBJINFO ) and _
		    (fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_DARWIN) and _
		    (not fbcIsUsingGoldLinker( )) ) then""",
        """		'' !! SU EX-OS NON SI CHIEDE SE E' gold, SI SA CHE NON LO E'.
		'' fbcIsUsingGoldLinker() lancia `ld --version` e ne legge la
		'' prima riga: dentro EX-OS quella strada passa da popen(), cioe'
		'' da /bin/sh, per rispondere a una domanda la cui risposta e'
		'' nota. Un processo in piu' a ogni collegamento, per niente.
		if( fbGetOption( FB_COMPOPT_OBJINFO ) and _
		    (fbGetOption( FB_COMPOPT_TARGET ) <> FB_COMPTARGET_DARWIN) and _
		    ((fbGetOption( FB_COMPOPT_TARGET ) = FB_COMPTARGET_EXOS) orelse _
		     (not fbcIsUsingGoldLinker( ))) ) then""",
    ),

    # -------------------------------------------------------------------------
    # 11. Dove si cercano crt0.o e le librerie
    #
    # !! ERA QUESTO A TENERE IN PIEDI L'ULTIMO PEZZO DEL DIFETTO. fbc guarda
    # solo dentro la propria lib/freebasic/<target>/ e, se non trova, CHIEDE A
    # gcc con `gcc -m32 -print-file-name=<file>`. Da noi crt0.o, libc.a,
    # libm.a e libgcc.a stanno in <prefisso>/lib, che non e' nessuna delle
    # due: hFindLib("crt0.o") falliva ed era di nuovo un'uscita muta prima
    # della riga `linking:`.
    #
    # !! E LA DOMANDA A gcc NON SI PUO' NEMMENO FARE, dentro EX-OS. La
    # risposta si legge con hGet1stOutputLineFromCommand(), cioe' popen():
    # l'output del figlio finisce sulla console invece che nel buffer, e
    # quella riga solitaria con il percorso di libgcc.a in mezzo all'output
    # di fbc era esattamente questo. Una domanda che stampa la risposta a
    # video e ne rende una vuota e' peggio che non farla.
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	'' Not found in our lib/, query the target-specific gcc
	dim as string path
	fbcFindBin( FBCTOOL_GCC, path )""",
        """	'' EX-OS: l'albero degli strumenti e' completo per costruzione, e
	'' quello che non sta in lib/freebasic/<target>/ sta in <prefisso>/lib
	'' — crt0.o, libc.a, libm.a, libgcc.a. Si guarda li' e si smette: a gcc
	'' non si chiede niente, perche' la risposta arriverebbe da popen() e
	'' dentro EX-OS l'output del figlio non torna indietro.
	if( fbGetOption( FB_COMPOPT_TARGET ) = FB_COMPTARGET_EXOS ) then
		found = fbc.prefix + "lib" + FB_HOST_PATHDIV + *file
		if( hFileExists( found ) ) then
			return found
		end if
		exit function
	end if

	'' Not found in our lib/, query the target-specific gcc
	dim as string path
	fbcFindBin( FBCTOOL_GCC, path )""",
    ),

    # -------------------------------------------------------------------------
    # 12. <prefisso>/lib fra i percorsi di ricerca di ld
    #
    # Trovare crt0.o non basta: crt0.o si passa per nome intero, ma -lc, -lm e
    # -lgcc li risolve ld, e ld cerca solo dove gli si dice con -L.
    #
    # !! SI DICE ESPLICITAMENTE invece di lasciarlo succedere. La riga
    # fbcAddLibPathFor("libgcc.a") qui sotto aggiungerebbe la stessa directory
    # come effetto collaterale, visto che libgcc.a sta li': ma sarebbe una
    # coincidenza, e una coincidenza smette di funzionare il giorno che
    # qualcuno sposta libgcc senza sapere che teneva in piedi anche la libc.
    # -------------------------------------------------------------------------
    (
        "src/compiler/fbc.bas",
        """	'' and the current path
	fbcAddDefLibPath( "." )""",
        """	'' and the current path
	fbcAddDefLibPath( "." )

	'' EX-OS: <prefisso>/lib, dove stanno la libc, la libm e libgcc.
	if( fbGetOption( FB_COMPOPT_TARGET ) = FB_COMPTARGET_EXOS ) then
		fbcAddDefLibPath( fbc.prefix + "lib" )
	end if""",
    ),

    # -------------------------------------------------------------------------
    # 13-20. Gli header .bi che descrivono la libreria C
    #
    # Ognuno di questi file e' uno SMISTATORE: guarda la piattaforma e include
    # il file vero. Senza un ramo `exos` si finisce su `#error Platform
    # unsupported`, oppure — peggio — su un ramo che non definisce i tipi e
    # l'errore parla di tutt'altro.
    #
    # !! I FILE VERI SONO NOSTRI E STANNO IN crt-exos/, non si aliasano a
    # quelli di Linux. Il perche' e' scritto in testa a crt-exos/sys/types.bi:
    # time_t qui e' di 8 byte e li' di 4, e un tipo sbagliato non da' un
    # errore, da' numeri sbagliati.
    # -------------------------------------------------------------------------
    (
        "inc/crt/sys/types.bi",
        """#elseif defined(__FB_LINUX__)
#include once "crt/sys/linux/types.bi"
#else""",
        """#elseif defined(__FB_LINUX__)
#include once "crt/sys/linux/types.bi"
#elseif defined(__FB_EXOS__)
#include once "crt/sys/exos/types.bi"
#else""",
    ),
    (
        "inc/crt/time.bi",
        '#elseif defined(__FB_LINUX__)\n#include once "crt/linux/time.bi"\n',
        '#elseif defined(__FB_LINUX__)\n#include once "crt/linux/time.bi"\n'
        '#elseif defined(__FB_EXOS__)\n#include once "crt/exos/time.bi"\n',
    ),
    (
        "inc/crt/stdio.bi",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/stdio.bi\"""",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/stdio.bi"
#elseif defined(__FB_EXOS__)
#include once "crt/exos/stdio.bi\"""",
    ),
    (
        "inc/crt/stdlib.bi",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/stdlib.bi\"""",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/stdlib.bi"
#elseif defined(__FB_EXOS__)
#include once "crt/exos/stdlib.bi\"""",
    ),
    (
        "inc/crt/ctype.bi",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/ctype.bi\"""",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/ctype.bi"
#elseif defined(__FB_EXOS__)
#include once "crt/exos/ctype.bi\"""",
    ),
    (
        "inc/crt/fcntl.bi",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/fcntl.bi\"""",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/fcntl.bi"
#elseif defined(__FB_EXOS__)
#include once "crt/exos/fcntl.bi\"""",
    ),
    (
        "inc/crt/wchar.bi",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/wchar.bi\"""",
        """#elseif defined(__FB_LINUX__)
#include once "crt/linux/wchar.bi"
#elseif defined(__FB_EXOS__)
#include once "crt/exos/wchar.bi\"""",
    ),

    # errno: qui non basta includere, i VALORI sono diversi. Si salta in
    # blocco la tabella generica e si usa la nostra. Vedi crt-exos/errno.bi.
    (
        "inc/crt/errno.bi",
        """#define EPERM 1""",
        """#ifdef __FB_EXOS__
'' I valori di EX-OS sono quelli di Linux, non quelli di MSVC che seguono:
'' sei codici non coincidono. Vedi crt/exos/errno.bi.
#include once "crt/exos/errno.bi"
#else
#define EPERM 1""",
    ),
    (
        "inc/crt/errno.bi",
        """#define EILSEQ 42

extern "C\"""",
        """#define EILSEQ 42
#endif '' __FB_EXOS__

extern "C\"""",
    ),

    # -------------------------------------------------------------------------
    # 21-25. Il generatore di codice x86: `exos` emette come `linux`.
    #
    # Sono le direttive ELF — .rodata, .type, .size, i flag di sezione — e su
    # EX-OS servono esattamente come su Linux, perche' il formato e' lo stesso.
    # Senza, si otterrebbero oggetti che si assemblano e si collegano male.
    # -------------------------------------------------------------------------
    (
        "src/compiler/emit_x86.bas",
        """	if( env.clopt.target = FB_COMPTARGET_LINUX ) then
		_setSection( IR_SECTION_CONST, 0 )""",
        """	if( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _
	    (env.clopt.target = FB_COMPTARGET_EXOS) ) then
		_setSection( IR_SECTION_CONST, 0 )""",
    ),
    (
        "src/compiler/emit_x86.bas",
        """	if( env.clopt.target = FB_COMPTARGET_LINUX ) then
    	outEx( ".size " + *symbGetMangledName( proc ) + ", .-" + *symbGetMangledName( proc ) + NEWLINE )""",
        """	if( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _
	    (env.clopt.target = FB_COMPTARGET_EXOS) ) then
    	outEx( ".size " + *symbGetMangledName( proc ) + ", .-" + *symbGetMangledName( proc ) + NEWLINE )""",
    ),
    (
        "src/compiler/emit_x86.bas",
        """	if( env.clopt.target = FB_COMPTARGET_LINUX ) then
		outEx( ".type " + *symbGetMangledName( proc ) + ", @function" + NEWLINE )""",
        """	if( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _
	    (env.clopt.target = FB_COMPTARGET_EXOS) ) then
		outEx( ".type " + *symbGetMangledName( proc ) + ", @function" + NEWLINE )""",
    ),
    (
        "src/compiler/emit_x86.bas",
        """		if( env.clopt.target = FB_COMPTARGET_LINUX ) then
			ostr += ", " + QUOTE + "aw" + QUOTE + ", @progbits"
		end if

	case IR_SECTION_DESTRUCTOR""",
        """		if( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _
		    (env.clopt.target = FB_COMPTARGET_EXOS) ) then
			ostr += ", " + QUOTE + "aw" + QUOTE + ", @progbits"
		end if

	case IR_SECTION_DESTRUCTOR""",
    ),
    (
        "src/compiler/emit_x86.bas",
        """		if( env.clopt.target = FB_COMPTARGET_LINUX ) then
			ostr += ", " + QUOTE +  "aw" + QUOTE + ", @progbits"
		end if

	end select""",
        """		if( (env.clopt.target = FB_COMPTARGET_LINUX) orelse _
		    (env.clopt.target = FB_COMPTARGET_EXOS) ) then
			ostr += ", " + QUOTE +  "aw" + QUOTE + ", @progbits"
		end if

	end select""",
    ),
]


def copia_header(albero, togli):
    """Mette (o toglie) crt/exos/ e crt/sys/exos/ dentro inc/.

    ! SI COPIA, NON SI COLLEGA. Un collegamento simbolico si romperebbe il
    giorno che l'albero di FreeBASIC viene spostato o impacchettato, e il
    sintomo sarebbe un header che non c'e' — cioe' di nuovo «Platform
    unsupported», con in piu' il dubbio di aver sbagliato la patch.
    """
    coppie = [
        (os.path.join(QUI, "crt-exos"), os.path.join(albero, "inc", "crt", "exos")),
        (os.path.join(QUI, "crt-exos", "sys"),
         os.path.join(albero, "inc", "crt", "sys", "exos")),
    ]

    if togli:
        for _, dst in coppie:
            if os.path.isdir(dst):
                shutil.rmtree(dst)
        print("  header crt/exos: tolti")
        return 0

    n = 0
    for sorg, dst in coppie:
        os.makedirs(dst, exist_ok=True)
        for nome in sorted(os.listdir(sorg)):
            p = os.path.join(sorg, nome)
            if not os.path.isfile(p) or not nome.endswith(".bi"):
                continue
            shutil.copy2(p, os.path.join(dst, nome))
            n += 1
    print("  header crt/exos: %d file" % n)
    return 0


def applica(albero, togli):
    fatte = 0
    gia = 0

    for rel, cerca, metti in MODIFICHE:
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
        # albero tolto A META', che si compila lo stesso e sbaglia altrove —
        # su 26 modifiche ne restavano dentro 12.
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

        if testo.count(da) != 1:
            # ! ZERO O PIU' DI UNO SONO ENTRAMBI UN ERRORE, e vanno detti.
            # Zero: il sorgente e' cambiato sotto e la modifica NON e' stata
            # applicata — proseguire darebbe un albero a meta'. Piu' di uno:
            # non si sa quale sia quello giusto, e sceglierne uno a caso e'
            # peggio che fermarsi.
            print("  ! %s: trovato %d volte invece di 1" % (rel, testo.count(da)))
            print("    il testo cercato comincia con:")
            print("      %s" % da.strip().splitlines()[0][:70])
            return 1

        with open(percorso, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(testo.replace(da, a))
        fatte += 1

    verso = "tolto" if togli else "messo"
    print("  %d modifiche applicate, %d gia' a posto (%s)" % (fatte, gia, verso))
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    togli = "--togli" in sys.argv[1:]

    if len(args) != 1:
        print(__doc__)
        return 1

    albero = args[0]
    if not os.path.isdir(os.path.join(albero, "src", "compiler")):
        print("Non sembra un albero di FreeBASIC: manca src/compiler in %s" % albero)
        return 1

    print("Bersaglio `exos` nel compilatore e negli header: %s" % albero)
    rc = applica(albero, togli)
    if rc != 0:
        return rc
    rc = copia_header(albero, togli)
    if rc != 0:
        return rc

    # fbextra.x: lo script che butta via la sezione .fbctinf.
    #
    # ! SERVE SUL BERSAGLIO, non qui. fbc lo passa a ld a ogni collegamento
    # (fbc.bas, il blocco dell'objinfo) prendendolo da <libpath>: se manca,
    # ld si ferma su "cannot open linker script file" — un errore che parla
    # di uno script e non del fatto che nessuno l'ha installato.
    # Lo copia prepara-fb.sh; qui si controlla solo che ci sia da copiare.
    sorgente = os.path.join(albero, "lib", "fbextra.x")
    if not togli and not os.path.isfile(sorgente):
        print("  ! attenzione: manca %s" % sorgente)
        print("    fbc lo passa a ld a ogni collegamento: senza, il link"
              " fallisce")
        return 1

    if not togli:
        print("\nAdesso `fbc -target exos` esiste. Per usarlo serve un fbc")
        print("ricostruito da questi sorgenti: vedi bootstrap-exos.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

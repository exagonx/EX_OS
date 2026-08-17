#!/usr/bin/env python3
"""genlibc.py — genera i due lati della libc condivisa a partire dai SIMBOLI

    python3 tools/genlibc.py build/obj/libc_per_so.o build/gen

scrive:
    build/gen/libc_esporta.S    la tabella dei nomi, dentro libc.so
    build/gen/libc_ponti.S      i ponti, dentro ogni programma che la usa

! L'ELENCO NON SI SCRIVE A MANO. Sono 313 funzioni: una lista scritta a mano
sarebbe rimasta indietro alla prima aggiunta, e una funzione che manca in un
solo lato non da' un errore di compilazione — da' un salto a zero al primo
programma che la chiama. Qui l'elenco viene da `nm` sull'oggetto vero, quindi
non puo' divergere da cio' che la libc contiene davvero.

! I PONTI SONO IN ASSEMBLY, E NON E' VEZZO. Un ponte scritto in C dovrebbe
conoscere la FIRMA di ogni funzione per riceverne gli argomenti e ripassarli:
313 firme copiate a mano, e ognuna sbagliabile in silenzio. Un `jmp` indiretto
non tocca ne' gli argomenti ne' il valore di ritorno — li lascia esattamente
dove sono. E' lo stesso mestiere che fa una PLT.

! DUE NOMI RESTANO FUORI, e stanno scritti qui sotto: _libc_start e
_libc_distruttori appartengono al PROGRAMMA, non alla libreria. Vedi
lib/libc_avvio.c.
"""
import os
import subprocess
import sys

# ! CHI NON PASSA DA QUI, e ogni riga ha il suo motivo.
FUORI = {
    # Toccano `main` e i vettori dei costruttori, che sono del programma.
    "_libc_start",
    "_libc_distruttori",
    # Il gancio che aggancia la libreria: se fosse un ponte, per chiamarlo
    # servirebbe la libreria che deve ancora agganciare.
    "__libc_ponti_avvia",
}


def simboli(oggetto):
    """I nomi delle funzioni definite nell'oggetto, in ordine stabile.

    Si prendono solo 'T' (testo) e 'W' (weak: sqrt, fabs... sono funzioni a
    tutti gli effetti). I simboli di DATO — errno, stdin, stdout, stderr,
    environ — restano fuori apposta: una variabile non si raggiunge con un
    salto. Per quelle ci sono gli accessori __*_dove(), che essendo funzioni
    passano di qui come tutte le altre.
    """
    out = subprocess.run(["nm", "--defined-only", "-g", oggetto],
                         capture_output=True, text=True, check=True).stdout
    nomi = []
    for riga in out.splitlines():
        parti = riga.split()
        if len(parti) != 3:
            continue
        tipo, nome = parti[1], parti[2]
        if tipo not in ("T", "W"):
            continue
        if nome in FUORI:
            continue
        nomi.append(nome)

    # Ordinati: due esecuzioni devono dare lo stesso file, o ogni ricostruzione
    # sembrerebbe un cambiamento.
    return sorted(set(nomi))


TESTA_ESPORTA = """/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * La tabella che libc.so mette a disposizione: %d funzioni, ognuna col suo
 * nome. Chi la legge risolve per NOME, quindi l'ordine di questo file non e'
 * parte dell'ABI e puo' cambiare a ogni ricostruzione senza rompere niente.
 */
"""

TESTA_PONTI = """/* GENERATO DA tools/genlibc.py — NON MODIFICARE A MANO.
 *
 * I ponti verso libc.so: %d salti indiretti, uno per funzione.
 *
 * __libc_ponti_nomi e' un BLOCCO di stringhe una dopo l'altra, nello stesso
 * ordine della tabella: si scorre avanzando di strlen+1.
 *
 * Ogni ponte e' un `jmp *tabella+N`, e non tocca ne' gli argomenti ne' il
 * valore di ritorno: e' il motivo per cui questo file puo' esistere senza
 * conoscere una sola firma. La tabella la riempie __libc_ponti_avvia() prima
 * che il programma esegua qualunque altra cosa.
 */
"""


def scrivi_esporta(percorso, nomi):
    r = [TESTA_ESPORTA % len(nomi)]
    r.append('    .section .rodata\n    .align 4\n')
    for i, n in enumerate(nomi):
        r.append('ln%d:\n    .asciz "%s"\n' % (i, n))

    r.append('\n    .align 4\nlibc_nomi:\n')
    for i in range(len(nomi)):
        r.append("    .long ln%d\n" % i)

    r.append('\n    .align 4\nlibc_indirizzi:\n')
    for n in nomi:
        r.append("    .long %s\n" % n)

    # La testa sta nella sua sezione, che il linker script tiene con KEEP() e
    # mette esattamente alla base: vedi lib/exlib/exlib.h.
    r.append('\n    .section .exlib_testa,"a"\n    .align 4\n')
    r.append('    .globl libc_tabella\nlibc_tabella:\n')
    r.append("    .long 0x424C5845        /* EXLIB_MAGIA, 'EXLB' */\n")
    r.append("    .long 1                 /* EXLIB_VERSIONE */\n")
    r.append("    .long %d\n" % len(nomi))
    r.append("    .long libc_nomi\n")
    r.append("    .long libc_indirizzi\n")
    r.append('\n    .section .note.GNU-stack,"",@progbits\n')
    open(percorso, "w").write("".join(r))


def scrivi_ponti(percorso, nomi):
    r = [TESTA_PONTI % len(nomi)]

    r.append('\n    .section .bss\n    .align 4\n')
    r.append('    .globl __libc_ponti_tabella\n__libc_ponti_tabella:\n')
    r.append("    .space %d\n" % (4 * len(nomi)))

    # ! I NOMI SONO UN BLOCCO UNICO, NON UN VETTORE DI PUNTATORI. Un vettore
    # di 322 puntatori sono 1288 byte in ogni programma, e non servono a
    # niente: le stringhe stanno gia' una dopo l'altra, separate dallo zero
    # finale, e chi le legge avanza di strlen+1. Mille e trecento byte per
    # programma, su una quarantina di programmi, sono cinquanta kilobyte su un
    # floppy da 1.44 MB.
    r.append('\n    .section .rodata\n    .align 4\n')
    r.append('    .globl __libc_ponti_nomi\n__libc_ponti_nomi:\n')
    for n in nomi:
        r.append('    .asciz "%s"\n' % n)

    r.append('\n    .align 4\n    .globl __libc_ponti_quanti\n__libc_ponti_quanti:\n')
    r.append("    .long %d\n" % len(nomi))

    # ! OGNI PONTE IN UNA SEZIONE SUA, e non e' ordine: cosi' --gc-sections
    # butta i ponti delle funzioni che questo programma non chiama. Tutti in
    # .text sarebbero 322 salti in ogni binario, anche in `hello`.
    for i, n in enumerate(nomi):
        r.append('    .section .text.%s,"ax",@progbits\n' % n)
        r.append("    .globl %s\n    .type %s, @function\n%s:\n"
                 "    jmp *__libc_ponti_tabella+%d\n"
                 "    .size %s, .-%s\n\n" % (n, n, n, 4 * i, n, n))

    r.append('    .section .note.GNU-stack,"",@progbits\n')
    open(percorso, "w").write("".join(r))


def main():
    if len(sys.argv) != 3:
        print("uso: genlibc.py <libc.o> <directory di uscita>", file=sys.stderr)
        return 1

    oggetto, uscita = sys.argv[1], sys.argv[2]
    os.makedirs(uscita, exist_ok=True)

    nomi = simboli(oggetto)
    if len(nomi) < 100:
        # Una libc con dieci simboli vuol dire che nm ha letto il file
        # sbagliato: meglio fermarsi che generare ponti a meta'.
        print("genlibc: solo %d funzioni in %s: mi fermo" % (len(nomi), oggetto),
              file=sys.stderr)
        return 1

    scrivi_esporta(os.path.join(uscita, "libc_esporta.S"), nomi)
    scrivi_ponti(os.path.join(uscita, "libc_ponti.S"), nomi)
    print("genlibc: %d funzioni condivise" % len(nomi))
    return 0


sys.exit(main())

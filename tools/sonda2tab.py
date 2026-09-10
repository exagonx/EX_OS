#!/usr/bin/env python3
# =============================================================================
# tools/sonda2tab.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# DA UN REFERTO DELLA SONDA A UNA TABELLA DI REGISTRI IN C
#
#     tools/sonda2tab.py sonda/SONDA3.TXT grafica > drivers/sis/sis_tab.h
#
# ! PERCHE' NON SI SCRIVONO A MANO. Sono circa centoventi valori per modalita',
# letti dal ferro: ricopiarli a mano vuol dire sbagliarne uno, e un registro
# sbagliato non da' un errore — da' uno schermo storto, o nero, su una macchina
# che non si puo' emulare.
#
# ! E NON SI PRENDONO TUTTI. Dei 256 registri del CRTC di una SiS, moltissimi
# sono di sola lettura o riportano stati che rileggerli non riproduce:
# riscriverli alla cieca vuol dire mandare la scheda in uno stato che il BIOS
# non le ha mai chiesto. Si prendono quelli standard VGA — che sono definiti da
# quarant'anni e si scrivono tutti — piu' l'elenco degli estesi che CAMBIANO
# fra le due modalita', che e' l'unica misura oggettiva di «questo serve».
# =============================================================================

import re
import sys

# I registri estesi SiS che cambiano fra testo e 800x600 su un Acer Aspire
# 3000.
#
# ! L'ELENCO E' UNA MISURA, MA CI VOGLIONO QUATTRO REFERTI PER FARLA, NON DUE.
# Con un referto per modalita' la sottrazione dice «questi sono diversi», e ci
# finisce dentro anche il RUMORE: su questa scheda trentadue registri cambiano
# da un avvio all'altro NELLA STESSA MODALITA' — CR8a, CR94-97, la fascia
# CRc0-CRf1, SR3e — perche' riportano stati, non impostazioni. Rileggerli e
# riscriverli non riproduce niente: rimette alla scheda una fotografia di un
# momento che non c'e' piu'.
#
# Con DUE referti per modalita' il rumore si riconosce e si toglie: resta cio'
# che e' STABILE dentro ogni modalita' e DIVERSO fra le due. Da 47 registri
# «diversi» a 52 che contano davvero — otto buttati via e tredici trovati
# (CRd1, CRd2, CRd4, CRd9 non comparivano nella prima sottrazione).
#
# ! E IL TERZO REFERTO NE HA TOLTO ANCORA UNO. Con SONDA5.TXT — 10 settembre
# 2026, terzo giro in 800x600 — CR1a e' uscito fc dove i due precedenti
# dicevano 5c: era rumore anche lui, e con due referti soli sembrava un
# registro di modalita' perche' per due volte di fila era caduto uguale. Il
# rumore non si dimostra in due misure: si dimostra continuando a misurare.
SR_ESTESI = [0x06, 0x08, 0x09, 0x0a, 0x0c, 0x0e, 0x10, 0x20, 0x21, 0x3d]
CR_ESTESI = [0x22, 0x34, 0xd1, 0xd2, 0xd4, 0xd9]

# ! IL RUMORE, SCRITTO PER NOME perche' non ci torni dentro da solo il giorno
# che qualcuno rifa' la sottrazione con due referti soli.
CR_RUMORE = [0x1a, 0x8a, 0x94, 0x95, 0x96, 0x97, 0xc0, 0xc5, 0xc6, 0xcd,
             0xce, 0xd5, 0xd8, 0xdd, 0xde, 0xe0, 0xe1, 0xe2, 0xec, 0xed, 0xf1]


def banchi(percorso):
    d, misc = {}, None
    for r in open(percorso, errors="replace"):
        m = re.match(r"^Misc Output \(0x3CC\): ([0-9a-f]{2})", r.strip())
        if m:
            misc = int(m.group(1), 16)
        m = re.match(r"^(SR|CR|GR|AR) ([0-9a-f]{2}): (.*)$", r.strip())
        if m:
            base = int(m.group(2), 16)
            for i, v in enumerate(m.group(3).split()):
                d[(m.group(1), base + i)] = int(v, 16)
    return misc, d


def voci(d, banco, indici):
    return [(i, d[(banco, i)]) for i in indici if (banco, i) in d]


def stampa(nome, elenco):
    print("static const SisReg %s[] = {" % nome)
    for i, v in elenco:
        print("    { 0x%02x, 0x%02x }," % (i, v))
    print("};")
    print("#define %s_N ((int)(sizeof(%s) / sizeof(%s[0])))"
          % (nome.upper(), nome, nome))
    print()


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: sonda2tab.py <referto> <nome>")

    misc, d = banchi(sys.argv[1])
    nome = sys.argv[2]

    if misc is None:
        sys.exit("referto senza Misc Output: non e' un referto della sonda")

    print("/* Generato da tools/sonda2tab.py: NON si modifica a mano. */")
    print("/* Sorgente: %s */" % sys.argv[1])
    print()
    print("#define MISC_%s 0x%02x" % (nome.upper(), misc))
    print()

    # ! IL SEQUENZIATORE 00-04 E' STANDARD VGA, dal 05 in su e' SiS.
    stampa("sr_" + nome,
           voci(d, "SR", list(range(0x00, 0x05)) + SR_ESTESI))
    # ! DEL CRTC SI PRENDONO I 25 STANDARD PIU' GLI ESTESI CHE CAMBIANO.
    # ! CR24 SI SALTA: e' quello che il cursore del kernel muove mentre la
    # sonda legge, ed e' l'unico che il referto dichiara instabile.
    cr = [i for i in range(0x00, 0x19) if i != 0x11] + CR_ESTESI
    stampa("cr_" + nome, voci(d, "CR", cr))
    stampa("gr_" + nome, voci(d, "GR", list(range(0x00, 0x09))))
    # ! DELL'ATTRIBUTO SI PRENDE DA AR10 IN SU, MAI LA TAVOLOZZA.
    # AR00..AR0F sono la tavolozza, e nei referti presi prima del 10 settembre
    # 2026 sono sedici 04 in fila: sonda.drv li rileggeva con il bit 5
    # dell'indice acceso, e con quel bit acceso non sono leggibili. Riscritti,
    # mandano tutti i colori del testo sull'indice 4 del DAC — rosso — e lo
    # schermo diventa rosso pieno. La tavolozza del testo la rimette il kernel
    # con i valori canonici, che sono quelli giusti su qualunque scheda.
    stampa("ar_" + nome, voci(d, "AR", list(range(0x10, 0x15))))


main()

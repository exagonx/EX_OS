#!/usr/bin/env python3
# =============================================================================
# tools/scava.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# =============================================================================
#
# CHE COSA FA UN BINARIO A UN DISPOSITIVO — la ROM di un BIOS video, o un
# driver Windows, letti per scriverne uno nostro.
#
#     scava.py VIDEO.BIN                  la ROM presa con sonda.drv -rom
#     scava.py sisgrp.sys                 un driver Windows (PE)
#     scava.py VIDEO.BIN -porte 3c4,3d4   solo chi tocca quelle porte
#     scava.py VIDEO.BIN -asm 0x1a40      il codice a quell'indirizzo
#
# -----------------------------------------------------------------------------
# ! PERCHE' E' LEGITTIMO, VISTO CHE LA DOMANDA ARRIVA SEMPRE
#
# Leggere un binario per capire come parla a un dispositivo, allo scopo di
# scrivere un programma che con quel dispositivo lavori, e' l'eccezione di
# INTEROPERABILITA': in Europa la Direttiva 2009/24/CE articolo 6 la nomina
# per esteso, e negli Stati Uniti il DMCA ha la sua. E' il modo in cui e' nato
# mezzo kernel Linux.
#
# Quello che NON si fa e' ricopiare il codice trovato. Da qui esce una
# DESCRIZIONE — quali porte, quali valori, in che ordine — e da quella si
# scrive codice proprio. E' la differenza fra leggere un libro e fotocopiarlo.
#
# -----------------------------------------------------------------------------
# ! DUE BERSAGLI, E IL PRIMO E' MOLTO PIU' FACILE
#
# La ROM del BIOS video sono 32 o 64 KB di codice a 16 bit che fa una cosa
# sola: impostare le modalita' di QUELLA scheda. Le tabelle sono li' dentro, e
# il codice che le applica pure.
#
# Un driver Windows sono megabyte di codice a 32 bit dove il modeset e' una
# parte fra tante — ma e' l'unico posto dove sta il motore 2D, che nella ROM
# non c'e' perche' al BIOS non serve.
#
# -----------------------------------------------------------------------------
# ! COSA CERCA, E PERCHE' PROPRIO QUESTO
#
# Un driver video parla all'hardware in tre modi, e sono tre soli:
#
#   1. PORTE. `out dx, al` dopo aver messo un numero in dx. Trovare quel
#      numero vuol dire sapere quale registro sta scrivendo.
#   2. MEMORIA. `mov [reg+0x8200], eax` — l'offset dentro la finestra dice
#      quale registro del motore 2D.
#   3. TABELLE. Le sequenze indice/valore non sono nel codice: sono nei dati,
#      e il codice le percorre in ciclo. Sono la cosa piu' preziosa e quella
#      che una lettura a occhio non trova mai.
#
# ! E LE COPPIE INDICE/VALORE SI RICONOSCONO DA UNA REGOLARITA', non da un
# indovinello: in una tabella di modeset gli indici salgono quasi sempre di uno
# e i valori no. Si cerca quello.
# =============================================================================
import os
import re
import subprocess
import sys


def esadecimale(n):
    return "0x%x" % n


# =============================================================================
# PE: l'involucro dei binari Windows
#
# ! NON SERVE UNA LIBRERIA, e non e' orgoglio: le tre strutture che servono
# sono sessanta righe, e una dipendenza in piu' e' una cosa che chi clona il
# repository deve installare prima di poter leggere un file.
# =============================================================================
class PE:
    def __init__(self, dati):
        self.dati = dati
        self.sezioni = []
        self.base = 0
        self.macchina = 0
        self.importazioni = {}
        self.iat = {}
        self.valido = False

        if len(dati) < 0x40 or dati[:2] != b"MZ":
            return

        pe = int.from_bytes(dati[0x3C:0x40], "little")
        if pe + 24 > len(dati) or dati[pe:pe + 4] != b"PE\0\0":
            return

        self.macchina = int.from_bytes(dati[pe + 4:pe + 6], "little")
        n_sez = int.from_bytes(dati[pe + 6:pe + 8], "little")
        dim_opt = int.from_bytes(dati[pe + 20:pe + 22], "little")
        opt = pe + 24

        magic = int.from_bytes(dati[opt:opt + 2], "little")
        if magic == 0x10B:                      # PE32
            self.base = int.from_bytes(dati[opt + 28:opt + 32], "little")
        elif magic == 0x20B:                    # PE32+
            self.base = int.from_bytes(dati[opt + 24:opt + 32], "little")

        tab = opt + dim_opt
        for i in range(n_sez):
            e = tab + i * 40
            if e + 40 > len(dati):
                break
            nome = dati[e:e + 8].rstrip(b"\0").decode("latin1")
            vsize = int.from_bytes(dati[e + 8:e + 12], "little")
            vaddr = int.from_bytes(dati[e + 12:e + 16], "little")
            rsize = int.from_bytes(dati[e + 16:e + 20], "little")
            roff = int.from_bytes(dati[e + 20:e + 24], "little")
            flag = int.from_bytes(dati[e + 36:e + 40], "little")
            self.sezioni.append({
                "nome": nome, "vaddr": vaddr, "vsize": vsize,
                "roff": roff, "rsize": rsize,
                "codice": bool(flag & 0x20000020),
            })

        # Le importazioni: la SECONDA voce della tabella delle directory,
        # cioe' +8 rispetto all'inizio. La prima e' l'export, e prenderla per
        # l'import da' zero librerie senza dare errore.
        if magic in (0x10B, 0x20B):
            dd = opt + (96 if magic == 0x10B else 112) + 8
            if dd + 8 <= len(dati):
                irva = int.from_bytes(dati[dd:dd + 4], "little")
                self.importazioni = self._importazioni(irva)

        self.valido = True

    def _rva(self, rva):
        """Da indirizzo virtuale a posizione nel file."""
        for s in self.sezioni:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s["roff"] + (rva - s["vaddr"])
        return None

    def _stringa(self, off):
        if off is None or off >= len(self.dati):
            return ""
        fine = self.dati.find(b"\0", off)
        return self.dati[off:fine if fine > 0 else off].decode("latin1")

    def _importazioni(self, irva):
        """Quali funzioni, da quali librerie.

        ! E' LA DOMANDA PIU' UTILE CHE SI POSSA FARE A UN DRIVER WINDOWS, e la
        prima versione di questo strumento non la faceva. Un driver di kernel
        non scrive `out dx, al`: chiama VideoPortWritePortUchar, o
        WRITE_PORT_UCHAR della HAL. Cercare le istruzioni `out` in un binario
        cosi' non trova niente e fa concludere che il driver non tocchi
        l'hardware — che e' l'opposto della verita'.

        L'elenco delle importazioni dice in una schermata COME quel driver
        parla al ferro, e quindi dove guardare.
        """
        fuori = {}
        off = self._rva(irva)
        if off is None:
            return fuori

        for i in range(64):                     # 64 librerie bastano e avanzano
            e = off + i * 20
            if e + 20 > len(self.dati):
                break
            ori = int.from_bytes(self.dati[e:e + 4], "little")
            nome_rva = int.from_bytes(self.dati[e + 12:e + 16], "little")
            primo = int.from_bytes(self.dati[e + 16:e + 20], "little")
            if ori == 0 and nome_rva == 0 and primo == 0:
                break

            lib = self._stringa(self._rva(nome_rva)).lower()
            funz = []
            t = self._rva(ori or primo)
            if t is None:
                continue

            passo = 8 if self.macchina == 0x8664 else 4
            for k in range(2048):
                q = t + k * passo
                if q + passo > len(self.dati):
                    break
                v = int.from_bytes(self.dati[q:q + passo], "little")
                if v == 0:
                    break
                # il bit alto acceso vuol dire «per numero, non per nome»
                if v & (1 << (passo * 8 - 1)):
                    continue
                n = self._stringa(self._rva(v + 2))
                if n:
                    funz.append(n)
                    # ! DOVE STA IL PUNTATORE, non solo come si chiama. Il
                    # codice non chiama la funzione per nome: fa
                    # `call [0x1a2b4]`, e quell'indirizzo e' la casella della
                    # tabella che Windows riempira' al caricamento. Senza
                    # questa mappa, una chiamata a WRITE_PORT_ULONG e' un
                    # numero esadecimale come tanti.
                    self.iat[self.base + primo + k * passo] = n

            if lib:
                fuori.setdefault(lib, []).extend(funz)

        return fuori

    def nome_macchina(self):
        return {0x14C: "i386", 0x8664: "x86-64", 0x1C0: "ARM",
                0x1C4: "ARMv7", 0xAA64: "ARM64"}.get(self.macchina,
                                                     "0x%x" % self.macchina)


# =============================================================================
# Il disassemblatore: non lo scrivo io
#
# ! ndisasm PER IL CODICE A 16 BIT, objdump PER QUELLO A 32. Scrivere un
# disassemblatore x86 e' un progetto a se' — i prefissi, le due lunghezze di
# operando, ModRM, SIB — e sarebbe un progetto con dei bug proprio nei casi
# strani, che sono quelli che contano quando si legge una ROM.
# =============================================================================
def disassembla(grezzo, bit, base):
    if bit == 16:
        if not eseguibile("ndisasm"):
            sys.exit("scava: serve ndisasm (sudo apt install nasm)")
        p = subprocess.run(["ndisasm", "-b", "16", "-o", str(base), "-"],
                           input=grezzo, capture_output=True)
        righe = []
        for r in p.stdout.decode("latin1").splitlines():
            m = re.match(r"^([0-9A-F]+)\s+([0-9A-F]+)\s+(.*)$", r)
            if m:
                righe.append((int(m.group(1), 16), m.group(3).strip()))
        return righe

    if not eseguibile("objdump"):
        sys.exit("scava: serve objdump (sudo apt install binutils)")

    tmp = "/tmp/scava-%d.bin" % os.getpid()
    with open(tmp, "wb") as f:
        f.write(grezzo)
    try:
        p = subprocess.run(
            ["objdump", "-D", "-b", "binary", "-m", "i386",
             "-M", "intel", "--adjust-vma=" + str(base), tmp],
            capture_output=True)
    finally:
        os.unlink(tmp)

    righe = []
    for r in p.stdout.decode("latin1").splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(.+)$", r)
        if m:
            righe.append((int(m.group(1), 16), m.group(2).strip()))
    return righe


def eseguibile(nome):
    for d in os.environ.get("PATH", "").split(":"):
        if os.path.isfile(os.path.join(d, nome)):
            return True
    return False


# =============================================================================
# 0. LE IMPORTAZIONI — come questo binario parla al ferro
#
# ! LA PRIMA DOMANDA DA FARE, E LA PRIMA VERSIONE DI QUESTO STRUMENTO NON LA
# FACEVA. Cercando istruzioni `out` in sisgrp.sys — il driver video di questo
# portatile — non usciva NIENTE, e la conclusione naturale sarebbe stata «non
# tocca le porte». E' l'opposto della verita': le tocca chiamando
# VideoPortWritePortUchar, perche' un driver di kernel Windows fa cosi'.
#
# L'elenco delle funzioni importate dice in una schermata come quel binario
# parla all'hardware, e quindi dove guardare — o se guardare.
# =============================================================================
IMPORTAZIONI_NOTE = {
    "videoportwriteportuchar":
        ("porte", "scrive su una porta: le scritture ai registri VGA passano "
                  "di qui, non da `out`"),
    "videoportreadportuchar":
        ("porte", "legge una porta"),
    "writeportuchar": ("porte", "scrive su una porta (HAL)"),
    "readportuchar":  ("porte", "legge una porta (HAL)"),
    "write_port_uchar":  ("porte", "scrive un byte su una porta (HAL)"),
    "read_port_uchar":   ("porte", "legge un byte da una porta (HAL)"),
    "write_port_ushort": ("porte", "scrive due byte su una porta (HAL)"),
    "read_port_ushort":  ("porte", "legge due byte da una porta (HAL)"),
    "write_port_ulong":  ("porte", "scrive QUATTRO byte su una porta: i "
                                   "registri di questa scheda sono a 32 bit"),
    "read_port_ulong":   ("porte", "legge quattro byte da una porta"),
    "ndismregisterioportrange":
        ("porte", "si prende una fascia di porte: la base sta li'"),
    "ndismallocatesharedmemory":
        ("DMA", "! ANELLI DI DESCRITTORI. La scheda legge e scrive in memoria "
                "da sola: e' cosi' che si trasmette senza copiare"),
    "ndismmapiospace":
        ("memoria", "mappa una finestra della scheda"),
    "ndismregisterinterrupt":
        ("IRQ", "si aggancia a un interrupt"),
    "ndisreadnetworkaddress":
        ("MAC", "legge l'indirizzo MAC scritto nella configurazione"),
    "kestallexecutionprocessor":
        ("tempo", "aspetta microsecondi"),
    "videoportint10":
        ("BIOS", "! CHIAMA IL BIOS VIDEO con INT 10h. Se c'e', il modeset "
                 "probabilmente NON e' in questo file: e' nella ROM, e questo "
                 "si limita a chiederglielo"),
    "videoportgetromimage":
        ("BIOS", "si fa dare l'immagine della ROM video: conferma che le "
                 "tabelle stanno li'"),
    "videoportmapmemory":
        ("memoria", "mappa una finestra della scheda: da qui in poi i "
                    "registri si toccano come memoria"),
    "videoportgetaccessranges":
        ("PCI", "si fa dare le finestre dal PCI invece di scriverle a mano"),
    "videoportgetbusdata": ("PCI", "legge la configurazione PCI"),
    "videoportsetbusdata": ("PCI", "scrive la configurazione PCI"),
    "halgetbusdatabyoffset": ("PCI", "legge la configurazione PCI (HAL)"),
    "halsetbusdatabyoffset": ("PCI", "scrive la configurazione PCI (HAL)"),
    "videoportgetagpservices": ("AGP", "usa l'AGP"),
    "videoportstallexecution": ("tempo", "aspetta microsecondi"),
    "engmapfile":  ("memoria", "mappa un file"),
    "engdevicelocked": ("grafica", "driver di visualizzazione in spazio "
                                   "utente"),
}


def leggi_importazioni(pe):
    if not pe.importazioni:
        return

    print("\n" + "=" * 70)
    print("COME PARLA AL FERRO — le funzioni importate")
    print("=" * 70)

    interessanti = []
    for lib, funzioni in pe.importazioni.items():
        for f in funzioni:
            k = f.lower()
            if k in IMPORTAZIONI_NOTE:
                interessanti.append((f, lib) + IMPORTAZIONI_NOTE[k])

    if not interessanti:
        print("nessuna funzione di accesso all'hardware fra quelle note.")
        print("Librerie: %s" % ", ".join(sorted(pe.importazioni)))
        return

    for f, lib, genere, che in sorted(interessanti, key=lambda x: x[2]):
        print("  [%-7s] %-32s %s" % (genere, f, che))

    generi = {g for _, _, g, _ in interessanti}

    print("\n  Cosa vuol dire:")
    if "BIOS" in generi:
        print("  ! QUESTO BINARIO DELEGA AL BIOS. Cercarci dentro le tabelle")
        print("    di modalita' e' cercare una cosa che sta altrove: la ROM.")
        print("    Prendila con `sonda.drv -rom` e scavaci in quella.")
    if "porte" in generi:
        print("  - le porte le tocca via API: il numero della porta e' un")
        print("    ARGOMENTO, quindi l'analisi delle istruzioni `out` qui")
        print("    non trova niente ed e' giusto cosi'.")
    if "memoria" in generi:
        print("  - mappa finestre di memoria: i registri che tocca cosi' si")
        print("    vedono nella sezione degli offset, piu' sotto.")


# =============================================================================
# 1. LE PORTE
#
# ! IL NUMERO DELLA PORTA QUASI MAI STA NELL'ISTRUZIONE. `out dx, al` dice
# soltanto «scrivi dove punta dx»: il valore ci e' stato messo prima, e per
# saperlo bisogna guardare indietro. Si guardano venti istruzioni: piu' su, il
# valore probabilmente viene da un ciclo o da un parametro, e allora questa
# analisi non lo puo' sapere e non deve fingere di saperlo.
#
# ! E `out 0x3c4, al` DIRETTA ESISTE, ma solo per porte sotto 256. Le porte
# VGA stanno sopra, quindi sulla nostra strada passano tutte da dx.
# =============================================================================
INDIETRO = 20


def cerca_porte(righe, filtro=None):
    trovate = {}

    for i, (ind, testo) in enumerate(righe):
        m = re.match(r"^(in|out)\s+(.*)$", testo)
        if not m:
            continue
        verso, resto = m.group(1), m.group(2)

        # forma diretta: out 0x70, al
        d = re.match(r"^(?:0x([0-9a-f]+)|al|ax|eax)\s*,", resto)
        porta = None
        if d and d.group(1):
            porta = int(d.group(1), 16)
        elif "dx" in resto:
            # si guarda indietro chi ha caricato dx
            for j in range(i - 1, max(-1, i - INDIETRO - 1), -1):
                mm = re.match(r"^mov\s+e?dx\s*,\s*(?:word\s+)?0x([0-9a-f]+)",
                              righe[j][1])
                if mm:
                    porta = int(mm.group(1), 16) & 0xFFFF
                    break

        if porta is None:
            continue
        if filtro is not None and porta not in filtro:
            continue

        v = trovate.setdefault(porta, {"letture": 0, "scritture": 0,
                                       "dove": []})
        if verso == "in":
            v["letture"] += 1
        else:
            v["scritture"] += 1
        if len(v["dove"]) < 8:
            v["dove"].append(ind)

    return trovate


# Le porte che un driver video tocca, e cosa sono. Non e' cosmesi: sapere che
# 0x3C4 e' l'indice del sequenziatore trasforma un elenco di numeri in una
# descrizione di cosa il codice sta facendo.
PORTE_NOTE = {
    0x3B4: "CRTC indice (monocromatico)", 0x3B5: "CRTC dato (monocromatico)",
    0x3BA: "stato / feature (mono)",
    0x3C0: "attributo indice+dato",       0x3C1: "attributo lettura",
    0x3C2: "Misc Output (scrittura)",     0x3C4: "sequenziatore indice",
    0x3C5: "sequenziatore dato",          0x3C6: "maschera del DAC",
    0x3C7: "DAC indice lettura",          0x3C8: "DAC indice scrittura",
    0x3C9: "DAC dato",                    0x3CC: "Misc Output (lettura)",
    0x3CE: "controllore grafico indice",  0x3CF: "controllore grafico dato",
    0x3D4: "CRTC indice",                 0x3D5: "CRTC dato",
    0x3DA: "stato 1 / azzera flip-flop",
    0xCF8: "PCI indirizzo",               0xCFC: "PCI dato",
}


# =============================================================================
# 1-bis. LE CHIAMATE ALL'API — dove un driver Windows nasconde le porte
#
# ! IL NUMERO DELLA PORTA E' UN ARGOMENTO, ED E' QUI CHE SI LEGGE. Un driver
# NDIS scrive un registro cosi':
#
#     lea  eax, [esi+0x24]        ; base della scheda + offset del registro
#     push ecx                    ; il valore
#     push eax                    ; la porta
#     call DWORD PTR [WRITE_PORT_ULONG]
#
# L'offset — 0x24 — E' IL NUMERO DEL REGISTRO, e sta in un'istruzione che di
# per se' non ha niente di speciale. Trova senso solo se si sa che tre
# istruzioni dopo c'e' quella chiamata.
#
# Percio' si cercano le chiamate alle funzioni di accesso, e per ognuna si
# guarda indietro cercando `lea reg, [reg+N]` o `add reg, N`. L'elenco degli N
# che ne esce E' LA MAPPA DEI REGISTRI di quella scheda.
#
# ! E NON SI INVENTA NIENTE QUANDO NON SI TROVA. Se prima della chiamata non
# c'e' un offset immediato, l'indirizzo viene da una variabile o da un ciclo, e
# questa analisi non lo puo' sapere: si conta fra le chiamate «senza offset
# visibile» e si va avanti. Un numero inventato in una mappa di registri e'
# peggio di un buco.
# =============================================================================
ACCESSO = ("write_port", "read_port", "writeport", "readport")


def trova_ponti(righe, iat):
    """Le funzioncine che il driver si scrive attorno alla HAL.

    ! SENZA QUESTO SI VEDONO TRE REGISTRI SU VENTI. Un driver Windows non
    chiama WRITE_PORT_ULONG dal punto in cui deve scrivere un registro: si
    scrive un ponte di sei istruzioni e chiama quello, perche' cosi' puo'
    infilarci una traccia di debug o un blocco. Cercare solo le chiamate
    dirette alla HAL trova i pochi punti che l'hanno saltato.

    Un ponte si riconosce da come e' fatto: comincia dopo un `ret`, e nelle
    sue prime istruzioni non fa altro che chiamare una funzione di accesso.
    Chi lo chiama sta scrivendo un registro tanto quanto.
    """
    ponti = {}
    inizio = None

    for i, (ind, testo) in enumerate(righe):
        if inizio is None:
            inizio = ind

        m = re.search(r"call\s+(?:DWORD PTR\s+)?(?:ds:)?(?:\[)?"
                      r"0x([0-9a-f]+)\]?", testo)
        if m:
            nome = iat.get(int(m.group(1), 16), "")
            if any(a in nome.lower() for a in ACCESSO):
                # ! SOLO SE E' VICINO ALL'INIZIO. Una funzione lunga che a
                # meta' scrive una porta non e' un ponte: e' una funzione che
                # fa il suo lavoro, e chi la chiama non sta scrivendo un
                # registro. Dodici istruzioni sono il tetto.
                if inizio is not None and i - indice_di(righe, inizio) <= 12:
                    ponti[inizio] = nome

        if re.match(r"^(ret|int3|jmp)\b", testo):
            inizio = None

    return ponti


def indice_di(righe, indirizzo):
    for i, (a, _) in enumerate(righe):
        if a == indirizzo:
            return i
    return 0


def cerca_chiamate(righe, iat, ponti=None):
    fuori = {}
    senza = 0
    ponti = ponti or {}

    for i, (ind, testo) in enumerate(righe):
        m = re.search(r"call\s+(?:DWORD PTR\s+)?(?:ds:)?"
                      r"(?:\[)?0x([0-9a-f]+)\]?", testo)
        if not m:
            continue

        bersaglio = int(m.group(1), 16)
        nome = iat.get(bersaglio) or ponti.get(bersaglio)
        if not nome:
            continue
        k = nome.lower()
        if not any(a in k for a in ACCESSO):
            continue

        # A ritroso: chi ha calcolato l'indirizzo della porta.
        #
        # ! I REGISTRI SONO A 16 BIT, E QUESTO SI SCOPRE GUARDANDO. La prima
        # versione cercava `add eax, 0x24` e trovava undici chiamate su
        # trentasette in un driver di rete. La forma vera, su questa scheda,
        # e' questa:
        #
        #     mov   ax, WORD PTR [esi+0xc0]   la base I/O, che e' un numero
        #     add   ax, 0xb0                  di porta e quindi sta in 16 bit
        #     movzx eax, ax
        #     push  <valore> ; push eax ; call WRITE_PORT_ULONG
        #
        # ! E QUANDO L'OFFSET E' ZERO NON C'E' NESSUN `add`. Il registro 0x00
        # e' quasi sempre quello di comando, cioe' il piu' importante: cercare
        # solo le somme lo salta sempre. Percio' il caricamento della base
        # conta come offset zero.
        off = None
        for j in range(i - 1, max(-1, i - 12), -1):
            testo_j = righe[j][1]

            mm = re.match(r"^(?:lea\s+e?[a-z]{2}\s*,\s*\[e?[a-z]{2}\s*\+\s*"
                          r"|add\s+e?[a-z]{2}\s*,\s*)0x([0-9a-f]+)", testo_j)
            if mm:
                off = int(mm.group(1), 16)
                break

            # la base caricata e nessuna somma dopo: registro zero
            if re.match(r"^mov(zx)?\s+e?[a-z]{2}\s*,\s*(?:WORD PTR\s*)?"
                        r"\[e[a-z]{2}\s*\+\s*0x[0-9a-f]+\]", testo_j):
                off = 0
                break

            # una `call` in mezzo interrompe: quel che c'e' prima e' di
            # un'altra faccenda.
            if "call" in testo_j:
                break

        if off is None or off > 0x200:
            senza += 1
            continue

        v = fuori.setdefault(off, {"scritture": 0, "letture": 0, "dove": []})
        if "write" in k:
            v["scritture"] += 1
        else:
            v["letture"] += 1
        if len(v["dove"]) < 6:
            v["dove"].append(ind)

    return fuori, senza


def leggi_chiamate(righe, pe):
    if not pe.iat:
        return

    ponti = trova_ponti(righe, pe.iat)
    offset, senza = cerca_chiamate(righe, pe.iat, ponti)
    if not offset and not senza:
        return

    print("\n" + "=" * 70)
    print("I REGISTRI, DEDOTTI DALLE CHIAMATE")
    print("=" * 70)

    if not offset:
        print("%d chiamate di accesso, nessuna con un offset immediato:" % senza)
        print("l'indirizzo viene da variabili. Serve seguirle a mano con -asm.")
        return

    if ponti:
        print("%d funzioni ponte attorno alla HAL: %s\n"
              % (len(ponti),
                 " ".join(esadecimale(p) for p in sorted(ponti)[:8])))

    print("offset dalla base della scheda; il numero e' il registro.\n")
    for o in sorted(offset):
        v = offset[o]
        print("  +0x%02x   %3d scritture, %3d letture   a: %s"
              % (o, v["scritture"], v["letture"],
                 " ".join(esadecimale(d) for d in v["dove"][:4])))

    if senza:
        print("\n  (%d chiamate senza offset visibile: l'indirizzo viene da" % senza)
        print("   una variabile o da un ciclo, e non lo si inventa.)")


# =============================================================================
# 2. LA MEMORIA
#
# Gli offset dentro una finestra mappata: `mov [eax+0x8200], ecx`. L'offset e'
# il numero del registro, ed e' la sola cosa che si puo' sapere senza seguire
# da dove viene il puntatore.
#
# ! GLI OFFSET PICCOLI NON SI CONTANO. Sotto 0x100 quasi ogni `[reg+n]` e' un
# campo di una struttura del driver, non un registro di una scheda: contarli
# significa annegare i venti che contano sotto duemila che non c'entrano.
# =============================================================================
def cerca_mmio(righe, minimo=0x100):
    trovati = {}

    for ind, testo in righe:
        for m in re.finditer(
                r"\[e?[a-d]x\s*\+\s*(?:e?[a-d]x\s*\*\s*\d\s*\+\s*)?"
                r"0x([0-9a-f]{3,8})\]", testo):
            off = int(m.group(1), 16)
            if off < minimo or off > 0x100000:
                continue
            v = trovati.setdefault(off, {"quante": 0, "dove": []})
            v["quante"] += 1
            if len(v["dove"]) < 4:
                v["dove"].append(ind)

    return trovati


# =============================================================================
# 3. LE TABELLE — la parte che vale il viaggio
#
# ! UNA TABELLA DI MODESET NON E' CODICE, e per questo un disassemblatore da
# solo non la trova: e' un blocco di dati che il codice percorre in ciclo. Ma
# ha una forma riconoscibile, ed e' questa: coppie (indice, valore) in cui gli
# INDICI salgono di uno e i VALORI no.
#
# Si scorre tutto il file cercando tratti in cui i byte di posto pari formano
# una successione crescente e regolare. Un tratto lungo abbastanza non capita
# per caso in dati qualunque.
#
# ! E SI CERCA ANCHE LA FORMA COMPATTA: certe tabelle non hanno l'indice,
# perche' i registri sono consecutivi e il codice parte da un indice fisso.
# Quelle si riconoscono da un'altra regolarita' — venticinque byte di seguito
# nella fascia dei valori del CRTC — e sono piu' incerte: si segnalano come
# sospette, non come trovate.
# =============================================================================
def cerca_tabelle(dati, minimo=6):
    tabelle = []
    i = 0
    n = len(dati)

    while i + minimo * 2 < n:
        # quanto dura la salita di uno negli indici a passo due?
        k = 0
        while (i + (k + 1) * 2 < n and
               dati[i + (k + 1) * 2] == (dati[i + k * 2] + 1) & 0xFF):
            k += 1

        if k + 1 >= minimo:
            coppie = [(dati[i + j * 2], dati[i + j * 2 + 1])
                      for j in range(k + 1)]
            valori = [v for _, v in coppie]

            # ! SE ANCHE I VALORI SALGONO DI UNO NON E' UNA TABELLA, e' una
            # rampa: 00 01 02 03... letta a coppie sembra indici e valori
            # crescenti. Una tavolozza lineare fa esattamente questo.
            rampa = all((valori[j + 1] - valori[j]) & 0xFF == 1
                        for j in range(len(valori) - 1))
            uguali = len(set(valori)) <= 2

            if not rampa and not uguali:
                tabelle.append({
                    "offset": i, "quante": k + 1,
                    "primo": coppie[0][0], "ultimo": coppie[-1][0],
                    "coppie": coppie,
                })
            i += (k + 1) * 2
        else:
            i += 1

    return tabelle


# =============================================================================
# 4. LE FILE DI SOLI VALORI — la forma vera delle tabelle VGA
#
# ! LA PRIMA VERSIONE CERCAVA COPPIE INDICE/VALORE E NON TROVAVA NIENTE, su una
# ROM VGA vera che di tabelle ne ha una decina. Il motivo e' che una tabella
# VGA non contiene gli indici: i registri del CRTC sono consecutivi da 00 a 18,
# quindi il codice tiene un contatore e scrive venticinque valori di fila. Gli
# indici sono impliciti, e cercarli e' cercare una cosa che non c'e'.
#
# ! E ALLORA SI CERCANO I VALORI CHE ABBIAMO MISURATO. Questa e' la parte che
# chiude il cerchio: sonda.drv ha letto dalla macchina i venticinque registri
# del CRTC in modo testo, e quei venticinque byte SONO nella ROM, in fila, nel
# punto in cui il BIOS li tiene. Trovato quel punto, i byte INTORNO sono il
# resto della definizione di quella modalita' — compresi i registri estesi che
# non sappiamo leggere.
#
# Non si cerca un motivo generico: si cerca una cosa nota, misurata su quella
# macchina. Il risultato o c'e' o non c'e', e non c'e' niente da interpretare.
# =============================================================================
def valori_dal_referto(percorso, banco="CR", primo=0x00, quanti=0x19):
    """I registri di un banco, dal referto di sonda.drv, in fila."""
    d = {}
    with open(percorso, errors="replace") as f:
        for r in f:
            m = re.match(r"^(SR|CR|GR|AR) ([0-9a-f]{2}): (.*)$", r.strip())
            if m and m.group(1) == banco:
                base = int(m.group(2), 16)
                for k, x in enumerate(m.group(3).split()):
                    d[base + k] = int(x, 16)
    return [d[i] for i in range(primo, primo + quanti) if i in d]


def cerca_sequenza(dati, seq, buchi=0):
    """Dove sta questa fila di byte. `buchi` = quanti possono non combaciare.

    ! QUALCHE BYTE PUO' NON COMBACIARE, ED E' NORMALE. Fra la tabella nella ROM
    e i registri letti dalla macchina ci sono passati il BIOS stesso — che
    qualche valore lo calcola invece di copiarlo — e il bit 7 di CR11, che e'
    un lucchetto e non un dato. Pretendere venticinque byte identici su
    venticinque vuol dire non trovare la tabella che c'e'.
    """
    fuori = []
    n, m = len(dati), len(seq)

    for i in range(n - m + 1):
        sbagliati = 0
        for j in range(m):
            if dati[i + j] != seq[j]:
                sbagliati += 1
                if sbagliati > buchi:
                    break
        else:
            fuori.append((i, sbagliati))

    return fuori


def tratto_piu_lungo(dati, seq, minimo=6):
    """Il tratto piu' lungo di `seq` che compare in `dati`, e dove.

    ! LA RICERCA TUTTO-O-NIENTE NON TROVA LE TABELLE CHE CI SONO. Provata su
    una ROM VGA vera: i venticinque valori del CRTC misurati sulla macchina non
    combaciavano in blocco nemmeno con tre byte di tolleranza, e la tabella
    era li' — nove dei venticinque, in fila, identici. Gli altri sedici
    differivano perche' quel BIOS li calcola, o perche' il registro porta un
    bit di lucchetto che nella tabella non c'e'.

    Nove valori consecutivi che combaciano non capitano per caso: sono uno su
    2^72. Percio' si cerca il TRATTO PIU' LUNGO invece della fila intera, e si
    dice quanto e' lungo — che e' anche la misura di quanto fidarsi.
    """
    migliore = (0, None, None)      # lunghezza, dove nei dati, dove in seq

    for inizio in range(len(seq) - minimo + 1):
        for lung in range(len(seq) - inizio, minimo - 1, -1):
            if lung <= migliore[0]:
                break
            ago = bytes(seq[inizio:inizio + lung])
            k = dati.find(ago)
            if k >= 0:
                migliore = (lung, k, inizio)
                break

    return migliore


def scava_referto(dati, percorso_referto):
    print("\n" + "=" * 70)
    print("I VALORI MISURATI, CERCATI DENTRO LA ROM")
    print("=" * 70)
    print("dal referto %s\n" % os.path.basename(percorso_referto))

    qualcosa = False

    for banco, primo, quanti, nome in (
            ("CR", 0x00, 0x19, "CRTC standard, 25 registri"),
            ("SR", 0x00, 0x05, "sequenziatore, 5 registri"),
            ("GR", 0x00, 0x09, "controllore grafico, 9 registri")):

        seq = valori_dal_referto(percorso_referto, banco, primo, quanti)
        if len(seq) < quanti:
            continue

        print("  %s: %s" % (nome, " ".join("%02x" % v for v in seq)))

        # Prima la fila intera: se c'e' tutta, e' la prova piu' forte.
        posti = cerca_sequenza(dati, seq, 0)
        if posti:
            print("     TUTTI E %d in fila, a: %s"
                  % (quanti, ", ".join(esadecimale(p) for p, _ in posti[:6])))
            mostra_intorno(dati, posti[0][0], quanti)
            qualcosa = True
            print()
            continue

        # Altrimenti il tratto piu' lungo, che e' quello che le trova davvero.
        lung, dove, da = tratto_piu_lungo(dati, seq)
        if lung:
            print("     %d di %d in fila (da %s%02x a %s%02x) a %s"
                  % (lung, quanti, banco, primo + da,
                     banco, primo + da + lung - 1, esadecimale(dove)))
            print("       %s" % " ".join("%02x" % v
                                         for v in seq[da:da + lung]))
            # ! LA TABELLA COMINCIA PRIMA. Se il tratto trovato parte dal
            # registro numero `da`, l'inizio della tabella sta `da` byte piu'
            # indietro: e' li' che si guarda.
            inizio = dove - da
            print("     la tabella dovrebbe cominciare a %s"
                  % esadecimale(inizio))
            mostra_intorno(dati, inizio, quanti)
            qualcosa = True
        else:
            print("     nemmeno sei valori di fila: qui non c'e'.")
        print()

    if qualcosa:
        print("  ! I BYTE PRIMA E DOPO SONO IL RESTO DELLA MODALITA'. Una")
        print("    tabella VGA tiene di fila sequenziatore, CRTC, attributo e")
        print("    controllore grafico; quello che avanza, su una scheda con")
        print("    registri estesi, sono gli estesi.")
        print("  ! E SE LA STESSA FILA COMPARE PIU' VOLTE, la distanza fra due")
        print("    ricorrenze e' il passo della tabella: da li' si contano le")
        print("    modalita' e si trova quella che serve.")
    else:
        print("  Nessuna delle tre file compare in questo binario.")
        print("  Se e' la ROM giusta, il BIOS calcola i valori invece di")
        print("  tenerli in tabella - cosa che certe schede fanno.")


# =============================================================================
# UNA CARTELLA INTERA — quale di questi file serve a quale dispositivo
#
# ! UN PACCHETTO DI DRIVER SONO CENTO FILE E TRE CHE CONTANO. Aprirli a uno a
# uno e' mezza giornata, e la domanda non e' «cosa c'e' dentro questo file» ma
# «quale file parla alla scheda che ho». La risposta ce l'hanno gli .inf, che
# sono testo semplice e contengono gli identificativi PCI: VEN_1039&DEV_6330 e'
# la scheda video di questo portatile, e l'.inf che la nomina dice anche quali
# file servono a guidarla.
#
# ! E GLI IDENTIFICATIVI DELLA MACCHINA LI ABBIAMO GIA'. Un referto di
# sonda.drv o di mappa.drv elenca ogni dispositivo di quel portatile con il suo
# venditore e prodotto. Incrociare le due liste risponde alla domanda vera:
# quale di questi file serve a quale dei MIEI dispositivi, e quali non servono
# a niente.
# =============================================================================
def id_dagli_inf(percorso):
    """Gli identificativi PCI nominati da un .inf, e i file che gli associa."""
    ids = set()
    file_nominati = set()

    try:
        testo = open(percorso, errors="replace").read()
    except OSError:
        return ids, file_nominati

    for m in re.finditer(r"VEN_([0-9A-Fa-f]{4})&DEV_([0-9A-Fa-f]{4})", testo):
        ids.add((int(m.group(1), 16), int(m.group(2), 16)))

    for m in re.finditer(r"([A-Za-z0-9_.\-]+\.(?:sys|dll|vxd|exe))", testo):
        file_nominati.add(m.group(1).lower())

    return ids, file_nominati


def id_dal_referto(percorso):
    """I dispositivi di quella macchina: (venditore, prodotto) -> descrizione."""
    fuori = {}
    ultimo = None

    with open(percorso, errors="replace") as f:
        for r in f:
            r = r.strip()
            m = re.match(r"^---\s+([0-9a-f]{2}:[0-9a-f]{2}\.[0-9])\s+"
                         r"([0-9a-f]{4}):([0-9a-f]{4})\s+rev\s+([0-9a-f]{2})"
                         r"\s+classe\s+([0-9a-f]{6})", r)
            if m:
                ultimo = (int(m.group(2), 16), int(m.group(3), 16))
                fuori[ultimo] = "%s  rev %s  classe %s" % (
                    m.group(1), m.group(4), m.group(5))
                continue
            m = re.match(r"^([0-9a-f]{2}:[0-9a-f]{2}\.[0-9])\s+"
                         r"([0-9a-f]{4}):([0-9a-f]{4})\s+classe\s+"
                         r"([0-9a-f]{6})\s*(.*)$", r)
            if m:
                k = (int(m.group(2), 16), int(m.group(3), 16))
                fuori[k] = "%s  classe %s  %s" % (m.group(1), m.group(4),
                                                  m.group(5))
    return fuori


def cartella(radice, referto):
    macchina = id_dal_referto(referto) if referto else {}

    print("=" * 70)
    print("LA CARTELLA %s" % radice)
    print("=" * 70)

    if macchina:
        print("Dispositivi di questa macchina, da %s:"
              % os.path.basename(referto))
        for (v, d) in sorted(macchina):
            print("    %04x:%04x   %s" % (v, d, macchina[(v, d)]))
    else:
        print("! senza -referto non posso dire quali di questi file servano")
        print("  a QUESTA macchina. Passalo: e' la meta' utile del lavoro.")
    print()

    binari, inf, altro = [], [], []
    for base, _, nomi in os.walk(radice):
        for n in sorted(nomi):
            pc = os.path.join(base, n)
            e = n.lower().rsplit(".", 1)[-1] if "." in n else ""
            if e == "inf":
                inf.append(pc)
            elif e in ("sys", "dll", "vxd", "exe", "drv"):
                binari.append(pc)
            else:
                altro.append(pc)

    # --- Gli .inf: chi nomina un dispositivo che abbiamo -------------------
    print("=" * 70)
    print("GLI .INF — chi dice di guidare cosa")
    print("=" * 70)

    utili = {}
    for pc in inf:
        ids, nominati = id_dagli_inf(pc)
        nostri = [k for k in ids if k in macchina] if macchina else []

        if macchina and not nostri:
            continue

        print("\n  %s" % os.path.relpath(pc, radice))
        if nostri:
            for (v, d) in sorted(nostri):
                print("    ! GUIDA %04x:%04x — %s" % (v, d, macchina[(v, d)]))
        elif ids:
            mostra = sorted(ids)[:8]
            print("    nomina %d dispositivi: %s%s"
                  % (len(ids),
                     " ".join("%04x:%04x" % k for k in mostra),
                     " ..." if len(ids) > 8 else ""))
        if nominati:
            print("    file: %s" % " ".join(sorted(nominati)[:10]))
            for f in nominati:
                utili[f] = utili.get(f, 0) + (10 if nostri else 1)

    if macchina and not utili:
        print("\n  nessun .inf nomina un dispositivo di questa macchina.")

    # --- I binari, ordinati per quanto probabilmente servono ---------------
    print("\n" + "=" * 70)
    print("I BINARI — e quali vale la pena scavare")
    print("=" * 70)

    def punteggio(pc):
        n = os.path.basename(pc).lower()
        p = utili.get(n, 0)
        # ! UN .sys VALE PIU' DI UNA .dll, e non e' un pregiudizio: il driver di
        # kernel e' l'unico che puo' scrivere sulle porte e mappare i registri.
        # Il modeset e le tabelle stanno spesso nella .dll, il motore 2D quasi
        # sempre nel .sys.
        if n.endswith(".sys"):
            p += 3
        elif n.endswith(".dll"):
            p += 2
        return p

    for pc in sorted(binari, key=lambda x: (-punteggio(x), x)):
        try:
            dati = open(pc, "rb").read(4096)
            dim = os.path.getsize(pc)
        except OSError:
            continue

        pe = PE(dati)
        segno = ""
        if utili.get(os.path.basename(pc).lower(), 0) >= 10:
            segno = "  <-- NOMINATO DA UN .INF CHE GUIDA UN NOSTRO DISPOSITIVO"

        print("  %-28s %9d byte  %s%s"
              % (os.path.relpath(pc, radice), dim,
                 pe.nome_macchina() if pe.valido else "non PE", segno))

    if altro:
        print("\n  (%d altri file: cataloghi, immagini, traduzioni)"
              % len(altro))

    print("\n" + "=" * 70)
    print("E ADESSO")
    print("=" * 70)
    print("Scava nei file segnati, uno per volta:")
    print("    scava.py <file> -porte 3c4,3c5,3d4,3d5,3ce,3cf,3c0,3c2")
    print("    scava.py <file> -referto sonda/PONTET.TXT")
    print("Il primo dice quali registri tocca e dove; il secondo cerca li'")
    print("dentro i valori misurati sulla macchina.")


# =============================================================================
# Il rapporto
# =============================================================================
def rapporto(percorso, filtro_porte, indirizzo_asm, referto):
    dati = open(percorso, "rb").read()
    pe = PE(dati)

    print("=" * 70)
    print("SCAVO IN %s — %d byte" % (os.path.basename(percorso), len(dati)))
    print("=" * 70)

    pezzi = []          # (nome, grezzo, base, bit)

    if pe.valido:
        print("Formato: PE (Windows), macchina %s, base 0x%08x"
              % (pe.nome_macchina(), pe.base))
        if pe.macchina not in (0x14C, 0x8664):
            print("! non e' x86: questo strumento sa leggere solo x86.")
            return
        bit = 32 if pe.macchina == 0x14C else 64
        if bit == 64:
            print("! a 64 bit: il disassemblatore qui e' impostato a 32,")
            print("  i risultati sull'analisi delle porte non sono affidabili.")
        print("\nSezioni:")
        for s in pe.sezioni:
            print("  %-8s  vaddr 0x%08x  %7d byte  %s"
                  % (s["nome"], s["vaddr"], s["rsize"],
                     "codice" if s["codice"] else "dati"))
            if s["codice"] and s["rsize"]:
                pezzi.append((s["nome"],
                              dati[s["roff"]:s["roff"] + s["rsize"]],
                              pe.base + s["vaddr"], 32))
    elif dati[:2] == b"\x55\xAA":
        blocchi = dati[2]
        print("Formato: ROM di espansione, %d blocchi = %d byte dichiarati"
              % (blocchi, blocchi * 512))
        if dati[3:5] in (b"\xe9", ) or True:
            print("Codice a 16 bit; l'ingresso e' a +0x03.")
        # ! LA ROM SI DISASSEMBLA TUTTA, dati compresi. Non c'e' una tabella
        # di sezioni che dica dove finisce il codice, e i tratti di dati
        # producono istruzioni senza senso che non danno fastidio: nessuna di
        # quelle scrive su una porta VGA per caso.
        pezzi.append(("rom", dati, 0xC0000, 16))
    else:
        print("Formato: sconosciuto (ne' PE ne' ROM 55 AA).")
        print("Lo tratto come codice grezzo a 32 bit.")
        pezzi.append(("grezzo", dati, 0, 32))

    if indirizzo_asm is not None:
        mostra_asm(pezzi, indirizzo_asm)
        return

    if referto is not None:
        scava_referto(dati, referto)
        return

    if pe.valido:
        leggi_importazioni(pe)

    # --- Porte ---------------------------------------------------------------
    print("\n" + "=" * 70)
    print("LE PORTE — quali registri tocca, e quante volte")
    print("=" * 70)

    tutte = {}
    righe_tot = []
    for nome, grezzo, base, bit in pezzi:
        righe = disassembla(grezzo, bit, base)
        righe_tot += righe
        for p, v in cerca_porte(righe, filtro_porte).items():
            t = tutte.setdefault(p, {"letture": 0, "scritture": 0, "dove": []})
            t["letture"] += v["letture"]
            t["scritture"] += v["scritture"]
            t["dove"] += v["dove"][:4]

    if not tutte:
        print("nessun accesso a porte con un numero riconoscibile.")
    else:
        # ! LE PORTE SOTTO 0x100 CHE NON CONOSCIAMO SONO RUMORE. Una ROM si
        # disassembla tutta, dati compresi, e un tratto di dati produce
        # istruzioni finte: fra queste, ogni tanto, una `out`. Le porte vere di
        # una scheda video stanno tutte sopra 0x300, e le poche basse che
        # contano - il DMA, il timer - sono in PORTE_NOTE.
        rumore = [p for p in tutte if p < 0x100 and p not in PORTE_NOTE]

        for p in sorted(tutte):
            if p in rumore:
                continue
            v = tutte[p]
            nota = PORTE_NOTE.get(p, "")
            print("  0x%04x  %4d scritture, %4d letture   %s"
                  % (p, v["scritture"], v["letture"], nota))
            print("          a: " + " ".join(esadecimale(d)
                                             for d in v["dove"][:6]))

        if rumore:
            print("\n  (%d porte sotto 0x100 non elencate: sono quasi tutte"
                  % len(rumore))
            print("   dati disassemblati come codice. -tutte le mostra.)")

    if pe.valido:
        leggi_chiamate(righe_tot, pe)

    # --- Memoria -------------------------------------------------------------
    print("\n" + "=" * 70)
    print("GLI OFFSET IN MEMORIA — i registri della finestra mappata")
    print("=" * 70)

    mm = cerca_mmio(righe_tot)
    if not mm:
        print("nessun offset sopra 0x100: questo binario non usa MMIO,")
        print("oppure lo fa con puntatori che questa analisi non segue.")
    else:
        print("i venti piu' usati:")
        for off in sorted(mm, key=lambda o: -mm[o]["quante"])[:20]:
            print("  +0x%05x  %4d volte   a: %s"
                  % (off, mm[off]["quante"],
                     " ".join(esadecimale(d) for d in mm[off]["dove"][:3])))

    # --- Tabelle -------------------------------------------------------------
    print("\n" + "=" * 70)
    print("LE TABELLE — coppie indice/valore, che il codice non contiene")
    print("=" * 70)

    tab = cerca_tabelle(dati)
    if not tab:
        print("nessuna successione di coppie abbastanza lunga.")
    else:
        tab.sort(key=lambda t: -t["quante"])
        print("%d trovate; le venti piu' lunghe:\n" % len(tab))
        for t in tab[:20]:
            print("  +0x%05x  %2d coppie, indici da %02x a %02x"
                  % (t["offset"], t["quante"], t["primo"], t["ultimo"]))
            print("     " + " ".join("%02x=%02x" % c
                                     for c in t["coppie"][:12])
                  + (" ..." if t["quante"] > 12 else ""))

    print("\n" + "=" * 70)
    print("COME SI LEGGE QUESTO RAPPORTO")
    print("=" * 70)
    print("Le tabelle che cominciano da 00 o 01 e arrivano oltre 18 sono")
    print("candidate a essere il CRTC: quello standard ha 25 registri.")
    print("Quelle corte che stanno nella fascia 00-04 sono il sequenziatore.")
    print("Per vedere il codice attorno a un indirizzo:")
    print("    scava.py %s -asm 0x1a40" % os.path.basename(percorso))
    print("Per cercare qui dentro i valori misurati sulla macchina:")
    print("    scava.py %s -referto sonda/acer_aspire_3000.TXT"
          % os.path.basename(percorso))


def mostra_asm(pezzi, indirizzo, quante=40):
    for nome, grezzo, base, bit in pezzi:
        if not (base <= indirizzo < base + len(grezzo)):
            continue
        salto = indirizzo - base
        righe = disassembla(grezzo[max(0, salto - 32):salto + 256], bit,
                            base + max(0, salto - 32))
        print("--- %s, attorno a 0x%x (%d bit) ---" % (nome, indirizzo, bit))
        for ind, testo in righe[:quante]:
            print("  %08x  %s%s" % (ind, testo,
                                    "   <---" if ind == indirizzo else ""))
        return
    print("scava: 0x%x non e' dentro nessun pezzo di codice." % indirizzo)


def mostra_intorno(dati, p, quanti, contorno=16):
    a = max(0, p - contorno)
    b = min(len(dati), p + quanti + contorno)

    print("     intorno (0x%x - 0x%x):" % (a, b))
    for r in range(a, b, 16):
        fetta = dati[r:min(r + 16, b)]
        segno = "  <-- qui" if r <= p < r + 16 else ""
        print("       %06x  %s%s"
              % (r, " ".join("%02x" % x for x in fetta), segno))


def aiuto():
    print(__doc__ or "")
    print("uso: scava.py <file|cartella> [-porte 3c4,3d4] [-asm 0x1a40]")
    print()
    print("  Con una CARTELLA dice quale dei file serve a quale dispositivo,")
    print("  incrociando gli .inf con il referto della macchina:")
    print("      scava.py drv_prop/AcerAspire3k -referto sonda/MMIO3.TXT")
    print()
    print("  <file>   la ROM presa con `sonda.drv -rom`, oppure un .sys/.dll")
    print("  -porte   guarda solo quelle porte (esadecimale, separate da ,)")
    print("  -asm A   mostra il codice attorno all'indirizzo A")
    print("  -referto R  cerca dentro il binario i valori che sonda.drv ha")
    print("           misurato sulla macchina: e' il modo di trovare LA")
    print("           tabella di QUESTA modalita' invece di una qualunque")


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        aiuto()
        return 0

    percorso = None
    filtro = None
    asm = None
    referto = None
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "-porte" and i + 1 < len(sys.argv):
            i += 1
            filtro = {int(x, 16) for x in sys.argv[i].split(",")}
        elif a == "-referto" and i + 1 < len(sys.argv):
            i += 1
            referto = sys.argv[i]
        elif a == "-asm" and i + 1 < len(sys.argv):
            i += 1
            asm = int(sys.argv[i], 0)
        elif a.startswith("-"):
            print("scava: non conosco '%s'" % a)
            return 1
        else:
            percorso = a
        i += 1

    if percorso is None:
        aiuto()
        return 1
    if os.path.isdir(percorso):
        cartella(percorso, referto)
        return 0

    if not os.path.isfile(percorso):
        print("scava: %s non esiste." % percorso)
        return 1

    rapporto(percorso, filtro, asm, referto)
    return 0


sys.exit(main())

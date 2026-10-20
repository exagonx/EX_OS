#!/usr/bin/env python3
"""Costruisce un'immagine ISO 9660 (+ Joliet) per EX-OS.

Due modi d'uso:

    python3 tools/mkiso.py uscita.iso --da build/iso [--etichetta "EXOS TOOLS"]
    python3 tools/mkiso.py uscita.iso --prova [--senza-joliet]

Il primo prende un albero di directory e ne fa un CD: e' quello che usa
`make iso` per il disco degli strumenti. Il secondo genera un'immagine
sintetica di collaudo del driver, di cui si conosce ogni byte — utile
proprio perche', quando il driver legge un nome sbagliato, si sa cosa
c'era scritto.

PERCHE' NON genisoimage. Sull'ambiente di sviluppo non c'e' (ne'
xorriso), e per collaudare un driver appena scritto avere un generatore
di cui si controlla ogni campo vale piu' di uno che fa tutto: quando la
lettura sbaglia, si sa da che parte guardare.

I DUE ALBERI. Un CD con nomi lunghi contiene DUE strutture di directory
complete che puntano agli STESSI dati: quella ISO 9660, con i nomi
maiuscoli in 8.3 e il numero di versione (`LEGGIMI.TXT;1`), e quella
Joliet, con i nomi veri in UCS-2. Non sono due viste della stessa
struttura, e questo generatore le costruisce entrambe: i blocchi dei file
sono condivisi, le directory no.
"""

import os
import struct
import time
import sys

BLOCCO = 2048

# I primi 16 blocchi (32 KB) sono l'area di sistema: ISO 9660 non li
# guarda affatto, ed e' li' che un disco avviabile mette il proprio
# settore di avvio.
PRIMO_DESCRITTORE = 16


# =============================================================================
# Campi del formato
# =============================================================================

def both16(v):
    """Un intero a 16 bit scritto DUE volte, little e big endian di
    seguito. Occupa 4 byte: e' la convenzione di ISO 9660, ed e' il primo
    punto in cui si sbagliano gli offset dei campi successivi."""
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


# ! LA DATA E' QUELLA VERA DI COSTRUZIONE, e prima era fissa al
# 2026-08-01 12:00. Un CD i cui file dichiarano tutti la stessa data
# inventata non e' neutro: `ls -d` la mostra, e chi guarda crede che quel
# disco sia di agosto anche quando l'ha masterizzato ieri. Meglio un dato
# vero che uno plausibile.
#
# L'orario e' quello LOCALE con offset di fuso a zero, perche' EX-OS non
# sa in che fuso si trova (vedi il commento su localtime in libc.h): una
# conversione a UTC produrrebbe un'ora che nessuno dei due sistemi
# rimetterebbe a posto.
_COSTRUZIONE = time.localtime()


def data7(t=None):
    t = t or _COSTRUZIONE
    return bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec, 0])


def data17(t=None):
    t = t or _COSTRUZIONE
    return ("%04d%02d%02d%02d%02d%02d00" % (t.tm_year, t.tm_mon, t.tm_mday,
                                            t.tm_hour, t.tm_min,
                                            t.tm_sec)).encode() + bytes([0])


def rec_dir(nome_bytes, extent, dim, is_dir):
    """Un record di directory. La lunghezza e' SEMPRE pari: il byte di
    riempimento c'e' quando il nome ha lunghezza pari (33 + pari = dispari)."""
    ln = 33 + len(nome_bytes)
    pad = ln % 2
    rec = bytearray()
    rec.append(ln + pad)
    rec.append(0)                       # lunghezza degli attributi estesi
    rec += both32(extent)
    rec += both32(dim)
    rec += data7()
    rec.append(0x02 if is_dir else 0x00)
    rec.append(0)                       # file unit size
    rec.append(0)                       # interleave
    rec += both16(1)                    # numero di volume
    rec.append(len(nome_bytes))
    rec += nome_bytes
    rec += b"\x00" * pad
    return bytes(rec)


def lunghezza_rec(nome_bytes):
    ln = 33 + len(nome_bytes)
    return ln + (ln % 2)


def blocchi_per(n_byte):
    return (n_byte + BLOCCO - 1) // BLOCCO if n_byte else 1


# =============================================================================
# Nomi
# =============================================================================

VALIDI_ISO = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


def pulisci_iso(pezzo, quanti):
    fuori = "".join(c if c.upper() in VALIDI_ISO else "_" for c in pezzo.upper())
    return fuori[:quanti]


def nome_iso(nome, is_dir, usati):
    """Nome in forma ISO 9660 livello 1: 8.3 maiuscolo, `;1` sui file.

    Si resta al livello 1 di proposito: i nomi veri li porta Joliet, e un
    nome corto e conservativo e' leggibile anche dai lettori piu' vecchi.
    Le collisioni prodotte dal troncamento si risolvono con un contatore —
    due file distinti che diventassero lo stesso nome sarebbero un file
    solo, cioe' un file perso in silenzio.
    """
    if is_dir:
        base, ext = pulisci_iso(nome, 8), ""
    else:
        radice, punto, coda = nome.rpartition(".")
        if not punto:
            radice, coda = nome, ""
        base, ext = pulisci_iso(radice, 8), pulisci_iso(coda, 3)

    if not base:
        base = "_"

    def componi(b):
        n = b if not ext else b + "." + ext
        return n if is_dir else n + ";1"

    finale = componi(base)
    contatore = 1
    while finale in usati:
        coda_num = "~%d" % contatore
        finale = componi(base[:8 - len(coda_num)] + coda_num)
        contatore += 1

    usati.add(finale)
    return finale.encode("ascii")


def nome_joliet(nome, is_dir):
    """Joliet conserva il nome vero, in UCS-2 big endian. Il `;1` sui file
    c'e' anche qui: e' parte del formato, non del nome, e i lettori lo
    tolgono — compreso kernel/fs/iso9660.c."""
    testo = nome if is_dir else nome + ";1"
    return testo.encode("utf-16-be")


# =============================================================================
# L'albero
# =============================================================================

class Voce:
    def __init__(self, nome, percorso=None, is_dir=False):
        self.nome = nome
        self.percorso = percorso        # None per le directory sintetiche
        self.is_dir = is_dir
        self.figli = []
        self.dati = b""
        self.dim = 0
        self.extent = 0                 # blocco dei dati (file) o della dir
        self.dim_iso = 0                # dimensione della directory, albero ISO
        self.dim_jol = 0                # idem, albero Joliet
        self.extent_iso = 0
        self.extent_jol = 0
        self.numero = 0                 # numero nella tabella dei percorsi
        self.n_iso = b""
        self.n_jol = b""

    def __repr__(self):
        return "<%s %s>" % ("dir" if self.is_dir else "file", self.nome)


def leggi_albero(radice_fs):
    radice = Voce("", is_dir=True)

    def scendi(voce, percorso):
        for nome in sorted(os.listdir(percorso)):
            pieno = os.path.join(percorso, nome)
            if os.path.isdir(pieno):
                d = Voce(nome, pieno, is_dir=True)
                voce.figli.append(d)
                scendi(d, pieno)
            elif os.path.isfile(pieno):
                f = Voce(nome, pieno, is_dir=False)
                with open(pieno, "rb") as fh:
                    f.dati = fh.read()
                f.dim = len(f.dati)
                voce.figli.append(f)

    scendi(radice, radice_fs)
    return radice


def assegna_nomi(voce):
    """I nomi ISO vanno resi unici DENTRO ogni directory, non nell'intero
    volume: e' la stessa regola di qualunque filesystem."""
    usati = set()
    for f in voce.figli:
        f.n_iso = nome_iso(f.nome, f.is_dir, usati)
        f.n_jol = nome_joliet(f.nome, f.is_dir)
        if f.is_dir:
            assegna_nomi(f)


def misura_directory(voce, joliet):
    """Quanti byte occupa il contenuto di una directory.

    Il conto DEVE tenere conto della regola che un record non attraversa
    mai il confine di un blocco: quando non ci sta, il resto del blocco e'
    riempimento. Ignorarlo qui darebbe una dimensione piu' piccola del
    vero, e le ultime voci finirebbero fuori dalla directory dichiarata.
    """
    n = lunghezza_rec(b"\x00") + lunghezza_rec(b"\x01")   # "." e ".."
    for f in voce.figli:
        r = lunghezza_rec(f.n_jol if joliet else f.n_iso)
        if n % BLOCCO + r > BLOCCO:
            n += BLOCCO - (n % BLOCCO)
        n += r
    return n


def tutte_le_directory(radice):
    """Directory in ordine di livello (radice, poi i suoi figli, ...): e'
    l'ordine che la tabella dei percorsi richiede."""
    fuori = [radice]
    i = 0
    while i < len(fuori):
        for f in fuori[i].figli:
            if f.is_dir:
                fuori.append(f)
        i += 1
    return fuori


def tabella_percorsi(dirs, big, joliet):
    out = bytearray()
    for d in dirs:
        nome = b"\x00" if d is dirs[0] else (d.n_jol if joliet else d.n_iso)
        extent = d.extent_jol if joliet else d.extent_iso
        out.append(len(nome))
        out.append(0)                                   # attributi estesi
        out += struct.pack(">I" if big else "<I", extent)
        out += struct.pack(">H" if big else "<H", d.numero_padre)
        out += nome
        if len(nome) % 2:
            out.append(0)
    return bytes(out)


def contenuto_directory(voce, padre, joliet):
    """Scrive i record di una directory: "." e ".." per primi, poi i figli
    con il riempimento di fine blocco dove serve."""
    ext_self = voce.extent_jol if joliet else voce.extent_iso
    dim_self = voce.dim_jol if joliet else voce.dim_iso
    ext_padre = padre.extent_jol if joliet else padre.extent_iso
    dim_padre = padre.dim_jol if joliet else padre.dim_iso

    out = bytearray()
    out += rec_dir(b"\x00", ext_self, dim_self, True)
    out += rec_dir(b"\x01", ext_padre, dim_padre, True)

    for f in voce.figli:
        nome = f.n_jol if joliet else f.n_iso
        if f.is_dir:
            ext = f.extent_jol if joliet else f.extent_iso
            dim = f.dim_jol if joliet else f.dim_iso
        else:
            ext, dim = f.extent, f.dim
        r = rec_dir(nome, ext, dim, f.is_dir)
        if len(out) % BLOCCO + len(r) > BLOCCO:
            out += b"\x00" * (BLOCCO - len(out) % BLOCCO)
        out += r

    return bytes(out)


# =============================================================================
# Descrittori di volume
# =============================================================================

def descrittore(tipo, etichetta, blocchi_volume, root_rec, joliet,
                pt_dim, pt_le, pt_be):
    d = bytearray(b"\x00" * BLOCCO)
    d[0] = tipo
    d[1:6] = b"CD001"
    d[6] = 1
    d[7] = 0

    ident = etichetta.encode("utf-16-be") if joliet else etichetta.encode("ascii")
    riempi = b"\x00 " if joliet else b" "
    d[8:40] = (b"\x00 " * 16) if joliet else (b" " * 32)     # identificatore di sistema
    d[40:72] = (ident + riempi * 32)[:32]                    # etichetta del volume
    d[80:88] = both32(blocchi_volume)
    if joliet:
        d[88:91] = b"%/E"                                    # UCS-2 livello 3
    d[120:124] = both16(1)
    d[124:128] = both16(1)
    d[128:132] = both16(BLOCCO)
    d[132:140] = both32(pt_dim)
    d[140:144] = struct.pack("<I", pt_le)
    d[148:152] = struct.pack(">I", pt_be)
    d[156:156 + len(root_rec)] = root_rec
    for off in (190, 318, 446, 574):
        d[off:off + 128] = (b"\x00 " * 64) if joliet else (b" " * 128)
    for off in (702, 739, 776):
        d[off:off + 37] = (b"\x00 " * 18 + b" ") if joliet else (b" " * 37)
    for off in (813, 830, 847, 864):
        d[off:off + 17] = data17()
    d[881] = 1
    return bytes(d)


def terminatore():
    t = bytearray(b"\x00" * BLOCCO)
    t[0] = 255
    t[1:6] = b"CD001"
    t[6] = 1
    return bytes(t)


# =============================================================================
# Costruzione
# =============================================================================

# =============================================================================
# EL TORITO — il CD che si avvia
#
# ! PER EMULAZIONE FLOPPY, ed e' la forma piu' semplice che esista: il BIOS
# legge l'immagine del floppy dal CD, la presenta al sistema come l'unita'
# A: e da quel momento non sa piu' di essere un CD. Il percorso di avvio
# gia' collaudato — stage1, stage2, FAT12 — NON CAMBIA DI UNA RIGA.
#
# L'alternativa ("no emulation") avrebbe voluto dire un settore di avvio
# nuovo che sa leggere ISO 9660 con i servizi INT 13h estesi: un secondo
# bootloader da scrivere e da mantenere, per arrivare allo stesso punto.
# E' la ragione per cui i CD di Windows 98 facevano cosi'.
#
# Servono due cose, e nessuna delle due sta dentro il filesystem:
#
#   1. un DESCRITTORE DI AVVIO subito dopo il descrittore primario, che
#      dice soltanto dove sta il catalogo;
#   2. il CATALOGO, un blocco con due voci da 32 byte — una che dichiara
#      "questo catalogo e' valido" e una che dice dove sta l'immagine e
#      come va emulata.
#
# ! IL BLOCCO DELL'IMMAGINE E' UN EXTENT COME UN ALTRO, ma il BIOS lo
# raggiunge SENZA passare per la directory: legge il numero dal catalogo e
# basta. Per questo l'immagine puo' anche non comparire fra i file — qui
# si sceglie di farla comparire lo stesso, perche' un CD in cui l'avvio e'
# invisibile e' un CD che nessuno sa piu' rifare.
# =============================================================================

BOOT_MEDIA_FLOPPY_144 = 0x02        # 0=nessuna, 1=1.2MB, 2=1.44MB, 3=2.88MB


def descrittore_avvio(blocco_catalogo):
    """Il Boot Record Volume Descriptor: tipo 0, e dentro solo un numero."""
    d = bytearray(BLOCCO)
    d[0] = 0
    d[1:6] = b"CD001"
    d[6] = 1
    # L'identificativo DEVE essere esattamente questa stringa, riempita di
    # zeri fino a 32 byte: e' cio' che il BIOS confronta per decidere che
    # questo descrittore lo riguarda.
    ident = b"EL TORITO SPECIFICATION"
    d[7:7 + len(ident)] = ident
    # 7+32 = 39 .. 71 sono riservati (zeri), poi il puntatore al catalogo.
    d[71:75] = struct.pack("<I", blocco_catalogo)
    return bytes(d)


def catalogo_avvio(blocco_immagine):
    """Le due voci da 32 byte: validazione e voce iniziale."""
    c = bytearray(BLOCCO)

    # --- voce di validazione ---
    v = bytearray(32)
    v[0] = 0x01                     # intestazione
    v[1] = 0x00                     # piattaforma: 80x86
    # ! L'IDENTIFICATIVO OCCUPA 24 BYTE ESATTI, e si riempie di zeri.
    # Assegnare a una fetta di bytearray una sequenza piu' corta la
    # ACCORCIA — non la riempie — e la voce di validazione finirebbe di 31
    # byte invece di 32, spostando tutto cio' che segue.
    v[4:28] = b"EX-OS".ljust(24, b"\x00")
    v[30] = 0x55
    v[31] = 0xAA

    # ! IL CHECKSUM E' SU PAROLE DA 16 BIT E LA SOMMA DEVE FARE ZERO.
    # Non e' una firma decorativa: un BIOS che non la trova a zero salta
    # l'avvio senza dire niente, e il disco sembra semplicemente non
    # avviabile. Si calcola con i due byte del checksum a zero e si scrive
    # il complemento.
    somma = 0
    for i in range(0, 32, 2):
        somma = (somma + v[i] + (v[i + 1] << 8)) & 0xFFFF
    checksum = (-somma) & 0xFFFF
    v[28] = checksum & 0xFF
    v[29] = (checksum >> 8) & 0xFF
    c[0:32] = v

    # --- voce iniziale: dove sta l'immagine e come si emula ---
    e = bytearray(32)
    e[0] = 0x88                     # avviabile
    e[1] = BOOT_MEDIA_FLOPPY_144
    # e[2:4] segmento di caricamento: 0 = il valore convenzionale 0x7C0
    # e[4]   tipo di sistema: si prende dalla tabella delle partizioni
    #        dell'immagine, e un floppy non ne ha: 0
    # ! IL CONTEGGIO DEI SETTORI VALE 1 IN EMULAZIONE, e non e' un errore:
    # non dice quanti settori caricare — li decide il tipo di supporto —
    # ma quanti "settori virtuali" il BIOS deve leggere per avviare.
    e[6:8] = struct.pack("<H", 1)
    e[8:12] = struct.pack("<I", blocco_immagine)
    c[32:64] = e

    return bytes(c)


def costruisci(radice, etichetta, joliet, avvio=None):
    assegna_nomi(radice)

    dirs = tutte_le_directory(radice)

    # Numerazione per la tabella dei percorsi: la radice e' 1, e ogni
    # directory conosce il numero del proprio padre.
    radice.numero = 1
    radice.numero_padre = 1
    for i, d in enumerate(dirs, start=1):
        d.numero = i
    for d in dirs:
        for f in d.figli:
            if f.is_dir:
                f.numero_padre = d.numero

    for d in dirs:
        d.dim_iso = misura_directory(d, joliet=False)
        d.dim_jol = misura_directory(d, joliet=True) if joliet else 0

    # --- Assegnazione dei blocchi ------------------------------------
    # L'ordine e' libero purche' coerente; questo tiene vicini i metadati
    # e mette i dati in fondo, cosi' un'immagine e' leggibile anche
    # guardandola con un editor esadecimale.
    blocco = PRIMO_DESCRITTORE
    b_pvd = blocco; blocco += 1
    # ! IL DESCRITTORE DI AVVIO VA SUBITO DOPO IL PRIMARIO. Il BIOS scorre
    # i descrittori in ordine e si ferma al terminatore: metterlo dopo di
    # quello lo renderebbe invisibile, e il disco sembrerebbe non
    # avviabile senza nessun messaggio.
    b_boot = None
    if avvio is not None:
        b_boot = blocco; blocco += 1
    b_svd = None
    if joliet:
        b_svd = blocco; blocco += 1
    b_term = blocco; blocco += 1

    b_catalogo = None
    if avvio is not None:
        b_catalogo = blocco; blocco += 1

    pt_iso_le = tabella_percorsi(dirs, big=False, joliet=False)
    pt_iso_be = tabella_percorsi(dirs, big=True,  joliet=False)

    b_pt_iso_le = blocco; blocco += blocchi_per(len(pt_iso_le))
    b_pt_iso_be = blocco; blocco += blocchi_per(len(pt_iso_be))

    b_pt_jol_le = b_pt_jol_be = 0
    if joliet:
        b_pt_jol_le = blocco; blocco += blocchi_per(len(pt_iso_le))
        b_pt_jol_be = blocco; blocco += blocchi_per(len(pt_iso_be))

    for d in dirs:
        d.extent_iso = blocco
        blocco += blocchi_per(d.dim_iso)
    if joliet:
        for d in dirs:
            d.extent_jol = blocco
            blocco += blocchi_per(d.dim_jol)

    # I file: un blocco intero anche quando sono vuoti, cosi' nessun
    # extent cade fuori dal volume dichiarato — un file da zero byte non
    # si legge mai, ma il suo record punta comunque da qualche parte.
    cima = [blocco]

    def scorri_file(voce):
        for f in voce.figli:
            if f.is_dir:
                scorri_file(f)
            else:
                f.extent = cima[0]
                cima[0] += blocchi_per(f.dim)

    scorri_file(radice)
    totale = cima[0]

    # Le tabelle dei percorsi Joliet vanno ricalcolate: contengono gli
    # extent, che ora si conoscono.
    if joliet:
        pt_jol_le = tabella_percorsi(dirs, big=False, joliet=True)
        pt_jol_be = tabella_percorsi(dirs, big=True,  joliet=True)
    pt_iso_le = tabella_percorsi(dirs, big=False, joliet=False)
    pt_iso_be = tabella_percorsi(dirs, big=True,  joliet=False)

    # --- Scrittura ----------------------------------------------------
    img = bytearray(b"\x00" * (totale * BLOCCO))

    def metti(b, dati):
        img[b * BLOCCO:b * BLOCCO + len(dati)] = dati

    rec_root_iso = rec_dir(b"\x00", radice.extent_iso, radice.dim_iso, True)
    metti(b_pvd, descrittore(1, etichetta, totale, rec_root_iso, False,
                             len(pt_iso_le), b_pt_iso_le, b_pt_iso_be))
    if joliet:
        rec_root_jol = rec_dir(b"\x00", radice.extent_jol, radice.dim_jol, True)
        metti(b_svd, descrittore(2, etichetta, totale, rec_root_jol, True,
                                 len(pt_jol_le), b_pt_jol_le, b_pt_jol_be))
        metti(b_pt_jol_le, pt_jol_le)
        metti(b_pt_jol_be, pt_jol_be)

    metti(b_term, terminatore())
    metti(b_pt_iso_le, pt_iso_le)
    metti(b_pt_iso_be, pt_iso_be)

    if avvio is not None:
        metti(b_boot, descrittore_avvio(b_catalogo))
        metti(b_catalogo, catalogo_avvio(avvio.extent))

    def scrivi_dirs(voce, padre):
        metti(voce.extent_iso, contenuto_directory(voce, padre, joliet=False))
        if joliet:
            metti(voce.extent_jol, contenuto_directory(voce, padre, joliet=True))
        for f in voce.figli:
            if f.is_dir:
                scrivi_dirs(f, voce)

    scrivi_dirs(radice, radice)

    def scrivi_file(voce):
        for f in voce.figli:
            if f.is_dir:
                scrivi_file(f)
            elif f.dim:
                metti(f.extent, f.dati)

    scrivi_file(radice)
    return bytes(img), totale


# =============================================================================
# Immagine sintetica di collaudo
# =============================================================================

def albero_di_prova():
    radice = Voce("", is_dir=True)

    leggimi = Voce("Leggimi importante.txt", is_dir=False)
    leggimi.dati = ("EX-OS: prova del driver CD/DVD.\r\n"
                    "Se leggi questa riga, ISO 9660 funziona.\r\n").encode()
    leggimi.dim = len(leggimi.dati)

    hello = Voce("hello.txt", is_dir=False)
    hello.dati = b"ciao dal CD\r\n"
    hello.dim = len(hello.dati)

    # Piu' di due blocchi, con ogni riga numerata: un salto ai confini di
    # blocco si vede subito invece di nascondersi in mezzo a testo uguale.
    note = Voce("note-lunghe.txt", is_dir=False)
    note.dati = b"".join(
        ("riga %04d di prova, offset oltre il blocco\r\n" % i).encode()
        for i in range(120))
    note.dim = len(note.dati)

    docs = Voce("documenti", is_dir=True)
    docs.figli = [note]

    radice.figli = [leggimi, docs, hello]
    return radice


# =============================================================================
def main():
    uscita = None
    da = None
    etichetta = "EXOS"
    joliet = True
    prova = False
    avvio_file = None

    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--da" and i + 1 < len(argv):
            da = argv[i + 1]; i += 2
        elif a == "--etichetta" and i + 1 < len(argv):
            etichetta = argv[i + 1]; i += 2
        elif a == "--senza-joliet":
            joliet = False; i += 1
        elif a == "--avvio" and i + 1 < len(argv):
            avvio_file = argv[i + 1]; i += 2
        elif a == "--prova":
            prova = True; i += 1
        elif a.startswith("--"):
            print("mkiso: opzione sconosciuta: %s" % a)
            return 1
        else:
            uscita = a; i += 1

    if uscita is None:
        uscita = "/tmp/exos.iso"

    if prova:
        radice = albero_di_prova()
        if etichetta == "EXOS":
            etichetta = "EXOS TEST CD"
    elif da:
        if not os.path.isdir(da):
            print("mkiso: '%s' non e' una directory" % da)
            return 1
        radice = leggi_albero(da)
        if not radice.figli:
            print("mkiso: '%s' e' vuota: non c'e' niente da masterizzare" % da)
            return 1
    else:
        print(__doc__)
        return 1

    # ! L'IMMAGINE DI AVVIO ENTRA COME UN FILE NORMALE, e non e' un
    # dettaglio estetico: il BIOS la raggiunge dal catalogo senza passare
    # per la directory, quindi tecnicamente potrebbe restare invisibile.
    # Un CD in cui l'avvio non si vede pero' e' un CD che nessuno sa piu'
    # rifare ne' verificare — e chi lo apre su un altro sistema non trova
    # traccia di come parta.
    avvio = None
    if avvio_file:
        if not os.path.isfile(avvio_file):
            print("mkiso: immagine di avvio '%s' non trovata" % avvio_file)
            return 1
        dati = open(avvio_file, "rb").read()
        if len(dati) != 1474560:
            print("mkiso: '%s' e' di %d byte: l'emulazione floppy vuole "
                  "esattamente 1474560 (1.44 MB)" % (avvio_file, len(dati)))
            return 1
        avvio = Voce("boot.img", is_dir=False)
        avvio.dati = dati
        avvio.dim = len(dati)
        radice.figli.append(avvio)

    img, totale = costruisci(radice, etichetta, joliet, avvio)

    with open(uscita, "wb") as fh:
        fh.write(img)

    def conta(voce):
        f = d = 0
        for v in voce.figli:
            if v.is_dir:
                d += 1
                sf, sd = conta(v)
                f += sf
                d += sd
            else:
                f += 1
        return f, d

    n_file, n_dir = conta(radice)
    print("mkiso: %s — %d file, %d directory, %d blocchi (%d KB), nomi %s"
          % (uscita, n_file, n_dir, totale, totale * BLOCCO // 1024,
             "Joliet + ISO 9660" if joliet else "solo ISO 9660"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

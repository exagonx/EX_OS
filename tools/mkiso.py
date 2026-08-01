#!/usr/bin/env python3
"""Costruisce un'immagine ISO 9660 di prova per il driver CD/DVD di EX-OS.

Perche' esiste: sull'ambiente di sviluppo non c'e' genisoimage/xorriso, e
serviva comunque un'immagine di cui si conosce ogni byte — cosi' quando il
driver legge un nome sbagliato si sa esattamente cosa c'era scritto.

Cosa produce:
  - un descrittore primario (PVD) con i nomi ISO 9660 classici, maiuscoli
    e con il numero di versione (LEGGIMI.TXT;1);
  - un descrittore supplementare Joliet, con GLI STESSI file ma nomi
    lunghi in UCS-2 — e' la struttura che il driver deve preferire;
  - una sottodirectory, per provare la risoluzione a piu' livelli;
  - un file piu' grande di un blocco, per provare la lettura oltre i
    2048 byte (che e' il caso in cui un driver che ignora l'offset si
    accorge di essere sbagliato).

Uso:  python3 tools/mkiso.py /percorso/uscita.iso [--senza-joliet]

Con --senza-joliet l'immagine ha il solo albero ISO 9660: serve a provare
il ramo dei nomi maiuscoli con il numero di versione, che e' quello che il
driver usa sui dischi piu' vecchi e che senza questa opzione non verrebbe
mai eseguito.
"""

import struct
import sys

BLOCCO = 2048


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def data7(anno=2026, mese=8, giorno=1, ora=12, minuto=0, secondo=0):
    return bytes([anno - 1900, mese, giorno, ora, minuto, secondo, 0])


def data17():
    return b"2026080112000000" + bytes([0])


def rec_dir(nome_bytes, extent, dim, is_dir):
    """Un record di directory. La lunghezza e' SEMPRE pari: il byte di
    riempimento c'e' quando il nome ha lunghezza pari (33 + pari = dispari)."""
    ln = 33 + len(nome_bytes)
    pad = ln % 2
    rec = bytearray()
    rec.append(ln + pad)
    rec.append(0)                       # lunghezza attributi estesi
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


def nome_iso(n):
    return n.encode("ascii")


def nome_joliet(n):
    return n.encode("utf-16-be")


def contenuto_dir(voci, extent_self, dim_self, extent_padre, dim_padre):
    """voci = [(nome_bytes, extent, dim, is_dir)] gia' ordinate."""
    out = bytearray()
    out += rec_dir(b"\x00", extent_self, dim_self, True)
    out += rec_dir(b"\x01", extent_padre, dim_padre, True)
    for nome, ext, dim, isdir in voci:
        r = rec_dir(nome, ext, dim, isdir)
        # Un record non attraversa mai il confine del blocco: se non ci
        # sta, si riempie di zeri fino al blocco successivo.
        if len(out) % BLOCCO + len(r) > BLOCCO:
            out += b"\x00" * (BLOCCO - len(out) % BLOCCO)
        out += r
    return bytes(out)


def descrittore(tipo, etichetta, blocchi_volume, root_rec, joliet=False,
                pt_dim=0, pt_le=0, pt_be=0):
    d = bytearray(b"\x00" * BLOCCO)
    d[0] = tipo
    d[1:6] = b"CD001"
    d[6] = 1
    d[7] = 0
    ident = etichetta.encode("utf-16-be") if joliet else etichetta.encode("ascii")
    riempi = b"\x00 " if joliet else b" "
    d[8:40] = (b"\x00 " * 16) if joliet else (b" " * 32)      # system id
    d[40:72] = (ident + riempi * 32)[:32]                     # volume id
    d[80:88] = both32(blocchi_volume)
    if joliet:
        d[88:91] = b"%/E"                                     # UCS-2 livello 3
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


def tabella_percorsi(voci, big):
    """voci = [(nome_bytes, extent, numero_padre)] — la radice per prima."""
    out = bytearray()
    for nome, extent, padre in voci:
        out.append(len(nome))
        out.append(0)
        out += struct.pack(">I" if big else "<I", extent)
        out += struct.pack(">H" if big else "<H", padre)
        out += nome
        if len(nome) % 2:
            out.append(0)
    return bytes(out)


def main():
    argomenti = [a for a in sys.argv[1:] if not a.startswith("--")]
    joliet = "--senza-joliet" not in sys.argv[1:]
    uscita = argomenti[0] if argomenti else "/tmp/exos-test.iso"

    # --- Contenuto dei file -------------------------------------------
    leggimi = ("EX-OS: prova del driver CD/DVD.\r\n"
               "Se leggi questa riga, ISO 9660 funziona.\r\n").encode("ascii")
    hello = b"ciao dal CD\r\n"
    # Piu' di un blocco: il driver deve leggere anche oltre i 2048 byte,
    # e ogni riga dice il proprio numero, cosi' un salto si vede subito.
    note = b"".join(("riga %04d di prova, offset oltre il blocco\r\n" % i).encode()
                    for i in range(120))

    # --- Assegnazione dei blocchi -------------------------------------
    b_pvd, b_svd, b_term = 16, 17, 18
    b_pt_le, b_pt_be = 19, 20
    b_root_iso, b_docs_iso = 21, 22
    b_root_jol, b_docs_jol = 23, 24
    b_leggimi, b_hello, b_note = 25, 26, 27

    def n_blocchi(dati):
        return (len(dati) + BLOCCO - 1) // BLOCCO

    b_note = b_hello + n_blocchi(hello)
    fine = b_note + n_blocchi(note)
    totale = fine

    # --- Directory ISO -------------------------------------------------
    voci_root_iso = sorted([
        (nome_iso("DOCS"), b_docs_iso, BLOCCO, True),
        (nome_iso("HELLO.TXT;1"), b_hello, len(hello), False),
        (nome_iso("LEGGIMI.TXT;1"), b_leggimi, len(leggimi), False),
    ])
    voci_docs_iso = [(nome_iso("NOTE.TXT;1"), b_note, len(note), False)]

    root_iso = contenuto_dir(voci_root_iso, b_root_iso, BLOCCO,
                             b_root_iso, BLOCCO)
    docs_iso = contenuto_dir(voci_docs_iso, b_docs_iso, BLOCCO,
                             b_root_iso, BLOCCO)

    # --- Directory Joliet: stessi dati, nomi veri ----------------------
    voci_root_jol = sorted([
        (nome_joliet("documenti"), b_docs_jol, BLOCCO, True),
        (nome_joliet("hello.txt;1"), b_hello, len(hello), False),
        (nome_joliet("Leggimi importante.txt;1"), b_leggimi, len(leggimi), False),
    ])
    voci_docs_jol = [(nome_joliet("note-lunghe.txt;1"), b_note, len(note), False)]

    root_jol = contenuto_dir(voci_root_jol, b_root_jol, BLOCCO,
                             b_root_jol, BLOCCO)
    docs_jol = contenuto_dir(voci_docs_jol, b_docs_jol, BLOCCO,
                             b_root_jol, BLOCCO)

    pt = [(b"\x00", b_root_iso, 1), (nome_iso("DOCS"), b_docs_iso, 1)]
    pt_le = tabella_percorsi(pt, big=False)
    pt_be = tabella_percorsi(pt, big=True)

    rec_root_iso = rec_dir(b"\x00", b_root_iso, BLOCCO, True)
    rec_root_jol = rec_dir(b"\x00", b_root_jol, BLOCCO, True)

    pvd = descrittore(1, "EXOS TEST CD", totale, rec_root_iso, joliet=False,
                      pt_dim=len(pt_le), pt_le=b_pt_le, pt_be=b_pt_be)
    svd = descrittore(2, "EXOS TEST CD", totale, rec_root_jol, joliet=True,
                      pt_dim=len(pt_le), pt_le=b_pt_le, pt_be=b_pt_be)

    term = bytearray(b"\x00" * BLOCCO)
    term[0] = 255
    term[1:6] = b"CD001"
    term[6] = 1

    # --- Scrittura -----------------------------------------------------
    img = bytearray(b"\x00" * (totale * BLOCCO))

    def metti(blocco, dati):
        img[blocco * BLOCCO:blocco * BLOCCO + len(dati)] = dati

    metti(b_pvd, pvd)
    if joliet:
        metti(b_svd, svd)
        metti(b_term, bytes(term))
    else:
        # Senza la SVD il terminatore prende il suo posto: la catena dei
        # descrittori non ammette buchi, e un blocco di zeri in mezzo
        # sarebbe una firma mancante invece che una fine.
        metti(b_svd, bytes(term))
    metti(b_pt_le, pt_le)
    metti(b_pt_be, pt_be)
    metti(b_root_iso, root_iso)
    metti(b_docs_iso, docs_iso)
    metti(b_root_jol, root_jol)
    metti(b_docs_jol, docs_jol)
    metti(b_leggimi, leggimi)
    metti(b_hello, hello)
    metti(b_note, note)

    with open(uscita, "wb") as fh:
        fh.write(img)

    print("scritto %s: %d blocchi (%d byte)" % (uscita, totale, len(img)))
    print("  /leggimi.txt          %d byte   (Joliet: 'Leggimi importante.txt')"
          % len(leggimi))
    print("  /hello.txt            %d byte" % len(hello))
    print("  /docs/note.txt        %d byte   (Joliet: '/documenti/note-lunghe.txt')"
          % len(note))
    return 0


if __name__ == "__main__":
    sys.exit(main())

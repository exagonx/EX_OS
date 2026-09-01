#!/usr/bin/env python3
"""Guarda dentro il file che QEMU ha registrato e dice cosa ha suonato EX-OS.

    python3 tools/prove/audio_wav.py uscita.wav [minimo_suoni]

! LA PROVA CHE CONTA NON E' «il driver ha detto ok». Il driver puo' contare
interrupt e vedere il contatore DMA avanzare mentre manda alla scheda campioni
sbagliati — formato invertito, byte scambiati, la meta' del buffer riempita al
posto dell'altra. Tutto quello si SENTE e nulla di quello si vede da dentro.
Qui si guarda l'uscita: l'audiodev `wav` di QEMU scrive su file esattamente i
campioni che sarebbero andati all'altoparlante.

Cosa fa, in tre passi:

  1. spezza il file nei SUONI, cioe' nei tratti separati da silenzio. Una
     sessione di prova ne contiene parecchi — le prove del collaudo, il file
     WAV, le note MIDI — e misurarli insieme li diluirebbe l'uno nell'altro.
  2. di ognuno stima la FREQUENZA con l'autocorrelazione: si cerca il ritardo
     a cui l'onda somiglia di piu' a se stessa, ed e' il suo periodo. Non si
     assume quale nota debba essere: si legge quale nota E'.
  3. di ognuno misura la PUREZZA a quella frequenza con un Goertzel. Un tono
     vero supera il 40%; il rumore bianco — memoria non inizializzata mandata
     all'altoparlante — sta sotto il 5%, e la differenza non e' opinabile.
"""
import math
import struct
import sys


def leggi(percorso):
    # ! IL FILE DI QEMU HA LE LUNGHEZZE A ZERO, e il modulo `wave` lo rifiuta.
    # Non e' corrotto: l'audiodev `wav` scrive l'intestazione all'apertura e ci
    # torna sopra solo se QEMU esce in modo ordinato — e una prova automatica
    # QEMU lo AMMAZZA. Leggendo a mano si prende cio' che c'e' davvero.
    with open(percorso, "rb") as fh:
        dati = fh.read()

    if len(dati) < 44 or dati[:4] != b"RIFF" or dati[8:12] != b"WAVE":
        raise ValueError("non e' un file WAV")

    canali = struct.unpack("<H", dati[22:24])[0]
    rate   = struct.unpack("<I", dati[24:28])[0]
    bit    = struct.unpack("<H", dati[34:36])[0]
    if bit != 16:
        raise ValueError("campioni da %d bit: non li leggo" % bit)

    g = dati[44:]
    g = g[:len(g) - (len(g) % (2 * canali))]
    camp = struct.unpack("<%dh" % (len(g) // 2), g)
    if canali > 1:
        camp = camp[0::canali]
    return rate, list(camp)


def frequenza(camp, rate):
    """Il periodo per autocorrelazione, fra 80 e 2000 Hz."""
    if len(camp) < 64:
        return 0.0

    lag_min = max(2, rate // 2000)
    lag_max = min(len(camp) // 2, rate // 80)
    if lag_max <= lag_min:
        return 0.0

    # Si toglie la componente continua: un campione a 8 bit senza segno mal
    # convertito ha una media lontana da zero, e l'autocorrelazione di una
    # costante e' massima ovunque.
    media = sum(camp) / float(len(camp))
    x = [v - media for v in camp]

    corr = {}
    meglio, dove = 0.0, 0
    for lag in range(lag_min, lag_max):
        s = 0.0
        for i in range(0, len(x) - lag, 2):     # a passo due: basta e costa meta'
            s += x[i] * x[i + lag]
        corr[lag] = s
        if s > meglio:
            meglio, dove = s, lag

    if not dove:
        return 0.0

    # ! L'ERRORE DI OTTAVA, e va corretto o la misura mente di un fattore due.
    # Un'onda periodica somiglia a se stessa anche a DUE periodi di distanza,
    # e su un suono ricco di armoniche — la sintesi FM, per esempio — la
    # correlazione al doppio del periodo puo' superare quella al periodo vero.
    # Il risultato e' una nota giusta letta un'ottava sotto, con una purezza
    # che crolla perche' si misura a una frequenza che nel suono non c'e'.
    # Se meta' del ritardo trovato correla quasi altrettanto, il periodo e'
    # quello: fra due spiegazioni si prende la piu' corta.
    for divisore in (4, 3, 2):
        corto = dove // divisore
        if corto >= lag_min and corr.get(corto, 0.0) > meglio * 0.75:
            dove = corto
            break

    return rate / float(dove)


def purezza(camp, rate, hz):
    if hz <= 0 or len(camp) < 32:
        return 0.0
    k = 2.0 * math.cos(2.0 * math.pi * hz / rate)
    s1 = s2 = 0.0
    tot = 0.0
    for v in camp:
        x = float(v)
        s0 = x + k * s1 - s2
        s2, s1 = s1, s0
        tot += x * x
    pot = s1 * s1 + s2 * s2 - k * s1 * s2
    return (pot / (len(camp) / 2.0)) / tot if tot > 0 else 0.0


def nome_nota(hz):
    """La nota piu' vicina, per chi legge: «440 Hz» dice meno di «la4»."""
    if hz <= 0:
        return ""
    nomi = ["do", "do#", "re", "re#", "mi", "fa",
            "fa#", "sol", "sol#", "la", "la#", "si"]
    n = int(round(12 * math.log(hz / 440.0, 2))) + 69
    if n < 0 or n > 127:
        return ""
    return "%s%d" % (nomi[n % 12], n // 12 - 1)


def analizza(percorso, minimo=1):
    rate, camp = leggi(percorso)
    print("file      %s" % percorso)
    print("formato   %d Hz, %d campioni (%.1f s)"
          % (rate, len(camp), len(camp) / float(rate)))

    # --- i blocchi da 25 ms, con il loro livello ---
    blocco = max(1, rate // 40)
    livelli = []
    for i in range(0, len(camp) - blocco, blocco):
        s = 0.0
        for v in camp[i:i + blocco:4]:
            s += float(v) * v
        livelli.append(math.sqrt(s / (blocco / 4.0)) / 32768.0)

    if not livelli:
        print("\nESITO: il file non contiene niente.")
        return 1

    soglia = max(0.01, max(livelli) * 0.10)

    # --- i suoni: gruppi di blocchi sopra soglia ---
    suoni, dentro, inizio = [], False, 0
    for i, l in enumerate(livelli):
        if l > soglia and not dentro:
            dentro, inizio = True, i
        elif l <= soglia and dentro:
            dentro = False
            if i - inizio >= 2:
                suoni.append((inizio, i))
    if dentro and len(livelli) - inizio >= 2:
        suoni.append((inizio, len(livelli)))

    if not suoni:
        print("livello massimo %.2f%%" % (max(livelli) * 100))
        print("\nESITO: SILENZIO. Il driver puo' aver contato interrupt a vuoto.")
        return 1

    print("\n  quando   durata  livello  frequenza      purezza")
    print("  -------  ------  -------  -------------  -------")

    buoni = 0
    for a, b in suoni[:24]:
        t0 = a * blocco / float(rate)
        dur = (b - a) * blocco / float(rate)
        liv = max(livelli[a:b])

        # ! CINQUE FINESTRE DENTRO IL SUONO, NON UNA IN MEZZO. Un «suono» qui
        # e' un tratto senza silenzio, e un arpeggio di quattro note e' UN
        # tratto solo: la finestra centrale casca sul passaggio fra la seconda
        # e la terza nota, misura due frequenze insieme e le da' per sporche.
        # Con la sola finestra centrale, un arpeggio suonato perfettamente
        # risultava «suono sporco» — cioe' la prova diceva che era rotto
        # proprio cio' che funzionava.
        fin = min(int(rate * 0.08), (b - a) * blocco // 2)
        if fin < 64:
            continue

        f, p = 0.0, 0.0
        for q in range(1, 6):
            centro = (a * blocco) + ((b - a) * blocco * q) // 6
            tratto = camp[centro - fin // 2: centro + fin // 2]
            if len(tratto) < 64:
                continue
            ff = frequenza(tratto, rate)
            pp = purezza(tratto, rate, ff)
            if pp > p:
                f, p = ff, pp

        # ! LA SOGLIA E' AL 12% E NON PIU' IN ALTO, E IL MOTIVO E' LA SINTESI
        # FM. Un tono PCM generato da una tavola di seno arriva sopra il 90%:
        # e' quasi solo la fondamentale. Una voce dell'OPL2 ha una modulante
        # che mette meta' dell'energia nelle armoniche, e alla fondamentale ne
        # resta il 15-25% — cioe' un MIDI suonato benissimo. Chiedere il 30%
        # boccerebbe la sintesi FM proprio quando funziona.
        # Il rumore bianco, che e' cio' che questa prova deve prendere, sta
        # sotto il 5% e non ci arriva comunque.
        if p > 0.12:
            buoni += 1

        print("  %6.2fs  %5.2fs  %6.1f%%  %7.1f Hz %-5s %6.1f%%"
              % (t0, dur, liv * 100, f, nome_nota(f), p * 100))

    if len(suoni) > 24:
        print("  ... e altri %d suoni" % (len(suoni) - 24))

    print("\n%d suoni, %d con una frequenza riconoscibile" % (len(suoni), buoni))

    if buoni < minimo:
        print("\nESITO: NO. C'e' energia ma non e' fatta di toni:")
        print("       formato sbagliato, byte scambiati, o buffer non riempito.")
        return 1

    print("\nESITO: la scheda ha suonato, e cio' che e' uscito sono toni.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(analizza(sys.argv[1],
                      int(sys.argv[2]) if len(sys.argv) > 2 else 1))

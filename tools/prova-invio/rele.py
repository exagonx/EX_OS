#!/usr/bin/env python3
# =============================================================================
# tools/prova-invio/rele.py - a network cable between two QEMU machines that
# behaves like a real line: delay, a bandwidth, a router queue, and losses
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     rele.py PORTA_A PORTA_B [--ritardo MS] [--kbit K] [--coda N]
#             [--perdi PERMILLE] [--seme N]
#
# Each QEMU uses "-netdev socket,connect=:PORTA" (EXOS_NET_SOCKET=connect:P in
# tools/qemu_drive.py). The QEMU socket protocol is a 4-byte big-endian length
# and the frame. Every frame waits --ritardo ms (one way), leaves at --kbit
# kbit/s, is thrown away when --coda frames are already waiting in that
# direction (a router's tail drop), and is lost --perdi times in a thousand.
#
# ! WHY NOT QEMU'S USER NETWORK: slirp ends the TCP connection inside QEMU and
# ACKs at once, so the sender never sees a round trip and its window never
# matters. Here both ends are EX-OS and every ACK crosses the same wire.
#
# At the end (both sides closed, or SIGTERM) it prints the frames carried and
# the ones dropped, per direction, on stdout.
# =============================================================================
import argparse
import asyncio
import random
import signal
import struct
import sys
import time


class Verso:
    def __init__(self, nome, a):
        self.nome = nome
        self.a = a
        self.coda = []           # (when it may leave, frame)
        self.libero = 0.0        # when the line is free again
        self.portati = self.persi = self.pieni = 0
        self.scrittore = None
        self.evento = asyncio.Event()
        self.traccia = None

    def scrivi(self, frame, esito):
        # --traccia: one line per TCP segment: time, direction, seq, ack,
        # length, window, flags, and what the wire did with it
        if self.traccia is None or len(frame) < 54 or frame[12:14] != b"\x08\x00" or frame[23] != 6:
            return
        ihl = (frame[14] & 15) * 4
        t = 14 + ihl
        tot = struct.unpack(">H", frame[16:18])[0]
        off = (frame[t + 12] >> 4) * 4
        seq, ack = struct.unpack(">II", frame[t + 4:t + 12])
        fin = struct.unpack(">H", frame[t + 14:t + 16])[0]
        n = tot - ihl - off
        self.traccia.write("%.4f %s %u %u %d %d %02x %s\n" % (
            time.monotonic(), self.nome, seq, ack, n, fin, frame[t + 13], esito))

    def entra(self, frame):
        ora = time.monotonic()
        # ! THE ROUTER QUEUE IS WHAT WAITS FOR THE LINE, not what is already
        # on its way: the time until the line is free, in full frames
        # (1514 bytes). The first version counted the frames in flight too,
        # and dropped at a third of the queue it claimed.
        attesa = max(0.0, self.libero - ora)
        if attesa > self.a.coda * 1514 * 8.0 / (self.a.kbit * 1000.0):
            self.pieni += 1
            self.scrivi(frame, "pieno")
            return
        if self.a.perdi and random.randrange(1000) < self.a.perdi:
            self.persi += 1
            self.scrivi(frame, "perso")
            return
        self.scrivi(frame, "ok")
        parte = max(ora, self.libero) + len(frame) * 8.0 / (self.a.kbit * 1000.0)
        self.libero = parte
        self.coda.append((parte + self.a.ritardo / 1000.0, frame))
        self.evento.set()

    async def gira(self):
        while True:
            if not self.coda:
                self.evento.clear()
                await self.evento.wait()
                continue
            quando, frame = self.coda[0]
            attesa = quando - time.monotonic()
            if attesa > 0:
                await asyncio.sleep(attesa)
                continue
            self.coda.pop(0)
            if self.scrittore is not None:
                self.scrittore.write(struct.pack(">I", len(frame)) + frame)
                self.portati += 1


async def main():
    p = argparse.ArgumentParser()
    p.add_argument("porta_a", type=int)
    p.add_argument("porta_b", type=int)
    p.add_argument("--ritardo", type=float, default=25.0)
    p.add_argument("--kbit", type=float, default=20000.0)
    p.add_argument("--coda", type=int, default=100)
    p.add_argument("--perdi", type=int, default=0)
    p.add_argument("--seme", type=int, default=1)
    p.add_argument("--traccia")
    a = p.parse_args()
    random.seed(a.seme)

    ab = Verso("A->B", a)
    ba = Verso("B->A", a)
    if a.traccia:
        ab.traccia = ba.traccia = open(a.traccia, "w")
    fine = asyncio.Event()

    def stampa():
        for v in (ab, ba):
            print("rele: %s portati %d, persi %d, coda piena %d" %
                  (v.nome, v.portati, v.persi, v.pieni), flush=True)

    async def leggi(lettore, verso):
        try:
            while True:
                h = await lettore.readexactly(4)
                (n,) = struct.unpack(">I", h)
                verso.entra(await lettore.readexactly(n))
        except (asyncio.IncompleteReadError, ConnectionError):
            pass

    async def lato_a(r, w):
        ba.scrittore = w
        await leggi(r, ab)

    async def lato_b(r, w):
        ab.scrittore = w
        await leggi(r, ba)

    sa = await asyncio.start_server(lato_a, "127.0.0.1", a.porta_a)
    sb = await asyncio.start_server(lato_b, "127.0.0.1", a.porta_b)
    asyncio.get_running_loop().add_signal_handler(signal.SIGTERM, fine.set)
    print("rele: pronto su %d e %d, ritardo %g ms, %g kbit/s, coda %d, persi %d per mille"
          % (a.porta_a, a.porta_b, a.ritardo, a.kbit, a.coda, a.perdi), flush=True)
    giri = [asyncio.create_task(ab.gira()), asyncio.create_task(ba.gira())]
    await fine.wait()
    for g in giri:
        g.cancel()
    sa.close()
    sb.close()
    stampa()
    if a.traccia:
        ab.traccia.close()


if __name__ == "__main__":
    asyncio.run(main())
    sys.exit(0)

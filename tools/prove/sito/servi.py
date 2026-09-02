#!/usr/bin/env python3
# =============================================================================
# tools/prove/sito/servi.py
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
"""Serve le pagine di prova del browser, e RITARDA le immagini di proposito.

Vedi leggimi.md, accanto a questo file: c'e' l'elenco di cosa prova ognuna e
la riga di qemu_drive.py con cui si guardano.

Il ritardo non e' un trucco: serve a fotografare lo stato intermedio, cioe'
esattamente il momento in cui il testo e' gia' impaginato e i pixel non sono
ancora arrivati. Senza, le immagini di 2 KB su una rete locale arrivano prima
che si riesca a guardare.
"""
import os, sys, time
from http.server import BaseHTTPRequestHandler, HTTPServer

DIR = os.path.dirname(os.path.abspath(__file__))
RITARDO = float(os.environ.get("RITARDO", "6"))

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        nome = self.path.lstrip("/").split("?")[0] or "con.html"

        # ! DUE INDIRIZZI FINTI PER I BISCOTTI, e non sono file su disco: per
        # provare i cookie serve un server che ne METTA e uno che dica quali
        # gli sono arrivati. Sono le due meta' del giro, e senza la seconda si
        # puo' solo vedere che il browser non si rompe — non che il biscotto e'
        # davvero tornato indietro.
        if nome == "metti-biscotti":
            dati = open(os.path.join(DIR, "biscotti.html"), "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(dati)))
            # Uno normale e uno che gli script non devono vedere.
            self.send_header("Set-Cookie", "dalserver=abc123; Path=/")
            self.send_header("Set-Cookie", "segreto=nonsivede; Path=/; HttpOnly")
            self.end_headers()
            self.wfile.write(dati)
            return

        if nome == "eco-biscotti":
            avuti = self.headers.get("Cookie") or "(nessuno)"
            dati = avuti.encode("utf-8", "replace")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(dati)))
            self.end_headers()
            self.wfile.write(dati)
            return

        via = os.path.join(DIR, os.path.basename(nome))
        if not os.path.isfile(via):
            self.send_error(404); return

        if via.endswith(".png"):
            time.sleep(RITARDO)
            tipo = "image/png"
        else:
            tipo = "text/html"

        dati = open(via, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(dati)))
        self.end_headers()
        self.wfile.write(dati)

    def log_message(self, f, *a):
        sys.stderr.write("%.1f  %s\n" % (time.time() % 1000, f % a))

if __name__ == "__main__":
    HTTPServer(("0.0.0.0", 8000), H).serve_forever()

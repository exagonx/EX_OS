#!/usr/bin/env python3
# =============================================================================
# tools/prova_netupdate_http.py - a repository for netupdate, inside QEMU only
#
# One HTTP answer on stdin/stdout. QEMU starts it for every connection the
# guest opens to 10.0.2.100:80, with
#
#     -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:<script>
#
# where <script> is a one-line shell file (EXOS_QEMU_EXTRA is split on spaces):
#     exec python3 tools/prova_netupdate_http.py dist/netinst
# No socket is opened on the host. On the guest:  url = 10.0.2.100
#
# TRONCA=<piece of a path>:<bytes> cuts that file short, once: it is how the
# retry of a part was tried. A log is written beside this script's caller in
# the directory of the script (httpino.log).
# =============================================================================
import os, sys

radice = sys.argv[1]
ing, usc = sys.stdin.buffer, sys.stdout.buffer
riga = ing.readline().decode("latin-1", "replace")
while True:
    r = ing.readline()
    if not r or r in (b"\r\n", b"\n"):
        break
parti = riga.split()
perc = parti[1].split("?")[0] if len(parti) > 1 else "/"
f = os.path.normpath(os.path.join(radice, perc.lstrip("/")))
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "httpino.log"), "a") as lg:
    lg.write(perc + "\n")
if not f.startswith(os.path.normpath(radice)) or not os.path.isfile(f):
    usc.write(b"HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
    usc.flush()
    sys.exit(0)
dati = open(f, "rb").read()
dico = len(dati)
tronca = os.environ.get("TRONCA", "")
segno = os.path.join(os.path.dirname(os.path.abspath(__file__)), "httpino.troncato")
if tronca and not os.path.exists(segno):
    cosa, _, quanti = tronca.partition(":")
    if cosa in perc:
        dati = dati[:int(quanti)]
        open(segno, "w").close()
usc.write(("HTTP/1.0 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % dico).encode())
import time
def dico_log(t):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "httpino.log"), "a") as lg:
        lg.write(t + "\n")
i = 0
try:
    fd = usc.fileno(); usc.flush()
    while i < len(dati):
        i += os.write(fd, dati[i:i + 4096])
    dico_log("  finito %s: %d byte" % (perc, i))
    time.sleep(3)
except BaseException as ex:
    dico_log("  ECCEZIONE %s a %d: %r" % (perc, i, ex))

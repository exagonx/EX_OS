#!/usr/bin/env python3
# =============================================================================
# tools/exilla/segui.py — watch Firefox on a real machine over telnet
#
#     tools/exilla/segui.py tools HOST SECONDS DURATION LOGFILE
#     e.g. tools/exilla/segui.py tools 192.168.0.183 5 1780 /tmp/cq60.log
#
# Every SECONDS: `mem`, `ps`, the new kernel lines (`dmesg -u`) and, every
# third round, the new lines of ~/.exilla/firefox.log (exilla 0.006). One
# line only when something moves; all kernel lines go to LOGFILE, Firefox's
# to LOGFILE.firefox. ! telnetd takes ONE session: stop this before running
# tools/telnet-comandi.py on the same machine.
# =============================================================================
# One line only when something moves. Never exits because firefox is absent.
import re, socket, sys, time
import importlib.util
spec = importlib.util.spec_from_file_location("tc", sys.argv[1] + "/telnet-comandi.py")
tc = importlib.util.module_from_spec(spec); spec.loader.exec_module(tc)

host, ogni, durata = sys.argv[2], float(sys.argv[3]), float(sys.argv[4])

def connetti():
    s = socket.create_connection((host, 23), timeout=15)
    tc.leggi(s, 4); s.sendall(b"\r\n"); tc.leggi(s, 2)
    return s

def chiedi(s, cmd, att):
    s.sendall(cmd.encode() + b"\r\n")
    return tc.leggi(s, att)

s = connetti()
print("collegato a %s" % host, flush=True)
g_nati = [0]; g_morti = [0]
t0 = time.time(); prima = None; visti = set(); era_vivo = False
# kernel lines already there are not news
for l in chiedi(s, "dmesg -u", 4).splitlines():
    visti.add(l.strip())
# Firefox's own log (exilla 0.006): only the new bytes, if tail exists
LOG = "/root/.exilla/firefox.log"
ha_tail = "comando non trovato" not in chiedi(s, "tail -c 1 /bin/sh", 3)
print("tail sulla macchina: %s" % ("si" if ha_tail else "no, uso cat"), flush=True)
log_off = 0; log_righe_viste = 0; log_giro = 0

def pulisci(t, cmd):
    out = []
    for l in t.replace("\r", "").splitlines():
        if cmd in l or l.rstrip().endswith(">"):
            continue
        out.append(l)
    return out

def leggi_log():
    global log_off, log_righe_viste
    m = chiedi(s, "ls -l " + LOG, 3)
    g = re.search(r"(?:<FILE>|-[rwx-]{9}\s+\S+\s+\S+)\s+(\d+)\s", m)
    if not g:
        return []
    dim = int(g.group(1))
    if dim < log_off:             # a new run: the file started again
        log_off = 0; log_righe_viste = 0
    if dim == log_off:
        return []
    if ha_tail:
        cmd = "tail -c +%d %s" % (log_off + 1, LOG)
        righe = pulisci(chiedi(s, cmd, 4 + (dim - log_off) // 20000), cmd)
    else:
        if dim > 400000:
            log_off = dim
            return ["(log di %d byte: troppo per cat, salto)" % dim]
        cmd = "cat " + LOG
        righe = pulisci(chiedi(s, cmd, 4 + dim // 20000), cmd)[log_righe_viste:]
        log_righe_viste += len(righe)
    log_off = dim
    return righe
while time.time() - t0 < durata:
    try:
        m = chiedi(s, "mem", 3); p = chiedi(s, "ps", 4); d = chiedi(s, "dmesg -u", 4)
    except (OSError, socket.timeout) as e:
        print("telnet caduto (%s): riprovo" % e, flush=True)
        time.sleep(10)
        try: s = connetti()
        except OSError: pass
        continue
    tot = re.search(r"TOTALE\s+(\d+)\s+(\d+)\s+(\d+)", m)
    sw  = re.search(r"scambio \(swap\)\s+(\d+)\s+(\d+)", m)
    io  = re.search(r"dall'avvio: (\d+), tornate in RAM: (\d+)", m)
    if not tot or "PADRE" not in p:
        time.sleep(2); continue
    ff = [l.split() for l in p.splitlines() if "firefox" in l]
    stati = {}
    for f in ff:
        st = f[3] if len(f) > 3 else "?"
        stati[st] = stati.get(st, 0) + 1
    at = any("attesa" in l and "zombie" not in l for l in p.splitlines())
    # kernel messages not seen before; identical repeats counted once
    nuove = []
    for l in d.splitlines():
        k = l.strip()
        if not k or k in visti or k.startswith("dmesg") or "-----" in k or k.endswith(">"):
            continue
        visti.add(k); nuove.append(k)
    with open(sys.argv[5], "a") as tutto:
        for k in nuove: tutto.write("%6ds %s\n" % (time.time() - t0, k))
    importanti = [k for k in nuove if re.search(
        r"WARN|ERROR|PANIC|fault|esaurit|codice (?!0\b)|ucciso|segnale", k)]
    nati = sum(1 for k in nuove if "processo creato" in k and "firefox" in k)
    morti = sum(1 for k in nuove if "terminato con codice 0" in k and "firefox" in k)
    g_nati[0] += nati; g_morti[0] += morti
    for k in importanti[:10]:
        print("  KERNEL: " + k[:200], flush=True)
    if len(importanti) > 10:
        print("  KERNEL: ... altre %d righe importanti" % (len(importanti) - 10), flush=True)
    log_giro += 1
    if log_giro % 3 == 0:
        try:
            nuove_log = leggi_log()
        except (OSError, socket.timeout):
            nuove_log = []
        with open(sys.argv[5] + ".firefox", "a") as fl:
            for l in nuove_log: fl.write(l + "\n")
        utili = [l for l in nuove_log if "gNetworkLinkService" not in l and l.strip()]
        rumore = len(nuove_log) - len(utili)
        for l in utili[:12]:
            print("  FIREFOX: " + l[:220], flush=True)
        if len(utili) > 12:
            print("  FIREFOX: ... altre %d righe" % (len(utili) - 12), flush=True)
        if rumore > 0:
            print("  FIREFOX: (%d avvisi gNetworkLinkService)" % rumore, flush=True)
    st_txt = ",".join("%s=%d" % kv for kv in sorted(stati.items())) or "ASSENTE"
    riga = "%5ds RAM usata %4d MB libera %4d MB | swap %4d MB fuori %s dentro %s | firefox %d fili (%s)%s" % (
        time.time() - t0, int(tot.group(2)) // 1024, int(tot.group(3)) // 1024,
        int(sw.group(2)) // 1024 if sw else -1,
        io.group(1) if io else "?", io.group(2) if io else "?",
        len(ff), st_txt, " | attesa viva" if at else "") + " | fili nati %d finiti %d" % (g_nati[0], g_morti[0])
    chiave = (int(tot.group(2)) // 51200, sw.group(2) if sw else "",
              len(ff) // 10, tuple(sorted(stati)), at)
    if chiave != prima:
        print(riga, flush=True); prima = chiave
    if ff: era_vivo = True
    elif era_vivo:
        print("FIREFOX USCITO: ultimi messaggi del kernel sopra", flush=True)
        era_vivo = False
    time.sleep(ogni)
s.sendall(b"exit\r\n"); tc.leggi(s, 2); s.close()
print("fine monitoraggio", flush=True)

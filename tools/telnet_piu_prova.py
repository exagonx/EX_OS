#!/usr/bin/env python3
# tools/telnet_piu_prova.py PORT [HOST] - several telnet sessions at once (telnetd
# 0.015, voce 62). Three connections open together, each runs its own echo
# while the others stay open; the first closes, a fourth comes in and the
# two left still answer. Prints [OK]/[NO] lines, exit 0 when all pass.
import socket, sys, time

PORTA = int(sys.argv[1])
HOST = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"   # a real machine too
ok = no = 0

def esito(b, t):
    global ok, no
    print(("  [OK] " if b else "  [NO] ") + t)
    if b: ok += 1
    else: no += 1

class S:
    def __init__(s, nome):
        s.nome = nome
        s.k = socket.create_connection((HOST, PORTA), timeout=10)
        s.k.settimeout(0.3)
        s.dati = b""
    def leggi(s, sec):
        fine = time.time() + sec
        while time.time() < fine:
            try:
                d = s.k.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not d: break
            # the typed line comes back as echo: the quotes vanish only in the output
            # answer every DO/WILL with WONT/DONT except what telnetd wants
            i = 0
            while i < len(d):
                if d[i] == 255 and i + 2 < len(d) and d[i+1] in (251, 253):
                    s.k.sendall(bytes([255, 254 if d[i+1] == 251 else 252, d[i+2]]))
                    i += 3
                    continue
                s.dati += d[i:i+1]
                i += 1
    def comando(s, c, sec=4):
        s.k.sendall(c.encode() + b"\r\n")
        s.leggi(sec)
    def c_e(s, t):
        return t.encode() in s.dati

def apri(nome):
    try:
        s = S(nome); s.leggi(3); return s
    except OSError as e:
        esito(False, "%s: connessione rifiutata (%s)" % (nome, e)); return None

a = apri("uno"); b = apri("due"); c = apri("tre")
if not (a and b and c):
    print("=== %d riusciti, %d no ===" % (ok, no)); sys.exit(1)
# the last one first: up to 0.014 it was the mute one
for s in (c, b, a):
    s.comando('echo SONO-%s""-42' % s.nome.upper())
for s in (a, b, c):
    esito(s.c_e("SONO-%s-42" % s.nome.upper()),
          "%s: risponde con le altre aperte" % s.nome)
a.k.close()
time.sleep(2)
d = apri("quattro")
if d:
    d.comando('echo SONO-QUATTRO""-25')
    esito(d.c_e("SONO-QUATTRO-25"), "quattro: entra dopo la chiusura di uno")
for s in (b, c):
    s.dati = b""
    s.comando('echo ANCORA""-%s' % s.nome.upper())
    esito(s.c_e("ANCORA-%s" % s.nome.upper()), "%s: risponde ancora" % s.nome)
for s in (b, c, d):
    if s:
        s.comando("exit", 2); s.k.close()
print("=== %d riusciti, %d no ===" % (ok, no))
sys.exit(1 if no else 0)

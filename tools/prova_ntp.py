#!/usr/bin/env python3
# =============================================================================
# tools/prova_ntp.py - an NTP server for tools/prova_ntp.sh, on the host
#
#     python3 tools/prova_ntp.py PORT  ISO-UTC[,kod] ...
#
# Answers the Nth request with the Nth instant (UTC, with milliseconds, e.g.
# 2026-03-29T00:59:59.600); "kod" answers stratum 0 (a Kiss-o'-Death). The
# guest reaches it at 10.0.2.2:PORT through QEMU's user network. Each request
# is logged on stdout.
# =============================================================================
import datetime, socket, struct, sys

porta = int(sys.argv[1])
risposte = sys.argv[2:]
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", porta))
n = 0
while n < len(risposte):
    q, da = s.recvfrom(512)
    if len(q) < 48:
        continue
    voce = risposte[n]; n += 1
    kod = voce.endswith(",kod")
    t = datetime.datetime.fromisoformat(voce.split(",")[0]).replace(tzinfo=datetime.timezone.utc)
    x = t.timestamp() + 2208988800
    sec, fr = int(x), int((x - int(x)) * 2**32)
    r = bytearray(48)
    r[0] = 0x24                          # no warning, version 4, server
    r[1] = 0 if kod else 2               # stratum
    r[24:32] = q[40:48]                  # origin = the client's transmit
    r[32:40] = r[40:48] = struct.pack(">II", sec, fr)
    s.sendto(bytes(r), da)
    print("risposta %d: %s%s" % (n, voce, " (kod)" if kod else ""), flush=True)

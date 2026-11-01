#!/usr/bin/env python3
# =============================================================================
# tools/guardia-pc.py — keeps a remote EX-OS machine's countdown alive
#
#     tools/guardia-pc.py [host] [secondi] [ogni]
#
# Default 192.168.0.183, 120, 30. Every `ogni` seconds it opens a telnet
# session and runs `guardia <secondi>`: if the disk stops, the network falls or
# the machine freezes, the call does not arrive and the kernel resets it.
#
# ! EVERY 30 SECONDS AND NOT EVERY 120: with a call every 120 s against a
# countdown of 120 s, the smallest delay would reset a healthy machine.
# ! Ctrl+C TURNS THE COUNTDOWN OFF: stopping this script must not reset it.
# ! THE COUNTDOWN IS NOT SET AT BOOT: with this PC off the machine would reset
# itself forever. After a reset this script sets it again by itself.
# =============================================================================
import subprocess
import sys
import time
from pathlib import Path

TELNET = Path(__file__).with_name("telnet-comandi.py")
host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.183"
secondi = int(sys.argv[2]) if len(sys.argv) > 2 else 120
ogni = int(sys.argv[3]) if len(sys.argv) > 3 else 30


def chiama(valore):
    try:
        r = subprocess.run([sys.executable, "-I", str(TELNET), host,
                            f"guardia {valore}@2"], capture_output=True,
                           text=True, timeout=max(ogni - 5, 15))
    except subprocess.TimeoutExpired:
        return False
    return "guardia: attiva" in r.stdout or "guardia: spenta" in r.stdout


ultima = time.time()
print(f"{time.strftime('%H:%M:%S')} guardia su {host}: {secondi} s, una chiamata ogni {ogni} s")
stato = None
try:
    while True:
        t0 = time.time()
        ok = chiama(secondi)
        if ok:
            ultima = t0
        if ok != stato:
            print(f"{time.strftime('%H:%M:%S')} {'risponde' if ok else 'NON risponde'}", flush=True)
            stato = ok
        if not ok:
            print(f"{time.strftime('%H:%M:%S')} silenzio da {int(time.time() - ultima)} s", flush=True)
        time.sleep(max(1, ogni - (time.time() - t0)))
except KeyboardInterrupt:
    print("\nspengo la guardia:", "fatto" if chiama(0) else "NON RIUSCITO, si riavviera' da sola")

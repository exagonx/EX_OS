#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/thunderbird-exos/prova.sh - Thunderbird on EX-OS in QEMU (voce 103)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/thunderbird-exos/prova.sh [32|64]        64 by default
#     ATTESA=300 tools/thunderbird-exos/prova.sh     seconds for the first start (180)
#     TB_AMBIENTE="LD_DEBUG=libs" tools/thunderbird-exos/prova.sh   more variables
#                                                   for Thunderbird (space-separated)
#
# After tools/thunderbird-exos/costruisci.sh package (dist/thunderbird). A
# disk with Thunderbird, the fonts of ExWin and a profile with one POP3
# account and one SMTP server at 10.0.0.2 - QEMU's user network takes them to
# tools/thunderbird-exos/posta-finta.py on this host, which has one letter in
# the box and logs every command it gets. Then:
#   1. Thunderbird starts in ExWin and checks the mail: it asks the password
#      in its dialog, the keys type "prova" and Enter;
#   2. the letter PROVA-EXOS-ARRIVATA is fetched (RETR in the server's log)
#      and written in the profile (Mail/10.0.0.2/Inbox, read back with
#      debugfs);
#   3. ctrl-q closes it, a second start with -compose opens a letter with
#      subject PROVA-EXOS-INVIATA, ctrl-Enter sends it: the server must get it.
# The screenshots stay in cross_build/<macchina>/prova-thunderbird<bit>/:
# looking at them is part of the judgement.
# ! NO @ IN THE TYPED COMMANDS: tools/qemu_drive.py cuts "comando@secondi" at
# the first @, so the lines with an address are scripts on the disk.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
BIT="${1:-64}"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "$BIT" = 64 ]; then
    SUFF=64; CD="${CD:-$R/dist/exos64.iso}"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    SUFF="";  CD="${CD:-$R/dist/exos.iso}";   CPU=""; unset EXOS_QEMU
fi
P="${EXOS_FUORI}/cross_build/thunderbird-obj/tb$SUFF/dist/thunderbird"
D="${EXOS_FUORI}/cross_build/$MACCHINA/prova-thunderbird$BIT"
S="$D/disco"; IMG="$D/hd.img"; PART="$D/part.img"; POSTA="$D/posta"
[ -x "$P/thunderbird" ] || { echo "manca $P/thunderbird: prima tools/thunderbird-exos/costruisci.sh package" >&2; exit 1; }
rm -rf "${D:?}"; mkdir -p "$S/font" "$S/p" "$S/casa" "$S/tmp" "$D/foto" /tmp/exos

cp -a "$P" "$S/thunderbird"; cp exwin/font/*.ttf "$S/font/"
# the profile: one POP3 account, Local Folders, one SMTP server, and none of
# the first-run pages and questions
cat > "$S/p/user.js" <<'EOF'
user_pref("browser.dom.window.dump.enabled", true);
user_pref("mail.shell.checkDefaultClient", false);
user_pref("mail.rights.version", 1);
user_pref("mailnews.start_page.enabled", false);
user_pref("mail.provider.enabled", false);
user_pref("mail.biff.show_alert", false);
user_pref("mail.warn_on_send_accel_key", false);
// the dialog waits for the keys of the test: Thunderbird gives up an idle
// connection after 100 s ("Connection to server timed out")
user_pref("mailnews.tcptimeout", 900);
// what the protocols do, in tb.log (read back with debugfs)
user_pref("mailnews.pop3.loglevel", "All");
user_pref("mailnews.smtp.loglevel", "All");
user_pref("toolkit.sqlitejsm.loglevel", "Debug");
user_pref("datareporting.policy.dataSubmissionPolicyBypassNotification", true);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("app.update.disabledForTesting", true);
user_pref("mail.accountmanager.accounts", "account1,account2");
user_pref("mail.accountmanager.defaultaccount", "account1");
user_pref("mail.accountmanager.localfoldersserver", "server2");
user_pref("mail.account.account1.identities", "id1");
user_pref("mail.account.account1.server", "server1");
user_pref("mail.account.account2.server", "server2");
user_pref("mail.identity.id1.fullName", "Prova EX-OS");
user_pref("mail.identity.id1.useremail", "prova@exos.prova");
user_pref("mail.identity.id1.smtpServer", "smtp1");
user_pref("mail.identity.id1.valid", true);
user_pref("mail.server.server1.type", "pop3");
user_pref("mail.server.server1.hostname", "10.0.0.2");
user_pref("mail.server.server1.port", 11110);
user_pref("mail.server.server1.userName", "prova");
user_pref("mail.server.server1.name", "prova@exos.prova");
user_pref("mail.server.server1.authMethod", 3);
user_pref("mail.server.server1.socketType", 0);
user_pref("mail.server.server1.login_at_startup", true);
user_pref("mail.server.server1.leave_on_server", true);
user_pref("mail.server.server1.download_on_biff", true);
user_pref("mail.server.server1.directory-rel", "[ProfD]Mail/10.0.0.2");
user_pref("mail.server.server2.type", "none");
user_pref("mail.server.server2.hostname", "Local Folders");
user_pref("mail.server.server2.userName", "nobody");
user_pref("mail.server.server2.name", "Cartelle locali");
user_pref("mail.server.server2.directory-rel", "[ProfD]Mail/Local Folders");
user_pref("mail.smtpservers", "smtp1");
user_pref("mail.smtp.defaultserver", "smtp1");
user_pref("mail.smtpserver.smtp1.type", "smtp");
user_pref("mail.smtpserver.smtp1.hostname", "10.0.0.2");
user_pref("mail.smtpserver.smtp1.port", 10025);
user_pref("mail.smtpserver.smtp1.authMethod", 1);
user_pref("mail.smtpserver.smtp1.try_ssl", 0);
EOF
cat > "$S/avvia.sh" <<'EOF'
echo AVVIO-TB
/disk/thunderbird/thunderbird -no-remote -profile /disk/p > /disk/casa/tb.log 2>&1 &
EOF
cat > "$S/scrivi.sh" <<'EOF'
echo SCRIVI-TB
/disk/thunderbird/thunderbird -no-remote -profile /disk/p -compose "to='prova2@exos.prova',subject='PROVA-EXOS-INVIATA',body='Ciao da Thunderbird su EX-OS'" > /disk/casa/tb2.log 2>&1 &
EOF

MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 256 ))
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L tb -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"

python3 tools/thunderbird-exos/posta-finta.py "$POSTA" 11110 10025 &
SERVITORE=$!
trap 'kill $SERVITORE 2>/dev/null' EXIT

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=tb$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15 -device ne2k_pci,netdev=n1"
SER=/tmp/exos/serialtb$BIT.txt; rm -f "$SER"
F="$D/foto"
A=("netdetect -c@12" "dhcp@8" "mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@25")
for v in HOME=/disk/casa TMPDIR=/disk/tmp LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font $TB_AMBIENTE; do A+=("export $v@1"); done
A+=("exwin@14" "key:alt-f5@3" "foto:$F/fondo.ppm@1" "key:alt-f1@2"
    "sh /disk/avvia.sh@1" "key:alt-f5@${ATTESA:-180}" "foto:$F/avvio.ppm@1"
    # the password dialog of the mail check at start: the password, Tab to
    # "Use Password Manager to remember this password", space, Enter - the
    # second start (-compose) must not ask again (logins.json, key4.db)
    "key:p,r,o,v,a@2" "key:tab,spc@2" "foto:$F/password.ppm@1" "key:ret@60" "foto:$F/posta.ppm@1"
    "key:ctrl-q@40" "key:alt-f1@2"
    "sh /disk/scrivi.sh@1" "key:alt-f5@${ATTESA2:-150}" "foto:$F/scrivi.ppm@1"
    "key:ctrl-ret@40" "foto:$F/inviata.ppm@1"
    "key:alt-f1@2" "dmesg | grep -a 'FAULT\|SEGV'@4" "echo FINE-TB@2")
timeout "${TEMPO:-1500}" python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
kill $SERVITORE 2>/dev/null
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
for f in "$F"/*.ppm; do python3 tools/ppm2png.py "$f" "${f%.ppm}.png" > /dev/null 2>&1 && rm -f "$f"; done
# debugfs wants the partition: the disk image starts 1 MB in
dd if="$IMG" of="$PART" bs=1M skip=1 status=none
for f in casa/tb.log casa/tb2.log "p/Mail/10.0.0.2/Inbox" "p/Mail/Local Folders/Sent"; do
    /sbin/debugfs -R "dump \"/$f\" \"$D/$(basename "$f")\"" "$PART" > /dev/null 2>&1
done
rm -f "$PART"

esito=0; si() { echo "  [OK] $*"; }; no() { echo "  [NO] $*"; esito=1; }
cambiato() {   # percent of the screen that differs between two PNGs
    python3 - "$1" "$2" <<'PY' 2>/dev/null || echo 0
import sys, zlib, struct
def leggi(p):
    b = open(p, 'rb').read(); i = 8; dati = b''; w = h = 0
    while i < len(b):
        n, t = struct.unpack('>I4s', b[i:i+8])
        if t == b'IHDR': w, h = struct.unpack('>II', b[i+8:i+16])
        if t == b'IDAT': dati += b[i+8:i+8+n]
        i += 12 + n
    return w, h, zlib.decompress(dati)
w, h, a = leggi(sys.argv[1]); w2, h2, b = leggi(sys.argv[2])
if (w, h) != (w2, h2) or len(a) != len(b): print(100); sys.exit()
riga = len(a) // h; diversi = 0
for y in range(0, h, 4):
    for x in range(1, riga - 3, 12):
        k = y * riga + x
        if a[k:k+3] != b[k:k+3]: diversi += 1
print(int(100 * diversi / ((h // 4 + 1) * ((riga - 4) // 12 + 1))))
PY
}
if [ -f "$F/avvio.png" ] && [ -f "$F/fondo.png" ]; then
    c=$(cambiato "$F/fondo.png" "$F/avvio.png")
    [ "${c:-0}" -ge 10 ] && si "Thunderbird ha aperto la finestra: $c% dello schermo cambiato ($F/avvio.png)" \
        || no "la finestra di Thunderbird non si vede: $c% cambiato ($F/avvio.png)"
else
    no "niente fotografie ($D/qemu.log)"
fi
grep -q "password giusta" "$POSTA/posta.log" 2>/dev/null \
    && si "POP3: la password scritta nella finestra arriva al server" \
    || no "POP3: nessuna password giusta ($POSTA/posta.log: $(grep -c POP3 "$POSTA/posta.log" 2>/dev/null) righe POP3)"
grep -q "POP3 RETR 1" "$POSTA/posta.log" 2>/dev/null && si "POP3: la lettera e' scaricata (RETR 1)" \
    || no "POP3: nessun RETR"
grep -q "PROVA-EXOS-ARRIVATA" "$D/Inbox" 2>/dev/null && si "la lettera e' nella Posta in arrivo del profilo (Mail/10.0.0.2/Inbox)" \
    || no "la lettera non e' in Mail/10.0.0.2/Inbox ($(ls -la "$D/Inbox" 2>&1))"
grep -lq "PROVA-EXOS-INVIATA" "$POSTA"/inviata-*.eml 2>/dev/null && si "SMTP: la lettera scritta con -compose arriva al server" \
    || no "SMTP: nessuna lettera inviata ($(grep -c SMTP "$POSTA/posta.log" 2>/dev/null) righe SMTP)"
# (not the dmesg|grep line itself, echoed on the serial)
grep -a "FAULT\|SIGSEGV" "$D/seriale.txt" | grep -avq "dmesg" && no "FAULT sulla seriale ($D/seriale.txt)" || si "nessun FAULT"
grep -aq "FINE-TB" "$D/seriale.txt" || no "il sistema non e' arrivato in fondo (FINE-TB)"
[ $esito = 0 ] && echo "=== TUTTO A POSTO ($BIT bit) ===" || echo "=== QUALCOSA NON VA ($BIT bit): $D ==="
exit $esito

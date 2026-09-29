#!/bin/bash
# =============================================================================
# tools/prova_disposizione.sh — il CSS che dispone la pagina (@EXBROWSER-HTML5)
#
# Una pagina con un pezzo per ogni cosa nuova del 28 settembre 2026, ognuno
# col suo colore di sfondo, e il verdetto dalle POSIZIONI dei colori nella
# fotografia:
#   verde    max-width: 300px; margin: 0 auto  -> largo ~300 e centrato
#   magenta  e giallo: le due voci di un nav display:flex con flex:1 ->
#            sulla stessa riga, meta' riga ciascuna
#   blu      float: right; width: 150px -> largo ~150, sul bordo destro, e
#            con del testo scuro ALLA SUA SINISTRA sulle stesse righe
#   rosso    e ciano: due display:inline-block; width: 100px -> affiancati
#   arancio  un link in position:absolute; left:-9999px -> NON si vede
#   marrone  float: left, e il verde acqua con clear: both -> SOTTO il marrone
#   oliva, viola, rosa: justify-content: space-between -> ai due bordi e in
#            mezzo
#   lilla    (tre righe) e verde chiaro: align-items: center; gap: 30px ->
#            il verde chiaro a meta' altezza del lilla, 30 pixel dopo
#   corallo  position: relative; left: 40px -> 40 pixel a destra del bordo
#   acciaio  un'immagine 200x100 con width: 100% in una colonna flex da 90
#            -> larga 90 e alta 45 (le miniature dei risultati di Wikipedia)
#   vino     un <input type="image" src=...> 60x30 -> si disegna la figura, non
#            un pulsante grigio (@NAVMETA)
#   mattone  width: 1500px -> si ferma al bordo della pagina, non va sopra la
#            barra di scorrimento (il ritaglio del 28 settembre)
#   e il testo del blu e' #FFFFFF: fino al 28 settembre il bianco era
#   CSS_NIENTE («nessun colore») e usciva nero
#
# Il lettore CSS si prova sull'host: make prova-excss.
#
#     tools/prova_disposizione.sh [directory-di-lavoro]   (default /tmp/exos-disp)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-disp}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

cat > "$D/disp.html" <<'PAGINA'
<html><head><style>
.salta { position: absolute; left: -9999px; color: #FF8000 }
.centro { max-width: 300px; margin: 0 auto; background: #20C040 }
nav { display: flex; margin-top: 10px }
nav a { flex: 1; background: #E020E0; color: #000000 }
nav a + a { background: #E0E020 }
.dx { float: right; width: 150px; background: #2040E0; color: #FFFFFF }
.ib { display: inline-block; width: 100px; background: #E02020 }
</style></head><body>
<a class="salta" href="#x">SALTA AL CONTENUTO SALTA AL CONTENUTO</a>
<div class="centro">centro centro centro</div>
<nav><a href="#a">uno</a><a href="#b">due</a></nav>
<div class="dx">destra destra destra destra destra destra</div>
<p>Questo testo deve scorrere accanto al riquadro blu che galleggia a destra, riga dopo riga, finche' il riquadro non finisce e il testo torna largo quanto la pagina intera come prima.</p>
<p><span class="ib">uno</span> <span class="ib" style="background: #20E0E0">due</span></p>
<div style="float: left; width: 120px; background: #804000; color: #FFFFFF">sinistra<br>due<br>tre</div>
<div style="clear: both; background: #008080; color: #FFFFFF">dopo il clear</div>
<div style="display: flex; justify-content: space-between"><span style="background: #808000">aaa</span><span style="background: #400080; color: #FFFFFF">bbb</span><span style="background: #FF80C0">ccc</span></div>
<div style="display: flex; align-items: center; gap: 30px"><div style="background: #C0C0FF">alto<br>alto<br>alto</div><div style="background: #80FF80">basso</div></div>
<div style="position: relative; left: 40px; width: 100px; background: #FF6060">spostato</div>
<div style="width: 1500px; background: #A05050">largo millecinquecento pixel</div>
<div style="display: flex"><div style="width: 90px"><img src="blu.png" style="width: 100%"></div><div>testo accanto alla miniatura</div><form><input type="image" src="vino.png" name="p"></form></div>
</body></html>
PAGINA

# ! 64 MB E NON I 32 PREDEFINITI: con 32 il navigatore sta al limite, e una
# volta su tre moriva caricando exhttp.so («pagina non allocata») prima di
# disegnare — un verdetto sulla memoria, non sulla disposizione.
magick -size 200x100 xc:'#336699' "$D/blu.png"
magick -size 60x30 xc:'#902040' "$D/vino.png"
export EXOS_ISTANZA=disp EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/disp.html disp.html" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/blu.png blu.png" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/vino.png vino.png" "$OFF" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/disp.html &@15" "key:alt-f5@4" \
    "foto:$D/disp.ppm@1" > "$D/1.log" 2>&1

[ -s "$D/disp.ppm" ] || { echo "  [NO]  manca la fotografia"; exit 1; }
python3 - "$D/disp.ppm" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read(); c = d.split(b"\n", 3)
W, H = map(int, c[1].split()); px = c[3]
def col(x, y): i = (y * W + x) * 3; return px[i], px[i+1], px[i+2]
def vicino(p, q): return all(abs(a - b) < 30 for a, b in zip(p, q))
# ! SOLO LE RIGHE CON ALMENO 8 PIXEL DEL COLORE, e dei bordi la mediana: i
# pixel sfumati del testo somigliano per caso a un colore di sfondo, e al
# primo giro allargavano le scatole a mezza pagina.
def scatola(rgb):
    righe = {}
    for y in range(40, H - 40):
        xs = [x for x in range(W) if vicino(col(x, y), rgb)]
        if len(xs) >= 8: righe[y] = xs
    # e la fascia piu' lunga di righe consecutive: un riquadro e' uno solo
    fasce, cur = [], []
    for y in sorted(righe):
        if cur and y - cur[-1] > 2: fasce.append(cur); cur = []
        cur.append(y)
    if cur: fasce.append(cur)
    if not fasce: return None
    ys = max(fasce, key=lambda f: sum(len(righe[y]) for y in f))
    righe = {y: righe[y] for y in ys}
    tot = sum(len(v) for v in righe.values())
    if tot < 50: return None
    mi = sorted(min(v) for v in righe.values()); ma = sorted(max(v) for v in righe.values())
    return mi[len(mi) // 2], ys[0], ma[len(ma) // 2], ys[-1], tot
esito = 0
def dì(ok, testo):
    global esito
    print(("  [OK]  " if ok else "  [NO]  ") + testo)
    if not ok: esito = 1
V = scatola((0x20, 0xC0, 0x40)); M = scatola((0xE0, 0x20, 0xE0)); G = scatola((0xE0, 0xE0, 0x20))
B = scatola((0x20, 0x40, 0xE0)); R = scatola((0xE0, 0x20, 0x20)); C = scatola((0x20, 0xE0, 0xE0))
A = scatola((0xFF, 0x80, 0x00))
for n, s in (("verde", V), ("magenta", M), ("giallo", G), ("blu", B), ("rosso", R), ("ciano", C)):
    print("        %-8s %s" % (n, s))
# la pagina: dal primo all'ultimo pixel bianco della riga del verde
if V:
    y = (V[1] + V[3]) // 2
    bianchi = [x for x in range(W) if col(x, y) == (255, 255, 255)]
    p0, p1 = (min(bianchi), max(bianchi)) if bianchi else (0, W)
    w = V[2] - V[0] + 1
    sx, dx = V[0] - p0, p1 - V[2]
    dì(280 <= w <= 320 and abs(sx - dx) < 30, "max-width + margin auto: verde largo %d, a %d dal bordo sinistro e %d dal destro" % (w, sx, dx))
else: dì(False, "max-width + margin auto: il verde non c'e'")
if M and G:
    stessa = abs(M[1] - G[1]) < 6
    dì(stessa and G[0] >= M[2] - 2 and (M[2] - M[0]) > 200 and (G[2] - G[0]) > 200,
       "flex: magenta %d e giallo %d affiancati sulla stessa riga" % (M[2] - M[0] + 1, G[2] - G[0] + 1))
else: dì(False, "flex: manca il magenta o il giallo")
if B and V:
    wb = B[2] - B[0] + 1
    testo_a_sx = 0
    for y in range(B[1], B[3] + 1):
        for x in range(V[0] - 200 if V[0] > 200 else 0, B[0] - 4):
            r, g, b = col(x, y)
            if r < 90 and g < 90 and b < 90: testo_a_sx += 1
    dì(140 <= wb <= 160 and B[2] > V[2] + 100 and testo_a_sx > 100,
       "float: right: blu largo %d sul bordo destro, testo alla sua sinistra (%d pixel scuri)" % (wb, testo_a_sx))
else: dì(False, "float: il blu non c'e'")
if R and C:
    dì(abs(R[1] - C[1]) < 6 and 90 <= R[2] - R[0] + 1 <= 110 and C[0] > R[2],
       "inline-block: rosso %d e ciano %d affiancati" % (R[2] - R[0] + 1, C[2] - C[0] + 1))
else: dì(False, "inline-block: manca il rosso o il ciano")
dì(A is None, "position: absolute; left: -9999px: il link arancio non si vede")
if B:
    bianchi = sum(1 for y in range(B[1], B[3] + 1) for x in range(B[0], B[2] + 1) if min(col(x, y)) > 200)
    dì(bianchi > 50, "color: #FFFFFF nel riquadro blu e' bianco, non nero (%d pixel chiari)" % bianchi)
Mr = scatola((0x80, 0x40, 0x00)); T = scatola((0x00, 0x80, 0x80))
O = scatola((0x80, 0x80, 0x00)); Vi = scatola((0x40, 0x00, 0x80)); Ro = scatola((0xFF, 0x80, 0xC0))
Li = scatola((0xC0, 0xC0, 0xFF)); Vc = scatola((0x80, 0xFF, 0x80)); Co = scatola((0xFF, 0x60, 0x60))
for n, q in (("marrone", Mr), ("acqua", T), ("oliva", O), ("viola", Vi), ("rosa", Ro), ("lilla", Li), ("verdech", Vc), ("corallo", Co)):
    print("        %-8s %s" % (n, q))
sx0 = M[0] if M else 10
dx0 = G[2] if G else W - 60
if Mr and T: dì(T[1] > Mr[3], "clear: both: il verde acqua comincia a %d, sotto il marrone che finisce a %d" % (T[1], Mr[3]))
else: dì(False, "clear: manca il marrone o il verde acqua")
if O and Vi and Ro:
    dì(abs(O[0] - sx0) <= 4 and abs(Ro[2] - dx0) <= 4 and O[2] < Vi[0] < Vi[2] < Ro[0] and abs(O[1] - Ro[1]) < 4,
       "space-between: oliva a %d (bordo %d), rosa fino a %d (bordo %d), viola in mezzo" % (O[0], sx0, Ro[2], dx0))
else: dì(False, "space-between: manca un colore")
if Li and Vc:
    cl = (Li[1] + Li[3]) / 2; cv = (Vc[1] + Vc[3]) / 2
    dì(abs(cl - cv) <= 4 and abs((Vc[0] - Li[2] - 1) - 30) <= 3 and (Li[3] - Li[1]) > 40,
       "align-items: center e gap: centri a %.0f e %.0f, distanza %d" % (cl, cv, Vc[0] - Li[2] - 1))
else: dì(False, "align-items: manca il lilla o il verde chiaro")
if Co: dì(abs((Co[0] - sx0) - 40) <= 3, "position: relative; left: 40px: corallo a %d dal bordo" % (Co[0] - sx0))
else: dì(False, "position: relative: il corallo non c'e'")
Ac = scatola((0x33, 0x66, 0x99))
if Ac: dì(abs((Ac[2] - Ac[0] + 1) - 90) <= 2 and abs((Ac[3] - Ac[1] + 1) - 45) <= 2,
          "img width:100%% in una colonna da 90: %dx%d" % (Ac[2] - Ac[0] + 1, Ac[3] - Ac[1] + 1))
else: dì(False, "img width:100%: l'acciaio non c'e'")
Vn = scatola((0x90, 0x20, 0x40))
if Vn: dì(abs((Vn[2] - Vn[0] + 1) - 60) <= 2 and abs((Vn[3] - Vn[1] + 1) - 30) <= 2,
          "input type=image disegna la sua figura: %dx%d" % (Vn[2] - Vn[0] + 1, Vn[3] - Vn[1] + 1))
else: dì(False, "input type=image: la figura vino non c'e'")
Ma = scatola((0xA0, 0x50, 0x50))
if Ma: dì(Ma[2] <= dx0 + 2, "width: 1500px: il mattone si ferma a %d (bordo %d)" % (Ma[2], dx0))
else: dì(False, "width: 1500px: il mattone non c'e'")
sys.exit(esito)
EOF
esito=$?
echo "        fotografia: $D/disp.ppm"
exit $esito

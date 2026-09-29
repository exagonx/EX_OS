#!/bin/bash
# =============================================================================
# tools/prova_es2015.sh — la sintassi di oggi nel navigatore, coi due motori
# (@EXJS-LACUNE, 29 settembre 2026)
#
# La stessa pagina due volte: con QuickJS (il predefinito) e con ExJs (una casa
# su ext2 con "motore = exjs" nelle impostazioni). Lo script usa quello che le
# pagine di oggi usano - let e const, frecce, classi con extends e super,
# modelli `${}`, destrutturazione, spread, ?. e ??, Map, for..of, le
# espressioni regolari (dal 29 settembre anche in ExJs) - anche sul
# DOM vero (querySelectorAll, textContent), e finisce LANCIANDO un Error col
# suo esito: e' l'unica cosa di una pagina che arriva sulla seriale.
#
# ! DAL 29 SETTEMBRE EXJS SA LANCIARE: la stessa pagina va bene per tutti e
# due. prova_date.sh e' di prima, e per ExJs guarda ancora la riga di `(0)()`.
#
# Il comportamento fine si prova sull'host: make prova-exjs.
#
#     tools/prova_es2015.sh [directory-di-lavoro]   (default /tmp/exos-es2015)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-es2015}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
SER=/tmp/exos/seriales2015.txt

pagina() {   # nome-file etichetta
    cat > "$D/$1" <<PAGINA
<html><body><p class="v">uno</p><p class="v">due</p><div id="out"></div><script>
const no = [];
const prova = (nome, ok) => { if (!ok) no.push(nome); };
class Forma { constructor(n) { this.n = n; } area() { return 0; } get nome() { return 'forma ' + this.n; } }
class Quadrato extends Forma { constructor(l) { super('q'); this.l = l; } area() { return this.l ** 2 + super.area(); } }
const q = new Quadrato(3);
prova('classi', q.area() === 9 && q.nome === 'forma q' && q instanceof Forma);
const { a, b: [c, ...r] = [], d = 4 } = { a: 1, b: [2, 3, 4] };
prova('destrutturazione', a + c + r.length + d === 9);
const f = [];
for (let i = 0; i < 3; i++) f.push(() => i);
prova('let e chiusure', f.map(g => g()).join() === '0,1,2');
const ps = [...document.querySelectorAll('p.v')];
prova('spread di NodeList', ps.length === 2);
let testi = '';
for (const p of document.querySelectorAll('p.v')) testi += p.textContent;
prova('for..of sul DOM', testi === 'unodue');
document.getElementById('out').textContent = \`\${ps.length} paragrafi, \${testi.toUpperCase()}\`;
prova('modello nel DOM', document.getElementById('out').textContent === '2 paragrafi, UNODUE');
const m = new Map(ps.map((p, i) => [p.textContent, i]));
prova('Map', m.get('due') === 1 && m.size === 2);
const o = { x: { y: null } };
prova('?. e ??', (o?.x?.y?.z ?? 'vuoto') === 'vuoto' && o.w?.() === undefined);
prova('spread e resto', Math.max(...[1, 5, 3]) === 5 && ((...z) => z.length)(1, 2) === 2);
let e = '';
try { null.f(); } catch (err) { e = err instanceof TypeError ? 'T' : 'X'; }
prova('try e catch', e === 'T');
prova('regexp', /^(\\w+)@(\\w+)\\.it\$/.exec('io@casa.it')[2] === 'casa' &&
      'a1b22'.replace(/\\d+/g, n => '<' + n + '>') === 'a<1>b<22>' &&
      'x, y ,z'.split(/\\s*,\\s*/).join('') === 'xyz');
prova('regexp sul DOM', ps.filter(p => /^d/.test(p.textContent)).length === 1);
throw new Error(no.length ? 'ES-$2-NO ' + no.join(', ') : 'ES-$2-OK');
</script></body></html>
PAGINA
}
pagina q.html QJS
pagina e.html EXJS
printf 'motore = exjs\n' > "$D/imp.txt"

export EXOS_ISTANZA=es2015 EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
# ! 64 MB, COME LE ALTRE PROVE DEL NAVIGATORE. Il navigatore che a 32 MB ogni
# tanto non caricava una libreria era una corsa nel kernel (due processi che
# caricavano la stessa libreria insieme), corretta nel kernel 0.224: da li'
# questa prova passa anche a 32 MB (29 settembre 2026). Si resta a 64 per
# misurare quel che la prova cerca e non la memoria.
export EXOS_RAM=64M
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
for f in q.html e.html; do "$DEBUGFS" -w -R "write $D/$f $f" "$OFF" > /dev/null 2>&1; done
for d in casa casa/.app casa/.app/exbrowser; do "$DEBUGFS" -w -R "mkdir $d" "$OFF" > /dev/null 2>&1; done
"$DEBUGFS" -w -R "write $D/imp.txt casa/.app/exbrowser/impostazioni.txt" "$OFF" > /dev/null 2>&1

timeout 500 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser /disk/q.html &@15" "key:alt-f5@3" "foto:$D/q.ppm@0" \
    "key:alt-f1@2" "export HOME=/disk/casa@1" \
    "/exwin/bin/exbrowser /disk/e.html &@15" "key:alt-f5@3" "foto:$D/e.ppm@0" \
    > "$D/1.log" 2>&1

esito=0
for m in QJS EXJS; do
    r=$(tr -d '\r' < "$SER" | grep -o "ES-$m-[A-Z]*[^\"']*" | head -1)
    case "$r" in
        ES-$m-OK*) echo "  [OK]  $m: tutti i controlli passati" ;;
        "")        echo "  [NO]  $m: la pagina non ha detto niente (vedi $D/*.ppm e $SER)"; esito=1 ;;
        *)         echo "  [NO]  $m: $r"; esito=1 ;;
    esac
done
exit $esito

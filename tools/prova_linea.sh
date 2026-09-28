#!/bin/bash
# =============================================================================
# tools/prova_linea.sh — le primitive di disegno di Pennello, sull'host
# (28 settembre 2026)
#
# ! SI PROVANO LE FUNZIONI VERE, estratte da exwin/bin/pennello/pennello.c,
# non una copia dell'algoritmo: una copia giusta accanto a un originale
# sbagliato passerebbe. E' come si e' trovato il difetto: linea() su certi
# segmenti non finiva MAI (il doppio dell'errore ricalcolato a meta' passo), e
# Pennello col mouse veloce girava a vuoto per sempre.
#
#   - ogni segmento con gli estremi fra -40 e 40 finisce, e a spessore 1
#     disegna ESATTAMENTE max(|dx|,|dy|)+1 punti (la proprieta' di Bresenham);
#   - ogni ellisse e ogni rettangolo fino a 40x40, vuoti e pieni, finiscono.
#
# Un difetto qui e' un ciclo infinito: si gira sotto `timeout`.
#
#     tools/prova_linea.sh [directory-di-lavoro]
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-linea}"
mkdir -p "$D"
python3 - "$D/linea.c" <<'PY'
import sys
src = open('exwin/bin/pennello/pennello.c', encoding='utf-8').read()
def funzione(nome):
    i = src.index(nome + '(')
    i = src.rindex('\n', 0, i) + 1
    return src[i:src.index('\n}\n', i) + 3]
def riga(nome):
    i = src.index(nome + '(')
    i = src.rindex('\n', 0, i) + 1
    return src[i:src.index('\n', i) + 1]
pezzi = [funzione('static void tocca'), funzione('static void metti'), funzione('static void punto'),
         funzione('static void linea'), riga('static void ordina'), funzione('static void rettangolo'),
         funzione('static int ellisse_dentro'), funzione('static int ellisse_sinistra'),
         funzione('static void ellisse')]
open(sys.argv[1], 'w').write('''#include <stdio.h>
#define W 200
#define H 200
static unsigned int img[W * H]; static unsigned int *g_img = img; static int g_iw = W, g_ih = H;
static int g_sx0, g_sy0, g_sx1, g_sy1;
''' + "\n".join(pezzi) + '''
static void pulisci(void) { int i; for (i = 0; i < W * H; i++) img[i] = 0; }
static int conta(void) { int i, n = 0; for (i = 0; i < W * H; i++) if (img[i]) n++; return n; }
int main(void)
{
    int dx, dy, w, h, sbagliate = 0, fatte = 0;
    for (dx = -40; dx <= 40; dx++)
        for (dy = -40; dy <= 40; dy++) {
            int atteso = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
            pulisci();
            linea(100, 100, 100 + dx, 100 + dy, 1, 0xFFFFFF);
            fatte++;
            if (conta() != atteso + 1 || !img[(100 + dy) * W + 100 + dx]) {
                if (sbagliate < 5) printf("  *** linea (%d,%d): %d punti, attesi %d\\n", dx, dy, conta(), atteso + 1);
                sbagliate++;
            }
        }
    for (w = 0; w <= 40; w++)
        for (h = 0; h <= 40; h++) {
            pulisci(); ellisse(50, 50, 50 + w, 50 + h, 0, 3, 1); fatte++;
            pulisci(); ellisse(50 + w, 50 + h, 50, 50, 1, 1, 1); fatte++;
            pulisci(); rettangolo(50, 50, 50 + w, 50 + h, 0, 5, 1); fatte++;
        }
    printf("prova_linea: %d prove, %d sbagliate\\n", fatte, sbagliate);
    return sbagliate != 0;
}
''')
PY
gcc -O1 -Wall -o "$D/linea" "$D/linea.c" || exit 1
if ! timeout 20 "$D/linea"; then echo "prova_linea: NON FINISCE o sbaglia (un ciclo infinito e' il difetto di prima)"; exit 1; fi
echo "prova_linea: TUTTO BENE"

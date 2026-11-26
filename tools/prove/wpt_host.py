#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Graziano Falcone
#
# wpt_host.py - web-platform-tests sull'host, con la prova del DOM (domprova)
# e QuickJS, senza accendere QEMU (BROWSER002, P9 e sezione 8).
#
#   python3 tools/prove/wpt_host.py [-v] percorso [percorso...]
#
# Un percorso e' un file di prova (.html, .window.js, .any.js) o una cartella
# (tutti quelli che contiene, sottocartelle comprese) sotto terze_browser/wpt.
# Per ogni file si fa una pagina sola: il preludio del browser (preso da
# exwin/bin/exbrowser/browser_preludio.c, come al browser), testharness.js e
# gli altri <script src> messi in linea, e al posto di testharnessreport.js un
# rapporto nostro che stampa "WPT PASS|FAIL|TIMEOUT|NOTRUN nome | messaggio".
# Si fa girare /tmp/exos-prove/domqjs (make prova-exqjs) con DOMPROVA_CARICA=1
# (DOMContentLoaded e load dopo gli script, come il browser).
#
# ! E' IL METRO DEL P9, NON IL BROWSER: gli script aggiunti dopo il caricamento
# non partono qui (partono nel browser), e i tempi sono simulati. Quel che
# passa qui si riprova in QEMU.
#
# Uscita: una riga per file (passate/totale, o l'errore che l'ha fermato) e il
# totale; con -v ogni prova. Il codice d'uscita e' 0 sempre: e' una misura.

import os
import re
import subprocess
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
RADICE = os.path.abspath(os.path.join(QUI, '..', '..'))
WPT = os.path.join(RADICE, 'terze_browser', 'wpt')
DOMQJS = '/tmp/exos-prove/domqjs'
LAVORO = os.environ.get('WPT_LAVORO', os.path.expanduser('~/ex_os/prove-wpt'))

RAPPORTO = r"""
(function () {
  var nomi = ['PASS', 'FAIL', 'TIMEOUT', 'NOTRUN', 'PRECONDITION_FAILED'];
  var pulisci = function (s) { return String(s).replace(/[\r\n]+/g, ' ').slice(0, 300); };
  add_completion_callback(function (tests, status) {
    var i;
    for (i = 0; i < tests.length; i++)
      console.log('WPT ' + nomi[tests[i].status] + ' ' + pulisci(tests[i].name) +
                  (tests[i].message ? ' | ' + pulisci(tests[i].message) : ''));
    console.log('WPT FINE ' + status.status + (status.message ? ' | ' + pulisci(status.message) : ''));
  });
})();
"""


def preludio():
    s = open(os.path.join(RADICE, 'exwin/bin/exbrowser/browser_preludio.c'),
             encoding='utf-8').read()
    a = s.index('static const char PRELUDIO[] =')
    b = s.index(';\n', a)
    corpo = s[a:b]
    pezzi = re.findall(r'"((?:[^"\\]|\\.)*)"', corpo[corpo.index('=') + 1:])
    return ''.join(bytes(p, 'utf-8').decode('unicode_escape') for p in pezzi)


def in_linea(js):
    # un </script> dentro lo script chiuderebbe il tag
    return re.sub(r'</(script)', r'<\\/\1', js, flags=re.I)


def leggi_src(src, cartella):
    if src.startswith('/'):
        p = os.path.join(WPT, src.lstrip('/'))
    else:
        p = os.path.join(cartella, src)
    p = p.split('?')[0]
    if os.path.basename(p) == 'testharnessreport.js':
        return RAPPORTO
    try:
        return open(p, encoding='utf-8').read()
    except OSError:
        return "console.log('WPT MANCA lo script " + src.replace("'", '') + "');"


def pagina_da_html(testo, cartella):
    def sostituisci(m):
        attr = m.group(1)
        s = re.search(r'''src\s*=\s*["']?([^"'\s>]+)''', attr)
        if not s:
            return m.group(0)
        return '<script>' + in_linea(leggi_src(s.group(1), cartella)) + '</script>'
    return re.sub(r'<script\b([^>]*)>\s*</script>', sostituisci, testo, flags=re.I | re.S)


def pagina_da_js(testo, cartella):
    # le intestazioni META: script=... di un .window.js / .any.js
    extra = ''
    for m in re.finditer(r'^//\s*META:\s*script=(\S+)', testo, re.M):
        extra += '<script>' + in_linea(leggi_src(m.group(1), cartella)) + '</script>'
    return ('<!DOCTYPE html><html><head><script>' + in_linea(leggi_src('/resources/testharness.js', cartella)) +
            '</script><script>' + RAPPORTO + '</script>' + extra +
            '</head><body><div id=log></div><script>' + in_linea(testo) + '</script></body></html>')


def una_prova(percorso, prel):
    cartella = os.path.dirname(percorso)
    testo = open(percorso, encoding='utf-8', errors='replace').read()
    if percorso.endswith('.js'):
        pag = pagina_da_js(testo, cartella)
    else:
        pag = pagina_da_html(testo, cartella)
    testa = '<script>innerWidth=800;innerHeight=600;</script><script>' + in_linea(prel) + '</script>'
    # il preludio PRIMA di tutto, come nel browser
    m = re.search(r'<head[^>]*>', pag, re.I)
    if m:
        pag = pag[:m.end()] + testa + pag[m.end():]
    else:
        pag = testa + pag
    rel = os.path.relpath(percorso, WPT)
    fuori = os.path.join(LAVORO, rel.replace('/', '__') + '.html')
    os.makedirs(LAVORO, exist_ok=True)
    open(fuori, 'w', encoding='utf-8').write(pag)
    amb = dict(os.environ, DOMPROVA_CARICA='1', DOMPROVA_GIRI=os.environ.get('DOMPROVA_GIRI', '100'))
    try:
        r = subprocess.run([DOMQJS, fuori], capture_output=True, timeout=120, env=amb)
        uscita = r.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired:
        return rel, [], 'tempo scaduto (120 s)', ''
    prove = []
    fine = None
    for riga in uscita.split('\n'):
        # only the console's lines, not the page printed back by the bench
        m = re.match(r'^(?:console(?: dei tempi)?: )?WPT (.*)$', riga)
        if not m:
            continue
        riga = m.group(1)
        if riga.startswith('FINE'):
            fine = riga
            continue
        if riga.startswith('MANCA'):
            prove.append(('FAIL', riga))
            continue
        esito, _, resto = riga.partition(' ')
        prove.append((esito, resto))
    errori = [l for l in uscita.split('\n') if l.startswith('!!')]
    nota = ''
    if fine is None:
        nota = 'nessun rapporto' + (': ' + errori[0][:200] if errori else '')
    return rel, prove, nota, uscita


def raccogli(argomenti):
    file = []
    for a in argomenti:
        p = a if os.path.isabs(a) else os.path.join(WPT, a)
        if not os.path.exists(p):
            p = os.path.abspath(a)
        if os.path.isdir(p):
            for d, _, nomi in os.walk(p):
                # resources and support are not tests; tentative/ holds
                # proposals that are not the standard yet
                if '/resources' in d or d.endswith('resources') or '/support' in d or '/tentative' in d:
                    continue
                for n in sorted(nomi):
                    if (n.endswith('.html') and '-ref' not in n and 'tentative' not in n and
                            not n.endswith('-manual.html')) or n.endswith('.window.js') or n.endswith('.any.js'):
                        file.append(os.path.join(d, n))
        else:
            file.append(p)
    return sorted(file)


def main():
    verboso = '-v' in sys.argv
    argomenti = [a for a in sys.argv[1:] if a != '-v']
    if not argomenti:
        print(__doc__ if __doc__ else 'uso: wpt_host.py [-v] percorso...')
        return 0
    prel = preludio()
    tot_p = tot_n = 0
    for f in raccogli(argomenti):
        rel, prove, nota, _ = una_prova(f, prel)
        p = sum(1 for e, _ in prove if e == 'PASS')
        tot_p += p
        tot_n += len(prove)
        print('%-70s %4d/%-4d %s' % (rel[:70], p, len(prove), nota))
        if verboso:
            for e, r in prove:
                if e != 'PASS':
                    print('    %s %s' % (e, r[:200]))
    print('TOTALE %d/%d' % (tot_p, tot_n))
    return 0


if __name__ == '__main__':
    sys.exit(main())

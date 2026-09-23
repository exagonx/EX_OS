/* =============================================================================
 * exwin/bin/exbrowser/browser_preludio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * THE WINDOW'S OWN OBJECTS — what every modern script takes for granted
 *
 * ! WRITTEN ON 23 SEPTEMBER 2026, WHILE RUNNING reCAPTCHA. Its engine stopped
 * at its first line on «ReferenceError: self is not defined», and a look at
 * what the bridge defined on the global answered the rest: window, document,
 * location, navigator, console, the timers, fetch and XHR. None of self,
 * performance, requestAnimationFrame, localStorage, TextEncoder, postMessage,
 * crypto — which the web has assumed for ten years.
 *
 * ! THEY ARE JAVASCRIPT, NOT C, because they can be: each one is a few lines
 * on top of what the bridge already has (setTimeout, Date, the listeners), and
 * JavaScript is where a mistake in them shows up as a JavaScript error, with
 * a line number. One thing only must be native: random bytes, which come from
 * getentropy() and not from Math.random.
 *
 * ! WRITTEN IN ES5, because ExJs runs it too when quickjs.so is missing.
 *
 * ! A NAME THAT ALREADY EXISTS IS LEFT ALONE (def): the day exdom or the
 * engine defines one of these properly, that one wins without anybody having
 * to remember this file.
 *
 * WHAT IS NOT REAL, AND SAYS SO:
 *   - localStorage/sessionStorage live in memory, and the engine is closed at
 *     every page (motore_chiudi): what a page stores is gone at the next.
 *   - postMessage and MessageChannel pass the object itself, not a copy
 *     (no structured clone), and every origin is accepted.
 *   - matchMedia answers min-width/max-width on the window, and never fires:
 *     the window's width does not change.
 *   - getComputedStyle returns the element's inline style, not the cascade.
 *   - MutationObserver never calls back.
 *   - crypto.subtle is not there at all: better missing than wrong.
 *   - Node, Element, HTMLElement... are empty constructors whose instanceof
 *     asks nodeType: our DOM objects do not have them as prototypes.
 * ============================================================================= */

#include "browser_priv.h"

/* __exos_casuali(n): n bytes from the kernel's entropy, as an array of
 * numbers, or null if the system has not gathered enough yet. */
static ExJsVal nat_casuali(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                           int n_arg, void *dato)
{
    unsigned char b[256];
    unsigned int  quanti, fatti = 0, i;
    ExJsVal       v;

    (void)questo; (void)dato;
    if (n_arg < 1) return exjs_nullo();
    {
        double d = exjs_a_numero(c, a[0]);

        if (!(d >= 0) || d > 65536) return exjs_nullo();
        quanti = (unsigned int)d;
    }
    v = exjs_vettore(c);
    while (fatti < quanti) {
        unsigned int pezzo = quanti - fatti;

        if (pezzo > sizeof(b)) pezzo = sizeof(b);
        if (getentropy(b, pezzo) != 0) return exjs_nullo();
        for (i = 0; i < pezzo; i++)
            exjs_indice_metti(c, v, fatti + i, exjs_numero(c, (double)b[i]));
        fatti += pezzo;
    }
    return v;
}

static const char PRELUDIO[] =
"(function (G) {\n"
"  function def(n, v) { if (typeof G[n] === 'undefined') G[n] = v; }\n"
"  def('self', G); def('top', G); def('parent', G); def('frames', G);\n"
"  def('globalThis', G);\n"
"  def('devicePixelRatio', 1);\n"
"  def('scrollX', 0); def('scrollY', 0); def('pageXOffset', 0); def('pageYOffset', 0);\n"
"  def('isSecureContext', location.protocol === 'https:');\n"
"\n"
"  /* performance: a clock from the page's start, and marks that are kept */\n"
"  var t0 = Date.now(), segni = [];\n"
"  def('performance', {\n"
"    timeOrigin: t0,\n"
"    now: function () { return Date.now() - t0; },\n"
"    mark: function (n) { var e = { name: String(n), entryType: 'mark', startTime: Date.now() - t0, duration: 0 }; segni.push(e); return e; },\n"
"    measure: function (n) { var e = { name: String(n), entryType: 'measure', startTime: 0, duration: Date.now() - t0 }; segni.push(e); return e; },\n"
"    getEntriesByName: function (n) { var r = [], i; for (i = 0; i < segni.length; i++) if (segni[i].name === String(n)) r.push(segni[i]); return r; },\n"
"    getEntriesByType: function (t) { var r = [], i; for (i = 0; i < segni.length; i++) if (segni[i].entryType === String(t)) r.push(segni[i]); return r; },\n"
"    getEntries: function () { return segni.slice(0); },\n"
"    clearMarks: function () { segni = []; }, clearMeasures: function () {}\n"
"  });\n"
"  def('requestAnimationFrame', function (f) { return setTimeout(function () { f(performance.now()); }, 16); });\n"
"  def('cancelAnimationFrame', function (id) { clearTimeout(id); });\n"
"  def('requestIdleCallback', function (f) { return setTimeout(function () { f({ didTimeout: false, timeRemaining: function () { return 50; } }); }, 1); });\n"
"  def('cancelIdleCallback', function (id) { clearTimeout(id); });\n"
"  if (typeof Promise !== 'undefined') def('queueMicrotask', function (f) { Promise.resolve().then(f); });\n"
"\n"
"  /* storage, in memory (see the head of browser_preludio.c) */\n"
"  function Memoria() { this._d = {}; this.length = 0; }\n"
"  Memoria.prototype.getItem = function (k) { k = String(k); return Object.prototype.hasOwnProperty.call(this._d, k) ? this._d[k] : null; };\n"
"  Memoria.prototype.setItem = function (k, v) { this._d[String(k)] = String(v); this.length = Object.keys(this._d).length; };\n"
"  Memoria.prototype.removeItem = function (k) { delete this._d[String(k)]; this.length = Object.keys(this._d).length; };\n"
"  Memoria.prototype.clear = function () { this._d = {}; this.length = 0; };\n"
"  Memoria.prototype.key = function (i) { var k = Object.keys(this._d); return i >= 0 && i < k.length ? k[i] : null; };\n"
"  def('Storage', Memoria);\n"
"  def('localStorage', new Memoria()); def('sessionStorage', new Memoria());\n"
"\n"
"  /* UTF-8 */\n"
"  function TextEncoder() { this.encoding = 'utf-8'; }\n"
"  TextEncoder.prototype.encode = function (s) {\n"
"    var o = [], i, c, d;\n"
"    s = String(s === undefined ? '' : s);\n"
"    for (i = 0; i < s.length; i++) {\n"
"      c = s.charCodeAt(i);\n"
"      if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.length) { d = s.charCodeAt(i + 1); if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i++; } }\n"
"      if (c < 0x80) o.push(c);\n"
"      else if (c < 0x800) o.push(0xC0 | (c >> 6), 0x80 | (c & 63));\n"
"      else if (c < 0x10000) o.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));\n"
"      else o.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));\n"
"    }\n"
"    return typeof Uint8Array !== 'undefined' ? new Uint8Array(o) : o;\n"
"  };\n"
"  function TextDecoder(e) { this.encoding = 'utf-8'; }\n"
"  TextDecoder.prototype.decode = function (b) {\n"
"    var s = '', i = 0, c, n;\n"
"    if (!b) return '';\n"
"    if (b.buffer && !b.length && b.byteLength !== undefined) b = new Uint8Array(b);\n"
"    else if (typeof ArrayBuffer !== 'undefined' && b instanceof ArrayBuffer) b = new Uint8Array(b);\n"
"    while (i < b.length) {\n"
"      c = b[i++];\n"
"      if (c < 0x80) n = c;\n"
"      else if (c < 0xE0) n = ((c & 31) << 6) | (b[i++] & 63);\n"
"      else if (c < 0xF0) { n = ((c & 15) << 12) | ((b[i] & 63) << 6) | (b[i + 1] & 63); i += 2; }\n"
"      else { n = ((c & 7) << 18) | ((b[i] & 63) << 12) | ((b[i + 1] & 63) << 6) | (b[i + 2] & 63); i += 3; }\n"
"      if (n >= 0x10000) { n -= 0x10000; s += String.fromCharCode(0xD800 + (n >> 10), 0xDC00 + (n & 1023)); }\n"
"      else s += String.fromCharCode(n);\n"
"    }\n"
"    return s;\n"
"  };\n"
"  def('TextEncoder', TextEncoder); def('TextDecoder', TextDecoder);\n"
"\n"
"  /* base64 */\n"
"  var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';\n"
"  def('btoa', function (s) {\n"
"    var o = '', i, a, b, c;\n"
"    s = String(s);\n"
"    for (i = 0; i < s.length; i++) if (s.charCodeAt(i) > 255) throw new Error('InvalidCharacterError');\n"
"    for (i = 0; i < s.length; i += 3) {\n"
"      a = s.charCodeAt(i); b = s.charCodeAt(i + 1); c = s.charCodeAt(i + 2);\n"
"      o += B64.charAt(a >> 2) + B64.charAt(((a & 3) << 4) | (isNaN(b) ? 0 : b >> 4)) +\n"
"           (isNaN(b) ? '=' : B64.charAt(((b & 15) << 2) | (isNaN(c) ? 0 : c >> 6))) +\n"
"           (isNaN(c) ? '=' : B64.charAt(c & 63));\n"
"    }\n"
"    return o;\n"
"  });\n"
"  def('atob', function (s) {\n"
"    var o = '', i, bits = 0, n = 0, v;\n"
"    s = String(s).replace(/[\\t\\n\\f\\r ]/g, '').replace(/=+$/, '');\n"
"    for (i = 0; i < s.length; i++) {\n"
"      v = B64.indexOf(s.charAt(i));\n"
"      if (v < 0) throw new Error('InvalidCharacterError');\n"
"      bits = (bits << 6) | v; n += 6;\n"
"      if (n >= 8) { n -= 8; o += String.fromCharCode((bits >> n) & 255); }\n"
"    }\n"
"    return o;\n"
"  });\n"
"\n"
"  /* crypto: getRandomValues from the kernel's entropy; no subtle */\n"
"  if (typeof __exos_casuali === 'function') {\n"
"    var cas = __exos_casuali;\n"
"    def('crypto', {\n"
"      getRandomValues: function (a) {\n"
"        var per = a.BYTES_PER_ELEMENT || 1, b = cas(a.length * per), i, k, v;\n"
"        if (!b) throw new Error('crypto.getRandomValues: il sistema non ha ancora abbastanza entropia');\n"
"        for (i = 0; i < a.length; i++) { v = 0; for (k = 0; k < per; k++) v = v * 256 + b[i * per + k]; a[i] = v; }\n"
"        return a;\n"
"      },\n"
"      randomUUID: function () {\n"
"        var b = cas(16), h = '', i;\n"
"        if (!b) throw new Error('crypto.randomUUID: entropia insufficiente');\n"
"        b[6] = (b[6] & 15) | 64; b[8] = (b[8] & 63) | 128;\n"
"        for (i = 0; i < 16; i++) { h += (b[i] < 16 ? '0' : '') + b[i].toString(16); if (i === 3 || i === 5 || i === 7 || i === 9) h += '-'; }\n"
"        return h;\n"
"      }\n"
"    });\n"
"  }\n"
"\n"
"  /* events that scripts build themselves */\n"
"  function Evento(t, o) {\n"
"    o = o || {};\n"
"    this.type = String(t); this.bubbles = !!o.bubbles; this.cancelable = !!o.cancelable;\n"
"    this.defaultPrevented = false; this.timeStamp = Date.now() - t0; this.isTrusted = false;\n"
"    this.detail = o.detail === undefined ? null : o.detail;\n"
"  }\n"
"  Evento.prototype.preventDefault = function () { if (this.cancelable) this.defaultPrevented = true; };\n"
"  Evento.prototype.stopPropagation = function () {};\n"
"  Evento.prototype.stopImmediatePropagation = function () {};\n"
"  def('Event', Evento); def('CustomEvent', Evento);\n"
"\n"
"  /* the page talking to itself: postMessage, MessageChannel */\n"
"  var origine = location.origin || (location.protocol + '//' + location.host);\n"
"  var ascolta = [], add0 = G.addEventListener, rem0 = G.removeEventListener;\n"
"  function consegna(chi, ev) {\n"
"    if (typeof chi.onmessage === 'function') chi.onmessage(ev);\n"
"    for (var i = 0; i < chi._asc.length; i++) chi._asc[i].call(chi, ev);\n"
"  }\n"
"  G._asc = ascolta;\n"
"  G.addEventListener = function (t, f, o) { if (t === 'message') { if (typeof f === 'function') ascolta.push(f); return; } return add0.call(G, t, f, o); };\n"
"  G.removeEventListener = function (t, f, o) { if (t === 'message') { var i = ascolta.indexOf(f); if (i >= 0) ascolta.splice(i, 1); return; } return rem0.call(G, t, f, o); };\n"
"  G.postMessage = function (d) {\n"
"    var ev = { type: 'message', data: d, origin: origine, source: G, ports: [], lastEventId: '' };\n"
"    setTimeout(function () { consegna(G, ev); }, 0);\n"
"  };\n"
"  function Porta() { this._asc = []; this._altra = null; this.onmessage = null; }\n"
"  Porta.prototype.postMessage = function (d) {\n"
"    var p = this._altra, ev = { type: 'message', data: d, origin: '', source: null, ports: [] };\n"
"    if (p) setTimeout(function () { consegna(p, ev); }, 0);\n"
"  };\n"
"  Porta.prototype.addEventListener = function (t, f) { if (t === 'message' && typeof f === 'function') this._asc.push(f); };\n"
"  Porta.prototype.removeEventListener = function (t, f) { var i = this._asc.indexOf(f); if (i >= 0) this._asc.splice(i, 1); };\n"
"  Porta.prototype.start = function () {}; Porta.prototype.close = function () { this._altra = null; };\n"
"  function Canale() { this.port1 = new Porta(); this.port2 = new Porta(); this.port1._altra = this.port2; this.port2._altra = this.port1; }\n"
"  def('MessageChannel', Canale); def('MessagePort', Porta);\n"
"\n"
"  /* media queries, on the window's width only */\n"
"  def('matchMedia', function (q) {\n"
"    var s = String(q), m = true, r = /\\((min|max)-width:\\s*([0-9.]+)px\\)/g, x, any = false;\n"
"    while ((x = r.exec(s))) { any = true; if (x[1] === 'min' ? G.innerWidth < +x[2] : G.innerWidth > +x[2]) m = false; }\n"
"    if (!any) m = /^\\s*(all|screen)\\s*$/.test(s);\n"
"    return { matches: m, media: s, onchange: null, addListener: function () {}, removeListener: function () {},\n"
"             addEventListener: function () {}, removeEventListener: function () {} };\n"
"  });\n"
"  def('getComputedStyle', function (el) {\n"
"    var s = (el && el.style) || {};\n"
"    if (!s.getPropertyValue) s.getPropertyValue = function (n) { return s[n] || ''; };\n"
"    return s;\n"
"  });\n"
"  function Osservatore(f) { this._f = f; }\n"
"  Osservatore.prototype.observe = function () {}; Osservatore.prototype.disconnect = function () {};\n"
"  Osservatore.prototype.takeRecords = function () { return []; }; Osservatore.prototype.unobserve = function () {};\n"
"  def('MutationObserver', Osservatore);\n"
"\n"
"  /* the DOM's constructors: instanceof asks nodeType, and they can be\n"
"     extended. Image() is an <img>. customElements is left missing on\n"
"     purpose: pages that test for it know it is not there. */\n"
"  function Cost(t) { var f = function () {}; if (typeof Symbol !== 'undefined' && Symbol.hasInstance)\n"
"      try { Object.defineProperty(f, Symbol.hasInstance, { value: function (o) { return !!o && (t === 0 ? o.nodeType > 0 : o.nodeType === t); } }); } catch (e) {}\n"
"    return f; }\n"
"  def('Node', Cost(0)); def('Element', Cost(1)); def('HTMLElement', Cost(1));\n"
"  def('Text', Cost(3)); def('Document', Cost(9)); def('HTMLDocument', Cost(9));\n"
"  def('Image', function (w, h) { var i = document.createElement('img');\n"
"    if (w !== undefined) i.width = w; if (h !== undefined) i.height = h; return i; });\n"
"\n"
"  /* history: one entry, and the state a page puts in it */\n"
"  def('history', { length: 1, state: null, scrollRestoration: 'auto',\n"
"    pushState: function (s) { this.state = s; this.length++; }, replaceState: function (s) { this.state = s; },\n"
"    back: function () {}, forward: function () {}, go: function () {} });\n"
"\n"
"  try {\n"
"    if (!navigator.languages) navigator.languages = [navigator.language || 'it'];\n"
"    if (navigator.onLine === undefined) navigator.onLine = true;\n"
"    if (navigator.hardwareConcurrency === undefined) navigator.hardwareConcurrency = 1;\n"
"    if (navigator.maxTouchPoints === undefined) navigator.maxTouchPoints = 0;\n"
"    if (navigator.webdriver === undefined) navigator.webdriver = false;\n"
"  } catch (e) {}\n"
"})(this);\n";

/* Runs the prelude in a freshly opened engine. The window's inner size and
 * the screen's are real numbers, so they go in before the script. */
void preludio_esegui(ExJsCtx *js, int dentro_w, int dentro_h)
{
    char         testa[256];
    unsigned int sw = 0, sh = 0;
    ExJsErrore   err;
    ExJsVal      r;

    if (!js) return;
    exjs_metti(js, exjs_globale(js), "__exos_casuali",
               exjs_nativa(js, nat_casuali, 0, "__exos_casuali"));

    ex_schermo(&sw, &sh);
    snprintf(testa, sizeof(testa),
             "innerWidth = %d; innerHeight = %d; outerWidth = %d; outerHeight = %d;"
             "screen = { width: %u, height: %u, availWidth: %u, availHeight: %u,"
             " colorDepth: 32, pixelDepth: 32 };",
             dentro_w, dentro_h, dentro_w, dentro_h, sw, sh, sw, sh);
    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(js, testa, (unsigned int)strlen(testa), &r, &err))
        printf("exbrowser: preludio (misure): %s\n", err.messaggio);

    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(js, PRELUDIO, (unsigned int)(sizeof(PRELUDIO) - 1), &r, &err))
        printf("exbrowser: preludio, riga %d: %s\n", err.riga, err.messaggio);
}

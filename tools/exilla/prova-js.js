// =============================================================================
// tools/exilla/prova-js.js — il motore di Firefox dentro EX-OS (tappa 4)
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Si esegue con tools/exilla/prova-js.sh: la shell di SpiderMonkey costruita
// per EX-OS (tools/exilla/js-costruisci.sh) legge questo file da /disk.
// Ogni controllo stampa [OK] o [NO]; l'ultima riga dice quanti NO.
// =============================================================================
let no = 0;
function ok(cosa, va) {
  print("  " + (va ? "[OK]" : "[NO]") + "  " + cosa);
  if (!va) no++;
}
function prova(cosa, f) {
  try { ok(cosa, f()); } catch (e) { ok(cosa + " (eccezione: " + e + ")", false); }
}

print("prova-js: SpiderMonkey dentro EX-OS");

prova("classi, ereditarieta' e super", () => {
  class Animale { constructor(n) { this.n = n; } verso() { return "..."; } descrivi() { return this.n + " fa " + this.verso(); } }
  class Gatto extends Animale { verso() { return "miao"; } }
  return new Gatto("Micio").descrivi() === "Micio fa miao";
});
prova("closure e contatori", () => {
  const conta = (() => { let n = 0; return () => ++n; })();
  conta(); conta();
  return conta() === 3;
});
prova("generatori e iteratori", () => {
  function* fib() { let [a, b] = [0, 1]; for (;;) { yield a; [a, b] = [b, a + b]; } }
  const v = []; for (const x of fib()) { if (x > 100) break; v.push(x); }
  return v.join() === "0,1,1,2,3,5,8,13,21,34,55,89";
});
prova("destrutturazione, spread, parametri di default", () => {
  const { a, ...resto } = { a: 1, b: 2, c: 3 };
  const f = (x, y = 10, ...z) => x + y + z.length;
  return a === 1 && Object.keys(resto).join() === "b,c" && f(1) === 11 && f(1, 2, 3, 4) === 5;
});
prova("espressioni regolari (gruppi con nome, lookbehind, flag g)", () => {
  const m = /(?<anno>\d{4})-(?<mese>\d{2})/.exec("oggi e' il 2026-09-30");
  const tutti = "a1b22c333".match(/\d+/g);
  return m.groups.anno === "2026" && m.groups.mese === "09" &&
         tutti.join("|") === "1|22|333" && "prezzo: 42€".replace(/(?<=: )\d+/, "99") === "prezzo: 99€";
});
prova("BigInt: 2**100 e fattoriale di 25", () => {
  let f = 1n; for (let i = 1n; i <= 25n; i++) f *= i;
  return (2n ** 100n).toString() === "1267650600228229401496703205376" &&
         f === 15511210043330985984000000n;
});
prova("array tipizzati e DataView", () => {
  const b = new ArrayBuffer(16), d = new DataView(b);
  d.setFloat64(0, Math.PI); d.setUint32(8, 0xdeadbeef, true);
  const u8 = new Uint8Array(b);
  return d.getFloat64(0) === Math.PI && u8[8] === 0xef && new Float32Array([1.5, 2.5]).reduce((x, y) => x + y) === 4;
});
prova("Map, Set, WeakMap", () => {
  const m = new Map([["uno", 1], ["due", 2]]), s = new Set([1, 2, 2, 3]), w = new WeakMap(), k = {};
  w.set(k, "v");
  return m.get("due") === 2 && s.size === 3 && w.get(k) === "v" && [...m.keys()].join() === "uno,due";
});
prova("eccezioni, finally e Error.cause", () => {
  let passi = [];
  try { try { throw new TypeError("uno", { cause: 7 }); } finally { passi.push("f"); } }
  catch (e) { passi.push(e instanceof TypeError && e.cause === 7 ? "c" : "x"); }
  return passi.join("") === "fc";
});
prova("Proxy e Reflect", () => {
  const p = new Proxy({}, { get: (t, k) => k === "saluto" ? "ciao" : Reflect.get(t, k) });
  return p.saluto === "ciao" && p.altro === undefined;
});
prova("stringhe: Unicode, padStart, template, localeCompare", () => {
  const s = "caffè ☕";
  return [...s].length === 7 && "7".padStart(3, "0") === "007" &&
         `${1 + 1} due` === "2 due" && "à".normalize("NFD").length === 2;
});
prova("JSON andata e ritorno di un oggetto annidato", () => {
  const o = { n: 1.5, s: "é\n\"", a: [null, true, { x: [] }] };
  return JSON.stringify(JSON.parse(JSON.stringify(o))) === JSON.stringify(o);
});
prova("Date: calcolo di un giorno della settimana", () => {
  return new Date(Date.UTC(2026, 8, 30)).getUTCDay() === 3;    // mercoledi'
});
prova("Math e numeri in virgola mobile", () => {
  return Math.abs(Math.sin(Math.PI / 6) - 0.5) < 1e-15 && (0.1 + 0.2).toFixed(2) === "0.30" &&
         Number.parseFloat("3.25e2") === 325 && Math.hypot(3, 4) === 5;
});
prova("il garbage collector: 200000 oggetti, poi gc()", () => {
  let lista = [];
  for (let i = 0; i < 200000; i++) lista.push({ i, s: "x" + i });
  const somma = lista.reduce((a, o) => a + o.i, 0);
  lista = null;
  if (typeof gc === "function") gc();
  return somma === 19999900000;
});
prova("ordinamento di 50000 numeri", () => {
  const a = []; let x = 12345;
  for (let i = 0; i < 50000; i++) { x = (x * 1103515245 + 12345) % 2147483648; a.push(x); }
  a.sort((p, q) => p - q);
  for (let i = 1; i < a.length; i++) if (a[i - 1] > a[i]) return false;
  return true;
});

let promesse = [];
const p = Promise.all([1, Promise.resolve(2), new Promise(r => r(3))]).then(v => { promesse.push(v.join()); });
(async () => { const x = await Promise.resolve(40); promesse.push(String(x + 2)); })();
drainJobQueue();
ok("Promise e async/await (coda dei lavori svuotata)", promesse.sort().join("|") === "1,2,3|42");

print("prova-js: " + no + " NO");

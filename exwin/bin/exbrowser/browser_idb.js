/* =============================================================================
 * exwin/bin/exbrowser/browser_idb.js — IndexedDB over SQLite (P11)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Turned into a C string at build time (tools/js-in-c.py) and run after the
 * prelude (browser_idb.c). The storage is one SQLite file per origin, through
 * __exos_sql(sql, ...params) -> JSON {r: rows, n: changes} or {e: message}.
 *
 * ! THE TRANSACTIONS RUN ONE AFTER THE OTHER, each in BEGIN ... COMMIT: the
 * standard lets a browser schedule them so, and one SQLite connection has one
 * transaction. A transaction commits when it has no request left after a task
 * in which none was added - so a callback, or a promise resolved in the
 * microtasks after it, can still add one (the idb library counts on this).
 *
 * ! THE KEY IS A STRING WHOSE BYTE ORDER IS THE STANDARD'S ORDER of keys
 * (number < date < string < binary < array, arrays element by element), so
 * SQLite's BINARY collation and ORDER BY sort as IndexedDB does. Its JSON
 * (__exos_ser) is kept next to it, to give the key back.
 * ========================================================================== */
(function (G) {
  if (typeof G.__exos_sql !== 'function' || typeof G.__exos_ser !== 'function') return;
  var SQL = G.__exos_sql, ser = G.__exos_ser, deser = G.__exos_deser;
  var q = function () {
    var r = JSON.parse(SQL.apply(null, arguments));
    if (r.e) { var x = new Error(r.e); x.name = /UNIQUE|PRIMARY/.test(r.e) ? 'ConstraintError' : 'UnknownError'; throw x; }
    return r;
  };
  var errore = function (n, m) { var e = new Error(m || n); e.name = n; return e; };
  var pronto = false;
  var prepara = function () {
    if (pronto) return;
    q('CREATE TABLE IF NOT EXISTS db (nome TEXT PRIMARY KEY, versione INTEGER)');
    q('CREATE TABLE IF NOT EXISTS negozio (db TEXT, nome TEXT, kp TEXT, autoinc INTEGER, prossimo INTEGER, PRIMARY KEY (db, nome))');
    q('CREATE TABLE IF NOT EXISTS indice (db TEXT, negozio TEXT, nome TEXT, kp TEXT, unico INTEGER, multi INTEGER, PRIMARY KEY (db, negozio, nome))');
    q('CREATE TABLE IF NOT EXISTS dato (db TEXT, negozio TEXT, chiave TEXT, kjs TEXT, valore TEXT, PRIMARY KEY (db, negozio, chiave)) WITHOUT ROWID');
    q('CREATE TABLE IF NOT EXISTS voce (db TEXT, negozio TEXT, indice TEXT, chiave TEXT, kjs TEXT, primaria TEXT, PRIMARY KEY (db, negozio, indice, chiave, primaria)) WITHOUT ROWID');
    pronto = true;
  };

  /* ---------------------------------------------------------------- keys */
  var esa = function (n, w) { var s = n.toString(16); while (s.length < w) s = '0' + s; return s; };
  var numero = function (x) {
    var b = new DataView(new ArrayBuffer(8)), i, s = '', v;
    b.setFloat64(0, x === 0 ? 0 : x);
    if (x < 0 || (x === 0 && 1 / x < 0 && false)) { for (i = 0; i < 8; i++) b.setUint8(i, 255 - b.getUint8(i)); }
    else b.setUint8(0, b.getUint8(0) | 128);
    for (i = 0; i < 8; i++) { v = b.getUint8(i); s += esa(v, 2); }
    return s;
  };
  var valida = function (k, dentro) {
    if (typeof k === 'number') { if (k !== k) throw errore('DataError', 'chiave NaN'); return true; }
    if (typeof k === 'string') return true;
    if (k instanceof Date) { if (k.getTime() !== k.getTime()) throw errore('DataError', 'data non valida'); return true; }
    if (k instanceof ArrayBuffer || (k && ArrayBuffer.isView(k))) return true;
    if (Array.isArray(k)) { for (var i = 0; i < k.length; i++) valida(k[i], 1); return true; }
    throw errore('DataError', 'non e\' una chiave valida: ' + k);
  };
  var codifica = function (k) {
    var s, i, b;
    if (typeof k === 'number') return '1' + numero(k);
    if (k instanceof Date) return '2' + numero(k.getTime());
    if (typeof k === 'string') { s = '3'; for (i = 0; i < k.length; i++) s += esa(k.charCodeAt(i) + 1, 5); return s + '00000'; }
    if (k instanceof ArrayBuffer || ArrayBuffer.isView(k)) {
      b = k instanceof ArrayBuffer ? new Uint8Array(k) : new Uint8Array(k.buffer, k.byteOffset, k.byteLength);
      s = '4'; for (i = 0; i < b.length; i++) s += esa(b[i] + 1, 3); return s + '000';
    }
    if (Array.isArray(k)) { s = '5'; for (i = 0; i < k.length; i++) s += codifica(k[i]); return s + '0'; }
    throw errore('DataError', 'chiave');
  };
  var chiave = function (k) { valida(k); return codifica(k); };
  var cmp = function (a, b) { a = chiave(a); b = chiave(b); return a < b ? -1 : a > b ? 1 : 0; };

  /* the key path of a value: 'a.b', '' (the value), or an array of them */
  var dalPercorso = function (v, kp) {
    var i, p, o;
    if (Array.isArray(kp)) { o = []; for (i = 0; i < kp.length; i++) { p = dalPercorso(v, kp[i]); if (p === undefined) return undefined; o.push(p); } return o; }
    if (kp === '') return v;
    p = kp.split('.');
    for (i = 0; i < p.length; i++) {
      if (v === null || v === undefined) return undefined;
      if (typeof v === 'string' && p[i] === 'length') v = v.length; else v = v[p[i]];
    }
    return v;
  };
  var metti = function (v, kp, k) {
    var p = kp.split('.'), i;
    for (i = 0; i < p.length - 1; i++) { if (v[p[i]] === undefined) v[p[i]] = {}; v = v[p[i]]; }
    v[p[p.length - 1]] = k;
  };

  /* ---------------------------------------------------------- key ranges */
  var KR = function (l, u, lo, uo) { this.lower = l; this.upper = u; this.lowerOpen = !!lo; this.upperOpen = !!uo; };
  KR.only = function (k) { valida(k); return new KR(k, k, false, false); };
  KR.lowerBound = function (k, o) { valida(k); return new KR(k, undefined, o, true); };
  KR.upperBound = function (k, o) { valida(k); return new KR(undefined, k, true, o); };
  KR.bound = function (l, u, lo, uo) { if (cmp(l, u) > 0) throw errore('DataError', 'lower > upper'); return new KR(l, u, lo, uo); };
  KR.prototype.includes = function (k) {
    var c = chiave(k);
    if (this.lower !== undefined) { var l = codifica(this.lower); if (c < l || (this.lowerOpen && c === l)) return false; }
    if (this.upper !== undefined) { var u = codifica(this.upper); if (c > u || (this.upperOpen && c === u)) return false; }
    return true;
  };
  /* a query (a key or a range) as SQL on a column */
  var dove = function (query, col, par) {
    var r = query, w = '';
    if (r === undefined || r === null) return '';
    if (!(r instanceof KR)) r = KR.only(r);
    if (r.lower !== undefined) { w += ' AND ' + col + (r.lowerOpen ? ' > ?' : ' >= ?'); par.push(codifica(r.lower)); }
    if (r.upper !== undefined) { w += ' AND ' + col + (r.upperOpen ? ' < ?' : ' <= ?'); par.push(codifica(r.upper)); }
    return w;
  };

  /* ------------------------------------------------------------ events */
  var Bersaglio = function () { this._a = {}; };
  Bersaglio.prototype.addEventListener = function (n, f) { (this._a[n] = this._a[n] || []).push(f); };
  Bersaglio.prototype.removeEventListener = function (n, f) { var l = this._a[n] || [], i = l.indexOf(f); if (i >= 0) l.splice(i, 1); };
  Bersaglio.prototype._lancia = function (n, e) {
    var l = (this._a[n] || []).slice(0), i;
    e = e || {};
    e.type = n; e.target = e.target || this; e.currentTarget = this;
    e._annullato = false; e.preventDefault = function () { e._annullato = true; e.defaultPrevented = true; };
    e.stopPropagation = function () { e._fermo = true; };
    try { if (typeof this['on' + n] === 'function') this['on' + n](e); } catch (x) { e._eccezione = x; if (G.console) console.error(String(x)); }
    for (i = 0; i < l.length; i++) try { l[i].call(this, e); } catch (x) { e._eccezione = x; if (G.console) console.error(String(x)); }
    return e;
  };

  var Rich = function (sorg, tx) { Bersaglio.call(this); this.source = sorg; this.transaction = tx; this.readyState = 'pending'; this.result = undefined; this.error = null; this.onsuccess = this.onerror = null; };
  Rich.prototype = Object.create(Bersaglio.prototype);

  /* ------------------------------------------------------- transactions */
  var CODA = [], gira = false, ORA = null;
  var avvia = function () { if (!gira) { gira = true; setTimeout(passo, 0); } };
  var Tx = function (db, nomi, modo) {
    Bersaglio.call(this);
    this.db = db; this.mode = modo || 'readonly'; this.error = null; this._nomi = nomi;
    this.objectStoreNames = lista(nomi); this._r = []; this._fatta = false; this._partita = false; this._st = {};
    this.oncomplete = this.onerror = this.onabort = null; this.durability = 'default';
    CODA.push(this); avvia();
  };
  Tx.prototype = Object.create(Bersaglio.prototype);
  Tx.prototype.objectStore = function (n) {
    if (this._nomi.indexOf(n) < 0) throw errore('NotFoundError', 'negozio fuori dalla transazione: ' + n);
    if (this._fatta) throw errore('InvalidStateError', 'transazione finita');
    return this._st[n] || (this._st[n] = new Negozio(this, n));
  };
  Tx.prototype.abort = function () {
    if (this._fatta) throw errore('InvalidStateError', 'transazione finita');
    this._annulla(errore('AbortError', 'abort()'));
  };
  Tx.prototype.commit = function () { this._commit = true; };
  Tx.prototype._annulla = function (err) {
    var i, r, t = this;
    if (this._fatta) return;
    this._fatta = true; this.error = err && err.name === 'AbortError' ? null : err;
    if (this._partita) try { q('ROLLBACK'); } catch (e) {}
    if (this._versione) this.db._ricarica();
    for (i = 0; i < this._r.length; i++) { r = this._r[i]; r.readyState = 'done'; r.error = errore('AbortError'); r._lancia('error', {}); }
    this._r = [];
    if (ORA === this) ORA = null;
    setTimeout(function () { t._lancia('abort', {}); if (t._dopo) t._dopo(false); avvia(); }, 0);
  };
  var richiedi = function (tx, sorg, f) {
    if (tx._fatta) throw errore('TransactionInactiveError', 'transazione finita');
    var r = new Rich(sorg, tx);
    r._f = f; tx._r.push(r); avvia();
    return r;
  };
  /* one step of the scheduler: one request, or the end of a transaction */
  var passo = function () {
    var tx, r, v;
    gira = false;
    while (CODA.length && CODA[0]._fatta) CODA.shift();
    tx = CODA[0];
    if (!tx) return;
    ORA = tx;
    if (!tx._partita) { prepara(); q('BEGIN'); tx._partita = true; }
    if (tx._r.length) {
      r = tx._r.shift();
      tx._quasi = false;
      /* ! EACH REQUEST IN A SAVEPOINT: one that fails halfway (a unique
       * index after the row was written) leaves nothing behind, even when
       * the page prevents the error and the transaction goes on */
      q('SAVEPOINT richiesta');
      try { v = r._f(); q('RELEASE richiesta'); r.result = v; r.readyState = 'done'; r._lancia('success', {}); }
      catch (e) {
        try { q('ROLLBACK TO richiesta'); q('RELEASE richiesta'); } catch (x) {}
        r.readyState = 'done'; r.error = e; r.result = undefined;
        var ev = r._lancia('error', {});
        if (!ev._annullato) { var ev2 = tx._lancia('error', { target: r }); if (!ev2._annullato) tx._annulla(e); }
      }
      if (!tx._fatta) avvia();
      return;
    }
    /* nothing left: in the next task, if still nothing, it commits */
    if (!tx._quasi) { tx._quasi = true; avvia(); return; }
    tx._fatta = true;
    try { q('COMMIT'); } catch (e) { tx._fatta = false; tx._annulla(e); return; }
    CODA.shift(); ORA = null;
    tx._lancia('complete', {});
    if (tx._dopo) tx._dopo(true);
    avvia();
  };
  var lista = function (a) {
    var l = a.slice(0).sort();
    l.contains = function (n) { return l.indexOf(n) >= 0; };
    l.item = function (i) { return l[i] === undefined ? null : l[i]; };
    return l;
  };

  /* -------------------------------------------------------- the database */
  var Db = function (nome, ver) { Bersaglio.call(this); this.name = nome; this.version = ver; this.onversionchange = this.onclose = this.onabort = this.onerror = null; this._ricarica(); };
  Db.prototype = Object.create(Bersaglio.prototype);
  Db.prototype._ricarica = function () {
    var r = q('SELECT nome, kp, autoinc FROM negozio WHERE db = ?', this.name).r, i;
    this._neg = {};
    for (i = 0; i < r.length; i++) this._neg[r[i][0]] = { kp: JSON.parse(r[i][1]), auto: !!r[i][2] };
    this.objectStoreNames = lista(Object.keys(this._neg));
  };
  Db.prototype.transaction = function (nomi, modo) {
    if (this._chiuso) throw errore('InvalidStateError', 'database chiuso');
    if (typeof nomi === 'string') nomi = [nomi];
    nomi = Array.prototype.slice.call(nomi);
    if (!nomi.length) throw errore('InvalidAccessError', 'nessun negozio');
    for (var i = 0; i < nomi.length; i++) if (!this._neg[nomi[i]]) throw errore('NotFoundError', 'negozio sconosciuto: ' + nomi[i]);
    if (modo && modo !== 'readonly' && modo !== 'readwrite') throw new TypeError('modo: ' + modo);
    return new Tx(this, nomi, modo);
  };
  Db.prototype.close = function () { this._chiuso = true; };
  Db.prototype.createObjectStore = function (n, o) {
    var tx = this._tx;
    if (!tx || tx._fatta) throw errore('InvalidStateError', 'solo in upgradeneeded');
    if (this._neg[n]) throw errore('ConstraintError', 'il negozio esiste: ' + n);
    var kp = o && o.keyPath !== undefined ? o.keyPath : null, auto = !!(o && o.autoIncrement);
    if (auto && (kp === '' || Array.isArray(kp))) throw errore('InvalidAccessError', 'autoIncrement con questo keyPath');
    q('INSERT INTO negozio VALUES (?, ?, ?, ?, 1)', this.name, n, JSON.stringify(kp), auto ? 1 : 0);
    this._neg[n] = { kp: kp, auto: auto };
    this.objectStoreNames = lista(Object.keys(this._neg));
    if (tx._nomi.indexOf(n) < 0) tx._nomi.push(n);
    tx.objectStoreNames = lista(tx._nomi);
    return tx.objectStore(n);
  };
  Db.prototype.deleteObjectStore = function (n) {
    if (!this._tx || this._tx._fatta) throw errore('InvalidStateError', 'solo in upgradeneeded');
    if (!this._neg[n]) throw errore('NotFoundError', n);
    ['negozio', 'indice', 'dato', 'voce'].forEach(function (t) { q('DELETE FROM ' + t + ' WHERE db = ? AND ' + (t === 'negozio' ? 'nome' : 'negozio') + ' = ?', this.name, n); }, this);
    delete this._neg[n];
    this.objectStoreNames = lista(Object.keys(this._neg));
  };

  /* --------------------------------------------------------- the stores */
  var Negozio = function (tx, n) {
    var d = tx.db._neg[n];
    this.transaction = tx; this.name = n; this.keyPath = d.kp; this.autoIncrement = d.auto; this._db = tx.db.name;
    this._indici();
  };
  Negozio.prototype._indici = function () {
    var r = q('SELECT nome, kp, unico, multi FROM indice WHERE db = ? AND negozio = ?', this._db, this.name).r, i;
    this._ix = {};
    for (i = 0; i < r.length; i++) this._ix[r[i][0]] = { kp: JSON.parse(r[i][1]), unico: !!r[i][2], multi: !!r[i][3] };
    this.indexNames = lista(Object.keys(this._ix));
  };
  Negozio.prototype._scrivibile = function () { if (this.transaction.mode === 'readonly') throw errore('ReadOnlyError', 'transazione in sola lettura'); };
  Negozio.prototype._voci = function (k, ck, v) {
    var n, d, ik, ks, i, t = this;
    for (n in this._ix) {
      d = this._ix[n];
      ik = dalPercorso(v, d.kp);
      if (ik === undefined) continue;
      ks = d.multi && Array.isArray(ik) ? ik : [ik];
      for (i = 0; i < ks.length; i++) {
        try { valida(ks[i]); } catch (e) { continue; }
        var c = codifica(ks[i]);
        if (d.unico && q('SELECT 1 FROM voce WHERE db = ? AND negozio = ? AND indice = ? AND chiave = ? AND primaria <> ?', t._db, t.name, n, c, ck).r.length)
          throw errore('ConstraintError', 'indice unico ' + n);
        q('INSERT OR IGNORE INTO voce VALUES (?, ?, ?, ?, ?, ?)', t._db, t.name, n, c, ser(ks[i]), ck);
      }
    }
  };
  Negozio.prototype._metti = function (v, k, sovrascrivi) {
    var kp = this.keyPath, ck, val;
    this._scrivibile();
    val = deser(ser(v));                  /* the clone, as stored */
    if (kp !== null) {
      if (k !== undefined) throw errore('DataError', 'chiave data con keyPath');
      k = dalPercorso(val, kp);
      if (k === undefined) {
        if (!this.autoIncrement) throw errore('DataError', 'il valore non ha la chiave ' + kp);
      }
    } else if (k === undefined && !this.autoIncrement) throw errore('DataError', 'manca la chiave');
    if (k !== undefined) valida(k);
    return { v: val, k: k, sovr: sovrascrivi };
  };
  Negozio.prototype._esegui = function (p) {
    var k = p.k, ck, r, prossimo;
    if (k === undefined) {
      r = q('SELECT prossimo FROM negozio WHERE db = ? AND nome = ?', this._db, this.name).r;
      k = r[0][0];
      if (this.keyPath !== null) metti(p.v, this.keyPath, k);
    }
    valida(k);
    if (this.autoIncrement && typeof k === 'number' && k >= 1) {
      prossimo = Math.floor(k) + 1;
      q('UPDATE negozio SET prossimo = MAX(prossimo, ?) WHERE db = ? AND nome = ?', prossimo, this._db, this.name);
    }
    ck = codifica(k);
    if (!p.sovr && q('SELECT 1 FROM dato WHERE db = ? AND negozio = ? AND chiave = ?', this._db, this.name, ck).r.length)
      throw errore('ConstraintError', 'la chiave esiste');
    q('DELETE FROM voce WHERE db = ? AND negozio = ? AND primaria = ?', this._db, this.name, ck);
    q('INSERT OR REPLACE INTO dato VALUES (?, ?, ?, ?, ?)', this._db, this.name, ck, ser(k), ser(p.v));
    this._voci(k, ck, p.v);
    return k;
  };
  Negozio.prototype.put = function (v, k) { var t = this, p = this._metti(v, k, true); return richiedi(this.transaction, this, function () { return t._esegui(p); }); };
  Negozio.prototype.add = function (v, k) { var t = this, p = this._metti(v, k, false); return richiedi(this.transaction, this, function () { return t._esegui(p); }); };
  Negozio.prototype._sel = function (cosa, query, n, dir) {
    var par = [this._db, this.name], w = dove(query, 'chiave', par);
    return q.apply(null, ['SELECT ' + cosa + ' FROM dato WHERE db = ? AND negozio = ?' + w + ' ORDER BY chiave' + (dir ? ' DESC' : '') + (n ? ' LIMIT ' + (n | 0) : '')].concat(par)).r;
  };
  Negozio.prototype.get = function (query) {
    var t = this; if (query === undefined) throw errore('DataError', 'get senza chiave');
    return richiedi(this.transaction, this, function () { var r = t._sel('valore', query, 1); return r.length ? deser(r[0][0]) : undefined; });
  };
  Negozio.prototype.getKey = function (query) { var t = this; return richiedi(this.transaction, this, function () { var r = t._sel('kjs', query, 1); return r.length ? deser(r[0][0]) : undefined; }); };
  Negozio.prototype.getAll = function (query, n) { var t = this; return richiedi(this.transaction, this, function () { return t._sel('valore', query, n).map(function (x) { return deser(x[0]); }); }); };
  Negozio.prototype.getAllKeys = function (query, n) { var t = this; return richiedi(this.transaction, this, function () { return t._sel('kjs', query, n).map(function (x) { return deser(x[0]); }); }); };
  Negozio.prototype.count = function (query) { var t = this; return richiedi(this.transaction, this, function () { return t._sel('COUNT(*)', query)[0][0]; }); };
  Negozio.prototype['delete'] = function (query) {
    var t = this; this._scrivibile();
    return richiedi(this.transaction, this, function () {
      var par = [t._db, t.name], w = dove(query, 'chiave', par), par2 = [t._db, t.name], w2 = dove(query, 'primaria', par2);
      q.apply(null, ['DELETE FROM voce WHERE db = ? AND negozio = ?' + w2].concat(par2));
      q.apply(null, ['DELETE FROM dato WHERE db = ? AND negozio = ?' + w].concat(par));
      return undefined;
    });
  };
  Negozio.prototype.clear = function () {
    var t = this; this._scrivibile();
    return richiedi(this.transaction, this, function () { q('DELETE FROM dato WHERE db = ? AND negozio = ?', t._db, t.name); q('DELETE FROM voce WHERE db = ? AND negozio = ?', t._db, t.name); return undefined; });
  };
  Negozio.prototype.createIndex = function (n, kp, o) {
    var tx = this.transaction, r, i, v, k;
    if (!tx._versione) throw errore('InvalidStateError', 'solo in upgradeneeded');
    if (this._ix[n]) throw errore('ConstraintError', 'l\'indice esiste: ' + n);
    q('INSERT INTO indice VALUES (?, ?, ?, ?, ?, ?)', this._db, this.name, n, JSON.stringify(kp), o && o.unique ? 1 : 0, o && o.multiEntry ? 1 : 0);
    this._indici();
    /* the values already there */
    r = q('SELECT chiave, kjs, valore FROM dato WHERE db = ? AND negozio = ?', this._db, this.name).r;
    var solo = {}; solo[n] = this._ix[n]; var tutti = this._ix; this._ix = solo;
    try { for (i = 0; i < r.length; i++) this._voci(deser(r[i][1]), r[i][0], deser(r[i][2])); }
    catch (e) { this._ix = tutti; tx._annulla(e); throw e; }
    this._ix = tutti;
    return this.index(n);
  };
  Negozio.prototype.deleteIndex = function (n) {
    if (!this.transaction._versione) throw errore('InvalidStateError', 'solo in upgradeneeded');
    q('DELETE FROM indice WHERE db = ? AND negozio = ? AND nome = ?', this._db, this.name, n);
    q('DELETE FROM voce WHERE db = ? AND negozio = ? AND indice = ?', this._db, this.name, n);
    this._indici();
  };
  Negozio.prototype.index = function (n) { if (!this._ix[n]) throw errore('NotFoundError', 'indice sconosciuto: ' + n); return new Indice(this, n); };
  Negozio.prototype.openCursor = function (query, dir) { return cursore(this, null, query, dir, true); };
  Negozio.prototype.openKeyCursor = function (query, dir) { return cursore(this, null, query, dir, false); };

  /* ------------------------------------------------------------ indexes */
  var Indice = function (st, n) { var d = st._ix[n]; this.objectStore = st; this.name = n; this.keyPath = d.kp; this.unique = d.unico; this.multiEntry = d.multi; };
  Indice.prototype._sel = function (cosa, query, n) {
    var s = this.objectStore, par = [s._db, s.name, this.name], w = dove(query, 'v.chiave', par);
    return q.apply(null, ['SELECT ' + cosa + ' FROM voce v JOIN dato d ON d.db = v.db AND d.negozio = v.negozio AND d.chiave = v.primaria WHERE v.db = ? AND v.negozio = ? AND v.indice = ?' + w + ' ORDER BY v.chiave, v.primaria' + (n ? ' LIMIT ' + (n | 0) : '')].concat(par)).r;
  };
  Indice.prototype.get = function (query) { var t = this; return richiedi(this.objectStore.transaction, this, function () { var r = t._sel('d.valore', query, 1); return r.length ? deser(r[0][0]) : undefined; }); };
  Indice.prototype.getKey = function (query) { var t = this; return richiedi(this.objectStore.transaction, this, function () { var r = t._sel('d.kjs', query, 1); return r.length ? deser(r[0][0]) : undefined; }); };
  Indice.prototype.getAll = function (query, n) { var t = this; return richiedi(this.objectStore.transaction, this, function () { return t._sel('d.valore', query, n).map(function (x) { return deser(x[0]); }); }); };
  Indice.prototype.getAllKeys = function (query, n) { var t = this; return richiedi(this.objectStore.transaction, this, function () { return t._sel('d.kjs', query, n).map(function (x) { return deser(x[0]); }); }); };
  Indice.prototype.count = function (query) { var t = this; return richiedi(this.objectStore.transaction, this, function () { return t._sel('COUNT(*)', query)[0][0]; }); };
  Indice.prototype.openCursor = function (query, dir) { return cursore(this.objectStore, this, query, dir, true); };
  Indice.prototype.openKeyCursor = function (query, dir) { return cursore(this.objectStore, this, query, dir, false); };

  /* ------------------------------------------------------------ cursors */
  var Cursore = function () {};
  var cursore = function (st, ix, query, dir, valori) {
    dir = dir || 'next';
    if (['next', 'nextunique', 'prev', 'prevunique'].indexOf(dir) < 0) throw new TypeError('direzione: ' + dir);
    var c = new Cursore(), giu = dir.indexOf('prev') === 0, unico = /unique/.test(dir), tx = st.transaction, r;
    c.source = ix || st; c.direction = dir; c.key = undefined; c.primaryKey = undefined; c._ck = null; c._cp = null;
    if (valori) c.value = undefined;
    c._muovi = function (verso, minima) {
      var par = [st._db, st.name], w = '', ord = giu ? ' DESC' : '', righe;
      if (ix) {
        par.push(ix.name);
        w = dove(query, 'v.chiave', par);
        if (c._ck !== null) {
          if (unico) { w += giu ? ' AND v.chiave < ?' : ' AND v.chiave > ?'; par.push(c._ck); }
          else { w += giu ? ' AND (v.chiave < ? OR (v.chiave = ? AND v.primaria < ?))' : ' AND (v.chiave > ? OR (v.chiave = ? AND v.primaria > ?))'; par.push(c._ck, c._ck, c._cp); }
        }
        if (minima !== undefined) { w += giu ? ' AND v.chiave <= ?' : ' AND v.chiave >= ?'; par.push(minima); }
        righe = q.apply(null, ['SELECT v.chiave, v.kjs, v.primaria, d.kjs, d.valore FROM voce v JOIN dato d ON d.db = v.db AND d.negozio = v.negozio AND d.chiave = v.primaria WHERE v.db = ? AND v.negozio = ? AND v.indice = ?' + w + ' ORDER BY v.chiave' + ord + ', v.primaria' + (giu && unico ? '' : ord) + ' LIMIT 1'].concat(par)).r;
      } else {
        w = dove(query, 'chiave', par);
        if (c._ck !== null) { w += giu ? ' AND chiave < ?' : ' AND chiave > ?'; par.push(c._ck); }
        if (minima !== undefined) { w += giu ? ' AND chiave <= ?' : ' AND chiave >= ?'; par.push(minima); }
        righe = q.apply(null, ['SELECT chiave, kjs, chiave, kjs, valore FROM dato WHERE db = ? AND negozio = ?' + w + ' ORDER BY chiave' + ord + ' LIMIT 1'].concat(par)).r;
      }
      if (!righe.length) { c.key = c.primaryKey = undefined; if (valori) c.value = undefined; return null; }
      c._ck = righe[0][0]; c._cp = righe[0][2];
      c.key = deser(righe[0][1]); c.primaryKey = deser(righe[0][3]);
      if (valori) c.value = deser(righe[0][4]);
      return c;
    };
    r = richiedi(tx, c.source, function () { return c._muovi(); });
    c.request = r;
    c._ancora = function (f) { r.readyState = 'pending'; tx._r.push(r); r._f = f; avvia(); };
    return r;
  };
  Cursore.prototype['continue'] = function (k) {
    var c = this, m;
    if (k !== undefined) { m = chiave(k); }
    this._ancora(function () { return c._muovi(1, m); });
  };
  Cursore.prototype.continuePrimaryKey = function (k, p) { this['continue'](k); };
  Cursore.prototype.advance = function (n) {
    var c = this;
    if (!(n > 0)) throw new TypeError('advance(' + n + ')');
    this._ancora(function () { var x = c; for (var i = 0; i < n && x; i++) x = c._muovi(); return x; });
  };
  Cursore.prototype.update = function (v) {
    var st = this.source.objectStore || this.source, c = this;
    st._scrivibile();
    if (st.keyPath !== null && cmp(dalPercorso(v, st.keyPath), this.primaryKey) !== 0) throw errore('DataError', 'update cambia la chiave');
    var p = { v: deser(ser(v)), k: st.keyPath === null ? this.primaryKey : undefined, sovr: true };
    if (st.keyPath !== null) p.k = this.primaryKey;
    return richiedi(st.transaction, this, function () { return st._esegui(p); });
  };
  Cursore.prototype['delete'] = function () { var st = this.source.objectStore || this.source; return st['delete'](this.primaryKey); };

  /* ------------------------------------------------------- the factory */
  var Apri = function () { Rich.call(this, null, null); this.onupgradeneeded = this.onblocked = null; };
  Apri.prototype = Object.create(Rich.prototype);
  var fabbrica = {
    open: function (nome, ver) {
      var r = new Apri();
      nome = String(nome);
      if (ver !== undefined && !(ver >= 1)) throw new TypeError('versione: ' + ver);
      setTimeout(function () {
        var db, vecchia, tx;
        try {
          prepara();
          var x = q('SELECT versione FROM db WHERE nome = ?', nome).r;
          vecchia = x.length ? x[0][0] : 0;
          if (ver === undefined) ver = vecchia || 1;
          if (ver < vecchia) { r.readyState = 'done'; r.error = errore('VersionError', 'versione ' + ver + ' < ' + vecchia); r._lancia('error', {}); return; }
          if (ver > vecchia) {
            /* the version change: a readwrite transaction on every store */
            db = new Db(nome, ver);
            tx = new Tx(db, Object.keys(db._neg), 'versionchange');
            tx._versione = true; db._tx = tx;
            r.result = db; r.transaction = tx; r.readyState = 'done';
            tx._dopo = function (ok) {
              db._tx = null; r.transaction = null;
              if (ok) { db._ricarica(); r._lancia('success', {}); }
              else { db.version = vecchia; r.result = undefined; r.error = errore('AbortError', 'upgrade annullato'); r._lancia('error', {}); }
            };
            /* the upgradeneeded handler runs inside the transaction, once BEGIN is done */
            tx._r.push({ _f: function () {
              q('INSERT OR REPLACE INTO db VALUES (?, ?)', nome, ver);     /* inside: an abort undoes it */
              r._lancia('upgradeneeded', { oldVersion: vecchia, newVersion: ver });
            }, _lancia: function () { return {}; } });
            avvia();
            return;
          }
          db = new Db(nome, ver);
          r.result = db; r.readyState = 'done'; r._lancia('success', {});
        } catch (e) { r.readyState = 'done'; r.error = e; r._lancia('error', {}); }
      }, 0);
      return r;
    },
    deleteDatabase: function (nome) {
      var r = new Apri();
      setTimeout(function () {
        try {
          prepara();
          var x = q('SELECT versione FROM db WHERE nome = ?', String(nome)).r;
          ['db', 'negozio', 'indice', 'dato', 'voce'].forEach(function (t) { q('DELETE FROM ' + t + ' WHERE ' + (t === 'db' ? 'nome' : 'db') + ' = ?', String(nome)); });
          r.readyState = 'done'; r.result = undefined; r._lancia('success', { oldVersion: x.length ? x[0][0] : 0, newVersion: null });
        } catch (e) { r.readyState = 'done'; r.error = e; r._lancia('error', {}); }
      }, 0);
      return r;
    },
    databases: function () {
      return new Promise(function (ok) { prepara(); ok(q('SELECT nome, versione FROM db').r.map(function (x) { return { name: x[0], version: x[1] }; })); });
    },
    cmp: cmp
  };
  G.indexedDB = fabbrica;
  G.IDBKeyRange = KR; G.IDBFactory = function () {}; G.IDBDatabase = Db; G.IDBTransaction = Tx;
  G.IDBObjectStore = Negozio; G.IDBIndex = Indice; G.IDBCursor = Cursore; G.IDBCursorWithValue = Cursore;
  G.IDBRequest = Rich; G.IDBOpenDBRequest = Apri;
  G.IDBVersionChangeEvent = function (t, o) { this.type = t; this.oldVersion = o && o.oldVersion; this.newVersion = o && o.newVersion; };
})(this);

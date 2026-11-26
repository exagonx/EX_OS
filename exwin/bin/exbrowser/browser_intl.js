/* =============================================================================
 * exwin/bin/exbrowser/browser_intl.js — the Intl objects (P11)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ECMA-402 over ICU4C through __exos_icu (browser_intl.c). The options become
 * ICU number skeletons and date skeletons here; ICU does the locale work.
 * Turned into a C string at build time (tools/js-in-c.py), run after the
 * prelude. Without ICU (no libicuuc.so.79) there is no Intl, as before P11.
 *
 * ! formatToParts IS RECONSTRUCTED from the formatted string and the
 * pattern (dates) or the symbols (numbers): ICU's field iterators are not
 * used yet. The types are right for the common cases (digits, separators,
 * signs, currency, the fields of a date).
 * ========================================================================== */
(function (G) {
  if (typeof G.__exos_icu !== 'function') return;
  var I = G.__exos_icu;
  if (I('loc', 'en') === '') return;
  var has = function (o, k) { return o !== undefined && o !== null && Object.prototype.hasOwnProperty.call(o, k) && o[k] !== undefined; };
  var opz = function (o, k, ammessi, pred) {
    if (o === undefined || o === null || o[k] === undefined) return pred;
    var v = String(o[k]);
    if (ammessi && ammessi.indexOf(v) < 0) throw new RangeError('Value ' + v + ' out of range for ' + k);
    return v;
  };
  var num = function (o, k, min, max, pred) {
    if (o === undefined || o === null || o[k] === undefined) return pred;
    var v = Number(o[k]);
    if (!(v >= min && v <= max)) throw new RangeError(k + ' value is out of range.');
    return Math.floor(v);
  };
  var BASE = (G.navigator && G.navigator.language) || 'it-IT';
  var canon = function (l) {
    if (typeof l !== 'string' && !(l instanceof Loc)) throw new TypeError('Language ID should be string or object.');
    var t = I('loc', String(l));
    if (!t) throw new RangeError('Incorrect locale information provided');
    return t;
  };
  var lista = function (l) {
    var a = [], o = [], i, c;
    if (l === undefined) return o;
    a = typeof l === 'string' || l instanceof Loc ? [l] : Array.prototype.slice.call(Object(l));
    for (i = 0; i < a.length; i++) { c = canon(a[i]); if (o.indexOf(c) < 0) o.push(c); }
    return o;
  };
  var scegli = function (l) { var a = lista(l); return a.length ? a[0] : I('loc', BASE) || 'it-IT'; };
  var supportati = function (l) { return lista(l); };
  var Intl = {};
  Intl.getCanonicalLocales = function (l) { return lista(l); };
  Intl.supportedValuesOf = function (k) {
    var V = { calendar: ['gregory'], collation: ['default'], currency: ['CHF', 'EUR', 'GBP', 'JPY', 'USD'], numberingSystem: ['latn'],
              timeZone: ['Europe/Rome', 'UTC'], unit: ['byte', 'day', 'hour', 'kilobyte', 'kilometer', 'meter', 'minute', 'percent', 'second'] };
    if (!V[k]) throw new RangeError('Invalid key : ' + k);
    return V[k].slice(0);
  };
  var legato = function (P, n, f) {
    Object.defineProperty(P, n, { configurable: true, get: function () { var t = this, b = function () { return f.apply(t, arguments); }; Object.defineProperty(this, n, { value: b, configurable: true }); return b; } });
  };

  /* -------------------------------------------------------------- Locale */
  var Loc = function (tag, o) {
    if (!(this instanceof Loc)) throw new TypeError('Constructor Intl.Locale requires \'new\'');
    var t = canon(tag instanceof Loc ? tag.toString() : tag), p, u;
    if (o) {
      p = t.split('-u-');
      u = p[1] ? '-u-' + p[1] : '';
      p = p[0].split('-');
      if (o.language) p[0] = o.language;
      if (o.script) { if (/^[A-Z][a-z]{3}$/.test(p[1])) p[1] = o.script; else p.splice(1, 0, o.script); }
      if (o.region) { var i = /^[A-Z][a-z]{3}$/.test(p[1]) ? 2 : 1; if (/^([A-Z]{2}|\d{3})$/.test(p[i])) p[i] = o.region; else p.splice(i, 0, o.region); }
      var kw = { calendar: 'ca', collation: 'co', hourCycle: 'hc', caseFirst: 'kf', numeric: 'kn', numberingSystem: 'nu' }, k;
      for (k in kw) if (o[k] !== undefined) u += (u ? '' : '-u') + '-' + kw[k] + '-' + (o[k] === true ? 'true' : String(o[k]));
      t = canon(p.join('-') + u);
    }
    this._t = t;
    var b = t.split('-u-')[0].split('-'), uu = (t.split('-u-')[1] || '').split('-'), j;
    this.language = b[0]; this.baseName = t.split('-u-')[0];
    this.script = /^[A-Z][a-z]{3}$/.test(b[1]) ? b[1] : undefined;
    this.region = b.filter(function (x, n) { return n > 0 && /^([A-Z]{2}|\d{3})$/.test(x); })[0];
    var kn = { ca: 'calendar', co: 'collation', hc: 'hourCycle', kf: 'caseFirst', kn: 'numeric', nu: 'numberingSystem' };
    for (j = 0; j < uu.length; j += 2) if (kn[uu[j]]) this[kn[uu[j]]] = uu[j] === 'kn' ? uu[j + 1] !== 'false' : uu[j + 1];
  };
  Loc.prototype.toString = function () { return this._t; };
  Loc.prototype.maximize = function () { return new Loc(I('max', this._t) || this._t); };
  Loc.prototype.minimize = function () { return new Loc(I('min', this._t) || this._t); };
  Loc.prototype.getWeekInfo = function () { return { firstDay: this.region === 'US' ? 7 : 1, weekend: [6, 7], minimalDays: this.region === 'US' ? 1 : 4 }; };
  Loc.prototype.getTimeZones = function () { return this.region === 'IT' ? ['Europe/Rome'] : undefined; };
  Intl.Locale = Loc;

  /* -------------------------------------------------------- NumberFormat */
  var DECIMALI_VALUTA = { JPY: 0, KRW: 0, VND: 0, CLP: 0, ISK: 0, HUF: 2, BHD: 3, KWD: 3, JOD: 3, TND: 3 };
  var NF = function (loc, o) {
    if (!(this instanceof NF)) return new NF(loc, o);
    o = o === undefined ? {} : Object(o);
    var r = this._r = { locale: scegli(loc), numberingSystem: 'latn' }, s = [], st, mn, mx, min, mxf, minf, tipo;
    st = r.style = opz(o, 'style', ['decimal', 'percent', 'currency', 'unit'], 'decimal');
    if (st === 'currency') {
      if (!has(o, 'currency')) throw new TypeError('Currency code is required with currency style.');
      r.currency = String(o.currency).toUpperCase();
      if (!/^[A-Z]{3}$/.test(r.currency)) throw new RangeError('Invalid currency code : ' + o.currency);
      r.currencyDisplay = opz(o, 'currencyDisplay', ['code', 'symbol', 'narrowSymbol', 'name'], 'symbol');
      r.currencySign = opz(o, 'currencySign', ['standard', 'accounting'], 'standard');
      s.push('currency/' + r.currency);
      if (r.currencyDisplay === 'code') s.push('unit-width-iso-code');
      else if (r.currencyDisplay === 'name') s.push('unit-width-full-name');
      else if (r.currencyDisplay === 'narrowSymbol') s.push('unit-width-narrow');
    } else if (st === 'percent') s.push('percent', 'scale/100');
    else if (st === 'unit') {
      if (!has(o, 'unit')) throw new TypeError('Unit is required with unit style.');
      r.unit = String(o.unit); r.unitDisplay = opz(o, 'unitDisplay', ['short', 'narrow', 'long'], 'short');
      s.push('unit/' + r.unit);
      if (r.unitDisplay === 'long') s.push('unit-width-full-name'); else if (r.unitDisplay === 'narrow') s.push('unit-width-narrow');
    }
    r.notation = opz(o, 'notation', ['standard', 'scientific', 'engineering', 'compact'], 'standard');
    if (r.notation === 'compact') { r.compactDisplay = opz(o, 'compactDisplay', ['short', 'long'], 'short'); s.push('compact-' + r.compactDisplay); }
    else if (r.notation !== 'standard') s.push(r.notation);
    r.minimumIntegerDigits = num(o, 'minimumIntegerDigits', 1, 21, 1);
    if (r.minimumIntegerDigits > 1) s.push('integer-width/*' + new Array(r.minimumIntegerDigits + 1).join('0'));
    tipo = st === 'currency' ? (DECIMALI_VALUTA[r.currency] === undefined ? 2 : DECIMALI_VALUTA[r.currency]) : 0;
    if (has(o, 'minimumSignificantDigits') || has(o, 'maximumSignificantDigits')) {
      mn = num(o, 'minimumSignificantDigits', 1, 21, 1); mx = num(o, 'maximumSignificantDigits', mn, 21, 21);
      r.minimumSignificantDigits = mn; r.maximumSignificantDigits = mx;
      s.push(new Array(mn + 1).join('@') + new Array(mx - mn + 1).join('#'));
    } else {
      minf = num(o, 'minimumFractionDigits', 0, 100, undefined); mxf = num(o, 'maximumFractionDigits', 0, 100, undefined);
      min = minf === undefined ? tipo : minf;
      mx = mxf === undefined ? Math.max(min, st === 'currency' ? tipo : st === 'percent' ? 0 : (r.notation === 'compact' ? 0 : 3)) : mxf;
      if (minf === undefined && min > mx) min = mx;
      if (min > mx) throw new RangeError('maximumFractionDigits value is out of range.');
      r.minimumFractionDigits = min; r.maximumFractionDigits = mx;
      if (r.notation === 'compact' && minf === undefined && mxf === undefined) { /* ICU's own compact rounding */ }
      else s.push(mx === 0 ? 'precision-integer' : '.' + new Array(min + 1).join('0') + new Array(mx - min + 1).join('#'));
    }
    var g = o.useGrouping;
    r.useGrouping = g === undefined ? (r.notation === 'compact' ? 'min2' : 'auto') : g === false || g === 'false' ? false : g === true ? 'always' : String(g);
    if (r.useGrouping === false) s.push('group-off'); else if (r.useGrouping === 'min2') s.push('group-min2'); else if (r.useGrouping === 'always') s.push('group-on-aligned');
    r.signDisplay = opz(o, 'signDisplay', ['auto', 'never', 'always', 'exceptZero', 'negative'], 'auto');
    var acc = r.currencySign === 'accounting';
    var SEGNI = { auto: acc ? 'sign-accounting' : '', never: 'sign-never', always: acc ? 'sign-accounting-always' : 'sign-always',
                  exceptZero: acc ? 'sign-accounting-except-zero' : 'sign-except-zero', negative: acc ? 'sign-accounting-negative' : 'sign-negative' };
    if (SEGNI[r.signDisplay]) s.push(SEGNI[r.signDisplay]);
    r.roundingMode = opz(o, 'roundingMode', ['ceil', 'floor', 'expand', 'trunc', 'halfCeil', 'halfFloor', 'halfExpand', 'halfTrunc', 'halfEven'], 'halfExpand');
    var RM = { ceil: 'ceiling', floor: 'floor', expand: 'up', trunc: 'down', halfCeil: 'half-ceiling', halfFloor: 'half-floor', halfExpand: 'half-up', halfTrunc: 'half-down', halfEven: 'half-even' };
    s.push('rounding-mode-' + RM[r.roundingMode]);
    this._s = s.join(' ');
    if (I('num', r.locale, this._s, 1) === '\u0001') throw new RangeError('Invalid number format options');
  };
  var numero = function (x) { return typeof x === 'bigint' ? Number(x) : Number(x); };
  legato(NF.prototype, 'format', function (x) { return I('num', this._r.locale, this._s, numero(x)); });
  NF.prototype.formatToParts = function (x) {
    var s = this.format(x), dec = I('num', this._r.locale, '.0', 1.5).replace(/\d/g, ''), grp = I('num', this._r.locale, 'group-on-aligned', 1234567).replace(/\d/g, '').charAt(0);
    var o = [], i = 0, c, t, prima = true, dopo = false, st = this._r.style;
    var tipo = function (ch) {
      if (/\d/.test(ch)) return dopo ? 'fraction' : 'integer';
      if (ch === dec) { dopo = true; return 'decimal'; }
      if (ch === grp && !dopo) return 'group';
      if (ch === '-' || ch === '−') return 'minusSign';
      if (ch === '+') return 'plusSign';
      if (ch === '%') return 'percentSign';
      if (ch === 'E') return 'exponentSeparator';
      if (/\s/.test(ch)) return 'literal';
      return st === 'currency' ? 'currency' : st === 'unit' ? 'unit' : (this && this._r && this._r.notation === 'compact') ? 'compact' : 'literal';
    };
    for (i = 0; i < s.length; i++) {
      c = s.charAt(i); t = tipo.call(this, c);
      if (o.length && o[o.length - 1].type === t && t !== 'decimal' && t !== 'group') o[o.length - 1].value += c;
      else o.push({ type: t, value: c });
    }
    return o;
  };
  NF.prototype.formatRange = function (a, b) { return this.format(a) + '–' + this.format(b); };
  NF.prototype.formatRangeToParts = function (a, b) { return this.formatToParts(a).concat([{ type: 'literal', value: '–' }], this.formatToParts(b)); };
  NF.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) o[k] = this._r[k]; return o; };
  NF.supportedLocalesOf = supportati;
  Intl.NumberFormat = NF;

  /* ------------------------------------------------------ DateTimeFormat */
  var CAMPI = ['weekday', 'era', 'year', 'month', 'day', 'dayPeriod', 'hour', 'minute', 'second', 'fractionalSecondDigits', 'timeZoneName'];
  var STILI = { full: 1, long: 2, medium: 3, short: 4 };
  var DTF = function (loc, o, req, def) {
    if (!(this instanceof DTF)) return new DTF(loc, o);
    o = o === undefined ? {} : Object(o);
    var r = this._r = { locale: scegli(loc), calendar: 'gregory', numberingSystem: 'latn' }, s = '', k, c = false, h;
    r.timeZone = o.timeZone === undefined ? I('tz') : String(o.timeZone);
    if (/^utc$/i.test(r.timeZone)) r.timeZone = 'UTC';
    r.dateStyle = opz(o, 'dateStyle', ['full', 'long', 'medium', 'short'], undefined);
    r.timeStyle = opz(o, 'timeStyle', ['full', 'long', 'medium', 'short'], undefined);
    for (k = 0; k < CAMPI.length; k++) if (o[CAMPI[k]] !== undefined) c = true;
    if (c && (r.dateStyle || r.timeStyle)) throw new TypeError('Can\'t set option ' + CAMPI.filter(function (x) { return o[x] !== undefined; })[0] + ' when dateStyle is used');
    if (!c && !r.dateStyle && !r.timeStyle) {
      req = req || 'date'; def = def || 'date';
      if (def === 'date' || def === 'all') { o = Object.assign({}, o, { year: 'numeric', month: 'numeric', day: 'numeric' }); }
      if (def === 'time' || def === 'all') { o = Object.assign({}, o, { hour: 'numeric', minute: '2-digit', second: '2-digit' }); }
    }
    if (o.hour12 !== undefined) r.hour12 = !!o.hour12;
    r.hourCycle = opz(o, 'hourCycle', ['h11', 'h12', 'h23', 'h24'], undefined);
    if (r.dateStyle || r.timeStyle) { this._s = 'S' + (STILI[r.dateStyle] || 0) + (STILI[r.timeStyle] || 0); }
    else {
      var M = {
        weekday: { narrow: 'EEEEE', short: 'EEE', long: 'EEEE' }, era: { narrow: 'GGGGG', short: 'G', long: 'GGGG' },
        year: { numeric: 'y', '2-digit': 'yy' }, month: { numeric: 'M', '2-digit': 'MM', short: 'MMM', long: 'MMMM', narrow: 'MMMMM' },
        day: { numeric: 'd', '2-digit': 'dd' }, dayPeriod: { narrow: 'BBBBB', short: 'B', long: 'BBBB' },
        minute: { numeric: 'm', '2-digit': 'mm' }, second: { numeric: 's', '2-digit': 'ss' },
        timeZoneName: { short: 'z', long: 'zzzz', shortOffset: 'O', longOffset: 'OOOO', shortGeneric: 'v', longGeneric: 'vvvv' }
      };
      for (k = 0; k < CAMPI.length; k++) {
        var n = CAMPI[k], v = o[n];
        if (v === undefined) continue;
        v = String(v);
        if (n === 'hour') {
          h = r.hour12 === true || r.hourCycle === 'h12' ? 'h' : r.hour12 === false || r.hourCycle === 'h23' ? 'H' : r.hourCycle === 'h11' ? 'K' : r.hourCycle === 'h24' ? 'k' : 'j';
          if (v !== 'numeric' && v !== '2-digit') throw new RangeError('hour: ' + v);
          s += v === '2-digit' ? h + h : h; r.hour = v;
        } else if (n === 'fractionalSecondDigits') { var f = num(o, n, 1, 3, 0); s += new Array(f + 1).join('S'); r[n] = f; }
        else { if (!M[n][v]) throw new RangeError('Value ' + v + ' out of range for Intl.DateTimeFormat options property ' + n); s += M[n][v]; r[n] = v; }
      }
      this._s = s;
      if (r.hour) {
        var p = I('pat', r.locale, s);
        r.hourCycle = /H/.test(p) ? 'h23' : /K/.test(p) ? 'h11' : /k/.test(p) ? 'h24' : 'h12';
        r.hour12 = r.hourCycle === 'h12' || r.hourCycle === 'h11';
        this._p = p;
      } else { delete r.hourCycle; delete r.hour12; this._p = I('pat', r.locale, s); }
    }
  };
  var ms = function (d) {
    var x = d === undefined ? Date.now() : (d instanceof Date ? d.getTime() : Number(d));
    if (!isFinite(x)) throw new RangeError('Invalid time value');
    return x;
  };
  legato(DTF.prototype, 'format', function (d) { return I('dat', this._r.locale, this._s, this._r.timeZone, ms(d)); });
  DTF.prototype.formatToParts = function (d) {
    var s = this.format(d), p = this._p, o = [], toks = [], i, m, re = /'([^']*)'|([a-zA-Z])\2*|[^a-zA-Z']+/g;
    var TIPI = { G: 'era', y: 'year', Y: 'year', M: 'month', L: 'month', d: 'day', E: 'weekday', c: 'weekday', a: 'dayPeriod', b: 'dayPeriod', B: 'dayPeriod',
                 h: 'hour', H: 'hour', k: 'hour', K: 'hour', m: 'minute', s: 'second', S: 'fractionalSecond', z: 'timeZoneName', Z: 'timeZoneName', O: 'timeZoneName', v: 'timeZoneName', V: 'timeZoneName', x: 'timeZoneName', X: 'timeZoneName' };
    if (!p) return [{ type: 'literal', value: s }];
    while ((m = re.exec(p))) {
      if (m[1] !== undefined) toks.push({ l: m[1] === '' ? "'" : m[1] });
      else if (m[2]) toks.push({ t: TIPI[m[2]] || 'literal' });
      else toks.push({ l: m[0] });
    }
    var pos = 0;
    for (i = 0; i < toks.length; i++) {
      if (toks[i].l !== undefined) {
        if (s.substr(pos, toks[i].l.length) === toks[i].l) { o.push({ type: 'literal', value: toks[i].l }); pos += toks[i].l.length; }
        continue;
      }
      var fine = s.length, j;
      for (j = i + 1; j < toks.length; j++) if (toks[j].l !== undefined && toks[j].l.length) { var q = s.indexOf(toks[j].l, pos + 1); if (q >= 0) fine = q; break; }
      if (fine > pos) o.push({ type: toks[i].t, value: s.slice(pos, fine) });
      pos = fine;
    }
    if (pos < s.length) o.push({ type: 'literal', value: s.slice(pos) });
    return o;
  };
  DTF.prototype.formatRange = function (a, b) { var x = this.format(a), y = this.format(b); return x === y ? x : x + ' – ' + y; };
  DTF.prototype.formatRangeToParts = function (a, b) { return this.formatToParts(a).concat([{ type: 'literal', value: ' – ', source: 'shared' }], this.formatToParts(b)); };
  DTF.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) if (this._r[k] !== undefined) o[k] = this._r[k]; return o; };
  DTF.supportedLocalesOf = supportati;
  Intl.DateTimeFormat = DTF;

  /* ------------------------------------------------------- PluralRules */
  var PR = function (loc, o) {
    if (!(this instanceof PR)) throw new TypeError('Constructor Intl.PluralRules requires \'new\'');
    this._r = { locale: scegli(loc), type: opz(o, 'type', ['cardinal', 'ordinal'], 'cardinal') };
    this._r.minimumIntegerDigits = 1; this._r.minimumFractionDigits = num(o, 'minimumFractionDigits', 0, 20, 0); this._r.maximumFractionDigits = num(o, 'maximumFractionDigits', 0, 20, 3);
    this._r.pluralCategories = this._r.type === 'ordinal' ? ['few', 'many', 'one', 'other', 'two', 'zero'].filter(function () { return true; }) : ['one', 'other'];
  };
  PR.prototype.select = function (n) { return I('plu', this._r.locale, this._r.type, Number(n)) || 'other'; };
  PR.prototype.selectRange = function (a, b) { return this.select(b); };
  PR.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) o[k] = this._r[k]; return o; };
  PR.supportedLocalesOf = supportati;
  Intl.PluralRules = PR;

  /* ------------------------------------------------ RelativeTimeFormat */
  var RTF = function (loc, o) {
    if (!(this instanceof RTF)) throw new TypeError('Constructor Intl.RelativeTimeFormat requires \'new\'');
    this._r = { locale: scegli(loc), style: opz(o, 'style', ['long', 'short', 'narrow'], 'long'), numeric: opz(o, 'numeric', ['always', 'auto'], 'always'), numberingSystem: 'latn' };
  };
  RTF.prototype.format = function (v, u) {
    u = String(u).replace(/s$/, '');
    if (['second', 'minute', 'hour', 'day', 'week', 'month', 'quarter', 'year'].indexOf(u) < 0) throw new RangeError('Invalid unit argument for format() \'' + u + '\'');
    v = Number(v); if (!isFinite(v)) throw new RangeError('Invalid value');
    return I('rel', this._r.locale, this._r.style + '|' + this._r.numeric, v, u);
  };
  RTF.prototype.formatToParts = function (v, u) {
    var s = this.format(v, u), m = /[\d.,  ]*\d/.exec(s);
    if (!m) return [{ type: 'literal', value: s }];
    var o = [];
    if (m.index) o.push({ type: 'literal', value: s.slice(0, m.index) });
    o.push({ type: 'integer', value: m[0], unit: String(u).replace(/s$/, '') });
    if (m.index + m[0].length < s.length) o.push({ type: 'literal', value: s.slice(m.index + m[0].length) });
    return o;
  };
  RTF.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) o[k] = this._r[k]; return o; };
  RTF.supportedLocalesOf = supportati;
  Intl.RelativeTimeFormat = RTF;

  /* ------------------------------------------------------------ Collator */
  var COL = function (loc, o) {
    if (!(this instanceof COL)) return new COL(loc, o);
    var u = opz(o, 'usage', ['sort', 'search'], 'sort');
    this._r = { locale: scegli(loc), usage: u, sensitivity: opz(o, 'sensitivity', ['base', 'accent', 'case', 'variant'], 'variant'),
                ignorePunctuation: !!(o && o.ignorePunctuation), collation: 'default', numeric: !!(o && o.numeric), caseFirst: opz(o, 'caseFirst', ['upper', 'lower', 'false'], 'false') };
    if (/-u-.*kn/.test(this._r.locale) && !(o && o.numeric !== undefined)) this._r.numeric = true;
    this._o = this._r.sensitivity + '|' + (this._r.numeric ? 'n' : '') + (this._r.ignorePunctuation ? 'p' : '');
  };
  legato(COL.prototype, 'compare', function (a, b) { return I('col', this._r.locale, this._o, String(a), String(b)); });
  COL.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) o[k] = this._r[k]; return o; };
  COL.supportedLocalesOf = supportati;
  Intl.Collator = COL;

  /* ----------------------------------------------------------- Segmenter */
  var SEG = function (loc, o) {
    if (!(this instanceof SEG)) throw new TypeError('Constructor Intl.Segmenter requires \'new\'');
    this._r = { locale: scegli(loc), granularity: opz(o, 'granularity', ['grapheme', 'word', 'sentence'], 'grapheme') };
  };
  SEG.prototype.segment = function (s) {
    s = String(s);
    var g = this._r.granularity, l = JSON.parse(I('seg', this._r.locale, g, s) || '[]'), parti = l.map(function (x) {
      var o = { segment: s.slice(x[0], x[1]), index: x[0], input: s };
      if (g === 'word') o.isWordLike = !!x[2];
      return o;
    });
    var r = {
      containing: function (i) { i = i === undefined ? 0 : Number(i); for (var k = 0; k < parti.length; k++) if (i >= parti[k].index && i < parti[k].index + parti[k].segment.length) return parti[k]; return undefined; }
    };
    r[Symbol.iterator] = function () { return parti[Symbol.iterator](); };
    return r;
  };
  SEG.prototype.resolvedOptions = function () { return { locale: this._r.locale, granularity: this._r.granularity }; };
  SEG.supportedLocalesOf = supportati;
  Intl.Segmenter = SEG;

  /* ---------------------------------------------------------- ListFormat */
  var LF = function (loc, o) {
    if (!(this instanceof LF)) throw new TypeError('Constructor Intl.ListFormat requires \'new\'');
    this._r = { locale: scegli(loc), type: opz(o, 'type', ['conjunction', 'disjunction', 'unit'], 'conjunction'), style: opz(o, 'style', ['long', 'short', 'narrow'], 'long') };
  };
  LF.prototype.format = function (l) {
    var a = [];
    if (l !== undefined) for (var x of l) { if (typeof x !== 'string') throw new TypeError('Iterable yielded ' + x + ' which is not a string'); a.push(x); }
    return I('lis', this._r.locale, this._r.type + '|' + this._r.style, a.join('\u0001'));
  };
  LF.prototype.formatToParts = function (l) {
    var a = Array.from(l), s = this.format(a), o = [], pos = 0, i, q;
    for (i = 0; i < a.length; i++) {
      q = s.indexOf(a[i], pos);
      if (q > pos) o.push({ type: 'literal', value: s.slice(pos, q) });
      o.push({ type: 'element', value: a[i] }); pos = q + a[i].length;
    }
    if (pos < s.length) o.push({ type: 'literal', value: s.slice(pos) });
    return o;
  };
  LF.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) o[k] = this._r[k]; return o; };
  LF.supportedLocalesOf = supportati;
  Intl.ListFormat = LF;

  /* -------------------------------------------------------- DisplayNames */
  var DN = function (loc, o) {
    if (!(this instanceof DN)) throw new TypeError('Constructor Intl.DisplayNames requires \'new\'');
    if (!o || o.type === undefined) throw new TypeError('Required option type is missing');
    this._r = { locale: scegli(loc), style: opz(o, 'style', ['long', 'short', 'narrow'], 'long'), type: opz(o, 'type', ['language', 'region', 'script', 'currency', 'calendar', 'dateTimeField'], undefined),
                fallback: opz(o, 'fallback', ['code', 'none'], 'code') };
  };
  DN.prototype.of = function (c) {
    c = String(c);
    var r = I('nom', this._r.locale, this._r.type + '|' + this._r.style, c);
    if (!r || r === c) return this._r.fallback === 'code' ? (this._r.type === 'region' || this._r.type === 'currency' ? c.toUpperCase() : c) : undefined;
    return r;
  };
  DN.prototype.resolvedOptions = function () { var o = {}, k; for (k in this._r) o[k] = this._r[k]; return o; };
  DN.supportedLocalesOf = supportati;
  Intl.DisplayNames = DN;

  G.Intl = Intl;

  /* ------------------------------------- the toLocale* of the language */
  var nfc = {}, dfc = {};
  var chiave = function (l, o) { try { return JSON.stringify([l, o]); } catch (e) { return null; } };
  Number.prototype.toLocaleString = function (l, o) {
    var k = chiave(l, o), f = k && nfc[k];
    if (!f) { f = new NF(l, o); if (k) nfc[k] = f; }
    return f.format(Number(this));
  };
  if (typeof BigInt !== 'undefined') BigInt.prototype.toLocaleString = function (l, o) { return new NF(l, o).format(Number(this)); };
  var dataLoc = function (quale) {
    return function (l, o) {
      if (isNaN(this.getTime())) return 'Invalid Date';
      var k = chiave([quale, l], o), f = k && dfc[k];
      if (!f) { f = new DTF(l, o, quale, quale); if (k) dfc[k] = f; }
      return f.format(this);
    };
  };
  Date.prototype.toLocaleString = dataLoc('all');
  Date.prototype.toLocaleDateString = dataLoc('date');
  Date.prototype.toLocaleTimeString = dataLoc('time');
  var colc = {};
  String.prototype.localeCompare = function (b, l, o) {
    var k = chiave(l, o), c = k && colc[k];
    if (this === undefined || this === null) throw new TypeError('String.prototype.localeCompare called on null or undefined');
    if (!c) { c = new COL(l, o); if (k) colc[k] = c; }
    return c.compare(String(this), String(b));
  };
})(this);

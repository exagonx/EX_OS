/* =============================================================================
 * exwin/bin/exbrowser/browser_wasm.js — the WebAssembly object (P12)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The JavaScript API of WebAssembly over __exos_wasm (browser_wasm.c, WAMR):
 * Module (exports, imports, customSections), Instance, Memory (an exported
 * one: buffer on the module's memory, grow), Global, Table (exported, only
 * length), validate, compile, instantiate, compileStreaming,
 * instantiateStreaming, CompileError, LinkError, RuntimeError.
 * The functions a module imports are kept here; the module calls them
 * through __exos_wasm_su(instance, import, args...).
 *
 * ! A Memory MADE BY JAVASCRIPT (new WebAssembly.Memory) has no module
 * behind it: it is a plain ArrayBuffer here, and importing it into a module
 * is a LinkError. ! i64 is a Number, not a BigInt.
 * ========================================================================== */
(function (G) {
  if (typeof G.__exos_wasm !== 'function') return;
  var W = G.__exos_wasm;
  var errore = function (nome) {
    var C = function (m) { var e = new Error(m); e.name = nome; Object.setPrototypeOf(e, C.prototype); return e; };
    C.prototype = Object.create(Error.prototype); C.prototype.name = nome; C.prototype.constructor = C;
    return C;
  };
  var CompileError = errore('CompileError'), LinkError = errore('LinkError'), RuntimeError = errore('RuntimeError');
  var byte = function (b) {
    if (b instanceof ArrayBuffer || (b && b.buffer instanceof ArrayBuffer)) return b;
    throw new TypeError('WebAssembly: the argument must be a BufferSource');
  };
  var FUNZ = {};                 /* instance id -> the imported functions */
  G.__exos_wasm_su = function (ist, i) {
    var f = FUNZ[ist] && FUNZ[ist][i], a = Array.prototype.slice.call(arguments, 2);
    return f.apply(undefined, a);
  };

  var Module = function (b) {
    if (!(this instanceof Module)) throw new TypeError('WebAssembly.Module must be invoked with new');
    var id = W('modulo', byte(b));
    if (id === undefined) throw new CompileError(W('errore') || 'WebAssembly non disponibile');
    if (id < 0) throw new CompileError(W('errore'));
    this._id = id;
    this._d = JSON.parse(W('descrivi', id));
    this._b = b;
  };
  Module.exports = function (m) { return m._d.exports.map(function (e) { return { name: e.name, kind: e.kind }; }); };
  Module.imports = function (m) { return m._d.imports.map(function (e) { return { module: e.module, name: e.name, kind: e.kind }; }); };
  Module.customSections = function () { return []; };

  var Memory = function (d, ist, k) {
    if (!(this instanceof Memory)) throw new TypeError('WebAssembly.Memory must be invoked with new');
    if (ist === undefined) {
      /* made by JavaScript: an ArrayBuffer of initial pages */
      var p = d && d.initial !== undefined ? d.initial : 0;
      this._solo = new ArrayBuffer(p * 65536); this._max = d && d.maximum; return;
    }
    this._ist = ist; this._k = k; this._buf = null; this._pagine = -1;
  };
  Object.defineProperty(Memory.prototype, 'buffer', { get: function () {
    if (this._solo) return this._solo;
    var p = W('pagine', this._ist, this._k);
    if (this._buf && p === this._pagine) return this._buf;
    if (this._buf) W('stacca', this._buf);
    this._buf = W('memoria', this._ist, this._k); this._pagine = p;
    return this._buf;
  } });
  Memory.prototype.grow = function (n) {
    if (this._solo) {
      var vecchie = this._solo.byteLength / 65536, nb = new ArrayBuffer((vecchie + n) * 65536);
      new Uint8Array(nb).set(new Uint8Array(this._solo)); this._solo = nb; return vecchie;
    }
    var r = W('cresci', this._ist, this._k, n >>> 0);
    if (r < 0) throw new RangeError('WebAssembly.Memory.grow: failed');
    if (this._buf) { W('stacca', this._buf); this._buf = null; }
    return r;
  };

  var Global = function (d, v, ist, k) {
    if (!(this instanceof Global)) throw new TypeError('WebAssembly.Global must be invoked with new');
    this._ist = ist; this._k = k; this._v = v === undefined ? 0 : Number(v); this._mut = !!(d && d.mutable);
  };
  Object.defineProperty(Global.prototype, 'value', {
    get: function () { return this._ist === undefined ? this._v : W('globale', this._ist, this._k); },
    set: function (v) { if (this._ist === undefined) this._v = Number(v); else W('globale', this._ist, this._k, Number(v)); }
  });
  Global.prototype.valueOf = function () { return this.value; };

  var Table = function (d) { if (!(this instanceof Table)) throw new TypeError('WebAssembly.Table must be invoked with new'); this.length = d && d.initial || 0; };
  Table.prototype.get = function () { return null; };
  Table.prototype.grow = function () { throw new RangeError('WebAssembly.Table.grow: non ancora'); };
  Table.prototype.set = function () {};

  var Instance = function (m, imp) {
    if (!(this instanceof Instance)) throw new TypeError('WebAssembly.Instance must be invoked with new');
    if (!(m instanceof Module)) throw new TypeError('WebAssembly.Instance: argument 0 must be a WebAssembly.Module');
    var id = W('prenota'), elenco = [], fz = [], i, d, v, t = this;
    if (id < 0) throw new RangeError(W('errore'));
    for (i = 0; i < m._d.imports.length; i++) {
      d = m._d.imports[i];
      if (!imp || typeof imp[d.module] !== 'object' || imp[d.module] === null) throw new TypeError('WebAssembly.Instance: import object field \'' + d.module + '\' is not an Object');
      v = imp[d.module][d.name];
      if (d.kind === 'function') {
        if (typeof v !== 'function') throw new LinkError('WebAssembly.Instance: import ' + d.module + '.' + d.name + ' must be callable');
        fz.push(v.__exos_wasm ? v.__exos_wasm : v); elenco.push(fz.length - 1);
      } else if (d.kind === 'memory') {
        if (!(v instanceof Memory) || v._solo) throw new LinkError('WebAssembly.Instance: memory import ' + d.module + '.' + d.name + ' must be a memory exported by a module');
        elenco.push([v._ist, v._k]);
      } else if (d.kind === 'global') {
        elenco.push(v instanceof Global ? v.value : Number(v));
      } else throw new LinkError('WebAssembly.Instance: import of a ' + d.kind + ' not supported yet');
    }
    FUNZ[id] = fz;
    if (!W('istanza', id, m._id, elenco)) { delete FUNZ[id]; throw new LinkError(W('errore')); }
    var es = {};
    m._d.exports.forEach(function (e, k) {
      if (e.kind === 'function') {
        var f = function () {
          var a = ['chiama', id, k], j, r;
          for (j = 0; j < arguments.length; j++) a.push(Number(arguments[j]));
          r = W.apply(null, a);
          if (typeof r === 'string' && r.charAt(0) === '\u0001') throw new RuntimeError(r.slice(1));
          return r;
        };
        Object.defineProperty(f, 'name', { value: String(k) });
        Object.defineProperty(f, 'length', { value: e.params.length });
        es[e.name] = f;
      } else if (e.kind === 'memory') es[e.name] = new Memory(null, id, k);
      else if (e.kind === 'global') es[e.name] = new Global({ value: e.type }, undefined, id, k);
      else es[e.name] = new Table({ initial: 0 });
    });
    this.exports = Object.freeze(es);
    t._id = id;
  };

  var promessa = function (f) { return new Promise(function (ok, no) { setTimeout(function () { try { ok(f()); } catch (e) { no(e); } }, 0); }); };
  var risposta = function (r) {
    return Promise.resolve(r).then(function (x) {
      if (!x || typeof x.arrayBuffer !== 'function') throw new TypeError('WebAssembly: a Response is needed');
      return x.arrayBuffer();
    });
  };
  G.WebAssembly = {
    Module: Module, Instance: Instance, Memory: Memory, Global: Global, Table: Table,
    CompileError: CompileError, LinkError: LinkError, RuntimeError: RuntimeError,
    validate: function (b) { return !!W('valida', byte(b)); },
    compile: function (b) { return promessa(function () { return new Module(b); }); },
    instantiate: function (x, imp) {
      if (x instanceof Module) return promessa(function () { return new Instance(x, imp); });
      return promessa(function () { var m = new Module(x); return { module: m, instance: new Instance(m, imp) }; });
    },
    compileStreaming: function (r) { return risposta(r).then(function (b) { return new Module(b); }); },
    instantiateStreaming: function (r, imp) { return risposta(r).then(function (b) { var m = new Module(b); return { module: m, instance: new Instance(m, imp) }; }); }
  };
})(this);

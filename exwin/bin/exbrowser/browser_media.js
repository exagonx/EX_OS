/* =============================================================================
 * exwin/bin/exbrowser/browser_media.js — HTMLMediaElement and MSE (P13)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * <video> and <audio> over __exos_lettore (browser_media.c, libexmedia over
 * FFmpeg): play, pause, load, currentTime, duration, paused, ended, volume,
 * muted, readyState, videoWidth/Height, buffered, seekable, canPlayType, and
 * the events (loadstart, loadedmetadata, loadeddata, canplay, canplaythrough,
 * play, playing, pause, timeupdate, seeking, seeked, ended, durationchange,
 * volumechange, progress, error), from a turn every 250 ms.
 * MediaSource and SourceBuffer (Media Source Extensions): appendBuffer feeds
 * the decoder as the bytes come, a video and an audio SourceBuffer each a
 * stream of their own (the way YouTube sends them).
 *
 * ! A page's script may set src at any time: the turn notices the change (an
 * attribute exdom reflects) and loads again. A whole file is fetched by the
 * browser before playing (no range requests yet).
 * ========================================================================== */
(function (G) {
  if (typeof G.__exos_lettore !== 'function') return;
  var L = G.__exos_lettore, EP = Object.getPrototypeOf(document);
  if (!EP) return;
  var media = function (el) { var t = el && String(el.tagName).toLowerCase(); return t === 'video' || t === 'audio'; };
  var evento = function (el, tipo) {
    try { el.dispatchEvent(new Event(tipo)); } catch (e) {}
  };
  var Tempi = function (l) { this._l = l; this.length = l.length; };
  Tempi.prototype.start = function (i) { if (i >= this._l.length) throw new RangeError('TimeRanges.start'); return this._l[i][0]; };
  Tempi.prototype.end = function (i) { if (i >= this._l.length) throw new RangeError('TimeRanges.end'); return this._l[i][1]; };
  G.TimeRanges = Tempi;

  /* the state of an element, kept on it */
  var S = function (el) {
    if (!el.__media) {
      el.__media = { id: L('nuovo', el), src: null, st: null, pausa: true, muto: false, vol: 1, eventi: {}, ultimo_t: -1,
                     rate: 1, ms: null, pronto: false };
    }
    return el.__media;
  };
  var sorgente = function (el) {
    var s = el.getAttribute('src'), c;
    if (s) return s;
    for (c = el.firstElementChild; c; c = c.nextElementSibling)
      if (String(c.tagName).toLowerCase() === 'source' && c.getAttribute('src')) {
        var ty = c.getAttribute('type');
        if (!ty || EP.canPlayType.call(el, ty)) return c.getAttribute('src');
      }
    return '';
  };
  var carica = function (el) {
    var m = S(el), s = sorgente(el), ms;
    m.src = s; m.st = null; m.pronto = false; m.inviati = {};
    if (!s || m.id < 0) return;
    evento(el, 'loadstart');
    ms = s.indexOf('blob:') === 0 && G.__exos_blob ? G.__exos_blob(s) : null;
    if (ms && ms._ms) { m.ms = ms; ms._el = el; ms._apri(); return; }
    var u = new URL(s, location.href).href;
    setTimeout(function () { if (!L('carica', m.id, u)) { m.errore = { code: 4, message: 'MEDIA_ERR_SRC_NOT_SUPPORTED' }; evento(el, 'error'); } }, 0);
  };
  var giro = function () {
    var l = document.querySelectorAll('video, audio'), i, el, m, st;
    for (i = 0; i < l.length; i++) {
      el = l[i]; m = el.__media;
      if (!m) { if (sorgente(el)) { carica(el); m = S(el); if (el.hasAttribute('autoplay')) el.play(); } continue; }
      if (sorgente(el) !== m.src) { carica(el); continue; }
      if (m.id < 0) continue;
      st = JSON.parse(L('stato', m.id));
      if (!m.pronto && st.pronto) {
        m.pronto = true; m.st = st;
        ['durationchange', 'loadedmetadata', 'loadeddata', 'canplay', 'canplaythrough'].forEach(function (k) { evento(el, k); });
        if (!m.pausa) { L('suona', m.id); evento(el, 'playing'); }
      }
      if (st.errore && !m.errore) { m.errore = { code: 4, message: 'MEDIA_ERR_SRC_NOT_SUPPORTED' }; evento(el, 'error'); }
      m.st = st;
      if (st.suona && Math.abs(st.t - m.ultimo_t) >= 0.2) { m.ultimo_t = st.t; evento(el, 'timeupdate'); }
      if (st.finito && st.pronto && !m.pausa) {
        if (el.hasAttribute('loop')) { L('vai', m.id, 0); L('suona', m.id); }
        else { m.pausa = true; evento(el, 'timeupdate'); evento(el, 'pause'); evento(el, 'ended'); }
      }
    }
  };
  setInterval(giro, 250);

  var prop = function (n, g, s) { if (!(n in EP)) Object.defineProperty(EP, n, { get: g, set: s, configurable: true }); };
  EP.play = function () {
    if (!media(this)) return undefined;
    var m = S(this), el = this;
    if (m.src === null) carica(this);
    m.pausa = false;
    evento(this, 'play');
    if (m.pronto) { L('suona', m.id); evento(this, 'playing'); }
    return Promise.resolve();
  };
  EP.pause = function () { if (!media(this)) return; var m = S(this); if (!m.pausa) { m.pausa = true; L('pausa', m.id); evento(this, 'pause'); } };
  EP.load = function () { if (media(this)) carica(this); };
  EP.canPlayType = function (t) {
    t = String(t).toLowerCase();
    if (/^(video|audio)\/(mp4|webm|ogg|mpeg|mp3|wav|x-wav|aac|flac|x-m4a|quicktime|x-matroska)/.test(t)) {
      if (/codecs/.test(t) && !/avc1|mp4a|vp8|vp9|vp09|av01|opus|vorbis|mp3|flac|theora/.test(t)) return '';
      return 'probably';
    }
    return '';
  };
  EP.fastSeek = function (t) { this.currentTime = t; };
  EP.addTextTrack = function () { return { mode: 'disabled', cues: [], addCue: function () {} }; };
  prop('currentTime', function () { if (!media(this)) return undefined; var m = S(this); return m.st ? m.st.t : 0; },
       function (t) { if (!media(this)) return; var m = S(this), el = this; evento(el, 'seeking'); L('vai', m.id, Number(t) || 0); if (m.st) m.st.t = Number(t) || 0; setTimeout(function () { evento(el, 'seeked'); evento(el, 'timeupdate'); }, 0); });
  prop('duration', function () { if (!media(this)) return undefined; var m = S(this); return m.st && m.st.pronto ? (m.st.durata || (m.ms ? m.ms.duration : Infinity)) : NaN; });
  prop('paused', function () { return media(this) ? S(this).pausa : undefined; });
  prop('ended', function () { return media(this) ? !!(S(this).st && S(this).st.finito) : undefined; });
  prop('readyState', function () { return media(this) ? (S(this).pronto ? 4 : (S(this).src ? 0 : 0)) : undefined; });
  prop('networkState', function () { return media(this) ? (S(this).src ? 1 : 0) : undefined; });
  prop('videoWidth', function () { return media(this) ? (S(this).st ? S(this).st.w : 0) : undefined; });
  prop('videoHeight', function () { return media(this) ? (S(this).st ? S(this).st.h : 0) : undefined; });
  prop('volume', function () { return media(this) ? S(this).vol : undefined; },
       function (v) { if (!media(this)) return; v = Number(v); if (!(v >= 0 && v <= 1)) throw new RangeError('volume'); var m = S(this); m.vol = v; L('volume', m.id, m.muto ? 0 : v); evento(this, 'volumechange'); });
  prop('muted', function () { return media(this) ? S(this).muto : undefined; },
       function (v) { if (!media(this)) return; var m = S(this); m.muto = !!v; L('volume', m.id, m.muto ? 0 : m.vol); evento(this, 'volumechange'); });
  prop('playbackRate', function () { return media(this) ? S(this).rate : undefined; }, function (v) { if (media(this)) S(this).rate = Number(v) || 1; });
  prop('defaultPlaybackRate', function () { return media(this) ? 1 : undefined; }, function () {});
  prop('currentSrc', function () { return media(this) ? (S(this).src ? new URL(S(this).src, location.href).href : '') : undefined; });
  prop('error', function () { return media(this) ? (S(this).errore || null) : undefined; });
  prop('buffered', function () {
    if (!media(this)) return undefined;
    var m = S(this), f = 0;
    if (m.ms) f = m.ms._fin(); else if (m.st && m.st.pronto) f = m.st.durata;
    return new Tempi(f > 0 ? [[0, f]] : []);
  });
  prop('seekable', function () { if (!media(this)) return undefined; var d = this.duration; return new Tempi(d > 0 && isFinite(d) ? [[0, d]] : []); });
  prop('played', function () { return media(this) ? new Tempi([]) : undefined; });
  prop('srcObject', function () { return media(this) ? (S(this).obj || null) : undefined; },
       function (o) { if (!media(this)) return; S(this).obj = o; if (o && o._ms) { this.src = URL.createObjectURL(o); } });
  prop('textTracks', function () { return media(this) ? { length: 0, addEventListener: function () {} } : undefined; });
  prop('audioTracks', function () { return media(this) ? { length: 0 } : undefined; });
  prop('videoTracks', function () { return media(this) ? { length: 0 } : undefined; });
  ['HTMLMediaElement', 'HTMLVideoElement', 'HTMLAudioElement'].forEach(function (n) {
    var f = function () { throw new TypeError('Illegal constructor'); };
    f.prototype = EP; f.HAVE_NOTHING = 0; f.HAVE_METADATA = 1; f.HAVE_CURRENT_DATA = 2; f.HAVE_FUTURE_DATA = 3; f.HAVE_ENOUGH_DATA = 4;
    G[n] = f;
  });
  G.Audio = function (src) { var a = document.createElement('audio'); if (src !== undefined) a.setAttribute('src', src); return a; };

  /* ------------------------------------------- Media Source Extensions */
  var Bersaglio = function () { this._a = {}; };
  Bersaglio.prototype.addEventListener = function (n, f) { (this._a[n] = this._a[n] || []).push(f); };
  Bersaglio.prototype.removeEventListener = function (n, f) { var l = this._a[n] || [], i = l.indexOf(f); if (i >= 0) l.splice(i, 1); };
  Bersaglio.prototype.dispatchEvent = function (e) { this._lancia(e.type); return true; };
  Bersaglio.prototype._lancia = function (n) {
    var e = { type: n, target: this, currentTarget: this }, l = (this._a[n] || []).slice(0), i;
    try { if (typeof this['on' + n] === 'function') this['on' + n](e); } catch (x) { if (G.console) console.error(String(x)); }
    for (i = 0; i < l.length; i++) try { l[i].call(this, e); } catch (x) { if (G.console) console.error(String(x)); }
  };

  var SB = function (ms, mime) {
    Bersaglio.call(this);
    this._ms = ms; this._id = L('sb'); this.mimeType = mime; this._video = /^video\//.test(mime);
    this.updating = false; this.mode = 'segments'; this.timestampOffset = 0;
    this.appendWindowStart = 0; this.appendWindowEnd = Infinity;
    this.onupdatestart = this.onupdate = this.onupdateend = this.onerror = this.onabort = null;
    if (this._id < 0) throw new Error('QuotaExceededError: troppi SourceBuffer');
  };
  SB.prototype = Object.create(Bersaglio.prototype);
  SB.prototype.appendBuffer = function (d) {
    var t = this;
    if (this.updating) { var e = new Error('appendBuffer mentre updating'); e.name = 'InvalidStateError'; throw e; }
    if (this._ms.readyState === 'ended') this._ms.readyState = 'open';
    this.updating = true;
    var ok = L('sb_aggiungi', this._id, d instanceof ArrayBuffer ? d : d.buffer ? new Uint8Array(d.buffer, d.byteOffset, d.byteLength) : d);
    setTimeout(function () {
      t.updating = false;
      if (!ok) { var q = new Error('QuotaExceededError'); q.name = 'QuotaExceededError'; t._lancia('error'); t._lancia('updateend'); return; }
      t._lancia('updatestart'); t._lancia('update'); t._lancia('updateend');
      t._ms._collega();
    }, 0);
  };
  SB.prototype.abort = function () { this.updating = false; this._lancia('abort'); };
  SB.prototype.remove = function () { var t = this; this.updating = true; setTimeout(function () { t.updating = false; t._lancia('updatestart'); t._lancia('update'); t._lancia('updateend'); }, 0); };
  SB.prototype.changeType = function (m) { this.mimeType = m; };
  Object.defineProperty(SB.prototype, 'buffered', { get: function () { var f = L('sb_fin', this._id); return new Tempi(f > 0 ? [[0, f]] : []); } });

  var MS = function () {
    if (!(this instanceof MS)) throw new TypeError('MediaSource requires new');
    Bersaglio.call(this);
    this._ms = true; this.readyState = 'closed'; this.duration = NaN;
    this.sourceBuffers = []; this.activeSourceBuffers = this.sourceBuffers;
    this.onsourceopen = this.onsourceended = this.onsourceclose = null;
  };
  MS.prototype = Object.create(Bersaglio.prototype);
  MS.isTypeSupported = function (t) { return EP.canPlayType.call({ tagName: 'video' }, t) !== ''; };
  MS.canConstructInDedicatedWorker = false;
  MS.prototype._apri = function () { var t = this; if (this.readyState !== 'closed') return; this.readyState = 'open'; setTimeout(function () { t._lancia('sourceopen'); }, 0); };
  MS.prototype.addSourceBuffer = function (mime) {
    if (this.readyState !== 'open') { var e = new Error('addSourceBuffer: MediaSource non aperta'); e.name = 'InvalidStateError'; throw e; }
    if (!MS.isTypeSupported(mime)) { var n = new Error('tipo non supportato: ' + mime); n.name = 'NotSupportedError'; throw n; }
    var s = new SB(this, String(mime));
    this.sourceBuffers.push(s);
    this._collega();
    return s;
  };
  MS.prototype.removeSourceBuffer = function (s) { var i = this.sourceBuffers.indexOf(s); if (i >= 0) this.sourceBuffers.splice(i, 1); };
  MS.prototype._video = function () { for (var i = 0; i < this.sourceBuffers.length; i++) if (this.sourceBuffers[i]._video) return this.sourceBuffers[i]; return null; };
  MS.prototype._audio = function () { for (var i = 0; i < this.sourceBuffers.length; i++) if (!this.sourceBuffers[i]._video) return this.sourceBuffers[i]; return null; };
  MS.prototype._collega = function () {
    var el = this._el, v = this._video(), a = this._audio();
    if (!el || !el.__media) return;
    L('collega', el.__media.id, v ? v._id : -1, a ? a._id : -1);
  };
  MS.prototype._fin = function () { var f = 0, i, x; for (i = 0; i < this.sourceBuffers.length; i++) { x = L('sb_fin', this.sourceBuffers[i]._id); if (i === 0 || x < f) f = x; } return f; };
  MS.prototype.endOfStream = function () {
    for (var i = 0; i < this.sourceBuffers.length; i++) L('sb_fine', this.sourceBuffers[i]._id);
    this.readyState = 'ended';
    var t = this; setTimeout(function () { t._lancia('sourceended'); }, 0);
  };
  MS.prototype.setLiveSeekableRange = function () {};
  MS.prototype.clearLiveSeekableRange = function () {};
  G.MediaSource = MS; G.ManagedMediaSource = undefined;
  G.SourceBuffer = SB; G.SourceBufferList = Array;
})(this);

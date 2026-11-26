/* tools/prove/sito/lavoro.js: the worker of lavoro.html (P11) */
importScripts('lavoro-aiuto.js');
var n = 0;
onmessage = function (e) {
  var d = e.data;
  if (d.cmd === 'somma') postMessage({ cmd: 'somma', r: aiuto_somma(d.v), tipo: d.v instanceof Uint8Array ? 'Uint8Array' : typeof d.v });
  else if (d.cmd === 'tempo') setTimeout(function () { postMessage({ cmd: 'tempo', ok: typeof document === 'undefined', self: self === globalThis }); }, 30);
  else if (d.cmd === 'mappa') postMessage({ cmd: 'mappa', m: new Map([['a', 1]]), dt: new Date(0), u: undefined });
  else if (d.cmd === 'errore') null.x;
  else if (d.cmd === 'fine') close();
  n++;
};
postMessage({ cmd: 'pronto', loc: self.location.href.split('/').pop() });

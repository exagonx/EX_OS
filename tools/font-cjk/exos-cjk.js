// tools/font-cjk/exos-cjk.js - the CJK fonts of [font-cjk] for each language
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Installed with Exilla in defaults/pref (tools/mknetinst.sh): Gecko reads
// every .js there at start. Without these lines a page asking for "serif" in
// Japanese or Chinese got the Sans faces, because Gecko had no font named
// for serif in those languages and took the first that had the characters.
// If [font-cjk] is not installed the names find nothing, and Gecko falls
// back as before.

pref("font.name.serif.ja", "Noto Serif CJK JP");
pref("font.name.sans-serif.ja", "Noto Sans CJK JP");
pref("font.name.monospace.ja", "Noto Sans Mono CJK JP");
pref("font.name.serif.ko", "Noto Serif CJK KR");
pref("font.name.sans-serif.ko", "Noto Sans CJK KR");
pref("font.name.monospace.ko", "Noto Sans Mono CJK KR");
pref("font.name.serif.zh-CN", "Noto Serif CJK SC");
pref("font.name.sans-serif.zh-CN", "Noto Sans CJK SC");
pref("font.name.monospace.zh-CN", "Noto Sans Mono CJK SC");
pref("font.name.serif.zh-TW", "Noto Serif CJK TC");
pref("font.name.sans-serif.zh-TW", "Noto Sans CJK TC");
pref("font.name.monospace.zh-TW", "Noto Sans Mono CJK TC");
pref("font.name.serif.zh-HK", "Noto Serif CJK HK");
pref("font.name.sans-serif.zh-HK", "Noto Sans CJK HK");
pref("font.name.monospace.zh-HK", "Noto Sans Mono CJK HK");

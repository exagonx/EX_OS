// tools/exilla/exos-media.js - audio and video in Exilla with several processes
//
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Installed with Exilla in defaults/pref (tools/mknetinst.sh), as exos-cjk.js.
//
// ! WITH SEVERAL PROCESSES (exilla 0.008, kernel 0.358) NO VIDEO PLAYED, NOT
// EVEN YOUTUBE. The page's process has no decoders of its own: it hands the
// work to the RDD process (video) and to the utility process (audio). In
// StaticPrefList.yaml both are on for XP_LINUX, XP_FREEBSD... and off in the
// #else, and Gecko for EX-OS is XP_EXOS: so nobody decoded anything ("Failed
// to open media", the content PDM list was GMP alone). With one process the
// parent decodes by itself, and that is why it worked before (25/11).
//
// media.utility-process.enabled is LOCKED by Gecko (MediaDecoder.cpp,
// Preferences::Lock): no file can turn it on, only the source. So, until the
// Gecko built with XP_EXOS in those prefs (tools/exilla/patch,
// modules_libpref_init_StaticPrefList.yaml.patch) is the one installed, the
// audio goes to the RDD process too. Measured in QEMU at 64 bit, several
// processes: the 8 files of tools/exilla/prova-media.sh play, H.264 and AAC
// included. With the new Gecko these lines change nothing.

pref("media.rdd-process.enabled", true);
pref("media.rdd-ffvpx.enabled", true);
pref("media.rdd-vpx.enabled", true);
pref("media.rdd-vorbis.enabled", true);
pref("media.rdd-opus.enabled", true);
pref("media.rdd-wav.enabled", true);
pref("media.allow-audio-non-utility", true);

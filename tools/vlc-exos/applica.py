#!/usr/bin/env python3
# =============================================================================
# tools/vlc-exos/applica.py SRC - teach VLC 3.0.24 the windows of ExWin
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# (voce 105 step 5, 21 November 2026) The video inside the Qt interface, as on
# X11, Windows, macOS and Wayland: a fourth kind of window handle,
# VOUT_WINDOW_TYPE_EXWIN, the id of a window of the server. The Qt interface
# gives the id of its video widget (QPlatformNativeInterface resource
# "exwin", tools/qt5-exos/qpa-exwin), the video output (src/vout_exos.c)
# makes a child window of it (WIN_MSG_CREA_FIGLIA, wserver 0.016).
#
# Until now the video had a window of its own: VLC 3 knows no other way on a
# platform it does not know, and embedded-video was set to 0 by sed.
#
# Every change is a replacement that must find its text once: a VLC that
# changed under it stops the build instead of building something else. Run
# twice, it finds its own text and changes nothing.
# =============================================================================
import sys
import os

SRC = sys.argv[1]
fatti = 0


def cambia(nome, prima, dopo):
    global fatti
    p = os.path.join(SRC, nome)
    s = open(p, encoding="utf-8").read()
    if dopo in s:
        return
    if s.count(prima) != 1:
        sys.exit("applica.py: %s: testo trovato %d volte:\n%s" % (nome, s.count(prima), prima))
    open(p, "w", encoding="utf-8").write(s.replace(prima, dopo))
    fatti += 1


# --- the window type and its handle -----------------------------------------
cambia("include/vlc_vout_window.h",
       "    VOUT_WINDOW_TYPE_WAYLAND /**< Wayland surface */,\n",
       "    VOUT_WINDOW_TYPE_WAYLAND /**< Wayland surface */,\n"
       "    VOUT_WINDOW_TYPE_EXWIN /**< EX-OS: a window of ExWin (its id) */,\n")
cambia("include/vlc_vout_window.h",
       "        struct wl_surface *wl;   /**< Wayland surface (client pointer) */\n",
       "        struct wl_surface *wl;   /**< Wayland surface (client pointer) */\n"
       "        unsigned  exwin;         /**< EX-OS: id of an ExWin window */\n")

# the screen saver is kept off while a video plays, as for the others
cambia("src/video_output/window.c",
       "      || window->type == VOUT_WINDOW_TYPE_WAYLAND))",
       "      || window->type == VOUT_WINDOW_TYPE_WAYLAND\n"
       "      || window->type == VOUT_WINDOW_TYPE_EXWIN))")

# --- the Qt interface: platform "exwin" -------------------------------------
cambia("modules/gui/qt/qt.cpp",
       "        else if( platform == qfu(\"cocoa\" ) )\n"
       "            p_sys->voutWindowType = VOUT_WINDOW_TYPE_NSOBJECT;\n",
       "        else if( platform == qfu(\"cocoa\" ) )\n"
       "            p_sys->voutWindowType = VOUT_WINDOW_TYPE_NSOBJECT;\n"
       "        else if( platform == qfu(\"exwin\") )\n"
       "            p_sys->voutWindowType = VOUT_WINDOW_TYPE_EXWIN;\n")

cambia("modules/gui/qt/components/interface_widgets.cpp",
       "#if defined(_WIN32)\n#include <QWindow>\n#include <qpa/qplatformnativeinterface.h>\n#endif\n",
       "#if defined(_WIN32) || defined(__exos__)\n#include <QWindow>\n#include <qpa/qplatformnativeinterface.h>\n#endif\n")
cambia("modules/gui/qt/components/interface_widgets.cpp",
       "        case VOUT_WINDOW_TYPE_NSOBJECT:\n"
       "            p_wnd->handle.nsobject = (void *)stable->winId();\n"
       "            break;\n",
       "        case VOUT_WINDOW_TYPE_NSOBJECT:\n"
       "            p_wnd->handle.nsobject = (void *)stable->winId();\n"
       "            break;\n"
       "#ifdef __exos__\n"
       "        case VOUT_WINDOW_TYPE_EXWIN:\n"
       "        {\n"
       "            /* the id of the widget's window in the server: the plugin\n"
       "             * makes it now if Qt has not shown the widget yet */\n"
       "            stable->winId();\n"
       "            QPlatformNativeInterface *n = qApp->platformNativeInterface();\n"
       "            void *id = n ? n->nativeResourceForWindow( \"exwin\", stable->windowHandle() ) : NULL;\n"
       "            p_wnd->handle.exwin = (unsigned)(uintptr_t)id;\n"
       "            if( p_wnd->handle.exwin == 0 )\n"
       "                msg_Err( p_intf, \"no ExWin window for the video\" );\n"
       "            else\n"
       "                msg_Dbg( p_intf, \"ExWin window %u for the video\", p_wnd->handle.exwin );\n"
       "            break;\n"
       "        }\n"
       "#endif\n")

print("applica.py: %d cambiamenti in %s" % (fatti, SRC))

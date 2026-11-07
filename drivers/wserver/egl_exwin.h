/* =============================================================================
 * drivers/wserver/egl_exwin.h — an ExWin window as an EGL native window
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     #include <exos/egl_exwin.h>
 *     struct exos_egl_finestra f = { EXOS_EGL_FINESTRA_VERSIONE, ... };
 *     eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)&f, NULL);
 *
 * What Mesa's EGL on EX-OS (voce 88, src/egl/drivers/exos in the patch)
 * takes as EGLNativeWindowType: the window's OWNER describes it with two
 * functions, and EGL calls them.
 *
 *   misura   how big the drawing area is now, in pixels. Called when the
 *            surface is made and after every eglSwapBuffers: a size that
 *            changed makes the next frame of the new size.
 *   mostra   a finished frame, at eglSwapBuffers: `larghezza` x `altezza`
 *            pixels of 32 bits, 0xAARRGGBB (the ARGB of ex_pixmap and of a
 *            window's zone; the alpha is what GL drew there, and the
 *            server does not look at it), the TOP row first, `passo` BYTES
 *            from a row to the next. The owner puts them where they go and tells the server
 *            (WIN_MSG_AGGIORNA, or ex_pixmap + ex_update with the toolkit).
 *            The pixels belong to EGL and stay valid only during the call.
 *
 * ! WHY THE OWNER, AND NOT EGL TALKING TO THE SERVER ITSELF. The server sends
 * the window's news — "your zone is another one" after a resize
 * (WIN_MSG_MISURATA), the events — to the mailbox of the process, and the
 * process reads it in ITS loop: the toolkit's ex_get_message, Firefox's
 * nsAppShell. A library that read the same mailbox would steal messages from
 * it, and one that did not would draw into a zone that no longer exists. And
 * at 64 bit the toolkit is linked inside each program, so libEGL could not
 * call it either. It is the shape of Wayland's wl_egl_window, with the
 * functions in place of the fields.
 *
 * ! `versione` FIRST, AND CHECKED: a program built against a later form of
 * this structure gets EGL_BAD_NATIVE_WINDOW from an older libEGL, not a call
 * through a field that is not there. Fields are only ever added at the end.
 * ============================================================================= */
#ifndef EXOS_EGL_EXWIN_H
#define EXOS_EGL_EXWIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXOS_EGL_FINESTRA_VERSIONE  1u

struct exos_egl_finestra {
    unsigned int versione;          /* EXOS_EGL_FINESTRA_VERSIONE */
    void (*misura)(struct exos_egl_finestra *f,
                   unsigned int *larghezza, unsigned int *altezza);
    void (*mostra)(struct exos_egl_finestra *f, const unsigned int *argb,
                   unsigned int larghezza, unsigned int altezza,
                   unsigned int passo);
    void *dato;                     /* the owner's: EGL never touches it */
};

#ifdef __cplusplus
}
#endif

#endif /* EXOS_EGL_EXWIN_H */

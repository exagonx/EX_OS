/* =============================================================================
 * tools/mesa-exos/prova-finestra.c - OpenGL ES in an ExWin window (voce 88 c)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A client of the window server's protocol (<exos/win_proto.h>), as
 * Firefox's widget is: it asks for a 320x240 window, maps its zone, and gives
 * EGL the window as <exos/egl_exwin.h> wants it: `misura` says the size,
 * `mostra` copies a finished frame into the zone and sends WIN_MSG_AGGIORNA.
 * Then GLES 2 draws: a red triangle in the UPPER half on blue, 60 frames
 * with the triangle moving, the last one still.
 *
 *   1  the server, the window, its zone
 *   2  eglCreateWindowSurface with the structure, an ES2 context
 *   3  eglQuerySurface: EGL_WIDTH and EGL_HEIGHT are the window's
 *   4  60 frames: each eglSwapBuffers reaches mostra
 *   5  the zone after the last frame: red at a quarter of the height, blue
 *      at three quarters (the top row came first: not upside down)
 *   6  a size that changes (misura says 200x150): the next frame is 200x150
 *   7  a wrong structure (versione 99): EGL_BAD_NATIVE_WINDOW
 *   8  eglDestroy*, eglTerminate, the window destroyed
 *
 * The last line is "prova-finestra: N NO". Between 6 and 8 the window stays
 * on the screen for SECONDI seconds (argv[1], 20 by default), for the
 * photograph of tools/mesa-exos/prova-finestra.sh.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <exos/win_proto.h>
#include <exos/egl_exwin.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static int g_no = 0;
static void esito(int n, int ok, const char *cosa)
{
    printf("  [%s] %d %s\n", ok ? "OK" : "NO", n, cosa);
    if (!ok) g_no++;
}

/* --- the window: what an owner keeps ------------------------------------- */
static unsigned int g_server, g_id, g_w, g_h, g_passo;
static unsigned int *g_zona;
static unsigned int g_mostrate, g_ultima_w, g_ultima_h;
static unsigned int g_finta_w, g_finta_h;     /* check 6: another size */

static void misura(struct exos_egl_finestra *f, unsigned int *w, unsigned int *h)
{
    (void)f;
    *w = g_finta_w ? g_finta_w : g_w;
    *h = g_finta_h ? g_finta_h : g_h;
}

/* the frame into the zone, row by row (the zone's passo is the server's),
 * clipped to the zone, then "I have finished" to the server */
static void mostra(struct exos_egl_finestra *f, const unsigned int *argb,
                   unsigned int w, unsigned int h, unsigned int passo)
{
    unsigned int y, cw = w < g_w ? w : g_w, ch = h < g_h ? h : g_h;
    WinRegione u;

    (void)f;
    for (y = 0; y < ch; y++)
        memcpy((char *)g_zona + (size_t)y * g_passo,
               (const char *)argb + (size_t)y * passo, (size_t)cw * 4);
    u.id = g_id; u.x = 0; u.y = 0; u.larghezza = cw; u.altezza = ch;
    ipc_send(g_server, WIN_MSG_AGGIORNA, &u, sizeof(u));
    g_mostrate++;
    g_ultima_w = w; g_ultima_h = h;
}

static int apri_finestra(void)
{
    char nome[32];
    WinCrea c;
    WinCreata r;
    IpcMessage m;
    ShmZona z;
    int pid, i;

    win_nome_servizio(nome, sizeof(nome));
    pid = ipc_attendi(nome, 10000);
    if (pid <= 0) { printf("      nessun server '%s': ExWin e' acceso?\n", nome); return 0; }
    g_server = (unsigned int)pid;

    memset(&c, 0, sizeof(c));
    c.x = WIN_XY_AUTO; c.y = WIN_XY_AUTO; c.larghezza = 320; c.altezza = 240;
    c.stile = WIN_ST_VISIBILE | WIN_ST_TITOLO | WIN_ST_BORDO | WIN_ST_CHIUDI;
    strcpy(c.titolo, "OpenGL ES su ExWin");
    if (ipc_send(g_server, WIN_MSG_CREA, &c, sizeof(c)) < 0) return 0;
    for (i = 0; i < 50; i++) {
        memset(&r, 0, sizeof(r));
        if (ipc_recv_timeout(&m, &r, sizeof(r), 15000) < 0) return 0;
        if (m.tipo == WIN_MSG_CREATA) break;
    }
    if (m.tipo != WIN_MSG_CREATA || !r.id) { printf("      CREATA non arrivata\n"); return 0; }
    memset(&z, 0, sizeof(z));
    win_nome_zona(z.nome, r.id, r.giro);
    z.byte = r.byte;
    if (shm_apri(&z) != 0 || !z.virt) { printf("      la zona non si apre\n"); return 0; }
    g_id = r.id; g_w = r.larghezza; g_h = r.altezza; g_passo = r.passo;
    g_zona = (unsigned int *)(unsigned long)z.virt;
    printf("      finestra %u, %ux%u, passo %u\n", g_id, g_w, g_h, g_passo);
    return 1;
}

static GLuint shader(GLenum tipo, const char *testo)
{
    GLuint s = glCreateShader(tipo);
    GLint ok = 0;
    glShaderSource(s, 1, &testo, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

/* the zone's pixel, without the alpha */
static unsigned int zona(unsigned int x, unsigned int y)
{
    return g_zona[(size_t)y * (g_passo / 4) + x] & 0x00FFFFFFu;
}

int main(int argc, char **argv)
{
    struct exos_egl_finestra fin = { EXOS_EGL_FINESTRA_VERSIONE, misura, mostra, NULL };
    struct exos_egl_finestra sbagliata = { 99, misura, mostra, NULL };
    static const EGLint scelta[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_NONE };
    static const EGLint es2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    int secondi = argc > 1 ? atoi(argv[1]) : 20;
    EGLDisplay d;
    EGLConfig c;
    EGLSurface s = EGL_NO_SURFACE;
    EGLContext x = EGL_NO_CONTEXT;
    EGLint n = 0, w = 0, h = 0;
    GLuint prog = 0, vs, fs;
    GLint posto;
    int i;

    printf("prova-finestra: OpenGL ES in una finestra ExWin\n");
    esito(1, apri_finestra(), "il server, la finestra, la sua zona");
    if (g_no) { printf("prova-finestra: %d NO\n", g_no); return 1; }

    d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(d, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);
    /* each step says how it went: a failure names its step, not only "2" */
    {
        EGLBoolean scelto = eglChooseConfig(d, scelta, &c, 1, &n), corrente = EGL_FALSE;
        EGLint e_scelta = eglGetError(), e_sup = 0, e_ctx = 0, e_cor = 0;
        if (scelto && n == 1) {
            s = eglCreateWindowSurface(d, c, &fin, NULL);     e_sup = eglGetError();
            x = eglCreateContext(d, c, EGL_NO_CONTEXT, es2);  e_ctx = eglGetError();
            if (s != EGL_NO_SURFACE && x != EGL_NO_CONTEXT) {
                corrente = eglMakeCurrent(d, s, s, x);         e_cor = eglGetError();
            }
        }
        esito(2, corrente, "eglCreateWindowSurface con la struttura, contesto ES2");
        if (g_no) {
            printf("      eglChooseConfig %d (%d config, 0x%x), superficie %s (0x%x), "
                   "contesto %s (0x%x), eglMakeCurrent %d (0x%x)\nprova-finestra: %d NO\n",
                   scelto, n, e_scelta, s != EGL_NO_SURFACE ? "si'" : "no", e_sup,
                   x != EGL_NO_CONTEXT ? "si'" : "no", e_ctx, corrente, e_cor, g_no);
            return 1;
        }
    }

    eglQuerySurface(d, s, EGL_WIDTH, &w);
    eglQuerySurface(d, s, EGL_HEIGHT, &h);
    printf("      superficie %dx%d\n", w, h);
    esito(3, (unsigned)w == g_w && (unsigned)h == g_h, "EGL_WIDTH e EGL_HEIGHT sono della finestra");

    vs = shader(GL_VERTEX_SHADER,
        "attribute vec2 p;\nuniform float dx;\nvoid main() { gl_Position = vec4(p.x + dx, p.y, 0.0, 1.0); }\n");
    fs = shader(GL_FRAGMENT_SHADER,
        "precision mediump float;\nvoid main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n");
    if (vs && fs) {
        GLint ok = 0;
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glBindAttribLocation(prog, 0, "p");
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) prog = 0;
    }
    {
        /* the upper half: y from 0.1 to 0.9 (GL's y goes up) */
        static const GLfloat tri[] = { -0.6f, 0.1f, 0.6f, 0.1f, 0.0f, 0.9f };
        glUseProgram(prog);
        posto = glGetUniformLocation(prog, "dx");
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
        glEnableVertexAttribArray(0);
    }
    for (i = 0; i < 60; i++) {
        float dx = i < 59 ? (float)((i % 20) - 10) / 40.0f : 0.0f;
        glViewport(0, 0, w, h);
        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(posto, dx);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        eglSwapBuffers(d, s);
    }
    printf("      %u fotogrammi mostrati\n", g_mostrate);
    esito(4, prog && g_mostrate == 60, "60 eglSwapBuffers arrivano a mostra");

    {
        /* NDC y 0.5 is a quarter from the top; the bottom half is all blue */
        unsigned int su = zona(g_w / 2, g_h / 4), giu = zona(g_w / 2, g_h * 3 / 4);
        printf("      zona: (%u,%u) = %06x, (%u,%u) = %06x\n", g_w / 2, g_h / 4, su, g_w / 2, g_h * 3 / 4, giu);
        esito(5, su == 0xFF0000u && giu == 0x0000FFu, "la zona: rosso in alto, blu in basso (non capovolta)");
    }

    /* 6: the owner now says 200x150 (as after a resize) */
    g_finta_w = 200; g_finta_h = 150;
    eglSwapBuffers(d, s);          /* this one still at the old size; misura is read after it */
    glViewport(0, 0, 200, 150);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    eglSwapBuffers(d, s);
    eglQuerySurface(d, s, EGL_WIDTH, &w);
    eglQuerySurface(d, s, EGL_HEIGHT, &h);
    printf("      dopo il cambio: fotogramma %ux%u, superficie %dx%d\n", g_ultima_w, g_ultima_h, w, h);
    esito(6, g_ultima_w == 200 && g_ultima_h == 150 && w == 200 && h == 150,
          "la misura cambiata: il fotogramma dopo e' 200x150");
    g_finta_w = g_finta_h = 0;
    glViewport(0, 0, (GLsizei)g_w, (GLsizei)g_h);
    eglSwapBuffers(d, s);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(posto, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    eglSwapBuffers(d, s);

    esito(7, eglCreateWindowSurface(d, c, &sbagliata, NULL) == EGL_NO_SURFACE &&
             eglGetError() == EGL_BAD_NATIVE_WINDOW, "versione 99: EGL_BAD_NATIVE_WINDOW");

    printf("      la finestra resta %d secondi\n", secondi);
    fflush(stdout);
    sleep((unsigned)secondi);

    {
        WinRegione u = { g_id, 0, 0, 0, 0 };
        int ok = eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        ok = eglDestroyContext(d, x) && eglDestroySurface(d, s) && ok;
        ok = eglTerminate(d) && ok;
        ok = ipc_send(g_server, WIN_MSG_DISTRUGGI, &u, sizeof(u)) == 0 && ok;
        esito(8, ok, "eglDestroy*, eglTerminate, la finestra distrutta");
    }
    printf("prova-finestra: %d NO\n", g_no);
    return g_no ? 1 : 0;
}

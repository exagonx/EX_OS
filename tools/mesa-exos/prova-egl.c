/* =============================================================================
 * tools/mesa-exos/prova-egl.c - OpenGL ES 2 through EGL on EX-OS (voce 88)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What WebGL needs, in small: EGL on the default display (the surfaceless
 * platform of Mesa's "exos" driver), a 64x64 pbuffer, a GLES 2 context, a
 * shader program, a red triangle on blue, glReadPixels. Each check prints
 * [OK] or [NO]; the last line is "prova-egl: N NO".
 *
 *   1  eglGetDisplay, eglInitialize (prints the EGL version and vendor)
 *   2  eglChooseConfig with a pbuffer and ES2
 *   3  eglCreatePbufferSurface 64x64, eglCreateContext ES2, eglMakeCurrent
 *   4  GL_VENDOR, GL_RENDERER, GL_VERSION (printed)
 *   5  glClear to blue: the corner pixel is blue
 *   6  a vertex and a fragment shader compile and link
 *   7  the triangle: the centre is red, a corner is still blue
 *   8  eglSwapBuffers on the pbuffer, eglDestroy*, eglTerminate
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static int g_no = 0;
static void esito(int n, int ok, const char *cosa)
{
    printf("  [%s] %d %s\n", ok ? "OK" : "NO", n, cosa);
    if (!ok) g_no++;
}

static GLuint shader(GLenum tipo, const char *testo)
{
    GLuint s = glCreateShader(tipo);
    GLint ok = 0;
    glShaderSource(s, 1, &testo, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("      shader: %s\n", log);
        return 0;
    }
    return s;
}

static int pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    unsigned char p[4] = { 0, 0, 0, 0 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    if (p[0] != r || p[1] != g || p[2] != b) {
        printf("      (%d,%d) = %u %u %u, atteso %u %u %u\n", x, y, p[0], p[1], p[2], r, g, b);
        return 0;
    }
    return 1;
}

int main(void)
{
    EGLDisplay d;
    EGLConfig  c;
    EGLSurface s = EGL_NO_SURFACE;
    EGLContext x = EGL_NO_CONTEXT;
    EGLint     maj = 0, min = 0, n = 0;
    GLuint     prog = 0;
    static const EGLint scelta[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_NONE };
    static const EGLint misura[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    static const EGLint es2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

    printf("prova-egl: OpenGL ES 2 su EGL\n");

    d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    esito(1, d != EGL_NO_DISPLAY && eglInitialize(d, &maj, &min), "eglInitialize");
    if (d != EGL_NO_DISPLAY)
        printf("      EGL %d.%d, %s\n", maj, min, eglQueryString(d, EGL_VENDOR));

    esito(2, eglChooseConfig(d, scelta, &c, 1, &n) && n == 1, "eglChooseConfig pbuffer ES2");
    if (n == 1) {
        s = eglCreatePbufferSurface(d, c, misura);
        eglBindAPI(EGL_OPENGL_ES_API);
        x = eglCreateContext(d, c, EGL_NO_CONTEXT, es2);
    }
    esito(3, s != EGL_NO_SURFACE && x != EGL_NO_CONTEXT && eglMakeCurrent(d, s, s, x),
          "pbuffer 64x64, contesto ES2, eglMakeCurrent");
    if (g_no) { printf("      eglGetError 0x%x\n", eglGetError()); printf("prova-egl: %d NO\n", g_no); return 1; }

    {
        const char *v = (const char *)glGetString(GL_VENDOR);
        const char *r = (const char *)glGetString(GL_RENDERER);
        const char *e = (const char *)glGetString(GL_VERSION);
        esito(4, v && r && e, "glGetString");
        printf("      %s | %s | %s\n", v ? v : "?", r ? r : "?", e ? e : "?");
    }

    glViewport(0, 0, 64, 64);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    esito(5, pixel(1, 1, 0, 0, 255), "glClear blu");

    {
        GLuint vs = shader(GL_VERTEX_SHADER,
            "attribute vec2 p;\nvoid main() { gl_Position = vec4(p, 0.0, 1.0); }\n");
        GLuint fs = shader(GL_FRAGMENT_SHADER,
            "precision mediump float;\nvoid main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n");
        GLint ok = 0;
        if (vs && fs) {
            prog = glCreateProgram();
            glAttachShader(prog, vs);
            glAttachShader(prog, fs);
            glBindAttribLocation(prog, 0, "p");
            glLinkProgram(prog);
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        }
        esito(6, ok, "shader compilati e collegati");
    }

    if (prog) {
        static const GLfloat tri[] = { -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f };
        glUseProgram(prog);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        esito(7, pixel(32, 28, 255, 0, 0) && pixel(1, 62, 0, 0, 255), "triangolo rosso, angolo blu");
    } else esito(7, 0, "triangolo: niente programma");

    {
        int ok = eglSwapBuffers(d, s);
        ok = eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) && ok;
        ok = eglDestroyContext(d, x) && eglDestroySurface(d, s) && ok;
        ok = eglTerminate(d) && ok;
        esito(8, ok, "eglSwapBuffers, eglDestroy*, eglTerminate");
    }

    printf("prova-egl: %d NO\n", g_no);
    return g_no ? 1 : 0;
}

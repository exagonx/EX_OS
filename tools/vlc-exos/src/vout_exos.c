/*****************************************************************************
 * vout_exos.c: EX-OS video output for VLC, a window of ExWin
 *****************************************************************************
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * The window server (drivers/wserver/win_proto.h) gives a window as a shared
 * memory zone of 32-bit ARGB pixels: VLC converts and scales every picture
 * to RGB32 at the size chosen here, the picture is copied into the zone and
 * WIN_MSG_AGGIORNA says it is ready. The events of the window come to the
 * thread that created it, the video output thread, and are read without
 * waiting before each picture.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <libc.h>
#include <exos/win_proto.h>
#include <exos/kbd_proto.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>
#include <vlc_actions.h>

#define ATTESA_MS 15000

struct vout_display_sys_t
{
    unsigned int server, id, giro;
    uint32_t *zona;
    unsigned int zona_w, zona_h, passo;
    unsigned int x, y;          /* where the picture goes in the zone */
    picture_pool_t *pool;
};

static int SoloCreata(const IpcMessage *m, void *dato)
{
    unsigned int server = *(const unsigned int *)dato;
    if (m->sender_pid != server)
        return IPC_ALTRUI;
    return m->tipo == WIN_MSG_CREATA ? IPC_MIO : IPC_ALTRUI;
}

static int DalServer(const IpcMessage *m, void *dato)
{
    unsigned int server = *(const unsigned int *)dato;
    return m->sender_pid == server ? IPC_MIO : IPC_ALTRUI;
}

static int Attacca(vout_display_sys_t *sys, const WinCreata *r)
{
    ShmZona z;

    memset(&z, 0, sizeof (z));
    win_nome_zona(z.nome, r->id, r->giro);
    z.byte = r->byte;
    if (shm_apri(&z) != 0 || !z.virt)
        return -1;
    if (sys->zona != NULL)
        shm_chiudi(sys->zona);
    sys->zona = (uint32_t *)(uintptr_t)z.virt;
    sys->zona_w = r->larghezza;
    sys->zona_h = r->altezza;
    sys->passo = r->passo ? r->passo : r->larghezza * 4;
    sys->giro = r->giro;
    memset(sys->zona, 0, (size_t)sys->passo * sys->zona_h);
    return 0;
}

static void Centra(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;
    sys->x = sys->zona_w > vd->fmt.i_visible_width
           ? (sys->zona_w - vd->fmt.i_visible_width) / 2 : 0;
    sys->y = sys->zona_h > vd->fmt.i_visible_height
           ? (sys->zona_h - vd->fmt.i_visible_height) / 2 : 0;
}

static int Tasto(unsigned int t)
{
    unsigned int c = t & KBD_KEY_MASK;
    int k;

    switch (c)
    {
        case KBD_K_UP:    k = KEY_UP; break;
        case KBD_K_DOWN:  k = KEY_DOWN; break;
        case KBD_K_LEFT:  k = KEY_LEFT; break;
        case KBD_K_RIGHT: k = KEY_RIGHT; break;
        case KBD_K_HOME:  k = KEY_HOME; break;
        case KBD_K_END:   k = KEY_END; break;
        case KBD_K_PGUP:  k = KEY_PAGEUP; break;
        case KBD_K_PGDN:  k = KEY_PAGEDOWN; break;
        case KBD_K_DEL:   k = KEY_DELETE; break;
        case 27:          k = KEY_ESC; break;
        case '\r': case '\n': k = KEY_ENTER; break;
        default:
            if (c < 32 || c > 126)
                return 0;
            k = (int)c;
    }
    if (t & KBD_MOD_SHIFT) k |= KEY_MODIFIER_SHIFT;
    if (t & KBD_MOD_CTRL)  k |= KEY_MODIFIER_CTRL;
    if (t & KBD_MOD_ALT)   k |= KEY_MODIFIER_ALT;
    return k;
}

static void Posta(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;
    IpcMessage meta;
    unsigned char buf[IPC_MSG_MAX_DATA];

    for (int giri = 0; giri < 32; giri++)
    {
        if (ipc_scegli(DalServer, &sys->server, &meta, buf, sizeof (buf),
                       IPC_SUBITO) < 0)
            return;
        if (meta.tipo == WIN_MSG_MISURATA && meta.len >= sizeof (WinCreata))
        {
            const WinCreata *r = (const WinCreata *)buf;
            if (r->id == sys->id && r->giro != sys->giro && Attacca(sys, r) == 0)
            {
                WinRegione u = { sys->id, 0, 0, r->larghezza, r->altezza };
                ipc_send(sys->server, WIN_MSG_AGGIORNA, &u, sizeof (u));
                Centra(vd);
            }
        }
        else if (meta.tipo == WIN_MSG_EVENTO && meta.len >= sizeof (WinEvento))
        {
            const WinEvento *e = (const WinEvento *)buf;
            if (e->id != sys->id)
                continue;
            switch (e->tipo)
            {
                case WIN_EV_CHIUDI:
                    vout_display_SendEventClose(vd);
                    break;
                case WIN_EV_TASTO:
                {
                    int k = Tasto(e->tasto);
                    if (k)
                        vout_display_SendEventKey(vd, k);
                    break;
                }
                case WIN_EV_MOUSE_GIU:
                    vout_display_SendEventMousePressed(vd, MOUSE_BUTTON_LEFT);
                    break;
                case WIN_EV_MOUSE_SU:
                    vout_display_SendEventMouseReleased(vd, MOUSE_BUTTON_LEFT);
                    break;
                default:
                    break;
            }
        }
    }
}

static picture_pool_t *Pool(vout_display_t *vd, unsigned count)
{
    vout_display_sys_t *sys = vd->sys;
    if (sys->pool == NULL)
        sys->pool = picture_pool_NewFromFormat(&vd->fmt, count);
    return sys->pool;
}

static void Display(vout_display_t *vd, picture_t *pic, subpicture_t *sub)
{
    vout_display_sys_t *sys = vd->sys;
    VLC_UNUSED(sub);

    Posta(vd);
    if (sys->zona != NULL)
    {
        const plane_t *p = &pic->p[0];
        unsigned int w = vd->fmt.i_visible_width, h = vd->fmt.i_visible_height;
        if (sys->x + w > sys->zona_w) w = sys->zona_w - sys->x;
        if (sys->y + h > sys->zona_h) h = sys->zona_h - sys->y;
        const uint8_t *src = p->p_pixels
                           + vd->fmt.i_y_offset * p->i_pitch
                           + vd->fmt.i_x_offset * 4;
        uint8_t *dst = (uint8_t *)sys->zona + (size_t)sys->y * sys->passo
                     + sys->x * 4;
        for (unsigned int r = 0; r < h; r++)
        {
            /* RGB32 with the alpha byte left as it comes: make it opaque */
            uint32_t *d = (uint32_t *)dst;
            const uint32_t *s = (const uint32_t *)src;
            for (unsigned int c = 0; c < w; c++)
                d[c] = s[c] | 0xFF000000u;
            src += p->i_pitch;
            dst += sys->passo;
        }
        WinRegione u = { sys->id, 0, 0, sys->zona_w, sys->zona_h };
        ipc_send(sys->server, WIN_MSG_AGGIORNA, &u, sizeof (u));
    }
    picture_Release(pic);
}

static void Manage(vout_display_t *vd)
{
    Posta(vd);
}

static int Control(vout_display_t *vd, int query, va_list args)
{
    VLC_UNUSED(vd); VLC_UNUSED(args);
    switch (query)
    {
        case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
        case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
        case VOUT_DISPLAY_CHANGE_ZOOM:
        case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
        case VOUT_DISPLAY_CHANGE_SOURCE_CROP:
            return VLC_SUCCESS;
        default:
            return VLC_EGENERIC;
    }
}

static int Open(vlc_object_t *obj)
{
    vout_display_t *vd = (vout_display_t *)obj;
    vout_display_sys_t *sys;
    char nome[32];
    int pid;

    win_nome_servizio(nome, sizeof (nome));
    pid = ipc_lookup(nome);
    if (pid <= 0)
    {
        msg_Dbg(vd, "no window server (%s): ExWin is not running", nome);
        return VLC_EGENERIC;
    }
    sys = calloc(1, sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;
    vd->sys = sys;
    sys->server = (unsigned int)pid;

    /* the size the picture has on screen, with its aspect ratio */
    video_format_t src = vd->source;
    unsigned int w = src.i_visible_width, h = src.i_visible_height;
    if (src.i_sar_num && src.i_sar_den && src.i_sar_num != src.i_sar_den)
        w = (unsigned int)((uint64_t)w * src.i_sar_num / src.i_sar_den);
    if (w == 0 || h == 0)
        goto fallito;

    WinCrea c;
    memset(&c, 0, sizeof (c));
    c.x = WIN_XY_AUTO;
    c.y = WIN_XY_AUTO;
    c.larghezza = w;
    c.altezza = h;
    c.stile = WIN_ST_VISIBILE | WIN_ST_TITOLO | WIN_ST_BORDO | WIN_ST_CHIUDI
            | WIN_ST_RIDIM;
    {
        char *t = var_InheritString(vd, "video-title");
        strncpy(c.titolo, t ? t : "VLC", WIN_TITOLO_LEN - 1);
        free(t);
    }
    if (ipc_send(sys->server, WIN_MSG_CREA, &c, sizeof (c)) < 0)
        goto fallito;

    IpcMessage meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    if (ipc_scegli(SoloCreata, &sys->server, &meta, buf, sizeof (buf),
                   ATTESA_MS) < 0 || meta.len < sizeof (WinCreata))
        goto fallito;
    WinCreata r;
    memcpy(&r, buf, sizeof (r));
    if (r.id == 0)
        goto fallito;
    sys->id = r.id;
    if (Attacca(sys, &r) != 0)
    {
        WinRegione d = { r.id, 0, 0, 0, 0 };
        ipc_send(sys->server, WIN_MSG_DISTRUGGI, &d, sizeof (d));
        sys->id = 0;
        goto fallito;
    }

    /* the server may grant a smaller window: fit the picture in it */
    if (w > sys->zona_w || h > sys->zona_h)
    {
        if ((uint64_t)w * sys->zona_h > (uint64_t)h * sys->zona_w)
        {
            h = (unsigned int)((uint64_t)h * sys->zona_w / w);
            w = sys->zona_w;
        }
        else
        {
            w = (unsigned int)((uint64_t)w * sys->zona_h / h);
            h = sys->zona_h;
        }
    }
    w &= ~1u;
    h &= ~1u;

    video_format_t fmt = vd->fmt;
    fmt.i_chroma = VLC_CODEC_RGB32;
    fmt.i_rmask = 0x00ff0000;
    fmt.i_gmask = 0x0000ff00;
    fmt.i_bmask = 0x000000ff;
    fmt.i_width = fmt.i_visible_width = w;
    fmt.i_height = fmt.i_visible_height = h;
    fmt.i_x_offset = fmt.i_y_offset = 0;
    fmt.i_sar_num = fmt.i_sar_den = 1;
    vd->fmt = fmt;
    Centra(vd);

    vd->pool = Pool;
    vd->prepare = NULL;
    vd->display = Display;
    vd->control = Control;
    vd->manage = Manage;
    vout_display_DeleteWindow(vd, NULL);
    msg_Dbg(vd, "window %u, %ux%u, picture %ux%u", sys->id, sys->zona_w,
            sys->zona_h, w, h);
    return VLC_SUCCESS;

fallito:
    msg_Err(vd, "the window server did not give a window");
    free(sys);
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *obj)
{
    vout_display_t *vd = (vout_display_t *)obj;
    vout_display_sys_t *sys = vd->sys;

    if (sys->pool != NULL)
        picture_pool_Release(sys->pool);
    if (sys->id)
    {
        WinRegione d = { sys->id, 0, 0, 0, 0 };
        ipc_send(sys->server, WIN_MSG_DISTRUGGI, &d, sizeof (d));
    }
    if (sys->zona != NULL)
        shm_chiudi(sys->zona);
    free(sys);
}

vlc_module_begin ()
    set_shortname("ExWin")
    set_description("EX-OS video output (ExWin)")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout display", 200)
    add_shortcut("exos", "exwin")
    set_callbacks(Open, Close)
vlc_module_end ()

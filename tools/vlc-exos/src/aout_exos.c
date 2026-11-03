/*****************************************************************************
 * aout_exos.c: EX-OS audio output for VLC
 *****************************************************************************
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * The "audio" service of drivers/audio/audio_proto.h: the samples go through
 * a ring in shared memory, IPC only opens, starts, stops and closes it. The
 * same protocol as Firefox's cubeb backend (media/libcubeb/src/cubeb_exos.c).
 * The card plays 16-bit samples; VLC converts and resamples to what the card
 * grants.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <libc.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>

/* From drivers/audio/audio_proto.h, the driver's ABI */
#define AUDIO_SERVIZIO   "audio"
#define AUDIO_MSG_APRI   0x0A02
#define AUDIO_MSG_VIA    0x0A03
#define AUDIO_MSG_FERMA  0x0A04
#define AUDIO_MSG_CHIUDI 0x0A05
#define AUDIO_MSG_ESITO  0x0A82

typedef struct { unsigned int rate, canali, bit; } AudioFormato;
typedef struct {
    int esito;
    AudioFormato formato;
    char zona[16];
    unsigned int zona_byte, anello_byte, dati_off;
} AudioEsito;
typedef struct {
    unsigned int magia, byte, dati_off, rate, canali, bit;
    volatile unsigned int scritto;
    volatile unsigned int suonato;
    unsigned int sottoflussi, stato;
} AudioAnello;

#define ATTESA_MS 2000
#define PASSO_US  5000
#define RITARDO_MS 250  /* sound queued ahead, at most */

struct aout_sys_t
{
    int pid;
    void *zona;
    AudioAnello *anello;
    unsigned char *dati;
    unsigned int rate, byte_frame;
    unsigned int limite;    /* bytes queued at most: RITARDO_MS */
    unsigned int meta;      /* the card's half buffer in bytes, 0 = not known */
    unsigned int via_pieno; /* bytes in the ring when VIA was sent */
    unsigned int visto;     /* `suonato` when last seen changing ... */
    mtime_t visto_quando;   /* ... and when */
    bool partito, in_pausa, pieno_detto;
};

static int ChiediApri(int pid, AudioFormato *f, AudioEsito *e)
{
    IpcMessage meta;
    unsigned char buf[IPC_MSG_MAX_DATA];

    if (ipc_send((unsigned)pid, AUDIO_MSG_APRI, f, sizeof (*f)) < 0)
        return -1;
    for (int giri = 0; giri < 8; giri++)
    {
        if (ipc_recv_timeout(&meta, buf, sizeof (buf), ATTESA_MS) < 0)
            return -1;
        if ((int)meta.sender_pid != pid || meta.tipo != AUDIO_MSG_ESITO)
            continue;
        if (meta.len < sizeof (*e))
            return -1;
        memcpy(e, buf, sizeof (*e));
        return 0;
    }
    return -1;
}

static int Apri(audio_output_t *aout, unsigned rate, unsigned canali)
{
    aout_sys_t *sys = aout->sys;
    AudioFormato f = { rate, canali, 16 };
    AudioEsito e;
    ShmZona z;

    if (ChiediApri(sys->pid, &f, &e) != 0 || e.esito != 0)
    {
        msg_Err(aout, "the card does not open %u Hz, %u channels "
                "(another program playing?)", rate, canali);
        return VLC_EGENERIC;
    }
    if (e.formato.bit != 16 || e.formato.canali < 1 || e.formato.canali > 2
     || e.formato.rate == 0)
    {
        ipc_send((unsigned)sys->pid, AUDIO_MSG_CHIUDI, 0, 0);
        return VLC_EGENERIC;
    }
    memset(&z, 0, sizeof (z));
    strcpy(z.nome, e.zona);
    z.byte = e.zona_byte;
    if (shm_apri(&z) < 0)
    {
        msg_Err(aout, "ring %s not mapped", z.nome);
        ipc_send((unsigned)sys->pid, AUDIO_MSG_CHIUDI, 0, 0);
        return VLC_EGENERIC;
    }
    sys->zona = (void *)(uintptr_t)z.virt;
    sys->anello = (AudioAnello *)(uintptr_t)z.virt;
    sys->dati = (unsigned char *)(uintptr_t)z.virt + sys->anello->dati_off;
    sys->rate = e.formato.rate;
    sys->byte_frame = 2 * e.formato.canali;
    sys->limite = sys->rate * sys->byte_frame / 1000 * RITARDO_MS;
    sys->limite -= sys->limite % sys->byte_frame;
    if (sys->limite > sys->anello->byte || sys->limite == 0)
        sys->limite = sys->anello->byte;
    sys->meta = 0;
    sys->visto = 0;
    sys->partito = false;
    return VLC_SUCCESS;
}

static void Chiudi(audio_output_t *aout)
{
    aout_sys_t *sys = aout->sys;

    ipc_send((unsigned)sys->pid, AUDIO_MSG_CHIUDI, 0, 0);
    if (sys->zona != NULL)
        shm_chiudi(sys->zona);
    sys->zona = NULL;
    sys->anello = NULL;
}

/* ! `suonato` IS WHAT THE DRIVER HAS TAKEN, NOT WHAT HAS BEEN HEARD. At VIA
 * it copies two halves of its DMA buffer at once, then one half at every
 * interrupt: between one and two halves are always in the card and not yet
 * heard. How big a half is depends on the driver and the rate, and nothing
 * in the protocol says it: the first jump of `suonato` after VIA is two
 * halves, when the ring had that much. */
static void Misura(aout_sys_t *sys)
{
    unsigned int s = sys->anello->suonato;

    if (s != sys->visto)
    {
        sys->visto = s;
        sys->visto_quando = mdate();
    }
    if (sys->meta == 0 && s > 0 && s < sys->via_pieno)
        sys->meta = s / 2;
}

/* What the card holds and has not played: two halves right after the driver
 * took one (an interrupt, or VIA), then less and less until the next
 * interrupt, one half. Counting it as a fixed 1.5 halves made the delay jump
 * by a half at every interrupt, and VLC answered with resampling, silence
 * and flushes. */
static unsigned int NellaScheda(aout_sys_t *sys)
{
    if (sys->meta == 0 || sys->visto == 0)
        return 0;
    mtime_t passati = mdate() - sys->visto_quando;
    uint64_t suonati = (uint64_t)passati * sys->rate / CLOCK_FREQ * sys->byte_frame;
    if (suonati > sys->meta)
        suonati = sys->meta;
    return 2 * sys->meta - (unsigned int)suonati;
}

static int TimeGet(audio_output_t *aout, mtime_t *delay)
{
    aout_sys_t *sys = aout->sys;
    unsigned int pieno;

    if (sys->anello == NULL || !sys->partito)
        return -1;
    Misura(sys);
    pieno = sys->anello->scritto - sys->anello->suonato + NellaScheda(sys);
    *delay = (mtime_t)(pieno / sys->byte_frame) * CLOCK_FREQ / sys->rate;
    return 0;
}

static void Via(aout_sys_t *sys)
{
    if (!sys->partito && !sys->in_pausa)
    {
        sys->via_pieno = sys->anello->scritto - sys->anello->suonato;
        ipc_send((unsigned)sys->pid, AUDIO_MSG_VIA, 0, 0);
        sys->partito = true;
    }
}

static void Play(audio_output_t *aout, block_t *block)
{
    aout_sys_t *sys = aout->sys;
    AudioAnello *a = sys->anello;
    const unsigned char *src = block->p_buffer;
    unsigned int n = block->i_buffer;

    while (a != NULL && n > 0)
    {
        unsigned int libero = sys->limite - (a->scritto - a->suonato);
        if ((int)libero < 0)
            libero = 0;
        if (libero < sys->byte_frame)
        {
            {
                static mtime_t ultimo;
                mtime_t ora = mdate();
                if (ora - ultimo > CLOCK_FREQ / 2)
                {
                    msg_Dbg(aout, "clock %"PRId64" ms: played %u bytes (%u Hz x %u)",
                            ora / 1000, a->suonato, sys->rate, sys->byte_frame);
                    ultimo = ora;
                }
            }
            if (!sys->pieno_detto)
                msg_Dbg(aout, "ring full: written %u, played %u, %s",
                        a->scritto, a->suonato, sys->partito ? "playing" : "not started");
            sys->pieno_detto = true;
            /* ! THE CARD STARTS WHEN THE RING IS FULL, NOT BEFORE: at VIA the
             * driver takes two halves of its buffer at once and pads with
             * silence what is missing, silence that `suonato` does not count
             * and that would put the clock behind for good. RITARDO_MS is
             * more than two halves for every driver of audio_comune.c. */
            Via(sys);
            usleep(PASSO_US);
            continue;
        }
        sys->pieno_detto = false;
        unsigned int k = n < libero ? n : libero;
        k -= k % sys->byte_frame;
        unsigned int testa = a->scritto & (a->byte - 1);
        unsigned int tratto = a->byte - testa;
        if (tratto > k)
            tratto = k;
        memcpy(sys->dati + testa, src, tratto);
        if (k > tratto)
            memcpy(sys->dati, src + tratto, k - tratto);
        __sync_synchronize();
        a->scritto += k;    /* samples first, counter after */
        src += k;
        n -= k;
    }
    block_Release(block);
}

static void Pause(audio_output_t *aout, bool pause, mtime_t date)
{
    aout_sys_t *sys = aout->sys;
    (void) date;

    sys->in_pausa = pause;
    if (pause)
    {
        if (sys->partito)
            ipc_send((unsigned)sys->pid, AUDIO_MSG_FERMA, 0, 0);
        sys->partito = false;
    }
    else if (sys->anello != NULL
          && sys->anello->scritto != sys->anello->suonato)
        Via(sys);
}

static void Flush(audio_output_t *aout, bool wait)
{
    aout_sys_t *sys = aout->sys;

    if (sys->anello == NULL)
        return;
    msg_Dbg(aout, "flush%s: written %u, played %u", wait ? " (drain)" : "",
            sys->anello->scritto, sys->anello->suonato);
    if (wait)
    {
        Via(sys);
        for (int i = 0; i < 2000 && sys->anello->scritto != sys->anello->suonato; i++)
            usleep(PASSO_US);
        msg_Dbg(aout, "drained: written %u, played %u",
                sys->anello->scritto, sys->anello->suonato);
        return;
    }
    /* ! THE RING HAS NO "DISCARD": only the driver moves `suonato`. A fresh
     * ring is the only way to drop what is queued (a seek). */
    unsigned rate = sys->rate, canali = sys->byte_frame / 2;
    Chiudi(aout);
    if (Apri(aout, rate, canali) != VLC_SUCCESS)
        msg_Err(aout, "could not reopen the card after a flush");
}

static int Start(audio_output_t *aout, audio_sample_format_t *restrict fmt)
{
    aout_sys_t *sys = aout->sys;

    if (aout_FormatNbChannels(fmt) == 0 || AOUT_FMT_SPDIF(fmt)
     || AOUT_FMT_HDMI(fmt))
        return VLC_EGENERIC;
    sys->pid = ipc_lookup(AUDIO_SERVIZIO);
    if (sys->pid <= 0)
    {
        msg_Err(aout, "no '%s' service: no card, or `audio -i` not run",
                AUDIO_SERVIZIO);
        return VLC_EGENERIC;
    }
    sys->in_pausa = false;
    /* ! 48000 HZ STEREO WHATEVER THE FILE IS: what every card plays, and
     * VLC resamples better than an AC'97 codec with VRA (8000 Hz in QEMU
     * kept a clock that VLC flushed every half second, 3 November 2026). */
    if (Apri(aout, 48000, 2) != VLC_SUCCESS)
        return VLC_EGENERIC;

    fmt->i_format = VLC_CODEC_S16N;
    fmt->i_rate = sys->rate;
    fmt->i_physical_channels = sys->byte_frame == 4 ? AOUT_CHANS_STEREO
                                                    : AOUT_CHAN_CENTER;
    fmt->channel_type = AUDIO_CHANNEL_TYPE_BITMAP;
    aout_FormatPrepare(fmt);
    msg_Dbg(aout, "card at %u Hz, %u channels", sys->rate, sys->byte_frame / 2);
    return VLC_SUCCESS;
}

static void Stop(audio_output_t *aout)
{
    Chiudi(aout);
}

static int Open(vlc_object_t *obj)
{
    audio_output_t *aout = (audio_output_t *)obj;
    aout_sys_t *sys = calloc(1, sizeof (*sys));

    if (unlikely(sys == NULL))
        return VLC_ENOMEM;
    aout->sys = sys;
    aout->start = Start;
    aout->stop = Stop;
    aout->time_get = TimeGet;
    aout->play = Play;
    aout->pause = Pause;
    aout->flush = Flush;
    return VLC_SUCCESS;
}

static void Close(vlc_object_t *obj)
{
    audio_output_t *aout = (audio_output_t *)obj;
    free(aout->sys);
}

vlc_module_begin ()
    set_shortname("EX-OS")
    set_description("EX-OS audio output")
    set_category(CAT_AUDIO)
    set_subcategory(SUBCAT_AUDIO_AOUT)
    set_capability("audio output", 200)
    add_shortcut("exos")
    set_callbacks(Open, Close)
vlc_module_end ()

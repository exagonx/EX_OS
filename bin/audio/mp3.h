/* bin/audio/mp3.h — il decodificatore MP3 di `audio`: vedi mp3.c.
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef AUDIO_MP3_H
#define AUDIO_MP3_H

/* Il pezzo piu' grande che mp3_prossimo() puo' rendere: 1152 campioni per
 * canale, due canali, due byte l'uno. */
#define MP3_PEZZO_BYTE (1152 * 2 * 2)

int          mp3_apri(const char *percorso, unsigned int *hz, unsigned int *canali);
unsigned int mp3_prossimo(short *pcm);
void         mp3_chiudi(void);

#endif

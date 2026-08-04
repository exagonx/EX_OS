/* =============================================================================
 * kernel/fs/cfg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Parser per /boot/kernel.cfg in formato INI.
 *
 * Formato supportato:
 *   # commento (riga intera)
 *   [sezione]
 *   chiave = valore      (spazi attorno a = ignorati)
 *   chiave=valore        (forma compatta)
 *
 * Valori letti e applicati:
 *   [kernel] loglevel, timer_hz
 *   [env]    PATH, HOME, TERM, OSNAME, OSVER e qualsiasi altra variabile
 *   [boot]   shell, modules
 *   [modules] nome=percorso
 * ============================================================================= */

#include "kernel.h"
#include "cfg.h"
#include "fat12.h"
#include "vfs.h"
#include "version.h"   /* EXOS_*: identità iniettata in [env], vedi cfg_load */

/* Definite più in basso, usate da cfg_load: iniettano OSNAME/OSVER/AUTHOR
 * in [env] a partire da version.h. */
static void cfg_env_set_identity(KernelConfig *cfg, const char *key,
                                  const char *value);
static void cfg_apply_identity(KernelConfig *cfg);

/* =============================================================================
 * Stato configurazione globale
 * ============================================================================= */
static KernelConfig g_config;
static int          g_config_loaded = 0;

/* =============================================================================
 * Helper stringhe (senza libc)
 * ============================================================================= */

static uint32_t cfg_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && *s++) n++;
    return n;
}

static void cfg_strcpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    if (!src || !dst || max == 0) return;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int cfg_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int __attribute__((unused)) cfg_strncmp(const char *a, const char *b, uint32_t n)
{
    while (n-- && *a && *b && *a == *b) { a++; b++; }
    return n == (uint32_t)-1 ? 0 : ((unsigned char)*a - (unsigned char)*b);
}

/* Converte stringa in intero (solo decimale positivo) */
static uint32_t cfg_atoi(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

/* Rimuove spazi iniziali e finali in-place, ritorna puntatore al primo non-spazio */
static char *cfg_trim(char *s)
{
    if (!s) return s;
    /* Spazi iniziali */
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    /* Spazi finali */
    uint32_t len = cfg_strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                        s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

/* =============================================================================
 * cfg_apply_key — Applica una coppia sezione/chiave/valore alla config
 * ============================================================================= */
static void cfg_apply_key(KernelConfig *cfg, const char *section,
                           const char *key, const char *value)
{
    klog(LOG_DEBUG, "CFG: [%s] %s = %s", section, key, value);

    /* --- [kernel] --- */
    if (cfg_strcmp(section, "kernel") == 0) {
        if (cfg_strcmp(key, "loglevel") == 0) {
            cfg->loglevel = (uint32_t)cfg_atoi(value);
            extern void klog_set_level(int);
            klog_set_level((int)cfg->loglevel);
            return;
        }
        if (cfg_strcmp(key, "timer_hz") == 0) {
            cfg->timer_hz = (uint32_t)cfg_atoi(value);
            return;
        }
        /* ⚠️ IL KERNEL NON LA CONTROLLA E NON LA USA. La stringa viene
         * solo conservata e riconsegnata a chi la chiede: chi sa quali
         * disposizioni esistono e' il driver di tastiera, e duplicare
         * quell'elenco qui darebbe due liste che divergono. Un nome
         * sbagliato lo segnala il driver, che ha di che segnalarlo. */
        if (cfg_strcmp(key, "keymap") == 0) {
            cfg_strcpy(cfg->keymap, value, sizeof(cfg->keymap));
            return;
        }
        if (cfg_strcmp(key, "verboseboot") == 0) {
            /* Il valore predefinito è 0 (agosto 2026: prima era 1) e resta
             * 0 in tutti i casi dubbi: SOLO un numero diverso da zero fa
             * parlare il sistema.
             *
             * IL VALORE DEV'ESSERE UN NUMERO, e il controllo non è una
             * formalità. cfg_atoi() si ferma al primo carattere non
             * numerico e ritorna 0, quindi senza il controllo
             * `verboseboot = si` varrebbe zero — cioè un refuso deciderebbe
             * il comportamento invece di essere ignorato. La regola vale in
             * entrambe le direzioni ed è la ragione per cui questa riga non
             * è un semplice `cfg_atoi(value) != 0`: un valore non numerico
             * non è né vero né falso, è un errore, e la risposta a un
             * errore è il default.
             *
             * Il default è impostato in cfg_load() PRIMA di leggere il
             * file, così vale anche se il file manca o è illeggibile. */
            cfg->verbose_boot = 0;
            if (value[0] >= '0' && value[0] <= '9' && cfg_atoi(value) != 0) {
                cfg->verbose_boot = 1;
            }
            return;
        }
    }

    /* --- [boot] --- */
    if (cfg_strcmp(section, "boot") == 0) {
        if (cfg_strcmp(key, "shell") == 0) {
            cfg_strcpy(cfg->shell_path, value, sizeof(cfg->shell_path));
            return;
        }
        if (cfg_strcmp(key, "modules") == 0) {
            cfg_strcpy(cfg->modules_list, value, sizeof(cfg->modules_list));
            return;
        }
    }

    /* --- [modules] --- */
    if (cfg_strcmp(section, "modules") == 0) {
        if (cfg->module_count < CFG_MAX_MODULES) {
            cfg_strcpy(cfg->modules[cfg->module_count].name,
                       key, CFG_NAME_LEN);
            cfg_strcpy(cfg->modules[cfg->module_count].path,
                       value, CFG_PATH_LEN);
            cfg->module_count++;
        }
        return;
    }

    /* --- [mount] ---
     * Solo raccolta: il montaggio vero avviene al PASSO 13d, dopo
     * blk_init(). Qui i dispositivi non esistono ancora. */
    if (cfg_strcmp(section, "mount") == 0) {
        if (cfg->mount_count < CFG_MAX_MOUNT) {
            cfg_strcpy(cfg->mounts[cfg->mount_count].punto,
                       key, CFG_NAME_LEN);
            cfg_strcpy(cfg->mounts[cfg->mount_count].dev,
                       value, CFG_NAME_LEN);
            cfg->mount_count++;
        } else {
            klog(LOG_WARN, "CFG: [mount] oltre %d voci: '%s' ignorata",
                 CFG_MAX_MOUNT, key);
        }
        return;
    }

    /* --- [env] --- */
    if (cfg_strcmp(section, "env") == 0) {
        if (cfg->env_count < CFG_MAX_ENV) {
            cfg_strcpy(cfg->env[cfg->env_count].key,
                       key, CFG_NAME_LEN);
            cfg_strcpy(cfg->env[cfg->env_count].value,
                       value, CFG_PATH_LEN);
            cfg->env_count++;
        }
        return;
    }
}

/* =============================================================================
 * cfg_parse_buffer — Parsa il contenuto di kernel.cfg già in memoria
 * ============================================================================= */
static void cfg_parse_buffer(KernelConfig *cfg, char *buf, uint32_t size)
{
    char    section[CFG_NAME_LEN] = "";
    char   *p   = buf;
    char   *end = buf + size;
    uint32_t line_no = 0;

    while (p < end) {
        /* Trova la fine della riga corrente */
        char *line_start = p;
        while (p < end && *p != '\n' && *p != '\0') p++;
        if (p < end) *p++ = '\0';  /* Termina la riga */

        line_no++;
        char *line = cfg_trim(line_start);

        /* Salta righe vuote e commenti */
        if (!line || *line == '\0' || *line == '#' || *line == ';') continue;

        /* Sezione: [nome] */
        if (*line == '[') {
            char *close = line + 1;
            while (*close && *close != ']') close++;
            if (*close == ']') {
                *close = '\0';
                cfg_strcpy(section, line + 1, CFG_NAME_LEN);
                /* Trim nome sezione */
                char *s = cfg_trim(section);
                if (s != section) cfg_strcpy(section, s, CFG_NAME_LEN);
            } else {
                klog(LOG_WARN, "CFG: riga %u: sezione malformata: %s", line_no, line);
            }
            continue;
        }

        /* Coppia chiave=valore */
        char *eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') {
            if (*line) klog(LOG_WARN, "CFG: riga %u ignorata: %s", line_no, line);
            continue;
        }

        *eq = '\0';
        char *key   = cfg_trim(line);
        char *value = cfg_trim(eq + 1);

        if (!key || *key == '\0') continue;

        cfg_apply_key(cfg, section, key, value);
    }
}

/* =============================================================================
 * cfg_load — Carica e parsa /boot/kernel.cfg dal FAT12
 *
 * Ritorna: puntatore alla configurazione globale, NULL se errore
 * ============================================================================= */
KernelConfig *cfg_load(void)
{
    int      handle;
    uint32_t i;

    klog(LOG_INFO, "CFG: lettura /boot/kernel.cfg...");

    /* Valori di default */
    {
        uint8_t *p = (uint8_t *)&g_config;
        uint32_t n = sizeof(KernelConfig);
        while (n--) *p++ = 0;
    }
    g_config.loglevel     = 3;
    g_config.timer_hz     = 100;
    g_config.verbose_boot = 0;   /* default: avvio silenzioso. Vedi cfg.h */
    g_config.keymap[0]    = '\0'; /* nessuna: il driver tiene la sua */
    cfg_strcpy(g_config.shell_path, "/bin/sh", sizeof(g_config.shell_path));

    /* Identità disponibile anche se il file manca o è illeggibile: le
     * uscite anticipate qui sotto ritornano senza parsare nulla, e
     * `uname`/`env` devono comunque dire cosa sta girando. */
    cfg_apply_identity(&g_config);

    /* Apri il file */
    handle = vfs_open("/boot/kernel.cfg", 0x0000);
    if (handle < 0) {
        /* Prova anche KERNEL.CFG in maiuscolo (FAT12 è case-insensitive) */
        handle = vfs_open("/boot/KERNEL.CFG", 0x0000);
    }
    if (handle < 0) {
        klog(LOG_WARN, "CFG: /boot/kernel.cfg non trovato, uso valori default");
        g_config_loaded = 1;
        return &g_config;
    }

    /* Leggi tutto il file in un buffer statico.
     *
     * TRAPPOLA TROVATA (2026-07-31, 0.132): il buffer era di 4096 byte e
     * il file ne era arrivato a 5246. Il kernel ne leggeva 4095 e
     * proseguiva SENZA DIRE NULLA: la sezione [mount], che sta in fondo,
     * spariva. Il sintomo era "il montaggio automatico non funziona",
     * senza un errore da nessuna parte, e nessuna riga di codice
     * sbagliata da cercare.
     *
     * Il buffer e' stato portato a 8192, ma la dimensione non e' il vero
     * rimedio: qualunque tetto si sceglie, un giorno il file lo supera.
     * Il rimedio e' l'AVVISO qui sotto — una lettura che riempie tutto il
     * buffer significa che il file potrebbe continuare, e va detto. */
    #define CFG_BUF_SIZE 8192
    static char cfg_buf[CFG_BUF_SIZE];

    int n = vfs_read(handle, cfg_buf, CFG_BUF_SIZE - 1, 0);
    vfs_close(handle);

    if (n <= 0) {
        klog(LOG_WARN, "CFG: file vuoto o errore lettura, uso valori default");
        g_config_loaded = 1;
        return &g_config;
    }

    if (n >= (int)(CFG_BUF_SIZE - 1)) {
        klog(LOG_ERROR, "CFG: /boot/kernel.cfg supera %d byte: LETTO SOLO L'INIZIO, "
                        "le sezioni finali sono state ignorate", CFG_BUF_SIZE - 1);
    }

    cfg_buf[n] = '\0';

    /* Parsa il contenuto */
    cfg_parse_buffer(&g_config, cfg_buf, (uint32_t)n);

    /* Ri-applicata DOPO il parsing: se il file conteneva OSNAME o OSVER,
     * il valore di version.h deve vincere comunque. Vedi
     * cfg_env_set_identity per il perché. */
    cfg_apply_identity(&g_config);

    g_config_loaded = 1;

    /* Log della configurazione caricata */
    klog(LOG_INFO, "CFG: configurazione caricata:");
    klog(LOG_INFO, "  loglevel  = %u", g_config.loglevel);
    klog(LOG_INFO, "  timer_hz  = %u", g_config.timer_hz);
    klog(LOG_INFO, "  verbose   = %u", g_config.verbose_boot);
    klog(LOG_INFO, "  shell     = %s", g_config.shell_path);
    klog(LOG_INFO, "  moduli    = %s (%u)", g_config.modules_list, g_config.module_count);

    for (i = 0; i < g_config.module_count; i++) {
        klog(LOG_INFO, "  modulo[%u]: %s = %s",
             i, g_config.modules[i].name, g_config.modules[i].path);
    }
    for (i = 0; i < g_config.env_count; i++) {
        klog(LOG_INFO, "  env[%u]: %s = %s",
             i, g_config.env[i].key, g_config.env[i].value);
    }

    return &g_config;
}

/* =============================================================================
 * cfg_get — Ritorna la configurazione corrente (deve essere già caricata)
 * ============================================================================= */
KernelConfig *cfg_get(void)
{
    if (!g_config_loaded) return cfg_load();
    return &g_config;
}

/* =============================================================================
 * cfg_env_set_identity — inietta l'identità del sistema in [env]
 *
 * OSNAME/OSVER/AUTHOR NON sono configurabili: sono derivati da
 * kernel/include/version.h, la fonte unica di verità. Questa funzione li
 * scrive (o li sovrascrive, se qualcuno li ha messi nel file) subito dopo
 * il parsing.
 *
 * PERCHÉ SOVRASCRIVERE invece di lasciarli configurabili: `ver` legge la
 * globale del kernel, `uname` e `env` leggono l'ambiente. Se le due
 * sorgenti potessero divergere, il sistema riporterebbe due versioni
 * diverse di sé stesso a seconda del comando — che è esattamente il
 * difetto trovato in questa stessa sessione (la sezione [env] duplicava
 * valori hardcoded altrove). In più la versione si incrementa a ogni
 * modifica del KERNEL: un file di configurazione, che vive sul floppy e
 * non viene ricompilato, non può esserne la fonte.
 *
 * Restano configurabili tutte le altre variabili di [env] (PATH, HOME,
 * TERM, ...): quelle sono davvero scelte dell'utente.
 * ============================================================================= */
static void cfg_env_set_identity(KernelConfig *cfg, const char *key,
                                  const char *value)
{
    uint32_t i;

    for (i = 0; i < cfg->env_count; i++) {
        if (cfg_strcmp(cfg->env[i].key, key) == 0) {
            cfg_strcpy(cfg->env[i].value, value, CFG_PATH_LEN);
            return;
        }
    }
    if (cfg->env_count < CFG_MAX_ENV) {
        cfg_strcpy(cfg->env[cfg->env_count].key,   key,   CFG_NAME_LEN);
        cfg_strcpy(cfg->env[cfg->env_count].value, value, CFG_PATH_LEN);
        cfg->env_count++;
    }
}

static void cfg_apply_identity(KernelConfig *cfg)
{
    cfg_env_set_identity(cfg, "OSNAME", EXOS_NAME);
    cfg_env_set_identity(cfg, "OSVER",  EXOS_VERSION);
    cfg_env_set_identity(cfg, "AUTHOR", EXOS_AUTHOR);
}

/* =============================================================================
 * cfg_getenv — Cerca una variabile d'ambiente nella configurazione
 * Ritorna il valore o NULL se non trovata
 * ============================================================================= */
const char *cfg_getenv(const char *key)
{
    uint32_t i;
    KernelConfig *cfg = cfg_get();
    for (i = 0; i < cfg->env_count; i++) {
        if (cfg_strcmp(cfg->env[i].key, key) == 0) {
            return cfg->env[i].value;
        }
    }
    return NULL;
}

/* =============================================================================
 * cfg_get_option — espone ai processi utente le opzioni scalari che NON
 * stanno nella sezione [env].
 *
 * `verboseboot` vive in [kernel] perché è una scelta di sistema, non una
 * variabile d'ambiente — ma la shell deve conoscerla per decidere se
 * stampare il proprio banner, altrimenti un boot silenzioso resterebbe
 * silenzioso solo a metà. Invece di spostarla in [env] (dove
 * semanticamente non appartiene) o di dare alla shell un secondo canale,
 * SYS_GETENV interroga anche questa funzione quando la chiave non è in
 * [env]: per il chiamante è una sola API, "leggi la configurazione".
 *
 * Ritorna un puntatore a un letterale statico, mai a memoria temporanea:
 * il chiamante (sys_getenv) copia subito in un buffer utente.
 * ============================================================================= */
const char *cfg_get_option(const char *key)
{
    KernelConfig *cfg = cfg_get();

    if (cfg_strcmp(key, "verboseboot") == 0) {
        return cfg->verbose_boot ? "1" : "0";
    }
    if (cfg_strcmp(key, "shell") == 0) {
        return cfg->shell_path;
    }
    if (cfg_strcmp(key, "keymap") == 0) {
        return cfg->keymap[0] ? cfg->keymap : NULL;
    }
    return NULL;
}

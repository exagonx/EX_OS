/* =============================================================================
 * bin/hwinfo/hwinfo.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Inventario dell'hardware, scritto per chi deve SCRIVERE UN DRIVER.
 *
 *     hwinfo              guarda e scrive /hwinfo.txt, ripetendo a video
 *     hwinfo <file>       lo scrive dove dici tu
 *     hwinfo -n           guarda e stampa, non scrive niente
 *     hwinfo -s           solo il riassunto: cosa manca di driver
 *
 * -----------------------------------------------------------------------------
 * PERCHE' ESISTE, VISTO CHE C'E' GIA' hwconfig
 *
 * `hwconfig` risponde a «come accendo questa macchina»: guarda l'hardware,
 * sceglie i driver che servono e scrive kernel.cfg. Le sue risposte sono
 * decisioni, e per prenderle gli basta sapere SE una scheda c'e'.
 *
 * Questo risponde a un'altra domanda: «cosa dovrei scrivere per pilotare
 * questa scheda». Sono dati diversi. A chi scrive un driver non serve
 * sapere che c'e' una scheda di rete: gli servono il numero di venditore e
 * dispositivo su cui agganciarsi, la classe che dice di che genere e', la
 * revisione, DOVE rispondono i registri (i BAR, e in quale dei due spazi),
 * su quale IRQ, e cos'ha gia' acceso il BIOS nel registro comando.
 *
 * Sono tutte cose che il bus sa gia' e che nessuno raccoglie in un posto
 * solo. Il risultato e' un file di testo: si legge, si allega a una
 * segnalazione, si tiene aperto accanto mentre si scrive il driver.
 *
 * -----------------------------------------------------------------------------
 * ! NON SONDA NIENTE. GUARDA E BASTA, ED E' UNA SCELTA
 *
 * Un inventario hardware e' tentato di «provare» gli indirizzi noti per
 * scoprire cosa c'e'. Qui non si fa, per tre casi concreti:
 *
 *   ATA        le porte 0x1F0-0x1F7 le sta usando IL KERNEL, in questo
 *              istante, per leggere il disco. Scriverci sopra durante un
 *              trasferimento significa perdere il disco da sotto i piedi a
 *              chi non sa che qualcuno ha scritto.
 *
 *   seriale    0x3F8 e' la console di debug su cui esce il log del kernel.
 *              Il modo classico di riconoscere una UART e' scrivere nel
 *              registro scratch e rileggere: sulla console attiva vuol dire
 *              sporcare l'unica traccia che si ha quando qualcosa va male.
 *
 *   ISA        una scheda ISA non ha spazio di configurazione e si trova
 *              solo scrivendo sulla sua porta di reset. Se a quell'indirizzo
 *              c'e' un'ALTRA scheda, le si e' appena scritto addosso. La
 *              lista dei «soliti indirizzi» e' esattamente la lista di
 *              quelli che tutte le schede ISA si contendevano.
 *
 * Percio' le fonti sono tre, e tutte e tre sono di sola lettura:
 *
 *   CPUID              un'istruzione, non una porta: nessun effetto
 *   /dev/pci.drv       lo spazio di configurazione, che leggere non rompe
 *   meminfo()          la bitmap della memoria, che il kernel gia' tiene
 *
 * Cio' che NON si vede finisce in fondo al rapporto, detto per nome. Un
 * inventario che tace su cio' che non ha guardato fa credere che non ci
 * sia — ed e' peggio di uno che lo dichiara.
 *
 * -----------------------------------------------------------------------------
 * ! IL RAPPORTO ESCE DA UN CODICE SOLO, NON DA DUE
 *
 * Va sia a video sia su file. Scriverlo due volte — un printf e un
 * fprintf — vuol dire due testi che il primo giorno coincidono e il
 * secondo no, e chi legge il file non ha modo di accorgersene. Qui c'e'
 * `rap()`, che scrive su tutte le destinazioni aperte; le destinazioni
 * sono al massimo due e le decide main().
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"

#define RAPPORTO_PREDEFINITO  "/hwinfo.txt"
#define ATTESA_MS             2000
#define MAX_DISPOSITIVI       64

/* -----------------------------------------------------------------------------
 * Uscita doppia: video e file, con un codice solo.
 * --------------------------------------------------------------------------- */
static FILE *g_dest[2];
static int   g_n_dest;

static void dest_aggiungi(FILE *f)
{
    if (f != NULL && g_n_dest < 2) g_dest[g_n_dest++] = f;
}

static void rap(const char *fmt, ...)
{
    int i;

    for (i = 0; i < g_n_dest; i++) {
        __builtin_va_list ap;
        __builtin_va_start(ap, fmt);
        vfprintf(g_dest[i], fmt, ap);
        __builtin_va_end(ap);
    }
}

/* =============================================================================
 * LA CPU
 *
 * ! CPUID NON SI PUO' ESEGUIRE E BASTA. Sull'80386 quell'istruzione non
 * esiste: eseguirla da' opcode non valido, e questo programma morirebbe
 * senza dire perche' — su una macchina che EX-OS per il resto supporta.
 *
 * Il modo di chiederlo e' obliquo ma e' l'unico: il bit 21 di EFLAGS (ID)
 * e' scrivibile SOLO se la CPU ha CPUID. Si prova a girarlo; se resta
 * com'era, CPUID non c'e'. E' lo stesso controllo che fa la runtime di
 * FreeBASIC in x86/cpudetect.s, per la stessa ragione.
 * ========================================================================== */
static int cpuid_c_e(void)
{
    unsigned int prima, dopo;

    __asm__ __volatile__(
        "pushfl\n\t"
        "pushfl\n\t"
        "popl %0\n\t"           /* prima = EFLAGS */
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"/* gira il bit 21 (ID) */
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"           /* dopo = EFLAGS come la CPU l'ha accettato */
        "popfl\n\t"             /* rimette EFLAGS com'era */
        : "=&r"(prima), "=&r"(dopo)
        :
        : "cc");

    return ((prima ^ dopo) & 0x200000) != 0;
}

static void cpuid(unsigned int foglia, unsigned int r[4])
{
    __asm__ __volatile__("cpuid"
        : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
        : "a"(foglia), "c"(0));
}

/* Copia i registri in una stringa, come CPUID vuole: 4 byte per registro,
 * in ordine. Non e' terminata da sola. */
static void parola(char *dst, unsigned int v)
{
    dst[0] = (char)(v & 0xFF);
    dst[1] = (char)((v >> 8) & 0xFF);
    dst[2] = (char)((v >> 16) & 0xFF);
    dst[3] = (char)((v >> 24) & 0xFF);
}

/* I flag che contano per chi scrive un driver o compila per questa
 * macchina. Non tutti: quelli che cambiano una decisione. */
typedef struct { int bit; const char *nome; const char *cosa; } Flag;

static const Flag g_flag_edx[] = {
    { 0,  "fpu",   "coprocessore in CPU"            },
    { 4,  "tsc",   "contatore di cicli (RDTSC)"     },
    { 5,  "msr",   "registri di modello (RDMSR)"    },
    { 6,  "pae",   "indirizzi fisici a 36 bit"      },
    { 8,  "cx8",   "CMPXCHG8B"                      },
    { 9,  "apic",  "APIC locale — serve all'SMP"    },
    { 11, "sep",   "SYSENTER/SYSEXIT"               },
    { 13, "pge",   "pagine globali"                 },
    { 15, "cmov",  "mosse condizionate"             },
    { 19, "clfsh", "CLFLUSH — serve al DMA coerente"},
    { 23, "mmx",   "MMX"                            },
    { 24, "fxsr",  "FXSAVE/FXRSTOR"                 },
    { 25, "sse",   "SSE"                            },
    { 26, "sse2",  "SSE2"                           },
    { 28, "htt",   "piu' thread per pacchetto"      },
};

static const Flag g_flag_ecx[] = {
    { 0,  "sse3",   "SSE3"                          },
    { 12, "fma",    "FMA"                           },
    { 13, "cx16",   "CMPXCHG16B"                    },
    { 19, "sse4_1", "SSE4.1"                        },
    { 20, "sse4_2", "SSE4.2"                        },
    { 23, "popcnt", "POPCNT"                        },
    { 25, "aes",    "AES-NI"                        },
    { 28, "avx",    "AVX"                           },
    { 31, "hyperv", "gira in una macchina virtuale" },
};

static void sezione_cpu(void)
{
    unsigned int r[4];
    char         testo[64];
    unsigned int massima;
    int          i, n;

    rap("\n");
    rap("=============================================================\n");
    rap(" PROCESSORE\n");
    rap("=============================================================\n");

    if (!cpuid_c_e()) {
        /* ! NON e' un errore del programma: e' un dato sul processore. */
        rap("  CPUID assente: e' un 80386 (o un 486 dei primi).\n");
        rap("  Tutto cio' che segue non e' interrogabile su questa CPU.\n");
        return;
    }

    cpuid(0, r);
    massima = r[0];
    parola(testo + 0, r[1]);        /* EBX, EDX, ECX: in QUEST'ordine */
    parola(testo + 4, r[3]);
    parola(testo + 8, r[2]);
    testo[12] = '\0';
    rap("  venditore        %s\n", testo);
    rap("  foglia massima   %u\n", massima);

    if (massima >= 1) {
        unsigned int fam, mod, step, ext_fam, ext_mod;

        cpuid(1, r);
        step    = r[0] & 0xF;
        mod     = (r[0] >> 4) & 0xF;
        fam     = (r[0] >> 8) & 0xF;
        ext_mod = (r[0] >> 16) & 0xF;
        ext_fam = (r[0] >> 20) & 0xFF;

        /* La codifica estesa non e' un dettaglio: su tutte le CPU dal
         * Pentium 4 in poi la famiglia "vera" e' 15 + l'estensione, e
         * leggere solo i 4 bit bassi fa apparire un Core i7 come una
         * famiglia 6 qualunque. */
        if (fam == 0xF) fam += ext_fam;
        if (fam == 0x6 || fam == 0xF) mod += (ext_mod << 4);

        rap("  famiglia         %u\n", fam);
        rap("  modello          %u\n", mod);
        rap("  revisione        %u\n", step);
        rap("  firma            0x%08x\n", r[0]);

        rap("  capacita'        ");
        n = 0;
        for (i = 0; i < (int)(sizeof(g_flag_edx) / sizeof(g_flag_edx[0])); i++) {
            if (r[3] & (1u << g_flag_edx[i].bit)) {
                rap("%s ", g_flag_edx[i].nome);
                n++;
            }
        }
        for (i = 0; i < (int)(sizeof(g_flag_ecx) / sizeof(g_flag_ecx[0])); i++) {
            if (r[2] & (1u << g_flag_ecx[i].bit)) {
                rap("%s ", g_flag_ecx[i].nome);
                n++;
            }
        }
        if (n == 0) rap("(nessuna fra quelle note)");
        rap("\n");
        rap("  CPUID(1).edx     0x%08x\n", r[3]);
        rap("  CPUID(1).ecx     0x%08x\n", r[2]);

        /* Quelle che cambiano cosa si puo' scrivere nel kernel, dette per
         * esteso invece che come sigla. */
        rap("\n  Cosa ne discende per il kernel:\n");
        rap("    APIC locale    %s\n", (r[3] & (1u << 9))
            ? "SI  — l'SMP e' possibile su questa macchina"
            : "NO  — niente SMP: senza APIC non si avviano le altre CPU");
        rap("    RDTSC          %s\n", (r[3] & (1u << 4))
            ? "SI  — si possono misurare i tempi senza il PIT"
            : "NO  — per misurare il tempo resta il PIT");
        rap("    SYSENTER       %s\n", (r[3] & (1u << 11))
            ? "SI  — le chiamate di sistema possono lasciare int 0x80"
            : "NO  — le chiamate di sistema restano su int 0x80");
    }

    /* Il nome commerciale sta nelle foglie estese, e vanno chieste a parte:
     * la foglia 0 dice la massima ORDINARIA, non la massima estesa. */
    cpuid(0x80000000u, r);
    if (r[0] >= 0x80000004u) {
        char nome[49];
        unsigned int f;

        for (f = 0; f < 3; f++) {
            cpuid(0x80000002u + f, r);
            parola(nome + f * 16 + 0,  r[0]);
            parola(nome + f * 16 + 4,  r[1]);
            parola(nome + f * 16 + 8,  r[2]);
            parola(nome + f * 16 + 12, r[3]);
        }
        nome[48] = '\0';
        rap("\n  nome             %s\n", nome);
    }
}

/* =============================================================================
 * LA MEMORIA
 * ========================================================================== */
static void sezione_memoria(void)
{
    MemInfo mi;

    rap("\n");
    rap("=============================================================\n");
    rap(" MEMORIA\n");
    rap("=============================================================\n");

    if (meminfo(&mi) != 0) {
        rap("  non leggibile: meminfo() ha rifiutato\n");
        return;
    }
    rap("  convenzionale    %u KB\n", mi.conv_total_kb);
    rap("  superiore (UMA)  %u KB\n", mi.uma_total_kb);
    rap("  estesa           %u KB\n", mi.ext_total_kb);
    rap("  totale           %u KB (%u MB), libera %u KB\n",
        mi.total_kb, mi.total_kb / 1024, mi.free_kb);
    rap("  pagina           %u byte\n", mi.page_size);
}

/* =============================================================================
 * IL BUS PCI
 * ========================================================================== */

/* Classi e sottoclassi. sotto == 0xFF significa «tutta la classe».
 *
 * ! L'ORDINE CONTA: si cerca dall'inizio e ci si ferma alla prima
 * corrispondenza, quindi le voci con sottoclasse precisa devono stare
 * PRIMA della voce generica della stessa classe. */
typedef struct {
    unsigned char classe;
    unsigned char sotto;
    const char   *nome;
} Classe;

static const Classe g_classi[] = {
    { 0x00, 0xFF, "dispositivo anteriore al PCI 2.0"          },

    { 0x01, 0x00, "controller SCSI"                            },
    { 0x01, 0x01, "controller IDE"                             },
    { 0x01, 0x02, "controller floppy"                          },
    { 0x01, 0x04, "controller RAID"                            },
    { 0x01, 0x05, "controller ATA"                             },
    { 0x01, 0x06, "controller SATA (AHCI)"                     },
    { 0x01, 0x07, "controller SAS"                             },
    { 0x01, 0x08, "controller NVMe"                            },
    { 0x01, 0xFF, "controller di memoria di massa"             },

    { 0x02, 0x00, "scheda Ethernet"                            },
    { 0x02, 0x01, "scheda Token Ring"                          },
    { 0x02, 0x02, "scheda FDDI"                                },
    { 0x02, 0x03, "scheda ATM"                                 },
    { 0x02, 0xFF, "controller di rete"                         },

    { 0x03, 0x00, "scheda video VGA"                           },
    { 0x03, 0x01, "scheda video XGA"                           },
    { 0x03, 0x02, "acceleratore 3D"                            },
    { 0x03, 0xFF, "controller video"                           },

    { 0x04, 0x00, "video multimediale"                         },
    { 0x04, 0x01, "scheda audio"                               },
    { 0x04, 0x03, "audio ad alta definizione (HDA)"            },
    { 0x04, 0xFF, "dispositivo multimediale"                   },

    { 0x05, 0xFF, "controller di memoria"                      },

    { 0x06, 0x00, "ponte host (northbridge)"                   },
    { 0x06, 0x01, "ponte ISA (southbridge)"                    },
    { 0x06, 0x02, "ponte EISA"                                 },
    { 0x06, 0x04, "ponte PCI-PCI"                              },
    { 0x06, 0x80, "ponte, altro tipo"                          },
    { 0x06, 0xFF, "ponte"                                      },

    { 0x07, 0x00, "porta seriale"                              },
    { 0x07, 0x01, "porta parallela"                            },
    { 0x07, 0xFF, "controller di comunicazione"                },

    { 0x08, 0x00, "controller di interruzioni (PIC)"           },
    { 0x08, 0x01, "controller DMA"                             },
    { 0x08, 0x02, "temporizzatore"                             },
    { 0x08, 0x03, "orologio in tempo reale (RTC)"              },
    { 0x08, 0xFF, "periferica di sistema"                      },

    { 0x09, 0x00, "tastiera"                                   },
    { 0x09, 0x02, "mouse"                                      },
    { 0x09, 0xFF, "dispositivo di ingresso"                    },

    { 0x0B, 0xFF, "processore"                                 },

    { 0x0C, 0x00, "controller FireWire"                        },
    { 0x0C, 0x03, "controller USB"                             },
    { 0x0C, 0x05, "controller SMBus"                           },
    { 0x0C, 0xFF, "controller di bus seriale"                  },

    { 0x0D, 0xFF, "controller senza fili"                      },
    { 0x10, 0xFF, "acceleratore di cifratura"                  },
    { 0x11, 0xFF, "acquisizione dati / elaborazione di segnale"},
};

static const char *nome_classe(unsigned char c, unsigned char s)
{
    int i;

    for (i = 0; i < (int)(sizeof(g_classi) / sizeof(g_classi[0])); i++) {
        if (g_classi[i].classe != c) continue;
        if (g_classi[i].sotto == s || g_classi[i].sotto == 0xFF)
            return g_classi[i].nome;
    }
    return "sconosciuto";
}

/* Per l'USB il prog-IF non e' un dettaglio: dice QUALE dei quattro
 * standard incompatibili e', cioe' quale driver va scritto. */
static const char *nome_interfaccia(unsigned char c, unsigned char s,
                                    unsigned char pi)
{
    if (c == 0x0C && s == 0x03) {
        switch (pi) {
        case 0x00: return "UHCI (USB 1.1, Intel/VIA)";
        case 0x10: return "OHCI (USB 1.1, il resto)";
        case 0x20: return "EHCI (USB 2.0)";
        case 0x30: return "XHCI (USB 3.0)";
        case 0xFE: return "solo dispositivo USB, non host";
        default:   break;
        }
    }
    if (c == 0x01 && s == 0x01) {
        /* Sui controller IDE il bit 7 dice se sa fare bus master, ed e'
         * la differenza fra un driver in PIO e uno in DMA. */
        return (pi & 0x80) ? "IDE con bus master (DMA possibile)"
                           : "IDE senza bus master (solo PIO)";
    }
    return NULL;
}

typedef struct { unsigned short id; const char *nome; } Venditore;

static const Venditore g_venditori[] = {
    { 0x1002, "AMD/ATI"        }, { 0x1013, "Cirrus Logic"   },
    { 0x1011, "DEC"            }, { 0x1022, "AMD"            },
    { 0x1039, "SiS"            }, { 0x1050, "Winbond"        },
    { 0x106B, "Apple"          }, { 0x1093, "National Instr."},
    { 0x1106, "VIA"            }, { 0x1234, "QEMU/Bochs"     },
    { 0x1274, "Ensoniq"        }, { 0x14E4, "Broadcom"       },
    { 0x15AD, "VMware"         }, { 0x1969, "Atheros"        },
    { 0x1AF4, "Red Hat (virtio)"}, { 0x1B36, "Red Hat (QEMU)" },
    { 0x10DE, "NVIDIA"         }, { 0x10EC, "Realtek"        },
    { 0x5333, "S3 Graphics"    }, { 0x8086, "Intel"          },
    { 0x80EE, "VirtualBox"     }, { 0x9710, "MosChip"        },
};

static const char *nome_venditore(unsigned short v)
{
    int i;

    for (i = 0; i < (int)(sizeof(g_venditori) / sizeof(g_venditori[0])); i++)
        if (g_venditori[i].id == v) return g_venditori[i].nome;
    return NULL;
}

/* -----------------------------------------------------------------------------
 * Chi guida cosa.
 *
 * ! TRE STATI, NON DUE. «C'e' il driver» e «non c'e'» non bastano a
 * descrivere la realta':
 *
 *   SCRITTO e PRESENTE   il file c'e' su questo supporto: si carica
 *   SCRITTO ma ASSENTE   esiste nel progetto, non su questo supporto —
 *                        chi legge non deve cercare un guasto nella scheda
 *   DA SCRIVERE          nessuno l'ha ancora scritto: e' il caso per cui
 *                        questo programma esiste
 *
 * Il primo e il secondo si distinguono solo guardando il filesystem, e per
 * questo la tabella dice cosa SERVIREBBE mentre stat() dice cosa C'E'.
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned short venditore;    /* PCI_QUALUNQUE = confronta sulla classe */
    unsigned short dispositivo;
    unsigned char  classe;
    unsigned char  sotto;
    const char    *modello;
    const char    *driver;       /* NULL = da scrivere */
    const char    *nota;
} Guida;

#define QQ PCI_QUALUNQUE

static const Guida g_guide[] = {
    /* --- schede di rete che sappiamo pilotare ---------------------------- */
    { 0x1022, 0x2000, 0, 0, "AMD PCnet-PCI II/FAST III (Am79C970/C973)",
      "/dev/pcnet.drv", NULL },
    { 0x1022, 0x2001, 0, 0, "AMD PCnet-Home", "/dev/pcnet.drv", NULL },
    { 0x10EC, 0x8029, 0, 0, "Realtek RTL8029(AS) — NE2000 PCI",
      "/dev/ne2k.drv", NULL },
    { 0x1050, 0x0940, 0, 0, "Winbond W89C940 — NE2000 PCI",
      "/dev/ne2k.drv", NULL },
    { 0x1106, 0x0926, 0, 0, "VIA VT86C926 Amazon — NE2000 PCI",
      "/dev/ne2k.drv", NULL },
    { 0x8E2E, 0x3000, 0, 0, "KTI ET32P2 — NE2000 PCI",
      "/dev/ne2k.drv", NULL },

    /* --- schede di rete note, driver da scrivere -------------------------- */
    /* ! QUESTA SCHEDA HA I REGISTRI IN MEMORIA, non nelle porte: il driver
     * li mappa con mmio_map(). E' il primo di EX-OS a farlo, ed e' il motivo
     * per cui quella syscall esiste (13 agosto 2026). */
    { 0x8086, 0x100E, 0, 0, "Intel 82540EM (e1000)", "/dev/e1000.drv",
      "registri in memoria (BAR0), mappati con mmio_map" },
    { 0x8086, 0x1229, 0, 0, "Intel 82557/8/9 (EtherExpress Pro/100)", NULL,
      "descrizione pubblica Intel" },
    { 0x10EC, 0x8139, 0, 0, "Realtek RTL8139", NULL,
      "il piu' semplice dopo NE2000: quattro buffer di trasmissione" },
    { 0x1011, 0x0019, 0, 0, "DEC 21140 (Tulip)", NULL, NULL },
    { 0x1AF4, 0x1000, 0, 0, "virtio-net", NULL,
      "richiede prima uno strato virtio (code condivise)" },

    /* --- video ------------------------------------------------------------ */
    { 0x1234, 0x1111, 0, 0, "QEMU/Bochs VGA standard", "/dev/svga.drv", NULL },
    { 0x80EE, 0xBEEF, 0, 0, "VirtualBox VGA", "/dev/svga.drv", NULL },
    { 0x15AD, 0x0405, 0, 0, "VMware SVGA II", NULL,
      "protocollo FIFO proprio, documentato da VMware" },
    { 0x1013, 0x00B8, 0, 0, "Cirrus Logic GD5446", NULL,
      "compatibile VGA: la modalita' base funziona senza driver" },

    /* --- memoria di massa -------------------------------------------------- */
    { 0x8086, 0x7010, 0, 0, "Intel PIIX3 IDE", "(nel kernel)",
      "kernel/block/ata.c — PIO e bus master DMA" },
    { 0x8086, 0x7111, 0, 0, "Intel PIIX4 IDE", "(nel kernel)",
      "kernel/block/ata.c" },

    /* --- corrispondenze per CLASSE: valgono per i modelli non elencati ----- */
    { QQ, QQ, 0x01, 0x01, NULL, "(nel kernel)",
      "kernel/block/ata.c gestisce l'IDE generico" },
    { QQ, QQ, 0x01, 0x06, NULL, NULL,
      "AHCI: registri in memoria, liste di comandi — driver da scrivere" },
    { QQ, QQ, 0x01, 0x08, NULL, NULL,
      "NVMe: code in memoria, nessuna eredita' ATA" },
    { QQ, QQ, 0x02, 0x00, NULL, NULL,
      "Ethernet non riconosciuta: serve la scheda tecnica del modello" },
    { QQ, QQ, 0x03, 0xFF, NULL, NULL,
      "video: /dev/svga.drv copre VGA/VBE standard, non l'accelerazione" },
    { QQ, QQ, 0x04, 0x01, NULL, NULL,
      "audio: EX-OS non ha ancora uno strato audio" },
    { QQ, QQ, 0x04, 0x03, NULL, NULL,
      "HDA: serve prima uno strato audio" },
    { QQ, QQ, 0x0C, 0x03, NULL, NULL,
      "USB: serve prima uno stack USB (host, hub, classi)" },
    { QQ, QQ, 0x06, 0xFF, NULL, "(non serve)",
      "i ponti li configura il BIOS; il sistema non li pilota" },
    { QQ, QQ, 0x08, 0xFF, NULL, "(nel kernel)",
      "PIC, PIT e RTC li gestisce direttamente il kernel" },
    { QQ, QQ, 0x05, 0xFF, NULL, "(non serve)",
      "controller di memoria: lo configura il BIOS" },
};

static const Guida *guida_di(const PciDispositivo *d)
{
    int i;

    /* Prima per modello: e' piu' preciso della classe e deve vincere. */
    for (i = 0; i < (int)(sizeof(g_guide) / sizeof(g_guide[0])); i++) {
        if (g_guide[i].venditore == QQ) continue;
        if (g_guide[i].venditore == d->venditore &&
            g_guide[i].dispositivo == d->dispositivo)
            return &g_guide[i];
    }
    for (i = 0; i < (int)(sizeof(g_guide) / sizeof(g_guide[0])); i++) {
        if (g_guide[i].venditore != QQ) continue;
        if (g_guide[i].classe != d->classe) continue;
        if (g_guide[i].sotto == d->sottoclasse || g_guide[i].sotto == 0xFF)
            return &g_guide[i];
    }
    return NULL;
}

/* Il driver e' un file vero su questo supporto? Le voci fra parentesi —
 * «(nel kernel)», «(non serve)» — non sono percorsi e non si cercano. */
static int driver_su_disco(const char *driver)
{
    struct stat st;

    if (driver == NULL || driver[0] != '/') return 0;
    return stat(driver, &st) == 0;
}

/* -----------------------------------------------------------------------------
 * Dialogo col server PCI
 *
 * ! SI CONTROLLA CHI HA RISPOSTO. ipc_recv consegna il prossimo messaggio
 * della mailbox, non «la risposta alla mia domanda». Se nel frattempo
 * scrive qualcun altro, i suoi byte verrebbero letti come un
 * PciDispositivo — cioe' hardware inventato dentro un documento che serve
 * proprio a dire cosa c'e' davvero.
 * --------------------------------------------------------------------------- */
static int pci_elenca(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           tentativi;

    r.ordinale    = ordinale;
    r.classe      = PCI_QUALUNQUE;
    r.sottoclasse = PCI_QUALUNQUE;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send(pid, PCI_MSG_ELENCA, &r, sizeof(r)) < 0) return -1;

    for (tentativi = 0; tentativi < 8; tentativi++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
        return -1;
    }
    return -1;
}

/* Legge 4 byte di configurazione. Torna 0 e riempie *v, oppure -1.
 * Serve per i registri che PciDispositivo non porta: comando, stato,
 * sottosistema — e il sottosistema e' spesso l'unico modo di distinguere
 * due schede che dichiarano lo stesso dispositivo. */
static int pci_leggi(int pid, const PciDispositivo *d, unsigned short off,
                     unsigned int *v)
{
    PciAzione     a;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           tentativi;

    a.bus = d->bus; a.slot = d->slot; a.funzione = d->funzione;
    a.riservato = 0; a.offset = off; a.bit = 0;

    if (ipc_send(pid, PCI_MSG_LEGGI, &a, sizeof(a)) < 0) return -1;

    for (tentativi = 0; tentativi < 8; tentativi++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ATTESA_MS) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo == PCI_MSG_VALORE && meta.len >= sizeof(PciValore)) {
            PciValore pv;
            memcpy(&pv, buf, sizeof(pv));
            *v = pv.valore;
            return 0;
        }
        return -1;
    }
    return -1;
}

/* Accende /dev/pci.drv se non c'e' gia'.
 *
 * ! SI CERCA IN PIU' POSTI. Avviando dal CD la radice e' il supporto, e
 * /dev/pci.drv non esiste: sta in /dev del CD oppure accanto agli altri
 * driver. Un errore «servizio PCI assente» su una macchina che il driver
 * ce l'ha, solo altrove, manda a cercare un guasto che non c'e'. */
static int assicura_pci(void)
{
    static const char *posti[] = {
        "/dev/pci.drv", "/drivers/pci.drv", "/cdrom/dev/pci.drv", NULL
    };
    int i, pid, atteso;

    pid = ipc_lookup(PCI_SERVIZIO);
    if (pid > 0) return pid;

    for (i = 0; posti[i] != NULL; i++) {
        struct stat st;
        char *const argv[] = { (char *)posti[i], NULL };

        if (stat(posti[i], &st) != 0) continue;
        if (spawn(posti[i], argv) <= 0) continue;

        for (atteso = 0; atteso < 4000; atteso += 100) {
            pid = ipc_lookup(PCI_SERVIZIO);
            if (pid > 0) return pid;
            usleep(100 * 1000);
        }
    }
    return -1;
}

/* -----------------------------------------------------------------------------
 * Il corpo del rapporto
 * --------------------------------------------------------------------------- */
static PciDispositivo g_disp[MAX_DISPOSITIVI];
static int            g_n_disp;

static void sezione_pci(int pid)
{
    unsigned int i;
    int          n;

    rap("\n");
    rap("=============================================================\n");
    rap(" BUS PCI\n");
    rap("=============================================================\n");

    if (pid <= 0) {
        rap("  Il servizio PCI non e' disponibile: niente enumerazione.\n");
        rap("  Serve /dev/pci.drv — c'e' sul CD di EX-OS. Avvialo con:\n");
        rap("      /dev/pci.drv &\n");
        return;
    }

    for (i = 0; i < MAX_DISPOSITIVI; i++) {
        n = pci_elenca(pid, i, &g_disp[g_n_disp]);
        if (n < 0) {
            rap("  !  il servizio PCI ha smesso di rispondere dopo %d "
                "dispositivi\n", g_n_disp);
            break;
        }
        if (n == 0) break;
        g_n_disp++;
    }

    rap("  dispositivi trovati: %d\n", g_n_disp);

    for (n = 0; n < g_n_disp; n++) {
        const PciDispositivo *d = &g_disp[n];
        const Guida          *g = guida_di(d);
        const char           *vn = nome_venditore(d->venditore);
        const char           *in = nome_interfaccia(d->classe, d->sottoclasse,
                                                    d->interfaccia);
        unsigned int          cmd = 0, sub = 0;
        int                   b, ha_cmd, ha_sub;

        rap("\n-------------------------------------------------------------\n");
        rap("  %02x:%02x.%u  %s\n", d->bus, d->slot, d->funzione,
            nome_classe(d->classe, d->sottoclasse));
        rap("-------------------------------------------------------------\n");
        rap("    venditore      0x%04x  %s\n", d->venditore,
            vn ? vn : "(non in tabella)");
        rap("    dispositivo    0x%04x  %s\n", d->dispositivo,
            (g && g->modello) ? g->modello : "(non in tabella)");
        rap("    revisione      0x%02x\n", d->revisione);
        rap("    classe         %02x.%02x.%02x  (classe.sottoclasse.progIF)\n",
            d->classe, d->sottoclasse, d->interfaccia);
        if (in != NULL)
            rap("    interfaccia    %s\n", in);

        ha_sub = (pci_leggi(pid, d, 0x2C, &sub) == 0);
        if (ha_sub && sub != 0 && sub != 0xFFFFFFFFu)
            rap("    sottosistema   0x%04x:0x%04x\n",
                (unsigned)(sub & 0xFFFF), (unsigned)(sub >> 16));

        if (d->irq_linea == 0xFF || d->irq_linea == 0)
            rap("    IRQ            nessuno assegnato dal BIOS\n");
        else
            rap("    IRQ            %u\n", d->irq_linea);

        /* I BAR sono la parte che serve davvero a chi scrive il driver:
         * dicono DOVE stanno i registri e in quale dei due spazi. */
        for (b = 0; b < 6; b++) {
            if (d->bar[b] == 0) continue;
            rap("    BAR%d           0x%08x  %s\n", b, d->bar[b],
                d->bar_io[b] ? "spazio I/O  (inb/outb, serve ioport_bind)"
                             : "memoria     (serve una mappatura)");
        }

        ha_cmd = (pci_leggi(pid, d, 0x04, &cmd) == 0);
        if (ha_cmd) {
            /* I bit si compongono in un buffer invece che con tre `%s`
             * consecutivi: separarli con lo spazio DENTRO ogni pezzo
             * lascia uno spazio penzolante quando l'ultimo bit e' spento,
             * e questo testo finisce in un documento che si legge. */
            char bit[40];
            bit[0] = '\0';
            if (cmd & PCI_ABIL_IO)        strcat(bit, "io");
            if (cmd & PCI_ABIL_MEMORIA) {
                if (bit[0]) strcat(bit, " ");
                strcat(bit, "memoria");
            }
            if (cmd & PCI_ABIL_BUSMASTER) {
                if (bit[0]) strcat(bit, " ");
                strcat(bit, "busmaster");
            }
            if (!bit[0]) strcpy(bit, "spento");

            rap("    comando        0x%04x  [%s]\n",
                (unsigned)(cmd & 0xFFFF), bit);
            rap("    stato          0x%04x\n", (unsigned)(cmd >> 16));
        }

        if (g == NULL) {
            rap("    DRIVER         DA SCRIVERE — modello non in tabella\n");
        } else if (g->driver == NULL) {
            rap("    DRIVER         DA SCRIVERE\n");
        } else if (g->driver[0] != '/') {
            rap("    DRIVER         %s\n", g->driver);
        } else if (driver_su_disco(g->driver)) {
            rap("    DRIVER         %s — presente su questo supporto\n",
                g->driver);
        } else {
            rap("    DRIVER         %s — SCRITTO ma non su questo supporto\n",
                g->driver);
        }
        if (g != NULL && g->nota != NULL)
            rap("    nota           %s\n", g->nota);
    }
}

/* -----------------------------------------------------------------------------
 * Il riassunto: la lista per cui questo programma esiste.
 * --------------------------------------------------------------------------- */
static void sezione_mancanti(void)
{
    int n, quanti = 0;

    rap("\n");
    rap("=============================================================\n");
    rap(" DA FARE — dispositivi senza driver\n");
    rap("=============================================================\n");

    for (n = 0; n < g_n_disp; n++) {
        const PciDispositivo *d = &g_disp[n];
        const Guida          *g = guida_di(d);
        const char           *vn;

        if (g != NULL && g->driver != NULL) {
            /* Scritto ma non su questo supporto: e' un altro problema —
             * si copia, non si scrive. Va detto lo stesso, separato. */
            if (g->driver[0] == '/' && !driver_su_disco(g->driver)) {
                rap("  %02x:%02x.%u  %s\n", d->bus, d->slot, d->funzione,
                    nome_classe(d->classe, d->sottoclasse));
                rap("            il driver %s ESISTE: va copiato, non scritto\n",
                    g->driver);
                quanti++;
            }
            continue;
        }

        vn = nome_venditore(d->venditore);
        rap("  %02x:%02x.%u  %s\n", d->bus, d->slot, d->funzione,
            nome_classe(d->classe, d->sottoclasse));
        rap("            id        %04x:%04x  %s\n",
            d->venditore, d->dispositivo, vn ? vn : "");
        rap("            classe    %02x.%02x.%02x\n",
            d->classe, d->sottoclasse, d->interfaccia);
        if (g != NULL && g->nota != NULL)
            rap("            %s\n", g->nota);
        quanti++;
    }

    if (quanti == 0)
        rap("  Nessuno: ogni dispositivo trovato ha il suo driver.\n");
}

/* -----------------------------------------------------------------------------
 * Cosa non e' stato guardato. Sta in fondo e non e' un dettaglio: un
 * inventario che tace su cio' che non ha guardato fa credere che non ci sia.
 * --------------------------------------------------------------------------- */
static void sezione_limiti(void)
{
    rap("\n");
    rap("=============================================================\n");
    rap(" COSA QUESTO RAPPORTO NON DICE\n");
    rap("=============================================================\n");
    rap("  Le fonti sono tre e tutte di sola lettura: CPUID, lo spazio di\n");
    rap("  configurazione PCI e la bitmap della memoria. Niente e' stato\n");
    rap("  sondato scrivendo. Percio' NON compaiono:\n");
    rap("\n");
    rap("  schede ISA      non hanno spazio di configurazione: si trovano\n");
    rap("                  solo scrivendo sulla loro porta di reset, e se\n");
    rap("                  a quell'indirizzo c'e' un'altra scheda le si\n");
    rap("                  scrive addosso. Vanno dichiarate a mano:\n");
    rap("                      /dev/ne2k.drv -p 0x300 -q 3\n");
    rap("\n");
    rap("  dischi ATA      le porte 0x1F0-0x1F7 le sta usando il kernel in\n");
    rap("                  questo istante. Il CONTROLLER si vede qui sopra\n");
    rap("                  fra i dispositivi PCI; i dischi attaccati li\n");
    rap("                  elenca `disk`.\n");
    rap("\n");
    rap("  porte seriali   0x3F8 e' la console su cui esce il log del\n");
    rap("                  kernel: riconoscerne una si fa scrivendo nel\n");
    rap("                  registro scratch, e sulla console attiva vuol\n");
    rap("                  dire sporcare l'unica traccia utile quando\n");
    rap("                  qualcosa va male.\n");
    rap("\n");
    rap("  USB             i dispositivi attaccati a un controller USB non\n");
    rap("                  stanno sul PCI: si vedono solo interrogando il\n");
    rap("                  controller, e serve prima uno stack USB.\n");
    rap("\n");
    rap("  dimensione      quanto e' larga la finestra di un BAR si misura\n");
    rap("  dei BAR         scrivendoci 0xFFFFFFFF e rileggendo. Per quel\n");
    rap("                  tempo il dispositivo decodifica un indirizzo\n");
    rap("                  assurdo — sul controller ATA vuol dire perdere\n");
    rap("                  il disco. La dimensione la sa chi scrive il\n");
    rap("                  driver: viene dalla scheda tecnica.\n");
}

static void intestazione(void)
{
    time_t     ora;
    struct tm *t;
    char       quando[32];

    rap("=============================================================\n");
    rap(" EX-OS — inventario dell'hardware\n");
    rap("=============================================================\n");

    ora = time(NULL);
    t   = localtime(&ora);
    if (t != NULL && strftime(quando, sizeof(quando), "%Y-%m-%d %H:%M:%S", t) > 0)
        rap("  generato il      %s\n", quando);
    rap("  generato da      hwinfo\n");
    rap("\n");
    rap("  A cosa serve: raccogliere in un posto solo i dati che servono a\n");
    rap("  scrivere un driver — identificatori su cui agganciarsi, classe,\n");
    rap("  dove rispondono i registri, su quale IRQ, e cosa il BIOS ha gia'\n");
    rap("  acceso. In fondo c'e' l'elenco di cio' che un driver non ce l'ha.\n");
}

static void uso(void)
{
    printf("uso: hwinfo [file] [-n] [-s]\n");
    printf("  (nessun argomento)  scrive %s e ripete a video\n",
           RAPPORTO_PREDEFINITO);
    printf("  file                scrive li' invece che in %s\n",
           RAPPORTO_PREDEFINITO);
    printf("  -n                  non scrive niente, stampa e basta\n");
    printf("  -s                  solo l'elenco di cio' che manca di driver\n");
    printf("\n");
    printf("  -s restringe il CONTENUTO, non spegne la scrittura: il file lo\n");
    printf("  scrive lo stesso, col solo elenco dentro. Per non scrivere: -n\n");
}

int main(int argc, char **argv)
{
    const char *percorso = RAPPORTO_PREDEFINITO;
    int         niente_file = 0, solo_riassunto = 0;
    int         i, pid;
    FILE       *f = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0)      niente_file = 1;
        else if (strcmp(argv[i], "-s") == 0) solo_riassunto = 1;
        else if (strcmp(argv[i], "-h") == 0) { uso(); return 0; }
        else if (argv[i][0] == '-') {
            printf("hwinfo: opzione sconosciuta '%s'\n", argv[i]);
            uso();
            return 2;
        } else percorso = argv[i];
    }

    dest_aggiungi(stdout);

    if (!niente_file) {
        f = fopen(percorso, "w");
        if (f == NULL) {
            /* ! NON E' FATALE. Avviando dal CD la radice e' di sola
             * lettura, e il rapporto a video vale comunque: rinunciare
             * del tutto perche' non si puo' scrivere sarebbe buttare via
             * l'unica cosa che si poteva ancora fare. */
            printf("hwinfo: non riesco a scrivere %s — proseguo solo a video\n",
                   percorso);
            printf("        (dal CD la radice e' in sola lettura: prova\n");
            printf("         `hwinfo /disk/hwinfo.txt` su un disco montato)\n\n");
        } else {
            dest_aggiungi(f);
        }
    }

    pid = assicura_pci();

    if (!solo_riassunto) {
        intestazione();
        sezione_cpu();
        sezione_memoria();
        sezione_pci(pid);
        sezione_mancanti();
        sezione_limiti();
    } else {
        /* ! ANCHE COL RIASSUNTO SI DEVE ENUMERARE: la lista di cio' che
         * manca si ricava dai dispositivi, non da una tabella. Ma
         * l'enumerazione non deve stampare, quindi si stacca l'uscita. */
        int salvati = g_n_dest;

        /* ! E SE IL PCI NON C'E' SI DICE, invece di proseguire. Con
         * l'uscita staccata il messaggio di sezione_pci() sparirebbe, e
         * sezione_mancanti() su zero dispositivi stamperebbe «nessuno:
         * ogni dispositivo ha il suo driver» — un via libera falso, che
         * e' il peggior modo di sbagliare per un programma che serve
         * proprio a dire cosa manca. */
        if (pid <= 0) {
            printf("hwinfo: il servizio PCI non e' disponibile, non posso\n");
            printf("        dire cosa manca. Serve /dev/pci.drv (c'e' sul\n");
            printf("        CD di EX-OS); avvialo con:  /dev/pci.drv &\n");
            if (f != NULL) fclose(f);
            return 1;
        }

        g_n_dest = 0;
        sezione_pci(pid);
        g_n_dest = salvati;
        sezione_mancanti();
    }

    if (f != NULL) {
        fclose(f);
        printf("\nhwinfo: rapporto scritto in %s\n", percorso);
    }
    return 0;
}

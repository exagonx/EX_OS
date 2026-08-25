/* =============================================================================
 * kernel/include/version.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Identità del sistema: nome, versione, autore, licenza.
 *
 * QUESTA È LA FONTE UNICA DI VERITÀ. Prima di luglio 2026 le stesse
 * informazioni erano duplicate in almeno cinque punti (due banner del
 * kernel, env_init/print_banner/cmd_uname della shell) più la sezione
 * [env] di /boot/kernel.cfg, che però non veniva letta da nessuno. Il
 * risultato era che modificare il file di configurazione non aveva alcun
 * effetto visibile, e il nome era scritto "ExOS" in un punto e "EX-OS"
 * ovunque altro.
 *
 * Ora esiste una sola stringa, `g_os_version`, composta a compile time da
 * queste macro. Kernel e programmi utente mostrano quella:
 *   - il kernel la stampa all'avvio;
 *   - i processi ring3 la ottengono con SYS_VERSION (vedi syscall.h), che
 *     la copia in un buffer utente — la shell la espone con `ver`/`version`.
 *
 * ---------------------------------------------------------------------------
 * REGOLA DI VERSIONAMENTO
 *
 *   EXOS_VERSION va incrementata di 0.001 a OGNI modifica del kernel.
 *
 * Formato "MAGGIORE.mmm" con tre decimali sempre presenti: 0.101 e non
 * 0.101000 né 0.1010. La versione precedente del progetto era "0.1", che
 * in questo schema si legge 0.100 — da lì la numerazione prosegue.
 *
 * È una stringa e non un numero di proposito: un float non rappresenta
 * 0.001 in modo esatto, e stamparlo richiederebbe aritmetica in virgola
 * mobile che questo kernel non usa. Dal 2026-08-02 la FPU viene
 * inizializzata e il suo stato salvato a ogni cambio di contesto (vedi
 * kernel/include/fpu.h), ma è per i programmi ring3: il kernel resta
 * senza virgola mobile e senza softfloat. Incrementarla è un'operazione
 * manuale e deliberata: chi tocca il kernel aggiorna la riga qui sotto.
 * ---------------------------------------------------------------------------
 * ============================================================================= */

#ifndef VERSION_H
#define VERSION_H

/* Nome breve e nome esteso */
#define EXOS_NAME       "EX OS"
#define EXOS_LONGNAME   "Extensible Operating System"

/* ▲ INCREMENTARE DI 0.001 A OGNI MODIFICA DEL KERNEL ▲ */
/* 0.176 -> 0.184: otto modifiche al kernel non ancora contate, dal 14 al 17
 * agosto 2026 — SYS_VIDEO_INFO, SYS_LOG, SPAWN_F_CONSOLE, il tetto di
 * mmio_map alzato a 16 MB, SYS_LIB_APRI con kernel/loader/lib.c, la guardia
 * su `kex` letto senza essere stato riempito, SpawnExtra ridotta a una
 * definizione sola (lib/include/spawn_abi.h) e il formato ELF salito in
 * elf.h perche' i lettori sono diventati due. */

/* 0.184 -> 0.202: diciotto modifiche al kernel rimaste non contate per
 * TREDICI commit, dal 17 al 20 agosto 2026. La riga qui sotto era ferma
 * mentre il kernel cambiava sotto, ed e' proprio il modo in cui un numero di
 * versione smette di voler dire qualcosa: non si aggiorna quando si aggiorna
 * il README, si aggiorna quando si tocca il kernel.
 *
 *   1  i permessi mordono: ext2 scrive il proprietario, vfs_permesso decide
 *   2  SYS_CHMOD e SYS_CHOWN
 *   3  memfun: memcpy e memset a otto byte per volta con MMX
 *   4  ipc_rimetti: un messaggio letto per sbaglio si rimette a posto
 *   5  il TSC, con la calibrazione (kernel/arch/x86/tsc.c)
 *   6  PSE: le pagine da 4 MB per la mappatura del framebuffer
 *   7  i pty (kernel/ipc/pty.c) e i gruppi di processi
 *   8  Ctrl+C che morde: il segnale arriva al gruppo in primo piano
 *   9  tre difetti in sched.h e shm.h trovati usando il sistema
 *  10  l'entropia (kernel/arch/x86/entropia.c), che e' servita a SSH
 *  11  aprire due volte una libreria non ne azzera piu' i dati (loader/lib.c)
 *  12  rename allineato a POSIX: la destinazione si sostituisce
 *  13  LIB_MAX da 4 a 12 — ed e' una cache di TUTTO il sistema, non per processo
 *  14  VfsStat non inizializzata: modo, uid e gid restavano quel che c'era
 *      nello stack sui rami ISO 9660, FAT12 e radice, e vfs_permesso DECIDE
 *      con modo
 *  15  SYS_STATPERM (253): modo, uid e gid di un percorso senza aprirlo
 *  16  la scia del cursore sulla console in modo grafico (arch/x86/vga.c)
 *  17  il recovery come root quando `login` non si apre (kernel_main.c), e
 *      sessantotto stringhe del kernel rese ASCII perche' il font della
 *      console e' indicizzato per BYTE
 *  18  SYS_SU (254) con SHA-256 dentro il kernel (kernel/crypto/sha256.c), e
 *      vfs_open_autorita() perche' il kernel non poteva leggere /boot/ombra
 */
/* 0.202 -> 0.203: SPAWN_F_UTENTE — spawn() sa far nascere il figlio con
 * l'identita' di un altro utente, e solo se chi chiama e' gia' root
 * (sys_spawn in kernel/syscall/syscall_impl.c, blocco EXTRA a 604 byte con
 * magia 'SPO0').
 *
 * ! LA MAGIA E' CAMBIATA, QUINDI OGNI PROGRAMMA VA RICOMPILATO: un binario
 * vecchio passa un blocco 'SPNZ', il kernel non lo riconosce e lo IGNORA —
 * cioe' perde redirezioni e ambiente, in silenzio. E' gia' successo il 14
 * agosto, e sta scritto in lib/include/spawn_abi.h.
 *
 * Serviva a `login`, che e' un ciclo: scendendo con setuid() per lanciare la
 * shell diventava lui stesso l'utente, e al primo `exit` non poteva piu'
 * leggere /boot/ombra — la console non faceva piu' entrare nessuno.
 */
/* 0.203 -> 0.204: sys_spawn capisce TUTTE le forme pubblicate del blocco EXTRA,
 * non solo l'ultima — 'SPNZ' (596 byte) accanto a 'SPO0' (604).
 *
 * ! LA 0.203 AVEVA ROTTO IL gcc CHE GIRA DENTRO EX-OS, e questo e' il difetto
 * che la voce corregge. Bumpando la magia, i binari compilati contro la libc
 * del 14 agosto — cioe' tutto il CD degli strumenti — passavano un blocco che
 * il kernel non riconosceva e IGNORAVA: partivano senza redirezioni e senza
 * ambiente, in silenzio. Il driver `gcc` redirige l'uscita di cc1 su un file
 * temporaneo: quell'uscita finiva a video.
 *
 * ! LA MAGIA DICE LA FORMA, NON «CAPISCO / NON CAPISCO». Il kernel legge tanti
 * byte quanti ne dichiara la magia, azzera i campi che quella forma non aveva
 * e spegne i bit di `flag` che allora non volevano dire niente. Ricostruire il
 * bersaglio resta la cosa giusta da fare, ma non e' piu' un'emergenza.
 *
 * ! E LA VERIFICA DEL PUNTATORE ADESSO E' DELLA MISURA GIUSTA: chiedere 604
 * byte leggibili a un blocco che ne ha 596 rifiuta un chiamante legittimo la
 * cui struttura finisce a ridosso di una pagina — una volta ogni mille, sul
 * programma sbagliato.
 */
/* 0.204 -> 0.205: i segni delle lettere accentate ridisegnati nel font della
 * console grafica (kernel/arch/x86/font8x16.c), 28 glifi.
 *
 * ! NON E' SOLO ESTETICA, E' LEGGIBILITA': nel font di partenza i segni stanno
 * DUE righe sopra la lettera mentre il punto della `i` ne sta una sola, quindi
 * `a` accentata sembrava una `a` normale con una macchia che galleggia sopra.
 * Adesso i segni stanno dove sta il punto della `i`, e il grave e l'acuto sono
 * un tratto continuo invece di una scaletta spezzata: a otto pixel di
 * larghezza due quadratini staccati non si leggono come un accento.
 *
 * ! E IL COMMENTO IN TESTA AL FONT DICEVA IL FALSO: «l'ordine e' Latin-1, non
 * CP437», mentre i dati sono sempre stati CP437 — come la tastiera, che lo
 * dichiara. Chi si fosse fidato di quella riga avrebbe "corretto" la tastiera
 * e rotto ogni tasto accentato.
 */
/* 0.205 -> 0.206: due cose, tutt'e due «il kernel tiene il posto, non decide».
 *
 * ! LE CONSOLE DIVENTANO CINQUE (VGA_N_CONSOLE, kernel/include/vga.h). Il
 * server a finestre si prendeva una delle quattro — la seconda — e chi
 * lavorava in testo si ritrovava con tre console invece di quattro senza
 * averlo chiesto. Adesso Alt+F1..F4 restano di chi scrive e la grafica sta
 * sulla quinta. Costa 4000 byte di BSS e una shell in piu'.
 * ! E DEVE COMBACIARE CON KBD_N_CONSOLE (drivers/kbd/kbd_proto.h): e' il
 * driver a tradurre Alt+Fn in un cambio di console, e se i due numeri
 * divergono l'ultima console esiste ma non ci si arriva.
 *
 * ! `lingua` IN [kernel] DI kernel.cfg (kernel/fs/cfg.c, cfg.h). La sceglie
 * l'installatore e la riconsegna `cfg_get_option`, esattamente come `keymap`:
 * il kernel non la usa per NIENTE — tradurre e' lavoro dei programmi, e ognuno
 * sa quali messaggi ha — ma sa dove sta scritta, cosi' che non ce ne siano
 * due.
 */
/* 0.206 -> 0.207: SYS_CONSOLE_GRAFICA (233) — chi tiene la console della
 * grafica, e quale sia.
 *
 * ! SERVE PERCHE' UNA CONSOLE SENZA NESSUNO SOPRA NON E' UN POSTO DOVE
 * ANDARE. La grafica sta sull'ultima console; finche' il server non c'e',
 * quella e' uno schermo nero con una shell che nessuno guarda, e portarcisi
 * con Alt+Fn vuol dire sparire nel nulla senza sapere come tornare. Adesso il
 * driver di tastiera chiede, e se non c'e' grafica quel tasto non fa niente.
 *
 * ! E LO STATO STA NEL KERNEL, NON NEL SERVER, apposta: quando il server muore
 * — anche ucciso — qualcuno deve poter dire «li' non c'e' piu' niente». Un
 * flag tenuto dal server morirebbe con lui e lascerebbe la porta aperta su una
 * stanza vuota. Il kernel ricontrolla da se' che chi l'ha presa sia vivo.
 *
 * Lo usa anche `exwin`, che con questa voce sa quando la grafica e' finita e
 * riporta l'utente alla console da cui era partito.
 */
#define EXOS_VERSION    "0.207"

/* Autore e contatto */
#define EXOS_AUTHOR     "Graziano Falcone"
#define EXOS_EMAIL      "exagonx@hotmail.com"

/* Licenza */
#define EXOS_LICENSE    "GPL 2.0"
#define EXOS_COPYRIGHT  "Copyright (C) 2025"

/* Architettura di destinazione */
#define EXOS_ARCH       "x86 32-bit"

/* =============================================================================
 * g_os_version — la variabile globale richiesta.
 *
 * Stringa unica e completa, pronta da stampare. Definita in
 * kernel/version.c, dichiarata qui, `const` perché nessuno deve poterla
 * modificare a runtime: è l'identità del sistema, non uno stato.
 * ============================================================================= */
extern const char g_os_version[];

/* Variante compatta su una riga sola, per il boot silenzioso
 * (verboseboot=0) e per i log, dove il blocco multiriga sarebbe rumore. */
extern const char g_os_version_short[];

#endif /* VERSION_H */

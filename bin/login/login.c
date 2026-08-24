/* =============================================================================
 * bin/login/login.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Chiede chi sei, e se la risposta torna avvia la shell.
 *
 * -----------------------------------------------------------------------------
 * ! E' UN CICLO, ED E' TUTTO IL PUNTO
 *
 * Fino ad agosto 2026 il kernel lanciava /bin/sh direttamente su ogni
 * console. `exit` faceva quello che deve fare — terminava la shell — e la
 * console restava morta: nessuno la riapriva, perche' nessuno stava
 * guardando. Su una macchina con quattro console bastavano quattro `exit`
 * per non avere piu' modo di dare un comando.
 *
 * Qui la shell e' un FIGLIO dentro un ciclo. Quando esce, il ciclo
 * ricomincia dalla richiesta di accesso. Non c'e' nessun caso speciale per
 * `exit`, nessun messaggio da intercettare, nessuna scorciatoia: uscire
 * dalla shell E' tornare al login, per come sono fatte le cose.
 *
 * ! QUESTO PROGRAMMA NON DEVE USCIRE MAI. Se uscisse, la console tornerebbe
 * morta esattamente come prima — e stavolta senza nemmeno la shell. Ogni
 * errore qui dentro si riferisce e si ricomincia; nessuno rende da main().
 *
 * -----------------------------------------------------------------------------
 * IL FILE DEGLI UTENTI
 *
 *     /boot/utenti   nome:uid:gid            0644, lo legge chiunque
 *     /boot/ombra    nome:sale:impronta      0600, solo root
 *
 * ! DUE FILE E NON UNO, ed e' il motivo per cui Unix ha /etc/passwd e
 * /etc/shadow separati. Con tutto in un file solo bisogna scegliere fra due
 * mali: leggibile da tutti — e allora chiunque si porta via le impronte e
 * prova candidati con comodo — oppure leggibile solo da root, e allora
 * NESSUNO PUO' PIU' TRADURRE UN uid IN UN NOME. Si e' visto subito: `id`
 * rispondeva «uid=1000(uid1000)» a un utente che si chiamava mario.
 *
 * I nomi sono pubblici perche' servono a chiunque; le impronte sono di root
 * perche' non servono a nessun altro.
 *
 * ! L'uid E' NEL FILE, dal 17 agosto 2026. Prima non c'era e non serviva:
 * login autenticava e basta, e la shell girava comunque da root. Adesso la
 * shell NASCE dell'utente — spawn_utente() — e per lanciarla bisogna sapere
 * chi e'.
 *
 * ! root E' uid 0 PER DEFINIZIONE, e non e' una convenzione di questo file: e'
 * il numero che vfs_permesso() lascia passare dappertutto. Un utente normale
 * comincia da 1000, come su ogni Unix, cosi' i numeri bassi restano liberi per
 * i conti di servizio che un giorno serviranno.
 *
 * `impronta` e' lo SHA-256 di "sale:password" in esadecimale. Il sale rende
 * diverse due impronte di password uguali e impedisce le tabelle
 * precalcolate. La password non tocca mai il disco.
 *
 * ! SHA-256 NON E' UNA FUNZIONE DI DERIVAZIONE DI CHIAVI, ed e' bene che
 * stia scritto anche qui: e' veloce per costruzione, quindi chi si porta via
 * questo file puo' provare molti candidati al secondo. Il sale non lo
 * rallenta. Un vero irrobustimento (molte iterazioni, o memoria dura) e' il
 * passo successivo, e cambiera' il formato di questo file.
 *
 * ! E IL FILE NON E' PROTETTO. EX-OS non ha ancora i permessi sui file:
 * chiunque puo' leggerlo, e chiunque puo' riscriverlo. L'autenticazione qui
 * serve a separare gli utenti e a non lasciare una macchina aperta a chi ci
 * si siede davanti, non a resistere a chi ha gia' un programma in esecuzione
 * sul sistema. Dirlo e' meglio che lasciarlo credere.
 * ============================================================================= */

#include "libc.h"
#include "kbd_proto.h"
#include "exuser.h"

/* +0.001 a ogni modifica: `login -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("login", "0.001");

#define UTENTI     "/boot/utenti"    /* nome:uid:gid — pubblico */
#define OMBRA      "/boot/ombra"     /* nome:sale:impronta — di root */
#define NOME_MAX   EXUSER_NOME_MAX
#define PASS_MAX   EXUSER_PASS_MAX

/* La shell da lanciare a chi entra: la si puo' cambiare da riga di comando. */
static char g_shell[128] = "/bin/sh";

/* ! LA CONSOLE SI RIVENDICA, NON SI EREDITA. Il driver di tastiera
 * consegna i tasti al processo dichiarato in primo piano su quella
 * console, e chi non si dichiara non riceve niente: il prompt compariva e
 * restava li' per sempre, senza un errore, perche' nessuno stava
 * ascoltando. La shell lo fa da sempre (sh_setfg in bin/sh/shell.c);
 * login, che il kernel lancia al suo posto, deve farlo per se'.
 *
 * Va rifatto anche DOPO ogni figlio: mentre gira la shell il primo piano
 * e' suo, e quando finisce va ripreso o la richiesta di accesso
 * successiva resterebbe muta. */
/* ! TUTTO CIO' CHE RIGUARDA L'ARCHIVIO DEGLI UTENTI E' PASSATO IN
 * lib/exuser IL 19 AGOSTO 2026, quando gli utenti sono diventati tre: questo
 * login, `install` che crea il primo conto sul disco che sta preparando, e
 * `su` che verifica per alzarsi a root. Il codice e' stato SPOSTATO, non
 * riscritto — riscrivere un archivio di password vuol dire rifare gli stessi
 * errori con numeri diversi.
 *
 * Qui restano soltanto le cose che sono di login e di nessun altro: la lista
 * di chi e' ammesso, l'avvio della shell, e il ciclo. */
/* =============================================================================
 * crea_utente — la stessa domanda, in due situazioni diverse
 *
 * ! `primo` DICE SE E' IL PRIMO CONTO DELLA MACCHINA, e non e' un dettaglio di
 * cortesia: la stessa funzione la usa l'avvio di un sistema nuovo, dove non
 * c'e' NESSUNO e senza un conto non si va avanti, e `login -a`, dove i conti
 * ci sono e se ne aggiunge uno.
 *
 * ! FINO AL 24 AGOSTO 2026 IL CARTELLO ERA UNO SOLO, e `login -a` su una
 * macchina piena di utenti annunciava «Sistema nuovo: non c'e' ancora nessun
 * utente». Il comportamento era giusto — l'uid lo sceglie exuser_prossimo_uid()
 * — ma il messaggio diceva il falso, e un messaggio falso e' peggio di un
 * messaggio assente: chi lo legge si chiede cosa e' successo all'archivio.
 * ============================================================================= */
static void crea_utente(int primo)
{
    char nome[NOME_MAX], p1[PASS_MAX], p2[PASS_MAX];

    if (primo) {
        printf("\n  Sistema nuovo: non c'e' ancora nessun utente.\n");
        printf("  Creane uno adesso: da qui in poi servira' per entrare.\n\n");
    } else {
        printf("\n  Nuovo utente.\n");
        printf("  Il nome dev'essere libero; il numero lo sceglie il sistema,\n");
        printf("  il primo dopo quelli gia' usati.\n\n");
    }

    for (;;) {
        printf("  nome utente: ");
        if (exuser_leggi_riga(nome, sizeof(nome)) <= 0) continue;
        if (!exuser_nome_valido(nome)) {
            printf("  Solo lettere, cifre e '_', al massimo %d caratteri.\n",
                   NOME_MAX - 1);
            continue;
        }

        printf("  password: ");
        if (exuser_leggi_password(p1, sizeof(p1)) < 0) {
            printf("\n  La tastiera non risponde in modalita' raw: non posso\n");
            printf("  chiedere una password senza mostrarla a schermo.\n");
            printf("  Serve /dev/kbd.drv: vedi [modules] in kernel.cfg.\n");
            sleep(3);
            continue;
        }
        if (p1[0] == '\0') {
            printf("  Una password vuota lascia la macchina aperta. Riprova.\n");
            continue;
        }

        printf("  ripetila:  ");
        if (exuser_leggi_password(p2, sizeof(p2)) < 0) continue;
        if (strcmp(p1, p2) != 0) {
            printf("  Le due non coincidono. Riprova.\n");
            continue;
        }

        /* ! IL PRIMO UTENTE E' root, uid 0, E GLI ALTRI PARTONO DA 1000. Non
         * e' una convenzione di comodo: 0 e' il numero che vfs_permesso()
         * lascia passare dappertutto, e chi crea il primo conto su una
         * macchina appena installata deve poterci fare tutto. I numeri fra 1 e
         * 999 restano liberi per i conti di servizio. */
        {
            unsigned int u = exuser_c_e_qualcuno(0) ? exuser_prossimo_uid(0) : 0u;
            int          r = exuser_aggiungi(0, nome, p1, u, u);

            if (r == 0) {
                printf("\n  Utente '%s' creato", nome);
                if (u == 0) printf(" (root)");
                else        printf(" (uid %u)", u);
                printf(".\n\n");
                return;
            }

            /* ! «C'E' GIA'» NON E' «NON SONO RIUSCITO A SCRIVERLO», e dirlo
             * con lo stesso messaggio manda a cercare un guasto nel disco. E
             * non si sovrascrive: cambiare la password di qualcuno non e' una
             * cosa che si fa per sbaglio digitando un nome. */
            if (r == -2) {
                printf("  '%s' esiste gia'. Scegli un altro nome.\n", nome);
                continue;
            }
        }
        printf("  Non sono riuscito a scriverlo. Riprovo.\n");
        sleep(2);
    }
}


/* ─────────────────────────────────────────────────────────────────────────────
 * main
 * ───────────────────────────────────────────────────────────────────────────── */

static char g_casa[128];

static void avvia_shell(const char *utente, unsigned int uid, unsigned int gid)
{
    char *argv[2];
    int   pid, stato;

    argv[0] = g_shell;
    argv[1] = NULL;

    /* L'utente entra nell'ambiente della shell: da li' lo legge `whoami`,
     * e ci si appoggera' il giorno che i file avranno un proprietario. */
    setenv("USER", utente, 1);

    /* =====================================================================
     * ! LA CASA SI CREA DA root E POI SI CONSEGNA, ed e' l'unico ordine che
     * funziona. /home appartiene a root con 0755: un utente normale non ci
     * puo' creare dentro, quindi non puo' farsi la propria directory. E se la
     * crea root senza consegnarla, il padrone ha una casa in cui non puo'
     * scrivere.
     *
     * Creare-e-consegnare e' esattamente quello che fa `adduser` su ogni Unix,
     * e qui si puo' fare perche' login e' root — e lo resta: vedi il blocco
     * qui sotto, dove a scendere e' la shell.
     *
     * ! SE ESISTE GIA' NON E' UN ERRORE — e' il caso normale dal secondo
     * accesso in poi — e nemmeno se il filesystem non ha i proprietari:
     * chown rende ENOSYS su FAT, e li' non c'e' niente da consegnare.
     * ===================================================================== */
    {
        char casa[128];

        /* ! /home E' IL CONTENITORE, LA CASA E' /home/<utente>. E root fa
         * eccezione con /root, come su ogni Unix: la sua casa deve stare fuori
         * da /home perche' /home puo' essere un volume a parte, montato dopo —
         * e l'amministratore deve poter entrare anche quando non c'e'. Le crea
         * tutt'e due `install`. */
        if (uid == 0) snprintf(casa, sizeof(casa), "/root");
        else          snprintf(casa, sizeof(casa), "/home/%s", utente);

        if (mkdir(casa, 0755) == 0) {
            if (uid != 0 && chown(casa, uid, gid) != 0 && errno != ENOSYS)
                printf("login: %s creata ma non consegnata (%s)\n",
                       casa, strerror(errno));
        }
        strncpy(g_casa, casa, sizeof(g_casa) - 1);
        g_casa[sizeof(g_casa) - 1] = '\0';
    }

    /* ! LA DIRECTORY LA SI PRENDE DA root E IL FIGLIO LA EREDITA. Farlo
     * attraversare all'utente non si puo' piu' — non siamo lui, e non dobbiamo
     * diventarlo — quindi il permesso si CONTROLLA invece di provarlo: una
     * casa che l'utente non puo' attraversare e' una casa da riparare, e dirlo
     * qui e' meglio di una shell che parte in una directory in cui non puo'
     * leggere. La crea `login` stesso qui sopra con 0755 e la consegna, quindi
     * il caso normale passa; ci si arriva se qualcuno le ha cambiato modo o
     * padrone. */
    if (g_casa[0]) {
        StatPerm sp;

        /* ! ATTRAVERSARE E' IL BIT x, NON r, e i tre insiemi vanno guardati
         * tutti: padrone, gruppo, tutti gli altri. Guardarne uno solo vuol
         * dire un avviso che compare su una casa perfettamente buona, e un
         * avviso che grida al lupo si impara a saltarlo. `modo == 0` e' il VFS
         * che dice «qui i permessi non esistono» — FAT, ISO 9660 — e li' non
         * c'e' niente da controllare. */
        if (statperm(g_casa, &sp) == 0 && sp.modo != 0 && uid != 0 &&
            !((sp.uid == uid       && (sp.modo & 0100)) ||
              (sp.gid == (unsigned short)gid && (sp.modo & 0010)) ||
                                      (sp.modo & 0001))) {
            printf("login: %s non e' attraversabile da %s: la shell parte\n",
                   g_casa, utente);
            printf("       da /. Si ripara con:  chown %s %s\n", utente, g_casa);
        } else if (chdir(g_casa) != 0) {
            printf("login: non riesco a entrare in %s (%s)\n",
                   g_casa, strerror(errno));
        }
        setenv("HOME", g_casa, 1);
    }

    /* =====================================================================
     * ! SCENDE LA SHELL, NON login — ED E' LA CORREZIONE DEL 24 AGOSTO 2026
     *
     * EX-OS non ha il bit setuid sui file, quindi un programma non puo'
     * alzarsi di privilegio: l'unica direzione possibile e' giu'. init resta
     * root, avvia login che resta root, login autentica e la shell nasce
     * dell'utente.
     *
     * ! PRIMA SCENDEVA login CON setuid() E POI LANCIAVA. Funzionava per un
     * accesso solo, e il difetto stava nella parola «ciclo»: dopo `exit`
     * questo processo era ormai l'utente, e /boot/ombra e' 0600 di root.
     * L'accesso successivo trovava sempre «Accesso non riuscito» — QUALUNQUE
     * password — e quella console non serviva piu' a nessuno. Con root non si
     * vedeva, perche' per uid 0 il setuid non si faceva nemmeno: e' il motivo
     * per cui il difetto e' sopravvissuto alle prove.
     *
     * ! E DA GIU' NON SI TORNA SU: non c'e' modo di rimediare dopo, per
     * costruzione. L'unica correzione possibile e' non scendere affatto —
     * spawn_utente() chiede al kernel un figlio di un altro utente, e la puo'
     * chiamare solo chi e' gia' root. Vedi lib/include/spawn_abi.h.
     * ===================================================================== */

    /* ! root CHE LANCIA root NON AVREBBE NIENTE DA CHIEDERE, e gli si passa
     * comunque l'identita': una strada sola vale piu' di un caso in meno da
     * eseguire, ed e' proprio il caso particolare — «se uid != 0» — che aveva
     * tenuto nascosto il difetto di prima. */
    pid = spawn_utente(g_shell, argv, environ, uid, gid);
    if (pid < 0) {
        printf("login: %s non si avvia come %s (%s)\n",
               g_shell, utente, strerror(errno));
        if (errno == EPERM) {
            printf("       login non e' root: l'accesso NON viene aperto.\n");
            printf("       Meglio nessun accesso che uno con i privilegi\n");
            printf("       sbagliati.\n");
        }
        sleep(3);
        return;
    }
    waitpid(pid, &stato, 0);
    exuser_prendi_console();     /* la shell l'aveva presa: se la riprende login */
}

/* -----------------------------------------------------------------------------
 * Chi puo' entrare — le liste che arrivano da chi ci ha lanciati
 *
 * ! LE FA RISPETTARE login E NON telnetd, perche' e' qui che si sa chi ha
 * bussato: quando il servitore decide che programma lanciare, un nome utente
 * non e' ancora stato battuto. E vale per QUALUNQUE chiamante — un domani un
 * server ssh passera' le stesse due liste alla stessa funzione.
 *
 * ! IL CONFRONTO E' SUL NOME INTERO, non su un pezzo: «mario» dentro
 * «mariolino» e' vero come sottostringa e falso come utente, e un controllo
 * d'accesso che si sbaglia in quel verso fa entrare chi non deve.
 *
 * ! E IL DINIEGO VINCE SUL PERMESSO. Chi scrive due liste che si contraddicono
 * si aspetta la lettura prudente, ed e' anche l'unica che permette «tutti
 * tranne root» senza elencare tutti gli altri.
 * --------------------------------------------------------------------------- */
static const char *g_concessi = 0;
static const char *g_negati    = 0;

static int in_lista(const char *lista, const char *nome)
{
    const char *p = lista;
    unsigned int n = (unsigned int)strlen(nome);

    if (lista == 0 || lista[0] == '\0') return 0;

    while (*p) {
        const char *inizio;
        unsigned int len;

        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        if (*p == '\0') break;

        inizio = p;
        while (*p && *p != ',') p++;

        len = (unsigned int)(p - inizio);
        while (len > 0 && (inizio[len-1] == ' ' || inizio[len-1] == '\t')) len--;

        if (len == n && strncmp(inizio, nome, n) == 0) return 1;
    }
    return 0;
}

/* Rende 1 se `nome` puo' entrare. Senza liste, chiunque abbia la password. */
static int utente_ammesso(const char *nome)
{
    if (g_negati && in_lista(g_negati, nome)) return 0;
    if (g_concessi && g_concessi[0]) return in_lista(g_concessi, nome);
    return 1;
}

int main(int argc, char **argv)
{
    char nome[NOME_MAX], pass[PASS_MAX];

    unsigned int uid = 0, gid = 0;
    int  i;

    for (i = 1; i < argc; i++) {
        /* =================================================================
         * ! «login -a» AGGIUNGE UN UTENTE E BASTA, e sta qui invece che in un
         * /bin/adduser a parte per una ragione sola: tutto cio' che serve —
         * leggere una password senza mostrarla, il sale, SHA-256, il formato
         * del file — e' gia' in questo binario. Un programma separato ne
         * sarebbe una seconda copia, e due copie della stessa cosa divergono
         * al primo cambiamento di formato.
         *
         * ! LO PUO' FARE SOLO root, e il controllo e' qui e non nel file:
         * /boot/utenti non e' protetto da permessi finche' non lo si crea con
         * i permessi giusti, e chiedere il privilegio a chi chiama e' il
         * controllo che non si puo' aggirare rinominando un file.
         * ================================================================= */
        if (strcmp(argv[i], "-a") == 0) {
            if (getuid() != 0) {
                printf("login -a: solo root puo' aggiungere un utente.\n");
                return 1;
            }
            exuser_prendi_console();
            crea_utente(0);          /* non e' il primo: i conti ci sono */
            return 0;
        }
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            g_concessi = argv[++i];             /* solo questi */
            continue;
        }
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            g_negati = argv[++i];               /* questi mai */
            continue;
        }
        if (argv[i][0] != '-') {
            strncpy(g_shell, argv[i], sizeof(g_shell) - 1);
            g_shell[sizeof(g_shell) - 1] = '\0';
        }
    }

    exuser_prendi_console();

    for (;;) {
        if (!exuser_c_e_qualcuno(0)) {
            crea_utente(1);           /* macchina nuova: non c'e' nessuno */
            continue;                 /* ora c'e': si passa dal login vero */
        }

        printf("\n");
        printf("  EX-OS - accesso\n\n");
        printf("  utente: ");
        if (exuser_leggi_riga(nome, sizeof(nome)) <= 0) continue;

        printf("  password: ");
        if (exuser_leggi_password(pass, sizeof(pass)) < 0) {
            printf("\n  Tastiera non disponibile in raw: non posso chiedere\n");
            printf("  una password senza mostrarla. Serve /dev/kbd.drv.\n");
            sleep(3);
            continue;
        }

        if (exuser_verifica(0, nome, pass, &uid, &gid)) {
            {
                /* ! IL CONTROLLO VIENE DOPO LA PASSWORD, ED E' VOLUTO. Fatto
                 * prima direbbe a chi prova quali nomi sono ammessi senza che
                 * ne conosca nemmeno uno: si vedrebbe subito la differenza fra
                 * «non sei nella lista» e «password sbagliata». Cosi' invece
                 * chi non e' ammesso paga lo stesso prezzo di chi sbaglia la
                 * password, e non impara niente. */
                if (!utente_ammesso(nome)) {
                    printf("\n  Accesso negato.\n");
                    sleep(2);
                    continue;
                }
                printf("\n");
                avvia_shell(nome, uid, gid);
                /* La shell e' finita: si ricomincia. E' `exit` che torna
                 * al login, senza che nessuno lo tratti come un caso a
                 * parte. Vedi il commento in testa al file. */
                continue;
            }
        }

        /* ! UN SOLO MESSAGGIO PER I DUE CASI. Dire "utente inesistente"
         * separatamente da "password sbagliata" regala a chi prova
         * l'elenco dei nomi validi: si scoprono i conti esistenti senza
         * indovinare una sola password.
         *
         * E la pausa non e' cortesia: senza, si possono provare migliaia
         * di password al minuto battendole da uno script. */
        printf("\n  Accesso non riuscito.\n");
        sleep(2);
    }

    /* Non raggiunto: vedi "questo programma non deve uscire mai". */
}

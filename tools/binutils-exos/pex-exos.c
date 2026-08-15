/* Utilities to execute a program in a subprocess (possibly linked by pipes
   with other subprocesses), and wait for it.  EX-OS specialization.

   Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* =============================================================================
 * PERCHE' ESISTE QUESTO FILE, al posto di pex-unix.c
 *
 * pex-unix.c e' costruito su fork(): il figlio nasce come copia del padre,
 * sistema i propri descrittori con dup2() e poi si sostituisce con execv().
 * EX-OS non ha fork — e non e' una mancanza da colmare, e' una scelta:
 * duplicare uno spazio di indirizzamento vuol dire copy-on-write, cioe'
 * condivisione di pagine con conteggio dei riferimenti, per poi buttare via
 * tutto un'istruzione dopo. Qui il figlio si crea gia' fatto, con
 *
 *     spawn_ex(percorso, argv, envp, redirezioni, n)
 *
 * dove le redirezioni si dichiarano PER PERCORSO — "il descrittore 1 del
 * figlio e' questo file" — invece che per descrittore gia' aperto.
 *
 * ! LA CONSEGUENZA E' CHE NON CI SONO PIPE, ed e' visibile da qui: la
 * tabella `funcs` ha NULL al posto di pipe/fdopenr/fdopenw. pex-common.c
 * se ne accorge da solo e passa alla modalita' a FILE TEMPORANEI, che
 * sapeva gia' fare per MSDOS: l'uscita di un comando finisce in un file, e
 * il comando dopo lo legge. Piu' lento di una pipe, identico nel risultato,
 * e nessuno dei due programmi si accorge della differenza.
 *
 * Il modello e' pex-msdos.c, non pex-unix.c. La differenza rispetto a
 * MSDOS e' che qui il figlio gira DAVVERO in parallelo: exec_child ritorna
 * un PID e wait aspetta, invece di eseguire tutto dentro exec_child.
 * ============================================================================= */

#include "pex-common.h"

#include <stdio.h>
#include <errno.h>
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* spawn_ex, SpawnRedir, waitpid: l'ABI di EX-OS. */
#include <libc.h>

/* Quanti file per volta puo' avere aperti un comando: standard input,
 * standard output, standard error. Tre bastano perche' e' esattamente cio'
 * che pex sa redirigere; chiederne di piu' vorrebbe dire che pex-common.c
 * e' cambiato, e allora va cambiato anche questo file. */
#define PEX_EXOS_FILE_COUNT 3

/* I descrittori finti partono da 10 per non poter essere scambiati con
 * quelli veri (0, 1, 2) che pex-common.c passa come STDIN_FILE_NO e
 * compagni. Stesso trucco di pex-msdos.c, e per la stessa ragione: qui un
 * "descrittore" e' solo un modo di riferirsi a un NOME di file, perche'
 * e' il nome cio' che serve a spawn_ex. */
#define PEX_EXOS_FD_OFFSET 10

struct pex_exos
{
  /* I nomi dei file, indirizzati come 10 + indice. NULL se lo slot e'
     libero. Sono copie: pex-common.c libera i propri. */
  char *files[PEX_EXOS_FILE_COUNT];
  /* Se lo slot e' stato aperto in scrittura con "aggiungi in coda". */
  int append[PEX_EXOS_FILE_COUNT];
};

static int pex_exos_open_read (struct pex_obj *, const char *, int);
static int pex_exos_open_write (struct pex_obj *, const char *, int, int);
static pid_t pex_exos_exec_child (struct pex_obj *, int, const char *,
				  char * const *, char * const *,
				  int, int, int, int,
				  const char **, int *);
static int pex_exos_close (struct pex_obj *, int);
static pid_t pex_exos_wait (struct pex_obj *, pid_t, int *, struct pex_time *,
			    int, const char **, int *);
static void pex_exos_cleanup (struct pex_obj *);

/* ! I TRE NULL NON SONO UN "DA FARE": sono la dichiarazione che questo
   sistema non ha pipe, ed e' cosi' che pex-common.c lo viene a sapere. */
const struct pex_funcs funcs =
{
  pex_exos_open_read,
  pex_exos_open_write,
  pex_exos_exec_child,
  pex_exos_close,
  pex_exos_wait,
  NULL, /* pipe    */
  NULL, /* fdopenr */
  NULL, /* fdopenw */
  pex_exos_cleanup
};

struct pex_obj *
pex_init (int flags, const char *pname, const char *tempbase)
{
  /* PEX_USE_PIPES si toglie qui e non lo si nega altrove: chi ci passa
     quel flag chiede una cosa che non c'e', e pex-common.c sa gia'
     ripiegare sui file temporanei se il flag non c'e'. Lasciarlo passare
     lo farebbe chiamare funcs->pipe, che e' NULL. */
  flags &= ~PEX_USE_PIPES;
  return pex_init_common (flags, pname, tempbase, &funcs);
}

/* Prende (o crea) la struttura privata attaccata all'oggetto pex. */
static struct pex_exos *
pex_exos_sysdep (struct pex_obj *obj)
{
  struct pex_exos *es = (struct pex_exos *) obj->sysdep;

  if (es == NULL)
    {
      int i;

      es = XNEW (struct pex_exos);
      for (i = 0; i < PEX_EXOS_FILE_COUNT; i++)
	{
	  es->files[i] = NULL;
	  es->append[i] = 0;
	}
      obj->sysdep = (void *) es;
    }

  return es;
}

/* Registra `name` e ritorna il descrittore finto che lo rappresenta. */
static int
pex_exos_registra (struct pex_obj *obj, const char *name, int append)
{
  struct pex_exos *es = pex_exos_sysdep (obj);
  int i;

  for (i = 0; i < PEX_EXOS_FILE_COUNT; i++)
    if (es->files[i] == NULL)
      {
	es->files[i] = xstrdup (name);
	es->append[i] = append;
	return i + PEX_EXOS_FD_OFFSET;
      }

  errno = EMFILE;
  return -1;
}

/* Il nome dietro a un descrittore finto, o NULL se non e' uno dei nostri. */
static const char *
pex_exos_nome (struct pex_obj *obj, int fd, int *append)
{
  struct pex_exos *es = (struct pex_exos *) obj->sysdep;
  int i = fd - PEX_EXOS_FD_OFFSET;

  if (es == NULL || i < 0 || i >= PEX_EXOS_FILE_COUNT)
    return NULL;

  if (append != NULL)
    *append = es->append[i];

  return es->files[i];
}

/* ! NON SI APRE NIENTE, e non e' una scorciatoia: spawn_ex vuole il
   PERCORSO del file da mettere sotto un descrittore del figlio, non un
   descrittore gia' aperto del padre. Aprirlo qui vorrebbe dire due
   processi sullo stesso handle del VFS, cioe' un conteggio di riferimenti
   che attraversa il confine fra processi — e una close() da una parte che
   sfila il file da sotto all'altra. */
static int
pex_exos_open_read (struct pex_obj *obj, const char *name,
		    int binary ATTRIBUTE_UNUSED)
{
  return pex_exos_registra (obj, name, 0);
}

static int
pex_exos_open_write (struct pex_obj *obj, const char *name,
		     int binary ATTRIBUTE_UNUSED, int append)
{
  return pex_exos_registra (obj, name, append);
}

static int
pex_exos_close (struct pex_obj *obj, int fd)
{
  struct pex_exos *es = (struct pex_exos *) obj->sysdep;
  int i = fd - PEX_EXOS_FD_OFFSET;

  /* Non c'e' niente da chiudere: il file non e' mai stato aperto (vedi
     sopra). Si libera il nome e basta. */
  if (es != NULL && i >= 0 && i < PEX_EXOS_FILE_COUNT && es->files[i] != NULL)
    {
      free (es->files[i]);
      es->files[i] = NULL;
      es->append[i] = 0;
      return 0;
    }

  return 0;
}

/* Aggiunge una redirezione, se `fd` non e' quello standard da ereditare.
   Ritorna 0, oppure -1 se il percorso non ci sta nel campo a lunghezza
   fissa di SpawnAzione. */
static int
pex_exos_redir (struct pex_obj *obj, SpawnRedir *red, int *n,
		int fd_figlio, int fd, int standard)
{
  const char *nome;
  int append = 0;

  if (fd == standard)
    return 0;			/* eredita quello del padre */

  nome = pex_exos_nome (obj, fd, &append);
  if (nome == NULL)
    return 0;			/* non e' uno dei nostri: niente da fare */

  if (strlen (nome) >= SPAWN_RED_PATH_MAX)
    return -1;

  red[*n].fd = fd_figlio;
  red[*n].percorso = nome;

  if (fd_figlio == 0)
    red[*n].flags = O_RDONLY;
  else if (append)
    red[*n].flags = O_WRONLY | O_CREAT | O_APPEND;
  else
    red[*n].flags = O_WRONLY | O_CREAT | O_TRUNC;

  (*n)++;
  return 0;
}

/* Cerca `nome` nelle directory di PATH, come farebbe execvp. Ritorna un
   percorso da liberare, o NULL se non lo trova — nel qual caso il
   chiamante prova il nome cosi' com'e', che e' cio' che fa ogni shell. */
static char *
pex_exos_cerca (const char *nome)
{
  const char *path = getenv ("PATH");
  const char *p;

  if (path == NULL || strchr (nome, '/') != NULL)
    return NULL;

  p = path;
  while (*p != '\0')
    {
      const char *fine = strchr (p, ':');
      size_t len = (fine != NULL) ? (size_t) (fine - p) : strlen (p);
      char *pieno;

      if (len > 0)
	{
	  pieno = XNEWVEC (char, len + strlen (nome) + 2);
	  memcpy (pieno, p, len);
	  pieno[len] = '/';
	  strcpy (pieno + len + 1, nome);

	  if (access (pieno, X_OK) == 0)
	    return pieno;

	  free (pieno);
	}

      if (fine == NULL)
	break;
      p = fine + 1;
    }

  return NULL;
}

static pid_t
pex_exos_exec_child (struct pex_obj *obj, int flags, const char *executable,
		     char * const * argv, char * const * env,
		     int in, int out, int errdes,
		     int toclose ATTRIBUTE_UNUSED,
		     const char **errmsg, int *err)
{
  SpawnRedir red[SPAWN_MAX_AZIONI];
  int n = 0;
  char *cercato = NULL;
  const char *da_lanciare = executable;
  int pid;

  /* ! PEX_STDERR_TO_STDOUT NON SI PUO' FARE, e vale la pena dire perche':
     "manda l'errore dove va l'uscita" e' una dup2 nel figlio, cioe' due
     descrittori sullo STESSO file aperto, con una posizione sola. Qui le
     redirezioni si dichiarano per percorso, quindi la stessa richiesta
     diventerebbe due aperture distinte dello stesso file, ognuna con la
     propria posizione: il secondo flusso riscriverebbe sopra il primo
     dalla cima. Meglio dirlo che farlo male. */
  if ((flags & PEX_STDERR_TO_STDOUT) != 0)
    {
      *err = ENOSYS;
      *errmsg = "PEX_STDERR_TO_STDOUT: EX-OS non ha dup2 fra processi";
      return (pid_t) -1;
    }

  if (pex_exos_redir (obj, red, &n, 0, in, STDIN_FILE_NO) != 0
      || pex_exos_redir (obj, red, &n, 1, out, STDOUT_FILE_NO) != 0
      || pex_exos_redir (obj, red, &n, 2, errdes, STDERR_FILE_NO) != 0)
    {
      *err = ENAMETOOLONG;
      *errmsg = "percorso di redirezione troppo lungo";
      return (pid_t) -1;
    }

  if ((flags & PEX_SEARCH) != 0)
    {
      cercato = pex_exos_cerca (executable);
      if (cercato != NULL)
	da_lanciare = cercato;
    }

  /* ! `env` NULL VUOL DIRE «EREDITA», NON «NESSUN AMBIENTE». pex_run()
     passa sempre NULL — solo pex_run_in_environment() mette qualcosa — e su
     Unix quel NULL fa scegliere execv() al posto di execve(), cioe' il
     figlio si tiene l'ambiente del padre. Girando NULL a spawn_ex() il
     figlio partiva invece con l'ambiente VUOTO, e nessuno lo diceva.

     Il prezzo si vedeva tutto in GCC, che parla ai propri stadi PROPRIO
     per variabili d'ambiente: GCC_EXEC_PREFIX (dove il driver e' stato
     davvero installato, che e' cio' su cui cc1 riloca gli header di
     sistema), COMPILER_PATH e LIBRARY_PATH per collect2, TMPDIR per i
     file intermedi. Senza, cc1 cercava gli header dove GCC era stato
     CONFIGURATO — /exos/... — invece che dove sta adesso, non trovava
     niente, e rispondeva «no include path in which to search for
     stdio.h»: un messaggio che parla di percorsi mentre il difetto e'
     l'ambiente. */
  pid = spawn_ex (da_lanciare, argv, env != NULL ? env : environ, red, n);

  if (cercato != NULL)
    free (cercato);

  if (pid < 0)
    {
      /* ! ERA `*err = -pid`, CON IL COMMENTO «le syscall ritornano -errno».
	 Vero per le syscall NUDE, falso per spawn_ex() della libc, che passa
	 da err_reg(): quella ritorna -1 e mette errno. Quindi -pid valeva 1,
	 cioe' EPERM, e QUALUNQUE fallimento — un cc1 non trovato, per dire —
	 usciva come «operazione non permessa». Il messaggio mandava a cercare
	 permessi che su EX-OS non esistono nemmeno. */
      *err = errno;
      *errmsg = "spawn";
      return (pid_t) -1;
    }

  return (pid_t) pid;
}

static pid_t
pex_exos_wait (struct pex_obj *obj ATTRIBUTE_UNUSED, pid_t child, int *status,
	       struct pex_time *time, int done ATTRIBUTE_UNUSED,
	       const char **errmsg, int *err)
{
  int stato = 0;
  int r;

  /* PEX_RECORD_TIMES non e' sostenibile: EX-OS non tiene il tempo di CPU
     per processo, e riportare zeri sarebbe peggio che dire di no —
     qualcuno ci costruirebbe sopra un profilo fatto di zeri. */
  if (time != NULL)
    memset (time, 0, sizeof (struct pex_time));

  r = waitpid ((int) child, &stato, 0);
  if (r < 0)
    {
      *err = -r;
      *errmsg = "waitpid";
      return (pid_t) -1;
    }

  /* pex-common.c legge lo stato con le macro di <sys/wait.h>: il codice di
     uscita sta negli otto bit alti, come su Unix. Lo compone gia' cosi'
     waitpid() di EX-OS. */
  if (status != NULL)
    *status = stato;

  return (pid_t) r;
}

static void
pex_exos_cleanup (struct pex_obj *obj)
{
  struct pex_exos *es = (struct pex_exos *) obj->sysdep;
  int i;

  if (es == NULL)
    return;

  for (i = 0; i < PEX_EXOS_FILE_COUNT; i++)
    if (es->files[i] != NULL)
      free (es->files[i]);

  free (es);
  obj->sysdep = NULL;
}

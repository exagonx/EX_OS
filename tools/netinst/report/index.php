<?php
/* =============================================================================
 * tools/netinst/report/index.php
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Riceve un referto da /bin/senderror e lo salva accanto a questo file.
 *
 *     senderror http://esempio.org/exos/netinst/report/ /SIS900.TXT
 *
 * -----------------------------------------------------------------------------
 * ! QUESTO E' UN POSTO DOVE CHIUNQUE PUO' SCRIVERE, e va trattato come tale.
 * Non c'e' modo di sapere chi manda: la chiave dentro senderror sta in un
 * binario che si distribuisce, quindi ferma uno scanner e non ferma nessun
 * altro. Le difese vere sono tre, e sono tutte tetti:
 *
 *   - quanto puo' essere grande un referto        (BYTE_MAX)
 *   - quanti referti possono esistere in tutto    (FILE_MAX)
 *   - quanti ne puo' mandare un indirizzo al giorno (AL_GIORNO)
 *
 * Nessuno dei tre impedisce a qualcuno di riempire lo spazio; tutti e tre
 * fanno in modo che ci metta tanto e che si veda.
 *
 * ! E IL NOME DEL FILE LO DECIDE QUESTO SCRIPT, NON CHI MANDA. Di quel che
 * arriva si tiene solo cio' che e' innocuo, e il resto lo si butta: il nome
 * definitivo comincia sempre con la data e finisce sempre per .txt. Un nome
 * che arriva da fuori e finisce dentro una fopen() e' il modo classico di
 * farsi scrivere un .php nella propria cartella.
 * ========================================================================== */

/* --- le manopole ---------------------------------------------------------- */
define('CHIAVE',    'exos');       /* la stessa che sta in senderror */
define('BYTE_MAX',  256 * 1024);   /* un referto piu' grande non esiste */
define('FILE_MAX',  500);          /* oltre, si rifiuta e lo si dice */
define('AL_GIORNO', 20);           /* per indirizzo */
define('DOVE',      __DIR__ . '/ricevuti');

header('Content-Type: text/plain; charset=utf-8');

function basta($codice, $messaggio) {
    http_response_code($codice);
    echo $messaggio . "\n";
    exit;
}

/* --- chi bussa ------------------------------------------------------------ */
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    /* ! UNA GET NON E' UN ERRORE, E' QUALCUNO CHE GUARDA. Rispondere con
     * istruzioni invece che con un 405 rende questo indirizzo utile anche a
     * chi ci arriva da un browser. */
    echo "EX-OS - punto di raccolta dei referti\n\n";
    echo "Si manda un referto cosi', dalla macchina che lo ha prodotto:\n";
    echo "  senderror " . (isset($_SERVER['HTTP_HOST'])
        ? 'http://' . $_SERVER['HTTP_HOST'] . $_SERVER['REQUEST_URI']
        : 'http://.../report/') . " /SIS900.TXT\n\n";
    echo "Il referto viaggia IN CHIARO e resta su questo server.\n";
    exit;
}

$chiave = isset($_POST['chiave']) ? $_POST['chiave'] : '';
if (!hash_equals(CHIAVE, $chiave))
    basta(403, "chiave sbagliata: questo non e' un posto dove scrivere.");

$testo = isset($_POST['testo']) ? $_POST['testo'] : '';
if ($testo === '')   basta(400, "referto vuoto: non salvo niente.");
if (strlen($testo) > BYTE_MAX)
    basta(413, "referto piu' lungo di " . BYTE_MAX . " byte: rifiutato.");

/* --- il nome, ripulito ---------------------------------------------------- */
$nome = isset($_POST['nome']) ? $_POST['nome'] : 'referto';
$nome = basename($nome);                       /* via ogni percorso */
$nome = preg_replace('/[^A-Za-z0-9._-]/', '_', $nome);
$nome = preg_replace('/\.+/', '.', $nome);     /* niente .. per nessuna via */
$nome = substr($nome, 0, 40);
if ($nome === '' || $nome[0] === '.') $nome = 'referto';
if (!preg_match('/\.txt$/i', $nome)) $nome .= '.txt';

/* --- dove ----------------------------------------------------------------- */
if (!is_dir(DOVE) && !@mkdir(DOVE, 0755, true))
    basta(500, "non riesco a creare la cartella dei referti.");

$quanti = count(glob(DOVE . '/*.txt'));
if ($quanti >= FILE_MAX)
    basta(507, "ci sono gia' " . FILE_MAX . " referti: fanne spazio.");

/* ! IL TETTO PER INDIRIZZO SI CONTA SUI NOMI, non con un database. I file
 * portano l'indirizzo nel nome, quindi contarli e' una glob: su una decina di
 * file al giorno costa niente, e non c'e' uno stato da tenere in piedi. */
$ip   = isset($_SERVER['REMOTE_ADDR']) ? $_SERVER['REMOTE_ADDR'] : '0.0.0.0';
$ipok = preg_replace('/[^0-9a-fA-F.:]/', '_', $ip);
$oggi = gmdate('Ymd');

if (count(glob(DOVE . "/{$oggi}-*-{$ipok}-*.txt")) >= AL_GIORNO)
    basta(429, "hai gia' mandato " . AL_GIORNO . " referti oggi: riprova domani.");

$finale = sprintf('%s-%s-%s-%s', $oggi, gmdate('His'), $ipok, $nome);
$perc   = DOVE . '/' . $finale;

/* --- l'intestazione, che vale quanto il referto --------------------------- */
$testa  = "# ricevuto  : " . gmdate('Y-m-d H:i:s') . " UTC\n";
$testa .= "# da        : " . $ipok . "\n";
$testa .= "# nome dato : " . $nome . "\n";
if (!empty($_POST['troncato']))
    $testa .= "# ! TRONCATO dalla macchina che l'ha mandato: manca la fine.\n";
$testa .= "# " . str_repeat('-', 60) . "\n\n";

if (@file_put_contents($perc, $testa . $testo, LOCK_EX) === false)
    basta(500, "non riesco a scrivere il referto.");

echo "salvato come " . $finale . "\n";
echo "byte " . strlen($testo) . ", referti in tutto " . ($quanti + 1) . "\n";

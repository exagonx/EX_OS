<?php
/* =============================================================================
 * tools/netinst/report/elenco.php
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * L'altra meta' di index.php: quello riceve i referti, questo li RILEGGE.
 *
 *     elenco.php?chiave=...              l'elenco: data, byte, nome
 *     elenco.php?chiave=...&file=NOME    il contenuto di uno
 *     elenco.php?chiave=...&cancella=NOME   lo toglie
 *
 * -----------------------------------------------------------------------------
 * ! LA CHIAVE NON STA QUI DENTRO, E NON PUO' STARCI. Questo file viaggia nel
 * repository pubblico insieme a tutto il resto: una chiave scritta qui la
 * leggerebbe chiunque abbia clonato il progetto, e leggere i referti vuol dire
 * leggere il MAC, l'hardware e i guasti delle macchine di chi li ha mandati.
 *
 * La chiave sta in un file accanto, `chiave.txt`, che NON e' nel repository e
 * arriva sul server per FTP da solo: la genera e la carica exagonx/referti.sh,
 * che se la ricorda in exagonx/server.cnf (fuori da git, permessi 600).
 *
 * ! E SE `chiave.txt` NON C'E', QUI NON SI FA NIENTE. Il caso predefinito -
 * cioe' quello di chiunque pubblichi il repository senza aver mai pensato ai
 * referti - e' una porta chiusa, non una aperta. Scrivere una chiave di
 * comodo nel codice e fidarsi che qualcuno la cambi e' il modo in cui le
 * chiavi di comodo restano in produzione per anni.
 *
 * ! SCRIVERE E LEGGERE SONO DUE CHIAVI DIVERSE APPOSTA. Quella di index.php
 * sta dentro il binario di senderror, che si distribuisce: vale quanto vale, e
 * serve solo a tenere lontano chi passa. Questa non e' mai stata distribuita.
 * ========================================================================== */

define('DOVE',      __DIR__ . '/ricevuti');
define('FILE_CHIAVE', __DIR__ . '/chiave.txt');
define('MOSTRA_MAX', 512 * 1024);   /* oltre, si tronca e lo si dice */

header('Content-Type: text/plain; charset=utf-8');

function basta($codice, $messaggio) {
    http_response_code($codice);
    echo $messaggio . "\n";
    exit;
}

/* --- la chiave ------------------------------------------------------------ */
$attesa = @file_get_contents(FILE_CHIAVE);
if ($attesa === false) basta(404, "non c'e' niente da vedere qui.");
$attesa = trim($attesa);
if ($attesa === '' || strlen($attesa) < 16)
    basta(404, "non c'e' niente da vedere qui.");

$data = isset($_GET['chiave']) ? $_GET['chiave'] : '';

/* ! hash_equals E NON ==, e la ragione e' il tempo. Un confronto che si ferma
 * al primo carattere diverso impiega di piu' quando i primi caratteri sono
 * giusti, e su molte richieste quella differenza si misura: una chiave si
 * indovina un carattere per volta. Questo confronta sempre tutto. */
if (!hash_equals($attesa, $data))
    basta(404, "non c'e' niente da vedere qui.");

/* --- un nome che arriva da fuori ------------------------------------------ */
function nome_pulito($n) {
    $n = basename($n);
    if (!preg_match('/^[A-Za-z0-9._-]+\.txt$/', $n)) return false;
    if (strpos($n, '..') !== false) return false;
    return $n;
}

/* --- cancella uno --------------------------------------------------------- */
if (isset($_GET['cancella'])) {
    $n = nome_pulito($_GET['cancella']);
    if ($n === false) basta(400, "nome non valido.");
    $p = DOVE . '/' . $n;
    if (!is_file($p)) basta(404, "non c'e': " . $n);
    if (!@unlink($p)) basta(500, "non riesco a cancellare " . $n);
    echo "cancellato " . $n . "\n";
    exit;
}

/* --- leggine uno ---------------------------------------------------------- */
if (isset($_GET['file'])) {
    $n = nome_pulito($_GET['file']);
    if ($n === false) basta(400, "nome non valido.");
    $p = DOVE . '/' . $n;
    if (!is_file($p)) basta(404, "non c'e': " . $n);
    $t = @file_get_contents($p, false, null, 0, MOSTRA_MAX);
    if ($t === false) basta(500, "non riesco a leggere " . $n);
    echo $t;
    if (filesize($p) > MOSTRA_MAX)
        echo "\n[...troncato a " . MOSTRA_MAX . " byte]\n";
    exit;
}

/* --- l'elenco ------------------------------------------------------------- */
/* ! ORDINATO DAL PIU' RECENTE, perche' il referto che interessa e' quello di
 * adesso: chi guarda ha appena chiesto a qualcuno di mandarlo. */
$v = @glob(DOVE . '/*.txt');
if ($v === false) $v = array();
usort($v, function ($a, $b) { return filemtime($b) - filemtime($a); });

echo "# referti: " . count($v) . "\n";
echo "# data                 byte  nome\n";
foreach ($v as $p) {
    printf("%s %8d  %s\n",
           gmdate('Y-m-d H:i:s', filemtime($p)), filesize($p), basename($p));
}

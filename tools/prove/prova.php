<?php
// tools/prove/prova.php - the PHP page of tools/prova_httpd.sh
//
// Answers one line: the PHP version, the method and the "nome" it got, the
// database extensions loaded, and a sum (the engine runs).
header('Content-Type: text/plain');
$m = $_SERVER['REQUEST_METHOD'];
$nome = $m === 'POST' ? ($_POST['nome'] ?? '') : ($_GET['nome'] ?? '');
$est = [];
foreach (['pdo_mysql', 'mysqli', 'Zend OPcache'] as $e) {
    if (extension_loaded($e)) $est[] = $e === 'Zend OPcache' ? 'opcache' : $e;
}
echo 'PHP ok ', PHP_VERSION, ' ', $m, ' nome=', htmlspecialchars($nome), ' ',
     implode(' ', $est), ' somma=', array_sum([1, 2, 3]), "\n";

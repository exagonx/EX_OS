/* =============================================================================
 * tools/exilla/prova-sqlite.c — SQLite dentro EX-OS come lo usa NSS (tappa 5)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Il softoken di NSS tiene le chiavi in un database SQLite e lo apre con DUE
 * connessioni sullo stesso file: una per leggere, una per le transazioni.
 * Qui si fa lo stesso, stampando l'errore di SQLite a ogni passo, cosi' se
 * qualcosa del sistema (lock, fsync, journal, stat) non va si vede dove.
 * Si costruisce e si esegue con tools/exilla/prova-sqlite.sh.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

static int g_no = 0;

static void ok(const char *cosa, int va, sqlite3 *db)
{
    printf("  %s  %s", va ? "[OK]" : "[NO]", cosa);
    if (!va && db) printf("  (%d: %s)", sqlite3_extended_errcode(db), sqlite3_errmsg(db));
    printf("\n");
    if (!va) g_no++;
}

static int esegui(sqlite3 *db, const char *sql)
{
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

static int conta(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st;
    int           n = -1;

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

int main(int argc, char **argv)
{
    const char *nome = argc > 1 ? argv[1] : "/disk/prova.db";
    int         fl = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    sqlite3    *lettore = NULL, *scrittore = NULL;

    printf("prova-sqlite: SQLite %s su %s\n", sqlite3_libversion(), nome);
    ok("apre la connessione che legge", sqlite3_open_v2(nome, &lettore, fl, NULL) == SQLITE_OK, lettore);
    sqlite3_busy_timeout(lettore, 1000);
    ok("CREATE TABLE fuori da una transazione",
       esegui(lettore, "CREATE TABLE IF NOT EXISTS uno (id PRIMARY KEY, a);"), lettore);
    ok("INSERT fuori da una transazione", esegui(lettore, "INSERT OR REPLACE INTO uno VALUES(1,'x');"), lettore);

    ok("apre la seconda connessione, per le transazioni",
       sqlite3_open_v2(nome, &scrittore, fl, NULL) == SQLITE_OK, scrittore);
    sqlite3_busy_timeout(scrittore, 1000);
    ok("la seconda vede la tabella della prima", conta(scrittore, "SELECT count(*) FROM uno;") == 1, scrittore);
    ok("BEGIN IMMEDIATE", esegui(scrittore, "BEGIN IMMEDIATE TRANSACTION;"), scrittore);
    ok("CREATE TABLE dentro la transazione",
       esegui(scrittore, "CREATE TABLE IF NOT EXISTS metaData (id PRIMARY KEY UNIQUE ON CONFLICT REPLACE, item1, item2);"),
       scrittore);
    ok("INSERT dentro la transazione", esegui(scrittore, "INSERT INTO metaData VALUES('password','a','b');"), scrittore);
    ok("COMMIT", esegui(scrittore, "COMMIT TRANSACTION;"), scrittore);
    ok("la prima vede quello che la seconda ha scritto",
       conta(lettore, "SELECT count(*) FROM metaData;") == 1, lettore);

    ok("BEGIN IMMEDIATE, di nuovo", esegui(scrittore, "BEGIN IMMEDIATE TRANSACTION;"), scrittore);
    ok("INSERT e ROLLBACK", esegui(scrittore, "INSERT INTO metaData VALUES('altro','c','d');") &&
                            esegui(scrittore, "ROLLBACK TRANSACTION;"), scrittore);
    ok("dopo il ROLLBACK la riga non c'e'", conta(lettore, "SELECT count(*) FROM metaData;") == 1, lettore);

    sqlite3_close(scrittore);
    sqlite3_close(lettore);

    ok("riaperto, i dati ci sono ancora",
       sqlite3_open_v2(nome, &lettore, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
       conta(lettore, "SELECT count(*) FROM metaData;") == 1, lettore);
    ok("integrity_check", conta(lettore, "SELECT count(*) FROM pragma_integrity_check WHERE integrity_check='ok';") == 1,
       lettore);
    sqlite3_close(lettore);

    /* La modalita' WAL, quella dei database di Firefox: il file -shm mappato
     * condiviso e scrivibile, e ftruncate sul -wal (kernel 0.233). */
    {
        char wal[256];
        sqlite3 *w = NULL, *w2 = NULL;
        int i, giri = 1;

        snprintf(wal, sizeof wal, "%s.wal.sqlite", nome);
        ok("WAL: apre", sqlite3_open_v2(wal, &w, fl, NULL) == SQLITE_OK, w);
        ok("WAL: journal_mode=WAL accettato",
           esegui(w, "PRAGMA journal_mode=WAL;") &&
           conta(w, "SELECT count(*) FROM pragma_journal_mode WHERE journal_mode='wal';") == 1, w);
        ok("WAL: CREATE e mille INSERT", esegui(w, "CREATE TABLE IF NOT EXISTS t(a INTEGER, b TEXT);"), w);
        for (i = 0; i < 1000 && giri; i++) {
            char q[96];
            snprintf(q, sizeof q, "INSERT INTO t VALUES(%d, 'riga numero %d');", i, i);
            giri = esegui(w, q);
        }
        ok("WAL: le mille righe", giri && conta(w, "SELECT count(*) FROM t;") == 1000, w);
        ok("WAL: una seconda connessione le vede",
           sqlite3_open_v2(wal, &w2, fl, NULL) == SQLITE_OK &&
           conta(w2, "SELECT count(*) FROM t;") == 1000, w2);
        ok("WAL: checkpoint con troncamento (ftruncate)",
           esegui(w, "PRAGMA wal_checkpoint(TRUNCATE);"), w);
        sqlite3_close(w2);
        sqlite3_close(w);
        ok("WAL: riaperto, integrity_check",
           sqlite3_open_v2(wal, &w, fl, NULL) == SQLITE_OK &&
           conta(w, "SELECT count(*) FROM t;") == 1000 &&
           conta(w, "SELECT count(*) FROM pragma_integrity_check WHERE integrity_check='ok';") == 1, w);
        sqlite3_close(w);
    }

    printf("prova-sqlite: %d NO\n", g_no);
    return g_no;
}

/* =============================================================================
 * tools/prove/connprova.c — MariaDB Connector/C, inside EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     connprova <host> <port>
 *
 * A C program against libmariadbclient.a (tools/mariadb-connector-c-exos):
 * connects as root, reads the server version, a prepared insert with two
 * parameters into prova.c, reads the rows back. One line per check:
 *   CONNC ok <what>   /   CONNC NO <what>   then "CONNC fine <failures>"
 * Built by tools/prova_mariadb.sh:
 *   i386-exos-gcc -O2 connprova.c -o connprova -I.../include/mariadb
 *       -lmariadbclient -lssl -lcrypto -lz -lzstd -lpthread
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql.h>

static int g_no;

static void esito(int ok, const char *cosa, MYSQL *m)
{
    if (ok) printf("CONNC ok %s\n", cosa);
    else { printf("CONNC NO %s: %s\n", cosa, m ? mysql_error(m) : "?"); g_no++; }
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    unsigned porta = argc > 2 ? (unsigned)atoi(argv[2]) : 3306;
    MYSQL *m = mysql_init(NULL);
    MYSQL_STMT *st;
    MYSQL_BIND b[2];
    MYSQL_RES *r;
    MYSQL_ROW riga;
    int id, n = 0;
    char nome[40];
    unsigned long lung;

    if (!mysql_real_connect(m, host, "root", NULL, NULL, porta, NULL, 0)) {
        esito(0, "connessione", m);
        printf("CONNC fine %d\n", g_no);
        return 1;
    }
    printf("CONNC versione %s (client %s)\n", mysql_get_server_info(m), mysql_get_client_info());
    esito(1, "connessione", m);
    esito(mysql_query(m, "create table if not exists prova.c (id int primary key, nome varchar(40)) engine=innodb") == 0,
          "create table", m);

    st = mysql_stmt_init(m);
    esito(st && mysql_stmt_prepare(st, "insert into prova.c values (?, ?)", 33) == 0, "prepare", m);
    memset(b, 0, sizeof b);
    b[0].buffer_type = MYSQL_TYPE_LONG;   b[0].buffer = &id;
    b[1].buffer_type = MYSQL_TYPE_STRING; b[1].buffer = nome; b[1].length = &lung;
    mysql_stmt_bind_param(st, b);
    for (id = 1; id <= 3; id++) {
        snprintf(nome, sizeof nome, "C%d", id);
        lung = strlen(nome);
        if (mysql_stmt_execute(st) == 0) n++;
    }
    esito(n == 3, "3 insert preparati", m);
    mysql_stmt_close(st);

    esito(mysql_query(m, "select group_concat(nome order by id) from prova.c") == 0
          && (r = mysql_store_result(m)) != NULL, "select", m);
    if (r && (riga = mysql_fetch_row(r)) && riga[0]) {
        printf("CONNC righe %s\n", riga[0]);
        esito(strcmp(riga[0], "C1,C2,C3") == 0, "righe lette", m);
    }
    if (r) mysql_free_result(r);
    mysql_close(m);
    printf("CONNC fine %d\n", g_no);
    return g_no != 0;
}

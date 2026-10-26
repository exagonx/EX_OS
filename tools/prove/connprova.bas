'' =============================================================================
'' tools/prove/connprova.bas - MariaDB from FreeBASIC (mariadb.bi), inside EX-OS
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
''
''     connprovafb <host> <port>
''
'' Connects as root, a table, rows in through a prepared statement (MYSQL_BIND,
'' whose layout must match the C library's), rows back with their field
'' names (MYSQL_FIELD, the same). One line per check: CONNFB ok/NO <what>,
'' then "CONNFB fine <failures>". Built by tools/prova_mariadb.sh.
'' =============================================================================
#include once "mariadb.bi"

dim shared as integer no

sub esito(byval ok as integer, byref cosa as string, byval db as MYSQL ptr)
    if ok then
        print "CONNFB ok " & cosa
    else
        print "CONNFB NO " & cosa & ": " & *mysql_error(db)
        no += 1
    end if
end sub

dim as string host = command(1)
if host = "" then host = "127.0.0.1"
dim as ulong porta = valuint(command(2))
if porta = 0 then porta = 3306

dim as MYSQL ptr db = mysql_init(0)
if mysql_real_connect(db, host, "root", "", "prova", porta, 0, 0) = 0 then
    esito(0, "connessione", db)
    print "CONNFB fine " & no
    end 1
end if
print "CONNFB versione " & *mysql_get_server_info(db) & " (client " & *mysql_get_client_info() & ")"
esito(1, "connessione", db)
esito(mysql_query(db, "create table if not exists fb (id int primary key, nome varchar(40)) engine=innodb") = 0, _
      "create table", db)

dim as MYSQL_STMT ptr st = mysql_stmt_init(db)
dim as zstring * 32 q = "insert into fb values (?, ?)"
esito(mysql_stmt_prepare(st, q, len(q)) = 0, "prepare", db)
dim as MYSQL_BIND b(0 to 1)
dim as long id
dim as zstring * 40 nome
dim as culong lung
b(0).buffer_type = MYSQL_TYPE_LONG : b(0).buffer = @id
b(1).buffer_type = MYSQL_TYPE_STRING : b(1).buffer = @nome : b(1).length = @lung
mysql_stmt_bind_param(st, @b(0))
dim as integer n = 0
for id = 1 to 3
    nome = "FB" & id
    lung = len(nome)
    if mysql_stmt_execute(st) = 0 then n += 1
next
esito(n = 3, "3 insert preparati", db)
mysql_stmt_close(st)

esito(mysql_query(db, "select id as numero, nome from fb order by id") = 0, "select", db)
dim as MYSQL_RES ptr r = mysql_store_result(db)
if r then
    dim as MYSQL_FIELD ptr f = mysql_fetch_fields(r)
    dim as string tutte, campi = *f[0].name & "," & *f[1].name
    dim as MYSQL_ROW riga = mysql_fetch_row(r)
    while riga
        if tutte <> "" then tutte &= ","
        tutte &= *riga[1]
        riga = mysql_fetch_row(r)
    wend
    print "CONNFB righe " & tutte & " campi " & campi
    esito(tutte = "FB1,FB2,FB3", "righe lette", db)
    esito(campi = "numero,nome" and f[1].type = MYSQL_TYPE_VAR_STRING, "MYSQL_FIELD", db)
    mysql_free_result(r)
end if
mysql_close(db)
print "CONNFB fine " & no
if no then end 1

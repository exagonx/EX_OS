'' =============================================================================
'' mariadb.bi - MariaDB and MySQL from FreeBASIC on EX-OS
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
''
''     #include "mariadb.bi"
''     dim as MYSQL ptr db = mysql_init(0)
''     if mysql_real_connect(db, "127.0.0.1", "root", "", "prova", 3306, 0, 0) = 0 then
''         print *mysql_error(db) : end 1
''     end if
''
'' The C API of Connector/C 3.4 (libmariadbclient.a, tools/mariadb-connector-c-exos)
'' as FreeBASIC declarations. The libraries come with the #inclib lines: a
'' program needs nothing more than `fbc prog.bas -x prog`.
''
'' ! NOT FreeBASIC'S OWN mysql/mysql.bi: that one is MySQL 4.0 (2003), its
'' MYSQL_FIELD has the wrong layout for a 3.4 library and it links
'' -lmysqlclient. Here MYSQL, MYSQL_RES and MYSQL_STMT are opaque (only
'' pointers to them), MYSQL_FIELD and MYSQL_BIND are those of mariadb_com.h
'' and mariadb_stmt.h of 3.4.11, field by field.
'' ! C's unsigned long is 4 bytes at 32 bit and 8 at 64: culong from crt/long.bi.
'' =============================================================================
#pragma once
#include once "crt/long.bi"

#inclib "mariadbclient"
#inclib "ssl"
#inclib "crypto"
#inclib "z"
#inclib "zstd"
#inclib "pthread"
#inclib "m"

extern "C"

type MYSQL as any
type MYSQL_RES as any
type MYSQL_STMT as any
type MYSQL_ROW as zstring ptr ptr
type my_bool as byte
type my_ulonglong as ulongint

enum enum_field_types
    MYSQL_TYPE_DECIMAL, MYSQL_TYPE_TINY, MYSQL_TYPE_SHORT, MYSQL_TYPE_LONG,
    MYSQL_TYPE_FLOAT, MYSQL_TYPE_DOUBLE, MYSQL_TYPE_NULL, MYSQL_TYPE_TIMESTAMP,
    MYSQL_TYPE_LONGLONG, MYSQL_TYPE_INT24, MYSQL_TYPE_DATE, MYSQL_TYPE_TIME,
    MYSQL_TYPE_DATETIME, MYSQL_TYPE_YEAR, MYSQL_TYPE_NEWDATE, MYSQL_TYPE_VARCHAR,
    MYSQL_TYPE_BIT, MYSQL_TYPE_TIMESTAMP2, MYSQL_TYPE_DATETIME2, MYSQL_TYPE_TIME2,
    MYSQL_TYPE_JSON = 245, MYSQL_TYPE_NEWDECIMAL = 246, MYSQL_TYPE_ENUM = 247,
    MYSQL_TYPE_SET = 248, MYSQL_TYPE_TINY_BLOB = 249, MYSQL_TYPE_MEDIUM_BLOB = 250,
    MYSQL_TYPE_LONG_BLOB = 251, MYSQL_TYPE_BLOB = 252, MYSQL_TYPE_VAR_STRING = 253,
    MYSQL_TYPE_STRING = 254, MYSQL_TYPE_GEOMETRY = 255
end enum

type MYSQL_FIELD
    name as zstring ptr
    org_name as zstring ptr
    table as zstring ptr
    org_table as zstring ptr
    db as zstring ptr
    catalog as zstring ptr
    def as zstring ptr
    length as culong
    max_length as culong
    name_length as ulong
    org_name_length as ulong
    table_length as ulong
    org_table_length as ulong
    db_length as ulong
    catalog_length as ulong
    def_length as ulong
    flags as ulong
    decimals as ulong
    charsetnr as ulong
    type as long                    '' an enum_field_types (a C enum is 4 bytes)
    extension as any ptr
end type

'' one parameter or result column of a prepared statement (struct st_mysql_bind)
type MYSQL_BIND
    length as culong ptr
    is_null as my_bool ptr
    buffer as any ptr
    error as my_bool ptr
    u as any ptr                    '' union { row_ptr; indicator }
    store_param_func as any ptr
    fetch_result as any ptr
    skip_result as any ptr
    buffer_length as culong
    offset as culong
    length_value as culong
    flags as ulong
    pack_length as ulong
    buffer_type as long             '' an enum_field_types
    error_value as my_bool
    is_unsigned as my_bool
    long_data_used as my_bool
    is_null_value as my_bool
    extension as any ptr
end type

declare function mysql_init(byval m as MYSQL ptr) as MYSQL ptr
declare function mysql_real_connect(byval m as MYSQL ptr, byval host as const zstring ptr, _
    byval user as const zstring ptr, byval passwd as const zstring ptr, _
    byval db as const zstring ptr, byval port as ulong, _
    byval unix_socket as const zstring ptr, byval flags as culong) as MYSQL ptr
declare sub mysql_close(byval m as MYSQL ptr)
declare function mysql_select_db(byval m as MYSQL ptr, byval db as const zstring ptr) as long
declare function mysql_query(byval m as MYSQL ptr, byval q as const zstring ptr) as long
declare function mysql_real_query(byval m as MYSQL ptr, byval q as const zstring ptr, byval length as culong) as long
declare function mysql_store_result(byval m as MYSQL ptr) as MYSQL_RES ptr
declare function mysql_use_result(byval m as MYSQL ptr) as MYSQL_RES ptr
declare sub mysql_free_result(byval r as MYSQL_RES ptr)
declare function mysql_fetch_row(byval r as MYSQL_RES ptr) as MYSQL_ROW
declare function mysql_fetch_lengths(byval r as MYSQL_RES ptr) as culong ptr
declare function mysql_fetch_field(byval r as MYSQL_RES ptr) as MYSQL_FIELD ptr
declare function mysql_fetch_fields(byval r as MYSQL_RES ptr) as MYSQL_FIELD ptr
declare function mysql_num_rows(byval r as MYSQL_RES ptr) as my_ulonglong
declare function mysql_num_fields(byval r as MYSQL_RES ptr) as ulong
declare function mysql_field_count(byval m as MYSQL ptr) as ulong
declare function mysql_affected_rows(byval m as MYSQL ptr) as my_ulonglong
declare function mysql_insert_id(byval m as MYSQL ptr) as my_ulonglong
declare function mysql_errno(byval m as MYSQL ptr) as ulong
declare function mysql_error(byval m as MYSQL ptr) as const zstring ptr
declare function mysql_sqlstate(byval m as MYSQL ptr) as const zstring ptr
declare function mysql_real_escape_string(byval m as MYSQL ptr, byval dest as zstring ptr, _
    byval src as const zstring ptr, byval length as culong) as culong
declare function mysql_get_server_info(byval m as MYSQL ptr) as const zstring ptr
declare function mysql_get_client_info() as const zstring ptr
declare function mysql_set_character_set(byval m as MYSQL ptr, byval csname as const zstring ptr) as long
declare function mysql_ping(byval m as MYSQL ptr) as long
declare function mysql_autocommit(byval m as MYSQL ptr, byval mode as my_bool) as my_bool
declare function mysql_commit(byval m as MYSQL ptr) as my_bool
declare function mysql_rollback(byval m as MYSQL ptr) as my_bool

declare function mysql_stmt_init(byval m as MYSQL ptr) as MYSQL_STMT ptr
declare function mysql_stmt_prepare(byval s as MYSQL_STMT ptr, byval q as const zstring ptr, byval length as culong) as long
declare function mysql_stmt_bind_param(byval s as MYSQL_STMT ptr, byval b as MYSQL_BIND ptr) as my_bool
declare function mysql_stmt_bind_result(byval s as MYSQL_STMT ptr, byval b as MYSQL_BIND ptr) as my_bool
declare function mysql_stmt_execute(byval s as MYSQL_STMT ptr) as long
declare function mysql_stmt_fetch(byval s as MYSQL_STMT ptr) as long
declare function mysql_stmt_store_result(byval s as MYSQL_STMT ptr) as long
declare function mysql_stmt_affected_rows(byval s as MYSQL_STMT ptr) as my_ulonglong
declare function mysql_stmt_error(byval s as MYSQL_STMT ptr) as const zstring ptr
declare function mysql_stmt_close(byval s as MYSQL_STMT ptr) as my_bool

end extern

'' mysql_stmt_fetch: no more rows
#define MYSQL_NO_DATA 100

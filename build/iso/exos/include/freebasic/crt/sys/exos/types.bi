'' =============================================================================
'' crt/sys/exos/types.bi — i tipi POSIX di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================
''
'' !! OGNI RIGA QUI DENTRO E' STATA CONFRONTATA CON lib/include/libc.h, e non
'' e' pignoleria. Un tipo sbagliato qui non da' un errore: da' numeri
'' sbagliati. E' la stessa famiglia del difetto che a EX-OS e' gia' costato
'' due giorni, quando `struct stat` e' cresciuta di 12 byte e i binari solo
'' ricollegati hanno cominciato a leggere i campi spostati.
''
'' !! E PER LO STESSO MOTIVO NON SI PUO' RIUSARE crt/sys/linux/types.bi.
'' Sembrerebbe la cosa ovvia — EX-OS e Linux x86 sono tutti e due ELF32 i386
'' — ma due tipi non coincidono:
''
''     time_t    EX-OS  longint (8 byte)   Linux x86  clong (4)
''     dev_t     EX-OS  ulong   (4)        Linux x86  ulongint (8)
''
'' time_t e' passato a 64 bit il 4 agosto 2026 per non finire nel 2038. Un
'' .bi che lo dichiarasse `clong` compilerebbe benissimo e darebbe date
'' assurde, senza un avviso da nessuna parte.
''
'' Corrispondenze usate (FreeBASIC -> C):
''     long     = int a 32 bit con segno
''     ulong    = unsigned int a 32 bit
''     clong    = il `long` del C (32 bit qui)
''     longint  = 64 bit con segno
'' =============================================================================

#ifndef __crt_sys_exos_types_bi__
#define __crt_sys_exos_types_bi__

#include once "crt/stddef.bi"
#include once "crt/long.bi"

'' typedef int                 pid_t;
type pid_t as long

'' typedef long                off_t;
''
'' !! 32 BIT CON SEGNO, E RESTA COSI' DI PROPOSITO: e' la larghezza che ha
'' lseek(), cioe' la syscall sotto. Dichiararlo a 64 bit non renderebbe piu'
'' grandi i file, renderebbe solo silenziosa la troncatura al confine col
'' kernel. Il limite di 2 GB per file e' vero, e va detto.
type off_t as clong

'' typedef unsigned int        mode_t;
type mode_t as ulong

'' typedef unsigned int        dev_t;
type dev_t as ulong

'' typedef unsigned int        ino_t;
type ino_t as ulong

'' typedef unsigned int        nlink_t;
type nlink_t as ulong

'' typedef unsigned int        blksize_t;
type blksize_t as ulong

'' typedef unsigned int        blkcnt_t;
type blkcnt_t as ulong

'' typedef unsigned int        useconds_t;
type useconds_t as ulong

'' EX-OS non ha utenti ne' gruppi: questi due esistono perche' `struct stat`
'' ha i campi (a zero) e chi li stampa deve poterli dichiarare.
'' typedef unsigned int        uid_t;
'' typedef unsigned int        gid_t;
type uid_t as ulong
type gid_t as ulong

'' !! ssize_t NON SI DICHIARA QUI: lo definisce gia' crt/stddef.bi, e
'' rifarlo da «Duplicated definition». Stessa regola per size_t.

'' typedef long long time_t;
''
'' !! OTTO BYTE. Vedi la nota in testa: e' la differenza con Linux x86 che
'' rende impossibile riusare i suoi header.
type time_t as longint

'' typedef long clock_t;
type clock_t as clong

'' struct timeval { time_t tv_sec; long tv_usec; }
type timeval
	tv_sec as time_t
	tv_usec as clong
end type

#endif '' __crt_sys_exos_types_bi__

'' =============================================================================
'' crt/exos/time.bi — <time.h> di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================
'' Confrontato riga per riga con lib/include/libc.h.
'' =============================================================================

#ifndef __crt_exos_time_bi__
#define __crt_exos_time_bi__

#include once "crt/long.bi"
#include once "crt/sys/types.bi"

'' #define CLOCKS_PER_SEC  100
''
'' !! CENTO, NON UN MILIONE. E' il tick del PIT: clock() conta i tick, non i
'' microsecondi. Su Linux la stessa macro vale 1000000, e un programma che
'' divide per il valore sbagliato non si accorge di niente — stampa tempi
'' sbagliati di quattro ordini di grandezza.
#define CLOCKS_PER_SEC 100l

'' struct tm di EX-OS: NOVE campi e basta.
''
'' !! Linux ne ha undici — __tm_gmtoff e __tm_zone in coda — e quella
'' differenza non e' innocua: chi dichiara un `tm` e lo passa a mktime()
'' passerebbe una struttura piu' corta di quella che la libc crede di
'' ricevere. Qui i campi sono quelli che ci sono davvero.
type tm
	tm_sec as long      '' 0..60
	tm_min as long      '' 0..59
	tm_hour as long     '' 0..23
	tm_mday as long     '' 1..31
	tm_mon as long      '' 0..11 — gennaio e' ZERO
	tm_year as long     '' anni dal 1900
	tm_wday as long     '' 0..6, domenica = 0
	tm_yday as long     '' 0..365
	tm_isdst as long    '' sempre 0
end type

'' !! LA RISOLUZIONE VERA E' 10 ms, il tick del PIT: tv_nsec e' sempre un
'' multiplo di 10 000 000. La struttura ha i nanosecondi perche' cosi' e'
'' fatta, non perche' li sappiamo misurare.
type timespec
	tv_sec as time_t
	tv_nsec as clong
end type

extern "C"

'' !! QUI CI VA SOLO CIO' CHE crt/time.bi NON DICHIARA GIA'. Quel file, subito
'' dopo aver incluso questo, dichiara da se' clock, time, difftime, mktime,
'' asctime, ctime, gmtime, localtime e strftime: ripeterle da «Duplicated
'' definition» su ognuna. Alla piattaforma restano i TIPI e le funzioni che
'' il C standard non ha.
declare function gettimeofday (byval as timeval ptr, byval as any ptr = 0) as long

end extern

'' !! NON CI SONO gmtime_r, localtime_r, asctime_r, ctime_r, timegm,
'' timelocal, dysize, __tzname, __daylight, __timezone. Il .bi di Linux le
'' dichiara tutte; qui non esistono nella libc, e dichiararle darebbe
'' programmi che compilano e non si collegano, con un messaggio su un
'' simbolo che nessuno ha mai scritto. Un header che promette e' peggio di
'' un header assente.

#endif '' __crt_exos_time_bi__

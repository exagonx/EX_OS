/* =============================================================================
 * src/rtlib/exos/drv_intl.c  (da tools/freebasic-exos/exos/)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: LGPL-2.1-or-later   (parte della runtime di FB)
 * =============================================================================
 *
 * Le convenzioni locali: separatori, formati di data e ora, nomi dei mesi
 * e dei giorni.
 *
 * ! SONO QUELLE DELLA LOCALE "C", E NON C'E' NIENTE DA INTERROGARE. La
 * versione unix/ di questi file chiama nl_langinfo() e legge cosa dice il
 * sistema; EX-OS ha una locale sola — vedi setlocale() in lib/libc.c — e
 * quindi la risposta e' nota a priori. Fingere una localizzazione che non
 * c'e' (leggendo una variabile d'ambiente, per dire) darebbe nomi
 * italiani a un sistema che poi ordina le stringhe come ASCII.
 *
 * ! LA DATA E' ISO 8601 (yyyy-mm-dd) E NON L'AMERICANA. La locale "C" di
 * POSIX direbbe %m/%d/%y, che e' ambiguo per chiunque non sia negli Stati
 * Uniti: il 03/04 e' il 3 aprile o il 4 marzo? EX-OS scrive le date in
 * modo ISO dappertutto (`ls`, `chkdsk`), e la runtime deve dire la stessa
 * cosa del resto del sistema — una data ambigua e' peggio di una data in
 * una lingua che non e' la propria.
 * ============================================================================= */

#include "../fb.h"
#include "../fb_datetime.h"

static const char *g_mesi[12] = {
	"January", "February", "March",     "April",   "May",      "June",
	"July",    "August",   "September", "October", "November", "December"
};

static const char *g_mesi_brevi[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ! L'INDICE 1 E' LA DOMENICA, non il lunedi': e' la convenzione di
 * WEEKDAY in BASIC, e cambiarla qui sposterebbe di un giorno ogni
 * programma che la usa. */
static const char *g_giorni[7] = {
	"Sunday", "Monday", "Tuesday", "Wednesday",
	"Thursday", "Friday", "Saturday"
};

static const char *g_giorni_brevi[7] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

const char *fb_DrvIntlGet( eFbIntlIndex index )
{
	switch( index ) {
	case eFIL_DateDivider:          return "-";   /* ISO 8601, vedi sopra */
	case eFIL_TimeDivider:          return ":";
	case eFIL_NumDecimalPoint:      return ".";
	case eFIL_NumThousandsSeparator: return ",";
	}
	return NULL;
}

/* Le due che riempiono un buffer ritornano la lunghezza scritta, 0 se non
 * ci stava: il chiamante distingue cosi' senza dover indovinare. */
int fb_DrvIntlGetDateFormat( char *buffer, size_t len )
{
	static const char fmt[] = "yyyy-MM-dd";

	if( buffer == NULL || len < sizeof( fmt ) ) return 0;
	memcpy( buffer, fmt, sizeof( fmt ) );
	return (int)( sizeof( fmt ) - 1 );
}

int fb_DrvIntlGetTimeFormat( char *buffer, size_t len )
{
	static const char fmt[] = "HH:mm:ss";

	if( buffer == NULL || len < sizeof( fmt ) ) return 0;
	memcpy( buffer, fmt, sizeof( fmt ) );
	return (int)( sizeof( fmt ) - 1 );
}

static FBSTRING *stringa_temp( const char *s )
{
	size_t    n = strlen( s );
	FBSTRING *r = fb_hStrAllocTemp( NULL, (ssize_t)n );

	if( r != NULL ) memcpy( r->data, s, n + 1 );
	return r;
}

FBSTRING *fb_DrvIntlGetMonthName( int month, int short_name )
{
	if( month < 1 || month > 12 ) return NULL;
	return stringa_temp( short_name ? g_mesi_brevi[month - 1]
	                                : g_mesi[month - 1] );
}

FBSTRING *fb_DrvIntlGetWeekdayName( int weekday, int short_names )
{
	if( weekday < 1 || weekday > 7 ) return NULL;
	return stringa_temp( short_names ? g_giorni_brevi[weekday - 1]
	                                 : g_giorni[weekday - 1] );
}

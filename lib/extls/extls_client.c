/* =============================================================================
 * lib/extls/extls_client.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * TLS 1.3, dalla parte del cliente: il record e la stretta di mano
 *
 * ! QUESTO FILE NON INVENTA NESSUNA CRITTOGRAFIA, e non deve. X25519,
 * ChaCha20 e Poly1305 stanno in lib/excrypt e sono provati contro OpenSSL da
 * quando c'e' sshd; SHA-256 lo da' la libc; HMAC, HKDF e RSA-PSS stanno
 * accanto, in extls_kdf.c e extls_pss.c; la catena dei certificati e' di
 * lib/excert. Qui c'e' soltanto l'ORDINE in cui quelle cose si chiamano — che
 * e' esattamente cio' che TLS 1.3 e', e anche il posto dove si sbaglia.
 *
 * -----------------------------------------------------------------------------
 * ! LA TRASCRIZIONE E' UN BUFFER, NON UN'IMPRONTA CHE CRESCE, e la ragione e'
 * che la nostra SHA-256 e' in un colpo solo: non c'e' un contesto da alimentare
 * un pezzo per volta. Quindi si tengono i messaggi di stretta uno dietro
 * l'altro e si rifa' l'impronta le quattro volte che serve. Su dodici
 * kilobyte, quattro SHA-256 sono niente; una seconda implementazione di
 * SHA-256 con l'API incrementale sarebbe stata la cosa piu' cara di tutto il
 * file.
 *
 * ! E QUELLO STESSO BUFFER E' ANCHE IL LETTORE DEI MESSAGGI. Un messaggio di
 * stretta puo' stare a cavallo di due record e un record puo' contenerne tre:
 * tenere i byte in fila e leggerli con un cursore fa sparire tutt'e due i casi
 * — che altrimenti sono due difetti che compaiono solo con certe catene di
 * certificati, cioe' solo su certi siti.
 *
 * -----------------------------------------------------------------------------
 * ! IL NONCE SI COSTRUISCE, NON SI TRASMETTE. In TLS 1.3 il numero di sequenza
 * non e' nel record: e' un contatore che le due parti tengono a mente e che
 * si mette in XOR con l'IV. Sbagliarne il verso — big endian, allineato a
 * DESTRA dentro dodici byte — da' «bad record mac» al primo record e nessuna
 * indicazione di dove guardare.
 *
 * ! E IL TIPO VERO DEL RECORD STA IN FONDO AL TESTO IN CHIARO, dopo il
 * riempimento di zeri. Fuori c'e' sempre 23 (application_data), che e' il modo
 * in cui 1.3 nasconde a chi guarda persino di che messaggio si tratta.
 * ============================================================================= */

#include "extls.h"
#include "excrypt.h"
#include "excurva.h"

/* Traccia di servizio: si accende solo compilando con -DEXTLS_TRACCIA, e serve
 * a confrontare i segreti con il keylog di OpenSSL quando una stretta non va. */
#ifdef EXTLS_TRACCIA
extern int printf(const char *, ...);
static void traccia(const char *nome, const unsigned char *b, unsigned int n)
{
    unsigned int i;
    printf("%s ", nome);
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}
#else
#define traccia(a,b,c) ((void)0)
#endif

/* ! NIENTE libc QUI DENTRO, come in tutti i mattoni dell'https: questo file si
 * compila anche sull'host, dove la prova lo mette a parlare con OpenSSL. Le
 * tre funzioni che servirebbero sono tre righe. */
static void bcopia(void *d, const void *s, unsigned int n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    unsigned int i;
    for (i = 0; i < n; i++) a[i] = b[i];
}

/* =============================================================================
 * A CHE PUNTO E' LA STRETTA — il perche' sta in extls.h
 * ========================================================================== */
static ExTlsPasso g_passo      = 0;
static void      *g_passo_dato = 0;

void extls_passo_metti(ExTlsPasso f, void *dato)
{
    g_passo      = f;
    g_passo_dato = dato;
}

const char *extls_passo_nome(int passo)
{
    switch (passo) {
    case EXTLS_P_CHIAVE:      return "preparo la chiave";
    case EXTLS_P_HELLO:       return "il server ha risposto";
    case EXTLS_P_SEGRETO:     return "concordo il segreto";
    case EXTLS_P_CERTIFICATI: return "leggo i certificati";
    case EXTLS_P_FIRMA:       return "controllo la firma";
    case EXTLS_P_CATENA:      return "verifico la catena";
    case EXTLS_P_FATTO:       return "connessione cifrata";
    default:                  return "";
    }
}

/* Rende 0 se chi ospita ha detto di smettere. */
static int passo(int quale)
{
    if (!g_passo) return 1;
    return g_passo(g_passo_dato, quale) ? 1 : 0;
}

static void bzero_(void *d, unsigned int n)
{
    unsigned char *a = (unsigned char *)d;
    unsigned int i;
    for (i = 0; i < n; i++) a[i] = 0;
}

static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s && s[n]) n++;
    return n;
}

/* =============================================================================
 * Le misure
 *
 * ! IL RECORD DI TLS HA UN TETTO DICHIARATO — 16384 byte di testo in chiaro —
 * e non e' un numero scelto da noi: e' nello standard, e un record piu' grande
 * e' un record da rifiutare, non un buffer da allargare.
 * ========================================================================== */
#define REC_CHIARO    16384u
#define REC_MAX       (REC_CHIARO + 256u)   /* + tag e riempimento */

/* Davanti al record ci stanno l'AAD e il suo riempimento a 16: cosi' il dato
 * su cui Poly1305 lavora e' gia' contiguo e non serve una seconda copia. */
#define AVANTI        16u
#define DOPO          32u                   /* riempimento + le due lunghezze */

/* ! LA TRASCRIZIONE HA UN TETTO, E LO DICE QUANDO LO TOCCA. Una catena di
 * certificati lunga e' l'unica cosa che ci si avvicina: dodici kilobyte
 * bastano a tutte quelle che si incontrano, e una piu' grande riceve
 * EXTLS_ERR_SPAZIO — un errore, non un troncamento silenzioso, che nella
 * verifica di una firma vorrebbe dire accettare un dialogo diverso da quello
 * avvenuto. */
#define TRASCR_MAX    16384u

typedef struct {
    unsigned char chiave[32];
    unsigned char iv[12];
    unsigned long long seq;
    int           attiva;
} Direzione;

typedef struct {
    const ExTlsSotto *sotto;
    void (*casuale)(unsigned char *, unsigned int);

    Direzione scrivo;
    Direzione leggo;

    /* Il segreto da cui si derivano le chiavi applicative, tenuto per il
     * ricambio di chiave (KeyUpdate) che un server puo' chiedere. */
    unsigned char s_app[EXTLS_IMPRONTA];
    unsigned char c_app[EXTLS_IMPRONTA];

    unsigned char trascr[TRASCR_MAX];
    unsigned int  trascr_n;      /* quanti byte di stretta ci sono */
    unsigned int  hs_off;        /* dove e' arrivato il lettore di messaggi */

    /* Il record in arrivo, gia' decifrato: `pos` e `fine` sono la finestra di
     * dati applicativi non ancora consegnati a chi legge. */
    unsigned char bin[AVANTI + REC_MAX + DOPO];
    unsigned int  pos, fine;
    unsigned int  tipo;          /* il tipo del record in `bin` */

    unsigned char bout[AVANTI + REC_MAX + DOPO];

    int chiuso;
    unsigned int allarme;   /* l'ultimo codice di allarme ricevuto */
    int          ultimo;    /* l'ultimo errore, per chi legge dopo la stretta */
    int          motivo;    /* il codice di lib/excert, quando la catena cade */
    unsigned int anello;    /* e QUALE anello: 0 e' il certificato del sito */
} Tls;

unsigned int extls_misura(void) { return (unsigned int)sizeof(Tls); }

/* =============================================================================
 * Numeri in ordine di rete
 * ========================================================================== */
static unsigned int be16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | p[1];
}

static unsigned int be24(const unsigned char *p)
{
    return ((unsigned int)p[0] << 16) | ((unsigned int)p[1] << 8) | p[2];
}

static void metti16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v;
}

/* =============================================================================
 * AEAD: ChaCha20-Poly1305 come lo vuole la RFC 8439
 *
 * `buf` ha l'AAD nei primi 5 byte, undici zeri di riempimento, e il testo
 * (in chiaro o cifrato) a partire da AVANTI. Cosi' il dato che Poly1305 deve
 * autenticare e' gia' tutto di fila.
 * ========================================================================== */
static void nonce_di(const Direzione *d, unsigned char out[12])
{
    unsigned int i;
    unsigned long long s = d->seq;

    bcopia(out, d->iv, 12);
    /* Il contatore va in XOR ALLINEATO A DESTRA, big endian: gli ultimi otto
     * byte dei dodici. */
    for (i = 0; i < 8; i++)
        out[11 - i] ^= (unsigned char)((s >> (8 * i)) & 0xFF);
}

static void tag_poly(unsigned char *buf, unsigned int testo_n,
                     const unsigned char chiave[32],
                     const unsigned char nonce[12], unsigned char out[16])
{
    unsigned char otk[64];
    unsigned int  p = (16u - (testo_n % 16u)) % 16u;
    unsigned int  i, tot;

    /* La chiave d'un colpo solo: il primo blocco di ChaCha20 col contatore 0. */
    chacha20_blocco(chiave, 0, nonce, otk);

    for (i = 0; i < p; i++) buf[AVANTI + testo_n + i] = 0;
    tot = AVANTI + testo_n + p;

    /* le64(len(aad)) || le64(len(testo)).
     *
     * ! OTTO BYTE DA UN NUMERO DI QUATTRO, E LO SCORRIMENTO SI FERMA A 32.
     * `testo_n >> 32` su un `unsigned int` non e' zero: e' comportamento
     * indefinito, e su x86 il processore prende solo i cinque bit bassi del
     * conto — cioe' rifa' `>> 0` e riscrive il byte basso. La coda usciva
     * «1d 00 00 00 1d 00 00 00» invece di «1d 00 00 00 00 00 00 00», il tag
     * non tornava mai, e il sintomo era «risposta che non e' TLS 1.3» su un
     * server che aveva risposto benissimo. */
    buf[tot + 0] = 5; for (i = 1; i < 8; i++) buf[tot + i] = 0;
    for (i = 0; i < 8; i++)
        buf[tot + 8 + i] = (i < 4)
                         ? (unsigned char)((testo_n >> (8 * i)) & 0xFF)
                         : 0;
    tot += 16;

    poly1305(otk, buf, tot, out);
}

/* =============================================================================
 * Il record: uno per volta, e sempre nello stesso buffer
 * ========================================================================== */
static int leggi_tutto(Tls *t, unsigned char *dst, unsigned int n,
                       unsigned int ms)
{
    unsigned int fatti = 0;

    while (fatti < n) {
        int r = t->sotto->leggi(t->sotto->stato, dst + fatti, n - fatti, ms);

        if (r <= 0) return EXTLS_ERR_RETE;
        fatti += (unsigned int)r;
    }
    return EXTLS_OK;
}

/* Legge un record e, se la direzione e' cifrata, lo apre. Lascia il testo in
 * chiaro in t->bin+AVANTI, `fine` byte, e il tipo vero in t->tipo. */
static int record_leggi(Tls *t, unsigned int ms)
{
    unsigned char *h = t->bin;          /* i 5 byte dell'intestazione */
    unsigned int   n;
    int            r;

    r = leggi_tutto(t, h, 5, ms);
    if (r != EXTLS_OK) return r;

    n = be16(h + 3);
    /* ! UN RECORD PIU' LUNGO DEL MASSIMO E' UNA VIOLAZIONE, NON UN NOSTRO
     * LIMITE: il tetto di 16384 byte di testo in chiaro sta nello standard.
     * Chiamarlo «messaggio troppo grande» faceva cercare un buffer da
     * allargare invece di un'intestazione letta storta. */
    if (n > REC_MAX) return EXTLS_ERR_PROTOCOLLO;

    r = leggi_tutto(t, t->bin + AVANTI, n, ms);
    if (r != EXTLS_OK) return r;

    /* ! IL ChangeCipherSpec DI MEZZO SI BUTTA SENZA GUARDARLO. In 1.3 non
     * significa niente: e' li' perche' certi apparati di rete chiudono le
     * connessioni che non lo vedono. Contarlo nella trascrizione o nel numero
     * di sequenza sarebbe un difetto che si vede solo dietro quegli apparati. */
    if (h[0] == 20) { t->tipo = 20; t->pos = t->fine = 0; return EXTLS_OK; }

    if (!t->leggo.attiva) {
        t->tipo = h[0];
        t->pos  = 0;
        t->fine = n;
        /* ! UN ALLARME IN CHIARO E' LA RISPOSTA PIU' UTILE CHE SI RICEVA, ed e'
         * anche quella che si perde piu' facilmente: arriva PRIMA che le
         * chiavi esistano, quando il server ha appena letto il nostro
         * ClientHello e non gli e' piaciuto. Trattarlo come «byte che non
         * capisco» butta via il motivo scritto dentro. */
        if (t->tipo == 21) {
            if (n >= 2) t->allarme = t->bin[AVANTI + 1];
            if (n >= 2 && t->bin[AVANTI + 1] == 0) { t->chiuso = 1; return EXTLS_OK; }
            return EXTLS_ERR_ALLERTA;
        }
        return EXTLS_OK;
    }

    /* Cifrato: fuori c'e' sempre 23, e i 16 byte finali sono il tag. */
    if (n < 17) return EXTLS_ERR_PROTOCOLLO;
    {
        unsigned char nonce[12], atteso[16], suo[16];
        unsigned int  ct = n - 16, k;

        nonce_di(&t->leggo, nonce);

        /* ! IL TAG SI METTE DA PARTE PRIMA DI CALCOLARE IL PROPRIO, perche'
         * il riempimento che Poly1305 vuole in coda al testo cifrato cade
         * esattamente sopra i suoi sedici byte. Confrontarlo dopo vorrebbe
         * dire confrontarlo con degli zeri — cioe' una verifica che non
         * verifica niente, e che passa o non passa a caso. */
        bcopia(suo, t->bin + AVANTI + ct, 16);

        /* L'AAD e' l'intestazione com'e' arrivata, e sta gia' dove serve. */
        for (k = 5; k < AVANTI; k++) t->bin[k] = 0;

        tag_poly(t->bin, ct, t->leggo.chiave, nonce, atteso);
        if (!poly1305_uguali(atteso, suo))
            return EXTLS_ERR_PROTOCOLLO;

        chacha20(t->leggo.chiave, 1, nonce,
                 t->bin + AVANTI, t->bin + AVANTI, ct);
        t->leggo.seq++;

        /* Il tipo vero e' l'ultimo byte non nullo. */
        while (ct > 0 && t->bin[AVANTI + ct - 1] == 0) ct--;
        if (ct == 0) return EXTLS_ERR_PROTOCOLLO;

        t->tipo = t->bin[AVANTI + ct - 1];
        t->pos  = 0;
        t->fine = ct - 1;
    }

    /* ! UN ALLARME SI GUARDA SUBITO, e non si confonde con dei dati. Il
     * close_notify e' la fine normale; tutto il resto e' un rifiuto. */
    if (t->tipo == 21) {
        if (t->fine >= 2) t->allarme = t->bin[AVANTI + 1];
        if (t->fine >= 2 && t->bin[AVANTI + 1] == 0) { t->chiuso = 1; return EXTLS_OK; }
        return EXTLS_ERR_ALLERTA;
    }

    return EXTLS_OK;
}

/* ! DUE PEZZI E NON UNO, e non e' generalita' per il gusto di averla: un
 * messaggio di stretta e' un'intestazione di quattro byte piu' un corpo, e
 * l'unico modo di passarne uno solo sarebbe copiare i due pezzi in un terzo
 * buffer da sedici kilobyte. Qui si scrivono di fila dentro quello che c'e'
 * gia'. */
static int record_scrivi2(Tls *t, unsigned int tipo,
                          const unsigned char *a, unsigned int an,
                          const unsigned char *b2, unsigned int bn)
{
    unsigned char *b = t->bout;
    unsigned int   n = an + bn, corpo;

    if (n > REC_CHIARO) return EXTLS_ERR_SPAZIO;

    bcopia(b + AVANTI, a, an);
    if (bn) bcopia(b + AVANTI + an, b2, bn);

    if (!t->scrivo.attiva) {
        b[0] = (unsigned char)tipo;
        b[1] = 3; b[2] = 3;
        metti16(b + 3, n);
        if (t->sotto->scrivi(t->sotto->stato, b, 5) != 5) return EXTLS_ERR_RETE;
        if (n && t->sotto->scrivi(t->sotto->stato, b + AVANTI, n) != (int)n)
            return EXTLS_ERR_RETE;
        return EXTLS_OK;
    }

    /* Cifrato: il tipo vero va in coda al testo, e fuori si scrive 23. */
    b[AVANTI + n] = (unsigned char)tipo;
    corpo = n + 1;

    b[0] = 23; b[1] = 3; b[2] = 3;
    metti16(b + 3, corpo + 16);
    { unsigned int i; for (i = 5; i < AVANTI; i++) b[i] = 0; }

    {
        unsigned char nonce[12], tag[16];

        nonce_di(&t->scrivo, nonce);
        chacha20(t->scrivo.chiave, 1, nonce, b + AVANTI, b + AVANTI, corpo);
        tag_poly(b, corpo, t->scrivo.chiave, nonce, tag);
        /* Il tag va dopo il testo cifrato, e il riempimento di tag_poly
         * l'ha appena sovrascritto: si rimette a posto scrivendolo adesso. */
        bcopia(b + AVANTI + corpo, tag, 16);
        t->scrivo.seq++;
    }

    if (t->sotto->scrivi(t->sotto->stato, b, 5) != 5) return EXTLS_ERR_RETE;
    if (t->sotto->scrivi(t->sotto->stato, b + AVANTI, corpo + 16)
        != (int)(corpo + 16)) return EXTLS_ERR_RETE;
    return EXTLS_OK;
}

static int record_scrivi(Tls *t, unsigned int tipo,
                         const unsigned char *dati, unsigned int n)
{
    return record_scrivi2(t, tipo, dati, n, 0, 0);
}

/* =============================================================================
 * La trascrizione, che e' anche il lettore dei messaggi di stretta
 * ========================================================================== */
static int trascr_aggiungi(Tls *t, const unsigned char *p, unsigned int n)
{
    if (t->trascr_n + n > TRASCR_MAX) return EXTLS_ERR_SPAZIO;
    bcopia(t->trascr + t->trascr_n, p, n);
    t->trascr_n += n;
    return EXTLS_OK;
}

/* ! L'IMPRONTA SI FERMA A `hs_off`, NON A `trascr_n`, E LA DIFFERENZA E' UN
 * DIFETTO CHE COMPARE SOLO SU CERTI SITI. `trascr_n` e' quanto e' ARRIVATO;
 * `hs_off` e' quanto e' stato LETTO. I due numeri coincidono finche' ogni
 * record porta un messaggio solo — ed e' cosi' che si comporta un `openssl
 * s_server` — ma un server vero impacchetta Certificate, CertificateVerify e
 * Finished dentro lo stesso record.
 *
 * Allora, al momento di calcolare l'impronta «fino al Certificate compreso»
 * per verificare la firma, `trascr_n` conteneva gia' anche la firma e il
 * Finished: l'impronta veniva su un dialogo diverso da quello che il server
 * aveva firmato, e la firma non tornava mai. Il sintomo era «la firma del
 * server non torna» su example.com e nessun problema sul banco di prova —
 * cioe' il difetto che una prova in laboratorio non puo' vedere. */
static void trascr_impronta(const Tls *t, unsigned char out[EXTLS_IMPRONTA])
{
    sha256(t->trascr, t->hs_off, out);
}

/* Rende il prossimo messaggio di stretta: tipo, corpo e lunghezza. Se i byte
 * non bastano ne legge altri dalla rete. */
static int hs_prossimo(Tls *t, unsigned int *tipo,
                       const unsigned char **corpo, unsigned int *n,
                       unsigned int ms)
{
    for (;;) {
        if (t->trascr_n - t->hs_off >= 4) {
            const unsigned char *p = t->trascr + t->hs_off;
            unsigned int len = be24(p + 1);

            if (t->trascr_n - t->hs_off >= 4 + len) {
                *tipo  = p[0];
                *corpo = p + 4;
                *n     = len;
                t->hs_off += 4 + len;
                return EXTLS_OK;
            }
        }

        {
            int r = record_leggi(t, ms);

            if (r != EXTLS_OK) return r;
            if (t->tipo == 20) continue;              /* ChangeCipherSpec */
            if (t->tipo != 22) return EXTLS_ERR_PROTOCOLLO;

            r = trascr_aggiungi(t, t->bin + AVANTI, t->fine);
            if (r != EXTLS_OK) return r;
        }
    }
}

/* Manda un messaggio di stretta e lo mette in trascrizione. */
static int hs_manda(Tls *t, unsigned int tipo, const unsigned char *corpo,
                    unsigned int n)
{
    unsigned char testa[4];
    int r;

    testa[0] = (unsigned char)tipo;
    testa[1] = (unsigned char)(n >> 16);
    testa[2] = (unsigned char)(n >> 8);
    testa[3] = (unsigned char)n;

    /* Si scrive in un colpo solo: la trascrizione deve contenere il messaggio
     * intero, e mandarlo in due pezzi non cambia i byte ma cambia il codice
     * che li mette in fila. */
    if (n + 4 > REC_CHIARO) return EXTLS_ERR_SPAZIO;

    r = trascr_aggiungi(t, testa, 4);
    if (r != EXTLS_OK) return r;
    r = trascr_aggiungi(t, corpo, n);
    if (r != EXTLS_OK) return r;

    /* ! IL CURSORE DI LETTURA SALTA CIO' CHE ABBIAMO SCRITTO NOI, e
     * dimenticarlo costa un'ora: la trascrizione e' UNA, e ci vanno dentro
     * tutt'e due le parti del dialogo. Senza questa riga il lettore dei
     * messaggi ripescava il nostro ClientHello e lo trattava come la risposta
     * del server — «risposta che non e' TLS 1.3» su un server che aveva
     * risposto benissimo. */
    t->hs_off = t->trascr_n;

    return record_scrivi2(t, 22, testa, 4, corpo, n);
}

/* =============================================================================
 * Il calendario delle chiavi (RFC 8446, sezione 7.1)
 * ========================================================================== */
static void derive_secret(const unsigned char segreto[EXTLS_IMPRONTA],
                          const char *etichetta,
                          const unsigned char impronta[EXTLS_IMPRONTA],
                          unsigned char out[EXTLS_IMPRONTA])
{
    extls_expand_label(segreto, etichetta, impronta, EXTLS_IMPRONTA,
                       out, EXTLS_IMPRONTA);
}

/* Da un segreto di traffico alle due cose che il record usa. */
static void chiavi_da(const unsigned char segreto[EXTLS_IMPRONTA], Direzione *d)
{
    extls_expand_label(segreto, "key", 0, 0, d->chiave, 32);
    extls_expand_label(segreto, "iv",  0, 0, d->iv, 12);
    d->seq    = 0;
    d->attiva = 1;
}

/* =============================================================================
 * Il ClientHello
 * ========================================================================== */
static int manda_hello(Tls *t, const char *host,
                       const unsigned char pubblica[32],
                       unsigned char sessione[32])
{
    unsigned char c[512];
    unsigned int  i = 0, ext_inizio, ext_n;
    unsigned int  hl = lung(host);

    if (hl == 0 || hl > 255) return EXTLS_ERR_USO;

    c[i++] = 3; c[i++] = 3;                     /* legacy_version = 1.2 */
    t->casuale(c + i, 32); i += 32;             /* random */

    /* ! LA SESSIONE FINTA C'E' APPOSTA. In 1.3 non serve a niente, ma un
     * ClientHello senza session_id viene scartato da certi apparati che
     * credono di guardare una 1.2. Trentadue byte casuali che il server
     * rimanda indietro identici. */
    c[i++] = 32;
    t->casuale(sessione, 32);
    bcopia(c + i, sessione, 32); i += 32;

    metti16(c + i, 2); i += 2;                  /* cipher_suites */
    c[i++] = 0x13; c[i++] = 0x03;               /* TLS_CHACHA20_POLY1305_SHA256 */

    c[i++] = 1; c[i++] = 0;                     /* compressione: nessuna */

    ext_inizio = i; i += 2;

    /* server_name (0) */
    metti16(c + i, 0); i += 2;
    metti16(c + i, hl + 5); i += 2;
    metti16(c + i, hl + 3); i += 2;
    c[i++] = 0;
    metti16(c + i, hl); i += 2;
    bcopia(c + i, host, hl); i += hl;

    /* supported_groups (10): x25519 */
    metti16(c + i, 10); i += 2;
    metti16(c + i, 4);  i += 2;
    metti16(c + i, 2);  i += 2;
    metti16(c + i, 0x001D); i += 2;

    /* signature_algorithms (13)
     * ! SOLO QUELLE CHE SAPPIAMO VERIFICARE, e non e' prudenza: annunciare un
     * algoritmo che non si sa controllare vuol dire ricevere una firma che si
     * dovra' accettare senza guardarla, oppure rifiutare dopo. */
    /* ! UNA SOLA, E NON TRE. Qui c'erano anche rsa_pss_rsae_sha384 e _sha512:
     * annunciarle voleva dire che un server le sceglieva — e' il suo diritto —
     * e poi la firma arrivava con dentro un'impronta SHA-384 che
     * extls_rsa_pss_verifica non sa fare, perche' prende uno SHA-256 e basta.
     * Il risultato era «la firma del server non torna» su un certificato
     * perfetto. Si annuncia cio' che si sa verificare. */
    /* ! E DAL 25 AGOSTO 2026 SONO DUE, non una. Con la sola RSA meta' del web
     * restava chiusa: wikipedia.org e news.ycombinator.com hanno solo
     * certificati ECDSA e rispondevano «nessun cifrario in comune» — allarme
     * 40 — perche' non avevamo niente da offrire che sapessero firmare.
     * lib/exp256 verifica le firme su P-256, quindi adesso si puo' annunciare.
     *
     * ! L'ORDINE E' UNA PREFERENZA, non un elenco: si mette per prima quella
     * che copre i siti che prima si rifiutavano. */
    metti16(c + i, 13); i += 2;
    metti16(c + i, 8);  i += 2;
    metti16(c + i, 6);  i += 2;
    metti16(c + i, 0x0403); i += 2;             /* ecdsa_secp256r1_sha256 */
    metti16(c + i, 0x0503); i += 2;             /* ecdsa_secp384r1_sha384 */
    metti16(c + i, 0x0804); i += 2;             /* rsa_pss_rsae_sha256 */

    /* supported_versions (43): solo 1.3 */
    metti16(c + i, 43); i += 2;
    metti16(c + i, 3);  i += 2;
    c[i++] = 2;
    metti16(c + i, 0x0304); i += 2;

    /* key_share (51): x25519 */
    metti16(c + i, 51); i += 2;
    metti16(c + i, 38); i += 2;
    metti16(c + i, 36); i += 2;
    metti16(c + i, 0x001D); i += 2;
    metti16(c + i, 32); i += 2;
    bcopia(c + i, pubblica, 32); i += 32;

    /* ALPN (16): http/1.1 — il browser parla quello, e dirlo evita che un
     * server moderno risponda in HTTP/2, che qui non si saprebbe leggere. */
    metti16(c + i, 16); i += 2;
    metti16(c + i, 11); i += 2;
    metti16(c + i, 9);  i += 2;
    c[i++] = 8;
    bcopia(c + i, "http/1.1", 8); i += 8;

    ext_n = i - ext_inizio - 2;
    metti16(c + ext_inizio, ext_n);

    return hs_manda(t, 1, c, i);
}

/* =============================================================================
 * Il ServerHello
 * ========================================================================== */
static int leggi_hello(const unsigned char *p, unsigned int n,
                       unsigned char altrui[32])
{
    unsigned int i = 0, ext_n, fine;
    int          visto_chiave = 0, visto_versione = 0;

    if (n < 38) return EXTLS_ERR_PROTOCOLLO;
    i += 2 + 32;                                /* versione + random */

    /* ! IL RANDOM DEL HelloRetryRequest E' UNA COSTANTE, ed e' l'unico modo di
     * riconoscerlo: e' un ServerHello a tutti gli effetti. Non si gestisce —
     * con un solo gruppo da offrire non ci sarebbe niente da riprovare — ma si
     * riconosce, perche' «HRR» detto a chi legge il log e' un'informazione e
     * «protocollo» non lo e'. */
    {
        static const unsigned char HRR[8] = {
            0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11
        };
        unsigned int k; int uguale = 1;
        for (k = 0; k < 8; k++) if (p[2 + k] != HRR[k]) { uguale = 0; break; }
        if (uguale) return EXTLS_ERR_HRR;
    }

    if (i >= n) return EXTLS_ERR_PROTOCOLLO;
    i += 1 + p[i];                              /* legacy_session_id_echo */
    if (i + 3 > n) return EXTLS_ERR_PROTOCOLLO;

    if (p[i] != 0x13 || p[i + 1] != 0x03) return EXTLS_ERR_CIFRARIO;
    i += 2;
    i += 1;                                     /* compressione */

    if (i + 2 > n) return EXTLS_ERR_PROTOCOLLO;
    ext_n = be16(p + i); i += 2;
    if (i + ext_n > n) return EXTLS_ERR_PROTOCOLLO;
    fine = i + ext_n;

    while (i + 4 <= fine) {
        unsigned int tipo = be16(p + i), len = be16(p + i + 2);

        i += 4;
        if (i + len > fine) return EXTLS_ERR_PROTOCOLLO;

        if (tipo == 43) {                       /* supported_versions */
            if (len != 2 || be16(p + i) != 0x0304) return EXTLS_ERR_VERSIONE;
            visto_versione = 1;
        } else if (tipo == 51) {                /* key_share */
            if (len != 36 || be16(p + i) != 0x001D || be16(p + i + 2) != 32)
                return EXTLS_ERR_PROTOCOLLO;
            bcopia(altrui, p + i + 4, 32);
            visto_chiave = 1;
        }
        i += len;
    }

    if (!visto_versione) return EXTLS_ERR_VERSIONE;
    if (!visto_chiave)   return EXTLS_ERR_PROTOCOLLO;
    return EXTLS_OK;
}

/* =============================================================================
 * Il certificato del server, e la firma che dimostra che e' suo
 * ========================================================================== */
static int leggi_certificati(const unsigned char *p, unsigned int n,
                             ExCert *catena, unsigned int max,
                             unsigned int *quanti)
{
    unsigned int i = 0, lista, fine;

    *quanti = 0;
    if (n < 4) return EXTLS_ERR_PROTOCOLLO;

    i += 1 + p[0];                              /* certificate_request_context */
    if (i + 3 > n) return EXTLS_ERR_PROTOCOLLO;
    lista = be24(p + i); i += 3;
    if (i + lista > n) return EXTLS_ERR_PROTOCOLLO;
    fine = i + lista;

    while (i + 3 <= fine && *quanti < max) {
        unsigned int len = be24(p + i);

        i += 3;
        if (i + len > fine) return EXTLS_ERR_PROTOCOLLO;

        if (excert_analizza(p + i, len, &catena[*quanti]) != 0)
            return EXTLS_ERR_CERTIFICATO;
        (*quanti)++;

        i += len;
        if (i + 2 > fine) return EXTLS_ERR_PROTOCOLLO;
        i += 2 + be16(p + i);                   /* le estensioni della voce */
    }

    if (*quanti == 0) return EXTLS_ERR_CERTIFICATO;
    return EXTLS_OK;
}

/* ! LA FIRMA ECDSA E' UNA SEQUENCE DI DUE INTERI, e chi la tratta come un
 * numero solo la vede fallire sempre — e cerca il difetto nella curva. Sta
 * qui e non in lib/excert perche' e' un fatto del FORMATO, non della catena:
 * la CertificateVerify di TLS porta esattamente gli stessi byte. */
static int extls_ecdsa_r_s(const ExDer *firma, ExDer *r, ExDer *s)
{
    ExDer     dentro;
    ExDerElem a, b;

    if (exder_dentro(firma, 0, 0x30, &dentro) != 0) return -1;
    if (exder_leggi(&dentro, 0, &a) != 0 || a.tag != 0x02) return -1;
    if (exder_leggi(&dentro, a.intestazione + a.valore.n, &b) != 0 ||
        b.tag != 0x02) return -1;

    *r = a.valore;
    *s = b.valore;
    return 0;
}

/* Il messaggio su cui il server firma: 64 spazi, una frase, uno zero e
 * l'impronta della trascrizione fino al Certificate compreso. */
static void contesto_firma(const unsigned char impronta[EXTLS_IMPRONTA],
                           unsigned char out[130], unsigned int *n)
{
    static const char FRASE[] = "TLS 1.3, server CertificateVerify";
    unsigned int i;

    for (i = 0; i < 64; i++) out[i] = 0x20;
    for (i = 0; i < 33; i++) out[64 + i] = (unsigned char)FRASE[i];
    out[97] = 0;
    bcopia(out + 98, impronta, EXTLS_IMPRONTA);
    *n = 98 + EXTLS_IMPRONTA;
}

/* =============================================================================
 * La stretta di mano
 * ========================================================================== */
int extls_stretta(void *opaco, const ExTlsSotto *sotto, const char *host,
                  const ExMagazzino *magazzino, const char *adesso,
                  void (*casuale)(unsigned char *, unsigned int))
{
    Tls *t = (Tls *)opaco;
    unsigned char privata[32], pubblica[32], altrui[32], condiviso[32];
    unsigned char sessione[32];
    unsigned char primo[EXTLS_IMPRONTA], derivato[EXTLS_IMPRONTA];
    unsigned char stretta[EXTLS_IMPRONTA];
    unsigned char c_hs[EXTLS_IMPRONTA], s_hs[EXTLS_IMPRONTA];
    unsigned char padrone[EXTLS_IMPRONTA];
    unsigned char impronta[EXTLS_IMPRONTA];
    unsigned char zeri[EXTLS_IMPRONTA];
    ExCert        catena[8];
    unsigned int  quanti = 0;
    int           r;

    if (!t || !sotto || !host || !magazzino || !casuale) return EXTLS_ERR_USO;

    bzero_(t, sizeof(*t));
    t->sotto   = sotto;
    t->casuale = casuale;
    bzero_(zeri, sizeof(zeri));

    /* --- la nostra meta' dello scambio ----------------------------------- */
    if (!passo(EXTLS_P_CHIAVE)) return EXTLS_ERR_RETE;
    casuale(privata, 32);
    if (x25519_pubblica(pubblica, privata) != 0) return EXTLS_ERR_PROTOCOLLO;

    r = manda_hello(t, host, pubblica, sessione);
    if (r != EXTLS_OK) return r;

    /* --- il ServerHello --------------------------------------------------- */
    {
        unsigned int tipo, n;
        const unsigned char *corpo;

        r = hs_prossimo(t, &tipo, &corpo, &n, 15000);
        if (r != EXTLS_OK) return r;
        if (tipo != 2) return EXTLS_ERR_PROTOCOLLO;

        r = leggi_hello(corpo, n, altrui);
        if (r != EXTLS_OK) return r;
    }
    if (!passo(EXTLS_P_HELLO)) return EXTLS_ERR_RETE;

    /* ! UN SEGRETO TUTTO ZERI SI RIFIUTA. Vuol dire che il punto ricevuto era
     * di ordine piccolo: il «segreto» condiviso lo conoscerebbe anche chi
     * guarda. x25519() lo dice, e qui si smette. */
    if (x25519(condiviso, privata, altrui) != 0) return EXTLS_ERR_PROTOCOLLO;
    if (!passo(EXTLS_P_SEGRETO)) return EXTLS_ERR_RETE;

    /* --- il calendario delle chiavi, primo giro --------------------------- */
    extls_hkdf_extract(0, 0, zeri, EXTLS_IMPRONTA, primo);
    sha256("", 0, impronta);
    derive_secret(primo, "derived", impronta, derivato);
    extls_hkdf_extract(derivato, EXTLS_IMPRONTA, condiviso, 32, stretta);

    trascr_impronta(t, impronta);
    derive_secret(stretta, "c hs traffic", impronta, c_hs);
    derive_secret(stretta, "s hs traffic", impronta, s_hs);

    /* ! I SEGRETI SI POSSONO STAMPARE, E SOLO COMPILANDO APPOSTA. Con
     * -DEXTLS_TRACCIA escono nella forma del keylog di OpenSSL, che e' cio'
     * che permette di confrontarli riga per riga con l'altra parte: e' il
     * modo in cui questa implementazione e' stata portata a termine. Fuori da
     * quella compilazione non esiste codice che li scriva da nessuna parte. */
    traccia("SERVER_HANDSHAKE_TRAFFIC_SECRET", s_hs, EXTLS_IMPRONTA);
    traccia("CLIENT_HANDSHAKE_TRAFFIC_SECRET", c_hs, EXTLS_IMPRONTA);

    chiavi_da(s_hs, &t->leggo);

    /* --- quello che il server dice sotto cifratura ------------------------ */
    {
        unsigned int tipo, n;
        const unsigned char *corpo;
        unsigned char cert_impronta[EXTLS_IMPRONTA];
        int visto_cert = 0, visto_firma = 0;

        for (;;) {
            r = hs_prossimo(t, &tipo, &corpo, &n, 15000);
            if (r != EXTLS_OK) return r;

            if (tipo == 8) continue;            /* EncryptedExtensions */

            if (tipo == 13) {
                /* ! UNA RICHIESTA DI CERTIFICATO NON SI PUO' SODDISFARE, e
                 * proseguire in silenzio darebbe un errore piu' avanti che
                 * parla d'altro. */
                return EXTLS_ERR_CERTIFICATO;
            }

            if (tipo == 11) {                   /* Certificate */
                r = leggi_certificati(corpo, n, catena, 8, &quanti);
                if (r != EXTLS_OK) return r;

                /* L'impronta va presa ADESSO: la firma e' su tutto quello che
                 * si e' detto FINO AL certificato compreso, non oltre. */
                trascr_impronta(t, cert_impronta);
                visto_cert = 1;
                if (!passo(EXTLS_P_CERTIFICATI)) return EXTLS_ERR_RETE;
                continue;
            }

            if (tipo == 15) {                   /* CertificateVerify */
                unsigned char messaggio[130], m_impronta[EXTLS_IMPRONTA];
                unsigned int  m_n, alg, firma_n;

                if (!visto_cert) return EXTLS_ERR_PROTOCOLLO;
                if (n < 4) return EXTLS_ERR_PROTOCOLLO;

                alg     = be16(corpo);
                firma_n = be16(corpo + 2);
                if (4 + firma_n > n) return EXTLS_ERR_PROTOCOLLO;

                /* ! LE DUE STRADE SONO QUELLE ANNUNCIATE, E NON UNA DI PIU'.
                 * Un server puo' firmare solo con cio' che il ClientHello ha
                 * offerto: qualunque altro numero qui e' un server che non
                 * rispetta il patto, e la risposta giusta e' smettere. */
                if (alg != 0x0804 && alg != 0x0403 && alg != 0x0503)
                    return EXTLS_ERR_FIRMA;

                contesto_firma(cert_impronta, messaggio, &m_n);

                if (alg == 0x0403 || alg == 0x0503) {
                    /* ECDSA: la curva la dice la CHIAVE, l'impronta la dice
                     * l'algoritmo annunciato. Sono due cose diverse. */
                    unsigned char lunga[48];
                    const unsigned char *imp;
                    unsigned int  imp_n;
                    int           curva;
                    ExDer f, r_i, s_i;

                    if (catena[0].tipo_chiave == EXASN1_CHIAVE_EC_P256)
                        curva = EXCURVA_P256;
                    else if (catena[0].tipo_chiave == EXASN1_CHIAVE_EC_P384)
                        curva = EXCURVA_P384;
                    else
                        return EXTLS_ERR_FIRMA;

                    if (alg == 0x0503) {
                        sha384(messaggio, m_n, lunga);
                        imp = lunga; imp_n = 48;
                    } else {
                        sha256(messaggio, m_n, m_impronta);
                        imp = m_impronta; imp_n = EXTLS_IMPRONTA;
                    }

                    f.p = corpo + 4;
                    f.n = firma_n;
                    if (extls_ecdsa_r_s(&f, &r_i, &s_i) != 0)
                        return EXTLS_ERR_FIRMA;

                    if (excurva_verifica(curva, catena[0].chiave_punto.p,
                                         catena[0].chiave_punto.n,
                                         imp, imp_n, r_i.p, r_i.n,
                                         s_i.p, s_i.n) != 0)
                        return EXTLS_ERR_FIRMA;
                } else {
                    sha256(messaggio, m_n, m_impronta);
                    if (catena[0].tipo_chiave != EXASN1_CHIAVE_RSA)
                        return EXTLS_ERR_FIRMA;

                    if (extls_rsa_pss_verifica(catena[0].chiave_modulo.p,
                                               catena[0].chiave_modulo.n,
                                               catena[0].chiave_esponente.p,
                                               catena[0].chiave_esponente.n,
                                               m_impronta, corpo + 4, firma_n,
                                               EXTLS_IMPRONTA) != 0)
                        return EXTLS_ERR_FIRMA;
                }

                visto_firma = 1;
                if (!passo(EXTLS_P_FIRMA)) return EXTLS_ERR_RETE;
                continue;
            }

            if (tipo == 20) {                   /* Finished */
                unsigned char chiave_f[EXTLS_IMPRONTA], atteso[EXTLS_IMPRONTA];
                unsigned int  k;

                if (!visto_cert || !visto_firma) return EXTLS_ERR_PROTOCOLLO;

                /* L'impronta su cui si calcola il Finished del server e'
                 * quella di tutto FUORCHE' il Finished stesso: si scala
                 * indietro di quanto quel messaggio occupa. */
                {
                    unsigned int salva = t->hs_off;

                    t->hs_off = salva - (n + 4);
                    trascr_impronta(t, impronta);
                    t->hs_off = salva;
                }

                extls_expand_label(s_hs, "finished", 0, 0, chiave_f,
                                   EXTLS_IMPRONTA);
                extls_hmac(chiave_f, EXTLS_IMPRONTA, impronta, EXTLS_IMPRONTA,
                           atteso);

                if (n != EXTLS_IMPRONTA) return EXTLS_ERR_FINISHED;
                for (k = 0; k < EXTLS_IMPRONTA; k++)
                    if (atteso[k] != corpo[k]) return EXTLS_ERR_FINISHED;
                break;
            }

            return EXTLS_ERR_PROTOCOLLO;
        }
    }

    /* --- il certificato vale per QUESTO sito, e viene da una CA vera ------ */
    /* ! IL MOTIVO DELLA CATENA SI TIENE. `excert_catena_valida` distingue nove
     * casi — scaduto, senza radice, firma sbagliata, non e' una CA — e
     * schiacciarli tutti in «certificato non verificabile» manda a cercare il
     * difetto nel posto sbagliato. Il codice resta leggibile con
     * extls_motivo(). */
    if (!passo(EXTLS_P_CATENA)) return EXTLS_ERR_RETE;
    r = excert_catena_valida(catena, quanti, magazzino, adesso, &t->anello);
    if (r != EXCERT_OK) { t->motivo = r; return EXTLS_ERR_CERTIFICATO; }

    if (excert_nome_combacia(&catena[0], host) != EXCERT_OK)
        return EXTLS_ERR_NOME;

    /* --- la nostra risposta ---------------------------------------------- */
    trascr_impronta(t, impronta);               /* fino al Finished del server */

    /* ! IL ChangeCipherSpec SI MANDA ANCHE SE NON SIGNIFICA NIENTE, per lo
     * stesso motivo per cui si e' inventata la sessione finta: le scatole di
     * mezzo. Va fuori in chiaro e NON entra nella trascrizione. */
    {
        unsigned char uno = 1;

        record_scrivi(t, 20, &uno, 1);   /* la scrittura e' ancora in chiaro */
    }

    chiavi_da(c_hs, &t->scrivo);

    {
        unsigned char chiave_f[EXTLS_IMPRONTA], mio[EXTLS_IMPRONTA];

        extls_expand_label(c_hs, "finished", 0, 0, chiave_f, EXTLS_IMPRONTA);
        extls_hmac(chiave_f, EXTLS_IMPRONTA, impronta, EXTLS_IMPRONTA, mio);

        r = hs_manda(t, 20, mio, EXTLS_IMPRONTA);
        if (r != EXTLS_OK) return r;
    }

    /* --- le chiavi dei dati ---------------------------------------------- */
    sha256("", 0, derivato);
    derive_secret(stretta, "derived", derivato, derivato);
    extls_hkdf_extract(derivato, EXTLS_IMPRONTA, zeri, EXTLS_IMPRONTA, padrone);

    derive_secret(padrone, "c ap traffic", impronta, t->c_app);
    derive_secret(padrone, "s ap traffic", impronta, t->s_app);

    chiavi_da(t->c_app, &t->scrivo);
    chiavi_da(t->s_app, &t->leggo);

    /* Da qui in poi la trascrizione non serve piu': il buffer resta, e serve
     * a leggere i record. */
    t->trascr_n = t->hs_off = 0;
    t->pos = t->fine = 0;

    /* ! L'ULTIMO PASSO NON PUO' PIU' ANNULLARE NIENTE: la connessione c'e', e
     * dire di no adesso vorrebbe dire buttarla via dopo averla pagata. Si
     * chiama lo stesso, perche' chi ospita ha una riga di stato da chiudere. */
    passo(EXTLS_P_FATTO);
    return EXTLS_OK;
}

/* =============================================================================
 * Dopo la stretta
 * ========================================================================== */
int extls_leggi(void *opaco, unsigned char *dst, unsigned int max,
                unsigned int ms)
{
    Tls *t = (Tls *)opaco;
    unsigned int q;

    if (!t || !dst || max == 0) return -1;

    while (t->pos >= t->fine) {
        int r;

        if (t->chiuso) return 0;
        r = record_leggi(t, ms);
        if (r != EXTLS_OK) {
            /* ! LA RAGIONE SI TIENE. `extls_leggi` deve rendere byte, non
             * codici, ma «zero byte» senza il perche' e' esattamente il tipo
             * di silenzio che costa un pomeriggio. */
            t->ultimo = r;
            return (r == EXTLS_ERR_RETE) ? 0 : -1;
        }

        if (t->chiuso) return 0;

        /* ! I BIGLIETTI DI SESSIONE ARRIVANO IN MEZZO AI DATI, e vanno
         * saltati senza fare rumore: sono l'offerta di riprendere questa
         * connessione piu' in fretta la prossima volta, e questo cliente non
         * la raccoglie. Trattarli come dati darebbe byte a caso dentro una
         * pagina HTML. */
        if (t->tipo == 22) { t->pos = t->fine = 0; continue; }
        if (t->tipo == 20) { t->pos = t->fine = 0; continue; }
        if (t->tipo != 23) { t->ultimo = EXTLS_ERR_PROTOCOLLO; return -1; }
    }

    q = t->fine - t->pos;
    if (q > max) q = max;
    bcopia(dst, t->bin + AVANTI + t->pos, q);
    t->pos += q;
    return (int)q;
}

int extls_pronto(void *opaco)
{
    Tls *t = (Tls *)opaco;

    if (!t) return -1;
    if (t->chiuso) return -1;
    /* Solo il chiaro gia' decifrato: quel che sta ancora nel trasporto sotto
     * lo sa il trasporto sotto, e chi chiama lo chiede a lui. */
    return (t->pos < t->fine) ? (int)(t->fine - t->pos) : 0;
}

int extls_scrivi(void *opaco, const unsigned char *src, unsigned int n)
{
    Tls *t = (Tls *)opaco;
    unsigned int fatti = 0;

    if (!t || !src) return -1;

    while (fatti < n) {
        unsigned int q = n - fatti;

        if (q > REC_CHIARO - 1) q = REC_CHIARO - 1;
        if (record_scrivi(t, 23, src + fatti, q) != EXTLS_OK) return -1;
        fatti += q;
    }
    return (int)n;
}

void extls_chiudi(void *opaco)
{
    Tls *t = (Tls *)opaco;
    unsigned char avviso[2];

    if (!t || t->chiuso || !t->scrivo.attiva) return;

    avviso[0] = 1;      /* warning */
    avviso[1] = 0;      /* close_notify */
    record_scrivi(t, 21, avviso, 2);
    t->chiuso = 1;
}

/* ! IL NUMERO DELL'ALLARME E' L'UNICA COSA CHE IL SERVER DICE SUL PERCHE'.
 * «Il server ha rifiutato» non si ripara; «40, handshake_failure» dice che non
 * abbiamo offerto niente che gli andasse bene, e «112, unrecognized_name» che
 * il nome nel SNI non e' suo. Vale la pena tenerlo. */
unsigned int extls_allarme(void *opaco)
{
    return opaco ? ((Tls *)opaco)->allarme : 0;
}

int extls_ultimo(void *opaco)
{
    return opaco ? ((Tls *)opaco)->ultimo : 0;
}

int extls_motivo(void *opaco)
{
    return opaco ? ((Tls *)opaco)->motivo : 0;
}

int extls_anello(void *opaco)
{
    return opaco ? (int)((Tls *)opaco)->anello : 0;
}

const char *extls_perche(int codice)
{
    switch (codice) {
    case EXTLS_OK:               return "tutto a posto";
    case EXTLS_ERR_RETE:         return "la connessione e' caduta";
    case EXTLS_ERR_PROTOCOLLO:   return "risposta che non e' TLS 1.3";
    case EXTLS_ERR_VERSIONE:     return "il server non parla TLS 1.3";
    case EXTLS_ERR_CIFRARIO:     return "nessun cifrario in comune";
    case EXTLS_ERR_CERTIFICATO:  return "certificato non verificabile";
    case EXTLS_ERR_NOME:         return "il certificato e' di un altro sito";
    case EXTLS_ERR_FIRMA:        return "la firma del server non torna";
    case EXTLS_ERR_FINISHED:     return "le chiavi non coincidono";
    case EXTLS_ERR_ALLERTA:      return "il server ha rifiutato";
    case EXTLS_ERR_SPAZIO:       return "messaggio troppo grande";
    case EXTLS_ERR_HRR:          return "il server vuole un altro gruppo";
    case EXTLS_ERR_USO:          return "manca qualcosa per connettersi";
    default:                     return "errore";
    }
}

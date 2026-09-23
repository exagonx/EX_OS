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
    case EXTLS_P_ANELLO:      return "un anello della catena";
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

/* Lo stesso, nella forma che excert vuole: la catena chiama qui una volta per
 * anello, e chi ospita vede un EXTLS_P_ANELLO. L'indice non gli serve — la
 * frase e' la stessa — ma excert lo passa perche' chi volesse scrivere
 * «2 di 4» ce l'ha. */
static int passo_anello(void *dato, unsigned int anello, unsigned int quanti)
{
    (void)dato; (void)anello; (void)quanti;
    return passo(EXTLS_P_ANELLO);
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
    GcmChiave     gcm;          /* the AES key schedule, for AES-128-GCM */
} Direzione;

/* =============================================================================
 * TWO CIPHERS, NOT ONE — 23 September 2026
 *
 * Until today the ClientHello offered TLS_CHACHA20_POLY1305_SHA256 only, and a
 * server without it answered with an alert: www.ebay.it, measured with
 * tools/prova_certificati.sh. Now TLS_AES_128_GCM_SHA256 is offered too — the
 * cipher every TLS 1.3 server has, since the RFC makes it mandatory — with the
 * AES that was already here for the Wi-Fi and lib/excrypt/gcm.c.
 *
 * ! ChaCha20 STAYS FIRST: in software, without AES instructions, it is the
 * faster of the two, and the order is our preference. The server chooses.
 *
 * ! AES_256_GCM_SHA384 IS NOT OFFERED: it would need SHA-384 in the whole
 * key schedule, not only a longer key. A server that has TLS 1.3 has
 * AES_128_GCM_SHA256 as well.
 * ============================================================================= */
#define TLS_CHACHA  0x1303
#define TLS_AES128  0x1301

/* =============================================================================
 * TWO GROUPS, AND THE SECOND ONE ONLY WHEN ASKED — 23 September 2026
 *
 * x25519 is offered with its key, as before: it is cheap and nearly every
 * server takes it. secp256r1 (P-256) is only NAMED in supported_groups. A
 * server that wants it — www.poste.it — answers with a HelloRetryRequest,
 * and only then the P-256 key is computed (lib/excrypt/p256.c, constant
 * time) and the ClientHello is sent again. The price of P-256 is paid by
 * the servers that ask for it, not by every connection.
 *
 * ! THE SECOND ClientHello IS THE FIRST ONE, byte for byte, but for the key
 * share and the cookie: same random, same session id (RFC 8446, 4.1.2). And
 * in the transcript the first one is replaced by its hash, in a synthetic
 * message_hash message (4.4.1): that is what the Finished of both sides is
 * computed on, and getting it wrong fails only at the very end.
 * ============================================================================= */
/* =============================================================================
 * TLS 1.2 — 23 September 2026
 *
 * Measured with tools/prova_certificati.sh: www.corriere.it, www.gazzetta.it
 * and www.istat.it speak ONLY TLS 1.2, and the client spoke only 1.3. So the
 * ClientHello now also offers 1.2, and a ServerHello without
 * supported_versions leads to stretta12().
 *
 * ! THE MODERN HALF OF 1.2, AND NOTHING ELSE: ECDHE (x25519 or P-256, the
 * code of 1.3) with AES-128-GCM or ChaCha20-Poly1305, signatures RSA-PSS,
 * RSA PKCS#1 v1.5 and ECDSA, the Extended Master Secret (RFC 7627). No CBC,
 * no RSA key transport, no session resumption, no renegotiation: those are
 * the parts of 1.2 that broke over the years, and the three sites measured do
 * not need them.
 *
 * ! AND THE DOWNGRADE SENTINEL IS CHECKED: a server that could speak 1.3 and
 * answers 1.2 writes «DOWNGRD\x01» at the end of its random (RFC 8446 4.1.3)
 * when it is doing so because someone in the middle removed our 1.3 offer.
 * ============================================================================= */
#define TLS12_ECDHE_ECDSA_AES128   0xC02B
#define TLS12_ECDHE_RSA_AES128     0xC02F
#define TLS12_ECDHE_ECDSA_CHACHA   0xCCA9
#define TLS12_ECDHE_RSA_CHACHA     0xCCA8

static int e_cifrario12(unsigned int c)
{
    return c == TLS12_ECDHE_ECDSA_AES128 || c == TLS12_ECDHE_RSA_AES128 ||
           c == TLS12_ECDHE_ECDSA_CHACHA || c == TLS12_ECDHE_RSA_CHACHA;
}

static int e_aes12(unsigned int c)
{
    return c == TLS12_ECDHE_ECDSA_AES128 || c == TLS12_ECDHE_RSA_AES128;
}

#define GR_X25519   0x001D
#define GR_P256     0x0017
#define COOKIE_MAX  256

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

    unsigned int cifrario;  /* TLS_CHACHA or TLS_AES128, from the ServerHello */

    /* What the second ClientHello must repeat after a HelloRetryRequest. */
    unsigned char ch_random[32];
    unsigned char ch_sessione[32];
    unsigned char cookie[COOKIE_MAX];
    unsigned int  cookie_n;
    unsigned int  gruppo;       /* the group of the key share received */

    /* TLS 1.2 */
    int           v12;          /* the server chose 1.2 */
    int           ems;          /* extended_master_secret agreed */
    int           leggo_pronta; /* the read keys wait for the server's CCS */
    unsigned char sh_random[32];

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
                     const unsigned char nonce[12], unsigned char out[16],
                     unsigned int aad_n)
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
    /* The AAD length: 5 in 1.3 (the record header), 13 in 1.2. Either way it
     * sits in buf[0 .. AVANTI), padded with zeros to 16. */
    buf[tot + 0] = (unsigned char)aad_n; for (i = 1; i < 8; i++) buf[tot + i] = 0;
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
    if (h[0] == 20) {
        /* ! IN 1.2 IT DOES MEAN SOMETHING: from the next record on, the
         * server writes with the keys. */
        if (t->v12 && t->leggo_pronta) {
            t->leggo.attiva = 1;
            t->leggo.seq = 0;
            t->leggo_pronta = 0;
        }
        t->tipo = 20; t->pos = t->fine = 0; return EXTLS_OK;
    }

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

    if (t->v12) {
        /* ! 1.2: THE TYPE OUTSIDE IS THE REAL ONE, and the AAD is 13 bytes:
         * sequence number, type, version, length of the PLAINTEXT. AES-GCM
         * carries 8 bytes of explicit nonce before the ciphertext. */
        unsigned char nonce[12], aad[13], suo[16];
        unsigned int  tipo = h[0], ct, k, testa = e_aes12(t->cifrario) ? 8 : 0;
        unsigned long long s = t->leggo.seq;

        if (n < testa + 16) return EXTLS_ERR_PROTOCOLLO;
        ct = n - testa - 16;
        for (k = 0; k < 8; k++) aad[k] = (unsigned char)(s >> (56 - 8 * k));
        aad[8] = (unsigned char)tipo; aad[9] = 3; aad[10] = 3;
        aad[11] = (unsigned char)(ct >> 8); aad[12] = (unsigned char)ct;
        bcopia(suo, t->bin + AVANTI + testa + ct, 16);

        if (testa) {
            bcopia(nonce, t->leggo.iv, 4);
            bcopia(nonce + 4, t->bin + AVANTI, 8);
            if (aes_gcm_decifra(&t->leggo.gcm, nonce, aad, 13, t->bin + AVANTI + 8,
                                t->bin + AVANTI, ct, suo) != 0)
                return EXTLS_ERR_PROTOCOLLO;
        } else {
            unsigned char atteso[16];

            nonce_di(&t->leggo, nonce);
            bcopia(t->bin, aad, 13);
            for (k = 13; k < AVANTI; k++) t->bin[k] = 0;
            tag_poly(t->bin, ct, t->leggo.chiave, nonce, atteso, 13);
            if (!poly1305_uguali(atteso, suo)) return EXTLS_ERR_PROTOCOLLO;
            chacha20(t->leggo.chiave, 1, nonce, t->bin + AVANTI, t->bin + AVANTI, ct);
        }
        t->leggo.seq++;
        t->tipo = tipo;
        t->pos  = 0;
        t->fine = ct;
    } else {
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

        if (t->cifrario == TLS_AES128) {
            /* The AAD is the record header as it arrived: five bytes. */
            if (aes_gcm_decifra(&t->leggo.gcm, nonce, h, 5,
                                t->bin + AVANTI, t->bin + AVANTI, ct, suo) != 0)
                return EXTLS_ERR_PROTOCOLLO;
        } else {
            /* L'AAD e' l'intestazione com'e' arrivata, e sta gia' dove serve. */
            for (k = 5; k < AVANTI; k++) t->bin[k] = 0;

            tag_poly(t->bin, ct, t->leggo.chiave, nonce, atteso, 5);
            if (!poly1305_uguali(atteso, suo))
                return EXTLS_ERR_PROTOCOLLO;

            chacha20(t->leggo.chiave, 1, nonce,
                     t->bin + AVANTI, t->bin + AVANTI, ct);
        }
        t->leggo.seq++;

        /* Il tipo vero e' l'ultimo byte non nullo. */
        while (ct > 0 && t->bin[AVANTI + ct - 1] == 0) ct--;
        if (ct == 0) return EXTLS_ERR_PROTOCOLLO;

        t->tipo = t->bin[AVANTI + ct - 1];
        t->pos  = 0;
        t->fine = ct - 1;
    }
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

    if (t->v12) {
        unsigned char nonce[12], aad[13], tag[16], hdr[5], espl[8];
        unsigned int  k, testa = e_aes12(t->cifrario) ? 8 : 0;
        unsigned long long s = t->scrivo.seq;

        for (k = 0; k < 8; k++) aad[k] = espl[k] = (unsigned char)(s >> (56 - 8 * k));
        aad[8] = (unsigned char)tipo; aad[9] = 3; aad[10] = 3;
        aad[11] = (unsigned char)(n >> 8); aad[12] = (unsigned char)n;

        if (testa) {
            /* The explicit nonce is the sequence number: unique by design. */
            bcopia(nonce, t->scrivo.iv, 4);
            bcopia(nonce + 4, espl, 8);
            aes_gcm_cifra(&t->scrivo.gcm, nonce, aad, 13, b + AVANTI, b + AVANTI, n, tag);
        } else {
            nonce_di(&t->scrivo, nonce);
            chacha20(t->scrivo.chiave, 1, nonce, b + AVANTI, b + AVANTI, n);
            bcopia(b, aad, 13);
            for (k = 13; k < AVANTI; k++) b[k] = 0;
            tag_poly(b, n, t->scrivo.chiave, nonce, tag, 13);
        }
        bcopia(b + AVANTI + n, tag, 16);
        t->scrivo.seq++;

        hdr[0] = (unsigned char)tipo; hdr[1] = 3; hdr[2] = 3;
        metti16(hdr + 3, testa + n + 16);
        if (t->sotto->scrivi(t->sotto->stato, hdr, 5) != 5) return EXTLS_ERR_RETE;
        if (testa && t->sotto->scrivi(t->sotto->stato, espl, 8) != 8) return EXTLS_ERR_RETE;
        if (t->sotto->scrivi(t->sotto->stato, b + AVANTI, n + 16) != (int)(n + 16))
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
        if (t->cifrario == TLS_AES128) {
            aes_gcm_cifra(&t->scrivo.gcm, nonce, b, 5,
                          b + AVANTI, b + AVANTI, corpo, tag);
        } else {
            chacha20(t->scrivo.chiave, 1, nonce, b + AVANTI, b + AVANTI, corpo);
            tag_poly(b, corpo, t->scrivo.chiave, nonce, tag, 5);
        }
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
static void chiavi_da(const Tls *t, const unsigned char segreto[EXTLS_IMPRONTA],
                      Direzione *d)
{
    /* ! THE KEY LENGTH IS THE CIPHER'S: 16 bytes for AES-128, 32 for
     * ChaCha20. HKDF-Expand-Label takes the length as an input, so a key of
     * the wrong length is not a truncated right key — it is another key. */
    unsigned int lung = (t->cifrario == TLS_AES128) ? 16 : 32;

    extls_expand_label(segreto, "key", 0, 0, d->chiave, lung);
    extls_expand_label(segreto, "iv",  0, 0, d->iv, 12);
    if (t->cifrario == TLS_AES128) gcm_chiave(&d->gcm, d->chiave, 16);
    d->seq    = 0;
    d->attiva = 1;
}

/* =============================================================================
 * Il ClientHello
 * ========================================================================== */
static int manda_hello(Tls *t, const char *host, int secondo,
                       unsigned int gruppo,
                       const unsigned char *pubblica, unsigned int pub_n)
{
    unsigned char c[1024];
    unsigned int  i = 0, ext_inizio, ext_n;
    unsigned int  hl = lung(host);

    if (hl == 0 || hl > 255) return EXTLS_ERR_USO;

    c[i++] = 3; c[i++] = 3;                     /* legacy_version = 1.2 */
    if (!secondo) t->casuale(t->ch_random, 32);
    bcopia(c + i, t->ch_random, 32); i += 32;   /* random: the same twice */

    /* ! LA SESSIONE FINTA C'E' APPOSTA. In 1.3 non serve a niente, ma un
     * ClientHello senza session_id viene scartato da certi apparati che
     * credono di guardare una 1.2. Trentadue byte casuali che il server
     * rimanda indietro identici. */
    c[i++] = 32;
    if (!secondo) t->casuale(t->ch_sessione, 32);
    bcopia(c + i, t->ch_sessione, 32); i += 32;

    metti16(c + i, 12); i += 2;                 /* cipher_suites */
    metti16(c + i, TLS_CHACHA); i += 2;         /* TLS_CHACHA20_POLY1305_SHA256 */
    metti16(c + i, TLS_AES128); i += 2;         /* TLS_AES_128_GCM_SHA256 */
    /* and for a 1.2 server: ECDHE with the same two AEADs */
    metti16(c + i, TLS12_ECDHE_ECDSA_CHACHA); i += 2;
    metti16(c + i, TLS12_ECDHE_RSA_CHACHA); i += 2;
    metti16(c + i, TLS12_ECDHE_ECDSA_AES128); i += 2;
    metti16(c + i, TLS12_ECDHE_RSA_AES128); i += 2;

    c[i++] = 1; c[i++] = 0;                     /* compressione: nessuna */

    ext_inizio = i; i += 2;

    /* server_name (0) */
    metti16(c + i, 0); i += 2;
    metti16(c + i, hl + 5); i += 2;
    metti16(c + i, hl + 3); i += 2;
    c[i++] = 0;
    metti16(c + i, hl); i += 2;
    bcopia(c + i, host, hl); i += hl;

    /* supported_groups (10): x25519, then secp256r1 — see GR_P256 */
    metti16(c + i, 10); i += 2;
    metti16(c + i, 6);  i += 2;
    metti16(c + i, 4);  i += 2;
    metti16(c + i, GR_X25519); i += 2;
    metti16(c + i, GR_P256); i += 2;

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
    /* ! rsa_pkcs1_sha256 (0x0401) IS FOR 1.2 ONLY: TLS 1.3 forbids it in a
     * CertificateVerify, and a 1.3 server will not pick it — while a 1.2
     * server with an RSA certificate often signs its ServerKeyExchange so.
     * It is verified by excert, the code of the certificate signatures. */
    metti16(c + i, 13); i += 2;
    metti16(c + i, 10); i += 2;
    metti16(c + i, 8);  i += 2;
    metti16(c + i, 0x0403); i += 2;             /* ecdsa_secp256r1_sha256 */
    metti16(c + i, 0x0503); i += 2;             /* ecdsa_secp384r1_sha384 */
    metti16(c + i, 0x0804); i += 2;             /* rsa_pss_rsae_sha256 */
    metti16(c + i, 0x0401); i += 2;             /* rsa_pkcs1_sha256 (1.2) */

    /* supported_versions (43): 1.3, and 1.2 after it */
    metti16(c + i, 43); i += 2;
    metti16(c + i, 5);  i += 2;
    c[i++] = 4;
    metti16(c + i, 0x0304); i += 2;
    metti16(c + i, 0x0303); i += 2;

    /* For 1.2 servers, and ignored by 1.3 ones:
     *   ec_point_formats (11): uncompressed only;
     *   extended_master_secret (23): the master secret bound to the whole
     *     handshake (RFC 7627), against the «triple handshake» attack;
     *   renegotiation_info (0xff01), empty: we never renegotiate, and a
     *     server that knows RFC 5746 wants to hear it said. */
    metti16(c + i, 11); i += 2; metti16(c + i, 2); i += 2; c[i++] = 1; c[i++] = 0;
    metti16(c + i, 23); i += 2; metti16(c + i, 0); i += 2;
    metti16(c + i, 0xff01); i += 2; metti16(c + i, 1); i += 2; c[i++] = 0;

    /* key_share (51): one key, of the group asked (x25519 the first time) */
    metti16(c + i, 51); i += 2;
    metti16(c + i, pub_n + 6); i += 2;
    metti16(c + i, pub_n + 4); i += 2;
    metti16(c + i, gruppo); i += 2;
    metti16(c + i, pub_n); i += 2;
    bcopia(c + i, pubblica, pub_n); i += pub_n;

    /* cookie (44): echoed as it came, when the HelloRetryRequest had one */
    if (secondo && t->cookie_n) {
        metti16(c + i, 44); i += 2;
        metti16(c + i, t->cookie_n + 2); i += 2;
        metti16(c + i, t->cookie_n); i += 2;
        bcopia(c + i, t->cookie, t->cookie_n); i += t->cookie_n;
    }

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
static int leggi_hello(Tls *t, const unsigned char *p, unsigned int n,
                       unsigned char altrui[65], unsigned int *altrui_n,
                       int *hrr)
{
    unsigned int i = 0, ext_n, fine;
    int          visto_chiave = 0, visto_versione = 0;

    if (n < 38) return EXTLS_ERR_PROTOCOLLO;
    bcopia(t->sh_random, p + 2, 32);
    i += 2 + 32;                                /* versione + random */

    /* ! IL RANDOM DEL HelloRetryRequest E' UNA COSTANTE, ed e' l'unico modo di
     * riconoscerlo: e' un ServerHello a tutti gli effetti. Dal 23 settembre
     * 2026 si GESTISCE (vedi GR_P256): qui si riconosce e si legge, e chi
     * chiama rimanda il ClientHello. */
    {
        static const unsigned char HRR[8] = {
            0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11
        };
        unsigned int k; int uguale = 1;
        for (k = 0; k < 8; k++) if (p[2 + k] != HRR[k]) { uguale = 0; break; }
        *hrr = uguale;
    }

    if (i >= n) return EXTLS_ERR_PROTOCOLLO;
    i += 1 + p[i];                              /* legacy_session_id_echo */
    if (i + 3 > n) return EXTLS_ERR_PROTOCOLLO;

    t->cifrario = be16(p + i);
    if (t->cifrario != TLS_CHACHA && t->cifrario != TLS_AES128 &&
        !e_cifrario12(t->cifrario)) return EXTLS_ERR_CIFRARIO;
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
        } else if (tipo == 23) {                /* extended_master_secret */
            t->ems = 1;
        } else if (tipo == 51 && *hrr) {        /* key_share: the group wanted */
            if (len != 2) return EXTLS_ERR_PROTOCOLLO;
            t->gruppo = be16(p + i);
            visto_chiave = 1;
        } else if (tipo == 51) {                /* key_share: the server's key */
            unsigned int kn;

            if (len < 4) return EXTLS_ERR_PROTOCOLLO;
            t->gruppo = be16(p + i);
            kn = be16(p + i + 2);
            if (kn + 4 != len) return EXTLS_ERR_PROTOCOLLO;
            if (!((t->gruppo == GR_X25519 && kn == 32) ||
                  (t->gruppo == GR_P256 && kn == 65))) return EXTLS_ERR_PROTOCOLLO;
            bcopia(altrui, p + i + 4, kn);
            *altrui_n = kn;
            visto_chiave = 1;
        } else if (tipo == 44 && *hrr) {        /* cookie, to echo */
            if (len < 2 || be16(p + i) + 2 != len || len - 2 > COOKIE_MAX)
                return EXTLS_ERR_PROTOCOLLO;
            t->cookie_n = len - 2;
            bcopia(t->cookie, p + i + 2, t->cookie_n);
        }
        i += len;
    }

    if (!visto_versione) {
        /* ! NO supported_versions: THE SERVER SPEAKS 1.2 (RFC 8446 4.2.1).
         * The legacy version must then say 1.2, the cipher must be a 1.2 one,
         * and the random must not carry the downgrade sentinel. */
        static const unsigned char GIU[8] = { 'D','O','W','N','G','R','D',1 };
        unsigned int k; int giu = 1;

        if (p[0] != 3 || p[1] != 3 || *hrr) return EXTLS_ERR_VERSIONE;
        if (!e_cifrario12(t->cifrario)) return EXTLS_ERR_CIFRARIO;
        for (k = 0; k < 8; k++) if (t->sh_random[24 + k] != GIU[k]) { giu = 0; break; }
        if (giu) return EXTLS_ERR_VERSIONE;
        t->v12 = 1;
        return EXTLS_OK;
    }
    if (e_cifrario12(t->cifrario)) return EXTLS_ERR_CIFRARIO;
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
/* =============================================================================
 * TLS 1.2: the PRF, the certificates, the handshake — see TLS12_* above
 * ========================================================================== */

/* P_SHA256 (RFC 5246, 5): as many bytes as asked, from a secret, a label and
 * a seed. */
static void prf12(const unsigned char *segreto, unsigned int segreto_n,
                  const char *etichetta, const unsigned char *seme,
                  unsigned int seme_n, unsigned char *out, unsigned int n)
{
    unsigned char ls[96], a[EXTLS_IMPRONTA], buf[EXTLS_IMPRONTA + 96];
    unsigned char blocco[EXTLS_IMPRONTA], nuova[EXTLS_IMPRONTA];
    unsigned int  l = lung(etichetta), ls_n, k;

    bcopia(ls, etichetta, l);
    bcopia(ls + l, seme, seme_n);
    ls_n = l + seme_n;

    extls_hmac(segreto, segreto_n, ls, ls_n, a);            /* A(1) */
    while (n > 0) {
        bcopia(buf, a, EXTLS_IMPRONTA);
        bcopia(buf + EXTLS_IMPRONTA, ls, ls_n);
        extls_hmac(segreto, segreto_n, buf, EXTLS_IMPRONTA + ls_n, blocco);
        k = n < EXTLS_IMPRONTA ? n : EXTLS_IMPRONTA;
        bcopia(out, blocco, k);
        out += k;
        n -= k;
        extls_hmac(segreto, segreto_n, a, EXTLS_IMPRONTA, nuova);
        bcopia(a, nuova, EXTLS_IMPRONTA);
    }
}

/* The Certificate of 1.2: a list of certificates, WITHOUT the request context
 * and the per-entry extensions that 1.3 added. */
static int leggi_certificati12(const unsigned char *p, unsigned int n,
                               ExCert *catena, unsigned int max,
                               unsigned int *quanti)
{
    unsigned int i = 3, fine;

    *quanti = 0;
    if (n < 3) return EXTLS_ERR_PROTOCOLLO;
    fine = 3 + be24(p);
    if (fine > n) return EXTLS_ERR_PROTOCOLLO;

    while (i + 3 <= fine && *quanti < max) {
        unsigned int len = be24(p + i);

        i += 3;
        if (i + len > fine) return EXTLS_ERR_PROTOCOLLO;
        if (excert_analizza(p + i, len, &catena[*quanti]) != 0)
            return EXTLS_ERR_CERTIFICATO;
        (*quanti)++;
        i += len;
    }
    if (*quanti == 0) return EXTLS_ERR_CERTIFICATO;
    return EXTLS_OK;
}

/* The signature of the ServerKeyExchange, over client_random, server_random
 * and the ECDH parameters.
 *
 * ! PKCS#1 v1.5 AND ECDSA GO THROUGH excert_firma_valida, the code that
 * already verifies every certificate: a certificate signature and this one
 * are the same computation on different bytes, so a «certificate» is built
 * with the signed bytes as its body. RSA-PSS goes where 1.3 sends it. */
static int firma12(const ExCert *sito, unsigned int alg,
                   const unsigned char *firmato, unsigned int firmato_n,
                   const unsigned char *firma, unsigned int firma_n)
{
    ExCert f;

    if (alg == 0x0804) {
        unsigned char h[EXTLS_IMPRONTA];

        if (sito->tipo_chiave != EXASN1_CHIAVE_RSA) return EXTLS_ERR_FIRMA;
        sha256(firmato, firmato_n, h);
        return extls_rsa_pss_verifica(sito->chiave_modulo.p, sito->chiave_modulo.n,
                                      sito->chiave_esponente.p, sito->chiave_esponente.n,
                                      h, firma, firma_n, EXTLS_IMPRONTA) == 0
               ? EXTLS_OK : EXTLS_ERR_FIRMA;
    }

    bzero_(&f, sizeof(f));
    f.tbs.p = firmato;  f.tbs.n = firmato_n;
    f.firma.p = firma;  f.firma.n = firma_n;
    if (alg == 0x0401)      f.alg_firma = EXASN1_ALG_RSA_SHA256;
    else if (alg == 0x0403) f.alg_firma = EXASN1_ALG_ECDSA_SHA256;
    else if (alg == 0x0503) f.alg_firma = EXASN1_ALG_ECDSA_SHA384;
    else return EXTLS_ERR_FIRMA;       /* not what we offered */

    return excert_firma_valida(&f, sito) == EXCERT_OK ? EXTLS_OK : EXTLS_ERR_FIRMA;
}

static int stretta12(Tls *t, const char *host, const ExMagazzino *magazzino,
                     const char *adesso, const unsigned char x_privata[32])
{
    ExCert        catena[8];
    unsigned int  quanti = 0, gruppo = 0, punto_n = 0, k;
    unsigned char punto[65], pms[32], ms[48], impronta[EXTLS_IMPRONTA];
    int           visto_cert = 0, visto_skx = 0, chiesto_cert = 0, r;

    /* --- what the server says in clear ----------------------------------- */
    for (;;) {
        unsigned int tipo, n;
        const unsigned char *corpo;

        r = hs_prossimo(t, &tipo, &corpo, &n, 15000);
        if (r != EXTLS_OK) return r;

        if (tipo == 11) {                       /* Certificate */
            r = leggi_certificati12(corpo, n, catena, 8, &quanti);
            if (r != EXTLS_OK) return r;
            visto_cert = 1;
            if (!passo(EXTLS_P_CERTIFICATI)) return EXTLS_ERR_RETE;
            continue;
        }
        if (tipo == 22) continue;               /* CertificateStatus: not asked */

        if (tipo == 12) {                       /* ServerKeyExchange */
            unsigned char firmato[64 + 4 + 65];
            unsigned int  par_n, alg, firma_n;

            if (!visto_cert || n < 4 || corpo[0] != 3) return EXTLS_ERR_PROTOCOLLO;
            gruppo  = be16(corpo + 1);
            punto_n = corpo[3];
            if (!((gruppo == GR_X25519 && punto_n == 32) ||
                  (gruppo == GR_P256 && punto_n == 65))) return EXTLS_ERR_PROTOCOLLO;
            par_n = 4 + punto_n;
            if (par_n + 4 > n) return EXTLS_ERR_PROTOCOLLO;
            alg     = be16(corpo + par_n);
            firma_n = be16(corpo + par_n + 2);
            if (par_n + 4 + firma_n > n) return EXTLS_ERR_PROTOCOLLO;

            bcopia(firmato, t->ch_random, 32);
            bcopia(firmato + 32, t->sh_random, 32);
            bcopia(firmato + 64, corpo, par_n);
            r = firma12(&catena[0], alg, firmato, 64 + par_n,
                        corpo + par_n + 4, firma_n);
            if (r != EXTLS_OK) return r;

            bcopia(punto, corpo + 4, punto_n);
            visto_skx = 1;
            if (!passo(EXTLS_P_FIRMA)) return EXTLS_ERR_RETE;
            continue;
        }

        if (tipo == 13) { chiesto_cert = 1; continue; }     /* CertificateRequest */

        if (tipo == 14) break;                  /* ServerHelloDone */
        return EXTLS_ERR_PROTOCOLLO;
    }
    if (!visto_cert || !visto_skx) return EXTLS_ERR_PROTOCOLLO;

    /* --- the certificate is for THIS site, and comes from a real CA ------- */
    if (!passo(EXTLS_P_CATENA)) return EXTLS_ERR_RETE;
    r = excert_catena_valida(catena, quanti, magazzino, adesso, &t->anello,
                             passo_anello, 0);
    if (r == EXCERT_ANNULLATO) return EXTLS_ERR_RETE;
    if (r != EXCERT_OK) { t->motivo = r; return EXTLS_ERR_CERTIFICATO; }
    if (excert_nome_combacia(&catena[0], host) != EXCERT_OK) return EXTLS_ERR_NOME;

    /* --- our half of ECDHE, and the pre-master secret -------------------- */
    {
        unsigned char nostro[66];
        unsigned int  nostro_n;

        if (gruppo == GR_P256) {
            unsigned char priv[32];
            int tentativi = 0;

            do t->casuale(priv, 32);
            while (p256_pubblica(priv, nostro + 1) != 0 && ++tentativi < 8);
            if (tentativi >= 8) return EXTLS_ERR_PROTOCOLLO;
            if (p256_condiviso(priv, punto, 65, pms) != 0) return EXTLS_ERR_PROTOCOLLO;
            nostro_n = 65;
        } else {
            if (x25519_pubblica(nostro + 1, x_privata) != 0) return EXTLS_ERR_PROTOCOLLO;
            if (x25519(pms, x_privata, punto) != 0) return EXTLS_ERR_PROTOCOLLO;
            nostro_n = 32;
        }
        nostro[0] = (unsigned char)nostro_n;
        if (!passo(EXTLS_P_SEGRETO)) return EXTLS_ERR_RETE;

        /* ! A CertificateRequest GETS AN EMPTY Certificate: we have none to
         * give, and saying so is allowed; the server decides whether it is
         * enough. Silence would be a protocol error. */
        if (chiesto_cert) {
            static const unsigned char vuoto[3] = { 0, 0, 0 };
            r = hs_manda(t, 11, vuoto, 3);
            if (r != EXTLS_OK) return r;
        }
        r = hs_manda(t, 16, nostro, nostro_n + 1);          /* ClientKeyExchange */
        if (r != EXTLS_OK) return r;
    }

    /* --- the master secret, and the keys ---------------------------------- */
    {
        unsigned char seme[64], blocco[88];
        unsigned int  kl = e_aes12(t->cifrario) ? 16 : 32;
        unsigned int  il = e_aes12(t->cifrario) ? 4 : 12;

        if (t->ems) {
            /* ! THE HASH OF THE HANDSHAKE SO FAR, ClientKeyExchange included. */
            trascr_impronta(t, impronta);
            prf12(pms, 32, "extended master secret", impronta, EXTLS_IMPRONTA, ms, 48);
        } else {
            bcopia(seme, t->ch_random, 32);
            bcopia(seme + 32, t->sh_random, 32);
            prf12(pms, 32, "master secret", seme, 64, ms, 48);
        }

        bcopia(seme, t->sh_random, 32);
        bcopia(seme + 32, t->ch_random, 32);
        prf12(ms, 48, "key expansion", seme, 64, blocco, 2 * kl + 2 * il);

        bcopia(t->scrivo.chiave, blocco, kl);
        bcopia(t->leggo.chiave, blocco + kl, kl);
        bcopia(t->scrivo.iv, blocco + 2 * kl, il);
        bcopia(t->leggo.iv, blocco + 2 * kl + il, il);
        if (kl == 16) {
            gcm_chiave(&t->scrivo.gcm, t->scrivo.chiave, 16);
            gcm_chiave(&t->leggo.gcm, t->leggo.chiave, 16);
        }
        t->scrivo.seq = t->leggo.seq = 0;
        for (k = 0; k < sizeof(blocco); k++) blocco[k] = 0;
    }

    /* --- ChangeCipherSpec, then our Finished, encrypted ------------------ */
    {
        unsigned char uno = 1, vd[12];

        r = record_scrivi(t, 20, &uno, 1);
        if (r != EXTLS_OK) return r;
        t->scrivo.attiva = 1;

        trascr_impronta(t, impronta);
        prf12(ms, 48, "client finished", impronta, EXTLS_IMPRONTA, vd, 12);
        r = hs_manda(t, 20, vd, 12);
        if (r != EXTLS_OK) return r;
    }

    /* --- the server's ChangeCipherSpec (record_leggi turns the keys on) and
     *     its Finished -------------------------------------------------------- */
    t->leggo_pronta = 1;
    {
        unsigned int tipo, n;
        const unsigned char *corpo;
        unsigned char atteso[12];
        unsigned int  salva;

        r = hs_prossimo(t, &tipo, &corpo, &n, 15000);
        if (r != EXTLS_OK) return r;
        if (tipo != 20 || n != 12 || !t->leggo.attiva) return EXTLS_ERR_FINISHED;

        salva = t->hs_off;
        t->hs_off = salva - (n + 4);
        trascr_impronta(t, impronta);
        t->hs_off = salva;
        prf12(ms, 48, "server finished", impronta, EXTLS_IMPRONTA, atteso, 12);
        {
            unsigned char diff = 0;
            for (k = 0; k < 12; k++) diff |= (unsigned char)(atteso[k] ^ corpo[k]);
            if (diff) return EXTLS_ERR_FINISHED;
        }
    }

    for (k = 0; k < 48; k++) ms[k] = 0;
    for (k = 0; k < 32; k++) pms[k] = 0;
    t->trascr_n = t->hs_off = 0;
    t->pos = t->fine = 0;
    passo(EXTLS_P_FATTO);
    return EXTLS_OK;
}

int extls_stretta(void *opaco, const ExTlsSotto *sotto, const char *host,
                  const ExMagazzino *magazzino, const char *adesso,
                  void (*casuale)(unsigned char *, unsigned int))
{
    Tls *t = (Tls *)opaco;
    unsigned char privata[32], pubblica[32], altrui[65], condiviso[32];
    unsigned int  altrui_n = 0;
    int           hrr = 0;
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

    r = manda_hello(t, host, 0, GR_X25519, pubblica, 32);
    if (r != EXTLS_OK) return r;

    /* --- il ServerHello --------------------------------------------------- */
    {
        unsigned int tipo, n, ch1_n = t->trascr_n;
        const unsigned char *corpo;

        r = hs_prossimo(t, &tipo, &corpo, &n, 15000);
        if (r != EXTLS_OK) return r;
        if (tipo != 2) return EXTLS_ERR_PROTOCOLLO;

        r = leggi_hello(t, corpo, n, altrui, &altrui_n, &hrr);
        if (r != EXTLS_OK) return r;

        if (hrr) {
            unsigned int cifrario_hrr = t->cifrario, hrr_n = t->trascr_n - ch1_n, k;
            unsigned char h1[32];

            /* ! ONLY P-256 CAN BE ASKED FOR: x25519 was already given, and a
             * request for it would be a server going round in circles. */
            if (t->gruppo != GR_P256) return EXTLS_ERR_HRR;

            /* The transcript: ClientHello1 becomes message_hash(its hash). */
            sha256(t->trascr, ch1_n, h1);
            for (k = 0; k < hrr_n; k++) t->trascr[36 + k] = t->trascr[ch1_n + k];
            t->trascr[0] = 254; t->trascr[1] = 0; t->trascr[2] = 0; t->trascr[3] = 32;
            bcopia(t->trascr + 4, h1, 32);
            t->trascr_n = t->hs_off = 36 + hrr_n;

            /* The P-256 key, now that someone wants it. A private key of zero
             * or above the order is drawn again: it means nothing about the
             * source, it happens once in 2^32 draws. */
            {
                unsigned char pub256[65];
                int tentativi = 0;

                do casuale(privata, 32);
                while (p256_pubblica(privata, pub256) != 0 && ++tentativi < 8);
                if (tentativi >= 8) return EXTLS_ERR_PROTOCOLLO;

                r = manda_hello(t, host, 1, GR_P256, pub256, 65);
                if (r != EXTLS_OK) return r;
            }

            r = hs_prossimo(t, &tipo, &corpo, &n, 15000);
            if (r != EXTLS_OK) return r;
            if (tipo != 2) return EXTLS_ERR_PROTOCOLLO;
            r = leggi_hello(t, corpo, n, altrui, &altrui_n, &hrr);
            if (r != EXTLS_OK) return r;

            /* ! A SECOND HelloRetryRequest, ANOTHER GROUP OR ANOTHER CIPHER
             * are all protocol errors (4.1.4): the server must stick to what
             * it asked for. */
            if (hrr || t->gruppo != GR_P256 || t->cifrario != cifrario_hrr)
                return EXTLS_ERR_PROTOCOLLO;
        } else if (t->v12) {
            /* 1.2: the key exchange is in the ServerKeyExchange. */
        } else if (t->gruppo != GR_X25519) {
            /* Without a retry the only key we sent is x25519's. */
            return EXTLS_ERR_PROTOCOLLO;
        }
    }
    if (t->v12) {
        if (!passo(EXTLS_P_HELLO)) return EXTLS_ERR_RETE;
        return stretta12(t, host, magazzino, adesso, privata);
    }
    if (!passo(EXTLS_P_HELLO)) return EXTLS_ERR_RETE;

    if (t->gruppo == GR_P256) {
        /* The peer point is checked inside: on the curve, or nothing. */
        if (p256_condiviso(privata, altrui, altrui_n, condiviso) != 0)
            return EXTLS_ERR_PROTOCOLLO;
    } else {
        /* ! UN SEGRETO TUTTO ZERI SI RIFIUTA. Vuol dire che il punto ricevuto
         * era di ordine piccolo: il «segreto» condiviso lo conoscerebbe anche
         * chi guarda. x25519() lo dice, e qui si smette. */
        if (x25519(condiviso, privata, altrui) != 0) return EXTLS_ERR_PROTOCOLLO;
    }
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

    chiavi_da(t, s_hs, &t->leggo);

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
    r = excert_catena_valida(catena, quanti, magazzino, adesso, &t->anello,
                             passo_anello, 0);

    /* ! ANNULLATO NON E' RIFIUTATO. Se chi ospita ha chiuso la finestra a meta'
     * della catena, il certificato non ha nessuna colpa: dirgli
     * EXTLS_ERR_CERTIFICATO manderebbe a cercare una CA che manca. E' la stessa
     * uscita degli altri passi che annullano — la connessione non si e'
     * stabilita, e questa e' la verita' piu' vicina. */
    if (r == EXCERT_ANNULLATO) return EXTLS_ERR_RETE;
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

    chiavi_da(t, c_hs, &t->scrivo);

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

    chiavi_da(t, t->c_app, &t->scrivo);
    chiavi_da(t, t->s_app, &t->leggo);

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

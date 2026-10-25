/* =============================================================================
 * tools/prove/sslprova.c — OpenSSL 4.1 with sockets and threads, inside EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     sslprova fili                     4 threads hash, encrypt and make
 *                                       SSL_CTXs at the same time
 *     sslprova locale                   a TLS server and a client in one
 *                                       process, over 127.0.0.1 (MariaDB's
 *                                       case: SSL_set_fd on accepted sockets)
 *     sslprova rete <host> <ca file>    TLS to <host>:443 on a real socket,
 *                                       the certificate checked, HEAD /
 *
 * One line per check, then "SSL fine <failures>":
 *   SSL ok <what>   /   SSL NO <what>
 * Built by tools/prova_openssl.sh against tools/openssl-exos/costruisci.sh:
 *   x86_64-exos-gcc -O2 sslprova.c -o sslprova -lssl -lcrypto
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/crypto.h>

static int g_male;

static void prova(const char *cosa, int giusto)
{
    printf("SSL %s %s\n", giusto ? "ok" : "NO", cosa);
    if (!giusto) {
        g_male++;
        ERR_print_errors_fp(stdout);
    }
    fflush(stdout);
}

/* --- fili ------------------------------------------------------------------ */

/* SHA-256 of 1000 x "EX-OS", as Linux computes it:
 *   python3 -c 'import hashlib;print(hashlib.sha256(b"EX-OS"*1000).hexdigest())' */
static const char *ATTESO =
    "e6a2f2a3200c4accafac86abeaf9965913d82d33bf4c0d23117cc09fb08cd9da";
static char g_hash[4][65];
static int  g_filo_ok[4];

static void esadecimale(const unsigned char *b, unsigned int n, char *out)
{
    for (unsigned int i = 0; i < n; i++)
        sprintf(out + 2 * i, "%02x", b[i]);
}

static void *filo(void *arg)
{
    int id = (int)(long)arg, ok = 1;
    static const unsigned char chiave[32] = "chiave di prova di EX-OS 32 byte";
    unsigned char iv[12] = { 0 }, tag[16];
    char testo[5001];

    for (int i = 0; i < 1000; i++)
        memcpy(testo + 5 * i, "EX-OS", 5);
    testo[5000] = 0;
    for (int giro = 0; giro < 50 && ok; giro++) {
        unsigned char md[32], cif[5016], dec[5016];
        unsigned int n = 0;
        int l1 = 0, l2 = 0, l3 = 0;

        ok = EVP_Digest(testo, 5000, md, &n, EVP_sha256(), NULL);
        esadecimale(md, n, g_hash[id]);
        /* AES-256-GCM there and back */
        EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
        ok = ok && EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), NULL, chiave, iv)
                && EVP_EncryptUpdate(c, cif, &l1, (unsigned char *)testo, 5000)
                && EVP_EncryptFinal_ex(c, cif + l1, &l2)
                && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, tag);
        EVP_CIPHER_CTX_free(c);
        c = EVP_CIPHER_CTX_new();
        ok = ok && EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), NULL, chiave, iv)
                && EVP_DecryptUpdate(c, dec, &l3, cif, l1 + l2)
                && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag)
                && EVP_DecryptFinal_ex(c, dec + l3, &l2)
                && l3 == 5000 && memcmp(dec, testo, 5000) == 0;
        EVP_CIPHER_CTX_free(c);
        /* an SSL_CTX: the provider store and the name maps, under their locks */
        SSL_CTX *ctx = SSL_CTX_new(TLS_method());
        ok = ok && ctx != NULL && SSL_CTX_set_cipher_list(ctx, "HIGH");
        SSL_CTX_free(ctx);
        iv[giro % 12]++;
    }
    g_filo_ok[id] = ok;
    return NULL;
}

static void prova_fili(void)
{
    pthread_t f[4];
    char riga[200];
    int creati = 0;

    for (long i = 0; i < 4; i++)
        if (pthread_create(&f[i], NULL, filo, (void *)i) == 0)
            creati++;
    for (int i = 0; i < creati; i++)
        pthread_join(f[i], NULL);
    snprintf(riga, sizeof riga, "4 fili insieme: sha256, aes-256-gcm, SSL_CTX (%d creati)", creati);
    prova(riga, creati == 4 && g_filo_ok[0] && g_filo_ok[1] && g_filo_ok[2] && g_filo_ok[3]);
    snprintf(riga, sizeof riga, "sha256 di 1000 x EX-OS = %.16s...", g_hash[0]);
    prova(riga, strcmp(g_hash[0], g_hash[1]) == 0 && strcmp(g_hash[0], g_hash[2]) == 0 &&
                strcmp(g_hash[0], g_hash[3]) == 0 && strcmp(g_hash[0], ATTESO) == 0);
}

/* --- locale: server and client over 127.0.0.1 ------------------------------ */

static SSL_CTX *g_srv;
static int      g_ascolto = -1, g_porta;
static char     g_ricevuto[64];
static int      g_server_ok;

/* a self-signed Ed25519 certificate, made here: no file needed */
static int certificato(SSL_CTX *ctx)
{
    EVP_PKEY *k = EVP_PKEY_Q_keygen(NULL, NULL, "ED25519");
    X509 *x = X509_new();
    int ok = k && x;

    if (ok) {
        X509_NAME *n = X509_NAME_new();
        ASN1_INTEGER_set(X509_get_serialNumber(x), 1);
        X509_gmtime_adj(X509_getm_notBefore(x), -3600);
        X509_gmtime_adj(X509_getm_notAfter(x), 86400);
        X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, (unsigned char *)"exos", -1, -1, 0);
        X509_set_subject_name(x, n);
        X509_set_issuer_name(x, n);
        X509_NAME_free(n);
        X509_set_pubkey(x, k);
        ok = X509_sign(x, k, NULL) > 0 && SSL_CTX_use_certificate(ctx, x) == 1 &&
             SSL_CTX_use_PrivateKey(ctx, k) == 1;
    }
    X509_free(x);
    EVP_PKEY_free(k);
    return ok;
}

static void *server(void *arg)
{
    struct sockaddr_in da;
    socklen_t l = sizeof da;
    int fd = accept(g_ascolto, (struct sockaddr *)&da, &l);
    SSL *s;

    (void)arg;
    if (fd < 0) return NULL;
    s = SSL_new(g_srv);
    SSL_set_fd(s, fd);
    if (SSL_accept(s) == 1) {
        int n = SSL_read(s, g_ricevuto, sizeof g_ricevuto - 1);
        if (n > 0) {
            g_ricevuto[n] = 0;
            g_server_ok = SSL_write(s, "risposta di EX-OS", 17) == 17;
        }
        SSL_shutdown(s);
    }
    SSL_free(s);
    close(fd);
    return NULL;
}

static void prova_locale(void)
{
    struct sockaddr_in a;
    socklen_t l = sizeof a;
    pthread_t f;
    char riga[200], risposta[64] = "";
    SSL_CTX *cli;
    SSL *s;
    int fd, ok = 0;

    g_srv = SSL_CTX_new(TLS_server_method());
    prova("certificato Ed25519 fatto sul posto", g_srv && certificato(g_srv));
    g_ascolto = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (g_ascolto < 0 || bind(g_ascolto, (struct sockaddr *)&a, sizeof a) < 0 ||
        listen(g_ascolto, 2) < 0 || getsockname(g_ascolto, (struct sockaddr *)&a, &l) < 0) {
        prova("socket in ascolto su 127.0.0.1", 0);
        return;
    }
    g_porta = ntohs(a.sin_port);
    snprintf(riga, sizeof riga, "socket in ascolto su 127.0.0.1:%d", g_porta);
    prova(riga, 1);

    /* connect first (the listen queue holds it), then the server thread
     * accepts: if connect fails nobody is left waiting in accept */
    cli = SSL_CTX_new(TLS_client_method());
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        snprintf(riga, sizeof riga, "connect a 127.0.0.1:%d (errno %d, %s)", g_porta, errno,
                 strerror(errno));
        prova(riga, 0);
        SSL_CTX_free(cli);
        SSL_CTX_free(g_srv);
        close(g_ascolto);
        return;
    }
    prova("connect a 127.0.0.1", 1);
    pthread_create(&f, NULL, server, NULL);
    {
        s = SSL_new(cli);
        SSL_set_fd(s, fd);
        if (SSL_connect(s) == 1) {
            snprintf(riga, sizeof riga, "stretta di mano locale: %s %s", SSL_get_version(s),
                     SSL_get_cipher(s));
            prova(riga, 1);
            if (SSL_write(s, "domanda del client", 18) == 18) {
                int n = SSL_read(s, risposta, sizeof risposta - 1);
                if (n > 0) risposta[n] = 0;
                ok = strcmp(risposta, "risposta di EX-OS") == 0;
            }
            SSL_shutdown(s);
        } else {
            prova("stretta di mano locale", 0);
        }
        SSL_free(s);
        close(fd);
    }
    pthread_join(f, NULL);
    snprintf(riga, sizeof riga, "dati cifrati nei due sensi: \"%s\" / \"%s\"", g_ricevuto, risposta);
    prova(riga, ok && g_server_ok && strcmp(g_ricevuto, "domanda del client") == 0);
    SSL_CTX_free(cli);
    SSL_CTX_free(g_srv);
    close(g_ascolto);
}

/* --- rete: a real server, the certificate checked -------------------------- */

static void prova_rete(const char *host, const char *ca)
{
    struct addrinfo h, *r = NULL;
    char riga[300], buf[512];
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    SSL *s;
    int fd = -1, n;

    memset(&h, 0, sizeof h);
    h.ai_family = AF_INET;
    h.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "https", &h, &r) != 0 || !r) {
        prova("getaddrinfo", 0);
        return;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, r->ai_addr, r->ai_addrlen) != 0) {
        prova("connect alla porta 443", 0);
        freeaddrinfo(r);
        return;
    }
    freeaddrinfo(r);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    prova("magazzino delle CA caricato", SSL_CTX_load_verify_locations(ctx, ca, NULL) == 1);
    s = SSL_new(ctx);
    SSL_set_fd(s, fd);
    SSL_set_tlsext_host_name(s, host);
    X509_VERIFY_PARAM_set1_host(SSL_get0_param(s), host, 0);
    if (SSL_connect(s) != 1) {
        snprintf(riga, sizeof riga, "stretta di mano con %s (verifica: %s)", host,
                 X509_verify_cert_error_string(SSL_get_verify_result(s)));
        prova(riga, 0);
    } else {
        snprintf(riga, sizeof riga, "stretta di mano con %s: %s %s, certificato verificato", host,
                 SSL_get_version(s), SSL_get_cipher(s));
        prova(riga, SSL_get_verify_result(s) == X509_V_OK);
        snprintf(buf, sizeof buf, "HEAD / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);
        SSL_write(s, buf, (int)strlen(buf));
        n = SSL_read(s, buf, sizeof buf - 1);
        buf[n > 0 ? n : 0] = 0;
        buf[strcspn(buf, "\r\n")] = 0;
        snprintf(riga, sizeof riga, "risposta: %s", buf);
        prova(riga, strncmp(buf, "HTTP/1.1 ", 9) == 0);
        SSL_shutdown(s);
    }
    SSL_free(s);
    SSL_CTX_free(ctx);
    close(fd);
}

int main(int argc, char **argv)
{
    char riga[200];

    snprintf(riga, sizeof riga, "%s", OpenSSL_version(OPENSSL_VERSION));
    prova(riga, OPENSSL_VERSION_MAJOR >= 4);
    if (argc >= 2 && strcmp(argv[1], "fili") == 0)
        prova_fili();
    else if (argc >= 2 && strcmp(argv[1], "locale") == 0)
        prova_locale();
    else if (argc >= 4 && strcmp(argv[1], "rete") == 0)
        prova_rete(argv[2], argv[3]);
    else {
        fprintf(stderr, "uso: sslprova fili | locale | rete <host> <ca>\n");
        return 2;
    }
    printf("SSL fine %d\n", g_male);
    return g_male != 0;
}

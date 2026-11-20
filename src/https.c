// Klient HTTPS na mbedTLS 2.28 (TLS 1.2) dla PSP.

#include "https.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pspkernel.h>
#include <psprtc.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl_internal.h>  // flaga resume w trakcie handshake'u

// Publiczne CA osadzone przy budowaniu (cacert.c).
extern const unsigned char ca_bundle[];
extern const unsigned int ca_bundle_len;

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt ca_chain;
static int g_ready = 0;
static char g_ca_source[96] = "brak";

#define MAX_RESPONSE (2 * 1024 * 1024)  // historia z 72 h potrafi mieć kilkaset KB

// Oddaje procesor na 1 ms. Bez tego oczekiwanie na dane kręci się w pętli
// i zagładza wątki sieciowe systemu oraz GUI.
static void yield(void) { sceKernelDelayThread(1000); }

// ---------------------------------------------------------------------------
// Losowość
// ---------------------------------------------------------------------------

// Dodatkowe źródło losowości: drgania zegara mikrosekundowego i zegara RTC.
// Domyślne getentropy() w pspdev to Mersenne Twister zasiany czasem, więc jest
// przewidywalne. To źródło je wzmacnia, ale nadal nie jest sprzętowym RNG.
// TODO: na prawdziwym PSP (tryb jądra, ARK-4) użyć generatora układu KIRK.
static int psp_jitter_entropy(void *data, unsigned char *out, size_t len, size_t *olen) {
    (void)data;
    for (size_t i = 0; i < len; i++) {
        unsigned int acc = 0;
        for (int round = 0; round < 8; round++) {
            volatile unsigned int spin = 0;
            unsigned int t0 = sceKernelGetSystemTimeLow();
            unsigned int n = (t0 ^ acc) & 0x3F;
            for (unsigned int k = 0; k < 64 + n; k++) spin += k * 2654435761u;
            unsigned int t1 = sceKernelGetSystemTimeLow();
            u64 tick = 0;
            sceRtcGetCurrentTick(&tick);
            acc = (acc << 5) ^ (acc >> 27) ^ (t1 - t0) ^ (unsigned int)tick ^ spin;
        }
        out[i] = (unsigned char)(acc ^ (acc >> 8) ^ (acc >> 16) ^ (acc >> 24));
    }
    *olen = len;
    return 0;
}

static void fmt_err(char *dst, size_t len, const char *where, int ret) {
    char msg[80];
    mbedtls_strerror(ret, msg, sizeof(msg));
    snprintf(dst, len, "%s: -0x%04X %s", where, (unsigned)-ret, msg);
}

int https_global_init(const char *ca_pem_path, char *err, size_t errlen) {
    if (g_ready) return 0;
    int ret;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&ca_chain);

    // Sprzętowy KIRK (jeśli rng_init() się udał) jest źródłem "silnym";
    // drgania zegara zostają jako dodatkowe, słabe źródło.
    if (rng_hw_available()) {
        ret = mbedtls_entropy_add_source(&entropy, rng_hw_entropy, NULL, 32,
                                         MBEDTLS_ENTROPY_SOURCE_STRONG);
        if (ret != 0) { fmt_err(err, errlen, "źródło KIRK", ret); return ret; }
    }
    mbedtls_entropy_add_source(&entropy, psp_jitter_entropy, NULL, 32,
                               MBEDTLS_ENTROPY_SOURCE_WEAK);

    const char *pers = "psp-homelab";
    ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                    (const unsigned char *)pers, strlen(pers));
    if (ret != 0) { fmt_err(err, errlen, "generator losowy", ret); return ret; }

    if (ca_pem_path) {
        ret = mbedtls_x509_crt_parse_file(&ca_chain, ca_pem_path);
        if (ret != 0) {  // dla własnego CA oczekujemy, że każdy certyfikat się wczyta
            fmt_err(err, errlen, "plik CA", ret < 0 ? ret : -1);
            return ret < 0 ? ret : -1;
        }
        snprintf(g_ca_source, sizeof(g_ca_source), "%s", ca_pem_path);
    } else {
        ret = mbedtls_x509_crt_parse(&ca_chain, ca_bundle, ca_bundle_len);
        if (ret < 0) { fmt_err(err, errlen, "wbudowane CA", ret); return ret; }
        snprintf(g_ca_source, sizeof(g_ca_source), "wbudowane publiczne CA");
    }

    g_ready = 1;
    return 0;
}

void https_global_free(void) {
    if (!g_ready) return;
    mbedtls_x509_crt_free(&ca_chain);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    g_ready = 0;
}

const char *https_ca_source(void) { return g_ca_source; }

// ---------------------------------------------------------------------------
// Połączenie
// ---------------------------------------------------------------------------

int https_client_init(https_client *c, const char *host, int port) {
    memset(c, 0, sizeof(*c));
    snprintf(c->host, sizeof(c->host), "%s", host);
    snprintf(c->port, sizeof(c->port), "%d", port);
    mbedtls_net_init(&c->sock);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_ssl_session_init(&c->session);

    int ret = mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return ret;
    // Tylko TLS 1.2, pełna weryfikacja certyfikatu i nazwy hosta.
    mbedtls_ssl_conf_min_version(&c->conf, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&c->conf, &ca_chain, NULL);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_read_timeout(&c->conf, 15000);
    mbedtls_ssl_conf_session_tickets(&c->conf, MBEDTLS_SSL_SESSION_TICKETS_ENABLED);
    c->conf_ready = 1;
    return 0;
}

void https_client_close(https_client *c) {
    if (c->connected) mbedtls_ssl_close_notify(&c->ssl);
    mbedtls_ssl_free(&c->ssl);
    mbedtls_net_free(&c->sock);
    mbedtls_ssl_init(&c->ssl);
    mbedtls_net_init(&c->sock);
    c->connected = 0;
}

void https_client_free(https_client *c) {
    https_client_close(c);
    mbedtls_ssl_session_free(&c->session);
    if (c->conf_ready) mbedtls_ssl_config_free(&c->conf);
    c->conf_ready = 0;
}

static void copy_cn(char *dst, size_t len, const mbedtls_x509_name *name) {
    char full[256];
    if (mbedtls_x509_dn_gets(full, sizeof(full), name) < 0) { snprintf(dst, len, "?"); return; }
    const char *cn = strstr(full, "CN=");
    const char *src = cn ? cn + 3 : full;
    size_t n = strcspn(src, ",");
    if (n >= len) n = len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int client_connect(https_client *c, char *err, size_t errlen) {
    int ret;
    if ((ret = mbedtls_net_connect(&c->sock, c->host, c->port, MBEDTLS_NET_PROTO_TCP)) != 0) {
        fmt_err(err, errlen, "połączenie TCP", ret);
        return ret;
    }
    if ((ret = mbedtls_ssl_setup(&c->ssl, &c->conf)) != 0) {
        fmt_err(err, errlen, "ssl_setup", ret);
        return ret;
    }
    if ((ret = mbedtls_ssl_set_hostname(&c->ssl, c->host)) != 0) {  // SNI + kontrola nazwy
        fmt_err(err, errlen, "hostname", ret);
        return ret;
    }
    if (c->have_session) mbedtls_ssl_set_session(&c->ssl, &c->session);
    mbedtls_ssl_set_bio(&c->ssl, &c->sock, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);

    // Handshake krok po kroku, żeby odczytać flagę "resume", zanim mbedTLS
    // zwolni dane handshake'u.
    unsigned int t0 = sceKernelGetSystemTimeLow();
    c->resumed = 0;
    ret = 0;
    while (c->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
        if (c->ssl.handshake && c->ssl.handshake->resume) c->resumed = 1;
        ret = mbedtls_ssl_handshake_step(&c->ssl);
        if (ret == 0) continue;
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            yield();
            ret = 0;
            continue;
        }
        break;
    }
    if (ret != 0) {
        uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);
        if (flags != 0 && flags != (uint32_t)-1) {
            char vinfo[96];
            mbedtls_x509_crt_verify_info(vinfo, sizeof(vinfo), "", flags);
            vinfo[strcspn(vinfo, "\n")] = '\0';
            snprintf(err, errlen, "certyfikat odrzucony: %s", vinfo);
        } else {
            fmt_err(err, errlen, "handshake TLS", ret);
        }
        c->have_session = 0;  // zła sesja nie powinna blokować kolejnych prób
        return ret;
    }
    c->handshake_ms = (sceKernelGetSystemTimeLow() - t0) / 1000;
    c->handshakes++;
    c->connected = 1;

    snprintf(c->tls_version, sizeof(c->tls_version), "%s", mbedtls_ssl_get_version(&c->ssl));
    snprintf(c->cipher, sizeof(c->cipher), "%s", mbedtls_ssl_get_ciphersuite(&c->ssl));
    const mbedtls_x509_crt *peer = mbedtls_ssl_get_peer_cert(&c->ssl);
    if (peer) {
        copy_cn(c->peer_subject, sizeof(c->peer_subject), &peer->subject);
        copy_cn(c->peer_issuer, sizeof(c->peer_issuer), &peer->issuer);
    }

    // Zapamiętaj sesję do wznowienia przy następnym połączeniu.
    mbedtls_ssl_session_free(&c->session);
    mbedtls_ssl_session_init(&c->session);
    c->have_session = (mbedtls_ssl_get_session(&c->ssl, &c->session) == 0);
    return 0;
}

// ---------------------------------------------------------------------------
// Odczyt odpowiedzi HTTP/1.1 (Content-Length, chunked albo do zamknięcia)
// ---------------------------------------------------------------------------

typedef struct {
    char  *d;
    size_t len, cap, pos;
    int    eof;
} rbuf;

static int rb_grow(rbuf *b, size_t need) {
    if (b->cap - b->len >= need) return 0;
    size_t ncap = b->cap ? b->cap : 8192;
    while (ncap - b->len < need) ncap *= 2;
    if (ncap > MAX_RESPONSE + 8192) return -1;
    char *n = realloc(b->d, ncap + 1);
    if (!n) return -1;
    b->d = n;
    b->cap = ncap;
    return 0;
}

// Doczytuje dane z TLS. Zwraca >0 liczba bajtów, 0 = koniec strumienia, <0 = błąd.
static int rb_read_more(https_client *c, rbuf *b) {
    if (b->eof) return 0;
    if (rb_grow(b, 4096) != 0) return -1;
    for (;;) {
        int ret = mbedtls_ssl_read(&c->ssl, (unsigned char *)b->d + b->len, b->cap - b->len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) { yield(); continue; }
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0 ||
            ret == MBEDTLS_ERR_NET_CONN_RESET) { b->eof = 1; return 0; }
        if (ret < 0) return ret;
        b->len += ret;
        b->d[b->len] = '\0';
        return ret;
    }
}

// Upewnia się, że od pozycji pos jest co najmniej n bajtów.
static int rb_need(https_client *c, rbuf *b, size_t n) {
    while (b->len - b->pos < n) {
        int r = rb_read_more(c, b);
        if (r <= 0) return r == 0 ? -2 : r;
    }
    return 0;
}

// Szuka "\r\n" od pozycji pos; zwraca indeks albo <0.
static long rb_line(https_client *c, rbuf *b) {
    for (;;) {
        if (b->len > b->pos) {
            char *p = strstr(b->d + b->pos, "\r\n");
            if (p) return p - b->d;
        }
        int r = rb_read_more(c, b);
        if (r <= 0) return r == 0 ? -2 : r;
    }
}

static const char *find_header(const char *headers, const char *name, size_t *vlen) {
    size_t nl = strlen(name);
    const char *p = headers;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            const char *v = p + nl + 1;
            while (*v == ' ' || *v == '\t') v++;
            *vlen = strcspn(v, "\r\n");
            return v;
        }
    }
    return NULL;
}

// Zwraca 0 = OK, -2 = połączenie zamknięte zanim przyszła odpowiedź, <0 = inny błąd.
static int read_response(https_client *c, https_response *res, int *keep_alive) {
    rbuf b = {0};
    int ret;
    *keep_alive = 1;

    // Nagłówki
    char *hdr_end;
    for (;;) {
        if (b.len && (hdr_end = strstr(b.d, "\r\n\r\n")) != NULL) break;
        ret = rb_read_more(c, &b);
        if (ret == 0) { free(b.d); return b.len ? -1 : -2; }
        if (ret < 0) { free(b.d); return b.len ? ret : -2; }
    }
    size_t hdr_len = hdr_end - b.d + 2;  // z końcowym \r\n ostatniego nagłówka
    b.pos = hdr_len + 2;

    char *headers = malloc(hdr_len + 1);
    if (!headers) { free(b.d); return -1; }
    memcpy(headers, b.d, hdr_len);
    headers[hdr_len] = '\0';

    if (sscanf(headers, "HTTP/%*s %d", &res->status) != 1) {
        snprintf(res->error, sizeof(res->error), "niepoprawna odpowiedź HTTP");
        free(headers); free(b.d);
        return -1;
    }

    size_t vlen;
    const char *v;
    if ((v = find_header(headers, "Connection", &vlen)) && vlen >= 5 && strncasecmp(v, "close", 5) == 0)
        *keep_alive = 0;
    if (strncmp(headers, "HTTP/1.0", 8) == 0) *keep_alive = 0;

    long content_length = -1;
    int chunked = 0;
    if ((v = find_header(headers, "Content-Length", &vlen))) content_length = strtol(v, NULL, 10);
    if ((v = find_header(headers, "Transfer-Encoding", &vlen)) && vlen >= 7 && strncasecmp(v, "chunked", 7) == 0)
        chunked = 1;
    free(headers);

    char *body = NULL;
    size_t body_len = 0;

    if (chunked) {
        body = malloc(1);
        if (!body) { free(b.d); return -1; }
        for (;;) {
            long eol = rb_line(c, &b);
            if (eol < 0) { free(body); free(b.d); return -1; }
            unsigned long sz = strtoul(b.d + b.pos, NULL, 16);
            b.pos = eol + 2;
            if (sz == 0) {
                // Pomijamy ewentualne trailery do pustej linii.
                for (;;) {
                    long e = rb_line(c, &b);
                    if (e < 0) break;
                    int empty = (size_t)e == b.pos;
                    b.pos = e + 2;
                    if (empty) break;
                }
                break;
            }
            if (body_len + sz > MAX_RESPONSE) { free(body); free(b.d); return -1; }
            if (rb_need(c, &b, sz + 2) != 0) { free(body); free(b.d); return -1; }
            char *nb = realloc(body, body_len + sz + 1);
            if (!nb) { free(body); free(b.d); return -1; }
            body = nb;
            memcpy(body + body_len, b.d + b.pos, sz);
            body_len += sz;
            b.pos += sz + 2;
        }
    } else if (content_length >= 0) {
        if (content_length > MAX_RESPONSE) { free(b.d); return -1; }
        if (rb_need(c, &b, (size_t)content_length) != 0) { free(b.d); return -1; }
        body = malloc(content_length + 1);
        if (!body) { free(b.d); return -1; }
        memcpy(body, b.d + b.pos, content_length);
        body_len = content_length;
    } else {
        // Brak długości: czytamy do zamknięcia połączenia.
        while ((ret = rb_read_more(c, &b)) > 0) {}
        body_len = b.len - b.pos;
        body = malloc(body_len + 1);
        if (!body) { free(b.d); return -1; }
        memcpy(body, b.d + b.pos, body_len);
        *keep_alive = 0;
    }

    body[body_len] = '\0';
    res->body = body;
    res->body_len = body_len;
    free(b.d);
    return 0;
}

// ---------------------------------------------------------------------------
// Żądanie
// ---------------------------------------------------------------------------

static int write_all(https_client *c, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int ret = mbedtls_ssl_write(&c->ssl, (const unsigned char *)data + sent, len - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) { yield(); continue; }
        if (ret < 0) return ret;
        sent += ret;
    }
    return 0;
}

int https_request(https_client *c, const char *method, const char *path,
                  const char *extra_headers, const char *body, https_response *res) {
    memset(res, 0, sizeof(*res));
    if (!g_ready || !c->conf_ready) {
        snprintf(res->error, sizeof(res->error), "TLS nie został zainicjalizowany");
        return -1;
    }

    size_t body_len = body ? strlen(body) : 0;
    char head[1024];
    int head_len = snprintf(head, sizeof(head),
                            "%s %s HTTP/1.1\r\n"
                            "Host: %s\r\n"
                            "User-Agent: psp-homelab/0.2\r\n"
                            "Accept: application/json\r\n"
                            "Connection: keep-alive\r\n"
                            "%s"
                            "Content-Length: %u\r\n\r\n",
                            method, path, c->host,
                            extra_headers ? extra_headers : "",
                            (unsigned)body_len);
    if (head_len < 0 || head_len >= (int)sizeof(head)) {
        snprintf(res->error, sizeof(res->error), "za długie nagłówki");
        return -1;
    }

    // Druga próba tylko wtedy, gdy stare połączenie keep-alive okazało się martwe.
    for (int attempt = 0; attempt < 2; attempt++) {
        int reused = c->connected;
        if (!c->connected) {
            if (client_connect(c, res->error, sizeof(res->error)) != 0) {
                https_client_close(c);
                return -1;
            }
        }

        int ret = write_all(c, head, head_len);
        if (ret == 0 && body_len) ret = write_all(c, body, body_len);
        if (ret != 0) {
            https_client_close(c);
            if (reused) continue;
            fmt_err(res->error, sizeof(res->error), "wysyłanie", ret);
            return -1;
        }

        int keep_alive = 1;
        ret = read_response(c, res, &keep_alive);
        if (ret == -2 && reused) {  // serwer zamknął bezczynne połączenie
            https_client_close(c);
            continue;
        }
        if (ret != 0) {
            if (!res->error[0]) snprintf(res->error, sizeof(res->error), "błąd odbioru odpowiedzi");
            https_client_close(c);
            return -1;
        }
        if (!keep_alive) https_client_close(c);
        return 0;
    }
    snprintf(res->error, sizeof(res->error), "połączenie zerwane");
    return -1;
}

void https_response_free(https_response *res) {
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

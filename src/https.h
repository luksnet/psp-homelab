// Klient HTTPS (TLS 1.2, mbedTLS 2.28) dla PSP.
// Utrzymuje połączenie (keep-alive), a po zerwaniu wznawia sesję TLS,
// żeby nie powtarzać pełnego, kilkusekundowego handshake'u.
#pragma once
#include <stddef.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>

typedef struct {
    char host[128];
    char port[8];

    // Stan połączenia (nie ruszaj z zewnątrz)
    mbedtls_net_context sock;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;
    mbedtls_ssl_session session;
    int conf_ready;
    int connected;
    int have_session;

    // Informacje o ostatnim połączeniu, do wyświetlenia
    char tls_version[16];
    char cipher[64];
    char peer_subject[128];
    char peer_issuer[128];
    unsigned int handshake_ms;
    int resumed;               // 1 = sesja wznowiona (szybki handshake)
    unsigned int handshakes;   // licznik wszystkich handshake'ów
} https_client;

typedef struct {
    int    status;     // kod HTTP, np. 200
    char  *body;       // treść zakończona \0, zwolnij https_response_free()
    size_t body_len;
    char   error[128]; // opis błędu, gdy https_request() zwróci <0
} https_response;

// Jednorazowo: generator losowości i zaufane CA.
// ca_pem_path = ścieżka do pliku PEM z własnym CA albo NULL (wbudowane publiczne CA).
int  https_global_init(const char *ca_pem_path, char *err, size_t errlen);
void https_global_free(void);
const char *https_ca_source(void);  // opis, skąd wczytano CA

int  https_client_init(https_client *c, const char *host, int port);
void https_client_close(https_client *c);  // zamyka połączenie, zachowuje sesję
void https_client_free(https_client *c);

// Wysyła żądanie. extra_headers: dodatkowe nagłówki zakończone \r\n albo NULL.
// body: treść (np. JSON) albo NULL.
int  https_request(https_client *c, const char *method, const char *path,
                   const char *extra_headers, const char *body, https_response *res);
void https_response_free(https_response *res);

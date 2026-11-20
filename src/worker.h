// Wątek roboczy: Wi-Fi, TLS i cykliczne pobieranie stanów z Home Assistant.
// GUI czyta stan przez worker_snapshot(), więc nigdy nie czeka na sieć.
#pragma once
#include "config.h"
#include "ha.h"

typedef enum { STEP_PENDING, STEP_ACTIVE, STEP_OK, STEP_WARN, STEP_ERROR } step_status;
enum { STEP_CONFIG, STEP_WIFI, STEP_RNG, STEP_TLS, STEP_HA, STEP_COUNT };

typedef struct {
    unsigned int version;      // rośnie przy każdej zmianie

    int running;               // 1 = pierwsze odświeżenie za nami, pokazujemy kafelki
    step_status steps[STEP_COUNT];
    char step_detail[STEP_COUNT][64];
    char error[128];           // błąd krytyczny (start się nie udał)
    char hint[256];            // podpowiedź do błędu, linie rozdzielone \n

    char host[128];
    int  refresh_s;
    int  autoexit_s;           // tylko do testów (config: autoexit)
    int  entity_count;
    ha_entity ents[CFG_MAX_ENTITIES];

    char tls_version[16];
    char cipher[64];
    unsigned int handshake_ms;
    int  resumed;
    unsigned int handshakes;
    char rng[64];
    int  rng_hw;
    char ip[32];

    night_config night;        // tryb nocny z config.ini
    int  backlight;            // 1 = działa backlight.prx (inaczej przyciemniamy obraz)
    int  static_count;         // ile encji z linii entity (reszta z entities_from)
    char list_error[96];       // błąd ostatniego pobrania listy encji z HA

    chart_data chart;          // wykres historii zaznaczonej encji
    char test_chart[64];       // tylko do testów (config: test_chart)

    int  refreshing;
    char last_refresh[16];     // HH:MM:SS
    unsigned int last_ms;      // czas ostatniego odświeżenia
    char refresh_error[128];   // błąd ostatniego odświeżenia (np. brak sieci)
} app_state;

int  worker_start(const char *argv0);
void worker_snapshot(app_state *out);
unsigned int worker_version(void);
void worker_request_refresh(void);
// Wczytuje historię encji z ostatnich `hours` godzin do state.chart.
void worker_request_chart(const char *entity_id, int hours);
void worker_close_chart(void);
void worker_stop(void);

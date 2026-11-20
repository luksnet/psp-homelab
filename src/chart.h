// Dane wykresu historii: próbki z Home Assistant uśrednione do stałej liczby kubełków.
// Czysty C bez zależności od PSP (testowane na komputerze).
#pragma once
#include "timeutil.h"

#define CHART_BUCKETS 200

typedef enum { CHART_NONE, CHART_LOADING, CHART_OK, CHART_ERROR } chart_status;

typedef struct {
    chart_status status;
    char entity[64];
    int hours;                     // zakres: ostatnie N godzin
    int binary;                    // 1 = stan wł./wył. (0/1), 0 = liczby
    int samples;                   // ile próbek przyszło z HA
    float v[CHART_BUCKETS];        // wartość w kubełku
    unsigned char has[CHART_BUCKETS];  // 1 = kubełek ma wartość
    float min, max, last;
    char error[64];
} chart_data;

// Akumulator używany podczas wczytywania (nie trafia do GUI).
typedef struct {
    tu_time start, end;
    float sum[CHART_BUCKETS];
    float maxv[CHART_BUCKETS];     // największa wartość w kubełku (dla wł./wył.)
    float lastv[CHART_BUCKETS];    // ostatnia wartość w kubełku (przechodzi dalej)
    unsigned short cnt[CHART_BUCKETS];
    float before;                  // ostatnia wartość sprzed początku zakresu
    int have_before;
    int numeric, binary;           // ile próbek liczbowych / wł.-wył.
} chart_acc;

void chart_begin(chart_acc *a, tu_time start, tu_time end);

// Dodaje próbkę: stan tekstowy z HA ("21.5", "on", "unavailable"...).
// Zwraca 1, gdy próbkę uwzględniono.
int chart_add(chart_acc *a, tu_time t, const char *state);

// Wypełnia d->v/has/min/max/last. Liczby: średnia z kubełka. Wł./wył.: 1, jeśli
// w kubełku choć chwilę było włączone. Kubełek bez próbek dostaje ostatnią znaną
// wartość (stan w HA trwa do następnej zmiany). Zwraca liczbę kubełków z wartością.
// Próbki muszą przychodzić w kolejności czasu (tak zwraca je HA).
int chart_finish(const chart_acc *a, chart_data *d);

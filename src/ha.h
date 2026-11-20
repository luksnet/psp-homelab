// Odczyt stanów encji z REST API Home Assistant.
#pragma once
#include "https.h"

typedef struct {
    char id[64];        // np. light.salon
    char name[64];      // friendly_name albo id
    char state[32];     // np. on, off, 21.5
    char unit[16];      // np. °C
    int  ok;            // 1 = odczytano poprawnie
    char error[48];     // krótki opis, gdy ok == 0
} ha_entity;

// GET /api/states/<id>. Zwraca 0, -1 = błąd połączenia (przerwij odświeżanie),
// -2 = błąd tej jednej encji (np. 404).
int ha_fetch_state(https_client *c, const char *token, ha_entity *e);

#include "chart.h"

#define HA_MAX_LIST 48

// Lista encji ze źródła "label:psp" (POST /api/template z label_entities)
// albo "group.psp" (atrybut entity_id grupy). Wynik posortowany.
// Zwraca liczbę encji (>=0) albo <0 z opisem w err.
int ha_fetch_entity_list(https_client *c, const char *token, const char *source,
                         char ids[][64], int max, char *err, int errlen);

// Historia stanów encji z ostatnich `hours` godzin (GET /api/history/period),
// uśredniona do kubełków wykresu. Zwraca 0 albo <0 z opisem w d->error.
int ha_fetch_history(https_client *c, const char *token, const char *entity_id,
                     int hours, tu_time now, chart_data *d);

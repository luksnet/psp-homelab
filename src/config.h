#pragma once
#include "night.h"

#define CFG_MAX_ENTITIES 24
#define CFG_MAX_SOURCES 8

typedef struct {
    char host[128];        // np. ha.example.dev
    int  port;             // domyślnie 443
    char token[512];       // długoterminowy token Home Assistant
    char ca_path[256];     // pełna ścieżka do ca.pem albo "" = wbudowane publiczne CA
    int  refresh_s;        // co ile sekund odświeżać panel
    int  wifi_profile;     // numer profilu sieci na PSP (1..)
    int  autoexit_s;       // tylko do testów: zakończ po N sekundach (0 = nigdy)
    char test_chart[64];   // tylko do testów: po starcie otwórz wykres tej encji
    int  entity_count;
    char entities[CFG_MAX_ENTITIES][64];
    // Skąd dociągać listę encji z HA: "label:psp" albo "group.psp" (klucz entities_from).
    int  source_count;
    char sources[CFG_MAX_SOURCES][64];
    night_config night;    // klucze night, night_brightness, night_wake
    char path[256];        // skąd wczytano plik (do wyświetlenia)
    char dir[256];         // katalog z config.ini (i kirkrng.prx), z ukośnikiem na końcu
} app_config;

// Szuka config.ini obok EBOOT.PBP (katalog z argv0), potem w bieżącym katalogu
// i w ms0:/PSP/GAME/psp-homelab/. Zwraca 0 albo <0 z opisem w err.
int config_load(const char *argv0, app_config *cfg, char *err, int errlen);

// Czas UTC jako sekundy od 1970 (bez zależności od PSP, testowane na komputerze).
#pragma once

typedef long long tu_time;

// Sekundy od 1970-01-01 00:00:00 UTC dla daty w UTC.
tu_time tu_from_civil(int y, int mon, int d, int h, int min, int s);

// "2026-11-20T12:00:00+00:00" (UTC). out musi mieć co najmniej 26 bajtów.
void tu_format_iso(tu_time t, char *out, int len);

// Parsuje znacznik czasu ISO 8601 z Home Assistant, np.
// "2026-11-20T12:00:00.123456+00:00", "...Z" albo "...+01:00". Zwraca 0 albo -1.
int tu_parse_iso(const char *s, tu_time *out);

// Bieżący czas UTC (na PSP z zegara RTC, na komputerze z time()).
tu_time tu_now(void);

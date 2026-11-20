// Tryb nocny: przyciemnianie ekranu w zadanych godzinach.
// Najpierw próbuje sterować podświetleniem (moduł backlight.prx); jeśli się nie da,
// GUI przyciemnia sam obraz (night_overlay()).
#pragma once

typedef struct {
    int enabled;        // 1 = skonfigurowano godziny
    int from_min;       // początek nocy, minuty od północy (np. 22:00 = 1320)
    int to_min;         // koniec nocy
    int brightness;     // jasność w nocy 0..100
    int wake_s;         // ile sekund po naciśnięciu przycisku świecić normalnie
} night_config;

// Parsuje "22:00-07:00". Zwraca 0 albo -1.
int night_parse(const char *s, int *from_min, int *to_min);

// Czy minuta doby (0..1439) wypada w nocy? Obsługuje przedziały przez północ.
int night_in_window(int from_min, int to_min, int minute_of_day);

// Wczytuje backlight.prx; zwraca 1, gdy sterowanie podświetleniem działa.
int night_init_backlight(const char *dir);
int night_backlight_available(void);

// Wywoływane co klatkę z wątku GUI.
// any_press: czy w tej klatce naciśnięto przycisk. Zwraca 1, jeśli naciśnięcie
// tylko wybudziło ekran (GUI powinno je wtedy zignorować).
int night_update(const night_config *cfg, int minute_of_day, unsigned int now_ms, int any_press);

int night_active(void);   // 1 = teraz jest przyciemnione
int night_overlay(void);  // 0..100: o ile przyciemnić obraz, gdy brak sterowania podświetleniem
void night_restore(void); // przywraca jasność dzienną (przy wyjściu)

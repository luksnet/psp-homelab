// Rysowanie interfejsu (SDL2 + SDL2_ttf), ekran PSP 480x272.
#pragma once
#include <SDL2/SDL.h>
#include "worker.h"

#define UI_W 480
#define UI_H 272
#define UI_TILES_PER_PAGE 6

int  ui_init(SDL_Renderer *r);
void ui_free(void);

// Zakresy wykresu (godziny), przełączane L/R.
#define UI_CHART_RANGES 4
extern const int ui_chart_hours[UI_CHART_RANGES];

typedef struct {
    int selected;           // indeks zaznaczonej encji
    unsigned int frame;     // licznik klatek do animacji
    int chart_open;         // 1 = pokazuj wykres zaznaczonej encji
    int chart_range;        // indeks w ui_chart_hours
    int dim;                // 0..100: przyciemnienie obrazu (tryb nocny bez backlight.prx)
    int night;              // 1 = trwa noc (ekran wybudzony przyciskiem)
} ui_view;

void ui_draw(const app_state *s, const ui_view *v);

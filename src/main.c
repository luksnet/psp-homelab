// psp-homelab: panel stanu Home Assistant na PSP (GUI na SDL2).
// Konfiguracja w config.ini obok EBOOT.PBP (patrz config.example.ini / config.example.pl.ini).
//
// SDL2main definiuje właściwe main(), informacje o module i obsługę HOME,
// dlatego tutaj nie ma PSP_MODULE_INFO ani własnych callbacków.

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <psprtc.h>

#include "ui.h"
#include "worker.h"
#include "night.h"

PSP_HEAP_SIZE_KB(-1024);  // cała wolna pamięć oprócz 1 MB na stos i system

// Sekunda doby czasu lokalnego (do zegara i trybu nocnego).
static int local_second_of_day(void) {
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) != 0) return 0;
    return t.hour * 3600 + t.minute * 60 + t.second;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("psp-homelab", SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED, UI_W, UI_H, 0);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (!ren || ui_init(ren) != 0) {
        printf("GUI: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);

    if (worker_start(argc > 0 ? argv[0] : NULL) != 0) {
        printf("Nie udalo sie uruchomic watku roboczego\n");
        ui_free();
        SDL_Quit();
        return 1;
    }

    static app_state s;
    unsigned int seen_version = (unsigned int)-1;
    unsigned int prev_buttons = 0;
    ui_view view = {0};
    view.chart_range = 2;  // 24 h
    int last_second = -1;
    int last_dim = -1;
    int quit = 0;

    Uint32 started = SDL_GetTicks();

    while (!quit) {
        // Tryb testowy: emulator robi zrzut ekranu przy zakończeniu programu.
        if (s.autoexit_s > 0 && SDL_GetTicks() - started > (Uint32)s.autoexit_s * 1000) quit = 1;

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT) quit = 1;  // HOME > Wyjdź

        int dirty = 0;

        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~prev_buttons;
        prev_buttons = pad.Buttons;

        unsigned int v = worker_version();
        if (v != seen_version) {
            worker_snapshot(&s);
            seen_version = v;
            dirty = 1;
        }

        // Tryb nocny: pierwsze naciśnięcie przycisku tylko budzi ekran.
        int sod = local_second_of_day();
        if (night_update(&s.night, sod / 60, SDL_GetTicks(), pressed != 0)) pressed = 0;
        view.night = s.night.enabled && night_in_window(s.night.from_min, s.night.to_min, sod / 60);
        view.dim = night_overlay();
        if (view.dim != last_dim) { last_dim = view.dim; dirty = 1; }

        if (pressed & PSP_CTRL_START) quit = 1;
        if (pressed & PSP_CTRL_CROSS) worker_request_refresh();

        // Tryb testowy: otwórz wykres wskazanej encji (emulator bez klawiatury).
        static int test_chart_done;
        if (s.running && s.test_chart[0] && !test_chart_done) {
            test_chart_done = 1;
            for (int i = 0; i < s.entity_count; i++)
                if (!strcmp(s.ents[i].id, s.test_chart)) { view.selected = i; pressed |= PSP_CTRL_TRIANGLE; }
        }

        if (s.running && s.entity_count > 0) {
            int old = view.selected, old_range = view.chart_range, old_open = view.chart_open;
            if (view.selected >= s.entity_count) view.selected = s.entity_count - 1;

            if (view.chart_open) {
                // Wykres: L/R zmienia zakres, lewo/prawo przechodzi do sąsiedniej encji.
                if (pressed & (PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE)) view.chart_open = 0;
                if (pressed & PSP_CTRL_RTRIGGER && view.chart_range < UI_CHART_RANGES - 1) view.chart_range++;
                if (pressed & PSP_CTRL_LTRIGGER && view.chart_range > 0) view.chart_range--;
                if (pressed & PSP_CTRL_RIGHT && view.selected < s.entity_count - 1) view.selected++;
                if (pressed & PSP_CTRL_LEFT && view.selected > 0) view.selected--;
            } else {
                // Kafelki: D-pad w siatce 2 kolumn, L/R całymi stronami.
                if (pressed & PSP_CTRL_RIGHT) view.selected += 1;
                if (pressed & PSP_CTRL_LEFT)  view.selected -= 1;
                if (pressed & PSP_CTRL_DOWN)  view.selected += 2;
                if (pressed & PSP_CTRL_UP)    view.selected -= 2;
                if (pressed & PSP_CTRL_RTRIGGER) view.selected += UI_TILES_PER_PAGE;
                if (pressed & PSP_CTRL_LTRIGGER) view.selected -= UI_TILES_PER_PAGE;
                if (view.selected < 0) view.selected = 0;
                if (view.selected >= s.entity_count) view.selected = s.entity_count - 1;
                if (pressed & PSP_CTRL_TRIANGLE) view.chart_open = 1;
            }

            if (view.chart_open && (!old_open || view.selected != old || view.chart_range != old_range))
                worker_request_chart(s.ents[view.selected].id, ui_chart_hours[view.chart_range]);
            if (!view.chart_open && old_open) worker_close_chart();
            if (view.selected != old || view.chart_open != old_open || view.chart_range != old_range)
                dirty = 1;
        }

        if (sod != last_second) { last_second = sod; dirty = 1; }
        // Animacje (migający krok startu, "Odświeżanie...") kilka razy na sekundę.
        int animating = s.refreshing || (!s.running && !s.error[0]) ||
                        (view.chart_open && s.chart.status == CHART_LOADING);
        if (animating && view.frame % 5 == 0) dirty = 1;

        if (dirty) ui_draw(&s, &view);
        view.frame++;
        SDL_Delay(16);
    }

    night_restore();
    worker_stop();
    ui_free();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

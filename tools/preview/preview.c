// Podgląd GUI psp-homelab na komputerze: rysuje src/ui.c z przykładowymi danymi
// do plików BMP (480x272), bez emulatora. Uruchamiaj przez tools/preview/build.sh.

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include "ui.h"
#include "i18n.h"
#include <math.h>

// Przykładowe dane w języku zrzutu.
static const char *L(const char *pl, const char *en) { return i18n_get() == LANG_EN ? en : pl; }

static void draw(const app_state *s, int selected, unsigned frame) {
    ui_view v = {0};
    v.selected = selected;
    v.frame = frame;
    v.chart_range = 2;
    ui_draw(s, &v);
}

// Sztuczna historia do zrzutów wykresu.
static void fake_chart(app_state *s, const char *id, int hours, int binary) {
    static chart_acc a;
    tu_time end = 1795176000LL, start = end - hours * 3600;
    chart_begin(&a, start, end);
    char v[16];
    if (binary) {
        chart_add(&a, start - 1, "off");
        for (tu_time t = start + 2200; t < end; t += 7500) {
            chart_add(&a, t, "on");
            chart_add(&a, t + 600, "off");
        }
    } else {
        for (tu_time t = start; t < end; t += 300) {
            double h = (double)((t / 3600) % 24) + (t % 3600) / 3600.0;
            snprintf(v, sizeof(v), "%.1f", 21 + 1.5 * sin((h - 9) / 24 * 2 * M_PI) + 0.12 * sin(t * 0.37));
            chart_add(&a, t, v);
        }
    }
    memset(&s->chart, 0, sizeof(s->chart));
    chart_finish(&a, &s->chart);
    s->chart.status = CHART_OK;
    s->chart.hours = hours;
    snprintf(s->chart.entity, sizeof(s->chart.entity), "%s", id);
}

static void draw_chart_view(const app_state *s, int selected, int range, int dim) {
    ui_view v = {0};
    v.selected = selected;
    v.chart_open = 1;
    v.chart_range = range;
    v.dim = dim;
    ui_draw(s, &v);
}

static void ent(app_state *s, const char *id, const char *name, const char *state,
                const char *unit, int ok, const char *err) {
    ha_entity *e = &s->ents[s->entity_count++];
    snprintf(e->id, sizeof(e->id), "%s", id);
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->state, sizeof(e->state), "%s", state);
    snprintf(e->unit, sizeof(e->unit), "%s", unit);
    snprintf(e->error, sizeof(e->error), "%s", err ? err : "");
    e->ok = ok;
}

static void base_panel(app_state *s) {
    memset(s, 0, sizeof(*s));
    s->running = 1;
    snprintf(s->host, sizeof(s->host), "ha.example.dev");
    s->refresh_s = 30;
    ent(s, "light.salon", L("Światło w salonie", "Living room light"), "on", "", 1, NULL);
    ent(s, "light.sypialnia", L("Sypialnia", "Bedroom"), "off", "", 1, NULL);
    ent(s, "sensor.temperatura_salon", L("Temperatura w salonie", "Living room temperature"), "21.4", "°C", 1, NULL);
    ent(s, "binary_sensor.drzwi_wejsciowe", L("Drzwi wejściowe", "Front door"), "closed", "", 1, NULL);
    ent(s, "sensor.proxmox_cpu", "Proxmox CPU", "12", "%", 1, NULL);
    ent(s, "switch.lab_ad", "Lab Active Directory", "unavailable", "", 1, NULL);
    ent(s, "sensor.nie_istnieje", "sensor.nie_istnieje", "", "", 0, tr("brak encji (404)"));
    snprintf(s->tls_version, sizeof(s->tls_version), "TLSv1.2");
    s->resumed = 1;
    s->handshake_ms = 18;
    s->rng_hw = 1;
    snprintf(s->rng, sizeof(s->rng), "%s", tr("KIRK (sprzętowy)"));
    snprintf(s->last_refresh, sizeof(s->last_refresh), "14:32:01");
    s->last_ms = 420;
}

static int save(SDL_Renderer *r, SDL_Surface *surf, const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    (void)r;
    if (SDL_SaveBMP(surf, path) != 0) {
        fprintf(stderr, "%s: %s\n", path, SDL_GetError());
        return 1;
    }
    printf("zapisano %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    i18n_set(argc > 2 && !strcmp(argv[2], "pl") ? LANG_PL : LANG_EN);
    if (SDL_Init(0) != 0) return 1;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, UI_W, UI_H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surf);
    if (!surf || !r || ui_init(r) != 0) {
        fprintf(stderr, "init: %s\n", SDL_GetError());
        return 1;
    }

    static app_state s;
    int rc = 0;

    // 1. Start w toku
    memset(&s, 0, sizeof(s));
    s.steps[STEP_CONFIG] = STEP_OK;   snprintf(s.step_detail[0], 64, tr("%d encji, odświeżanie co %d s"), 7, 30);
    s.steps[STEP_WIFI] = STEP_OK;     snprintf(s.step_detail[1], 64, "IP 192.168.1.42");
    s.steps[STEP_RNG] = STEP_OK;      snprintf(s.step_detail[2], 64, "%s", tr("KIRK (sprzętowy)"));
    s.steps[STEP_TLS] = STEP_ACTIVE;  snprintf(s.step_detail[3], 64, "%s", tr("wczytywanie CA"));
    draw(&s, 0, 0);
    rc |= save(r, surf, out, "1-start");

    // 2. Błąd startu
    s.steps[STEP_TLS] = STEP_OK;      snprintf(s.step_detail[3], 64, "CA: ms0:/PSP/GAME/psp-homelab/ca.pem");
    s.steps[STEP_HA] = STEP_ERROR;    snprintf(s.step_detail[4], 64, tr("certyfikat odrzucony: %s"), L("nazwa się nie zgadza", "CN mismatch"));
    s.steps[STEP_RNG] = STEP_WARN;    snprintf(s.step_detail[2], 64, tr("SŁABY: brak kirkrng.prx (0x%08X)"), 0x80010002u);
    snprintf(s.error, sizeof(s.error), "%s", tr("Nie można połączyć z Home Assistant"));
    snprintf(s.hint, sizeof(s.hint), "%s", tr("Sprawdź host i port w config.ini oraz to,\n"
                                                 "czy certyfikat pasuje do nazwy hosta."));
    draw(&s, 0, 0);
    rc |= save(r, surf, out, "2-blad");

    // 3. Panel
    base_panel(&s);
    draw(&s, 0, 0);
    rc |= save(r, surf, out, "3-panel");

    // 4. Panel: zaznaczony kafelek, odświeżanie, słaby RNG
    base_panel(&s);
    s.refreshing = 1;
    s.rng_hw = 0;
    s.resumed = 0;
    s.handshake_ms = 3412;
    draw(&s, 3, 20);
    rc |= save(r, surf, out, "4-odswiezanie");

    // 5. Druga strona
    base_panel(&s);
    snprintf(s.refresh_error, sizeof(s.refresh_error), "%s: -0x0052 NET - Failed to connect", tr("połączenie TCP"));
    draw(&s, 6, 0);
    rc |= save(r, surf, out, "5-strona2");

    // 6. Wykres temperatury (24 h)
    base_panel(&s);
    fake_chart(&s, "sensor.temperatura_salon", 24, 0);
    draw_chart_view(&s, 2, 2, 0);
    rc |= save(r, surf, out, "6-wykres");

    // 7. Wykres drzwi (wł./wył., 24 h)
    base_panel(&s);
    snprintf(s.ents[3].state, sizeof(s.ents[3].state), "off");
    fake_chart(&s, "binary_sensor.drzwi_wejsciowe", 24, 1);
    draw_chart_view(&s, 3, 2, 0);
    rc |= save(r, surf, out, "7-wykres-drzwi");

    // 8. Wykres w trakcie wczytywania
    base_panel(&s);
    memset(&s.chart, 0, sizeof(s.chart));
    s.chart.status = CHART_LOADING;
    s.chart.hours = 72;
    draw_chart_view(&s, 4, 3, 0);
    rc |= save(r, surf, out, "8-wykres-wczytywanie");

    // 9. Noc bez backlight.prx: przyciemniony obraz, błąd listy encji
    base_panel(&s);
    snprintf(s.list_error, sizeof(s.list_error), tr("%s: 401 (szablony wymagają admina, użyj group.)"), "label:psp");
    {
        ui_view v = {0};
        v.dim = 80;
        ui_draw(&s, &v);
    }
    rc |= save(r, surf, out, "9-noc");

    // 10. Jak 9, bez przyciemnienia (żeby było widać pasek błędu listy)
    {
        ui_view v = {0};
        ui_draw(&s, &v);
    }
    rc |= save(r, surf, out, "10-lista-blad");

    ui_free();
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surf);
    SDL_Quit();
    return rc;
}

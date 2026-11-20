#include "worker.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <pspkernel.h>
#include <psprtc.h>

#include "net.h"
#include "https.h"
#include "rng.h"
#include "night.h"
#include "timeutil.h"

static SDL_mutex *lock;
static SDL_Thread *thread;
static app_state state;           // chronione przez lock
static volatile int quit_flag;
static volatile int refresh_flag;
static char argv0_copy[256];

// Żądanie wykresu z wątku GUI (chronione przez lock).
static int chart_req_gen, chart_done_gen;
static char chart_req_entity[64];
static int chart_req_hours;

#define LIST_REFRESH_S 600   // co ile odświeżać listę encji z etykiety/grupy

static app_config cfg;
static https_client client;

// Wykonuje kod pod blokadą i zgłasza GUI zmianę stanu.
#define LOCKED(...) do { SDL_LockMutex(lock); __VA_ARGS__; state.version++; SDL_UnlockMutex(lock); } while (0)

static void set_step(int step, step_status st, const char *fmt, ...) {
    char buf[64] = "";
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
    }
    LOCKED(state.steps[step] = st; snprintf(state.step_detail[step], sizeof(state.step_detail[0]), "%s", buf));
    printf("[start] krok %d: %d %s\n", step, st, buf);
}

static void fail(const char *error, const char *hint) {
    LOCKED(snprintf(state.error, sizeof(state.error), "%s", error);
           snprintf(state.hint, sizeof(state.hint), "%s", hint ? hint : ""));
    printf("[start] BLAD: %s\n", error);
}

static void clock_str(char *out, size_t len) {
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) == 0)
        snprintf(out, len, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    else
        snprintf(out, len, "--:--:--");
}

static void copy_tls_info(void) {
    snprintf(state.tls_version, sizeof(state.tls_version), "%s", client.tls_version);
    snprintf(state.cipher, sizeof(state.cipher), "%s", client.cipher);
    state.handshake_ms = client.handshake_ms;
    state.resumed = client.resumed;
    state.handshakes = client.handshakes;
}

// Pobiera wszystkie encje jednym połączeniem; kafelki aktualizują się na bieżąco.
static int refresh_all(void) {
    SceInt64 t0 = sceKernelGetSystemTimeWide();
    char err[128] = "";
    LOCKED(state.refreshing = 1);
    SDL_LockMutex(lock);
    int count = state.entity_count;  // listę zmienia tylko ten wątek
    SDL_UnlockMutex(lock);

    for (int i = 0; i < count && !quit_flag; i++) {
        ha_entity e;
        SDL_LockMutex(lock);
        e = state.ents[i];
        SDL_UnlockMutex(lock);

        int rc = ha_fetch_state(&client, cfg.token, &e);

        LOCKED(
            if (e.ok || rc == -2) state.ents[i] = e;  // przy zerwaniu połączenia zostaw stary stan
            copy_tls_info()
        );
        if (rc == -1) {
            snprintf(err, sizeof(err), "%s", e.error);
            break;
        }
    }

    unsigned int ms = (unsigned int)((sceKernelGetSystemTimeWide() - t0) / 1000);
    LOCKED(
        state.refreshing = 0;
        state.last_ms = ms;
        clock_str(state.last_refresh, sizeof(state.last_refresh));
        snprintf(state.refresh_error, sizeof(state.refresh_error), "%s", err)
    );

    printf("[refresh] %s %s handshake=%u ms resumed=%d handshakes=%u czas=%u ms %s\n",
           client.tls_version, client.cipher, client.handshake_ms, client.resumed,
           client.handshakes, ms, err[0] ? err : "ok");
    for (int i = 0; i < count; i++) {
        const ha_entity *e = &state.ents[i];
        printf("  %-30s %s %s %s\n", e->id, e->ok ? e->state : "ERR", e->ok ? e->unit : "",
               e->ok ? "" : e->error);
    }
    fflush(stdout);
    return err[0] ? -1 : 0;
}

// Ustala listę encji: najpierw linie entity z config.ini, potem encje z etykiet
// i grup HA (bez powtórzeń). Istniejące kafelki zachowują ostatni stan.
// Zwraca liczbę encji albo -1, gdy żadnego źródła nie udało się odczytać.
static int resolve_entities(void) {
    static char ids[CFG_MAX_ENTITIES][64];
    static char found[HA_MAX_LIST][64];
    int n = 0;
    for (int i = 0; i < cfg.entity_count; i++) snprintf(ids[n++], 64, "%s", cfg.entities[i]);

    char err[96] = "";
    int any_ok = cfg.source_count == 0;
    for (int s = 0; s < cfg.source_count && !quit_flag; s++) {
        char e[96];
        int k = ha_fetch_entity_list(&client, cfg.token, cfg.sources[s], found, HA_MAX_LIST, e, sizeof(e));
        if (k < 0) {
            printf("[lista] BLAD %s\n", e);
            snprintf(err, sizeof(err), "%s", e);
            continue;
        }
        any_ok = 1;
        for (int j = 0; j < k && n < CFG_MAX_ENTITIES; j++) {
            int dup = 0;
            for (int m = 0; m < n && !dup; m++) dup = !strcmp(ids[m], found[j]);
            if (!dup) snprintf(ids[n++], 64, "%s", found[j]);
        }
        printf("[lista] %s: %d encji\n", cfg.sources[s], k);
    }
    if (!any_ok && cfg.entity_count == 0) {
        LOCKED(snprintf(state.list_error, sizeof(state.list_error), "%s", err));
        return -1;
    }

    LOCKED(
        static ha_entity old[CFG_MAX_ENTITIES];
        int old_n = state.entity_count;
        memcpy(old, state.ents, sizeof(old));
        for (int i = 0; i < n; i++) {
            int j = 0;
            while (j < old_n && strcmp(old[j].id, ids[i])) j++;
            if (j < old_n) state.ents[i] = old[j];
            else {
                memset(&state.ents[i], 0, sizeof(state.ents[i]));
                snprintf(state.ents[i].id, sizeof(state.ents[i].id), "%s", ids[i]);
                snprintf(state.ents[i].name, sizeof(state.ents[i].name), "%s", ids[i]);
                snprintf(state.ents[i].error, sizeof(state.ents[i].error), "...");
            }
        }
        state.entity_count = n;
        state.static_count = cfg.entity_count;
        snprintf(state.list_error, sizeof(state.list_error), "%s", err)
    );
    return n;
}

static void load_chart(void) {
    char entity[64];
    int hours, gen;
    SDL_LockMutex(lock);
    gen = chart_req_gen;
    hours = chart_req_hours;
    snprintf(entity, sizeof(entity), "%s", chart_req_entity);
    SDL_UnlockMutex(lock);

    LOCKED(memset(&state.chart, 0, sizeof(state.chart));
           state.chart.status = CHART_LOADING;
           state.chart.hours = hours;
           snprintf(state.chart.entity, sizeof(state.chart.entity), "%s", entity));

    static chart_data d;
    memset(&d, 0, sizeof(d));
    d.hours = hours;
    snprintf(d.entity, sizeof(d.entity), "%s", entity);
    SceInt64 t0 = sceKernelGetSystemTimeWide();
    int rc = ha_fetch_history(&client, cfg.token, entity, hours, tu_now(), &d);
    d.status = rc == 0 ? CHART_OK : CHART_ERROR;
    printf("[wykres] %s %d h: %d próbek, %s (%u ms)\n", entity, hours, d.samples,
           rc == 0 ? "ok" : d.error, (unsigned)((sceKernelGetSystemTimeWide() - t0) / 1000));

    LOCKED(
        if (chart_req_gen == gen && state.chart.status != CHART_NONE) state.chart = d;
        chart_done_gen = gen;
        copy_tls_info()
    );
}

static int worker_main(void *arg) {
    char err[128];

    // Wyższy priorytet niż wątek GUI (0x20): na PSP wątki o równym priorytecie
    // nie wywłaszczają się, więc sieć czekałaby na każdą klatkę interfejsu.
    sceKernelChangeThreadPriority(0, 0x18);

    // 1. Konfiguracja
    set_step(STEP_CONFIG, STEP_ACTIVE, NULL);
    if (config_load(argv0_copy, &cfg, err, sizeof(err)) != 0) {
        set_step(STEP_CONFIG, STEP_ERROR, "%s", err);
        fail("Błąd konfiguracji",
             "Skopiuj config.example.ini jako config.ini\n"
             "obok EBOOT.PBP i uzupełnij host, token i encje.");
        return 0;
    }
    if (cfg.source_count)
        set_step(STEP_CONFIG, STEP_OK, "%d encji + %s, co %d s", cfg.entity_count, cfg.sources[0], cfg.refresh_s);
    else
        set_step(STEP_CONFIG, STEP_OK, "%d encji, odświeżanie co %d s", cfg.entity_count, cfg.refresh_s);
    int bl = night_init_backlight(cfg.dir);
    printf("[start] podswietlenie: %s\n", bl ? "backlight.prx" : "brak modulu, przyciemnianie obrazu");
    LOCKED(
        snprintf(state.host, sizeof(state.host), "%s", cfg.host);
        state.refresh_s = cfg.refresh_s;
        state.autoexit_s = cfg.autoexit_s;
        state.night = cfg.night;
        snprintf(state.test_chart, sizeof(state.test_chart), "%s", cfg.test_chart);
        state.backlight = bl;
        state.static_count = cfg.entity_count;
        state.entity_count = cfg.entity_count;
        for (int i = 0; i < cfg.entity_count; i++) {
            snprintf(state.ents[i].id, sizeof(state.ents[i].id), "%s", cfg.entities[i]);
            snprintf(state.ents[i].name, sizeof(state.ents[i].name), "%s", cfg.entities[i]);
            snprintf(state.ents[i].error, sizeof(state.ents[i].error), "...");
        }
    );

    // 2. Wi-Fi
    set_step(STEP_WIFI, STEP_ACTIVE, "profil %d", cfg.wifi_profile);
    char ip[32] = "?";
    int ret = net_connect(cfg.wifi_profile, 20000, ip, sizeof(ip));
    if (ret < 0) {
        set_step(STEP_WIFI, STEP_ERROR, "kod 0x%08X", (unsigned)ret);
        fail("Brak połączenia Wi-Fi",
             "PPSSPP: Ustawienia > Sieć > włącz sieć.\n"
             "PSP: zapisz sieć w Ustawienia > Ustawienia sieci.");
        return 0;
    }
    LOCKED(snprintf(state.ip, sizeof(state.ip), "%s", ip));
    set_step(STEP_WIFI, STEP_OK, "IP %s", ip);

    // 3. Generator losowy
    set_step(STEP_RNG, STEP_ACTIVE, NULL);
    int hw = rng_init(cfg.dir);
    LOCKED(state.rng_hw = hw; snprintf(state.rng, sizeof(state.rng), "%s", rng_status()));
    set_step(STEP_RNG, hw ? STEP_OK : STEP_WARN, "%s", rng_status());

    // 4. TLS
    set_step(STEP_TLS, STEP_ACTIVE, "wczytywanie CA");
    if (https_global_init(cfg.ca_path[0] ? cfg.ca_path : NULL, err, sizeof(err)) != 0) {
        set_step(STEP_TLS, STEP_ERROR, "%s", err);
        fail("Błąd TLS", "Sprawdź plik CA wskazany w config.ini (klucz ca).");
        return 0;
    }
    if (https_client_init(&client, cfg.host, cfg.port) != 0) {
        set_step(STEP_TLS, STEP_ERROR, "konfiguracja klienta");
        fail("Błąd TLS", NULL);
        return 0;
    }
    set_step(STEP_TLS, STEP_OK, "CA: %s", https_ca_source());

    // 5. Pierwsze połączenie z Home Assistant
    set_step(STEP_HA, STEP_ACTIVE, "%s:%d", cfg.host, cfg.port);
    if (cfg.source_count) {
        set_step(STEP_HA, STEP_ACTIVE, "lista encji: %s", cfg.sources[0]);
        int n = resolve_entities();
        if (n <= 0 && client.handshakes) {
            SDL_LockMutex(lock);
            char msg[96];
            snprintf(msg, sizeof(msg), "%s", n < 0 ? state.list_error : "lista encji jest pusta");
            SDL_UnlockMutex(lock);
            set_step(STEP_HA, STEP_ERROR, "%s", msg);
            fail("Brak encji do wyświetlenia",
                 "Nadaj encjom etykietę (np. psp) w HA albo dodaj je\n"
                 "do grupy; możesz też wpisać linie entity w config.ini.");
            https_client_free(&client);
            return 0;
        }
    }
    if (refresh_all() != 0 && !client.handshakes) {
        // Ani razu się nie połączyliśmy: pokaż błąd na ekranie startowym.
        SDL_LockMutex(lock);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s", state.refresh_error);
        SDL_UnlockMutex(lock);
        set_step(STEP_HA, STEP_ERROR, "%s", msg);
        fail("Nie można połączyć z Home Assistant",
             "Sprawdź host i port w config.ini oraz to,\n"
             "czy certyfikat pasuje do nazwy hosta.");
        https_client_free(&client);
        return 0;
    }
    set_step(STEP_HA, STEP_OK, "%s", client.tls_version);
    LOCKED(state.running = 1);

    // Pętla odświeżania
    SceInt64 next = sceKernelGetSystemTimeWide() + (SceInt64)cfg.refresh_s * 1000000;
    SceInt64 next_list = sceKernelGetSystemTimeWide() + (SceInt64)LIST_REFRESH_S * 1000000;
    while (!quit_flag) {
        SDL_LockMutex(lock);
        int want_chart = chart_req_gen != chart_done_gen;
        SDL_UnlockMutex(lock);
        if (want_chart) load_chart();

        if (refresh_flag || sceKernelGetSystemTimeWide() >= next) {
            refresh_flag = 0;
            if (cfg.source_count && sceKernelGetSystemTimeWide() >= next_list) {
                resolve_entities();
                next_list = sceKernelGetSystemTimeWide() + (SceInt64)LIST_REFRESH_S * 1000000;
            }
            refresh_all();
            next = sceKernelGetSystemTimeWide() + (SceInt64)cfg.refresh_s * 1000000;
        }
        sceKernelDelayThread(100 * 1000);
    }

    https_client_free(&client);
    https_global_free();
    return 0;
}

int worker_start(const char *argv0) {
    snprintf(argv0_copy, sizeof(argv0_copy), "%s", argv0 ? argv0 : "");
    lock = SDL_CreateMutex();
    if (!lock) return -1;
    memset(&state, 0, sizeof(state));
    snprintf(state.rng, sizeof(state.rng), "nie sprawdzono");
    // mbedTLS potrzebuje sporo stosu przy handshake'u.
    thread = SDL_CreateThreadWithStackSize(worker_main, "worker", 256 * 1024, NULL);
    return thread ? 0 : -1;
}

void worker_snapshot(app_state *out) {
    SDL_LockMutex(lock);
    *out = state;
    SDL_UnlockMutex(lock);
}

unsigned int worker_version(void) {
    SDL_LockMutex(lock);
    unsigned int v = state.version;
    SDL_UnlockMutex(lock);
    return v;
}

void worker_request_refresh(void) { refresh_flag = 1; }

void worker_request_chart(const char *entity_id, int hours) {
    SDL_LockMutex(lock);
    snprintf(chart_req_entity, sizeof(chart_req_entity), "%s", entity_id);
    chart_req_hours = hours;
    chart_req_gen++;
    memset(&state.chart, 0, sizeof(state.chart));
    state.chart.status = CHART_LOADING;
    state.chart.hours = hours;
    snprintf(state.chart.entity, sizeof(state.chart.entity), "%s", entity_id);
    state.version++;
    SDL_UnlockMutex(lock);
}

void worker_close_chart(void) {
    SDL_LockMutex(lock);
    chart_done_gen = chart_req_gen;  // porzuć oczekujące żądanie
    state.chart.status = CHART_NONE;
    state.version++;
    SDL_UnlockMutex(lock);
}

void worker_stop(void) {
    quit_flag = 1;
    if (thread) SDL_DetachThread(thread);  // nie czekamy na ewentualny timeout sieci
    thread = NULL;
}

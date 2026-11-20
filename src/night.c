#include "night.h"

#include <stdio.h>

#ifdef __psp__
#include "kmod.h"
// Funkcje z backlight.prx (słaby import; wołać tylko, gdy bl_ok == 1).
int backlight_set(int level);
int backlight_get(void);
#endif

static int bl_ok = 0;
#ifdef __psp__
static int day_level = 100;     // jasność sprzed przyciemnienia
#endif
static int dimmed = 0;          // 1 = obecnie przyciemnione
static int overlay = 0;
static unsigned int awake_until = 0;

int night_parse(const char *s, int *from_min, int *to_min) {
    int h1, m1, h2, m2;
    if (!s || sscanf(s, "%d:%d-%d:%d", &h1, &m1, &h2, &m2) != 4) return -1;
    if (h1 < 0 || h1 > 23 || h2 < 0 || h2 > 23 || m1 < 0 || m1 > 59 || m2 < 0 || m2 > 59) return -1;
    *from_min = h1 * 60 + m1;
    *to_min = h2 * 60 + m2;
    return *from_min == *to_min ? -1 : 0;
}

int night_in_window(int from_min, int to_min, int minute) {
    if (from_min < to_min) return minute >= from_min && minute < to_min;  // np. 01:00-06:00
    return minute >= from_min || minute < to_min;                         // przez północ
}

int night_init_backlight(const char *dir) {
#ifdef __psp__
    if (kmod_load(dir, "backlight.prx") < 0) return 0;
    int level = backlight_get();
    if (level >= 0 && level <= 100) day_level = level > 0 ? level : 100;
    bl_ok = 1;
#else
    (void)dir;
#endif
    return bl_ok;
}

int night_backlight_available(void) { return bl_ok; }

static void apply(int dim, int night_level) {
    if (dim == dimmed) return;
    dimmed = dim;
    printf("[noc] %s (%s, jasnosc %d)\n", dim ? "przyciemnienie" : "jasno",
           bl_ok ? "podswietlenie" : "nakladka", dim ? night_level : 100);
#ifdef __psp__
    if (bl_ok) {
        backlight_set(dim ? night_level : day_level);
        overlay = 0;
        return;
    }
#endif
    overlay = dim ? 100 - night_level : 0;
}

int night_update(const night_config *cfg, int minute_of_day, unsigned int now_ms, int any_press) {
    if (!cfg->enabled) {
        apply(0, 100);
        return 0;
    }
    int in_night = night_in_window(cfg->from_min, cfg->to_min, minute_of_day);
    int woke = 0;
    if (in_night && any_press) {
        woke = dimmed;  // pierwsze naciśnięcie tylko budzi ekran
        awake_until = now_ms + (unsigned int)cfg->wake_s * 1000;
    }
    int awake = (int)(awake_until - now_ms) > 0;
    apply(in_night && !awake, cfg->brightness);
    return woke;
}

int night_active(void) { return dimmed; }
int night_overlay(void) { return overlay; }

void night_restore(void) {
#ifdef __psp__
    if (bl_ok && dimmed) backlight_set(day_level);
#endif
    dimmed = 0;
    overlay = 0;
}

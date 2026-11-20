// Testy czystej logiki (czas, wykres, tryb nocny) na komputerze.
// Uruchamianie: tools/test/run.sh
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "timeutil.h"
#include "chart.h"
#include "night.h"
#include "i18n.h"

static int fails;

// Ciąg specyfikatorów printf ("%s", "%.40s", "%u"...), bez "%%".
static void specs(const char *f, char *out, size_t len) {
    size_t n = 0;
    for (; *f && n + 2 < len; f++) {
        if (*f != '%') continue;
        if (f[1] == '%') { f++; continue; }
        while (*++f && !strchr("diouxXeEfgGcspn", *f)) out[n++] = *f;
        if (*f) out[n++] = *f;
        out[n++] = ' ';
        if (!*f) break;
    }
    out[n] = '\0';
}
#define CHECK(c) do { if (!(c)) { printf("BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    // timeutil
    CHECK(tu_from_civil(1970, 1, 1, 0, 0, 0) == 0);
    CHECK(tu_from_civil(2026, 11, 20, 12, 0, 0) == 1795176000LL);
    CHECK(tu_from_civil(2024, 2, 29, 23, 59, 59) == 1709251199LL);
    char buf[32];
    tu_format_iso(1795176000LL, buf, sizeof(buf));
    CHECK(!strcmp(buf, "2026-11-20T12:00:00+00:00"));
    tu_time t;
    CHECK(tu_parse_iso("2026-11-20T12:00:00.123456+00:00", &t) == 0 && t == 1795176000LL);
    CHECK(tu_parse_iso("2026-11-20T13:00:00+01:00", &t) == 0 && t == 1795176000LL);
    CHECK(tu_parse_iso("2026-11-20T12:00:00Z", &t) == 0 && t == 1795176000LL);
    CHECK(tu_parse_iso("2026-11-20 12:00", &t) == 0 && t == 1795176000LL);
    CHECK(tu_parse_iso("nie-data", &t) == -1);
    CHECK(tu_parse_iso("2026-13-01T00:00:00Z", &t) == -1);

    // chart: liczby
    static chart_acc a;
    static chart_data d;
    tu_time start = 1000000, end = start + 3600 * 2;  // 2 h
    chart_begin(&a, start, end);
    CHECK(chart_add(&a, start - 50, "20.0"));          // stan sprzed zakresu
    CHECK(chart_add(&a, start + 3600, "22.0"));        // połowa zakresu
    CHECK(!chart_add(&a, start + 3700, "unavailable"));
    CHECK(chart_add(&a, end + 10, "21.0"));            // spóźniona próbka trafia do ostatniego kubełka
    memset(&d, 0, sizeof(d));
    CHECK(chart_finish(&a, &d) == CHART_BUCKETS);
    CHECK(!d.binary);
    CHECK(fabsf(d.v[0] - 20.0f) < 1e-4 && fabsf(d.v[CHART_BUCKETS / 2] - 22.0f) < 1e-4);
    CHECK(fabsf(d.v[CHART_BUCKETS / 2 + 5] - 22.0f) < 1e-4);  // stan trwa do następnej zmiany
    CHECK(fabsf(d.last - 21.0f) < 1e-4 && fabsf(d.min - 20.0f) < 1e-4 && fabsf(d.max - 22.0f) < 1e-4);
    CHECK(d.samples == 3);

    // chart: bez stanu początkowego puste kubełki na początku
    chart_begin(&a, start, end);
    chart_add(&a, start + 3600, "5");
    memset(&d, 0, sizeof(d));
    CHECK(chart_finish(&a, &d) == CHART_BUCKETS / 2);
    CHECK(!d.has[0] && d.has[CHART_BUCKETS - 1]);

    // chart: wł./wył.
    chart_begin(&a, start, end);
    chart_add(&a, start - 1, "off");
    chart_add(&a, start + 600, "on");
    chart_add(&a, start + 1200, "off");
    memset(&d, 0, sizeof(d));
    chart_finish(&a, &d);
    CHECK(d.binary && d.max == 1 && d.min == 0 && d.last == 0);
    // krótkie włączenie w środku kubełka: pełna wysokość, potem znów wyłączone
    chart_begin(&a, start, end);
    chart_add(&a, start - 1, "off");
    chart_add(&a, start + 3600, "on");
    chart_add(&a, start + 3610, "off");
    memset(&d, 0, sizeof(d));
    chart_finish(&a, &d);
    CHECK(d.v[CHART_BUCKETS / 2] == 1 && d.v[CHART_BUCKETS / 2 + 1] == 0 && d.v[0] == 0);
    // liczby: kubełek po zmianie przenosi ostatnią wartość, nie średnią
    chart_begin(&a, start, end);
    chart_add(&a, start + 3600, "10");
    chart_add(&a, start + 3601, "30");
    memset(&d, 0, sizeof(d));
    chart_finish(&a, &d);
    CHECK(d.v[CHART_BUCKETS / 2] == 20 && d.v[CHART_BUCKETS / 2 + 1] == 30 && d.last == 30);

    // chart: brak danych
    chart_begin(&a, start, end);
    chart_add(&a, start + 10, "unknown");
    memset(&d, 0, sizeof(d));
    CHECK(chart_finish(&a, &d) == 0);

    // night
    int f, to;
    CHECK(night_parse("22:00-07:00", &f, &to) == 0 && f == 1320 && to == 420);
    CHECK(night_parse("1:30-6:00", &f, &to) == 0 && f == 90 && to == 360);
    CHECK(night_parse("25:00-07:00", &f, &to) == -1);
    CHECK(night_parse("07:00-07:00", &f, &to) == -1);
    CHECK(night_parse("rano", &f, &to) == -1);
    CHECK(night_in_window(1320, 420, 23 * 60));
    CHECK(night_in_window(1320, 420, 3 * 60));
    CHECK(!night_in_window(1320, 420, 7 * 60));
    CHECK(!night_in_window(1320, 420, 12 * 60));
    CHECK(night_in_window(90, 360, 120) && !night_in_window(90, 360, 400));

    // night: przyciemnianie i budzenie (bez backlight.prx → nakładka)
    night_config nc = {1, 1320, 420, 10, 30};
    CHECK(night_update(&nc, 12 * 60, 1000, 0) == 0 && !night_active() && night_overlay() == 0);
    CHECK(night_update(&nc, 23 * 60, 2000, 0) == 0 && night_active() && night_overlay() == 90);
    CHECK(night_update(&nc, 23 * 60, 3000, 1) == 1 && !night_active());   // budzi, przycisk połknięty
    CHECK(night_update(&nc, 23 * 60, 4000, 1) == 0 && !night_active());   // kolejny działa normalnie
    CHECK(night_update(&nc, 23 * 60, 4000 + 29000, 0) == 0 && !night_active());
    CHECK(night_update(&nc, 23 * 60, 4000 + 31000, 0) == 0 && night_active());  // po 30 s znów ciemno
    CHECK(night_update(&nc, 7 * 60, 40000, 0) == 0 && !night_active());   // rano jasno
    nc.enabled = 0;
    CHECK(night_update(&nc, 23 * 60, 50000, 0) == 0 && !night_active());

    // i18n: angielskie wzorce mają te same specyfikatory co polskie
    for (int i = 0; i < i18n_count(); i++) {
        char a[64], b[64];
        specs(i18n_pl(i), a, sizeof(a));
        specs(i18n_en(i), b, sizeof(b));
        if (strcmp(a, b)) printf("  i18n: \"%s\" [%s] != [%s]\n", i18n_pl(i), a, b);
        CHECK(!strcmp(a, b));
        for (int j = 0; j < i; j++) CHECK(strcmp(i18n_pl(i), i18n_pl(j)));  // bez duplikatów
    }
    CHECK(i18n_get() == LANG_EN);   // domyślny język
    i18n_set(LANG_PL);
    CHECK(!strcmp(tr("teraz"), "teraz") && i18n_decimal() == ',');
    i18n_set(LANG_EN);
    CHECK(!strcmp(tr("teraz"), "now") && i18n_decimal() == '.');
    CHECK(!strcmp(tr("tekst spoza tabeli"), "tekst spoza tabeli"));
    i18n_set(LANG_PL);

    printf(fails ? "%d błędów\n" : "wszystko OK\n", fails);
    return fails != 0;
}

#include "chart.h"

#include <stdlib.h>
#include <string.h>

void chart_begin(chart_acc *a, tu_time start, tu_time end) {
    memset(a, 0, sizeof(*a));
    a->start = start;
    a->end = end > start ? end : start + 1;
}

// Liczba albo stan dwójkowy (wł./wył.). Zwraca 1 = liczba, 2 = dwójkowy, 0 = pomiń.
static int parse_value(const char *s, float *out) {
    static const char *on[] = {"on", "open", "home", "playing", "detected", "wet", NULL};
    static const char *off[] = {"off", "closed", "not_home", "idle", "clear", "dry", NULL};
    if (!s || !*s) return 0;
    for (int i = 0; on[i]; i++)
        if (!strcmp(s, on[i])) { *out = 1; return 2; }
    for (int i = 0; off[i]; i++)
        if (!strcmp(s, off[i])) { *out = 0; return 2; }
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end) return 0;   // "unavailable", "unknown", "21.5abc"
    if (v != v) return 0;             // NaN
    *out = v;
    return 1;
}

int chart_add(chart_acc *a, tu_time t, const char *state) {
    float v;
    int kind = parse_value(state, &v);
    if (!kind) return 0;
    if (kind == 1) a->numeric++;
    else a->binary++;

    if (t < a->start) {  // stan obowiązujący na początku zakresu
        a->before = v;
        a->have_before = 1;
        return 1;
    }
    if (t >= a->end) t = a->end - 1;
    long long span = a->end - a->start;
    int b = (int)((t - a->start) * CHART_BUCKETS / span);
    if (b < 0) b = 0;
    if (b >= CHART_BUCKETS) b = CHART_BUCKETS - 1;
    a->sum[b] += v;
    if (!a->cnt[b] || v > a->maxv[b]) a->maxv[b] = v;
    a->lastv[b] = v;
    if (a->cnt[b] < 65535) a->cnt[b]++;
    return 1;
}

int chart_finish(const chart_acc *a, chart_data *d) {
    // Mieszanka liczb i wł./wył. jest mało prawdopodobna; decyduje większość.
    d->binary = a->binary > a->numeric;
    d->samples = a->numeric + a->binary;
    int filled = 0, have = a->have_before;
    float cur = a->before;
    d->min = d->max = d->last = 0;
    for (int i = 0; i < CHART_BUCKETS; i++) {
        float shown = cur;
        if (a->cnt[i]) {
            if (d->binary) shown = (have && cur > a->maxv[i]) ? cur : a->maxv[i];
            else shown = a->sum[i] / a->cnt[i];
            cur = a->lastv[i];
            have = 1;
        }
        d->has[i] = (unsigned char)have;
        d->v[i] = have ? shown : 0;
        if (have) {
            if (!filled || shown < d->min) d->min = shown;
            if (!filled || shown > d->max) d->max = shown;
            filled++;
        }
    }
    d->last = cur;
    return filled;
}

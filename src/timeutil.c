#include "timeutil.h"

#include <stdio.h>
#include <ctype.h>

#ifdef __psp__
#include <psprtc.h>
#else
#include <time.h>
#endif

// Algorytm "days from civil" (H. Hinnant), działa dla kalendarza gregoriańskiego.
static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

static void civil_from_days(long long z, int *y, int *m, int *d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long yy = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

tu_time tu_from_civil(int y, int mon, int d, int h, int min, int s) {
    return days_from_civil(y, mon, d) * 86400 + h * 3600 + min * 60 + s;
}

void tu_format_iso(tu_time t, char *out, int len) {
    long long days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    int sod = (int)(t - days * 86400);
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d+00:00",
             y, m, d, sod / 3600, sod / 60 % 60, sod % 60);
}

static int num(const char **p, int digits, int *out) {
    int v = 0;
    for (int i = 0; i < digits; i++) {
        if (!isdigit((unsigned char)**p)) return -1;
        v = v * 10 + (**p - '0');
        (*p)++;
    }
    *out = v;
    return 0;
}

int tu_parse_iso(const char *s, tu_time *out) {
    int y, mo, d, h, mi, sec = 0;
    const char *p = s;
    if (!p) return -1;
    if (num(&p, 4, &y) || *p++ != '-' || num(&p, 2, &mo) || *p++ != '-' || num(&p, 2, &d)) return -1;
    if (*p != 'T' && *p != ' ') return -1;
    p++;
    if (num(&p, 2, &h) || *p++ != ':' || num(&p, 2, &mi)) return -1;
    if (*p == ':') {
        p++;
        if (num(&p, 2, &sec)) return -1;
    }
    if (*p == '.')  // ułamki sekundy pomijamy
        for (p++; isdigit((unsigned char)*p); p++) {}

    int offset = 0;
    if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1, oh, om = 0;
        p++;
        if (num(&p, 2, &oh)) return -1;
        if (*p == ':') p++;
        if (isdigit((unsigned char)*p) && num(&p, 2, &om)) return -1;
        offset = sign * (oh * 3600 + om * 60);
    } else if (*p == 'Z') {
        p++;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return -1;
    *out = tu_from_civil(y, mo, d, h, mi, sec) - offset;
    return 0;
}

tu_time tu_now(void) {
#ifdef __psp__
    u64 tick = 0;
    if (sceRtcGetCurrentTick(&tick) != 0) return 0;
    // Tyknięcia RTC: mikrosekundy od 0001-01-01 UTC.
    return (tu_time)(tick / 1000000ULL) - 62135596800LL;
#else
    return (tu_time)time(NULL);
#endif
}

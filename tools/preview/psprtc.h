// Zamiennik psprtc.h dla podglądu GUI na komputerze (nie dla PSP).
#pragma once
#include <time.h>

typedef struct ScePspDateTime {
    unsigned short year, month, day, hour, minute, second;
    unsigned int microsecond;
} ScePspDateTime;

// Podgląd pokazuje stałą godzinę, żeby zrzuty były powtarzalne.
static inline int sceRtcGetCurrentClockLocalTime(ScePspDateTime *t) {
    t->year = 2026; t->month = 11; t->day = 20;
    t->hour = 14; t->minute = 32; t->second = 7;
    t->microsecond = 0;
    return 0;
}

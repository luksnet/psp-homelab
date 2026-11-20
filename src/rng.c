#include "rng.h"
#include "kmod.h"

#include <stdio.h>
#include <string.h>
#include <pspkernel.h>

// Funkcja z modułu kirkrng.prx (stub w kirkrng/KirkRng.S, import "słaby":
// rozwiązywany dopiero po wczytaniu modułu). Wolno ją wołać tylko, gdy hw_ok == 1.
int kirkrng_get(void *buf, int len);

static int hw_ok = 0;
static char status[64] = "nie sprawdzono";

static int all_same(const unsigned char *b, int n) {
    for (int i = 1; i < n; i++)
        if (b[i] != b[0]) return 0;
    return 1;
}

int rng_init(const char *dir) {
    int mod = kmod_load(dir, "kirkrng.prx");
    if (mod < 0) {
        snprintf(status, sizeof(status), "SŁABY: brak kirkrng.prx (0x%08X)", (unsigned)mod);
        return 0;
    }

    // Prosty test poprawności: dwa odczyty muszą się udać, różnić się
    // i nie składać się z jednego powtarzanego bajtu.
    unsigned char a[64], b[64];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    int ra = kirkrng_get(a, sizeof(a));
    int rb = kirkrng_get(b, sizeof(b));
    if (ra != (int)sizeof(a) || rb != (int)sizeof(b)) {
        snprintf(status, sizeof(status), "SŁABY: KIRK błąd %d/%d", ra, rb);
        return 0;
    }
    if (memcmp(a, b, sizeof(a)) == 0 || all_same(a, sizeof(a)) || all_same(b, sizeof(b))) {
        snprintf(status, sizeof(status), "SŁABY: KIRK zwraca stałe dane");
        return 0;
    }

    hw_ok = 1;
    snprintf(status, sizeof(status), "KIRK (sprzętowy)");
    return 1;
}

int rng_hw_available(void) { return hw_ok; }
const char *rng_status(void) { return status; }

int rng_hw_entropy(void *data, unsigned char *out, size_t len, size_t *olen) {
    (void)data;
    *olen = 0;
    if (!hw_ok) return -1;
    size_t done = 0;
    while (done < len) {
        int n = (int)(len - done);
        if (n > 4096) n = 4096;
        int ret = kirkrng_get(out + done, n);
        if (ret != n) return -1;  // MBEDTLS_ERR_ENTROPY_SOURCE_FAILED obsłuży wywołujący
        done += n;
    }
    *olen = len;
    return 0;
}

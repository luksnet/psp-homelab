// Źródło losowości: sprzętowy generator KIRK przez moduł kirkrng.prx.
#pragma once
#include <stddef.h>

// Wczytuje kirkrng.prx z katalogu dir (z ukośnikiem na końcu) i sprawdza,
// czy generator zwraca sensowne dane. Zwraca 1 = KIRK działa, 0 = brak.
int rng_init(const char *dir);

int rng_hw_available(void);
const char *rng_status(void);  // krótki opis do wyświetlenia

// Callback źródła entropii dla mbedTLS.
int rng_hw_entropy(void *data, unsigned char *out, size_t len, size_t *olen);

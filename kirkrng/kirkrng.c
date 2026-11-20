// kirkrng: moduł jądra udostępniający sprzętowy generator liczb losowych
// układu kryptograficznego KIRK (polecenie 0xE, PRNG) programom w trybie użytkownika.
//
// KIRK jest dostępny tylko z trybu jądra, dlatego to osobny moduł PRX,
// który program główny wczytuje przy starcie (wymaga custom firmware, np. ARK-4).

#include <pspkernel.h>
#include <pspsdk.h>

PSP_MODULE_INFO("kirkrng", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define KIRK_CMD_PRNG   0x0E
#define KIRK_PRNG_BYTES 0x14   // KIRK zwraca 20 bajtów na wywołanie
#define MAX_REQUEST     4096

// Faktyczna kolejność argumentów: wyjście, rozmiar wyjścia, wejście, rozmiar wejścia, polecenie.
// (W nagłówku pspcrypt.h nazwy parametrów są zamienione.)
int sceUtilsBufferCopyWithRange(void *outbuf, SceSize outsize, void *inbuf, SceSize insize, int cmd);

static unsigned char kbuf[KIRK_PRNG_BYTES] __attribute__((aligned(64)));

// Wypełnia buf (w pamięci użytkownika) losowymi bajtami z KIRK.
// Zwraca liczbę bajtów albo kod błędu < 0.
int kirkrng_get(void *buf, int len) {
    unsigned int k1 = pspSdkSetK1(0);
    int result = len;

    // Odrzucamy wskaźniki do pamięci jądra (najwyższy bit adresu) i absurdalne długości.
    unsigned int addr = (unsigned int)buf;
    if (!buf || len <= 0 || len > MAX_REQUEST ||
        ((addr | (addr + (unsigned int)len)) & 0x80000000u)) {
        pspSdkSetK1(k1);
        return -1;
    }

    unsigned char *out = (unsigned char *)buf;
    int done = 0;
    while (done < len) {
        int ret = sceUtilsBufferCopyWithRange(kbuf, KIRK_PRNG_BYTES, NULL, 0, KIRK_CMD_PRNG);
        if (ret != 0) {
            result = ret < 0 ? ret : -2;
            break;
        }
        int n = len - done;
        if (n > KIRK_PRNG_BYTES) n = KIRK_PRNG_BYTES;
        for (int i = 0; i < n; i++) out[done + i] = kbuf[i];
        done += n;
    }

    // Nie zostawiamy losowych bajtów w pamięci modułu.
    for (int i = 0; i < KIRK_PRNG_BYTES; i++) ((volatile unsigned char *)kbuf)[i] = 0;

    pspSdkSetK1(k1);
    return result;
}

int module_start(SceSize args, void *argp) { return 0; }
int module_stop(SceSize args, void *argp) { return 0; }

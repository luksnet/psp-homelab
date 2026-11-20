// backlight: moduł jądra do zmiany jasności podświetlenia ekranu PSP.
// sceDisplaySetBrightness jest dostępne tylko w trybie jądra (sceDisplay_driver).
// Osobny moduł od kirkrng, żeby problem z jasnością nie wyłączał generatora losowego.

#include <pspkernel.h>
#include <pspsdk.h>
#include <pspdisplay_kernel.h>

PSP_MODULE_INFO("backlight", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

// Ustawia jasność 0..100. Zwraca 0 albo <0 przy złym argumencie.
int backlight_set(int level) {
    unsigned int k1 = pspSdkSetK1(0);
    if (level < 0 || level > 100) {
        pspSdkSetK1(k1);
        return -1;
    }
    sceDisplaySetBrightness(level, 0);
    pspSdkSetK1(k1);
    return 0;
}

// Zwraca bieżącą jasność 0..100 albo <0.
int backlight_get(void) {
    unsigned int k1 = pspSdkSetK1(0);
    int level = -1, unk = 0;
    sceDisplayGetBrightness(&level, &unk);
    pspSdkSetK1(k1);
    return level;
}

int module_start(SceSize args, void *argp) { return 0; }
int module_stop(SceSize args, void *argp) { return 0; }

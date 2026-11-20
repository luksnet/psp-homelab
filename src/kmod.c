#include "kmod.h"

#include <stdio.h>
#include <pspkernel.h>

int kmod_load(const char *dir, const char *name) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", dir ? dir : "", name);
    SceUID mod = sceKernelLoadModule(path, 0, NULL);
    if (mod < 0) return mod;
    int status = 0;
    int ret = sceKernelStartModule(mod, 0, NULL, &status, NULL);
    if (ret < 0) {
        sceKernelUnloadModule(mod);
        return ret;
    }
    return mod;
}

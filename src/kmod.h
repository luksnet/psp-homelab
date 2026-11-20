// Wczytywanie modułów jądra (PRX) leżących obok EBOOT.PBP.
#pragma once

// Wczytuje i uruchamia dir + name (np. "ms0:/PSP/GAME/psp-homelab/" + "backlight.prx").
// Zwraca UID modułu (>=0) albo kod błędu PSP (<0).
int kmod_load(const char *dir, const char *name);

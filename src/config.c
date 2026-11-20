#include "config.h"
#include "i18n.h"
#include "night.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void copy(char *dst, size_t len, const char *src) {
    snprintf(dst, len, "%s", src);
}

// Katalog pliku, z ukośnikiem na końcu ("ms0:/PSP/GAME/x/").
static void dir_of(const char *path, char *out, size_t len) {
    out[0] = '\0';
    if (!path) return;
    const char *slash = strrchr(path, '/');
    if (!slash) return;
    size_t n = (size_t)(slash - path) + 1;
    if (n >= len) return;
    memcpy(out, path, n);
    out[n] = '\0';
}

static FILE *open_config(const char *argv0, char *found, size_t len, char *dir, size_t dirlen) {
    char candidates[3][256];
    char d0[200];
    dir_of(argv0, d0, sizeof(d0));
    snprintf(candidates[0], sizeof(candidates[0]), "%sconfig.ini", d0);
    snprintf(candidates[1], sizeof(candidates[1]), "config.ini");
    snprintf(candidates[2], sizeof(candidates[2]), "ms0:/PSP/GAME/psp-homelab/config.ini");

    for (int i = 0; i < 3; i++) {
        if (i == 0 && !d0[0]) continue;
        FILE *f = fopen(candidates[i], "r");
        if (f) {
            copy(found, len, candidates[i]);
            dir_of(candidates[i], dir, dirlen);
            return f;
        }
    }
    return NULL;
}

int config_load(const char *argv0, app_config *cfg, char *err, int errlen) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 443;
    cfg->refresh_s = 30;
    cfg->wifi_profile = 1;
    cfg->night.brightness = 10;
    cfg->night.wake_s = 30;

    char *dir = cfg->dir;
    FILE *f = open_config(argv0, cfg->path, sizeof(cfg->path), dir, sizeof(cfg->dir));
    if (!f) {
        snprintf(err, errlen, "config.ini not found / nie znaleziono config.ini");
        return -2;
    }

    // Najpierw sam klucz lang, żeby kolejne błędy były już w wybranym języku.
    char line[640];
    i18n_set(LANG_EN);
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line), *eq = strchr(s, '=');
        if (!eq || *s == '#' || *s == ';') continue;
        *eq = '\0';
        if (!strcmp(trim(s), "lang") && !strcmp(trim(eq + 1), "pl")) i18n_set(LANG_PL);
    }
    rewind(f);

    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        char *eq = strchr(s, '=');
        if (!eq) {
            snprintf(err, errlen, tr("linia %d: brak '='"), lineno);
            fclose(f);
            return -1;
        }
        *eq = '\0';
        char *key = trim(s);
        char *val = trim(eq + 1);

        if (!strcmp(key, "host"))              copy(cfg->host, sizeof(cfg->host), val);
        else if (!strcmp(key, "port"))         cfg->port = atoi(val);
        else if (!strcmp(key, "token"))        copy(cfg->token, sizeof(cfg->token), val);
        else if (!strcmp(key, "refresh"))      cfg->refresh_s = atoi(val);
        else if (!strcmp(key, "wifi_profile")) cfg->wifi_profile = atoi(val);
        else if (!strcmp(key, "autoexit"))     cfg->autoexit_s = atoi(val);
        else if (!strcmp(key, "lang")) {
            if (strcmp(val, "pl") && strcmp(val, "en")) {
                snprintf(err, errlen, tr("linia %d: lang = pl albo en"), lineno);
                fclose(f);
                return -1;
            }
        }
        else if (!strcmp(key, "test_chart"))   copy(cfg->test_chart, sizeof(cfg->test_chart), val);
        else if (!strcmp(key, "ca")) {
            if (*val == '\0') cfg->ca_path[0] = '\0';
            else if (strchr(val, ':')) copy(cfg->ca_path, sizeof(cfg->ca_path), val);  // pełna ścieżka
            else snprintf(cfg->ca_path, sizeof(cfg->ca_path), "%s%s", dir, val);      // obok config.ini
        } else if (!strcmp(key, "night")) {
            if (!*val || !strcmp(val, "off")) cfg->night.enabled = 0;
            else if (night_parse(val, &cfg->night.from_min, &cfg->night.to_min) == 0) cfg->night.enabled = 1;
            else {
                snprintf(err, errlen, tr("linia %d: night = GG:MM-GG:MM"), lineno);
                fclose(f);
                return -1;
            }
        } else if (!strcmp(key, "night_brightness")) {
            cfg->night.brightness = atoi(val);
        } else if (!strcmp(key, "night_wake")) {
            cfg->night.wake_s = atoi(val);
        } else if (!strcmp(key, "entities_from")) {
            // label:NAZWA albo encja-grupa (group.psp, light.psp, sensor.psp...):
            // dowolna encja z atrybutem entity_id; sprawdza to dopiero HA.
            int okv = !strncmp(val, "label:", 6) ? val[6] != '\0' : strchr(val, '.') != NULL;
            if (!okv) {
                snprintf(err, errlen, tr("linia %d: entities_from = label:NAZWA albo encja grupy, np. light.psp"), lineno);
                fclose(f);
                return -1;
            }
            if (cfg->source_count < CFG_MAX_SOURCES)
                copy(cfg->sources[cfg->source_count++], sizeof(cfg->sources[0]), val);
        } else if (!strcmp(key, "entity")) {
            if (cfg->entity_count < CFG_MAX_ENTITIES)
                copy(cfg->entities[cfg->entity_count++], sizeof(cfg->entities[0]), val);
        }
        // Nieznane klucze pomijamy, żeby starszy program działał z nowszym plikiem.
    }
    fclose(f);

    if (!cfg->host[0])         { snprintf(err, errlen, "%s", tr("brak 'host' w config.ini"));  return -1; }
    if (!cfg->token[0])        { snprintf(err, errlen, "%s", tr("brak 'token' w config.ini")); return -1; }
    if (!cfg->entity_count && !cfg->source_count) {
        snprintf(err, errlen, "%s", tr("brak 'entity' ani 'entities_from' w config.ini"));
        return -1;
    }
    if (cfg->port <= 0 || cfg->port > 65535) cfg->port = 443;
    if (cfg->refresh_s < 5) cfg->refresh_s = 5;
    if (cfg->wifi_profile < 1) cfg->wifi_profile = 1;
    if (cfg->night.brightness < 0) cfg->night.brightness = 0;
    if (cfg->night.brightness > 100) cfg->night.brightness = 100;
    if (cfg->night.wake_s < 5) cfg->night.wake_s = 5;
    return 0;
}

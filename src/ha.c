#include "ha.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

// Kopiuje tekst UTF-8, a przy skracaniu nie przecina znaku wielobajtowego.
static void utf8_copy(char *dst, size_t len, const char *src) {
    size_t n = strlen(src);
    if (n >= len) {
        n = len - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int ha_fetch_state(https_client *c, const char *token, ha_entity *e) {
    char path[128];
    char headers[600];
    snprintf(path, sizeof(path), "/api/states/%s", e->id);
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);

    e->ok = 0;
    e->error[0] = '\0';

    https_response res;
    if (https_request(c, "GET", path, headers, NULL, &res) != 0) {
        snprintf(e->error, sizeof(e->error), "%.47s", res.error);
        return -1;
    }

    int rc = 0;
    if (res.status == 401) {
        snprintf(e->error, sizeof(e->error), "token odrzucony (401)");
        rc = -1;  // ten sam token dla wszystkich encji, nie ma sensu próbować dalej
    } else if (res.status == 404) {
        snprintf(e->error, sizeof(e->error), "brak encji (404)");
        rc = -2;
    } else if (res.status != 200) {
        snprintf(e->error, sizeof(e->error), "HTTP %d", res.status);
        rc = -2;
    } else {
        cJSON *root = cJSON_Parse(res.body);
        const cJSON *state = root ? cJSON_GetObjectItemCaseSensitive(root, "state") : NULL;
        if (!cJSON_IsString(state)) {
            snprintf(e->error, sizeof(e->error), "zła odpowiedź JSON");
            rc = -2;
        } else {
            utf8_copy(e->state, sizeof(e->state), state->valuestring);
            const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(root, "attributes");
            const cJSON *fn = cJSON_GetObjectItemCaseSensitive(attrs, "friendly_name");
            const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attrs, "unit_of_measurement");
            utf8_copy(e->name, sizeof(e->name), cJSON_IsString(fn) ? fn->valuestring : e->id);
            utf8_copy(e->unit, sizeof(e->unit), cJSON_IsString(unit) ? unit->valuestring : "");
            e->ok = 1;
        }
        cJSON_Delete(root);
    }
    https_response_free(&res);
    return rc;
}

// ---------------------------------------------------------------------------
// Lista encji z etykiety albo grupy
// ---------------------------------------------------------------------------

static int valid_name(const char *s) {
    if (!*s) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return 0;
    return 1;
}

static int valid_entity_id(const char *s) {
    const char *dot = strchr(s, '.');
    if (!dot || dot == s || !dot[1] || strlen(s) >= 64) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '.'))
            return 0;
    return 1;
}

static int cmp_id(const void *a, const void *b) { return strcmp(a, b); }

static int add_id(char ids[][64], int n, int max, const char *id) {
    if (n >= max || !valid_entity_id(id)) return n;
    for (int i = 0; i < n; i++)
        if (!strcmp(ids[i], id)) return n;
    snprintf(ids[n], 64, "%s", id);
    return n + 1;
}

int ha_fetch_entity_list(https_client *c, const char *token, const char *source,
                         char ids[][64], int max, char *err, int errlen) {
    char headers[640];
    https_response res;
    int n = 0;
    int is_label = !strncmp(source, "label:", 6);

    if (is_label) {
        const char *label = source + 6;
        // Nazwa trafia do szablonu Jinja, więc dopuszczamy tylko [a-z0-9_].
        if (!valid_name(label)) {
            snprintf(err, errlen, "etykieta: tylko a-z, 0-9 i _");
            return -1;
        }
        char body[160];
        snprintf(body, sizeof(body),
                 "{\"template\":\"{{ label_entities('%s') | join(',') }}\"}", label);
        snprintf(headers, sizeof(headers),
                 "Authorization: Bearer %s\r\nContent-Type: application/json\r\n", token);
        if (https_request(c, "POST", "/api/template", headers, body, &res) != 0) {
            snprintf(err, errlen, "%s", res.error);
            return -1;
        }
    } else {
        // Encja-grupa: group.psp z YAML albo pomocnik z interfejsu (light.psp, sensor.psp...).
        if (!valid_entity_id(source)) {
            snprintf(err, errlen, "%.40s: zły identyfikator encji", source);
            return -1;
        }
        char path[96];
        snprintf(path, sizeof(path), "/api/states/%s", source);
        snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
        if (https_request(c, "GET", path, headers, NULL, &res) != 0) {
            snprintf(err, errlen, "%s", res.error);
            return -1;
        }
    }

    if (res.status == 401) {
        // /api/template wymaga konta administratora; grupa działa z każdym tokenem.
        snprintf(err, errlen, is_label ? "%s: 401 (szablony wymagają admina, użyj group.)"
                                       : "%s: token odrzucony (401)", source);
        n = -1;
    } else if (res.status == 404) {
        snprintf(err, errlen, "%s: brak (404)", source);
        n = -1;
    } else if (res.status != 200) {
        snprintf(err, errlen, "%s: HTTP %d", source, res.status);
        n = -1;
    } else if (is_label) {
        // Odpowiedź to zwykły tekst: "light.a,sensor.b"
        char *list = res.body;
        for (char *tok = strtok(list, ", \r\n"); tok; tok = strtok(NULL, ", \r\n"))
            n = add_id(ids, n, max, tok);
    } else {
        cJSON *root = cJSON_Parse(res.body);
        const cJSON *attrs = cJSON_GetObjectItemCaseSensitive(root, "attributes");
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(attrs, "entity_id");
        if (!cJSON_IsArray(list)) {
            snprintf(err, errlen, "%s: to nie jest grupa", source);
            n = -1;
        } else {
            const cJSON *it;
            cJSON_ArrayForEach(it, list)
                if (cJSON_IsString(it)) n = add_id(ids, n, max, it->valuestring);
        }
        cJSON_Delete(root);
    }
    https_response_free(&res);
    if (n > 1) qsort(ids, (size_t)n, 64, cmp_id);
    return n;
}

// ---------------------------------------------------------------------------
// Historia
// ---------------------------------------------------------------------------

int ha_fetch_history(https_client *c, const char *token, const char *entity_id,
                     int hours, tu_time now, chart_data *d) {
    d->error[0] = '\0';
    if (!valid_entity_id(entity_id)) {
        snprintf(d->error, sizeof(d->error), "zły identyfikator encji");
        return -1;
    }
    tu_time start = now - (tu_time)hours * 3600;
    char t0[32], t1[32], path[256], headers[600];
    tu_format_iso(start, t0, sizeof(t0));
    tu_format_iso(now, t1, sizeof(t1));
    // "+" w adresie oznaczałby spację, więc strefę zapisujemy jako %2B.
    char *p0 = strchr(t0 + 19, '+'), *p1 = strchr(t1 + 19, '+');
    if (p0) *p0 = '\0';
    if (p1) *p1 = '\0';
    snprintf(path, sizeof(path),
             "/api/history/period/%s%%2B00:00?filter_entity_id=%s&end_time=%s%%2B00:00"
             "&minimal_response&no_attributes",
             t0, entity_id, t1);
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);

    https_response res;
    if (https_request(c, "GET", path, headers, NULL, &res) != 0) {
        snprintf(d->error, sizeof(d->error), "%.63s", res.error);
        return -1;
    }
    int rc = 0;
    if (res.status != 200) {
        snprintf(d->error, sizeof(d->error), res.status == 401 ? "token odrzucony (401)" : "HTTP %d",
                 res.status);
        rc = -1;
    } else {
        cJSON *root = cJSON_Parse(res.body);
        // Odpowiedź: [[{state, last_changed}, ...]] — jedna lista na encję.
        const cJSON *list = cJSON_IsArray(root) ? cJSON_GetArrayItem(root, 0) : NULL;
        if (!root || (list && !cJSON_IsArray(list))) {
            snprintf(d->error, sizeof(d->error), "zła odpowiedź JSON");
            rc = -1;
        } else {
            static chart_acc acc;  // ~1,2 KB, nie na stosie wątku
            chart_begin(&acc, start, now);
            const cJSON *it;
            cJSON_ArrayForEach(it, list) {
                const cJSON *st = cJSON_GetObjectItemCaseSensitive(it, "state");
                const cJSON *lc = cJSON_GetObjectItemCaseSensitive(it, "last_changed");
                if (!cJSON_IsString(lc)) lc = cJSON_GetObjectItemCaseSensitive(it, "last_updated");
                tu_time t;
                if (cJSON_IsString(st) && cJSON_IsString(lc) && tu_parse_iso(lc->valuestring, &t) == 0)
                    chart_add(&acc, t, st->valuestring);
            }
            if (chart_finish(&acc, d) == 0) {
                snprintf(d->error, sizeof(d->error), "brak danych liczbowych w tym zakresie");
                rc = -1;
            }
        }
        cJSON_Delete(root);
    }
    https_response_free(&res);
    return rc;
}

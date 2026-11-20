#include "ui.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <SDL2/SDL_ttf.h>
#include <psprtc.h>

// Czcionki osadzone przy budowaniu (assets/fonts). Długość obejmuje dopisane \0.
extern const unsigned char font_regular[];
extern const unsigned int font_regular_len;
extern const unsigned char font_bold[];
extern const unsigned int font_bold_len;

const int ui_chart_hours[UI_CHART_RANGES] = {3, 6, 24, 72};

static SDL_Renderer *R;
static TTF_Font *f_logo, *f_title, *f_name, *f_value, *f_small;

// Paleta Gruvbox (ciemna), jak na luks.net
static const SDL_Color BG      = {0x28, 0x28, 0x28, 255};
static const SDL_Color BG_HARD = {0x1d, 0x20, 0x21, 255};
static const SDL_Color BG1     = {0x3c, 0x38, 0x36, 255};
static const SDL_Color BG2     = {0x50, 0x49, 0x45, 255};
static const SDL_Color FG      = {0xeb, 0xdb, 0xb2, 255};
static const SDL_Color GRAY    = {0xa8, 0x99, 0x84, 255};
static const SDL_Color DIM     = {0x92, 0x83, 0x74, 255};
static const SDL_Color GREEN   = {0xb8, 0xbb, 0x26, 255};
static const SDL_Color RED     = {0xfb, 0x49, 0x34, 255};
static const SDL_Color YELLOW  = {0xfa, 0xbd, 0x2f, 255};
static const SDL_Color BLUE    = {0x83, 0xa5, 0x98, 255};
static const SDL_Color AQUA    = {0x8e, 0xc0, 0x7c, 255};

// ---------------------------------------------------------------------------
// Pamięć podręczna tekstur z tekstem: renderowanie czcionek jest na PSP drogie,
// a większość napisów nie zmienia się między klatkami.
// ---------------------------------------------------------------------------

#define CACHE_SIZE 96
typedef struct {
    TTF_Font *font;
    Uint32 color;
    char text[96];
    SDL_Texture *tex;
    int w, h;
    unsigned int used;
} text_entry;

static text_entry cache[CACHE_SIZE];
static unsigned int cache_tick;

static Uint32 color_key(SDL_Color c) {
    return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) | ((Uint32)c.b << 8) | c.a;
}

static text_entry *text_get(TTF_Font *f, SDL_Color c, const char *s) {
    if (!s || !s[0]) return NULL;
    Uint32 key = color_key(c);
    cache_tick++;

    int slot = -1;
    for (int i = 0; i < CACHE_SIZE; i++) {
        text_entry *e = &cache[i];
        if (e->tex && e->font == f && e->color == key && strcmp(e->text, s) == 0) {
            e->used = cache_tick;
            return e;
        }
        if (slot < 0 && !e->tex) slot = i;
    }
    if (slot < 0) {  // brak wolnego miejsca: wyrzuć najdawniej używany
        slot = 0;
        for (int i = 1; i < CACHE_SIZE; i++)
            if (cache[i].used < cache[slot].used) slot = i;
        SDL_DestroyTexture(cache[slot].tex);
        cache[slot].tex = NULL;
    }

    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s, c);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(R, surf);
    text_entry *e = &cache[slot];
    e->font = f;
    e->color = key;
    snprintf(e->text, sizeof(e->text), "%s", s);
    e->tex = tex;
    e->w = surf->w;
    e->h = surf->h;
    e->used = cache_tick;
    SDL_FreeSurface(surf);
    return tex ? e : NULL;
}

// Rysuje tekst; zwraca jego szerokość.
static int text(TTF_Font *f, SDL_Color c, int x, int y, const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text_entry *e = text_get(f, c, buf);
    if (!e) return 0;
    SDL_Rect dst = {x, y, e->w, e->h};
    SDL_RenderCopy(R, e->tex, NULL, &dst);
    return e->w;
}

static int text_width(TTF_Font *f, const char *s) {
    int w = 0, h = 0;
    TTF_SizeUTF8(f, s, &w, &h);
    return w;
}

// Skraca tekst (z poszanowaniem UTF-8) i dodaje "…", żeby zmieścił się w max_w.
static void fit(TTF_Font *f, const char *s, int max_w, char *out, size_t len) {
    snprintf(out, len, "%s", s);
    if (text_width(f, out) <= max_w) return;
    size_t n = strlen(out);
    while (n > 0) {
        do { n--; } while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);
        if (n + 4 > len) continue;
        memcpy(out + n, "\xE2\x80\xA6", 4);  // … i \0
        if (text_width(f, out) <= max_w) return;
    }
    out[0] = '\0';
}

static void fill(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(R, &rc);
}

static void outline(int x, int y, int w, int h, int t, SDL_Color c) {
    fill(x, y, w, t, c);
    fill(x, y + h - t, w, t, c);
    fill(x, y, t, h, c);
    fill(x + w - t, y, t, h, c);
}

// ---------------------------------------------------------------------------

int ui_init(SDL_Renderer *r) {
    R = r;
    if (TTF_Init() < 0) return -1;
    struct { TTF_Font **dst; const unsigned char *data; unsigned int len; int size; } fonts[] = {
        {&f_logo,  font_bold,    font_bold_len,    26},
        {&f_title, font_bold,    font_bold_len,    15},
        {&f_name,  font_regular, font_regular_len, 13},
        {&f_value, font_bold,    font_bold_len,    22},
        {&f_small, font_regular, font_regular_len, 11},
    };
    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        SDL_RWops *rw = SDL_RWFromConstMem(fonts[i].data, (int)fonts[i].len - 1);
        *fonts[i].dst = rw ? TTF_OpenFontRW(rw, 1, fonts[i].size) : NULL;
        if (!*fonts[i].dst) return -1;
    }
    return 0;
}

void ui_free(void) {
    for (int i = 0; i < CACHE_SIZE; i++)
        if (cache[i].tex) SDL_DestroyTexture(cache[i].tex);
    TTF_CloseFont(f_logo);
    TTF_CloseFont(f_title);
    TTF_CloseFont(f_name);
    TTF_CloseFont(f_value);
    TTF_CloseFont(f_small);
    TTF_Quit();
}

static void clock_str(char *out, size_t len) {
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) == 0)
        snprintf(out, len, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    else
        snprintf(out, len, "--:--:--");
}

static SDL_Color state_color(const ha_entity *e) {
    if (!e->ok) return RED;
    const char *s = e->state;
    if (!strcmp(s, "on") || !strcmp(s, "home") || !strcmp(s, "open") ||
        !strcmp(s, "playing") || !strcmp(s, "running"))
        return GREEN;
    if (!strcmp(s, "off") || !strcmp(s, "closed") || !strcmp(s, "not_home") ||
        !strcmp(s, "idle") || !strcmp(s, "standby"))
        return DIM;
    if (!strcmp(s, "unavailable") || !strcmp(s, "unknown")) return YELLOW;
    return FG;
}

// Nazwy typowych stanów w języku interfejsu; liczby i reszta bez zmian.
static const char *state_label(const char *s) {
    static const struct { const char *from, *to; } map[] = {
        {"on", "włączone"}, {"off", "wyłączone"}, {"open", "otwarte"}, {"closed", "zamknięte"},
        {"home", "w domu"}, {"not_home", "poza domem"}, {"unavailable", "niedostępne"},
        {"unknown", "nieznany"}, {"playing", "odtwarza"}, {"idle", "bezczynny"},
        {"standby", "czuwanie"}, {"running", "działa"},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (!strcmp(s, map[i].from)) return tr(map[i].to);
    return s;
}

// ---------------------------------------------------------------------------
// Ekran startowy: kroki uruchamiania i ewentualny błąd
// ---------------------------------------------------------------------------

static void draw_start(const app_state *s, unsigned int frame) {
    static const char *names[STEP_COUNT] = {
        "Konfiguracja", "Wi-Fi", "Generator losowy", "TLS", "Home Assistant"};

    int w = text_width(f_logo, ">_ psp-homelab");
    int x = (UI_W - w) / 2;
    x += text(f_logo, BLUE, x, 22, ">_");
    text(f_logo, FG, x, 22, " psp-homelab");

    for (int i = 0; i < STEP_COUNT; i++) {
        int y = 78 + i * 26;
        SDL_Color c = DIM;
        switch (s->steps[i]) {
            case STEP_ACTIVE: c = (frame / 15) % 2 ? YELLOW : BG2; break;
            case STEP_OK:     c = GREEN; break;
            case STEP_WARN:   c = YELLOW; break;
            case STEP_ERROR:  c = RED; break;
            default:          c = BG2; break;
        }
        fill(96, y + 4, 10, 10, c);
        text(f_name, s->steps[i] == STEP_PENDING ? DIM : FG, 116, y, "%s", tr(names[i]));
        if (s->step_detail[i][0]) {
            char d[96];
            fit(f_small, s->step_detail[i], UI_W - 250 - 16, d, sizeof(d));
            text(f_small, s->steps[i] == STEP_ERROR ? RED : GRAY, 250, y + 2, "%s", d);
        }
    }

    if (s->error[0]) {
        fill(0, 212, UI_W, UI_H - 212, BG_HARD);
        text(f_title, RED, 16, 216, "%s", s->error);
        // Podpowiedź może mieć kilka linii.
        char hint[256];
        snprintf(hint, sizeof(hint), "%s", s->hint);
        int y = 236;
        for (char *line = strtok(hint, "\n"); line && y < UI_H - 12; line = strtok(NULL, "\n"), y += 14)
            text(f_small, FG, 16, y, "%s", line);
    }
}

// ---------------------------------------------------------------------------
// Panel z kafelkami
// ---------------------------------------------------------------------------

#define TOP_H     26
#define BOTTOM_Y  240
#define TILE_W    228
#define TILE_H    64
#define TILE_GAP  6
#define TILE_X0   8
#define TILE_Y0   (TOP_H + 6)

static void draw_tile(const ha_entity *e, int x, int y, int selected) {
    fill(x, y, TILE_W, TILE_H, selected ? BG2 : BG1);
    SDL_Color sc = state_color(e);
    fill(x, y, 4, TILE_H, sc);
    if (selected) outline(x, y, TILE_W, TILE_H, 2, BLUE);

    char buf[96];
    fit(f_name, e->name[0] ? e->name : e->id, TILE_W - 22, buf, sizeof(buf));
    text(f_name, GRAY, x + 12, y + 6, "%s", buf);

    if (e->ok) {
        fit(f_value, state_label(e->state), TILE_W - 22 - (e->unit[0] ? 40 : 0), buf, sizeof(buf));
        int vw = text(f_value, sc, x + 12, y + 28, "%s", buf);
        if (e->unit[0]) text(f_name, GRAY, x + 12 + vw + 5, y + 36, "%s", e->unit);
    } else {
        text(f_title, RED, x + 12, y + 26, "%s", tr("błąd"));
        fit(f_small, e->error, TILE_W - 22, buf, sizeof(buf));
        text(f_small, GRAY, x + 12, y + 46, "%s", buf);
    }
}

static void draw_panel(const app_state *s, const ui_view *v) {
    int selected = v->selected;
    unsigned int frame = v->frame;
    // Górny pasek
    fill(0, 0, UI_W, TOP_H, BG_HARD);
    int x = 8;
    x += text(f_title, BLUE, x, 4, ">_");
    x += text(f_title, FG, x, 4, " psp-homelab");
    char host[96];
    fit(f_small, s->host, 200, host, sizeof(host));
    text(f_small, DIM, x + 10, 8, "%s", host);

    char now[16];
    clock_str(now, sizeof(now));
    int cw = text_width(f_title, now);
    text(f_title, FG, UI_W - 8 - cw, 4, "%s", now);

    int pages = (s->entity_count + UI_TILES_PER_PAGE - 1) / UI_TILES_PER_PAGE;
    int page = selected / UI_TILES_PER_PAGE;
    if (pages > 1) {
        char p[24];
        snprintf(p, sizeof(p), "%d/%d", page + 1, pages);
        text(f_small, GRAY, UI_W - 8 - cw - 10 - text_width(f_small, p), 8, "%s", p);
    }

    // Kafelki
    int first = page * UI_TILES_PER_PAGE;
    for (int i = 0; i < UI_TILES_PER_PAGE && first + i < s->entity_count; i++) {
        int col = i % 2, row = i / 2;
        draw_tile(&s->ents[first + i],
                  TILE_X0 + col * (TILE_W + 8),
                  TILE_Y0 + row * (TILE_H + TILE_GAP),
                  first + i == selected);
    }

    // Dolny pasek
    fill(0, BOTTOM_Y, UI_W, UI_H - BOTTOM_Y, BG_HARD);
    int y1 = BOTTOM_Y + 3, y2 = BOTTOM_Y + 17;

    if (s->refreshing) {
        static const char *dots[] = {"", ".", "..", "..."};
        text(f_small, YELLOW, 8, y1, tr("Odświeżanie%s"), dots[(frame / 10) % 4]);
    } else if (s->refresh_error[0]) {
        char e[96];
        fit(f_small, s->refresh_error, 300, e, sizeof(e));
        text(f_small, RED, 8, y1, tr("Błąd: %s"), e);
    } else if (s->list_error[0]) {
        char e[96];
        fit(f_small, s->list_error, 290, e, sizeof(e));
        text(f_small, YELLOW, 8, y1, tr("Lista: %s"), e);
    } else {
        text(f_small, GRAY, 8, y1, tr("Odświeżono %s · %u,%u s · co %d s"),
             s->last_refresh, s->last_ms / 1000, (s->last_ms % 1000) / 100, s->refresh_s);
    }
    if (selected < s->entity_count) {
        char id[96];
        fit(f_small, s->ents[selected].id, 160, id, sizeof(id));
        text(f_small, DIM, UI_W - 8 - text_width(f_small, id), y1, "%s", id);
    }

    x = 8;
    x += text(f_small, GRAY, x, y2, "%s · %s %u ms · RNG: ",
              s->tls_version[0] ? s->tls_version : "TLS",
              s->resumed ? tr("wznowiona") : tr("pełny"), s->handshake_ms);
    text(f_small, s->rng_hw ? AQUA : YELLOW, x, y2, "%s", s->rng_hw ? "KIRK" : tr("słaby"));

    const char *keys = tr("Δ wykres · X odśwież · START wyjście");
    text(f_small, DIM, UI_W - 8 - text_width(f_small, keys), y2, "%s", keys);
}

// ---------------------------------------------------------------------------
// Wykres historii
// ---------------------------------------------------------------------------

#define CH_X0 52
#define CH_X1 (UI_W - 12)
#define CH_Y0 (TOP_H + 42)
#define CH_Y1 (BOTTOM_Y - 20)

// Liczba w krótkiej postaci do osi: 21,5 / 1250 / 0,03.
static void fmt_num(float v, char *out, size_t len) {
    float a = v < 0 ? -v : v;
    if (a >= 100 || v == (int)v) snprintf(out, len, "%.0f", v);
    else if (a >= 1) snprintf(out, len, "%.1f", v);
    else snprintf(out, len, "%.2f", v);
    for (char *p = out; *p; p++)
        if (*p == '.') *p = i18n_decimal();
}

static void draw_chart(const app_state *s, const ui_view *v) {
    const chart_data *c = &s->chart;
    const ha_entity *e = v->selected < s->entity_count ? &s->ents[v->selected] : NULL;

    // Górny pasek: nazwa encji i zegar
    fill(0, 0, UI_W, TOP_H, BG_HARD);
    char now[16], buf[96];
    clock_str(now, sizeof(now));
    int cw = text_width(f_title, now);
    text(f_title, FG, UI_W - 8 - cw, 4, "%s", now);
    int x = 8 + text(f_title, BLUE, 8, 4, "~ ");
    fit(f_title, e ? (e->name[0] ? e->name : e->id) : c->entity, UI_W - 30 - cw - x, buf, sizeof(buf));
    text(f_title, FG, x, 4, "%s", buf);

    // Zakresy i bieżąca wartość
    x = 8;
    for (int i = 0; i < UI_CHART_RANGES; i++) {
        char r[16];
        snprintf(r, sizeof(r), "%d h", ui_chart_hours[i]);
        int w = text_width(f_small, r) + 14;
        int on = i == v->chart_range;
        fill(x, TOP_H + 8, w, 18, on ? BLUE : BG1);
        text(f_small, on ? BG_HARD : GRAY, x + 7, TOP_H + 10, "%s", r);
        x += w + 4;
    }
    if (e && e->ok) {
        char val[64];
        snprintf(val, sizeof(val), "%s%s%s", state_label(e->state), e->unit[0] ? " " : "", e->unit);
        fit(f_title, val, 180, buf, sizeof(buf));
        text(f_title, state_color(e), UI_W - 12 - text_width(f_title, buf), TOP_H + 7, "%s", buf);
    }

    // Ramka i siatka
    fill(CH_X0, CH_Y0, CH_X1 - CH_X0, CH_Y1 - CH_Y0, BG_HARD);
    for (int i = 1; i < 4; i++) fill(CH_X0, CH_Y0 + (CH_Y1 - CH_Y0) * i / 4, CH_X1 - CH_X0, 1, BG1);
    for (int i = 1; i < 6; i++) fill(CH_X0 + (CH_X1 - CH_X0) * i / 6, CH_Y0, 1, CH_Y1 - CH_Y0, BG1);

    // Oś czasu: -N h ... teraz
    for (int i = 0; i <= 2; i++) {
        char t[24];
        int h = c->hours ? c->hours : ui_chart_hours[v->chart_range];
        if (i == 2) snprintf(t, sizeof(t), "%s", tr("teraz"));
        else if (i == 1) snprintf(t, sizeof(t), h % 2 ? tr("-%d,5 h") : "-%d h", h / 2);
        else snprintf(t, sizeof(t), "-%d h", h);
        int tw = text_width(f_small, t);
        int tx = CH_X0 + (CH_X1 - CH_X0) * i / 2 - (i == 0 ? 0 : i == 2 ? tw : tw / 2);
        text(f_small, DIM, tx, CH_Y1 + 3, "%s", t);
    }

    if (c->status == CHART_LOADING || c->status == CHART_NONE) {
        static const char *dots[] = {"", ".", "..", "..."};
        text(f_name, YELLOW, CH_X0 + 16, (CH_Y0 + CH_Y1) / 2 - 8, tr("Wczytywanie historii%s"),
             dots[(v->frame / 10) % 4]);
    } else if (c->status == CHART_ERROR) {
        fit(f_name, c->error, CH_X1 - CH_X0 - 32, buf, sizeof(buf));
        text(f_name, RED, CH_X0 + 16, (CH_Y0 + CH_Y1) / 2 - 8, "%s", buf);
    } else {
        float lo = c->min, hi = c->max;
        if (c->binary) { lo = 0; hi = 1; }
        else {
            float pad = (hi - lo) * 0.1f;
            if (pad < 0.05f) pad = hi == 0 ? 1 : (hi < 0 ? -hi : hi) * 0.05f + 0.05f;
            lo -= pad;
            hi += pad;
        }
        int ph = CH_Y1 - CH_Y0 - 8;  // 4 px marginesu u góry i u dołu
        #define VY(val) (CH_Y1 - 4 - (int)(((val) - lo) / (hi - lo) * ph))

        // Etykiety osi Y
        char a[24], b[24];
        if (c->binary) { snprintf(a, sizeof(a), "%s", tr("wł.")); snprintf(b, sizeof(b), "%s", tr("wył.")); }
        else { fmt_num(c->max, a, sizeof(a)); fmt_num(c->min, b, sizeof(b)); }
        text(f_small, GRAY, CH_X0 - 6 - text_width(f_small, a), VY(c->binary ? 1 : c->max) - 7, "%s", a);
        text(f_small, GRAY, CH_X0 - 6 - text_width(f_small, b), VY(c->binary ? 0 : c->min) - 7, "%s", b);
        if (e && e->unit[0] && !c->binary) text(f_small, DIM, 6, CH_Y0 - 2, "%s", e->unit);

        // Wypełnienie pod linią i sama linia
        SDL_Color lc = c->binary ? GREEN : AQUA;
        int w = CH_X1 - CH_X0;
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(R, lc.r, lc.g, lc.b, 48);
        for (int px = 0; px < w; px++) {
            int i = px * CHART_BUCKETS / w;
            if (!c->has[i]) continue;
            SDL_RenderDrawLine(R, CH_X0 + px, VY(c->v[i]), CH_X0 + px, CH_Y1 - 1);
        }
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(R, lc.r, lc.g, lc.b, 255);
        int prev_y = -1;
        for (int px = 0; px < w; px++) {
            int i = px * CHART_BUCKETS / w;
            if (!c->has[i]) { prev_y = -1; continue; }
            int y = VY(c->v[i]);
            if (prev_y >= 0) {
                // Poziomo do bieżącej kolumny, potem pionowo do nowej wartości.
                // (Stroma ukośna linia rozkłada się w SDL na dwie kolumny i wygląda
                // jak stan pośredni.) Linia ma 2 px grubości.
                SDL_RenderDrawLine(R, CH_X0 + px - 1, prev_y, CH_X0 + px, prev_y);
                SDL_RenderDrawLine(R, CH_X0 + px - 1, prev_y + 1, CH_X0 + px, prev_y + 1);
                if (y != prev_y) SDL_RenderDrawLine(R, CH_X0 + px, prev_y, CH_X0 + px, y + 1);
            }
            prev_y = y;
        }
        #undef VY
    }

    // Dolny pasek
    fill(0, BOTTOM_Y, UI_W, UI_H - BOTTOM_Y, BG_HARD);
    int y1 = BOTTOM_Y + 3, y2 = BOTTOM_Y + 17;
    if (c->status == CHART_OK) {
        char mn[24], mx[24];
        fmt_num(c->min, mn, sizeof(mn));
        fmt_num(c->max, mx, sizeof(mx));
        if (c->binary) text(f_small, GRAY, 8, y1, tr("%d zmian stanu · %d h"), c->samples, c->hours);
        else text(f_small, GRAY, 8, y1, tr("min %s · max %s · %d próbek"), mn, mx, c->samples);
    }
    if (e) {
        char id[96];
        fit(f_small, e->id, 200, id, sizeof(id));
        text(f_small, DIM, UI_W - 8 - text_width(f_small, id), y1, "%s", id);
    }
    const char *keys = tr("L/R zakres · ‹ › encja · Δ/O powrót");
    text(f_small, DIM, UI_W - 8 - text_width(f_small, keys), y2, "%s", keys);
}

void ui_draw(const app_state *s, const ui_view *v) {
    SDL_SetRenderDrawColor(R, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(R);
    if (s->running && v->chart_open) draw_chart(s, v);
    else if (s->running) draw_panel(s, v);
    else draw_start(s, v->frame);

    // Tryb nocny bez sterowania podświetleniem: przyciemnij sam obraz.
    if (v->dim > 0) {
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(R, 0, 0, 0, (Uint8)(v->dim * 255 / 100));
        SDL_RenderFillRect(R, NULL);
        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    }
    SDL_RenderPresent(R);
}

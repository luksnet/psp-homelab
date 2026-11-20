#include "i18n.h"

#include <string.h>

static ui_lang current = LANG_EN;

// Polski tekst (dokładnie jak w kodzie) -> angielski.
// Wzorce printf muszą mieć te same specyfikatory w tej samej kolejności
// (sprawdza to tools/test/test_logic.c).
static const struct { const char *pl, *en; } table[] = {
    // ui.c: stany encji
    {"włączone", "on"}, {"wyłączone", "off"}, {"otwarte", "open"}, {"zamknięte", "closed"},
    {"w domu", "home"}, {"poza domem", "away"}, {"niedostępne", "unavailable"},
    {"nieznany", "unknown"}, {"odtwarza", "playing"}, {"bezczynny", "idle"},
    {"czuwanie", "standby"}, {"działa", "running"},
    // ui.c: ekran startowy
    {"Konfiguracja", "Configuration"},
    {"Generator losowy", "Random generator"},
    // ui.c: panel
    {"błąd", "error"},
    {"Odświeżanie%s", "Refreshing%s"},
    {"Błąd: %s", "Error: %s"},
    {"Lista: %s", "List: %s"},
    {"Odświeżono %s · %u,%u s · co %d s", "Refreshed %s · %u.%u s · every %d s"},
    {"wznowiona", "resumed"},
    {"pełny", "full"},
    {"słaby", "weak"},
    {"Δ wykres · X odśwież · START wyjście", "Δ chart · X refresh · START quit"},
    // ui.c: wykres
    {"teraz", "now"},
    {"-%d,5 h", "-%d.5 h"},
    {"Wczytywanie historii%s", "Loading history%s"},
    {"wł.", "on"},
    {"wył.", "off"},
    {"%d zmian stanu · %d h", "%d state changes · %d h"},
    {"min %s · max %s · %d próbek", "min %s · max %s · %d samples"},
    {"L/R zakres · ‹ › encja · Δ/O powrót", "L/R range · ‹ › entity · Δ/O back"},
    // worker.c
    {"Błąd konfiguracji", "Configuration error"},
    {"Skopiuj config.example.pl.ini jako config.ini\n"
     "obok EBOOT.PBP i uzupełnij host, token i encje.",
     "Copy config.example.ini to config.ini\n"
     "next to EBOOT.PBP and fill in host, token and entities."},
    {"%d encji + %s, co %d s", "%d entities + %s, every %d s"},
    {"%d encji, odświeżanie co %d s", "%d entities, refresh every %d s"},
    {"profil %d", "profile %d"},
    {"kod 0x%08X", "code 0x%08X"},
    {"Brak połączenia Wi-Fi", "No Wi-Fi connection"},
    {"PPSSPP: Ustawienia > Sieć > włącz sieć.\n"
     "PSP: zapisz sieć w Ustawienia > Ustawienia sieci.",
     "PPSSPP: Settings > Networking > enable networking.\n"
     "PSP: save a network in Settings > Network Settings."},
    {"wczytywanie CA", "loading CA"},
    {"Błąd TLS", "TLS error"},
    {"Sprawdź plik CA wskazany w config.ini (klucz ca).",
     "Check the CA file set in config.ini (key ca)."},
    {"konfiguracja klienta", "client setup"},
    {"lista encji: %s", "entity list: %s"},
    {"lista encji jest pusta", "entity list is empty"},
    {"Brak encji do wyświetlenia", "No entities to show"},
    {"Nadaj encjom etykietę (np. psp) w HA albo dodaj je\n"
     "do grupy; możesz też wpisać linie entity w config.ini.",
     "Give entities a label (e.g. psp) in HA or add them\n"
     "to a group; you can also add entity lines to config.ini."},
    {"Nie można połączyć z Home Assistant", "Cannot connect to Home Assistant"},
    {"Sprawdź host i port w config.ini oraz to,\n"
     "czy certyfikat pasuje do nazwy hosta.",
     "Check host and port in config.ini and whether\n"
     "the certificate matches the host name."},
    {"nie sprawdzono", "not checked"},
    // config.c
    {"linia %d: brak '='", "line %d: missing '='"},
    {"linia %d: night = GG:MM-GG:MM", "line %d: night = HH:MM-HH:MM"},
    {"linia %d: entities_from = label:NAZWA albo encja grupy, np. light.psp",
     "line %d: entities_from = label:NAME or a group entity, e.g. light.psp"},
    {"linia %d: lang = pl albo en", "line %d: lang = pl or en"},
    {"brak 'host' w config.ini", "missing 'host' in config.ini"},
    {"brak 'token' w config.ini", "missing 'token' in config.ini"},
    {"brak 'entity' ani 'entities_from' w config.ini",
     "missing 'entity' or 'entities_from' in config.ini"},
    // ha.c
    {"token odrzucony (401)", "token rejected (401)"},
    {"brak encji (404)", "no such entity (404)"},
    {"zła odpowiedź JSON", "bad JSON response"},
    {"%.40s: zły identyfikator encji", "%.40s: invalid entity id"},
    {"etykieta: tylko a-z, 0-9 i _", "label: only a-z, 0-9 and _"},
    {"%s: 401 (szablony wymagają admina, użyj group.)", "%s: 401 (templates need admin, use a group)"},
    {"%s: token odrzucony (401)", "%s: token rejected (401)"},
    {"%s: brak (404)", "%s: not found (404)"},
    {"%s: to nie jest grupa", "%s: not a group"},
    {"zły identyfikator encji", "invalid entity id"},
    {"brak danych liczbowych w tym zakresie", "no numeric data in this range"},
    // https.c
    {"źródło KIRK", "KIRK source"},
    {"generator losowy", "random generator"},
    {"plik CA", "CA file"},
    {"wbudowane CA", "built-in CA"},
    {"wbudowane publiczne CA", "built-in public CAs"},
    {"brak", "none"},
    {"połączenie TCP", "TCP connection"},
    {"certyfikat odrzucony: %s", "certificate rejected: %s"},
    {"handshake TLS", "TLS handshake"},
    {"niepoprawna odpowiedź HTTP", "invalid HTTP response"},
    {"TLS nie został zainicjalizowany", "TLS not initialised"},
    {"za długie nagłówki", "headers too long"},
    {"wysyłanie", "sending"},
    {"błąd odbioru odpowiedzi", "error receiving response"},
    {"połączenie zerwane", "connection lost"},
    // rng.c
    {"SŁABY: brak kirkrng.prx (0x%08X)", "WEAK: no kirkrng.prx (0x%08X)"},
    {"SŁABY: KIRK błąd %d/%d", "WEAK: KIRK error %d/%d"},
    {"SŁABY: KIRK zwraca stałe dane", "WEAK: KIRK returns constant data"},
    {"KIRK (sprzętowy)", "KIRK (hardware)"},
};

#define TABLE_N ((int)(sizeof(table) / sizeof(table[0])))

void    i18n_set(ui_lang lang) { current = lang; }
ui_lang i18n_get(void) { return current; }
char    i18n_decimal(void) { return current == LANG_EN ? '.' : ','; }

const char *tr(const char *pl) {
    if (current == LANG_PL || !pl) return pl;
    for (int i = 0; i < TABLE_N; i++)
        if (!strcmp(table[i].pl, pl)) return table[i].en;
    return pl;
}

int         i18n_count(void) { return TABLE_N; }
const char *i18n_pl(int i) { return i >= 0 && i < TABLE_N ? table[i].pl : NULL; }
const char *i18n_en(int i) { return i >= 0 && i < TABLE_N ? table[i].en : NULL; }

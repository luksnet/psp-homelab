#pragma once
// Język interfejsu: polski (domyślnie) albo angielski, klucz lang w config.ini.
// Teksty w kodzie są po polsku; tr() zwraca ich odpowiednik w bieżącym języku.

typedef enum { LANG_PL = 0, LANG_EN = 1 } ui_lang;

void    i18n_set(ui_lang lang);
ui_lang i18n_get(void);

// Tekst w bieżącym języku; brak tłumaczenia = tekst polski.
// format_arg: kompilator sprawdza argumenty printf względem polskiego wzorca.
const char *tr(const char *pl) __attribute__((format_arg(1)));

// Separator dziesiętny: ',' po polsku, '.' po angielsku.
char i18n_decimal(void);

// Do testów: dostęp do tabeli tłumaczeń.
int         i18n_count(void);
const char *i18n_pl(int i);
const char *i18n_en(int i);

#!/usr/bin/env bash
# Wgrywa psp-homelab na PSP podłączone przez USB (tryb "Połączenie USB" w XMB).
#
# Użycie:
#   tools/deploy.sh                 # znajdź PSP w /run/media/$USER, skopiuj program
#   tools/deploy.sh --config        # skopiuj też config.ini i ca.pem (nadpisze te na PSP)
#   tools/deploy.sh --eject         # po skopiowaniu odmontuj kartę
#   tools/deploy.sh --target DIR    # katalog główny karty (np. /mnt/psp)
#   tools/deploy.sh --no-build      # nie przebudowuj przed kopiowaniem
#
# Bez --config pliki config.ini i ca.pem są kopiowane tylko wtedy,
# gdy jeszcze ich nie ma na PSP — żeby nie nadpisać ustawień z karty.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
target="" config=0 eject=0 build=1

while [ $# -gt 0 ]; do
    case "$1" in
        --target) target="$2"; shift 2 ;;
        --config) config=1; shift ;;
        --eject) eject=1; shift ;;
        --no-build) build=0; shift ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Nieznana opcja: $1" >&2; exit 2 ;;
    esac
done

if [ "$build" = 1 ]; then
    "$root/build.sh" >/dev/null
fi
for f in EBOOT.PBP kirkrng.prx backlight.prx; do
    [ -f "$root/build/$f" ] || { echo "Brak build/$f — uruchom ./build.sh" >&2; exit 1; }
done

# Karta PSP to katalog z podkatalogiem PSP/ (PSP/GAME, PSP/SAVEDATA...).
if [ -z "$target" ]; then
    found=()
    for d in /run/media/"$USER"/* /media/"$USER"/* /media/*; do
        [ -d "$d/PSP" ] && found+=("$d")
    done
    if [ ${#found[@]} -eq 0 ]; then
        echo "Nie znaleziono PSP. Na PSP: Ustawienia > Połączenie USB, potem spróbuj ponownie" >&2
        echo "albo podaj katalog karty: tools/deploy.sh --target /ścieżka" >&2
        exit 1
    fi
    if [ ${#found[@]} -gt 1 ]; then
        echo "Znaleziono kilka kart: ${found[*]} — wybierz --target" >&2
        exit 1
    fi
    target="${found[0]}"
fi
[ -d "$target/PSP" ] || { echo "$target nie wygląda na kartę PSP (brak katalogu PSP/)" >&2; exit 1; }

dest="$target/PSP/GAME/psp-homelab"
mkdir -p "$dest"
echo "Karta: $target"

copy() {  # copy ŹRÓDŁO NAZWA
    cp "$1" "$dest/$2"
    printf '  %-14s %8s B\n' "$2" "$(stat -c %s "$1")"
}

copy "$root/build/EBOOT.PBP" EBOOT.PBP
copy "$root/build/kirkrng.prx" kirkrng.prx
copy "$root/build/backlight.prx" backlight.prx

for f in config.ini ca.pem; do
    src="$root/$f"
    [ -f "$src" ] || continue
    if [ "$config" = 1 ] || [ ! -f "$dest/$f" ]; then
        copy "$src" "$f"
    else
        echo "  $f — już jest na PSP, pomijam (użyj --config, żeby nadpisać)"
    fi
done
[ -f "$dest/config.ini" ] || echo "Uwaga: na PSP nie ma config.ini — skopiuj config.example.ini jako config.ini i uzupełnij" >&2

echo "Zapisywanie na kartę…"
sync

if [ "$eject" = 1 ]; then
    dev="$(findmnt -n -o SOURCE --target "$target" || true)"
    if [ -n "$dev" ] && command -v udisksctl >/dev/null; then
        udisksctl unmount -b "$dev" && echo "Odmontowano $dev — można odłączyć kabel"
    else
        echo "Nie udało się ustalić urządzenia; odmontuj ręcznie" >&2
    fi
fi
echo "Gotowe: $dest"

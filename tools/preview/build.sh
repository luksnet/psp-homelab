#!/usr/bin/env bash
# Buduje i uruchamia podgląd GUI na komputerze; zapisuje zrzuty PNG.
# Użycie: tools/preview/build.sh [katalog_wyjściowy]   (domyślnie build/preview)
# Wymaga: sdl2, sdl2_ttf (pacman), pspdev (dla nagłówków mbedTLS), opcjonalnie magick.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$root/build/preview}"
work="$root/build/preview-host"
mkdir -p "$out" "$work/shim"
PSPDEV="${PSPDEV:-$HOME/pspdev}"

# Nagłówki mbedTLS bierzemy z pspdev (ui.c ich nie używa, ale dołącza przez https.h).
ln -sfn "$PSPDEV/psp/include/mbedtls" "$work/shim/mbedtls"

# Czcionki osadzane tak samo jak na PSP.
for f in regular:NotoSans-Regular bold:NotoSans-Bold; do
    name="font_${f%%:*}"
    cmake -DIN="$root/assets/fonts/${f#*:}.ttf" -DOUT="$work/$name.c" -DNAME="$name" \
          -P "$root/cmake/embed.cmake"
done

cc -std=gnu11 -O1 -Wall -Wno-unused-parameter \
   -I"$root/tools/preview" -I"$work/shim" -I"$root/src" \
   "$root/tools/preview/preview.c" "$root/src/ui.c" "$root/src/chart.c" "$root/src/timeutil.c" \
   "$work/font_regular.c" "$work/font_bold.c" \
   $(pkg-config --cflags --libs sdl2 SDL2_ttf) -lm -o "$work/preview"

"$work/preview" "$out"
if command -v magick >/dev/null; then
    for b in "$out"/*.bmp; do magick "$b" "${b%.bmp}.png" && rm "$b"; done
fi
ls "$out"

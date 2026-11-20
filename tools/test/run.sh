#!/usr/bin/env bash
# Testy czystej logiki psp-homelab na komputerze (bez PSP i emulatora).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/test-host"
mkdir -p "$out"
cc -std=gnu11 -O1 -Wall -Wextra -I"$root/src" \
   "$root/tools/test/test_logic.c" "$root/src/timeutil.c" "$root/src/chart.c" "$root/src/night.c" \
   -lm -o "$out/test_logic"
"$out/test_logic"

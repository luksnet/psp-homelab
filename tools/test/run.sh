#!/usr/bin/env bash
# Testy czystej logiki psp-homelab na komputerze (bez PSP i emulatora).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build/test-host"
mkdir -p "$out"
cc -std=gnu11 -O1 -Wall -Wextra -I"$root/src" \
   "$root/tools/test/test_logic.c" "$root/src/timeutil.c" "$root/src/chart.c" "$root/src/night.c" "$root/src/i18n.c" \
   -lm -o "$out/test_logic"
"$out/test_logic"

# Każdy tekst w tr("...") w src/ musi mieć tłumaczenie w src/i18n.c.
python3 - "$root" <<'PY'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
def lits(text):
    # sklej sąsiednie literały "a" "b" w jeden
    out = []
    for m in re.finditer(r'tr\(((?:\s*"(?:[^"\\]|\\.)*")+)\s*\)', text):
        parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
        out.append("".join(parts))
    return out
table = (root / "src/i18n.c").read_text()
keys = set()
for m in re.finditer(r'\{((?:\s*"(?:[^"\\]|\\.)*")+)\s*,', table):
    keys.add("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))))
missing = []
for f in sorted((root / "src").glob("*.c")):
    if f.name == "i18n.c": continue
    for s in lits(f.read_text()):
        if s not in keys: missing.append(f"{f.name}: {s}")
# Napisy przekazywane przez zmienną (names[], map[].to) sprawdzamy jawnie.
for s in ["Konfiguracja", "Generator losowy", "włączone", "poza domem", "działa"]:
    if s not in keys: missing.append(f"(tablica) {s}")
if missing:
    print("i18n: brak tłumaczenia:"); [print("  " + m) for m in missing]; sys.exit(1)
print(f"i18n: {len(keys)} tekstów, wszystkie wywołania tr() przetłumaczone")
PY

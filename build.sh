#!/usr/bin/env bash
# Buduje EBOOT.PBP w katalogu build/
set -euo pipefail
export PSPDEV="${PSPDEV:-$HOME/pspdev}"
export PATH="$PSPDEV/bin:$PATH"
cd "$(dirname "$0")"
psp-cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
echo
echo "Gotowe: $(pwd)/build/EBOOT.PBP"

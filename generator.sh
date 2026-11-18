#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export PATH="/opt/devkitpro/devkitARM/bin:$PATH"

echo "=== [1/4] Building Medabots: Rokusho decomp ROM ==="
make -C decomp -j"$(nproc)"

echo "=== [2/4] Extracting symbols and generating configs ==="
python3 tools/extract_map_symbols.py

echo "=== [3/4] Ensuring host gba_recompile is built ==="
if [ ! -f "build_host/gba_recompile" ]; then
    cmake -B build_host -S gbarecomp -DCMAKE_BUILD_TYPE=Release
    cmake --build build_host --target gba_recompile -j"$(nproc)"
fi

echo "=== [4/4] Running gba_recompile ==="
mkdir -p generated
./build_host/gba_recompile \
    --rom decomp/medabots_rokusho.gba \
    --config game.toml \
    --symbols symbols/imported_symbols.tsv \
    --data-symbols symbols/imported_data_symbols.tsv \
    --out generated \
    --max-functions 65536

echo "Recompilation complete. Generated shards are in generated/."

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export PATH="/opt/devkitpro/devkitARM/bin:$PATH"

echo "=== [1/5] Building Medabots: Rokusho decomp ROM ==="
if [ -d "decomp" ]; then
    make -C decomp -j"$(nproc)"
fi

echo "=== [2/5] Building Medabots: Metabee decomp ROM ==="
if [ -d "decomp_metabee" ]; then
    (cd decomp_metabee && python3 tools/build.py)
fi

echo "=== [3/5] Extracting symbols and generating configs ==="
python3 tools/extract_map_symbols.py all

echo "=== [4/5] Ensuring host gba_recompile is built ==="
if [ -f "tools/gba_recompile.patch" ] && [ -d "gbarecomp" ]; then
    (cd gbarecomp && git apply --check ../tools/gba_recompile.patch >/dev/null 2>&1 && git apply ../tools/gba_recompile.patch || true)
fi
if [ ! -f "build_host/gba_recompile" ]; then
    cmake -B build_host -S gbarecomp -DCMAKE_BUILD_TYPE=Release
    cmake --build build_host --target gba_recompile -j"$(nproc)"
fi

echo "=== [5/5] Running gba_recompile for Rokusho & Metabee ==="
mkdir -p generated

if [ -f "decomp/medabots_rokusho.gba" ]; then
    echo "--- Recompiling Rokusho ---"
    ./build_host/gba_recompile \
        --rom decomp/medabots_rokusho.gba \
        --config game.toml \
        --symbols symbols/imported_symbols.tsv \
        --data-symbols symbols/imported_data_symbols.tsv \
        --out generated \
        --max-functions 65536
fi

if [ -f "decomp_metabee/build/candidate/medabots.gba" ]; then
    echo "--- Recompiling Metabee ---"
    ./build_host/gba_recompile \
        --rom decomp_metabee/build/candidate/medabots.gba \
        --config game_metabee.toml \
        --symbols symbols/metabee_symbols.tsv \
        --data-symbols symbols/metabee_data_symbols.tsv \
        --output-prefix metabee_ \
        --out generated \
        --max-functions 65536
elif [ -f "decomp_metabee/build/medabots.gba" ]; then
    echo "--- Recompiling Metabee ---"
    ./build_host/gba_recompile \
        --rom decomp_metabee/build/medabots.gba \
        --config game_metabee.toml \
        --symbols symbols/metabee_symbols.tsv \
        --data-symbols symbols/metabee_data_symbols.tsv \
        --output-prefix metabee_ \
        --out generated \
        --max-functions 65536
fi

echo "Recompilation complete. Generated shards are in generated/."

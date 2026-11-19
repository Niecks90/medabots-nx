#!/usr/bin/env bash
set -e

export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITARM/bin:$PATH

mkdir -p build
cd build

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE="-O3 -mcpu=cortex-a57+crypto -mtune=cortex-a57" \
  -DGBARECOMP_SCANLINE_IMPLEMENTATION=HLE

copy_if_changed() {
    if [ -f "$1" ] && [ -d "$2" ]; then
        dest="$2/$(basename "$1")"
        if [ ! -f "$dest" ] || ! cmp -s "$1" "$dest"; then
            cp -f "$1" "$dest"
        fi
    fi
}

copy_if_changed ../switch/host_window.h ../gbarecomp/src/runtime
copy_if_changed ../switch/host_window.cpp ../gbarecomp/src/runtime
copy_if_changed ../switch/host_platform.cpp ../gbarecomp/src/runtime
copy_if_changed ../switch/runtime_arm.cpp ../gbarecomp/src/armv4t
copy_if_changed ../switch/runtime_arm.h ../gbarecomp/src/armv4t
copy_if_changed ../switch/medabots_symbols.h ../gbarecomp/src/runtime

make -j$(nproc)

if [ -f ../decomp/medabots_rokusho.gba ]; then
    cp -f ../decomp/medabots_rokusho.gba rom.gba
fi

echo "=== Build finished: medabots_nx.nro ==="
ls -lh medabots_nx.nro rom.gba

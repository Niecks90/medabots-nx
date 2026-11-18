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

if [ -f ../switch/host_window.h ] && [ -d ../gbarecomp/src/runtime ]; then
    cp -f ../switch/host_window.h ../gbarecomp/src/runtime/host_window.h
fi
if [ -f ../switch/host_window.cpp ] && [ -d ../gbarecomp/src/runtime ]; then
    cp -f ../switch/host_window.cpp ../gbarecomp/src/runtime/host_window.cpp
fi
if [ -f ../switch/host_platform.cpp ] && [ -d ../gbarecomp/src/runtime ]; then
    cp -f ../switch/host_platform.cpp ../gbarecomp/src/runtime/host_platform.cpp
fi
if [ -f ../switch/medabots_symbols.h ] && [ -d ../gbarecomp/src/runtime ]; then
    cp -f ../switch/medabots_symbols.h ../gbarecomp/src/runtime/medabots_symbols.h
fi

make -j$(nproc)

if [ -f ../decomp/medabots_rokusho.gba ]; then
    cp -f ../decomp/medabots_rokusho.gba rom.gba
fi

echo "=== Build finished: medabots_nx.nro ==="
ls -lh medabots_nx.nro rom.gba

cmake_minimum_required(VERSION 3.20)
project(MedabotsRokushoRecomp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(ANDROID)
    set(_netplay_default OFF)
else()
    set(_netplay_default ON)
endif()
option(GBARECOMP_NETPLAY "Build desktop link-cable netplay" ${_netplay_default})

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
    CACHE PATH "Path to this game's gbarecomp worktree")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui"
    CACHE PATH "Path to the shared recomp-ui checkout")
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}"
    CACHE PATH "recomp-ui root used by the live GBA runtime adapter")
if (CMAKE_SYSTEM_NAME STREQUAL "Switch" OR __SWITCH__ OR SWITCH)
    set(GBARECOMP_RUNTIME_UI_ROOT "" CACHE PATH "" FORCE)
    set(RECOMP_UI_ROOT "" CACHE PATH "" FORCE)
endif()
set(GBARECOMP_SELFHEAL_RECOMPILE_DEFAULT ON CACHE BOOL
    "Enable runtime self-heal sharding by default" FORCE)
set(GBARECOMP_ENABLE_MODS ON CACHE BOOL
    "Enable mod catalog and GBA mod packages" FORCE)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/CMakeLists.txt")
    message(STATUS "gbarecomp checkout not found. Cloning gbarecomp recursively...")
    execute_process(
        COMMAND git clone --depth 1 --recurse-submodules https://github.com/mstan/gbarecomp.git "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp"
        RESULT_VARIABLE GBARECOMP_CLONE_RES
    )
    if(NOT GBARECOMP_CLONE_RES EQUAL 0)
        message(FATAL_ERROR "Failed to clone gbarecomp repository")
    endif()
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/external/arm-recomp-core/cmake/ArmRecompCore.cmake")
    message(STATUS "Updating gbarecomp submodules...")
    execute_process(
        COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp" submodule update --init --depth 1 --recursive
    )
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/switch/host_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/src/runtime/host_window.cpp"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/switch/overlay_compile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/src/runtime/overlay_compile.cpp"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/switch/gba_ppu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/src/gba/gba_ppu.cpp"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/switch/host_platform.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/src/runtime/host_platform.cpp"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/switch/medabots_symbols.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/src/runtime/medabots_symbols.h"
    COPYONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/switch/host_window.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp/src/runtime/host_window.h"
    COPYONLY
)

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp" CACHE PATH "Path to gbarecomp")

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "GBARECOMP_ROOT is not a gbarecomp checkout:\n"
        "  ${GBARECOMP_ROOT}\n"
        "Configure with -DGBARECOMP_ROOT=<path> if the local worktree link is absent.")
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Switch" OR __SWITCH__ OR SWITCH)
    remove_definitions(-DGBARECOMP_RUNTIME_UI)
    set(GBARECOMP_RUNTIME_UI_ROOT "" CACHE PATH "" FORCE)
    set(GBARECOMP_RUNTIME_UI OFF CACHE BOOL "" FORCE)
    set(RECOMP_UI_ROOT "" CACHE PATH "" FORCE)
    add_compile_definitions(SWITCH)
    add_definitions(-U GBARECOMP_RUNTIME_UI)
endif()
set(GBARECOMP_NETPLAY OFF)
set(GBAGAME_RECOMP_UI OFF)

if (CMAKE_SYSTEM_NAME STREQUAL "Switch" OR __SWITCH__ OR SWITCH)
    set(GBARECOMP_RUNTIME_UI_ROOT "" CACHE PATH "" FORCE)
endif()
set(GBARECOMP_SCANLINE_IMPLEMENTATION "HLE" CACHE STRING "" FORCE)
add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

set(GBA_GAME_GENERATED_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH
    "Generated native corpus (may point at an independent qualification build)")
if(EXISTS "${GBA_GAME_GENERATED_ROOT}/recompiled.cpp")
    message(FATAL_ERROR
        "Stale monolithic generated/recompiled.cpp found. "
        "Run generator.sh with the current gbarecomp before building.")
endif()
file(GLOB GAME_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${GBA_GAME_GENERATED_ROOT}/*.c"
    "${GBA_GAME_GENERATED_ROOT}/*.cpp")

add_executable(MedabotsRokushoRecomp
    src/main.cpp
)

file(GLOB GENERATED_GAME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp")
message(STATUS "Building with generated game sources")
target_sources(MedabotsRokushoRecomp PRIVATE ${GENERATED_GAME_SOURCES})

target_include_directories(MedabotsRokushoRecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/generated")

# Keep every generated guest shard cheaper to compile than the runtime. The
# dispatch table and symbol map remain ordinary small generated units.
set(GAME_RECOMPILED_SOURCES ${GAME_GENERATED_SOURCES})
list(FILTER GAME_RECOMPILED_SOURCES INCLUDE REGEX
    "/recompiled_[0-9]+\\.cpp$")
set(MEDABOTS_GENERATED_OPTIMIZATION "O2" CACHE STRING "Generated CPU optimization: O1 or O2")
set_property(CACHE MEDABOTS_GENERATED_OPTIMIZATION PROPERTY STRINGS O1 O2)
if(NOT MEDABOTS_GENERATED_OPTIMIZATION MATCHES "^(O1|O2)$")
    message(FATAL_ERROR "MEDABOTS_GENERATED_OPTIMIZATION must be O1 or O2")
endif()
if(GAME_RECOMPILED_SOURCES)
    set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-${MEDABOTS_GENERATED_OPTIMIZATION};-g0")
endif()

target_include_directories(MedabotsRokushoRecomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${GBARECOMP_ROOT}/src/armv4t"
    "${GBARECOMP_ROOT}/src/gba"
    "${GBARECOMP_ROOT}/src/runtime"
    "${GBARECOMP_ROOT}/src/debug")

if(NINTENDO_SWITCH OR __SWITCH__ OR SWITCH)
    include_directories(
        /opt/devkitpro/portlibs/switch/include
        /opt/devkitpro/portlibs/switch/include/SDL2
        $ENV{DEVKITPRO}/portlibs/switch/include
        $ENV{DEVKITPRO}/portlibs/switch/include/SDL2
    )
endif()

target_link_libraries(MedabotsRokushoRecomp PRIVATE
    "-Wl,--start-group"
    gbarecomp_runtime
    gbarecomp_debug
    gbarecomp_gba
    gbarecomp_armv4t
    "-Wl,--end-group")

if(NINTENDO_SWITCH OR __SWITCH__ OR SWITCH)
    target_compile_definitions(MedabotsRokushoRecomp PRIVATE SWITCH)
    target_link_directories(MedabotsRokushoRecomp PRIVATE
        /opt/devkitpro/portlibs/switch/lib
        /opt/devkitpro/libnx/lib
    )

    target_link_options(MedabotsRokushoRecomp PRIVATE
        "-L/opt/devkitpro/portlibs/switch/lib"
        "-L/opt/devkitpro/libnx/lib"
    )

    target_link_libraries(MedabotsRokushoRecomp PRIVATE
        /opt/devkitpro/portlibs/switch/lib/libSDL2.a
        /opt/devkitpro/portlibs/switch/lib/libEGL.a
        /opt/devkitpro/portlibs/switch/lib/libGLESv2.a
        /opt/devkitpro/portlibs/switch/lib/libglapi.a
        /opt/devkitpro/portlibs/switch/lib/libdrm_nouveau.a
        /opt/devkitpro/libnx/lib/libnx.a
        m
    )
    target_link_libraries(MedabotsRokushoRecomp PRIVATE gbarecomp_runtime gbarecomp_armv4t)

    option(MEDABOTS_EMBED_ROMFS "Embed rom.gba in RomFS (NOT for public distribution)" OFF)
    set(MEDABOTS_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/romfs")
    file(MAKE_DIRECTORY "${MEDABOTS_ROMFS_DIR}")
    if(MEDABOTS_EMBED_ROMFS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/decomp/medabots_rokusho.gba")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/decomp/medabots_rokusho.gba"
            "${MEDABOTS_ROMFS_DIR}/rom.gba"
            COPYONLY
        )
    endif()

    add_custom_command(TARGET MedabotsRokushoRecomp POST_BUILD
        COMMAND nacptool --create "Medabots: Rokusho" "Niecks90" "0.1.0" $<TARGET_FILE_DIR:MedabotsRokushoRecomp>/medabots_nx.nacp
        COMMAND elf2nro $<TARGET_FILE:MedabotsRokushoRecomp> $<TARGET_FILE_DIR:MedabotsRokushoRecomp>/medabots_nx.nro --nacp=$<TARGET_FILE_DIR:MedabotsRokushoRecomp>/medabots_nx.nacp --icon=${CMAKE_CURRENT_SOURCE_DIR}/switch/icon.png --romfsdir=${MEDABOTS_ROMFS_DIR}
        COMMENT "Generating medabots_nx.nro"
    )
endif()

target_compile_definitions(MedabotsRokushoRecomp PRIVATE
    MEDABOTS_VERSION="${PROJECT_VERSION}"
    GBARECOMP_DEFAULT_DEBUG_PORT=19872
    GBARECOMP_WINDOW_TITLE="MedabotsRokushoRecomp"
    GBARECOMP_DEFAULT_GAME_CONFIG="game.toml")
if(GBARECOMP_NETPLAY)
    target_link_libraries(MedabotsRokushoRecomp PRIVATE gbarecomp_netplay)
    gbarecomp_target_netplay_identity(MedabotsRokushoRecomp "${CMAKE_CURRENT_SOURCE_DIR}" "${GBA_GAME_GENERATED_ROOT}")
    set_property(SOURCE src/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS GBAGAME_NETPLAY=1)
endif()

if(MSVC)
    target_link_options(MedabotsRokushoRecomp PRIVATE /STACK:16777216)
elseif(MINGW OR WIN32)
    target_link_options(MedabotsRokushoRecomp PRIVATE -Wl,--stack,16777216)
endif()

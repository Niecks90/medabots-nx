#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#include "runtime.h"
#include "sha1.h"
#if defined(GBAGAME_NETPLAY)
#include "multiplayer_launch.h"
#include "gba_netplay_build_identity.h"
#endif

#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

#if defined(__SWITCH__)
#include <SDL.h>
#include <unistd.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <cstdarg>
#include <exception>

void log_boot(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fflush(stdout);
}

// Dispatch table and native RAM declarations
extern "C" struct DispatchEntry { uint32_t addr; uint8_t thumb; uint8_t resume; void (*fn)(void); };
extern "C" const DispatchEntry kDispatchTable[];
extern "C" const unsigned kDispatchTableLen;
extern "C" void register_native_ram(void);

extern "C" const DispatchEntry metabee_kDispatchTable[];
extern "C" const unsigned metabee_kDispatchTableLen;
extern "C" void metabee_register_native_ram(void);

extern "C" void runtime_set_cart_dispatch_table(const void* table, unsigned len);

extern "C" bool g_config_audio = true;
bool g_config_show_fps = false;

enum GameVersion {
    GAME_UNKNOWN = 0,
    GAME_ROKUSHO,
    GAME_METABEE
};

constexpr const char* kRokushoSha1   = "c4572428ea97b302f699a3b4eba2a1f0e87c1c9c";
constexpr const char* kMetabeeSha1   = "cd3d674e88f40a0707b150c4293588a659001d29";
constexpr const char* kMetabeeUsaSha1 = "fee2d1db994e1a13fc2bc9943198136192412185";

struct GameThreadArgs {
    int argc;
    char** argv;
    gbarecomp::RunOptions* opts;
    int return_code;
};

static void game_runner_thread(void* arg) {
    auto* ctx = static_cast<GameThreadArgs*>(arg);
    const Result affinity_rc =
        svcSetThreadCoreMask(threadGetSelf()->handle, 1, 1u << 1);
    log_boot("Game thread affinity=0x%x rc=0x%08x on Core %u\n",
             1u << 1, affinity_rc, svcGetCurrentProcessorNumber());
    try {
        log_boot("Starting gbarecomp runtime inside 32 MiB stack thread...\n");
        ctx->return_code = gbarecomp::run_game(ctx->argc, ctx->argv, *ctx->opts);
        log_boot("Exited gbarecomp runtime cleanly with code %d.\n", ctx->return_code);
    } catch (const std::exception& e) {
        log_boot("FATAL EXCEPTION in game thread: %s\n", e.what());
        ctx->return_code = 1;
    } catch (...) {
        log_boot("FATAL UNKNOWN EXCEPTION in game thread!\n");
        ctx->return_code = 1;
    }
}

#endif

int main(int argc, char** argv) {
#if defined(__SWITCH__)
    std::set_terminate([]() {
        log_boot("FATAL: std::terminate() called!\n");
    });
    atexit([]() {
        log_boot("Application exited via atexit / end.\n");
    });

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/medabots_nx", 0777);
    mkdir("sdmc:/switch/medabots_nx/save", 0777);
    chdir("sdmc:/switch/medabots_nx");

    freopen("sdmc:/switch/medabots_nx/boot.log", "w", stdout);
    freopen("sdmc:/switch/medabots_nx/boot.log", "a", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    std::printf("--- Medabots NX Boot Log (v0.2.0) ---\n");
    std::fflush(stdout);

    setenv("GBARECOMP_BIOS_HLE", "1", 1);
    setenv("GBARECOMP_DIAGNOSTICS", "0", 1);
    setenv("GBARECOMP_LANGUAGE", "0", 1);
    // On Switch, default want_vsync = false to avoid blocking Core 1 on HDMI scanout
    setenv("GBARECOMP_NO_VSYNC", "1", 1);
    unsetenv("GBARECOMP_VSYNC");

    const bool romfs_mounted = R_SUCCEEDED(romfsInit());
    log_boot("Verified directories (RomFS: %s).\n", romfs_mounted ? "mounted" : "unavailable");

    SDL_LogSetOutputFunction([](void* userdata, int category, SDL_LogPriority priority, const char* message) {
        log_boot("[SDL] (%d) %s\n", priority, message);
    }, nullptr);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

    auto compute_sha1 = [](const std::string& path, size_t* out_size) -> std::string {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return {};
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        if (sz <= 0) { fclose(f); return {}; }
        if (out_size) *out_size = static_cast<size_t>(sz);
        fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> buf(static_cast<size_t>(sz));
        size_t n = fread(buf.data(), 1, buf.size(), f);
        fclose(f);
        if (n != buf.size()) return {};
        return gba::sha1(buf.data(), buf.size()).hex();
    };

    GameVersion detected_version = GAME_UNKNOWN;
    std::string detected_sha1;
    static std::string selected_rom_path;

    const std::vector<std::string> rom_search_paths = {
        "sdmc:/switch/medabots_nx/rom.gba",
        "sdmc:/switch/medabots_nx/rokusho.gba",
        "sdmc:/switch/medabots_nx/metabee.gba",
        "romfs:/rom.gba",
        "romfs:/rokusho.gba",
        "romfs:/metabee.gba"
    };

    for (const auto& path : rom_search_paths) {
        size_t sz = 0;
        std::string h = compute_sha1(path, &sz);
        if (h.empty()) continue;
        log_boot("Inspecting %s: %zu bytes, SHA-1=%s\n", path.c_str(), sz, h.c_str());
        if (h == kRokushoSha1) {
            detected_version = GAME_ROKUSHO;
            detected_sha1 = h;
            selected_rom_path = path;
            log_boot("Target ROM validated as Medabots: Rokusho Version (USA).\n");
            break;
        } else if (h == kMetabeeSha1 || h == kMetabeeUsaSha1) {
            detected_version = GAME_METABEE;
            detected_sha1 = h;
            selected_rom_path = path;
            log_boot("Target ROM validated as Medabots: Metabee Version (%s).\n", (h == kMetabeeSha1 ? "EUR" : "USA"));
            break;
        }
    }

    if (selected_rom_path.empty()) {
        log_boot("FATAL: No clean Medabots (Rokusho or Metabee) ROM found!\n");
        consoleInit(NULL);
        printf("\x1b[1;31m====================================================\x1b[0m\n");
        printf("\x1b[1;37m             Medabots NX - Boot Error               \x1b[0m\n");
        printf("\x1b[1;31m====================================================\x1b[0m\n\n");
        printf("ROM file missing or SHA-1 mismatch!\n\n");
        printf("Supported versions (8,388,608 bytes clean ROM):\n");
        printf("  - Medabots: Rokusho Version (USA)\n");
        printf("    SHA-1: %s\n\n", kRokushoSha1);
        printf("  - Medabots: Metabee Version (EUR / USA)\n");
        printf("    SHA-1: %s\n\n", kMetabeeSha1);
        printf("Please place your ROM on SD card:\n");
        printf("  'sdmc:/switch/medabots_nx/rom.gba'\n");
        printf("  (or 'rokusho.gba' / 'metabee.gba')\n\n");
        printf("Press PLUS (+) or B to return to Homebrew Menu.\n");
        consoleUpdate(NULL);

        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        u64 start_tick = armGetSystemTick();
        u64 timeout = armGetSystemTickFreq() * 15;
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & (HidNpadButton_Plus | HidNpadButton_B)) break;
            if (armGetSystemTick() - start_tick > timeout) break;
            svcSleepThread(50000000ULL);
        }
        consoleExit(NULL);
        return 1;
    }

    FILE* bios = fopen("sdmc:/switch/medabots_nx/gba_bios.bin", "rb");
    if (bios) {
        fseek(bios, 0, SEEK_END);
        long size = ftell(bios);
        fclose(bios);
        log_boot("Found gba_bios.bin: %ld bytes.\n", size);
    } else {
        log_boot("ERROR: Missing sdmc:/switch/medabots_nx/gba_bios.bin!\n");
        consoleInit(NULL);
        printf("\x1b[1;31m====================================================\x1b[0m\n");
        printf("\x1b[1;37m             Medabots NX - Boot Error               \x1b[0m\n");
        printf("\x1b[1;31m====================================================\x1b[0m\n\n");
        printf("Missing GBA BIOS file!\n\n");
        printf("Please place 'gba_bios.bin' (16,384 bytes) in:\n");
        printf("'sdmc:/switch/medabots_nx/gba_bios.bin'\n\n");
        printf("Press PLUS (+) or B to return to Homebrew Menu.\n");
        consoleUpdate(NULL);

        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        u64 start_tick = armGetSystemTick();
        u64 timeout = armGetSystemTickFreq() * 15;
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 kDown = padGetButtonsDown(&pad);
            if (kDown & (HidNpadButton_Plus | HidNpadButton_B)) break;
            if (armGetSystemTick() - start_tick > timeout) break;
            svcSleepThread(50000000ULL);
        }
        consoleExit(NULL);
        return 1;
    }

    bool config_linear_filter = false;
    bool config_vsync = false;
    bool config_uncapped = false;
    int config_cpu_boost = 0;
    g_config_show_fps = false;

    std::string config_paths[] = {
        "sdmc:/switch/medabots_nx/config.txt",
        "./config.txt"
    };

    std::string loaded_config_path;
    std::ifstream config_file;
    for (const auto& path : config_paths) {
        config_file.clear();
        config_file.open(path);
        if (config_file.is_open()) {
            loaded_config_path = path;
            break;
        }
    }

    if (config_file.is_open()) {
        log_boot("Loaded config from %s\n", loaded_config_path.c_str());
        std::string line;
        while (std::getline(config_file, line)) {
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos || line[first] == '#' || line[first] == ';') continue;
            line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

            auto eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                for (auto& c : key) c = static_cast<char>(std::tolower(c));
                for (auto& c : val) c = static_cast<char>(std::tolower(c));
                const bool is_true = (val == "1" || val == "true" || val == "on" || val == "yes");
                const bool is_false = (val == "0" || val == "false" || val == "off" || val == "no");

                if (key == "linear_filter" || key == "linearfilter") {
                    if (is_true) config_linear_filter = true;
                    else if (is_false) config_linear_filter = false;
                } else if (key == "show_fps" || key == "showfps") {
                    if (is_true) g_config_show_fps = true;
                    else if (is_false) g_config_show_fps = false;
                } else if (key == "vsync") {
                    if (is_true) config_vsync = true;
                    else if (is_false) config_vsync = false;
                } else if (key == "uncapped") {
                    if (is_true) config_uncapped = true;
                    else if (is_false) config_uncapped = false;
                } else if (key == "cpu_boost" || key == "cpuboost") {
                    config_cpu_boost = std::atoi(val.c_str());
                } else if (key == "audio") {
                    if (is_true) g_config_audio = true;
                    else if (is_false) g_config_audio = false;
                }
            }
        }
    } else {
        log_boot("Config not found, creating default at sdmc:/switch/medabots_nx/config.txt\n");
        std::ofstream new_config("sdmc:/switch/medabots_nx/config.txt");
        new_config << "vsync = 0\n";
        new_config << "uncapped = 0\n";
        new_config << "cpu_boost = 0\n";
        new_config << "linear_filter = 0\n";
        new_config << "show_fps = 0\n";
        new_config << "audio = 1\n";
    }
    ApmCpuBoostMode boost_mode = ApmCpuBoostMode_Normal;
    if (config_cpu_boost == 1) boost_mode = ApmCpuBoostMode_FastLoad;
    else if (config_cpu_boost == 2) boost_mode = ApmCpuBoostMode_Type2;
    const Result boost_rc = appletSetCpuBoostMode(boost_mode);
    log_boot("CPU boost mode %d rc=0x%08x\n", config_cpu_boost, boost_rc);
    if (config_vsync) {
        setenv("GBARECOMP_VSYNC", "1", 1);
        unsetenv("GBARECOMP_NO_VSYNC");
    } else {
        setenv("GBARECOMP_NO_VSYNC", "1", 1);
        unsetenv("GBARECOMP_VSYNC");
    }
    if (g_config_show_fps) {
        setenv("GBARECOMP_SHOW_FPS", "1", 1);
    } else {
        setenv("GBARECOMP_SHOW_FPS", "0", 1);
    }

    log_boot("Config: vsync=%d, uncapped=%d, cpu_boost=%d, linear_filter=%d, show_fps=%d\n",
             config_vsync, config_uncapped, config_cpu_boost, config_linear_filter, g_config_show_fps);
#endif
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("Medabots NX v%s\n", MEDABOTS_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "Medabots NX [--bios <path>] [--rom <path>] [game.toml]\n");
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
    std::vector<std::string> args(argv, argv + argc);

    static std::string active_save_path;
#if defined(__SWITCH__)
    if (detected_version == GAME_METABEE) {
        log_boot("Configuring runtime for Medabots: Metabee Version\n");
        runtime_set_cart_dispatch_table(metabee_kDispatchTable, metabee_kDispatchTableLen);
        metabee_register_native_ram();
        log_boot("Loaded Metabee dispatch table (%u entries)\n", metabee_kDispatchTableLen);
        opts.builtin_game_name = "Medabots: Metabee";
        opts.builtin_rom_sha1 = detected_sha1.c_str();
        opts.builtin_rom_crc32 = 0x50927f3eu;
        opts.mod_game_id = "medabots-metabee";
        opts.launcher_region = (detected_sha1 == kMetabeeSha1 ? "EUR" : "USA");
        active_save_path = "sdmc:/switch/medabots_nx/save/metabee.sav";
    } else {
        log_boot("Configuring runtime for Medabots: Rokusho Version\n");
        runtime_set_cart_dispatch_table(kDispatchTable, kDispatchTableLen);
        register_native_ram();
        log_boot("Loaded Rokusho dispatch table (%u entries)\n", kDispatchTableLen);
        opts.builtin_game_name = "Medabots: Rokusho";
        opts.builtin_rom_sha1 = "c4572428ea97b302f699a3b4eba2a1f0e87c1c9c";
        opts.builtin_rom_crc32 = 0xe144ded2u;
        opts.mod_game_id = "medabots-rokusho";
        opts.launcher_region = "USA";
        active_save_path = "sdmc:/switch/medabots_nx/save/rokusho.sav";

        // Migrate legacy save/rom.sav to save/rokusho.sav if rokusho.sav does not exist yet
        FILE* cur_sav = fopen("sdmc:/switch/medabots_nx/save/rokusho.sav", "rb");
        if (cur_sav) {
            fclose(cur_sav);
        } else {
            FILE* src_sav = fopen("sdmc:/switch/medabots_nx/save/rom.sav", "rb");
            if (src_sav) {
                FILE* dst_sav = fopen("sdmc:/switch/medabots_nx/save/rokusho.sav", "wb");
                if (dst_sav) {
                    char chunk[4096];
                    size_t n;
                    while ((n = fread(chunk, 1, sizeof(chunk), src_sav)) > 0) {
                        fwrite(chunk, 1, n, dst_sav);
                    }
                    fclose(dst_sav);
                    log_boot("Migrated legacy save/rom.sav to save/rokusho.sav\n");
                }
                fclose(src_sav);
            }
        }
    }
    opts.launcher_save_path = active_save_path.c_str();
#else
    opts.builtin_game_name = "Medabots";
    opts.builtin_rom_sha1 = kRokushoSha1;
    opts.builtin_rom_crc32 = 0xe144ded2u;
    opts.mod_game_id = "medabots";
    opts.launcher_save_path = "saves/rom.sav";
#endif

    opts.mod_owns_adaptive_view = false;

    // Clean native 4:3 (240x160) presentation
    opts.max_view_width = 240;
    opts.max_resize_view_width = 240;
    opts.resize_driven_view = false;
    opts.extended_view_init = nullptr;
    opts.high_resolution_affine_provider = nullptr;

    static const char* const view_labels[] = {"Native (4:3)"};
    static const uint16_t view_widths[] = {240};
    opts.launcher_aspect_labels = view_labels;
    opts.launcher_aspect_view_widths = view_widths;
    opts.launcher_num_aspects = 1;
    opts.launcher_expose_widescreen = false;
    opts.launcher_expose_adaptive_view = false;
    opts.launcher_expose_sharp_filter = true;
    opts.launcher_default_sharp_filter = true;
    opts.launcher_expose_affine_filter = false;
    opts.launcher_default_affine_filter = false;

#if !defined(__SWITCH__)
    opts.launcher_game_config = "game.toml";
#endif

#if defined(GBAGAME_RECOMP_UI) && !defined(__SWITCH__)
    if (game_launcher_preboot(args, opts)) return 0;
#endif

#if defined(__SWITCH__)
    opts.show_fps_by_default = g_config_show_fps;

    static char arg_prog[]        = "medabots_nx";
    static char arg_bios_flag[]   = "--bios";
    static char arg_bios_path[]   = "sdmc:/switch/medabots_nx/gba_bios.bin";
    static char arg_rom_flag[]    = "--rom";
    static char arg_rom_path[256];
    std::snprintf(arg_rom_path, sizeof(arg_rom_path), "%s", selected_rom_path.c_str());
    static char arg_save_flag[]   = "--save";
    static char arg_save_path[256];
    std::snprintf(arg_save_path, sizeof(arg_save_path), "%s", active_save_path.c_str());
    static char arg_linear_flag[] = "--linear-filter";
    static char arg_uncapped_flag[] = "--uncapped";
    static char arg_hle[]         = "--bios-hle";

    std::vector<char*> av;
    av.push_back(argv && argv[0] ? argv[0] : arg_prog);
    av.push_back(arg_bios_flag);
    av.push_back(arg_bios_path);
    av.push_back(arg_rom_flag);
    av.push_back(arg_rom_path);
    av.push_back(arg_save_flag);
    av.push_back(arg_save_path);
    av.push_back(arg_hle);

    if (config_linear_filter) {
        av.push_back(arg_linear_flag);
    }
    if (config_uncapped) {
        av.push_back(arg_uncapped_flag);
    }
    av.push_back(nullptr);

    GameThreadArgs thread_args;
    thread_args.argc = static_cast<int>(av.size() - 1);
    thread_args.argv = av.data();
    thread_args.opts = &opts;
    thread_args.return_code = 0;

    Thread game_thread;
    constexpr size_t kStackSize = 32 * 1024 * 1024; // 32 MiB stack
    Result rc = threadCreate(&game_thread, game_runner_thread, &thread_args, nullptr, kStackSize, 0x2C, 1);
    if (R_FAILED(rc)) {
        log_boot("ERROR: Failed to create game thread (rc=0x%x)!\n", rc);
        return 1;
    }

    log_boot("Launching game thread...\n");
    rc = threadStart(&game_thread);
    if (R_FAILED(rc)) {
        log_boot("ERROR: Failed to start game thread (rc=0x%x)!\n", rc);
        threadClose(&game_thread);
        return 1;
    }

    threadWaitForExit(&game_thread);
    threadClose(&game_thread);

    return thread_args.return_code;
#else
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#endif
}

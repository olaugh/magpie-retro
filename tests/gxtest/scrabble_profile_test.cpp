/**
 * Scrabble Genesis ROM Profiling Test
 *
 * Runs a game with CPU cycle profiling enabled and prints
 * a breakdown of cycles per function.
 */

#include <gxtest.h>
#include <profiler.h>
#include "scrabble_symbols.h"
#include <iostream>

namespace {

constexpr int MAX_GAME_FRAMES = 30000;

void RunProfile(const char* rom_path, const char* elf_path, const char* name) {
    GX::Emulator emu;
    if (!emu.LoadRom(rom_path)) {
        std::cerr << "Failed to load ROM: " << rom_path << std::endl;
        return;
    }

    GX::Profiler profiler;
    int sym_count = profiler.LoadSymbolsFromELF(elf_path);
    if (sym_count <= 0) {
        std::cerr << "Failed to load symbols from: " << elf_path << std::endl;
        return;
    }

    emu.WriteLong(Scrabble::test_seed_override, 0);

    profiler.Start(GX::ProfileMode::Simple);
    int frames = emu.RunUntilMemoryEquals(Scrabble::test_game_over, 1, MAX_GAME_FRAMES);
    profiler.Stop();

    if (frames < 0) {
        std::cerr << "Game did not complete" << std::endl;
        return;
    }

    int16_t p0 = static_cast<int16_t>(emu.ReadWord(Scrabble::test_player0_score));
    int16_t p1 = static_cast<int16_t>(emu.ReadWord(Scrabble::test_player1_score));
    uint32_t total = emu.ReadLong(Scrabble::total_frames);

    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "Game completed in " << total << " frames" << std::endl;
    std::cout << "Score: " << p0 << " - " << p1 << std::endl;
    std::cout << "Total cycles: " << profiler.GetTotalCycles() << std::endl;
    std::cout << "\nTop 20 functions by cycle count:" << std::endl;
    profiler.PrintReport(std::cout, 20);
}

TEST(ScrabbleProfile, Shadow) {
    RunProfile(ROM_NWL23_SHADOW, "build/nwl23-shadow/scrabble.elf", "NWL23 Shadow");
}

TEST(ScrabbleProfile, NoShadow) {
    RunProfile(ROM_NWL23_NOSHADOW, "build/nwl23-noshadow/scrabble.elf", "NWL23 NoShadow");
}

TEST(ScrabbleProfile, ShadowVsNoShadow) {
    std::cout << "\n======================================" << std::endl;
    std::cout << "Shadow vs No-Shadow Profiling Comparison" << std::endl;
    std::cout << "======================================" << std::endl;

    RunProfile(ROM_NWL23_SHADOW, "build/nwl23-shadow/scrabble.elf", "NWL23 Shadow");
    RunProfile(ROM_NWL23_NOSHADOW, "build/nwl23-noshadow/scrabble.elf", "NWL23 NoShadow");
}

} // namespace

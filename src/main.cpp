// Minimal boot-trace driver: loads a real Model 1 ROM set (the user's own
// legally-dumped files -- see docs/planning/06-legal-and-assets.md) into
// Model1Bus, resets the V60 core against it, and traces execution until it
// hits something this project doesn't implement yet, or a fixed
// instruction budget runs out.
//
// This is deliberately NOT a playable emulator: there is no video/sound/
// input, and most of the bus (video RAM, the TGP coprocessor, the sound
// board) is still stubbed (see docs/hardware-notes and
// docs/planning/05-roadmap.md's Phase 3/4/5/7 status). Its purpose is to
// turn "what does this core need next" from a guess into an empirical,
// reproducible answer: the exact PC/opcode/address a real ROM's boot code
// first requires that this project hasn't built yet.

#include "board/games/virtua_fighter.h"
#include "board/games/virtua_racing.h"
#include "board/rom_set.h"
#include "bus/model1_bus.h"
#include "cpu/v60/v60.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

using model1::board::RomLoadError;
using model1::board::RomRegion;
using model1::board::load_rom_region;
using model1::bus::Model1Bus;
using model1::bus::UnmappedAddress;
using model1::cpu::v60::Cpu;
using model1::cpu::v60::UnimplementedAddressingMode;
using model1::cpu::v60::UnimplementedOpcode;

// The V60's real reset vector on this board: the CPU core's own class
// default is the raw 0xFFFFFFF0, but this board's address bus is masked to
// 24 bits (confirmed both from MAME's own address-map validation and the
// NEC datasheet -- see docs/hardware-notes/07-v60-architecture.md), so the
// vector actually resolves to 0x00FFFFF0, where ROM0's boot code lives.
constexpr uint32_t kResetVector = 0x00fffff0;

// Every target game's main-CPU ROM region is laid out identically
// (confirmed against MAME source for both games modeled so far): ROMX
// interleaved at 0x200000, two plain ROM0 chunks at window offsets
// 0x040000/0x060000 (CPU addresses 0xfc0000/0xfe0000), and 4 interleaved
// ROMO banks. Loading only differs in which game's descriptor functions
// supply the RomRegions.
void load_main_cpu_roms(Model1Bus& bus, const std::string& rom_dir, const RomRegion& romx,
                         const RomRegion& rom0_low, const RomRegion& rom0_high,
                         const std::array<RomRegion, 4>& romo_banks) {
    bus.load_romX(load_rom_region(rom_dir, romx));
    bus.load_rom0(0x040000, load_rom_region(rom_dir, rom0_low));
    bus.load_rom0(0x060000, load_rom_region(rom_dir, rom0_high));
    for (int bank = 0; bank < 4; ++bank) bus.load_romO_bank(bank, load_rom_region(rom_dir, romo_banks[bank]));
}

void load_game(Model1Bus& bus, const std::string& rom_dir, const std::string& game) {
    if (game == "vf") {
        using namespace model1::board::games::virtua_fighter;
        load_main_cpu_roms(bus, rom_dir, romx(), rom0_low(), rom0_high(), romo_banks());
    } else if (game == "vr") {
        using namespace model1::board::games::virtua_racing;
        load_main_cpu_roms(bus, rom_dir, romx(), rom0_low(), rom0_high(), romo_banks());
    } else {
        throw std::runtime_error("unknown game '" + game + "' (expected 'vf' or 'vr')");
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <rom_directory> [game=vf|vr] [max_instructions=2000000]\n";
        return 2;
    }
    const std::string rom_dir = argv[1];
    const std::string game = argc > 2 ? argv[2] : "vf";
    const long max_instructions = argc > 3 ? std::strtol(argv[3], nullptr, 10) : 2'000'000;

    Model1Bus bus;
    try {
        load_game(bus, rom_dir, game);
    } catch (const RomLoadError& e) {
        std::cerr << "ROM load failed: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    Cpu cpu(bus);
    cpu.reset(kResetVector);

    std::cout << std::hex << std::showbase;
    std::cout << "Booting '" << game << "' from " << rom_dir << ", reset vector " << kResetVector << "\n";

    // Traces the first 200 instructions in full, then only every 10,000th,
    // so a long straight-line run doesn't flood the terminal while still
    // showing the interesting early boot sequence in detail.
    constexpr long kFullTraceInstructions = 200;
    constexpr long kSparseTraceInterval = 10000;

    // Detects getting stuck in ANY short repeating cycle, not just simple
    // straight-line drift: real boot code commonly polls a hardware
    // register in a tight loop (e.g. "wait for the TGP FIFO to say it's
    // ready", "wait for VBLANK") -- with most of the bus still stubbed
    // (see docs/hardware-notes and the Phase 3/4/5/7 roadmap status), a
    // polled flag that never real hardware would flip can spin forever
    // without ever tripping a naive "PC hasn't moved" check, since PC
    // *is* moving, just around a small loop. Tracked per-PC: whenever a
    // given PC recurs with the same short period (<= kLoopPeriodMax
    // instructions) as its previous recurrence, that's one more lap of a
    // stable cycle; kLoopRepeatThreshold laps in a row means it's not
    // going anywhere on its own. A period of exactly 1 is the special
    // case of straight-line drift through uninitialized/erased memory,
    // including the V60's own HALT (opcode 0x00), which (see
    // docs/hardware-notes/07-v60-architecture.md) does NOT actually halt
    // execution the way its name suggests -- it just advances PC by 1
    // forever, which this same detector catches without needing separate
    // logic for it.
    struct LoopTrack {
        long last_seen = -1;
        long period = 0;
        long repeat = 0;
    };
    constexpr long kLoopPeriodMax = 256;
    constexpr long kLoopRepeatThreshold = 2000;
    std::unordered_map<uint32_t, LoopTrack> loop_track;

    for (long i = 0; i < max_instructions; ++i) {
        const uint32_t pc_before = cpu.pc();

        try {
            cpu.step();
        } catch (const UnimplementedOpcode& e) {
            std::cout << "\n[STOP] UnimplementedOpcode after " << std::dec << i << std::hex
                      << " instructions: PC=" << e.pc << " opcode=" << int(e.opcode) << "\n";
            return 3;
        } catch (const UnimplementedAddressingMode& e) {
            std::cout << "\n[STOP] UnimplementedAddressingMode after " << std::dec << i << std::hex
                      << " instructions: PC=" << e.pc << " modifier=" << int(e.modifier) << "\n";
            return 3;
        } catch (const UnmappedAddress& e) {
            std::cout << "\n[STOP] UnmappedAddress after " << std::dec << i << std::hex
                      << " instructions: address=" << e.address << "\n";
            return 3;
        }

        if (i < kFullTraceInstructions || i % kSparseTraceInterval == 0) {
            std::cout << std::dec << "#" << i << std::hex << " PC=" << pc_before << "\n";
        }

        LoopTrack& track = loop_track[pc_before];
        if (track.last_seen >= 0) {
            const long period = i - track.last_seen;
            if (period > 0 && period <= kLoopPeriodMax) {
                track.repeat = (period == track.period) ? track.repeat + 1 : 1;
                track.period = period;
                if (track.repeat >= kLoopRepeatThreshold) {
                    std::cout << "\n[STOP] Stuck in a tight loop after " << std::dec << i
                              << " instructions: PC=" << std::hex << pc_before << std::dec << " recurs every "
                              << period << " instruction" << (period == 1 ? "" : "s") << ", " << track.repeat
                              << " times straight.\n";
                    if (period == 1) {
                        std::cout << "This is a period-1 cycle -- either draining through uninitialized/erased "
                                     "memory, or\n"
                                     "the V60's HALT (which doesn't actually halt).\n";
                    } else {
                        std::cout << "This is almost always real boot code polling a hardware register this "
                                     "project\n"
                                     "still stubs (e.g. the TGP command/result FIFO, an interrupt/VBLANK flag, or "
                                     "the\n"
                                     "I/O board's dual-port RAM handshake) -- look at what "
                                  << std::hex << pc_before << std::dec
                                  << " and its loop body actually\nread/write for the next concrete thing to "
                                     "implement.\n";
                    }
                    return 5;
                }
            } else {
                track.repeat = 0;
            }
        }
        track.last_seen = i;
    }

    std::cout << std::dec << "\n[STOP] Reached max_instructions (" << max_instructions << ") without error.\n";
    return 0;
}

#pragma once

#include "board/rom_set.h"

#include <array>

// Virtua Racing's per-game ROM descriptor. Filenames/sizes/CRC32s below are
// sourced from MAME's model1.cpp ROM_START(vr) block, used purely as
// documentation/fingerprints -- see docs/planning/06-legal-and-assets.md.
// No ROM bytes are included or fetched by this project.
//
// Only the main-CPU-visible regions this project has bring-up logic for so
// far are covered: ROMX (fixed), ROM0 (fixed boot ROM, two files at
// specific offsets within the window), and ROMO's four banks (switched via
// the bank register at 0xE00004 -- see docs/hardware-notes/01-cpu-and-bus.md
// and model1.cpp's bank_w). The TGP/sound-CPU/sample/polygon-data ROMs
// also present in the real ROM set belong to later phases (5/4/6) and are
// not modeled here yet.
namespace model1::board::games::virtua_racing {

// 0x200000-0x2fffff in CPU address space. Two 512KB files interleaved.
const RomRegion& romx();

// 0xf80000-0xffffff is the CPU-visible window, but only two 128KB chunks
// within it are actually populated (0xfc0000-0xfdffff and
// 0xfe0000-0xffffff); the rest reads as erased (0xFF), matching the real
// ROM_START's ROMREGION_ERASEFF default.
const RomRegion& rom0_low();  // loads at 0xfc0000
const RomRegion& rom0_high(); // loads at 0xfe0000

// 0x100000-0x1fffff in CPU address space is a 1MB window onto whichever of
// these 4 banks the bank register currently selects. Each bank is a 1MB
// region formed from two 512KB files interleaved. Confirmed against
// model1.cpp's bank_w: selecting bank N maps to raw ROM offset
// 0x1000000 + 0x100000*N within the original "maincpu" ROM_REGION -- these
// four correspond to N=0..3.
const std::array<RomRegion, 4>& romo_banks();

} // namespace model1::board::games::virtua_racing

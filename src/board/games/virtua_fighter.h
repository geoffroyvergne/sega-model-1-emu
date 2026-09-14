#pragma once

#include "board/rom_set.h"

#include <array>

// Virtua Fighter's per-game ROM descriptor. Filenames/sizes/CRC32s below are
// sourced from MAME's model1.cpp ROM_START(vf) block, used purely as
// documentation/fingerprints -- see docs/planning/06-legal-and-assets.md.
// No ROM bytes are included or fetched by this project.
//
// Confirmed against source: Virtua Fighter's main-CPU ROM region is laid
// out identically to Virtua Racing's (same offsets/sizes/interleaving for
// ROMX, ROM0, and all 4 ROMO banks -- see virtua_racing.h for the general
// shape), just with different files/CRCs. Only that main-CPU-visible
// portion is covered here, for the same reason as virtua_racing.h: the
// TGP/sound-CPU/sample/polygon-data ROMs also present in the real set
// belong to later phases (5/4/6) and aren't modeled yet.
namespace model1::board::games::virtua_fighter {

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
// region formed from two 512KB files interleaved. See virtua_racing.h's
// comment on the bank_w-confirmed offset formula this follows too.
const std::array<RomRegion, 4>& romo_banks();

} // namespace model1::board::games::virtua_fighter

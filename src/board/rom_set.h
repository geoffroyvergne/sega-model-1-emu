#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Generic ROM loading and validation, independent of any specific game.
//
// Per docs/planning/06-legal-and-assets.md: this project never includes,
// downloads, or links to ROM data. A `RomFile` entry below records only a
// fingerprint (filename/size/CRC32) so a user's own legally-dumped file can
// be checked against known-good values -- the same purpose any ROM
// verification database (e.g. the ones bundled with MAME itself) serves.
// The actual bytes always come from the user's own disk.
namespace model1::board {

// One physical ROM chip's expected identity. `crc32` is optional because
// not every fact-finding pass has confirmed it yet; `size` is always
// required since it determines how much of the target region this file
// fills.
struct RomFile {
    std::string filename;
    size_t size = 0;
    std::optional<uint32_t> crc32;
};

// How a set of RomFiles combine into one contiguous logical region.
enum class RomLayout {
    // A single file, loaded byte-for-byte starting at the region's base.
    Plain,
    // Two same-sized files interleaved byte-by-byte (file 0 at even byte
    // offsets, file 1 at odd offsets) -- the real hardware's
    // ROM_LOAD16_BYTE pattern, confirmed pervasive across Model 1 ROM
    // sets (e.g. Virtua Racing's ROMX: epr-14882.14 / epr-14883.15).
    Interleaved16,
};

// One named logical region (e.g. "ROMX", one bank of "ROMO") built from
// one or more RomFiles per `layout`.
struct RomRegion {
    std::string name;
    RomLayout layout = RomLayout::Plain;
    std::vector<RomFile> files;
    uint32_t base_address = 0; // where this region starts in CPU address space
};

struct RomLoadError : std::runtime_error {
    explicit RomLoadError(const std::string& message) : std::runtime_error(message) {}
};

// Loads and validates one RomRegion's files from `directory` (a plain
// filesystem directory the caller points at their own ROM dump -- never
// fetched or assumed by this project). Throws RomLoadError on a missing
// file, a size mismatch, or (when `crc32` is set) a checksum mismatch.
// Interleaves per `region.layout` automatically. Returns the assembled
// bytes, ready to copy into a Bus's backing memory.
std::vector<uint8_t> load_rom_region(const std::string& directory, const RomRegion& region);

uint32_t crc32(const uint8_t* data, size_t length);

} // namespace model1::board

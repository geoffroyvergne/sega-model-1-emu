#include "bus/model1_bus.h"

#include <algorithm>
#include <stdexcept>

namespace model1::bus {

UnmappedAddress::UnmappedAddress(uint32_t address)
    : std::runtime_error("unmapped Model 1 bus address"), address(address) {}

namespace {
// Fixed ROM regions read as this value where a real dump hasn't filled
// them in -- matches MAME's ROMREGION_ERASEFF default, confirmed as the
// real state of (for example) Virtua Racing's unused ROMA region and the
// unpopulated stretch of its ROM0 window.
constexpr uint8_t kErasedByte = 0xff;

// TGP RAM interface mirror masks, confirmed against model1_mem()'s
// .mirror(0x1fffe)/.mirror(0x1fffc) calls for these exact registers.
constexpr uint32_t kCoproAdrMirror = 0x1fffe;  // v60_copro_ram_adr: a 2-byte-wide register
constexpr uint32_t kCoproWordMirror = 0x1fffc; // copro_ram/fifo/fifoin_status: 4-byte-wide registers

// Does `address` refer to the register based at `base`, once both the
// mirror bits AND the register's own byte-selector bits are ignored?
// `mirror_mask`'s bits are exactly what MAME's .mirror() call declares as
// "don't care" for matching -- but the register's own width also occupies
// some low bits (which byte of a 2- or 4-byte-wide register this access
// is), and those must be cleared too before comparing to `base`, or only
// byte 0 of the register would ever match (the bug this comment is
// replacing: an earlier version compared against `base` without clearing
// those bits, so e.g. 0xd00001 -- byte 1 of the 2-byte v60_copro_ram_adr
// register -- never matched 0xd00000 and fell through to "unmapped").
bool address_in_mirrored_register(uint32_t address, uint32_t base, uint32_t mirror_mask) {
    const uint32_t lowest_mirror_bit = mirror_mask & (~mirror_mask + 1);
    const uint32_t selector_mask = lowest_mirror_bit - 1;
    return (address & ~mirror_mask & ~selector_mask) == base;
}
} // namespace

Model1Bus::Model1Bus()
    : rom_a_(kRomASize, kErasedByte),
      rom_x_(kRomXSize, kErasedByte),
      rom_0_(kRom0Size, kErasedByte),
      ram_a_(0x010000, 0),
      ram_b_(0x040000, 0),
      tgp_display_list0_(0x010000, 0),
      tgp_display_list1_(0x010000, 0),
      scr_tile_ram_(0x010000, 0),
      scr_char_ram_(0x080000, 0),
      palette_ram_(0x004000, 0),
      color_xlat_ram_(0x00c000, 0),
      io_dpram_(0x001000, 0),
      copro_ram_(0x2000, 0) {
    for (auto& bank : rom_o_banks_) bank.assign(kRomOBankSize, kErasedByte);
}

void Model1Bus::load_romA(const std::vector<uint8_t>& data) {
    std::copy(data.begin(), data.begin() + std::min(data.size(), rom_a_.size()), rom_a_.begin());
}

void Model1Bus::load_romX(const std::vector<uint8_t>& data) {
    std::copy(data.begin(), data.begin() + std::min(data.size(), rom_x_.size()), rom_x_.begin());
}

void Model1Bus::load_rom0(uint32_t offset_in_window, const std::vector<uint8_t>& data) {
    if (offset_in_window >= rom_0_.size()) return;
    const size_t n = std::min(data.size(), rom_0_.size() - offset_in_window);
    std::copy(data.begin(), data.begin() + static_cast<long>(n), rom_0_.begin() + offset_in_window);
}

void Model1Bus::load_romO_bank(int bank, const std::vector<uint8_t>& data) {
    if (bank < 0 || bank >= kRomOBankCount) {
        throw std::out_of_range("ROMO bank index out of range");
    }
    std::copy(data.begin(), data.begin() + std::min(data.size(), rom_o_banks_[static_cast<size_t>(bank)].size()),
              rom_o_banks_[static_cast<size_t>(bank)].begin());
}

// bank_w, confirmed against model1.cpp: low nibble of the written value
// selects which region's bank changes; only case 0x1 (ROMO) is exercised
// by any known ROM set (VR/VF/SWA all have a single ROMX/ROM0 bank, per
// the reference's own comment to that effect), so cases 0x2/0xf are
// accepted but intentionally do nothing here.
void Model1Bus::bank_register_write(uint16_t value) {
    switch (value & 0xf) {
        case 0x1:
            rom_o_bank_ = (value >> 4) & 0x7;
            break;
        default:
            break;
    }
}

uint16_t Model1Bus::copro_ram_address_read() const { return copro_ram_addr_; }

void Model1Bus::copro_ram_address_write(uint16_t value) { copro_ram_addr_ = value; }

// Confirmed against model1_m.cpp's v60_copro_ram_r: reading the low half
// (offset 0) never auto-increments; reading the high half (offset 1) does,
// and only when bit 0x8000 of the address register is set.
uint16_t Model1Bus::copro_ram_data_read(bool high_half) {
    const uint32_t word = copro_ram_[copro_ram_addr_ & 0x1fff];
    if (!high_half) {
        return static_cast<uint16_t>(word);
    }
    const uint16_t result = static_cast<uint16_t>(word >> 16);
    if (copro_ram_addr_ & 0x8000) copro_ram_addr_++;
    return result;
}

// Confirmed against model1_m.cpp's v60_copro_ram_w: the low and high
// halves are latched separately, and the actual 32-bit write (plus the
// same conditional auto-increment) only happens when the high half is
// written -- matching COMBINE_DATA(latch) + write-on-offset-1 exactly.
void Model1Bus::copro_ram_data_write(bool high_half, uint16_t value) {
    copro_ram_write_latch_[high_half ? 1 : 0] = value;
    if (!high_half) return;
    const uint32_t word =
        static_cast<uint32_t>(copro_ram_write_latch_[0]) | (static_cast<uint32_t>(copro_ram_write_latch_[1]) << 16);
    copro_ram_[copro_ram_addr_ & 0x1fff] = word;
    if (copro_ram_addr_ & 0x8000) copro_ram_addr_++;
}

uint8_t Model1Bus::read_byte(uint32_t address) {
    address &= 0xffffff; // 24-bit address bus, confirmed in docs/hardware-notes/07-v60-architecture.md

    if (address < 0x100000) return rom_a_[address];
    if (address < 0x200000) return rom_o_banks_[static_cast<size_t>(rom_o_bank_)][address - 0x100000];
    if (address < 0x300000) return rom_x_[address - 0x200000];

    if (address >= 0x400000 && address < 0x410000) return ram_a_[address - 0x400000];
    if (address >= 0x500000 && address < 0x540000) return ram_b_[address - 0x500000];

    if (address >= 0x600000 && address < 0x610000) return tgp_display_list0_[address - 0x600000];
    if (address >= 0x610000 && address < 0x620000) return tgp_display_list1_[address - 0x610000];
    if (address >= 0x680000 && address < 0x680004) {
        return static_cast<uint8_t>(listctl_ >> (8 * (address - 0x680000)));
    }

    if (address >= 0x700000 && address < 0x710000) return scr_tile_ram_[address - 0x700000];
    if (address >= 0x780000 && address < 0x800000) return scr_char_ram_[address - 0x780000];

    if (address >= 0x900000 && address < 0x904000) return palette_ram_[address - 0x900000];
    if (address >= 0x910000 && address < 0x91c000) return color_xlat_ram_[address - 0x910000];

    if (address >= 0xc00000 && address < 0xc01000) return io_dpram_[address - 0xc00000];
    if (address >= 0xc40000 && address < 0xc40004) return 0; // UART stub

    // The TGP RAM interface (0xd00000-0xdc0003) is handled in read16/write16
    // instead of here: it's genuinely 16-bit-wide in hardware (confirmed by
    // the real handlers' `u16` signatures), and decomposing a 16-bit access
    // to it into two independent byte accesses would double-trigger its
    // side effects (the auto-increment, specifically) -- see read16's
    // comment. read8 on these addresses is synthesized from read16 instead
    // of reaching this function at all.

    if (address == 0xe00002) return irq_mask_;
    if (address >= 0xe0000c && address < 0xe00010) return 0; // timer_r stub

    if (address >= 0xf80000) return rom_0_[address - 0xf80000];

    // Unmapped: real hardware's behavior here is undefined/open-bus; we'd
    // rather find out a game touches this than silently return garbage.
    throw UnmappedAddress(address);
}

void Model1Bus::write_byte(uint32_t address, uint8_t value) {
    address &= 0xffffff;

    if (address < 0x300000) return; // ROM regions: writes are ignored, matching real hardware

    if (address >= 0x400000 && address < 0x410000) { ram_a_[address - 0x400000] = value; return; }
    if (address >= 0x500000 && address < 0x540000) { ram_b_[address - 0x500000] = value; return; }

    if (address >= 0x600000 && address < 0x610000) { tgp_display_list0_[address - 0x600000] = value; return; }
    if (address >= 0x610000 && address < 0x620000) { tgp_display_list1_[address - 0x610000] = value; return; }
    if (address >= 0x680000 && address < 0x680004) {
        const uint32_t shift = 8 * (address - 0x680000);
        listctl_ = (listctl_ & ~(0xffu << shift)) | (static_cast<uint32_t>(value) << shift);
        return;
    }

    if (address >= 0x700000 && address < 0x710000) { scr_tile_ram_[address - 0x700000] = value; return; }
    if (address == 0x720000 || address == 0x720001) return; // unknown sync register, no-op
    if (address == 0x740000 || address == 0x740001) return; // horizontal sync register, no-op
    if (address == 0x760000 || address == 0x760001) return; // vertical sync register, no-op
    if (address == 0x770000 || address == 0x770001) return; // video sync switch, no-op
    if (address >= 0x780000 && address < 0x800000) { scr_char_ram_[address - 0x780000] = value; return; }

    if (address >= 0x900000 && address < 0x904000) { palette_ram_[address - 0x900000] = value; return; }
    if (address >= 0x910000 && address < 0x91c000) { color_xlat_ram_[address - 0x910000] = value; return; }

    if (address >= 0xc00000 && address < 0xc01000) { io_dpram_[address - 0xc00000] = value; return; }
    if (address >= 0xc40000 && address < 0xc40004) return; // UART stub

    // See the matching comment in read_byte: the TGP RAM interface is
    // handled in read16/write16, not here.

    if (address == 0xe00000) return; // irq_control_w stub (no real IRQ line to clear yet)
    if (address == 0xe00002) { irq_mask_ = value; return; }
    if (address == 0xe00004 || address == 0xe00005) {
        // bank_w is written as a 16-bit register; a byte-granular write
        // (unusual for real code) still needs to produce the same
        // combined value bank_register_write expects.
        const uint32_t shift = 8 * (address - 0xe00004);
        bank_write_latch_ =
            static_cast<uint16_t>((bank_write_latch_ & ~(0xffu << shift)) | (static_cast<uint32_t>(value) << shift));
        if (address == 0xe00005) bank_register_write(bank_write_latch_);
        return;
    }
    if (address >= 0xe00006 && address < 0xe00008) { timer_mode_ = value; return; }
    if (address >= 0xe00008 && address < 0xe0000c) { timer_period_ = value; return; }
    if (address >= 0xe0000c && address < 0xe00010) return; // timer count registers, writes ignored

    if (address >= 0xf80000) return; // ROM0: writes ignored

    throw UnmappedAddress(address);
}

namespace {
// True for any address in the TGP RAM interface's 4 register ranges,
// regardless of which byte/mirror instance -- used to route byte accesses
// through the 16-bit-primary handlers below rather than read_byte/write_byte.
bool address_in_copro_ram_interface(uint32_t address) {
    return address_in_mirrored_register(address, 0xd00000, kCoproAdrMirror) ||
           address_in_mirrored_register(address, 0xd20000, kCoproWordMirror) ||
           address_in_mirrored_register(address, 0xd80000, kCoproWordMirror) ||
           address_in_mirrored_register(address, 0xdc0000, kCoproWordMirror);
}
} // namespace

uint8_t Model1Bus::read8(uint32_t address) {
    if (address_in_copro_ram_interface(address)) {
        // Real hardware declares these registers 16-bit-wide; a byte
        // access is approximated as "read the containing 16-bit word,
        // take the requested byte" -- real code always accesses them at
        // their native 16-bit width anyway (confirmed by the reference
        // handlers' `u16` signatures), so this path exists only so an
        // unusual byte-granular access doesn't crash, not because real
        // ROMs are expected to exercise it.
        const uint16_t v = read16(address & ~1u);
        return static_cast<uint8_t>(v >> (8 * (address & 1)));
    }
    return read_byte(address);
}

void Model1Bus::write8(uint32_t address, uint8_t value) {
    if (address_in_copro_ram_interface(address)) {
        const uint32_t aligned = address & ~1u;
        uint16_t v = read16(aligned); // deliberately re-reads rather than tracking a separate latch
        const uint32_t shift = 8 * (address & 1);
        v = static_cast<uint16_t>((v & ~(0xffu << shift)) | (static_cast<uint32_t>(value) << shift));
        write16(aligned, v);
        return;
    }
    write_byte(address, value);
}

// The TGP RAM interface is handled here directly (not decomposed into two
// read_byte/write_byte calls) because it's genuinely 16-bit-wide in
// hardware and its side effects (specifically, copro_ram_data's
// auto-increment) are defined per-16-bit-access, confirmed against
// model1_m.cpp's v60_copro_ram_r/w (`offs_t offset` there is a WORD
// offset, 0 or 1, not a byte offset). An earlier version of this function
// built read16/write16 generically from two read_byte/write_byte calls,
// which for this register meant each of its two bytes independently
// re-triggered the "commit and maybe auto-increment" logic -- silently
// corrupting the second byte written (it would read back a still-partial
// latch value) and double-incrementing the address. Caught by a unit test
// exercising two consecutive auto-incrementing writes, not by inspection.
uint16_t Model1Bus::read16(uint32_t address) {
    if (address_in_mirrored_register(address, 0xd00000, kCoproAdrMirror)) {
        return copro_ram_address_read();
    }
    if (address_in_mirrored_register(address, 0xd20000, kCoproWordMirror)) {
        const bool high_half = ((address & 3) / 2) != 0;
        return copro_ram_data_read(high_half);
    }
    if (address_in_mirrored_register(address, 0xd80000, kCoproWordMirror)) {
        const bool high_half = ((address & 3) / 2) != 0;
        return static_cast<uint16_t>(copro_fifo_stub_ >> (high_half ? 16 : 0));
    }
    if (address_in_mirrored_register(address, 0xdc0000, kCoproWordMirror)) return 0; // fifoin_status_r stub

    return static_cast<uint16_t>(read_byte(address) | (read_byte(address + 1) << 8));
}

void Model1Bus::write16(uint32_t address, uint16_t value) {
    if (address_in_mirrored_register(address, 0xd00000, kCoproAdrMirror)) {
        copro_ram_address_write(value);
        return;
    }
    if (address_in_mirrored_register(address, 0xd20000, kCoproWordMirror)) {
        const bool high_half = ((address & 3) / 2) != 0;
        copro_ram_data_write(high_half, value);
        return;
    }
    if (address_in_mirrored_register(address, 0xd80000, kCoproWordMirror)) {
        const bool high_half = ((address & 3) / 2) != 0;
        const uint32_t shift = high_half ? 16 : 0;
        copro_fifo_stub_ = (copro_fifo_stub_ & ~(0xffffu << shift)) | (static_cast<uint32_t>(value) << shift);
        return;
    }
    if (address_in_mirrored_register(address, 0xdc0000, kCoproWordMirror)) return; // fifoin_status_r: read-only stub

    write_byte(address, static_cast<uint8_t>(value));
    write_byte(address + 1, static_cast<uint8_t>(value >> 8));
}

uint32_t Model1Bus::read32(uint32_t address) {
    return static_cast<uint32_t>(read16(address)) | (static_cast<uint32_t>(read16(address + 2)) << 16);
}
void Model1Bus::write32(uint32_t address, uint32_t value) {
    write16(address, static_cast<uint16_t>(value));
    write16(address + 2, static_cast<uint16_t>(value >> 16));
}

} // namespace model1::bus

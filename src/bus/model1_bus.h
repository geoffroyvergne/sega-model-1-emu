#pragma once

#include "cpu/v60/v60.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// The Model 1 main-CPU address map, confirmed against MAME's model1_mem()
// (src/mame/sega/model1.cpp) -- see docs/hardware-notes/01-cpu-and-bus.md.
// This is a from-scratch reimplementation; MAME's source was read as
// documentation only (see docs/planning/06-legal-and-assets.md).
//
// Scope: this covers the address decode for every region in the real map,
// with genuine, confirmed-correct behavior where that's already
// understood (ROM regions, ROMO bank switching, the main-CPU-facing half
// of the TGP RAM interface) and clearly-marked stubs for everything whose
// real behavior belongs to a later phase (video, sound, the TGP FIFO
// handshake, real interrupt delivery -- this project's V60 core doesn't
// implement interrupts yet either, so IRQ registers are necessarily stubs
// on both ends right now).
namespace model1::bus {

// A real bus address with no handler -- distinct from the CPU's own
// UnimplementedAddressingMode (a decode-time failure inside the CPU
// itself); this is a runtime failure of the *bus*, at whatever address the
// CPU tried to access. Kept as its own type rather than reusing the CPU's
// exception, which would misleadingly imply an instruction-decode problem.
struct UnmappedAddress : std::runtime_error {
    explicit UnmappedAddress(uint32_t address);
    uint32_t address;
};

class Model1Bus final : public cpu::v60::Bus {
public:
    Model1Bus();

    uint8_t read8(uint32_t address) override;
    void write8(uint32_t address, uint8_t value) override;
    uint16_t read16(uint32_t address) override;
    void write16(uint32_t address, uint16_t value) override;
    uint32_t read32(uint32_t address) override;
    void write32(uint32_t address, uint32_t value) override;

    // Loads raw, already-assembled region bytes (see board::load_rom_region)
    // into the given fixed ROM region. `data.size()` must not exceed the
    // region's real size; a shorter load leaves the rest at the erased
    // value (0xFF), matching ROMREGION_ERASEFF.
    void load_romA(const std::vector<uint8_t>& data);
    void load_romX(const std::vector<uint8_t>& data);
    void load_rom0(uint32_t offset_in_window, const std::vector<uint8_t>& data);

    // ROMO is a 1MB window (0x100000-0x1fffff) onto whichever bank the
    // bank register (0xE00004, confirmed via model1.cpp's bank_w) last
    // selected. `bank` is 0-based, matching bank_w's `(data >> 4) & 0x7`
    // field (only 0-3 are used by any real ROM set found so far).
    void load_romO_bank(int bank, const std::vector<uint8_t>& data);

private:
    static constexpr uint32_t kRomASize = 0x100000;
    static constexpr uint32_t kRomOBankSize = 0x100000;
    static constexpr uint32_t kRomXSize = 0x100000;
    static constexpr uint32_t kRom0Size = 0x080000; // 0xf80000-0xffffff
    static constexpr int kRomOBankCount = 8;        // register field allows 0-7; only 0-3 populated by known games

    std::vector<uint8_t> rom_a_;
    std::array<std::vector<uint8_t>, kRomOBankCount> rom_o_banks_;
    std::vector<uint8_t> rom_x_;
    std::vector<uint8_t> rom_0_;

    std::vector<uint8_t> ram_a_;              // 0x400000-0x40ffff, NVRAM (64KB)
    std::vector<uint8_t> ram_b_;              // 0x500000-0x53ffff, work RAM (256KB)
    std::vector<uint8_t> tgp_display_list0_;  // 0x600000-0x60ffff (64KB)
    std::vector<uint8_t> tgp_display_list1_;  // 0x610000-0x61ffff (64KB)
    std::vector<uint8_t> scr_tile_ram_;       // 0x700000-0x70ffff, stub pending Phase 3
    std::vector<uint8_t> scr_char_ram_;       // 0x780000-0x7fffff, stub pending Phase 3
    std::vector<uint8_t> palette_ram_;        // 0x900000-0x903fff, stub pending Phase 3/6
    std::vector<uint8_t> color_xlat_ram_;     // 0x910000-0x91bfff, stub pending Phase 3/6
    std::vector<uint8_t> io_dpram_;           // 0xc00000-0xc00fff, stub pending Phase 7

    // ROMO bank register state (0xE00004-5, bank_w). Only the "select
    // ROMO bank" case (data&0xf == 1) is exercised by any target game
    // found so far; the ROMX/ROM0 banking cases exist in the real
    // register but are unused by every known ROM set (see model1.cpp's
    // own comment to that effect) and are accepted but not acted on here.
    int rom_o_bank_ = 0;
    uint16_t bank_write_latch_ = 0; // holds partial bytes for a byte-granular write to this 16-bit register
    void bank_register_write(uint16_t value);

    // Main-CPU-facing half of the TGP RAM interface (0xd00000-0xd20003),
    // confirmed against model1_m.cpp's v60_copro_ram_adr_r/w and
    // v60_copro_ram_r/w: a 16-bit address register addressing a shared
    // array of 0x2000 32-bit words, with auto-increment on read/write of
    // the high half when bit 0x8000 of the address is set. This is
    // genuinely correct plumbing, not a stub -- it doesn't depend on the
    // TGP coprocessor core itself (Phase 5) existing yet, only on the
    // shared RAM array it will eventually read/write too.
    std::vector<uint32_t> copro_ram_;   // 0x2000 dwords
    uint16_t copro_ram_addr_ = 0;
    uint16_t copro_ram_write_latch_[2] = {0, 0};
    uint16_t copro_ram_address_read() const;
    void copro_ram_address_write(uint16_t value);
    uint16_t copro_ram_data_read(bool high_half);
    void copro_ram_data_write(bool high_half, uint16_t value);

    // TGP command/result FIFO (0xd80000-0xd80003). The real hardware
    // handshake actually halts the CPU on a full/empty FIFO (see
    // docs/hardware-notes/02-tgp-coprocessor.md) -- modeling that needs a
    // scheduler this project doesn't have yet (Phase 5), so this is
    // explicitly a stub: non-blocking, and nothing is on the other end to
    // consume/produce real values yet.
    uint32_t copro_fifo_stub_ = 0;

    // Glue-logic registers (0xe00000-0xe0000f): IRQ control/mask and
    // timers. All stubs -- this project's V60 core doesn't implement
    // interrupt delivery yet either, so there's no real behavior to give
    // these until both ends exist.
    uint8_t irq_mask_ = 0;
    uint16_t timer_mode_ = 0;
    uint32_t timer_period_ = 0;

    uint32_t listctl_ = 0; // 0x680000-0x680003, display-list buffer select

    uint8_t read_byte(uint32_t address);
    void write_byte(uint32_t address, uint8_t value);
};

} // namespace model1::bus

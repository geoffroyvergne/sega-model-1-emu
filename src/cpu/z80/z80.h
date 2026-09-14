#pragma once

#include <cstdint>
#include <stdexcept>

// The Sega Model 1 I/O board's CPU (`Z0840004PSC`, 4MHz) -- see
// docs/hardware-notes/04-io-and-controls.md. Unlike the V60 core, Z80
// semantics are extremely well-documented and stable across decades of
// independently cross-verified implementations (Zilog's own datasheet,
// the ZEXALL/ZEXDOC test-ROM consensus used to validate essentially every
// accurate Z80 emulator ever written) -- this project still spot-checks
// specific details against MAME's z80.cpp/z80.inc, but doesn't need it as
// the sole source of truth the way the undocumented V60 core did.
//
// Scope so far: the register file; the full unprefixed opcode map EXCEPT
// the DD/FD (IX/IY indexed addressing) prefixes, its own future increment
// -- concretely, that's the 8-bit load
// group (LD r,r' / LD r,n / LD (HL),n / LD A,(BC)/(DE)/(nn) and their
// reverse), 16-bit loads (LD dd,nn, LD (nn),HL / LD HL,(nn), LD SP,HL),
// 8-bit and 16-bit INC/DEC, the full ALU-vs-A group (ADD/ADC/SUB/SBC/
// AND/XOR/OR/CP, register and immediate forms), ADD HL,ss, the
// accumulator-only fast rotates (RLCA/RRCA/RLA/RRA), unconditional and
// conditional JP/JR/CALL/RET, JP (HL), RST, DJNZ, PUSH/POP, the exchange
// instructions (EX DE,HL / EX (SP),HL / EX AF,AF' / EXX), CPL, DAA,
// SCF/CCF, DI/EI, and the full CB-prefixed group (RLC/RRC/RL/RR/SLA/SRA/
// SLL/SRL, BIT/RES/SET -- all 256 opcodes, since their regular
// 2-bit-group/3-bit-operation/3-bit-register encoding makes them
// mechanical to decode uniformly once the pattern is confirmed).
//
// Also the ED-prefixed group's non-I/O-port opcodes: 16-bit ADC/SBC HL,ss
// (unlike plain ADD HL,ss, these DO set S/Z/PV), the extended 16-bit
// memory loads (LD (nn),dd / LD dd,(nn) for BC/DE/HL/SP), NEG, LD A,I /
// LD A,R / LD I,A / LD R,A, RRD/RLD, IM 0/1/2 (canonical opcodes only --
// the documented duplicate encodings aren't covered), RETN/RETI
// (canonical opcodes only, same reasoning), and the block
// transfer/compare group (LDI/LDD/LDIR/LDDR, CPI/CPD/CPIR/CPDR). The
// repeating forms (LDIR/LDDR/CPIR/CPDR) run to completion within a single
// step() call via an internal loop rather than being interruptible
// mid-repeat the way real cycle-accurate hardware is -- consistent with
// this core's timing model overall (none yet -- see docs/planning/05-roadmap.md
// Phase 7).
//
// SCF/CCF's undocumented Y/X flags: this core always mirrors them from
// the current accumulator (A), rather than modeling the internal "Q
// register" mechanism real hardware (and MAME, per its z80.h comment
// "CCF/SCF YX mask") uses -- a mechanism that also depends on whether the
// *previous* instruction affected flags, genuinely disputed/hardware-
// revision-dependent territory unlike the rest of Z80 semantics this
// project treats as settled. This isn't left as an untested guess,
// though: `tools/oracle-harness/compare_z80.py` confirms "mirror the
// current A" against the real MAME z80_device reference in both the
// obvious case (a preceding instruction that never touches flags) and a
// deliberately adversarial one (A reset to a value with both bits clear
// immediately after a CP whose own internal result has both bits set --
// real hardware agrees with "mirror A," not "carry over the last
// flags-affecting op's result"). Still not proof this holds for every
// possible preceding-instruction sequence, so revisit with a further
// source-level (or real-silicon-test) citation if a target ROM ever
// turns out to depend on a case not covered by those tests.
//
// BIT b,(HL)'s Y/X flags are a real, oracle-confirmed exception to "mirror
// the tested value": real hardware derives them from the high byte of an
// internal address latch ("WZ"/MEMPTR) set to HL+1 during the
// instruction's memory read, NOT from the tested byte itself. Confirmed
// against real MAME with a deliberately discriminating test (a tested
// byte with both undocumented bits set, while HL+1's high byte has both
// clear) before trusting it -- the naive "mirror the operand" guess that
// works for BIT b,r's register form is simply wrong for BIT b,(HL), and
// this core computes HL+1's high byte directly rather than adding a full
// WZ register just for this one case. RES/SET/the rotate-shift group
// don't share this quirk -- their Y/X always mirror their own actual
// result byte, register or (HL) operand alike, confirmed by the absence
// of any special-casing needed to pass the oracle for those.
//
// The block transfer/compare group's undocumented Y/X flags follow a
// well-established (not disputed the way SCF/CCF's Q-register is) rule
// distinct from every other instruction in this core: they're derived
// from `transferred_byte + A` (LDI/LDD/LDIR/LDDR) or from the CP-style
// subtraction result adjusted by its own half-carry (CPI/CPD/CPIR/CPDR),
// not from the instruction's own "primary" result the way most
// instructions' Y/X mirror their result byte. Implemented exactly per
// that rule (see block_ld/block_cp), not approximated.
//
// Also I/O ports: the unprefixed IN A,(n) / OUT (n),A (confirmed the real
// hardware detail that A goes out on the address bus's upper 8 bits
// alongside the immediate n, not just n alone -- Bus::port_read/write
// take the full 16-bit port for this reason) and the ED-prefixed
// IN r,(C) / OUT (C),r for all 8 register-or-undocumented-form encodings
// (the full BC register pair is the port address here, not zero-extended
// C), including the undocumented "IN (C)" (0x70: sets flags, discards the
// value) and "OUT (C),0" (0x71: writes a literal 0) forms.
//
// Deliberately NOT yet implemented (each a substantial, separately-scoped
// increment): the ED-prefixed group's I/O-BLOCK instructions
// (INI/IND/INIR/INDR/OUTI/OUTD/OTIR/OTDR) -- unlike plain IN/OUT above,
// these have historically-disputed undocumented "K-flag" formulas (see
// Sean Young's "The Undocumented Z80 Documented") that deserve their own
// careful, oracle-checked increment rather than a guess; the DD/FD-
// prefixed group (IX/IY indexed addressing, including their own CB
// sub-prefix); interrupt acceptance (IRQ/NMI -- so DI/EI/IM exist as
// opcodes without yet having anything to gate); and the R register's
// per-M1-cycle auto-increment (only externally observable via LD A,R and
// refresh-timing tricks). An opcode landing in any of these throws
// UnimplementedOpcode rather than silently misbehaving.
namespace cpu::z80 {

// Flag bits within the F register -- standard Z80 layout (Zilog
// datasheet; also confirmed against MAME's z80.inc CF/NF/PF/HF/YXF/ZF/SF
// #defines).
enum Flag : uint8_t {
    kFlagC = 0x01,  // Carry
    kFlagN = 0x02,  // Add/Subtract (tracks which family the last op was, for DAA)
    kFlagPV = 0x04, // Parity (logical ops) or signed oVerflow (arithmetic ops)
    kFlagX = 0x08,  // Undocumented: mirrors bit 3 of the result
    kFlagH = 0x10,  // Half-carry: carry/borrow out of bit 3
    kFlagY = 0x20,  // Undocumented: mirrors bit 5 of the result
    kFlagZ = 0x40,  // Zero
    kFlagS = 0x80,  // Sign
};

// Thrown for any opcode (or opcode prefix) not yet implemented -- see the
// scope list above. Matches this project's general policy (established by
// the V60 core) of failing loudly on unimplemented behavior rather than
// silently doing the wrong thing.
class UnimplementedOpcode : public std::runtime_error {
public:
    explicit UnimplementedOpcode(uint8_t opcode);
    uint8_t opcode() const { return opcode_; }

private:
    uint8_t opcode_;
};

// The Z80's full register file, including the shadow (alternate) set --
// exposed directly (not behind accessors) for the same reason the V60
// core exposes its register file directly: tests and callers need to set
// up and inspect exact machine state.
struct Registers {
    uint8_t a = 0, f = 0;
    uint8_t b = 0, c = 0;
    uint8_t d = 0, e = 0;
    uint8_t h = 0, l = 0;

    // Shadow set, swapped in by EX AF,AF' / EXX -- not yet implemented,
    // so these are inert for now.
    uint8_t a2 = 0, f2 = 0;
    uint8_t b2 = 0, c2 = 0, d2 = 0, e2 = 0, h2 = 0, l2 = 0;

    uint16_t ix = 0, iy = 0;
    uint16_t sp = 0, pc = 0;

    uint8_t i = 0, r = 0;
    bool iff1 = false, iff2 = false;
    uint8_t im = 0;

    bool halted = false;
};

// Real Z80 hardware only ever accesses memory one byte at a time (true of
// every 16-bit operation too, unlike the V60's genuinely 16-bit-wide TGP
// RAM register) -- so, unlike cpu::v60::Bus, this interface is 8-bit only;
// 16-bit reads/writes are composed from two 8-bit accesses by the core
// itself, low byte first, matching real bus order.
class Bus {
public:
    virtual ~Bus() = default;
    virtual uint8_t read8(uint16_t address) = 0;
    virtual void write8(uint16_t address, uint8_t value) = 0;

    // The Z80's I/O address space is electrically distinct from memory
    // (driven by the IORQ pin, not MREQ) -- a real, separate 16-bit
    // address space, not a memory-mapped alias, hence separate methods
    // here rather than reusing read8/write8. Takes the full 16-bit port
    // value real hardware puts on the address bus (for IN A,(n)/OUT (n),A
    // that's A in the upper byte and the immediate n in the lower byte;
    // for IN r,(C)/OUT (C),r it's the full BC pair) -- confirmed real
    // games/firmware can and do rely on the upper byte for I/O address
    // decoding, so truncating to 8 bits here would be a real fidelity
    // loss, not a harmless simplification.
    virtual uint8_t port_read(uint16_t port) = 0;
    virtual void port_write(uint16_t port, uint8_t value) = 0;
};

class Z80 {
public:
    explicit Z80(Bus& bus) : bus_(bus) {}

    Registers& regs() { return regs_; }
    const Registers& regs() const { return regs_; }

    // Fetches and executes exactly one (unprefixed) instruction. Throws
    // UnimplementedOpcode for anything in the not-yet-implemented list
    // above.
    void step();

private:
    Bus& bus_;
    Registers regs_{};

    uint8_t fetch8();
    uint16_t fetch16();
    uint16_t read16(uint16_t address);
    void write16(uint16_t address, uint16_t value);

    bool flag(Flag f) const { return (regs_.f & f) != 0; }
    void set_flag(Flag f, bool value);

    uint16_t bc() const { return static_cast<uint16_t>((regs_.b << 8) | regs_.c); }
    uint16_t de() const { return static_cast<uint16_t>((regs_.d << 8) | regs_.e); }
    uint16_t hl() const { return static_cast<uint16_t>((regs_.h << 8) | regs_.l); }
    uint16_t af() const { return static_cast<uint16_t>((regs_.a << 8) | regs_.f); }
    void set_bc(uint16_t value);
    void set_de(uint16_t value);
    void set_hl(uint16_t value);
    void set_af(uint16_t value);

    // 8-bit register access by the standard 3-bit encoding used
    // throughout the unprefixed opcode map: 0=B,1=C,2=D,3=E,4=H,5=L,
    // 6=(HL) [a memory operand, not a register], 7=A.
    uint8_t read_r(int index);
    void write_r(int index, uint8_t value);

    // 16-bit register-pair access by the 2-bit "dd" encoding (used by
    // LD dd,nn and 16-bit INC/DEC): 0=BC,1=DE,2=HL,3=SP. Note this
    // differs from the "qq" encoding PUSH/POP use, where 3=AF instead of
    // SP -- a real, easy-to-miss Z80 quirk (the same 2-bit pattern means
    // different things in the two instruction classes), so push_qq/pop_qq
    // are kept separate from dd's get/set below rather than sharing code.
    uint16_t get_dd(int index);
    void set_dd(int index, uint16_t value);

    // Shared ALU core for the ADD/ADC/SUB/SBC/CP family: computes the
    // 8-bit result and sets every flag, but leaves storing the result
    // into A to the caller (CP needs the flags without the store).
    uint8_t add_core(uint8_t value, bool with_carry);
    uint8_t sub_core(uint8_t value, bool with_carry);

    // ADD HL,ss: unlike every other add/subtract in this core, leaves
    // S/Z/PV completely untouched -- a real, well-known Z80 quirk (16-bit
    // ADC/SBC HL,ss, once implemented, DO set them; this is specifically
    // plain ADD's own exception). Only C/H/N and Y/X (mirrored from the
    // result's high byte) are affected.
    uint16_t add16(uint16_t a, uint16_t b);

    // ED-prefixed ADC HL,ss / SBC HL,ss: unlike add16 above, these DO set
    // S/Z/PV (16-bit versions of add_core/sub_core's flag formulas).
    uint16_t adc16(uint16_t a, uint16_t b);
    uint16_t sbc16(uint16_t a, uint16_t b);

    void and_a(uint8_t value);
    void or_a(uint8_t value);
    void xor_a(uint8_t value);
    uint8_t inc8(uint8_t value);
    uint8_t dec8(uint8_t value);

    // Decimal-adjusts A after a preceding 8-bit add/subtract, per the
    // reference's own daa(). A genuine, well-established Z80 quirk kept
    // exactly: the resulting PV flag holds A's PARITY, not signed overflow
    // (DAA repurposes the bit unlike every other arithmetic instruction).
    void daa();

    void push16(uint16_t value);
    uint16_t pop16();

    bool test_condition(int cc);

    // Shared flag computation for the 8 CB-prefixed rotate/shift ops
    // (RLC/RRC/RL/RR/SLA/SRA/SLL/SRL): C <- the bit shifted out, H/N
    // cleared, S/Z/PV(parity)/Y/X from the result -- confirmed uniform
    // across all 8 (unlike the ALU-vs-A group, none of these have
    // per-operation flag quirks).
    void set_rotate_shift_flags(uint8_t result, bool carry_out);
    uint8_t rlc(uint8_t value);
    uint8_t rrc(uint8_t value);
    uint8_t rl(uint8_t value);
    uint8_t rr(uint8_t value);
    uint8_t sla(uint8_t value);
    uint8_t sra(uint8_t value);
    uint8_t sll(uint8_t value); // undocumented ("shift left, fill with 1")
    uint8_t srl(uint8_t value);

    // BIT b,r: sets Z (and PV, mirroring Z -- a well-known BIT-specific
    // quirk) from the tested bit, S only when b=7 and it's set, H always,
    // N never, and leaves C untouched entirely. `yx_source` provides the
    // byte Y/X are mirrored from -- see the header comment on why that's
    // NOT always `value` itself.
    void bit(int b, uint8_t value, uint8_t yx_source);
    uint8_t res_bit(int b, uint8_t value); // RES b,r -- no flags affected
    uint8_t set_bit(int b, uint8_t value); // SET b,r -- no flags affected

    void execute_cb(uint8_t opcode);

    // LDI/LDD (and, by repetition, LDIR/LDDR): copies (HL)->(DE), then
    // steps HL/DE by +1 (increment=true) or -1, and decrements BC. See
    // the header comment for the Y/X flag rule this uses (derived from
    // transferred_byte + A, not the transfer itself).
    void block_ld(bool increment);

    // CPI/CPD (and, by repetition, CPIR/CPDR): compares A against (HL)
    // like a CP (S/Z/H set accordingly, C left untouched), then steps HL
    // by +1/-1 and decrements BC. See the header comment for the Y/X flag
    // rule (derived from the comparison result adjusted by its own
    // half-carry).
    void block_cp(bool increment);

    void execute_ed(uint8_t opcode);
    void execute(uint8_t opcode);
};

} // namespace cpu::z80

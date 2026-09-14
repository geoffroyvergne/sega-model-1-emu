#include "cpu/z80/z80.h"

#include <utility>

namespace cpu::z80 {

namespace {
bool parity_even(uint8_t value) {
    value ^= value >> 4;
    value ^= value >> 2;
    value ^= value >> 1;
    return (value & 1) == 0;
}

// Undocumented Y/X flags mirror bits 5/3 of whatever the instruction's
// "result" is (the meaning of "result" varies by instruction -- for most
// 8-bit ops it's the 8-bit result itself, confirmed the standard/ZEXALL-
// consistent convention).
uint8_t yx_bits(uint8_t result) { return result & (kFlagY | kFlagX); }
} // namespace

UnimplementedOpcode::UnimplementedOpcode(uint8_t opcode)
    : std::runtime_error("Z80: unimplemented opcode"), opcode_(opcode) {}

void Z80::set_flag(Flag f, bool value) {
    if (value) {
        regs_.f |= f;
    } else {
        regs_.f &= static_cast<uint8_t>(~f);
    }
}

void Z80::set_bc(uint16_t value) {
    regs_.b = static_cast<uint8_t>(value >> 8);
    regs_.c = static_cast<uint8_t>(value & 0xff);
}

void Z80::set_de(uint16_t value) {
    regs_.d = static_cast<uint8_t>(value >> 8);
    regs_.e = static_cast<uint8_t>(value & 0xff);
}

void Z80::set_hl(uint16_t value) {
    regs_.h = static_cast<uint8_t>(value >> 8);
    regs_.l = static_cast<uint8_t>(value & 0xff);
}

void Z80::set_af(uint16_t value) {
    regs_.a = static_cast<uint8_t>(value >> 8);
    regs_.f = static_cast<uint8_t>(value & 0xff);
}

uint8_t Z80::fetch8() { return bus_.read8(regs_.pc++); }

uint16_t Z80::fetch16() {
    const uint8_t lo = fetch8();
    const uint8_t hi = fetch8();
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint16_t Z80::read16(uint16_t address) {
    const uint8_t lo = bus_.read8(address);
    const uint8_t hi = bus_.read8(static_cast<uint16_t>(address + 1));
    return static_cast<uint16_t>((hi << 8) | lo);
}

void Z80::write16(uint16_t address, uint16_t value) {
    bus_.write8(address, static_cast<uint8_t>(value & 0xff));
    bus_.write8(static_cast<uint16_t>(address + 1), static_cast<uint8_t>(value >> 8));
}

uint8_t Z80::read_r(int index) {
    switch (index) {
    case 0:
        return regs_.b;
    case 1:
        return regs_.c;
    case 2:
        return regs_.d;
    case 3:
        return regs_.e;
    case 4:
        return regs_.h;
    case 5:
        return regs_.l;
    case 6:
        return bus_.read8(hl());
    default:
        return regs_.a;
    }
}

void Z80::write_r(int index, uint8_t value) {
    switch (index) {
    case 0:
        regs_.b = value;
        break;
    case 1:
        regs_.c = value;
        break;
    case 2:
        regs_.d = value;
        break;
    case 3:
        regs_.e = value;
        break;
    case 4:
        regs_.h = value;
        break;
    case 5:
        regs_.l = value;
        break;
    case 6:
        bus_.write8(hl(), value);
        break;
    default:
        regs_.a = value;
        break;
    }
}

uint16_t Z80::get_dd(int index) {
    switch (index) {
    case 0:
        return bc();
    case 1:
        return de();
    case 2:
        return hl();
    default:
        return regs_.sp;
    }
}

void Z80::set_dd(int index, uint16_t value) {
    switch (index) {
    case 0:
        set_bc(value);
        break;
    case 1:
        set_de(value);
        break;
    case 2:
        set_hl(value);
        break;
    default:
        regs_.sp = value;
        break;
    }
}

// Confirmed against the standard/ZEXALL-consistent Z80 ALU flag formulas
// (half-carry: carry out of bit 3; overflow: both operands share a sign
// that the result doesn't).
uint8_t Z80::add_core(uint8_t value, bool with_carry) {
    const uint8_t carry_in = (with_carry && flag(kFlagC)) ? 1 : 0;
    const int result = regs_.a + value + carry_in;
    const uint8_t result8 = static_cast<uint8_t>(result);

    set_flag(kFlagC, result > 0xff);
    set_flag(kFlagN, false);
    set_flag(kFlagPV, ((~(regs_.a ^ value)) & (regs_.a ^ result8) & 0x80) != 0);
    set_flag(kFlagH, ((regs_.a & 0xf) + (value & 0xf) + carry_in) > 0xf);
    set_flag(kFlagZ, result8 == 0);
    set_flag(kFlagS, (result8 & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result8));
    return result8;
}

// Confirmed against the well-known ADD HL,ss quirk: S/Z/PV are left
// completely untouched (unlike every 8-bit ALU op, and unlike the
// 16-bit ADC/SBC HL,ss forms); only C/H/N and Y/X (from the result's high
// byte) are affected.
uint16_t Z80::add16(uint16_t a, uint16_t b) {
    const uint32_t result = static_cast<uint32_t>(a) + b;
    const uint16_t result16 = static_cast<uint16_t>(result);

    set_flag(kFlagC, result > 0xffff);
    set_flag(kFlagH, ((a & 0xfff) + (b & 0xfff)) > 0xfff);
    set_flag(kFlagN, false);
    const uint8_t high = static_cast<uint8_t>(result16 >> 8);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(high));
    return result16;
}

// ED-prefixed 16-bit versions of add_core/sub_core -- unlike add16 above,
// these DO set S/Z/PV, confirmed against the standard formulas (the same
// shape as the 8-bit ones, widened to 16 bits).
uint16_t Z80::adc16(uint16_t a, uint16_t b) {
    const uint32_t carry_in = flag(kFlagC) ? 1 : 0;
    const uint32_t result = static_cast<uint32_t>(a) + b + carry_in;
    const uint16_t result16 = static_cast<uint16_t>(result);

    set_flag(kFlagC, result > 0xffff);
    set_flag(kFlagH, ((a & 0xfff) + (b & 0xfff) + carry_in) > 0xfff);
    set_flag(kFlagN, false);
    set_flag(kFlagPV, ((~(a ^ b)) & (a ^ result16) & 0x8000) != 0);
    set_flag(kFlagZ, result16 == 0);
    set_flag(kFlagS, (result16 & 0x8000) != 0);
    const uint8_t high = static_cast<uint8_t>(result16 >> 8);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(high));
    return result16;
}

uint16_t Z80::sbc16(uint16_t a, uint16_t b) {
    const uint32_t carry_in = flag(kFlagC) ? 1 : 0;
    const int32_t result = static_cast<int32_t>(a) - b - carry_in;
    const uint16_t result16 = static_cast<uint16_t>(result);

    set_flag(kFlagC, result < 0);
    set_flag(kFlagH, (static_cast<int32_t>(a & 0xfff) - (b & 0xfff) - carry_in) < 0);
    set_flag(kFlagN, true);
    set_flag(kFlagPV, ((a ^ b) & (a ^ result16) & 0x8000) != 0);
    set_flag(kFlagZ, result16 == 0);
    set_flag(kFlagS, (result16 & 0x8000) != 0);
    const uint8_t high = static_cast<uint8_t>(result16 >> 8);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(high));
    return result16;
}

uint8_t Z80::sub_core(uint8_t value, bool with_carry) {
    const uint8_t carry_in = (with_carry && flag(kFlagC)) ? 1 : 0;
    const int result = regs_.a - value - carry_in;
    const uint8_t result8 = static_cast<uint8_t>(result);

    set_flag(kFlagC, result < 0);
    set_flag(kFlagN, true);
    set_flag(kFlagPV, ((regs_.a ^ value) & (regs_.a ^ result8) & 0x80) != 0);
    set_flag(kFlagH, (static_cast<int>(regs_.a & 0xf) - (value & 0xf) - carry_in) < 0);
    set_flag(kFlagZ, result8 == 0);
    set_flag(kFlagS, (result8 & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result8));
    return result8;
}

void Z80::and_a(uint8_t value) {
    const uint8_t result = static_cast<uint8_t>(regs_.a & value);
    regs_.a = result;
    set_flag(kFlagC, false);
    set_flag(kFlagN, false);
    set_flag(kFlagH, true);
    set_flag(kFlagPV, parity_even(result));
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagS, (result & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result));
}

void Z80::or_a(uint8_t value) {
    const uint8_t result = static_cast<uint8_t>(regs_.a | value);
    regs_.a = result;
    set_flag(kFlagC, false);
    set_flag(kFlagN, false);
    set_flag(kFlagH, false);
    set_flag(kFlagPV, parity_even(result));
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagS, (result & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result));
}

void Z80::xor_a(uint8_t value) {
    const uint8_t result = static_cast<uint8_t>(regs_.a ^ value);
    regs_.a = result;
    set_flag(kFlagC, false);
    set_flag(kFlagN, false);
    set_flag(kFlagH, false);
    set_flag(kFlagPV, parity_even(result));
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagS, (result & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result));
}

// INC/DEC leave the carry flag untouched -- a real, easy-to-miss Z80
// quirk, confirmed standard behavior (not an oversight here).
uint8_t Z80::inc8(uint8_t value) {
    const uint8_t result = static_cast<uint8_t>(value + 1);
    set_flag(kFlagN, false);
    set_flag(kFlagPV, value == 0x7f);
    set_flag(kFlagH, (value & 0xf) == 0xf);
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagS, (result & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result));
    return result;
}

uint8_t Z80::dec8(uint8_t value) {
    const uint8_t result = static_cast<uint8_t>(value - 1);
    set_flag(kFlagN, true);
    set_flag(kFlagPV, value == 0x80);
    set_flag(kFlagH, (value & 0xf) == 0);
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagS, (result & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result));
    return result;
}

// Confirmed against the reference's daa(): the adjustment amount depends
// on N (which family the preceding op belonged to), H, and whether the
// low/whole byte overflowed a decimal digit/pair; the new H is the XOR of
// the pre- and post-adjustment A (bit 4); N itself is left untouched; and
// PV ends up holding the adjusted result's PARITY, not signed overflow --
// a genuine, well-established quirk specific to this one instruction.
void Z80::daa() {
    const uint8_t original_a = regs_.a;
    uint8_t a = original_a;
    const bool subtract = flag(kFlagN);
    const bool half_carry = flag(kFlagH);
    const bool carry = flag(kFlagC);

    if (subtract) {
        if (half_carry || (original_a & 0xf) > 9) a = static_cast<uint8_t>(a - 6);
        if (carry || original_a > 0x99) a = static_cast<uint8_t>(a - 0x60);
    } else {
        if (half_carry || (original_a & 0xf) > 9) a = static_cast<uint8_t>(a + 6);
        if (carry || original_a > 0x99) a = static_cast<uint8_t>(a + 0x60);
    }

    set_flag(kFlagC, carry || original_a > 0x99);
    set_flag(kFlagH, ((original_a ^ a) & kFlagH) != 0);
    set_flag(kFlagZ, a == 0);
    set_flag(kFlagS, (a & 0x80) != 0);
    set_flag(kFlagPV, parity_even(a));
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(a));
    regs_.a = a;
}

void Z80::push16(uint16_t value) {
    regs_.sp = static_cast<uint16_t>(regs_.sp - 1);
    bus_.write8(regs_.sp, static_cast<uint8_t>(value >> 8));
    regs_.sp = static_cast<uint16_t>(regs_.sp - 1);
    bus_.write8(regs_.sp, static_cast<uint8_t>(value & 0xff));
}

uint16_t Z80::pop16() {
    const uint8_t lo = bus_.read8(regs_.sp);
    regs_.sp = static_cast<uint16_t>(regs_.sp + 1);
    const uint8_t hi = bus_.read8(regs_.sp);
    regs_.sp = static_cast<uint16_t>(regs_.sp + 1);
    return static_cast<uint16_t>((hi << 8) | lo);
}

// Standard Z80 3-bit condition-code encoding: 0=NZ,1=Z,2=NC,3=C,4=PO,
// 5=PE,6=P,7=M. Used by conditional JP/CALL/RET; JR only ever encodes the
// first 4 of these.
bool Z80::test_condition(int cc) {
    switch (cc) {
    case 0:
        return !flag(kFlagZ);
    case 1:
        return flag(kFlagZ);
    case 2:
        return !flag(kFlagC);
    case 3:
        return flag(kFlagC);
    case 4:
        return !flag(kFlagPV);
    case 5:
        return flag(kFlagPV);
    case 6:
        return !flag(kFlagS);
    default:
        return flag(kFlagS);
    }
}

void Z80::set_rotate_shift_flags(uint8_t result, bool carry_out) {
    set_flag(kFlagC, carry_out);
    set_flag(kFlagH, false);
    set_flag(kFlagN, false);
    set_flag(kFlagPV, parity_even(result));
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagS, (result & 0x80) != 0);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(result));
}

uint8_t Z80::rlc(uint8_t value) {
    const bool carry_out = (value & 0x80) != 0;
    const uint8_t result = static_cast<uint8_t>((value << 1) | (carry_out ? 1 : 0));
    set_rotate_shift_flags(result, carry_out);
    return result;
}

uint8_t Z80::rrc(uint8_t value) {
    const bool carry_out = (value & 0x01) != 0;
    const uint8_t result = static_cast<uint8_t>((value >> 1) | (carry_out ? 0x80 : 0));
    set_rotate_shift_flags(result, carry_out);
    return result;
}

uint8_t Z80::rl(uint8_t value) {
    const bool carry_out = (value & 0x80) != 0;
    const uint8_t result = static_cast<uint8_t>((value << 1) | (flag(kFlagC) ? 1 : 0));
    set_rotate_shift_flags(result, carry_out);
    return result;
}

uint8_t Z80::rr(uint8_t value) {
    const bool carry_out = (value & 0x01) != 0;
    const uint8_t result = static_cast<uint8_t>((value >> 1) | (flag(kFlagC) ? 0x80 : 0));
    set_rotate_shift_flags(result, carry_out);
    return result;
}

uint8_t Z80::sla(uint8_t value) {
    const bool carry_out = (value & 0x80) != 0;
    const uint8_t result = static_cast<uint8_t>(value << 1);
    set_rotate_shift_flags(result, carry_out);
    return result;
}

// Arithmetic: bit 7 (the sign) is preserved rather than zero-filled.
uint8_t Z80::sra(uint8_t value) {
    const bool carry_out = (value & 0x01) != 0;
    const uint8_t result = static_cast<uint8_t>((value >> 1) | (value & 0x80));
    set_rotate_shift_flags(result, carry_out);
    return result;
}

// Undocumented: shifts left like SLA but fills bit 0 with 1 instead of 0
// (sometimes called SL1). Confirmed as a real, implemented-everywhere
// undocumented opcode (MAME itself declares `u8 sll(u8 value)` in
// z80.h), not a guess.
uint8_t Z80::sll(uint8_t value) {
    const bool carry_out = (value & 0x80) != 0;
    const uint8_t result = static_cast<uint8_t>((value << 1) | 1);
    set_rotate_shift_flags(result, carry_out);
    return result;
}

uint8_t Z80::srl(uint8_t value) {
    const bool carry_out = (value & 0x01) != 0;
    const uint8_t result = static_cast<uint8_t>(value >> 1);
    set_rotate_shift_flags(result, carry_out);
    return result;
}

// Confirmed against the well-established BIT semantics: PV mirrors Z
// (not overflow/parity of anything), S only reflects bit 7's own state,
// H is always set, N always cleared, and C is left completely untouched.
// yx_source -- see the header comment on why BIT b,(HL) passes HL+1's
// high byte here instead of the tested value itself.
void Z80::bit(int b, uint8_t value, uint8_t yx_source) {
    const bool bit_set = (value & (1 << b)) != 0;
    set_flag(kFlagZ, !bit_set);
    set_flag(kFlagPV, !bit_set);
    set_flag(kFlagS, b == 7 && bit_set);
    set_flag(kFlagH, true);
    set_flag(kFlagN, false);
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(yx_source));
}

uint8_t Z80::res_bit(int b, uint8_t value) { return static_cast<uint8_t>(value & ~(1 << b)); }
uint8_t Z80::set_bit(int b, uint8_t value) { return static_cast<uint8_t>(value | (1 << b)); }

// Confirmed against the standard CB-prefix encoding: bits 7-6 select the
// group (0=rotate/shift, 1=BIT, 2=RES, 3=SET), bits 5-3 select the
// operation (rotate/shift group) or bit number (BIT/RES/SET groups), and
// bits 2-0 select the operand via the same 3-bit register encoding as the
// unprefixed opcode map's read_r/write_r.
void Z80::execute_cb(uint8_t opcode) {
    const int group = (opcode >> 6) & 3;
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;

    if (group == 0) {
        uint8_t value = read_r(z);
        switch (y) {
        case 0:
            value = rlc(value);
            break;
        case 1:
            value = rrc(value);
            break;
        case 2:
            value = rl(value);
            break;
        case 3:
            value = rr(value);
            break;
        case 4:
            value = sla(value);
            break;
        case 5:
            value = sra(value);
            break;
        case 6:
            value = sll(value);
            break;
        default:
            value = srl(value);
            break;
        }
        write_r(z, value);
        return;
    }

    if (group == 1) { // BIT y,r
        const uint8_t value = read_r(z);
        // BIT b,(HL)'s Y/X flags come from HL+1's high byte (an internal
        // address-latch artifact), not the tested byte -- see the header
        // comment. Every other operand (a plain register) mirrors itself.
        const uint8_t yx_source = (z == 6) ? static_cast<uint8_t>((hl() + 1) >> 8) : value;
        bit(y, value, yx_source);
        return;
    }

    const uint8_t value = read_r(z);
    write_r(z, group == 2 ? res_bit(y, value) : set_bit(y, value)); // RES / SET
}

// Confirmed against the well-established (not disputed the way SCF/CCF's
// Q-register is) block-instruction Y/X rule: derived from
// transferred_byte + A, truncated to 8 bits -- bit 1 of that sum becomes
// Y, bit 3 becomes X. S/Z/C are left completely untouched.
void Z80::block_ld(bool increment) {
    const uint8_t value = bus_.read8(hl());
    bus_.write8(de(), value);
    set_hl(static_cast<uint16_t>(hl() + (increment ? 1 : -1)));
    set_de(static_cast<uint16_t>(de() + (increment ? 1 : -1)));
    set_bc(static_cast<uint16_t>(bc() - 1));

    set_flag(kFlagH, false);
    set_flag(kFlagN, false);
    set_flag(kFlagPV, bc() != 0);
    const uint8_t n = static_cast<uint8_t>(value + regs_.a);
    const uint8_t yx = static_cast<uint8_t>(((n & 0x02) << 4) | (n & 0x08));
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx);
}

// Confirmed against the well-established block-compare Y/X rule: derived
// from the CP-style subtraction result, minus 1 more if that subtraction
// itself half-carried, truncated to 8 bits -- same bit1->Y/bit3->X
// extraction as block_ld's rule, but from a different intermediate value.
// C is left completely untouched (unlike a plain CP, which also leaves it
// untouched -- this one's consistent with that, not an exception to it).
void Z80::block_cp(bool increment) {
    const uint8_t value = bus_.read8(hl());
    const uint8_t result = static_cast<uint8_t>(regs_.a - value);
    const bool half_carry = (regs_.a & 0xf) < (value & 0xf);

    set_hl(static_cast<uint16_t>(hl() + (increment ? 1 : -1)));
    set_bc(static_cast<uint16_t>(bc() - 1));

    set_flag(kFlagS, (result & 0x80) != 0);
    set_flag(kFlagZ, result == 0);
    set_flag(kFlagH, half_carry);
    set_flag(kFlagN, true);
    set_flag(kFlagPV, bc() != 0);

    const uint8_t n = static_cast<uint8_t>(half_carry ? result - 1 : result);
    const uint8_t yx = static_cast<uint8_t>(((n & 0x02) << 4) | (n & 0x08));
    regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx);
}

// Confirmed against the standard ED-prefix opcode assignments. Only the
// canonical opcode is handled for instructions with documented duplicate
// encodings (IM, RETN/RETI) -- see the header comment.
void Z80::execute_ed(uint8_t opcode) {
    switch (opcode) {
    // ADC HL,ss / SBC HL,ss -- 01 ss 1010 / 01 ss 0010, same dd-style
    // register-pair encoding as LD dd,nn (see get_dd's comment).
    case 0x4a:
    case 0x5a:
    case 0x6a:
    case 0x7a:
        set_hl(adc16(hl(), get_dd((opcode >> 4) & 3)));
        return;
    case 0x42:
    case 0x52:
    case 0x62:
    case 0x72:
        set_hl(sbc16(hl(), get_dd((opcode >> 4) & 3)));
        return;

    // LD (nn),dd / LD dd,(nn) -- 01 dd 0011 / 01 dd 1011.
    case 0x43:
    case 0x53:
    case 0x63:
    case 0x73: {
        const uint16_t addr = fetch16();
        write16(addr, get_dd((opcode >> 4) & 3));
        return;
    }
    case 0x4b:
    case 0x5b:
    case 0x6b:
    case 0x7b: {
        const uint16_t addr = fetch16();
        set_dd((opcode >> 4) & 3, read16(addr));
        return;
    }

    case 0x44: { // NEG -- confirmed against the reference's own neg(): A = 0 - A, reusing sub_core's flags.
        const uint8_t old_a = regs_.a;
        regs_.a = 0;
        regs_.a = sub_core(old_a, false);
        return;
    }

    case 0x57: // LD A,I
        regs_.a = regs_.i;
        set_flag(kFlagS, (regs_.a & 0x80) != 0);
        set_flag(kFlagZ, regs_.a == 0);
        set_flag(kFlagH, false);
        set_flag(kFlagPV, regs_.iff2);
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    case 0x5f: // LD A,R
        regs_.a = regs_.r;
        set_flag(kFlagS, (regs_.a & 0x80) != 0);
        set_flag(kFlagZ, regs_.a == 0);
        set_flag(kFlagH, false);
        set_flag(kFlagPV, regs_.iff2);
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    case 0x47: // LD I,A -- no flags affected
        regs_.i = regs_.a;
        return;
    case 0x4f: // LD R,A -- no flags affected
        regs_.r = regs_.a;
        return;

    // RRD/RLD -- 4-bit nibble rotation across A's low nibble and both
    // nibbles of (HL). Confirmed against the standard definitions (see
    // the header comment for which nibble goes where).
    case 0x67: { // RRD
        const uint8_t mem = bus_.read8(hl());
        const uint8_t a_low = regs_.a & 0x0f;
        const uint8_t new_a = static_cast<uint8_t>((regs_.a & 0xf0) | (mem & 0x0f));
        const uint8_t new_mem = static_cast<uint8_t>(((mem >> 4) & 0x0f) | (a_low << 4));
        bus_.write8(hl(), new_mem);
        regs_.a = new_a;
        set_flag(kFlagS, (regs_.a & 0x80) != 0);
        set_flag(kFlagZ, regs_.a == 0);
        set_flag(kFlagH, false);
        set_flag(kFlagPV, parity_even(regs_.a));
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    }
    case 0x6f: { // RLD
        const uint8_t mem = bus_.read8(hl());
        const uint8_t a_low = regs_.a & 0x0f;
        const uint8_t new_a = static_cast<uint8_t>((regs_.a & 0xf0) | ((mem >> 4) & 0x0f));
        const uint8_t new_mem = static_cast<uint8_t>(((mem & 0x0f) << 4) | a_low);
        bus_.write8(hl(), new_mem);
        regs_.a = new_a;
        set_flag(kFlagS, (regs_.a & 0x80) != 0);
        set_flag(kFlagZ, regs_.a == 0);
        set_flag(kFlagH, false);
        set_flag(kFlagPV, parity_even(regs_.a));
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    }

    case 0x46: // IM 0
        regs_.im = 0;
        return;
    case 0x56: // IM 1
        regs_.im = 1;
        return;
    case 0x5e: // IM 2
        regs_.im = 2;
        return;

    case 0x45: // RETN
        regs_.pc = pop16();
        regs_.iff1 = regs_.iff2;
        return;
    case 0x4d: // RETI
        regs_.pc = pop16();
        regs_.iff1 = regs_.iff2;
        return;

    case 0xa0: // LDI
        block_ld(true);
        return;
    case 0xa8: // LDD
        block_ld(false);
        return;
    case 0xb0: // LDIR
        do {
            block_ld(true);
        } while (bc() != 0);
        return;
    case 0xb8: // LDDR
        do {
            block_ld(false);
        } while (bc() != 0);
        return;

    case 0xa1: // CPI
        block_cp(true);
        return;
    case 0xa9: // CPD
        block_cp(false);
        return;
    case 0xb1: // CPIR
        do {
            block_cp(true);
        } while (bc() != 0 && !flag(kFlagZ));
        return;
    case 0xb9: // CPDR
        do {
            block_cp(false);
        } while (bc() != 0 && !flag(kFlagZ));
        return;

    // IN r,(C) -- 01 rrr 000. The full BC pair is the port address (not
    // zero-extended C). r==6 (opcode 0x70) is the undocumented "IN (C)"
    // form: sets flags exactly like every other r, but discards the value
    // instead of storing it (there's no "(HL) operand" meaning here the
    // way index 6 has elsewhere -- deliberately NOT routed through
    // read_r/write_r for that reason).
    case 0x40:
    case 0x48:
    case 0x50:
    case 0x58:
    case 0x60:
    case 0x68:
    case 0x70:
    case 0x78: {
        const uint8_t value = bus_.port_read(bc());
        const int r = (opcode >> 3) & 7;
        if (r != 6) write_r(r, value);
        set_flag(kFlagS, (value & 0x80) != 0);
        set_flag(kFlagZ, value == 0);
        set_flag(kFlagH, false);
        set_flag(kFlagPV, parity_even(value));
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(value));
        return;
    }

    // OUT (C),r -- 01 rrr 001. r==6 (opcode 0x71) is the undocumented
    // "OUT (C),0" form: writes a literal 0, not (HL)'s contents.
    case 0x41:
    case 0x49:
    case 0x51:
    case 0x59:
    case 0x61:
    case 0x69:
    case 0x71:
    case 0x79: {
        const int r = (opcode >> 3) & 7;
        const uint8_t value = (r == 6) ? 0 : read_r(r);
        bus_.port_write(bc(), value);
        return;
    }

    default:
        throw UnimplementedOpcode(opcode);
    }
}

void Z80::step() {
    if (regs_.halted) return;
    execute(fetch8());
}

void Z80::execute(uint8_t opcode) {
    // LD r,r' / LD r,(HL) / LD (HL),r -- 01 ddd sss, except 0x76 (both
    // operands (HL)) which is HALT instead.
    if (opcode == 0x76) {
        regs_.halted = true;
        return;
    }
    if ((opcode & 0xc0) == 0x40) {
        const int dst = (opcode >> 3) & 7;
        const int src = opcode & 7;
        write_r(dst, read_r(src));
        return;
    }

    // ADD/ADC/SUB/SBC/AND/XOR/OR/CP A,r -- 10 ooo sss.
    if ((opcode & 0xc0) == 0x80) {
        const int op = (opcode >> 3) & 7;
        const uint8_t value = read_r(opcode & 7);
        switch (op) {
        case 0:
            regs_.a = add_core(value, false);
            return;
        case 1:
            regs_.a = add_core(value, true);
            return;
        case 2:
            regs_.a = sub_core(value, false);
            return;
        case 3:
            regs_.a = sub_core(value, true);
            return;
        case 4:
            and_a(value);
            return;
        case 5:
            xor_a(value);
            return;
        case 6:
            or_a(value);
            return;
        default:
            sub_core(value, false); // CP: flags only, discard the result
            return;
        }
    }

    switch (opcode) {
    case 0x00: // NOP
        return;

    // LD (BC),A / LD A,(BC) / LD (DE),A / LD A,(DE).
    case 0x02:
        bus_.write8(bc(), regs_.a);
        return;
    case 0x0a:
        regs_.a = bus_.read8(bc());
        return;
    case 0x12:
        bus_.write8(de(), regs_.a);
        return;
    case 0x1a:
        regs_.a = bus_.read8(de());
        return;

    // LD (nn),HL / LD HL,(nn) / LD (nn),A / LD A,(nn).
    case 0x22: {
        const uint16_t addr = fetch16();
        write16(addr, hl());
        return;
    }
    case 0x2a: {
        const uint16_t addr = fetch16();
        set_hl(read16(addr));
        return;
    }
    case 0x32: {
        const uint16_t addr = fetch16();
        bus_.write8(addr, regs_.a);
        return;
    }
    case 0x3a: {
        const uint16_t addr = fetch16();
        regs_.a = bus_.read8(addr);
        return;
    }

    // ADD HL,ss -- 00 ss 1001. Confirmed leaving S/Z/PV untouched, unlike
    // every other add/subtract this core implements (see add16's comment).
    case 0x09:
        set_hl(add16(hl(), bc()));
        return;
    case 0x19:
        set_hl(add16(hl(), de()));
        return;
    case 0x29:
        set_hl(add16(hl(), hl()));
        return;
    case 0x39:
        set_hl(add16(hl(), regs_.sp));
        return;

    // Accumulator-only fast rotates -- same bit-level operation as CB's
    // RLC/RRC/RL/RR A, but S/Z/PV are left untouched (unlike the CB
    // forms, which DO set them). Reuses those helpers for the C/H/N/Y/X
    // computation (which the helpers get right for any input including
    // A) and then restores S/Z/PV to their pre-instruction values.
    case 0x07: { // RLCA
        const uint8_t old_f = regs_.f;
        regs_.a = rlc(regs_.a);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagS | kFlagZ | kFlagPV)) |
                                        (old_f & (kFlagS | kFlagZ | kFlagPV)));
        return;
    }
    case 0x0f: { // RRCA
        const uint8_t old_f = regs_.f;
        regs_.a = rrc(regs_.a);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagS | kFlagZ | kFlagPV)) |
                                        (old_f & (kFlagS | kFlagZ | kFlagPV)));
        return;
    }
    case 0x17: { // RLA
        const uint8_t old_f = regs_.f;
        regs_.a = rl(regs_.a);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagS | kFlagZ | kFlagPV)) |
                                        (old_f & (kFlagS | kFlagZ | kFlagPV)));
        return;
    }
    case 0x1f: { // RRA
        const uint8_t old_f = regs_.f;
        regs_.a = rr(regs_.a);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagS | kFlagZ | kFlagPV)) |
                                        (old_f & (kFlagS | kFlagZ | kFlagPV)));
        return;
    }

    // RST p -- pushes PC and jumps to a fixed low-memory vector, derived
    // directly from the opcode's own bits (00 ttt 111 -> target ttt000).
    case 0xc7:
    case 0xcf:
    case 0xd7:
    case 0xdf:
    case 0xe7:
    case 0xef:
    case 0xf7:
    case 0xff:
        push16(regs_.pc);
        regs_.pc = opcode & 0x38;
        return;

    // JP (HL) -- a famous naming trap: this jumps to the *value* held in
    // HL, never dereferencing it as an address the way "JP (nn)" style
    // naming might suggest.
    case 0xe9:
        regs_.pc = hl();
        return;

    case 0xf9: // LD SP,HL
        regs_.sp = hl();
        return;

    // DI/EI. Real hardware delays EI's effect by one instruction before
    // interrupts can actually trigger -- moot for now, since interrupt
    // acceptance isn't implemented at all yet (see the header comment).
    case 0xf3:
        regs_.iff1 = false;
        regs_.iff2 = false;
        return;
    case 0xfb:
        regs_.iff1 = true;
        regs_.iff2 = true;
        return;

    // IN A,(n) / OUT (n),A -- confirmed A rides the address bus's upper
    // byte alongside the immediate n (see Bus::port_read/write's
    // comment), not just n zero-extended. No flags affected.
    case 0xdb: {
        const uint8_t n = fetch8();
        regs_.a = bus_.port_read(static_cast<uint16_t>((regs_.a << 8) | n));
        return;
    }
    case 0xd3: {
        const uint8_t n = fetch8();
        bus_.port_write(static_cast<uint16_t>((regs_.a << 8) | n), regs_.a);
        return;
    }

    // LD r,n / LD (HL),n -- 00 ddd 110.
    case 0x06:
    case 0x0e:
    case 0x16:
    case 0x1e:
    case 0x26:
    case 0x2e:
    case 0x36:
    case 0x3e: {
        const int dst = (opcode >> 3) & 7;
        write_r(dst, fetch8());
        return;
    }

    // LD dd,nn -- 00 dd 0001.
    case 0x01:
    case 0x11:
    case 0x21:
    case 0x31:
        set_dd((opcode >> 4) & 3, fetch16());
        return;

    // INC r / DEC r -- 00 rrr 100 / 00 rrr 101.
    case 0x04:
    case 0x0c:
    case 0x14:
    case 0x1c:
    case 0x24:
    case 0x2c:
    case 0x34:
    case 0x3c: {
        const int r = (opcode >> 3) & 7;
        write_r(r, inc8(read_r(r)));
        return;
    }
    case 0x05:
    case 0x0d:
    case 0x15:
    case 0x1d:
    case 0x25:
    case 0x2d:
    case 0x35:
    case 0x3d: {
        const int r = (opcode >> 3) & 7;
        write_r(r, dec8(read_r(r)));
        return;
    }

    // INC dd / DEC dd -- 00 dd 0011 / 00 dd 1011 (no flags affected).
    case 0x03:
    case 0x13:
    case 0x23:
    case 0x33: {
        const int dd = (opcode >> 4) & 3;
        set_dd(dd, static_cast<uint16_t>(get_dd(dd) + 1));
        return;
    }
    case 0x0b:
    case 0x1b:
    case 0x2b:
    case 0x3b: {
        const int dd = (opcode >> 4) & 3;
        set_dd(dd, static_cast<uint16_t>(get_dd(dd) - 1));
        return;
    }

    // ALU A,n immediate forms -- 11 ooo 110.
    case 0xc6:
        regs_.a = add_core(fetch8(), false);
        return;
    case 0xce:
        regs_.a = add_core(fetch8(), true);
        return;
    case 0xd6:
        regs_.a = sub_core(fetch8(), false);
        return;
    case 0xde:
        regs_.a = sub_core(fetch8(), true);
        return;
    case 0xe6:
        and_a(fetch8());
        return;
    case 0xee:
        xor_a(fetch8());
        return;
    case 0xf6:
        or_a(fetch8());
        return;
    case 0xfe:
        sub_core(fetch8(), false); // CP n: flags only
        return;

    // Unconditional/conditional jumps and DJNZ.
    case 0xc3:
        regs_.pc = fetch16();
        return;
    case 0x18: {
        const int8_t offset = static_cast<int8_t>(fetch8());
        regs_.pc = static_cast<uint16_t>(regs_.pc + offset);
        return;
    }
    case 0x10: { // DJNZ e
        const int8_t offset = static_cast<int8_t>(fetch8());
        regs_.b = static_cast<uint8_t>(regs_.b - 1);
        if (regs_.b != 0) regs_.pc = static_cast<uint16_t>(regs_.pc + offset);
        return;
    }
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38: { // JR cc,e (cc in {NZ,Z,NC,C} only)
        const int cc = (opcode >> 3) & 3;
        const int8_t offset = static_cast<int8_t>(fetch8());
        if (test_condition(cc)) regs_.pc = static_cast<uint16_t>(regs_.pc + offset);
        return;
    }
    case 0xc2:
    case 0xca:
    case 0xd2:
    case 0xda:
    case 0xe2:
    case 0xea:
    case 0xf2:
    case 0xfa: { // JP cc,nn
        const int cc = (opcode >> 3) & 7;
        const uint16_t target = fetch16();
        if (test_condition(cc)) regs_.pc = target;
        return;
    }

    // Calls and returns.
    case 0xcd: {
        const uint16_t target = fetch16();
        push16(regs_.pc);
        regs_.pc = target;
        return;
    }
    case 0xc9:
        regs_.pc = pop16();
        return;
    case 0xc4:
    case 0xcc:
    case 0xd4:
    case 0xdc:
    case 0xe4:
    case 0xec:
    case 0xf4:
    case 0xfc: { // CALL cc,nn
        const int cc = (opcode >> 3) & 7;
        const uint16_t target = fetch16();
        if (test_condition(cc)) {
            push16(regs_.pc);
            regs_.pc = target;
        }
        return;
    }
    case 0xc0:
    case 0xc8:
    case 0xd0:
    case 0xd8:
    case 0xe0:
    case 0xe8:
    case 0xf0:
    case 0xf8: { // RET cc
        const int cc = (opcode >> 3) & 7;
        if (test_condition(cc)) regs_.pc = pop16();
        return;
    }

    // PUSH/POP -- "qq" encoding, where 11 means AF (unlike "dd", where 11
    // means SP -- see get_dd/set_dd's header comment).
    case 0xc5:
        push16(bc());
        return;
    case 0xd5:
        push16(de());
        return;
    case 0xe5:
        push16(hl());
        return;
    case 0xf5:
        push16(af());
        return;
    case 0xc1:
        set_bc(pop16());
        return;
    case 0xd1:
        set_de(pop16());
        return;
    case 0xe1:
        set_hl(pop16());
        return;
    case 0xf1:
        set_af(pop16());
        return;

    // Exchange instructions -- pure register swaps, no flag/ALU effects
    // of their own (EX AF,AF' does change F's *value*, since it's swapped
    // in wholesale, but nothing here computes a new flag result).
    case 0x08: { // EX AF,AF'
        std::swap(regs_.a, regs_.a2);
        std::swap(regs_.f, regs_.f2);
        return;
    }
    case 0xeb: { // EX DE,HL
        std::swap(regs_.d, regs_.h);
        std::swap(regs_.e, regs_.l);
        return;
    }
    case 0xd9: { // EXX -- BC/DE/HL only, NOT AF (unlike EX AF,AF' above).
        std::swap(regs_.b, regs_.b2);
        std::swap(regs_.c, regs_.c2);
        std::swap(regs_.d, regs_.d2);
        std::swap(regs_.e, regs_.e2);
        std::swap(regs_.h, regs_.h2);
        std::swap(regs_.l, regs_.l2);
        return;
    }
    case 0xe3: { // EX (SP),HL
        const uint16_t tmp = read16(regs_.sp);
        write16(regs_.sp, hl());
        set_hl(tmp);
        return;
    }

    // CPL -- bitwise complement of A. Sets H/N, mirrors Y/X from the new
    // A, leaves S/Z/PV/C untouched.
    case 0x2f: {
        regs_.a = static_cast<uint8_t>(~regs_.a);
        set_flag(kFlagH, true);
        set_flag(kFlagN, true);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    }

    case 0x27: // DAA
        daa();
        return;

    // SCF/CCF -- see the header comment for the Y/X approximation both
    // use.
    case 0x37: // SCF
        set_flag(kFlagC, true);
        set_flag(kFlagH, false);
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    case 0x3f: { // CCF: H becomes the old carry, then carry is inverted.
        const bool old_carry = flag(kFlagC);
        set_flag(kFlagH, old_carry);
        set_flag(kFlagC, !old_carry);
        set_flag(kFlagN, false);
        regs_.f = static_cast<uint8_t>((regs_.f & ~(kFlagY | kFlagX)) | yx_bits(regs_.a));
        return;
    }

    case 0xcb:
        execute_cb(fetch8());
        return;

    case 0xed:
        execute_ed(fetch8());
        return;

    default:
        throw UnimplementedOpcode(opcode);
    }
}

} // namespace cpu::z80

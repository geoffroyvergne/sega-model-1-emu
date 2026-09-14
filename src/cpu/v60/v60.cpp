#include "cpu/v60/v60.h"

namespace model1::cpu::v60 {

UnimplementedOpcode::UnimplementedOpcode(uint32_t pc, uint8_t opcode)
    : std::runtime_error("unimplemented V60 opcode"), pc(pc), opcode(opcode) {}

UnimplementedAddressingMode::UnimplementedAddressingMode(uint32_t pc, uint8_t modifier)
    : std::runtime_error("unimplemented V60 addressing mode"), pc(pc), modifier(modifier) {}

void Cpu::reset(uint32_t start_pc) {
    regs_.fill(0);
    pc_ = start_pc;
    flags_ = Flags{};
}

uint32_t Cpu::read_sized(uint32_t value, Dim dim) const {
    switch (dim) {
        case Dim::Byte: return static_cast<uint8_t>(value);
        case Dim::Word: return static_cast<uint16_t>(value);
        case Dim::Long: return value;
    }
    return value;
}

// Byte/word writes preserve the untouched upper bits of the 32-bit
// register slot (matches the real hardware's SETREG8/SETREG16 behavior).
void Cpu::write_sized(uint32_t& dest, Dim dim, uint32_t value) const {
    switch (dim) {
        case Dim::Byte: dest = (dest & ~0xffu) | (value & 0xffu); break;
        case Dim::Word: dest = (dest & ~0xffffu) | (value & 0xffffu); break;
        case Dim::Long: dest = value; break;
    }
}

uint32_t Cpu::dim_bytes(Dim dim) {
    switch (dim) {
        case Dim::Byte: return 1;
        case Dim::Word: return 2;
        case Dim::Long: return 4;
    }
    return 1;
}

uint32_t Cpu::read_operand(const Operand& op, Dim dim) {
    switch (op.kind) {
        case Operand::Kind::Register:
            return read_sized(regs_[op.reg], dim);
        case Operand::Kind::Immediate:
            return op.value; // already the right width; decoded at that size
        case Operand::Kind::Memory:
            switch (dim) {
                case Dim::Byte: return bus_.read8(op.address);
                case Dim::Word: return bus_.read16(op.address);
                case Dim::Long: return bus_.read32(op.address);
            }
            break;
    }
    return 0;
}

void Cpu::write_operand(const Operand& op, Dim dim, uint32_t value) {
    switch (op.kind) {
        case Operand::Kind::Register:
            write_sized(regs_[op.reg], dim, value);
            return;
        case Operand::Kind::Immediate:
            // No real hardware meaning -- an immediate is a literal, not a
            // location. A real ROM should never encode this.
            throw UnimplementedAddressingMode(pc_, 0);
        case Operand::Kind::Memory:
            switch (dim) {
                case Dim::Byte: bus_.write8(op.address, static_cast<uint8_t>(value)); return;
                case Dim::Word: bus_.write16(op.address, static_cast<uint16_t>(value)); return;
                case Dim::Long: bus_.write32(op.address, value); return;
            }
            return;
    }
}

uint32_t Cpu::operand_as_address(const Operand& op, uint32_t opcode_pc, uint8_t modifier_byte) {
    switch (op.kind) {
        case Operand::Kind::Memory: return op.address;
        case Operand::Kind::Immediate: return op.value; // "jump to this literal address"
        case Operand::Kind::Register: throw UnimplementedAddressingMode(opcode_pc, modifier_byte);
    }
    return 0;
}

// See the header comment for the (modm, top-3-bits) -> mode table this
// implements. `modm` distinguishes two otherwise-identically-indexed
// 8-entry addressing-mode tables -- getting this wrong (checking only the
// modifier byte, as an earlier version of this file did) silently
// misinterprets register-direct as an unrelated mode. See
// docs/hardware-notes/07-v60-architecture.md.
Cpu::Operand Cpu::decode_general_operand(uint32_t modifier_addr, Dim dim, bool modm, uint8_t& out_length) {
    const uint8_t modifier = bus_.read8(modifier_addr);
    const uint8_t index = modifier >> 5;
    const uint8_t regnum = modifier & 0x1f;
    const uint32_t size = dim_bytes(dim);

    if (modm) {
        switch (index) {
            case 3: // Register direct
                out_length = 1;
                return Operand{Operand::Kind::Register, regnum, 0, 0};
            case 4: { // Autoincrement: read/write at the current address, then bump
                const uint32_t address = regs_[regnum];
                regs_[regnum] += size;
                out_length = 1;
                return Operand{Operand::Kind::Memory, 0, address, 0};
            }
            case 5: { // Autodecrement: bump first, then read/write at the new address
                regs_[regnum] -= size;
                out_length = 1;
                return Operand{Operand::Kind::Memory, 0, regs_[regnum], 0};
            }
            default:
                throw UnimplementedAddressingMode(pc_, modifier);
        }
    } else {
        switch (index) {
            case 0: { // Displacement-8: [reg + sign-extended 8-bit displacement]
                const auto disp = static_cast<int8_t>(bus_.read8(modifier_addr + 1));
                out_length = 2;
                return Operand{Operand::Kind::Memory, 0, regs_[regnum] + static_cast<uint32_t>(disp), 0};
            }
            case 3: // Register indirect: [reg]
                out_length = 1;
                return Operand{Operand::Kind::Memory, 0, regs_[regnum], 0};
            case 7: { // "Group 7" -- sub-decoded by the modifier's low 5 bits
                      // (which is `regnum` here, despite the name -- Group 7
                      // has no register operand). Only the immediate
                      // sub-modes are implemented; see the header comment.
                if (regnum <= 15) { // "Immediate quick": the value IS these bits
                    out_length = 1;
                    return Operand{Operand::Kind::Immediate, 0, 0, static_cast<uint32_t>(regnum)};
                }
                if (regnum == 20) { // Full-width immediate, `dim`-sized, follows
                    switch (dim) {
                        case Dim::Byte: {
                            const uint32_t v = bus_.read8(modifier_addr + 1);
                            out_length = 2;
                            return Operand{Operand::Kind::Immediate, 0, 0, v};
                        }
                        case Dim::Word: {
                            const uint32_t v = bus_.read16(modifier_addr + 1);
                            out_length = 3;
                            return Operand{Operand::Kind::Immediate, 0, 0, v};
                        }
                        case Dim::Long: {
                            const uint32_t v = bus_.read32(modifier_addr + 1);
                            out_length = 5;
                            return Operand{Operand::Kind::Immediate, 0, 0, v};
                        }
                    }
                }
                throw UnimplementedAddressingMode(pc_, modifier);
            }
            default:
                throw UnimplementedAddressingMode(pc_, modifier);
        }
    }
}

Cpu::Format12 Cpu::decode_format12(Dim dim1, Dim dim2) {
    const uint8_t instflags = bus_.read8(pc_ + 1);

    if (instflags & 0x80) {
        // Both operands "general" -- op2's modifier byte position depends
        // on op1's encoded length, needing more plumbing than this
        // increment covers.
        throw UnimplementedAddressingMode(pc_, instflags);
    }

    const bool modm = (instflags & 0x40) != 0;
    uint32_t op1_value;
    Operand op2;
    uint8_t length = 2; // opcode + instflags

    if (instflags & 0x20) {
        // Operand 1 general, operand 2 short.
        uint8_t extra = 0;
        const Operand op1 = decode_general_operand(pc_ + 2, dim1, modm, extra);
        op1_value = read_operand(op1, dim1);
        length += extra;
        op2 = Operand{Operand::Kind::Register, static_cast<uint8_t>(instflags & 0x1f), 0, 0};
    } else {
        // Operand 1 short, operand 2 general.
        op1_value = read_sized(regs_[instflags & 0x1f], dim1);
        uint8_t extra = 0;
        op2 = decode_general_operand(pc_ + 2, dim2, modm, extra);
        length += extra;
    }

    return Format12{op1_value, op2, length};
}

// Same instflags decode as decode_format12, but leaves both operands as
// raw, unread Operands -- see the header comment for why CALL needs this
// instead. Unlike decode_format12, this ALSO handles the "both operands
// general" case (instflags bit 7): CALL's operands can never legitimately
// be a bare register (operand_as_address rejects that), so "one general,
// one short" would make CALL permanently unusable -- whichever operand
// ended up short-form would always throw. Confirmed against the
// reference's F12DecodeOperands bit-7 branch: op2's modifier byte sits
// immediately after op1's (at PC+2+op1's encoded length), and op2's modm
// comes from instflags bit 5 in this branch specifically -- NOT bit 6,
// which is op1's modm here. (decode_format12, used by every value-reading
// instruction so far, does not need this yet and still throws on bit 7.)
Cpu::Format12RawOperands Cpu::decode_format12_raw(Dim dim1, Dim dim2) {
    const uint8_t instflags = bus_.read8(pc_ + 1);
    Operand op1;
    Operand op2;
    uint8_t length = 2; // opcode + instflags

    if (instflags & 0x80) {
        uint8_t extra1 = 0;
        op1 = decode_general_operand(pc_ + 2, dim1, (instflags & 0x40) != 0, extra1);
        uint8_t extra2 = 0;
        op2 = decode_general_operand(pc_ + 2 + extra1, dim2, (instflags & 0x20) != 0, extra2);
        length += extra1 + extra2;
        return Format12RawOperands{op1, op2, length};
    }

    const bool modm = (instflags & 0x40) != 0;
    if (instflags & 0x20) {
        uint8_t extra = 0;
        op1 = decode_general_operand(pc_ + 2, dim1, modm, extra);
        length += extra;
        op2 = Operand{Operand::Kind::Register, static_cast<uint8_t>(instflags & 0x1f), 0, 0};
    } else {
        op1 = Operand{Operand::Kind::Register, static_cast<uint8_t>(instflags & 0x1f), 0, 0};
        uint8_t extra = 0;
        op2 = decode_general_operand(pc_ + 2, dim2, modm, extra);
        length += extra;
    }

    return Format12RawOperands{op1, op2, length};
}

void Cpu::set_szf(Dim dim, uint64_t result) {
    switch (dim) {
        case Dim::Byte:
            flags_.zero = static_cast<uint8_t>(result) == 0;
            flags_.sign = (result & 0x80) != 0;
            break;
        case Dim::Word:
            flags_.zero = static_cast<uint16_t>(result) == 0;
            flags_.sign = (result & 0x8000) != 0;
            break;
        case Dim::Long:
            flags_.zero = static_cast<uint32_t>(result) == 0;
            flags_.sign = (result & 0x80000000u) != 0;
            break;
    }
}

// dst + src, per-size carry/overflow, matching the reference core's
// ADDB/ADDW (plain `unsigned` arithmetic) and ADDL (widened to uint64_t so
// the carry-out bit isn't lost) macros exactly.
void Cpu::set_add_flags(Dim dim, uint64_t result, uint32_t src, uint32_t dst) {
    switch (dim) {
        case Dim::Byte:
            flags_.carry = (result & 0x100) != 0;
            flags_.overflow = ((result ^ src) & (result ^ dst) & 0x80) != 0;
            break;
        case Dim::Word:
            flags_.carry = (result & 0x10000) != 0;
            flags_.overflow = ((result ^ src) & (result ^ dst) & 0x8000) != 0;
            break;
        case Dim::Long:
            flags_.carry = (result & (uint64_t{1} << 32)) != 0;
            flags_.overflow = ((result ^ src) & (result ^ dst) & 0x80000000u) != 0;
            break;
    }
    set_szf(dim, result);
}

// dst - src, mirroring the reference core's SUBB/SUBW/SUBL macros exactly:
// SetOFB_Sub(x=result, y=src, z=dst) expands to (z^y)&(z^x), i.e.
// (dst^src)&(dst^result) -- NOT (src^dst)&(src^result). The first term is
// commutative so that half looks the same either way; the second term is
// not, and getting it backwards (an earlier bug here, caught by
// "CMPB detects signed overflow" in v60_test.cpp) silently drops real
// overflow cases.
void Cpu::set_sub_flags(Dim dim, uint64_t result, uint32_t src, uint32_t dst) {
    switch (dim) {
        case Dim::Byte:
            flags_.carry = (result & 0x100) != 0;
            flags_.overflow = ((dst ^ src) & (dst ^ result) & 0x80) != 0;
            break;
        case Dim::Word:
            flags_.carry = (result & 0x10000) != 0;
            flags_.overflow = ((dst ^ src) & (dst ^ result) & 0x8000) != 0;
            break;
        case Dim::Long:
            flags_.carry = (result & (uint64_t{1} << 32)) != 0;
            flags_.overflow = ((dst ^ src) & (dst ^ result) & 0x80000000u) != 0;
            break;
    }
    set_szf(dim, result);
}

// AND/OR/XOR/NOT flags: overflow always cleared, sign/zero set from the
// result -- but carry is left completely untouched, confirmed against the
// reference core's ANDB/ORB/XORB/NOTB macros (which simply never mention
// _CY). Every arithmetic instruction implemented so far (ADD/SUB/CMP) does
// touch carry, so this is an easy default to assume incorrectly.
void Cpu::set_logical_flags(Dim dim, uint32_t result) {
    flags_.overflow = false;
    set_szf(dim, result);
}

int Cpu::op_halt() {
    // Real hardware waits for an interrupt; there is nothing to wait for
    // yet in this core (matches the reference core's own simplification).
    pc_ += 1;
    return kApproximateCyclesPerInstruction;
}

int Cpu::op_nop() {
    pc_ += 1;
    return kApproximateCyclesPerInstruction;
}

// MOV: operand 1 (source) is copied into operand 2 (destination). Does not
// affect flags.
int Cpu::op_mov(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    write_operand(d.op2, dim, d.op1_value);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// CMP: computes operand2 - operand1 for flags only; the result is
// discarded (matches the reference core's opCMPB: `appb = op2; SUBB(appb,
// op1, 0);`).
int Cpu::op_cmp(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t dst = read_operand(d.op2, dim);
    const uint32_t src = d.op1_value;
    const uint64_t result = static_cast<uint64_t>(dst) - static_cast<uint64_t>(src);
    set_sub_flags(dim, result, src, dst);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// ADD: operand2 = operand2 + operand1 (a read-modify-write on operand 2,
// unlike MOV/CMP which only ever read or only ever write it -- this is
// exactly what decode_format12's `Operand` (resolved but not yet read or
// written) was designed for; no new addressing-mode work was needed to add
// this. Matches the reference core's opADDB/opADDH/opADDW: F12LOADOP2*,
// ADD*(dst, src, carry=0), F12STOREOP2*.
int Cpu::op_add(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t dst = read_operand(d.op2, dim);
    const uint32_t src = d.op1_value;
    const uint64_t result = static_cast<uint64_t>(dst) + static_cast<uint64_t>(src);
    set_add_flags(dim, result, src, dst);
    write_operand(d.op2, dim, static_cast<uint32_t>(result));
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// SUB: operand2 = operand2 - operand1. Same read-modify-write shape as ADD;
// matches the reference core's opSUBB/opSUBH/opSUBW.
int Cpu::op_sub(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t dst = read_operand(d.op2, dim);
    const uint32_t src = d.op1_value;
    const uint64_t result = static_cast<uint64_t>(dst) - static_cast<uint64_t>(src);
    set_sub_flags(dim, result, src, dst);
    write_operand(d.op2, dim, static_cast<uint32_t>(result));
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// AND/OR/XOR: same read-modify-write shape as ADD/SUB, but flags differ --
// see set_logical_flags. Matches the reference core's opANDB/opORB/opXORB.
int Cpu::op_and(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t result = read_operand(d.op2, dim) & d.op1_value;
    set_logical_flags(dim, result);
    write_operand(d.op2, dim, result);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

int Cpu::op_or(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t result = read_operand(d.op2, dim) | d.op1_value;
    set_logical_flags(dim, result);
    write_operand(d.op2, dim, result);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

int Cpu::op_xor(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t result = read_operand(d.op2, dim) ^ d.op1_value;
    set_logical_flags(dim, result);
    write_operand(d.op2, dim, result);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// NOT: operand2 = ~operand1 -- same read-then-write shape as MOV (op2 is
// only ever written, never read), unlike AND/OR/XOR's read-modify-write.
// Matches the reference core's opNOTB: F12DecodeFirstOperand (read op1),
// bitwise complement, F12WriteSecondOperand (write op2) -- confirmed as a
// genuine two-operand instruction despite the name suggesting unary.
int Cpu::op_not(Dim dim) {
    Format12 d = decode_format12(dim, dim);
    const uint32_t result = read_sized(~d.op1_value, dim);
    set_logical_flags(dim, result);
    write_operand(d.op2, dim, result);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// SHL: op1 is a signed shift count; op2 is shifted logically (zero-fill)
// in whichever direction the count's sign says. Mirrors the reference
// core's own approach of widening before shifting to read off the correct
// carry bit, rather than deriving an equivalent-looking formula by hand.
//
// Real gotcha, caught by re-reading the reference before writing tests
// rather than after: op1 (the count) is decoded as Byte-sized
// UNCONDITIONALLY, even in opSHLH/opSHLW (`F12DecodeOperands(&ReadAM, 0,
// &ReadAMAddress, dim)` -- note the literal `0`, not `dim`, for the first
// argument). This is specific to shift instructions; ADD/SUB/AND/OR/XOR
// use matching dims for both operands, confirmed separately. An earlier
// version of this function passed `dim` for both operands and would have
// silently misread the count for SHLH/SHLW.
int Cpu::op_shl(Dim dim) {
    Format12 d = decode_format12(Dim::Byte, dim);
    const auto count = static_cast<int8_t>(d.op1_value & 0xff);
    const uint32_t dst = read_operand(d.op2, dim);
    const uint32_t bits = dim_bytes(dim) * 8;
    uint32_t result = dst;

    flags_.overflow = false; // always cleared, both directions, per the reference
    if (count == 0) {
        flags_.carry = false;
    } else if (count > 0) {
        const auto shift = static_cast<uint32_t>(count);
        if (shift < bits) {
            const uint64_t widened = static_cast<uint64_t>(dst) << shift;
            flags_.carry = ((widened >> bits) & 1) != 0;
            result = static_cast<uint32_t>(widened);
        } else {
            // Shifting by >= the operand's width: the reference core's own
            // comment marks this "undefined what happens to CY". We define
            // it as an all-bits-shifted-out zero result with no carry --
            // the most natural reading, but not confirmed against real
            // hardware, since even the reference doesn't commit to one.
            flags_.carry = false;
            result = 0;
        }
    } else {
        const auto shift = static_cast<uint32_t>(-count);
        if (shift < bits) {
            flags_.carry = ((dst >> (shift - 1)) & 1) != 0;
            result = dst >> shift;
        } else {
            flags_.carry = false;
            result = 0;
        }
    }

    set_szf(dim, result);
    write_operand(d.op2, dim, result);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// SHA: same signed-count encoding as SHL (and the same Byte-sized count
// gotcha -- see op_shl), but:
//   - right shift (negative count) is ARITHMETIC: sign-preserving, and
//     always clears overflow (confirmed: SHIFTARITHMETICRIGHT_OV is
//     unconditionally 0 in the reference).
//   - left shift (positive count) computes a genuine overflow flag: did
//     any bit shifted past the sign position disagree with what the final
//     sign implies (confirmed against SHIFTLEFT_OV) -- unlike SHL, which
//     always clears overflow regardless of direction.
// Carry, both directions, is "the last bit shifted out" exactly as in SHL.
int Cpu::op_sha(Dim dim) {
    Format12 d = decode_format12(Dim::Byte, dim);
    const int32_t count = static_cast<int8_t>(d.op1_value & 0xff); // widen before negating (avoids INT8_MIN edge case)
    const uint32_t dst = read_operand(d.op2, dim);
    const uint32_t bits = dim_bytes(dim) * 8;
    const uint32_t sign_bit = 1u << (bits - 1);
    uint32_t result = dst;

    if (count == 0) {
        flags_.carry = false;
        flags_.overflow = false;
    } else if (count > 0) {
        const auto shift = static_cast<uint32_t>(count);
        if (shift < bits) {
            // Mask of the top `shift` bits (the ones shifted out or past
            // the sign position), mirroring SHIFTLEFT_OV exactly.
            const uint64_t mask = ((uint64_t{1} << shift) - 1) << (bits - shift);
            flags_.overflow = (dst & sign_bit) != 0
                ? (dst & mask) != mask   // sign was 1: overflow if not all those bits were already 1
                : (dst & mask) != 0;     // sign was 0: overflow if any of those bits was 1
            flags_.carry = ((dst >> (bits - shift)) & 1) != 0; // SHIFTLEFT_CY
            result = dst << shift;
        } else {
            // Same "reference doesn't commit to a value" edge case as SHL.
            flags_.carry = false;
            flags_.overflow = false;
            result = 0;
        }
    } else {
        const auto shift = static_cast<uint32_t>(-count);
        // Sign-extend dst's dim-width value into a full 32-bit signed
        // value by shifting its sign bit up to bit 31 and back down
        // arithmetically, then shift right by `shift` -- avoids a
        // per-dim switch for the sign-extension itself.
        const auto signed_dst = static_cast<int32_t>(dst << (32 - bits)) >> (32 - bits);
        if (shift < bits) {
            flags_.carry = ((dst >> (shift - 1)) & 1) != 0; // SHIFTARITHMETICRIGHT_CY
            flags_.overflow = false;                        // SHIFTARITHMETICRIGHT_OV
            result = static_cast<uint32_t>(signed_dst >> shift);
        } else {
            // Reference-confirmed behavior for this case (unlike SHL's
            // analogous branch): fully sign-extend rather than zero.
            flags_.carry = (dst & sign_bit) != 0; // our own choice; reference's own CY macro is not well-defined here either
            flags_.overflow = false;
            result = (dst & sign_bit) ? 0xffffffffu : 0u;
        }
    }

    set_szf(dim, result);
    write_operand(d.op2, dim, result);
    pc_ += d.length;
    return kApproximateCyclesPerInstruction;
}

// Condition codes 0-15 (11 reserved), confirmed one-for-one against
// MAME's opBV8/opBNV8/.../opBGT8 (op4.hxx): each tests a specific flag
// combination, matching the standard signed/unsigned comparison flag
// logic (e.g. "less than, signed" = sign != overflow after a CMP-style
// subtraction; "above, unsigned" = neither carry nor zero set).
bool Cpu::test_condition(uint8_t condition_code) const {
    switch (condition_code) {
        case 0: return flags_.overflow;                                     // V
        case 1: return !flags_.overflow;                                    // NV
        case 2: return flags_.carry;                                        // L  (below, unsigned)
        case 3: return !flags_.carry;                                       // NL (not below, unsigned)
        case 4: return flags_.zero;                                         // E
        case 5: return !flags_.zero;                                        // NE
        case 6: return flags_.carry || flags_.zero;                         // NH (not above, unsigned)
        case 7: return !(flags_.carry || flags_.zero);                      // H  (above, unsigned)
        case 8: return flags_.sign;                                         // N
        case 9: return !flags_.sign;                                        // P
        case 10: return true;                                               // R  (always)
        case 12: return flags_.sign != flags_.overflow;                     // LT (less, signed)
        case 13: return flags_.sign == flags_.overflow;                     // GE (greater-equal, signed)
        case 14: return (flags_.sign != flags_.overflow) || flags_.zero;    // LE (less-equal, signed)
        case 15: return !((flags_.sign != flags_.overflow) || flags_.zero); // GT (greater, signed)
        default:
            throw UnimplementedOpcode(pc_, static_cast<uint8_t>(0x60 + condition_code));
    }
}

int Cpu::op_branch(uint8_t opcode) {
    const uint32_t opcode_pc = pc_;
    const uint8_t condition_code = opcode & 0x0f;
    const bool wide = (opcode & 0x10) != 0;

    if (test_condition(condition_code)) {
        // Displacement is relative to the branch opcode's own address, not
        // the following instruction -- see the header comment.
        if (wide) {
            const auto disp = static_cast<int16_t>(bus_.read16(opcode_pc + 1));
            pc_ = opcode_pc + static_cast<uint32_t>(disp);
        } else {
            const auto disp = static_cast<int8_t>(bus_.read8(opcode_pc + 1));
            pc_ = opcode_pc + static_cast<uint32_t>(disp);
        }
    } else {
        pc_ = opcode_pc + (wide ? 3 : 2);
    }
    return kApproximateCyclesPerInstruction;
}

// JMP: unconditional jump to an address (confirmed against the reference
// core's opJMP, marked "TRUSTED"). The target is decoded the same way any
// other general operand is (decode_general_operand); operand_as_address
// accepts a resolved memory address OR an immediate (a literal absolute
// address -- confirmed valid against the reference's am2Immediate), and
// rejects a bare register (a register holds a value, not something you
// can jump "to" directly -- the reference core asserts this).
int Cpu::op_jmp(bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const uint8_t modifier_byte = bus_.read8(opcode_pc + 1);
    const Operand target = decode_general_operand(opcode_pc + 1, Dim::Byte, modm, modifier_length);
    pc_ = operand_as_address(target, opcode_pc, modifier_byte);
    return kApproximateCyclesPerInstruction;
}

// JSR: like JMP, but first pushes the address of the instruction
// immediately after this one (opcode_pc + 1 + modifier_length) so a
// matching RSR can return here. Confirmed against the reference core's
// opJSR ("TRUSTED"). This is the simpler of the two call/return pairs this
// core implements -- see op_ret's comment for the other (CALL/RET, which
// additionally links the AP register into a stack frame and is not yet
// implemented here).
int Cpu::op_jsr(bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const uint8_t modifier_byte = bus_.read8(opcode_pc + 1);
    const Operand target = decode_general_operand(opcode_pc + 1, Dim::Byte, modm, modifier_length);
    const uint32_t target_address = operand_as_address(target, opcode_pc, modifier_byte);
    const uint32_t return_address = opcode_pc + 1 + modifier_length;
    regs_[SP] -= 4;
    bus_.write32(regs_[SP], return_address);
    pc_ = target_address;
    return kApproximateCyclesPerInstruction;
}

// RSR: "return from subroutine" -- pops a return address pushed by JSR.
// Does not touch AP; pairs with JSR, not with CALL/RET (see op_ret).
int Cpu::op_rsr() {
    pc_ = bus_.read32(regs_[SP]);
    regs_[SP] += 4;
    return kApproximateCyclesPerInstruction;
}

// RET: the other half of the CALL/RET pair (CALL itself -- opcode 0x49,
// which additionally links AP into a stack frame -- is not yet
// implemented; RET is added now because it shares no new decode work with
// JMP/JSR/RSR beyond what decode_general_operand/read_operand already do).
// Confirmed against the reference core's opRET: reads a Long-sized general
// operand (a byte count of caller-pushed arguments to discard), pops PC,
// then pops AP, then skips that many extra bytes of stack.
int Cpu::op_ret(bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const Operand extra_operand = decode_general_operand(opcode_pc + 1, Dim::Long, modm, modifier_length);
    const uint32_t extra_bytes = read_operand(extra_operand, Dim::Long);

    pc_ = bus_.read32(regs_[SP]);
    regs_[SP] += 4;
    regs_[AP] = bus_.read32(regs_[SP]);
    regs_[SP] += 4;
    regs_[SP] += extra_bytes;
    return kApproximateCyclesPerInstruction;
}

// CALL: pushes the old AP, sets AP to operand 2's resolved address, pushes
// the return address, jumps to operand 1's resolved address. Confirmed
// against the reference's opCALL ("TRUSTED"). Like JMP/JSR, a bare
// register operand is rejected via operand_as_address -- there's no real
// hardware meaning for "set AP to this register's index number" or "jump
// to this register's index number" (as opposed to the register's value).
int Cpu::op_call() {
    const uint32_t opcode_pc = pc_;
    const uint8_t instflags = bus_.read8(opcode_pc + 1);
    const Format12RawOperands ops = decode_format12_raw(Dim::Byte, Dim::Long);
    const uint32_t target = operand_as_address(ops.op1, opcode_pc, instflags);
    const uint32_t new_ap = operand_as_address(ops.op2, opcode_pc, instflags);

    regs_[SP] -= 4;
    bus_.write32(regs_[SP], regs_[AP]);
    regs_[AP] = new_ap;

    regs_[SP] -= 4;
    bus_.write32(regs_[SP], opcode_pc + ops.length);
    pc_ = target;
    return kApproximateCyclesPerInstruction;
}

// PUSH: read the operand as a 32-bit value, decrement SP by 4, store it.
// Confirmed against the reference core's opPUSH ("m_moddim = 2" -- always
// Long-sized, regardless of what the operand's own natural width might
// otherwise suggest). Does not touch flags.
int Cpu::op_push(bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const Operand operand = decode_general_operand(opcode_pc + 1, Dim::Long, modm, modifier_length);
    const uint32_t value = read_operand(operand, Dim::Long);
    regs_[SP] -= 4;
    bus_.write32(regs_[SP], value);
    pc_ = opcode_pc + 1 + modifier_length;
    return kApproximateCyclesPerInstruction;
}

// POP: pop a 32-bit value, increment SP by 4, then write it to the operand
// (a valid write target -- register or memory -- unlike JMP/JSR's
// address-only operand). Confirmed against the reference core's opPOP.
// Does not touch flags.
//
// Order matters and is confirmed from the reference: SP is popped
// *before* the destination operand's address is decoded (POP's own
// modifier byte is read at PC+1 regardless, but decode_general_operand
// evaluates the address it points to -- e.g. a displacement or indirect
// through some register -- only when called). This only differs from
// "decode first, then pop" when the destination operand itself is
// SP-relative, but matching the real order costs nothing here.
int Cpu::op_pop(bool modm) {
    const uint32_t opcode_pc = pc_;
    const uint32_t value = bus_.read32(regs_[SP]);
    regs_[SP] += 4;
    uint8_t modifier_length = 0;
    const Operand operand = decode_general_operand(opcode_pc + 1, Dim::Long, modm, modifier_length);
    write_operand(operand, Dim::Long, value);
    pc_ = opcode_pc + 1 + modifier_length;
    return kApproximateCyclesPerInstruction;
}

// INC: operand += 1, using the full ADD-style flags (carry included) --
// confirmed against the reference's opINCB, which computes it via the
// exact same ADDB(dst, src=1, carry=0) macro opADDB uses.
int Cpu::op_inc(Dim dim, bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const Operand operand = decode_general_operand(opcode_pc + 1, dim, modm, modifier_length);
    const uint32_t dst = read_operand(operand, dim);
    const uint64_t result = static_cast<uint64_t>(dst) + 1;
    set_add_flags(dim, result, 1, dst);
    write_operand(operand, dim, static_cast<uint32_t>(result));
    pc_ = opcode_pc + 1 + modifier_length;
    return kApproximateCyclesPerInstruction;
}

// DEC: operand -= 1, using the full SUB-style flags (carry included) --
// confirmed against the reference's opDECB (SUBB(dst, src=1, carry=0)).
int Cpu::op_dec(Dim dim, bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const Operand operand = decode_general_operand(opcode_pc + 1, dim, modm, modifier_length);
    const uint32_t dst = read_operand(operand, dim);
    const uint64_t result = static_cast<uint64_t>(dst) - 1;
    set_sub_flags(dim, result, 1, dst);
    write_operand(operand, dim, static_cast<uint32_t>(result));
    pc_ = opcode_pc + 1 + modifier_length;
    return kApproximateCyclesPerInstruction;
}

int Cpu::step() {
    const uint32_t opcode_pc = pc_;
    const uint8_t opcode = bus_.read8(pc_);

    if ((opcode & 0xe0) == 0x60) {
        return op_branch(opcode);
    }

    switch (opcode) {
        case 0x00: return op_halt();
        case 0xcd: return op_nop();
        case 0x09: return op_mov(Dim::Byte);
        case 0x1b: return op_mov(Dim::Word);
        case 0x2d: return op_mov(Dim::Long);
        case 0xb8: return op_cmp(Dim::Byte);
        case 0xba: return op_cmp(Dim::Word);
        case 0xbc: return op_cmp(Dim::Long);
        case 0x80: return op_add(Dim::Byte);
        case 0x82: return op_add(Dim::Word);
        case 0x84: return op_add(Dim::Long);
        case 0xa8: return op_sub(Dim::Byte);
        case 0xaa: return op_sub(Dim::Word);
        case 0xac: return op_sub(Dim::Long);
        case 0x38: return op_not(Dim::Byte);
        case 0x3a: return op_not(Dim::Word);
        case 0x3c: return op_not(Dim::Long);
        case 0x88: return op_or(Dim::Byte);
        case 0x8a: return op_or(Dim::Word);
        case 0x8c: return op_or(Dim::Long);
        case 0xa0: return op_and(Dim::Byte);
        case 0xa2: return op_and(Dim::Word);
        case 0xa4: return op_and(Dim::Long);
        case 0xb0: return op_xor(Dim::Byte);
        case 0xb2: return op_xor(Dim::Word);
        case 0xb4: return op_xor(Dim::Long);
        case 0xd6: return op_jmp(false);
        case 0xd7: return op_jmp(true);
        case 0xe8: return op_jsr(false);
        case 0xe9: return op_jsr(true);
        case 0xca: return op_rsr();
        case 0x49: return op_call();
        case 0xe2: return op_ret(false);
        case 0xe3: return op_ret(true);
        case 0xe6: return op_pop(false);
        case 0xe7: return op_pop(true);
        case 0xee: return op_push(false);
        case 0xef: return op_push(true);
        case 0xd0: return op_dec(Dim::Byte, false);
        case 0xd1: return op_dec(Dim::Byte, true);
        case 0xd2: return op_dec(Dim::Word, false);
        case 0xd3: return op_dec(Dim::Word, true);
        case 0xd4: return op_dec(Dim::Long, false);
        case 0xd5: return op_dec(Dim::Long, true);
        case 0xd8: return op_inc(Dim::Byte, false);
        case 0xd9: return op_inc(Dim::Byte, true);
        case 0xda: return op_inc(Dim::Word, false);
        case 0xdb: return op_inc(Dim::Word, true);
        case 0xdc: return op_inc(Dim::Long, false);
        case 0xdd: return op_inc(Dim::Long, true);
        case 0xa9: return op_shl(Dim::Byte);
        case 0xab: return op_shl(Dim::Word);
        case 0xad: return op_shl(Dim::Long);
        case 0xb9: return op_sha(Dim::Byte);
        case 0xbb: return op_sha(Dim::Word);
        case 0xbd: return op_sha(Dim::Long);
        default:
            throw UnimplementedOpcode(opcode_pc, opcode);
    }
}

} // namespace model1::cpu::v60

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
    if (op.is_register) return read_sized(regs_[op.reg], dim);
    switch (dim) {
        case Dim::Byte: return bus_.read8(op.address);
        case Dim::Word: return bus_.read16(op.address);
        case Dim::Long: return bus_.read32(op.address);
    }
    return 0;
}

void Cpu::write_operand(const Operand& op, Dim dim, uint32_t value) {
    if (op.is_register) {
        write_sized(regs_[op.reg], dim, value);
        return;
    }
    switch (dim) {
        case Dim::Byte: bus_.write8(op.address, static_cast<uint8_t>(value)); break;
        case Dim::Word: bus_.write16(op.address, static_cast<uint16_t>(value)); break;
        case Dim::Long: bus_.write32(op.address, value); break;
    }
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
                return Operand{true, regnum, 0};
            case 4: { // Autoincrement: read/write at the current address, then bump
                const uint32_t address = regs_[regnum];
                regs_[regnum] += size;
                out_length = 1;
                return Operand{false, 0, address};
            }
            case 5: { // Autodecrement: bump first, then read/write at the new address
                regs_[regnum] -= size;
                out_length = 1;
                return Operand{false, 0, regs_[regnum]};
            }
            default:
                throw UnimplementedAddressingMode(pc_, modifier);
        }
    } else {
        switch (index) {
            case 0: { // Displacement-8: [reg + sign-extended 8-bit displacement]
                const auto disp = static_cast<int8_t>(bus_.read8(modifier_addr + 1));
                out_length = 2;
                return Operand{false, 0, regs_[regnum] + static_cast<uint32_t>(disp)};
            }
            case 3: // Register indirect: [reg]
                out_length = 1;
                return Operand{false, 0, regs_[regnum]};
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
        op2 = Operand{true, static_cast<uint8_t>(instflags & 0x1f), 0};
    } else {
        // Operand 1 short, operand 2 general.
        op1_value = read_sized(regs_[instflags & 0x1f], dim1);
        uint8_t extra = 0;
        op2 = decode_general_operand(pc_ + 2, dim2, modm, extra);
        length += extra;
    }

    return Format12{op1_value, op2, length};
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
// other general operand is (decode_general_operand), but must not resolve
// to a bare register -- a register holds a value, not something you can
// jump "to" directly (the reference core asserts this rather than
// defining behavior for it).
int Cpu::op_jmp(bool modm) {
    const uint32_t opcode_pc = pc_;
    uint8_t modifier_length = 0;
    const Operand target = decode_general_operand(opcode_pc + 1, Dim::Byte, modm, modifier_length);
    if (target.is_register) {
        throw UnimplementedAddressingMode(opcode_pc, bus_.read8(opcode_pc + 1));
    }
    pc_ = target.address;
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
    const Operand target = decode_general_operand(opcode_pc + 1, Dim::Byte, modm, modifier_length);
    if (target.is_register) {
        throw UnimplementedAddressingMode(opcode_pc, bus_.read8(opcode_pc + 1));
    }
    const uint32_t return_address = opcode_pc + 1 + modifier_length;
    regs_[SP] -= 4;
    bus_.write32(regs_[SP], return_address);
    pc_ = target.address;
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
        case 0xd6: return op_jmp(false);
        case 0xd7: return op_jmp(true);
        case 0xe8: return op_jsr(false);
        case 0xe9: return op_jsr(true);
        case 0xca: return op_rsr();
        case 0xe2: return op_ret(false);
        case 0xe3: return op_ret(true);
        default:
            throw UnimplementedOpcode(opcode_pc, opcode);
    }
}

} // namespace model1::cpu::v60

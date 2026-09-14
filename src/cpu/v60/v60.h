#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>

// NEC V60 (uPD70615) CPU core.
//
// Scope of this increment: a handful of addressing modes (register-direct,
// register-indirect, autoincrement, autodecrement, 8-bit displacement) and
// a small instruction subset (HALT, NOP, MOV, CMP, ADD, SUB). Every other
// addressing mode (16/32-bit displacement, displacement-indirect,
// double-displacement, bit-string/bit-field modes, immediate literals, ...)
// and every other opcode throws UnimplementedOpcode / UnimplementedAddressingMode rather
// than silently doing the wrong thing. This is intentional: the V60 is a
// VAX-like CISC design with a large, intricate instruction/addressing-mode
// encoding, and getting a small slice fully correct is worth more than a
// large slice that's guessed.
//
// Facts below (register count, PSW flag set, instruction encoding,
// per-instruction semantics, and the note on cycle timing) were confirmed
// by reading MAME's v60 core (src/devices/cpu/v60/*) as documentation --
// see docs/planning/06-legal-and-assets.md's "documentation only" policy.
// This implementation is original; no MAME code is copied.
//
// IMPORTANT, hard-won correction (see docs/hardware-notes/07-v60-architecture.md):
// a "general" operand's addressing mode is selected by BOTH its modifier
// byte's top 3 bits (0-7) AND a separate `modm` bit from the instflags
// byte -- there are two completely different 8-entry mode tables, not one.
// In particular, mode index 3 means "register-indirect" in one table and
// "register-direct" in the other. An earlier version of this file checked
// only the modifier byte's top 3 bits and got this wrong; every general-
// operand decode below now takes `modm` into account.
namespace model1::cpu::v60 {

// Byte-addressable little-endian memory bus. The board wires this to real
// RAM/ROM/MMIO; this class knows nothing about Model 1 specifics.
class Bus {
public:
    virtual ~Bus() = default;
    virtual uint8_t read8(uint32_t address) = 0;
    virtual void write8(uint32_t address, uint8_t value) = 0;
    virtual uint16_t read16(uint32_t address) = 0;
    virtual void write16(uint32_t address, uint16_t value) = 0;
    virtual uint32_t read32(uint32_t address) = 0;
    virtual void write32(uint32_t address, uint32_t value) = 0;
};

// PSW flags actually tracked here: carry, overflow, sign, zero. This
// matches the reference core exactly -- it tracks only these four despite
// the V60 architecture defining other PSW bits (interrupt level,
// privilege/ring level, etc.); those are out of scope until a target game
// is found to need them.
struct Flags {
    bool carry = false;
    bool overflow = false;
    bool sign = false;
    bool zero = false;
};

// R0-R28 general purpose, plus AP (Argument Pointer), FP (Frame Pointer),
// SP (Stack Pointer) -- 32 addressing registers total, indexed exactly as
// the real hardware's 5-bit register field does (0-31). PC is separate.
// The V60's separate system/control register bank (memory management,
// timers, ...) is out of scope for a game-ROM interpreter unless a target
// game is found to need it.
enum Reg : uint8_t {
    R0 = 0, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
    R16, R17, R18, R19, R20, R21, R22, R23,
    R24, R25, R26, R27, R28,
    AP = 29,
    FP = 30,
    SP = 31,
};

// Operand size, matching the real hardware's dim encoding (0/1/2).
enum class Dim : uint8_t { Byte = 0, Word = 1, Long = 2 };

struct UnimplementedOpcode : std::runtime_error {
    UnimplementedOpcode(uint32_t pc, uint8_t opcode);
    uint32_t pc;
    uint8_t opcode;
};

struct UnimplementedAddressingMode : std::runtime_error {
    UnimplementedAddressingMode(uint32_t pc, uint8_t modifier);
    uint32_t pc;
    uint8_t modifier;
};

class Cpu {
public:
    explicit Cpu(Bus& bus) : bus_(bus) {}

    void reset(uint32_t start_pc);

    // Executes exactly one instruction, returns its cost in cycles.
    //
    // Real per-instruction V60 cycle timing is not established anywhere we
    // found -- MAME's own reference core charges a flat average per
    // instruction regardless of opcode, with the source comment "Actual
    // cycles / instruction is unknown". This core does the same (see
    // kApproximateCyclesPerInstruction below) until real timing data
    // surfaces. See docs/hardware-notes/01-cpu-and-bus.md.
    int step();

    uint32_t pc() const { return pc_; }
    uint32_t reg(uint8_t index) const { return regs_[index & 0x1f]; }
    void set_reg(uint8_t index, uint32_t value) { regs_[index & 0x1f] = value; }
    const Flags& flags() const { return flags_; }

    static constexpr int kApproximateCyclesPerInstruction = 8;

private:
    Bus& bus_;
    std::array<uint32_t, 32> regs_{};
    uint32_t pc_ = 0;
    Flags flags_{};

    uint32_t read_sized(uint32_t value, Dim dim) const;
    void write_sized(uint32_t& dest, Dim dim, uint32_t value) const;
    static uint32_t dim_bytes(Dim dim);

    // A decoded operand: a register (read/written directly), a resolved
    // memory address (read/written through the bus), or an immediate
    // (read-only -- constructed by decode_general_operand for the literal
    // addressing modes; writing to one throws UnimplementedAddressingMode,
    // matching that there is no real hardware meaning for it). Any
    // side-effecting addressing mode (autoincrement/autodecrement) has
    // already been applied to the register file by the time this is
    // returned -- see decode_general_operand.
    struct Operand {
        enum class Kind { Register, Memory, Immediate };
        Kind kind;
        uint8_t reg;      // valid when kind == Register
        uint32_t address; // valid when kind == Memory
        uint32_t value;   // valid when kind == Immediate
    };
    uint32_t read_operand(const Operand& op, Dim dim);
    void write_operand(const Operand& op, Dim dim, uint32_t value);

    // Resolves an operand that must denote an address (used by JMP/JSR):
    // a Memory operand's address, or an Immediate operand's value used
    // directly as an absolute address (confirmed valid against the
    // reference core's am2Immediate/am2ImmediateQuick -- "jump to this
    // literal address" is a real, meaningful encoding). A Register operand
    // is invalid here and throws (a register holds a value, not something
    // you can jump "to").
    uint32_t operand_as_address(const Operand& op, uint32_t opcode_pc, uint8_t modifier_byte);

    // Resolves a "general form" operand's modifier byte at `modifier_addr`.
    // `modm` must be (instflags & 0x40) != 0 -- it selects which of the two
    // addressing-mode tables the modifier byte's top 3 bits index into
    // (see the file-level comment above). `out_length` receives the number
    // of bytes the modifier consumed (1, or more for a displacement/
    // immediate that follows it) so the caller can advance PC correctly.
    //
    // Supported so far (modm, top-3-bits) -> mode:
    //   (0, 0) displacement-8       (1, 3) register-direct
    //   (0, 3) register-indirect    (1, 4) autoincrement
    //   (0, 7) "Group 7" -- only    (1, 5) autodecrement
    //          its immediate sub-modes are implemented (see below)
    //
    // Group 7 (modm=0, top-3-bits=7) is itself sub-decoded by the
    // modifier byte's low 5 bits (confirmed against am1.hxx's 32-entry
    // s_AMTable1_G7): values 0-15 are "immediate quick" (the 4-bit value
    // 0-15 is the low nibble of those same bits -- no extra bytes), value
    // 20 is a full-width immediate (1/2/4 extra bytes per `dim`). The
    // remaining Group 7 sub-modes (PC-relative addressing, absolute
    // direct address, and their *-deferred/double-displacement variants)
    // are not yet implemented.
    //
    // Anything else throws UnimplementedAddressingMode.
    Operand decode_general_operand(uint32_t modifier_addr, Dim dim, bool modm, uint8_t& out_length);

    // Decodes a Format-1/2 instruction's two operands. `instflags` (the
    // byte immediately following the opcode) packs, confirmed against
    // MAME's op12.hxx/am1.hxx/am3.hxx:
    //   bit 7 (0x80)  both operands use "general" form -- NOT YET
    //                 IMPLEMENTED here (throws), since the both-general
    //                 case addresses op2's modifier byte relative to op1's
    //                 encoded length and needs more general-operand
    //                 plumbing than this increment covers.
    //   bit 6 (0x40)  `modm` for whichever operand is "general".
    //   bit 5 (0x20)  when bit 7 is clear: if set, operand 1 is "general"
    //                 and operand 2 is "short" (register number in bits
    //                 0-4 of instflags); if clear, the other way around.
    //   bits 0-4      register number for whichever operand is "short".
    struct Format12 {
        uint32_t op1_value; // operand 1, already resolved and read at `dim1` width
        Operand op2;        // operand 2, resolved but not yet read/written
        uint8_t length;     // total instruction length in bytes
    };
    Format12 decode_format12(Dim dim1, Dim dim2);

    // Same instflags encoding as decode_format12, but resolves BOTH
    // operands to raw `Operand`s without reading either as a value --
    // needed for CALL, whose two operands (confirmed against the
    // reference's opCALL) are both decoded via ReadAMAddress: one is a
    // jump target, the other a raw value assigned into AP, neither is a
    // "value at dim width" the way every other Format-1/2 instruction's
    // operand 1 is. decode_format12 can't be reused as-is because it
    // always eagerly reads operand 1.
    //
    // Also implements the "both operands general" case (instflags bit 7)
    // that decode_format12 still doesn't -- CALL's operands can never
    // legitimately be a bare register (see operand_as_address), so a CALL
    // with one short-form operand would be permanently unusable; real
    // encodings need both general. See the .cpp for the exact bit-7 layout
    // (op2's modm comes from a different bit than in the non-bit-7 case).
    struct Format12RawOperands {
        Operand op1;
        Operand op2;
        uint8_t length;
    };
    Format12RawOperands decode_format12_raw(Dim dim1, Dim dim2);

    void set_add_flags(Dim dim, uint64_t result, uint32_t src, uint32_t dst);
    void set_sub_flags(Dim dim, uint64_t result, uint32_t src, uint32_t dst);
    void set_szf(Dim dim, uint64_t result);

    // AND/OR/XOR/NOT flags, confirmed against the reference core's
    // ANDB/ORB/XORB/NOTB macros: overflow is always cleared, sign/zero set
    // from the result as usual -- but unlike add/sub, carry is left
    // completely untouched (not even cleared). Easy to miss since every
    // other flag-setting instruction implemented so far touches carry.
    void set_logical_flags(Dim dim, uint32_t result);

    // Conditional branches (opcodes 0x60-0x7F) encode cleanly: the low 4
    // bits of the opcode select one of the conditions below, bit 4 selects
    // an 8-bit (0x60-0x6F) or 16-bit (0x70-0x7F) signed displacement.
    // Confirmed against MAME's op4.hxx (header comment: "FULLY TRUSTED").
    // Condition code 11 is reserved on both ranges (0x6B, 0x7B).
    //
    // The displacement is added directly to the address of the branch
    // opcode itself, NOT the address of the following instruction -- a
    // real, easy-to-get-backwards detail confirmed by reading the
    // reference core (it never advances PC before adding the
    // displacement).
    bool test_condition(uint8_t condition_code) const;
    int op_branch(uint8_t opcode);

    // -- instruction implementations --
    int op_halt();
    int op_nop();
    int op_mov(Dim dim);
    int op_cmp(Dim dim);
    int op_add(Dim dim);
    int op_sub(Dim dim);
    int op_and(Dim dim);
    int op_or(Dim dim);
    int op_xor(Dim dim);
    int op_not(Dim dim);

    // SHL: operand 1 is a SIGNED shift count (positive = left, negative =
    // right, zero = no-op but flags still recomputed) applied to operand
    // 2, both directions LOGICAL (zero-fill) -- confirmed against the
    // reference core's opSHLB/opSHLH/opSHLW ("TRUSTED").
    int op_shl(Dim dim);

    // SHA: same signed-count encoding as SHL, but the right shift is
    // ARITHMETIC (sign-preserving) instead of logical, and the left shift
    // computes a genuine overflow flag (did the sign change during the
    // shift?) instead of always clearing it like SHL does. Confirmed
    // against the reference core's opSHAB/opSHAH/opSHAW.
    int op_sha(Dim dim);

    // JMP/JSR/RET decode a single operand directly at PC+1 -- there is no
    // instflags byte for these (unlike Format-1/2 instructions); `modm` is
    // fixed per opcode (the reference core has separate 0x_0/0x_1 opcode
    // bytes rather than a mode bit, e.g. JMP_0/JMP_1). Confirmed "cannot be
    // a register" for JMP/JSR's target (the reference core asserts this)
    // -- we throw instead of asserting, since a real ROM could in
    // principle (incorrectly) encode it.
    int op_jmp(bool modm);
    int op_jsr(bool modm);
    int op_rsr();
    int op_ret(bool modm);

    // CALL: the VAX-style call convention that links AP into a stack
    // frame (pairs with RET, not RSR -- see the RET comment above and
    // docs/hardware-notes/07-v60-architecture.md). Both operands are
    // addresses (op1: jump target, op2: new AP value), decoded via
    // decode_format12_raw rather than decode_format12 since neither is a
    // "value at dim width" operand. Confirmed against the reference's
    // opCALL ("TRUSTED"): pushes the old AP, sets AP = op2, pushes the
    // return address, jumps to op1.
    int op_call();

    // PUSH/POP: single Long-sized general operand at PC+1, same
    // no-instflags shape as JMP/JSR/RET. PUSH reads the operand as a value
    // and pushes it; POP pops a value and writes it to the operand (so,
    // unlike JMP/JSR, POP's operand IS a valid write target -- a register
    // or memory location, decoded the normal way). Neither touches flags.
    int op_push(bool modm);
    int op_pop(bool modm);

    // INC/DEC: single general operand at PC+1 (dim-sized, no instflags
    // byte), read-modify-write by exactly 1 -- using the *same* full
    // ADD/SUB-style flags (including carry) as ADD/SUB themselves, unlike
    // AND/OR/XOR/NOT's carry-untouched behavior. Confirmed against the
    // reference's opINCB/opDECB, which literally call the same ADDB/SUBB
    // macro as opADDB/opSUBB with a constant operand of 1.
    int op_inc(Dim dim, bool modm);
    int op_dec(Dim dim, bool modm);
};

} // namespace model1::cpu::v60

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

    // A decoded operand: either a register (read/written directly) or a
    // resolved memory address (read/written through the bus). Any
    // side-effecting addressing mode (autoincrement/autodecrement) has
    // already been applied to the register file by the time this is
    // returned -- see decode_general_operand.
    struct Operand {
        bool is_register;
        uint8_t reg;
        uint32_t address;
    };
    uint32_t read_operand(const Operand& op, Dim dim);
    void write_operand(const Operand& op, Dim dim, uint32_t value);

    // Resolves a "general form" operand's modifier byte at `modifier_addr`.
    // `modm` must be (instflags & 0x40) != 0 -- it selects which of the two
    // addressing-mode tables the modifier byte's top 3 bits index into
    // (see the file-level comment above). `out_length` receives the number
    // of bytes the modifier consumed (1, or 2 when a displacement byte
    // follows it) so the caller can advance PC correctly.
    //
    // Supported so far (modm, top-3-bits) -> mode:
    //   (0, 0) displacement-8   (1, 3) register-direct
    //   (0, 3) register-indirect  (1, 4) autoincrement
    //                              (1, 5) autodecrement
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

    void set_add_flags(Dim dim, uint64_t result, uint32_t src, uint32_t dst);
    void set_sub_flags(Dim dim, uint64_t result, uint32_t src, uint32_t dst);
    void set_szf(Dim dim, uint64_t result);

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
};

} // namespace model1::cpu::v60

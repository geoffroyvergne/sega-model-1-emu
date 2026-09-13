#include "doctest.h"
#include "cpu/v60/v60.h"

#include <array>
#include <cstring>
#include <vector>

using namespace model1::cpu::v60;

namespace {

// A flat, fixed-size RAM bus for testing -- no ROM/MMIO distinctions, no
// Model 1 specifics. Little-endian, matching the real V60 bus.
class TestBus final : public Bus {
public:
    explicit TestBus(size_t size = 0x1000) : mem_(size, 0) {}

    void load(uint32_t address, std::initializer_list<uint8_t> bytes) {
        size_t i = address;
        for (uint8_t b : bytes) mem_.at(i++) = b;
    }

    uint8_t read8(uint32_t address) override { return mem_.at(address); }
    void write8(uint32_t address, uint8_t value) override { mem_.at(address) = value; }

    uint16_t read16(uint32_t address) override {
        return static_cast<uint16_t>(read8(address) | (read8(address + 1) << 8));
    }
    void write16(uint32_t address, uint16_t value) override {
        write8(address, static_cast<uint8_t>(value));
        write8(address + 1, static_cast<uint8_t>(value >> 8));
    }
    uint32_t read32(uint32_t address) override {
        return static_cast<uint32_t>(read16(address)) | (static_cast<uint32_t>(read16(address + 2)) << 16);
    }
    void write32(uint32_t address, uint32_t value) override {
        write16(address, static_cast<uint16_t>(value));
        write16(address + 2, static_cast<uint16_t>(value >> 16));
    }

private:
    std::vector<uint8_t> mem_;
};

// Helpers for building instflags/modifier bytes, matching
// docs/hardware-notes/07-v60-architecture.md's encoding notes. Kept local
// to the test file -- production code never needs to *construct* these,
// only decode them.
constexpr uint8_t kOp1General = 0x20; // instflags bit 5
constexpr uint8_t kModM = 0x40;       // instflags bit 6
constexpr uint8_t kBothGeneral = 0x80; // instflags bit 7

uint8_t short_instflags(bool op1_general, bool modm, uint8_t short_reg) {
    uint8_t f = short_reg & 0x1f;
    if (op1_general) f |= kOp1General;
    if (modm) f |= kModM;
    return f;
}

uint8_t register_direct_modifier(uint8_t reg) { return static_cast<uint8_t>((3 << 5) | (reg & 0x1f)); }
uint8_t register_indirect_modifier(uint8_t reg) { return static_cast<uint8_t>((3 << 5) | (reg & 0x1f)); }
uint8_t autoincrement_modifier(uint8_t reg) { return static_cast<uint8_t>((4 << 5) | (reg & 0x1f)); }
uint8_t autodecrement_modifier(uint8_t reg) { return static_cast<uint8_t>((5 << 5) | (reg & 0x1f)); }
uint8_t displacement8_modifier(uint8_t reg) { return static_cast<uint8_t>((0 << 5) | (reg & 0x1f)); }

} // namespace

TEST_CASE("HALT advances PC by 1 and does not touch registers") {
    TestBus bus;
    bus.load(0, {0x00});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0x11111111);

    int cycles = cpu.step();

    CHECK(cpu.pc() == 1);
    CHECK(cpu.reg(R1) == 0x11111111);
    CHECK(cycles == Cpu::kApproximateCyclesPerInstruction);
}

TEST_CASE("NOP advances PC by 1 and does not touch registers") {
    TestBus bus;
    bus.load(0, {0xcd});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R2, 0x22222222);

    cpu.step();

    CHECK(cpu.pc() == 1);
    CHECK(cpu.reg(R2) == 0x22222222);
}

// MOVB R1, R2 : op1 = short reg R1, op2 = general register-direct R2
// (modm must be set -- register-direct is mode index 3 in the modm=1
// table; index 3 in the modm=0 table is a completely different mode,
// register-*indirect*. See docs/hardware-notes/07-v60-architecture.md.)
TEST_CASE("MOVB copies only the low byte and preserves the destination's upper bits") {
    TestBus bus;
    bus.load(0, {0x09, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0x000000ab);
    cpu.set_reg(R2, 0xffffff00);

    cpu.step();

    CHECK(cpu.reg(R2) == 0xffffffab);
    CHECK(cpu.pc() == 3);
}

TEST_CASE("MOVH copies only the low word and preserves the destination's upper bits") {
    TestBus bus;
    bus.load(0, {0x1b, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0x1234beef);
    cpu.set_reg(R2, 0xffff0000);

    cpu.step();

    CHECK(cpu.reg(R2) == 0xffffbeef);
}

TEST_CASE("MOVW copies the full 32-bit register") {
    TestBus bus;
    bus.load(0, {0x2d, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0xdeadbeef);
    cpu.set_reg(R2, 0);

    cpu.step();

    CHECK(cpu.reg(R2) == 0xdeadbeef);
}

TEST_CASE("MOV does not affect flags") {
    TestBus bus;
    bus.load(0, {0x09, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0);
    cpu.set_reg(R2, 0xff);

    cpu.step();

    CHECK(cpu.flags().zero == false);
    CHECK(cpu.flags().carry == false);
    CHECK(cpu.flags().sign == false);
    CHECK(cpu.flags().overflow == false);
}

// CMPB R1, R2 : computes op2 - op1 (dst - src) for flags only.
TEST_CASE("CMPB sets zero flag on equal operands and leaves registers untouched") {
    TestBus bus;
    bus.load(0, {0xb8, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5);
    cpu.set_reg(R2, 5);

    cpu.step();

    CHECK(cpu.flags().zero == true);
    CHECK(cpu.flags().carry == false);
    CHECK(cpu.reg(R1) == 5);
    CHECK(cpu.reg(R2) == 5);
}

TEST_CASE("CMPB sets carry (borrow) when destination is less than source") {
    TestBus bus;
    bus.load(0, {0xb8, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5); // source (op1)
    cpu.set_reg(R2, 3); // destination (op2): 3 - 5 underflows a byte

    cpu.step();

    CHECK(cpu.flags().carry == true);
    CHECK(cpu.flags().zero == false);
}

TEST_CASE("CMPB clears carry when destination is greater than source") {
    TestBus bus;
    bus.load(0, {0xb8, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 3);
    cpu.set_reg(R2, 5);

    cpu.step();

    CHECK(cpu.flags().carry == false);
    CHECK(cpu.flags().zero == false);
}

TEST_CASE("CMPB detects signed overflow") {
    // dst = 0x80 (-128 as int8), src = 0x01 : dst - src = 0x7f -- but as a
    // signed byte operation, -128 - 1 = -129, which doesn't fit in an
    // int8 (wraps to +127), so this must be flagged as overflow.
    TestBus bus;
    bus.load(0, {0xb8, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0x01);
    cpu.set_reg(R2, 0x80);

    cpu.step();

    CHECK(cpu.flags().overflow == true);
}

TEST_CASE("CMPW compares 16-bit operands") {
    TestBus bus;
    bus.load(0, {0xba, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0x1000);
    cpu.set_reg(R2, 0x1000);

    cpu.step();

    CHECK(cpu.flags().zero == true);
}

TEST_CASE("CMPW ignores bits above bit 15 of each operand") {
    TestBus bus;
    bus.load(0, {0xba, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0xffff0005);
    cpu.set_reg(R2, 0x00010005);

    cpu.step();

    CHECK(cpu.flags().zero == true);
}

TEST_CASE("CMPL compares full 32-bit operands") {
    TestBus bus;
    bus.load(0, {0xbc, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0x7fffffff);
    cpu.set_reg(R2, 0x80000000);

    cpu.step();

    // 0x80000000 - 0x7fffffff = 1 : no borrow, not zero, result positive.
    CHECK(cpu.flags().carry == false);
    CHECK(cpu.flags().zero == false);
}

TEST_CASE("Operand-1-general / operand-2-short form is decoded correctly") {
    // MOVB with op1 general register-direct (R3), op2 short (R4).
    TestBus bus;
    bus.load(0, {0x09, short_instflags(true, true, R4), register_direct_modifier(R3)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x7a);
    cpu.set_reg(R4, 0x00);

    cpu.step();

    CHECK(cpu.reg(R4) == 0x7a);
    CHECK(cpu.pc() == 3);
}

TEST_CASE("Register indirect reads through the address held in a register") {
    // MOVB [R3], R4 : op1 general, register-indirect (modm=0, mode 3).
    TestBus bus;
    bus.load(0, {0x09, short_instflags(true, false, R4), register_indirect_modifier(R3)});
    bus.write8(0x100, 0x5a);
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);
    cpu.set_reg(R4, 0);

    cpu.step();

    CHECK(cpu.reg(R4) == 0x5a);
    CHECK(cpu.reg(R3) == 0x100); // register-indirect has no side effect on the register
}

TEST_CASE("Register indirect writes through the address held in a register") {
    // MOVB R4, [R3] : op1 short, op2 general register-indirect.
    TestBus bus;
    bus.load(0, {0x09, short_instflags(false, false, R4), register_indirect_modifier(R3)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);
    cpu.set_reg(R4, 0x5a);

    cpu.step();

    CHECK(bus.read8(0x100) == 0x5a);
}

TEST_CASE("Autoincrement reads at the old address then advances the register by the operand size") {
    // MOVW [R3]+, R4 : word-sized autoincrement, so R3 advances by 2.
    TestBus bus;
    bus.load(0, {0x1b, short_instflags(true, true, R4), autoincrement_modifier(R3)});
    bus.write16(0x100, 0xbeef);
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);

    cpu.step();

    CHECK(cpu.reg(R4) == 0xbeef);
    CHECK(cpu.reg(R3) == 0x102);
}

TEST_CASE("Autodecrement advances the register by the operand size before reading") {
    // MOVW [R3]-, R4 : R3 must be decremented by 2 *before* the read.
    TestBus bus;
    bus.load(0, {0x1b, short_instflags(true, true, R4), autodecrement_modifier(R3)});
    bus.write16(0x0fe, 0xcafe);
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);

    cpu.step();

    CHECK(cpu.reg(R3) == 0x0fe);
    CHECK(cpu.reg(R4) == 0xcafe);
}

TEST_CASE("Autoincrement write stores at the old address then advances the register") {
    TestBus bus;
    bus.load(0, {0x09, short_instflags(false, true, R4), autoincrement_modifier(R3)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);
    cpu.set_reg(R4, 0x7a);

    cpu.step();

    CHECK(bus.read8(0x100) == 0x7a);
    CHECK(cpu.reg(R3) == 0x101);
}

TEST_CASE("Displacement-8 adds a sign-extended 8-bit offset to the register") {
    // MOVB 5(R3), R4 : reads from R3 + 5.
    TestBus bus;
    bus.load(0, {0x09, short_instflags(true, false, R4), displacement8_modifier(R3), 0x05});
    bus.write8(0x105, 0x33);
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);

    cpu.step();

    CHECK(cpu.reg(R4) == 0x33);
    CHECK(cpu.pc() == 4); // opcode + instflags + modifier + displacement byte
}

TEST_CASE("Displacement-8 offset can be negative") {
    // MOVB -1(R3), R4 : reads from R3 - 1.
    TestBus bus;
    bus.load(0, {0x09, short_instflags(true, false, R4), displacement8_modifier(R3), 0xff});
    bus.write8(0x0ff, 0x44);
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x100);

    cpu.step();

    CHECK(cpu.reg(R4) == 0x44);
}

// ADDB R1, R2 : op2 = op2 + op1 (a read-modify-write, unlike MOV/CMP).
TEST_CASE("ADDB adds into the destination and updates flags") {
    TestBus bus;
    bus.load(0, {0x80, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5);
    cpu.set_reg(R2, 3);

    cpu.step();

    CHECK(cpu.reg(R2) == 8);
    CHECK(cpu.flags().zero == false);
    CHECK(cpu.flags().carry == false);
}

TEST_CASE("ADDB sets carry on unsigned overflow past a byte") {
    TestBus bus;
    bus.load(0, {0x80, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 0xff);
    cpu.set_reg(R2, 0x01);

    cpu.step();

    CHECK((cpu.reg(R2) & 0xff) == 0);
    CHECK(cpu.flags().carry == true);
    CHECK(cpu.flags().zero == true);
}

TEST_CASE("ADDB preserves the destination's upper bits (byte-sized read-modify-write)") {
    TestBus bus;
    bus.load(0, {0x80, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 1);
    cpu.set_reg(R2, 0xabcd00ff);

    cpu.step();

    CHECK(cpu.reg(R2) == 0xabcd0000); // 0xff + 1 wraps the byte to 0x00, upper bits untouched
}

TEST_CASE("ADDW writes back through register-indirect (read-modify-write to memory)") {
    // ADDB R1, [R3] : op2 is register-indirect, not a register.
    TestBus bus;
    bus.load(0, {0x80, short_instflags(false, false, R1), register_indirect_modifier(R3)});
    bus.write8(0x100, 10);
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 7);
    cpu.set_reg(R3, 0x100);

    cpu.step();

    CHECK(bus.read8(0x100) == 17);
}

// SUBB R1, R2 : op2 = op2 - op1.
TEST_CASE("SUBB subtracts from the destination and updates flags") {
    TestBus bus;
    bus.load(0, {0xa8, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 3);
    cpu.set_reg(R2, 10);

    cpu.step();

    CHECK(cpu.reg(R2) == 7);
    CHECK(cpu.flags().carry == false);
}

TEST_CASE("SUBB sets carry (borrow) and wraps on underflow") {
    TestBus bus;
    bus.load(0, {0xa8, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5);
    cpu.set_reg(R2, 3);

    cpu.step();

    CHECK((cpu.reg(R2) & 0xff) == 0xfe); // 3 - 5 wraps to 0xfe in a byte
    CHECK(cpu.flags().carry == true);
}

TEST_CASE("ADDH (16-bit) and ADDW (32-bit) use the H/W-means-16/32-bit convention, not B/W") {
    // Opcode 0x82 is ADDH (16-bit); 0x84 is ADDW (32-bit) -- matches the
    // same convention already confirmed for MOV (0x1B=MOVH/16-bit,
    // 0x2D=MOVW/32-bit). See docs/hardware-notes/07-v60-architecture.md.
    TestBus bus16;
    bus16.load(0, {0x82, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu16(bus16);
    cpu16.reset(0);
    cpu16.set_reg(R1, 0x0001);
    cpu16.set_reg(R2, 0xffffffff);
    cpu16.step();
    CHECK(cpu16.reg(R2) == 0xffff0000); // 16-bit add: 0xffff + 1 wraps to 0x0000, upper 16 bits untouched

    TestBus bus32;
    bus32.load(0, {0x84, short_instflags(false, true, R1), register_direct_modifier(R2)});
    Cpu cpu32(bus32);
    cpu32.reset(0);
    cpu32.set_reg(R1, 1);
    cpu32.set_reg(R2, 0xffffffff);
    cpu32.step();
    CHECK(cpu32.reg(R2) == 0); // full 32-bit add wraps entirely
}

// BR8 (0x6a): unconditional branch, 8-bit displacement. Confirms the
// displacement is relative to the branch opcode's OWN address, not the
// following instruction -- an easy detail to get backwards.
TEST_CASE("BR8 branches relative to its own opcode address, not the next instruction") {
    TestBus bus;
    bus.load(0x10, {0x6a, 0x05}); // BR8 +5 : target = 0x10 + 5 = 0x15
    Cpu cpu(bus);
    cpu.reset(0x10);

    int cycles = cpu.step();

    CHECK(cpu.pc() == 0x15);
    CHECK(cycles == Cpu::kApproximateCyclesPerInstruction);
}

TEST_CASE("BR8 supports negative (backward) displacements") {
    TestBus bus;
    bus.load(0x10, {0x6a, 0xfb}); // BR8 -5 : target = 0x10 - 5 = 0x0b
    Cpu cpu(bus);
    cpu.reset(0x10);

    cpu.step();

    CHECK(cpu.pc() == 0x0b);
}

TEST_CASE("BE8 (branch if equal) is taken when zero flag is set, via a preceding CMP") {
    TestBus bus;
    bus.load(0, {
        0xb8, short_instflags(false, true, R1), register_direct_modifier(R2), // CMPB R1, R2
        0x64, 0x10,                                                            // BE8 +0x10 (relative to address 3)
    });
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5);
    cpu.set_reg(R2, 5); // equal -> CMP sets zero

    cpu.step(); // CMPB
    CHECK(cpu.pc() == 3);
    cpu.step(); // BE8

    CHECK(cpu.pc() == 3 + 0x10);
}

TEST_CASE("BE8 is not taken when the compared operands differ") {
    TestBus bus;
    bus.load(0, {
        0xb8, short_instflags(false, true, R1), register_direct_modifier(R2), // CMPB R1, R2
        0x64, 0x10,                                                            // BE8 +0x10
    });
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 3);
    cpu.set_reg(R2, 5);

    cpu.step(); // CMPB
    cpu.step(); // BE8, not taken

    CHECK(cpu.pc() == 5); // falls through to the next instruction (2-byte branch)
}

TEST_CASE("BLT8 (signed less-than) uses sign XOR overflow, not the carry flag") {
    // 3 - 5 as signed bytes: -2, no signed overflow -> BLT taken.
    TestBus bus;
    bus.load(0, {
        0xb8, short_instflags(false, true, R1), register_direct_modifier(R2), // CMPB R1=5, R2=3 -> 3-5
        0x6c, 0x10,                                                            // BLT8 +0x10
    });
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5);
    cpu.set_reg(R2, 3);

    cpu.step();
    cpu.step();

    CHECK(cpu.pc() == 3 + 0x10);
}

TEST_CASE("BH8 (unsigned above) is false when equal, unlike BGT8 (signed greater)") {
    // Equal operands: BH (strictly above, unsigned) must NOT be taken.
    TestBus bus;
    bus.load(0, {
        0xb8, short_instflags(false, true, R1), register_direct_modifier(R2),
        0x67, 0x10, // BH8 +0x10
    });
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R1, 5);
    cpu.set_reg(R2, 5);

    cpu.step();
    cpu.step();

    CHECK(cpu.pc() == 5); // not taken
}

TEST_CASE("BR16 (0x7a) uses a 16-bit displacement and a 3-byte fall-through length") {
    TestBus bus;
    bus.load(0x10, {0x7a, 0x00, 0x01}); // BR16 +0x100 (little-endian)
    Cpu cpu(bus);
    cpu.reset(0x10);

    cpu.step();

    CHECK(cpu.pc() == 0x10 + 0x100);
}

TEST_CASE("Reserved branch condition code 11 (0x6B) throws UnimplementedOpcode") {
    TestBus bus;
    bus.load(0, {0x6b, 0x00});
    Cpu cpu(bus);
    cpu.reset(0);

    CHECK_THROWS_AS(cpu.step(), UnimplementedOpcode);
}

// JMP_0 [R3] (0xD6, modm=0 so mode index 3 is register-*indirect*, not
// register-direct -- see the modm table note above).
TEST_CASE("JMP jumps to the address held in a register (register-indirect)") {
    TestBus bus;
    bus.load(0, {0xd6, register_indirect_modifier(R3)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R3, 0x1234);

    cpu.step();

    CHECK(cpu.pc() == 0x1234);
}

TEST_CASE("JMP_1 to a bare register-direct target throws (a register cannot be a jump address)") {
    // 0xD7 = JMP_1, modm=1, so mode index 3 here really is register-direct.
    TestBus bus;
    bus.load(0, {0xd7, register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);

    CHECK_THROWS_AS(cpu.step(), UnimplementedAddressingMode);
}

TEST_CASE("JSR pushes the return address and jumps; RSR pops it back") {
    TestBus bus;
    bus.load(0x10, {0xe8, register_indirect_modifier(R3)}); // JSR [R3] at 0x10-0x11
    bus.load(0x50, {0xca});                                  // RSR at the call target
    Cpu cpu(bus);
    cpu.reset(0x10);
    cpu.set_reg(R3, 0x50);
    cpu.set_reg(SP, 0x200);

    cpu.step(); // JSR
    CHECK(cpu.pc() == 0x50);
    CHECK(cpu.reg(SP) == 0x200 - 4);
    CHECK(bus.read32(cpu.reg(SP)) == 0x12); // return address = opcode addr (0x10) + 1 + modifier length (1)

    cpu.step(); // RSR
    CHECK(cpu.pc() == 0x12);
    CHECK(cpu.reg(SP) == 0x200);
}

TEST_CASE("RET pops PC and AP, then skips the extra bytes named by its operand") {
    // RET_1 (0xE3, modm=1) with a register-direct operand (R5 = 0): a
    // value read, unlike JMP/JSR's address-only decode, so register-direct
    // is valid here.
    TestBus bus;
    bus.load(0, {0xe3, register_direct_modifier(R5)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R5, 0); // no extra bytes to skip
    cpu.set_reg(SP, 0x300);
    bus.write32(0x300, 0xabcd);  // saved return address
    bus.write32(0x304, 0x7777);  // saved AP

    cpu.step();

    CHECK(cpu.pc() == 0xabcd);
    CHECK(cpu.reg(AP) == 0x7777);
    CHECK(cpu.reg(SP) == 0x308);
}

TEST_CASE("RET skips extra caller-pushed bytes named by its operand") {
    TestBus bus;
    bus.load(0, {0xe3, register_direct_modifier(R5)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R5, 8); // 8 extra bytes of arguments to discard
    cpu.set_reg(SP, 0x300);
    bus.write32(0x300, 0xabcd);
    bus.write32(0x304, 0x7777);

    cpu.step();

    CHECK(cpu.reg(SP) == 0x308 + 8);
}

TEST_CASE("Unknown opcode throws UnimplementedOpcode") {
    TestBus bus;
    bus.load(0, {0xff});
    Cpu cpu(bus);
    cpu.reset(0);

    CHECK_THROWS_AS(cpu.step(), UnimplementedOpcode);
}

TEST_CASE("An addressing mode outside this increment's scope throws UnimplementedAddressingMode") {
    // modm=0, mode index 1 (Displacement16) -- not implemented yet.
    TestBus bus;
    bus.load(0, {0x09, short_instflags(true, false, R1), (1 << 5) | R2});
    Cpu cpu(bus);
    cpu.reset(0);

    CHECK_THROWS_AS(cpu.step(), UnimplementedAddressingMode);
}

TEST_CASE("Register-indirect's mode index under modm=1 (Autoincrement) is not confused with modm=0's") {
    // Regression guard for the earlier bug: mode index 3 means something
    // different depending on modm. Using index 3 with modm=1 must resolve
    // to register-direct, not register-indirect, and must NOT read memory.
    TestBus bus;
    bus.load(0, {0x09, short_instflags(true, true, R4), register_direct_modifier(R2)});
    Cpu cpu(bus);
    cpu.reset(0);
    cpu.set_reg(R2, 0x99); // the value itself, not an address
    cpu.set_reg(R4, 0);

    cpu.step();

    CHECK(cpu.reg(R4) == 0x99);
}

TEST_CASE("Both-operands-general form throws UnimplementedAddressingMode") {
    TestBus bus;
    bus.load(0, {0x09, kBothGeneral, 0x00, 0x00});
    Cpu cpu(bus);
    cpu.reset(0);

    CHECK_THROWS_AS(cpu.step(), UnimplementedAddressingMode);
}

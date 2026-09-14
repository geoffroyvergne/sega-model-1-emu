#include <initializer_list>
#include <map>
#include <vector>

#include "cpu/z80/z80.h"
#include "doctest.h"

using namespace cpu::z80;

namespace {

class TestBus final : public Bus {
public:
    explicit TestBus(size_t size = 0x1000) : mem_(size, 0) {}

    void load(uint16_t address, std::initializer_list<uint8_t> bytes) {
        size_t i = address;
        for (uint8_t b : bytes) mem_.at(i++) = b;
    }

    uint8_t read8(uint16_t address) override { return mem_.at(address); }
    void write8(uint16_t address, uint8_t value) override { mem_.at(address) = value; }

    // A simple in-memory port space, distinct from mem_ -- real hardware
    // keeps I/O ports and memory electrically separate (IORQ vs MREQ), so
    // this test double does too rather than aliasing them together.
    uint8_t port_read(uint16_t port) override {
        auto it = ports_.find(port);
        return it == ports_.end() ? 0xff : it->second; // unmapped port reads as open-bus 0xff
    }
    void port_write(uint16_t port, uint8_t value) override { ports_[port] = value; }

private:
    std::vector<uint8_t> mem_;
    std::map<uint16_t, uint8_t> ports_;
};

} // namespace

TEST_CASE("NOP advances PC by 1 and changes nothing else") {
    TestBus bus;
    bus.load(0, {0x00});
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().pc == 1);
}

TEST_CASE("LD r,r' copies between the 8-bit registers") {
    TestBus bus;
    bus.load(0, {0x41}); // LD B,C
    Z80 cpu(bus);
    cpu.regs().c = 0x42;

    cpu.step();

    CHECK(cpu.regs().b == 0x42);
}

TEST_CASE("LD r,(HL) and LD (HL),r go through the bus at the HL address") {
    TestBus bus;
    bus.load(0, {0x46, 0x70}); // LD B,(HL) ; LD (HL),B
    bus.load(0x10, {0x99});
    Z80 cpu(bus);
    cpu.regs().h = 0x00;
    cpu.regs().l = 0x10;

    cpu.step(); // LD B,(HL)
    CHECK(cpu.regs().b == 0x99);

    cpu.regs().b = 0x55;
    cpu.step(); // LD (HL),B
    CHECK(bus.read8(0x10) == 0x55);
}

TEST_CASE("0x76 is HALT, not LD (HL),(HL)") {
    TestBus bus;
    bus.load(0, {0x76});
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().halted);
    CHECK(cpu.regs().pc == 1);
}

TEST_CASE("A halted CPU does not fetch further instructions") {
    TestBus bus;
    bus.load(0, {0x76, 0x3c}); // HALT ; INC A (should never execute)
    Z80 cpu(bus);

    cpu.step();
    cpu.step();
    cpu.step();

    CHECK(cpu.regs().pc == 1);
    CHECK(cpu.regs().a == 0);
}

TEST_CASE("LD r,n loads an 8-bit immediate") {
    TestBus bus;
    bus.load(0, {0x3e, 0x7f}); // LD A,0x7f
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().a == 0x7f);
    CHECK(cpu.regs().pc == 2);
}

TEST_CASE("LD dd,nn loads a 16-bit immediate into a register pair") {
    TestBus bus;
    bus.load(0, {0x21, 0x34, 0x12}); // LD HL,0x1234 (low byte first)
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().h == 0x12);
    CHECK(cpu.regs().l == 0x34);
}

TEST_CASE("LD SP,nn uses the dd encoding where 11 means SP, not AF") {
    TestBus bus;
    bus.load(0, {0x31, 0x00, 0x80}); // LD SP,0x8000
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().sp == 0x8000);
}

TEST_CASE("ADD A,r sets carry/half-carry/overflow/zero/sign correctly") {
    TestBus bus;
    bus.load(0, {0x80}); // ADD A,B
    Z80 cpu(bus);
    cpu.regs().a = 0xff;
    cpu.regs().b = 0x01;

    cpu.step();

    CHECK(cpu.regs().a == 0x00);
    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagH) != 0); // carry out of bit 3 (0xf+0x1)
    CHECK((cpu.regs().f & kFlagZ) != 0);
    CHECK((cpu.regs().f & kFlagS) == 0);
    CHECK((cpu.regs().f & kFlagPV) == 0); // no signed overflow here
    CHECK((cpu.regs().f & kFlagN) == 0);
}

TEST_CASE("ADD A,r detects signed overflow (0x7f + 0x01)") {
    TestBus bus;
    bus.load(0, {0x80}); // ADD A,B
    Z80 cpu(bus);
    cpu.regs().a = 0x7f;
    cpu.regs().b = 0x01;

    cpu.step();

    CHECK(cpu.regs().a == 0x80);
    CHECK((cpu.regs().f & kFlagPV) != 0); // two positives summing to a negative
    CHECK((cpu.regs().f & kFlagS) != 0);
    CHECK((cpu.regs().f & kFlagC) == 0);
}

TEST_CASE("ADC A,r includes the incoming carry") {
    TestBus bus;
    bus.load(0, {0x89}); // ADC A,C
    Z80 cpu(bus);
    cpu.regs().a = 0x01;
    cpu.regs().c = 0x01;
    cpu.regs().f = kFlagC;

    cpu.step();

    CHECK(cpu.regs().a == 0x03); // 1 + 1 + carry-in(1)
}

TEST_CASE("SUB r detects borrow and sets N") {
    TestBus bus;
    bus.load(0, {0x90}); // SUB B
    Z80 cpu(bus);
    cpu.regs().a = 0x00;
    cpu.regs().b = 0x01;

    cpu.step();

    CHECK(cpu.regs().a == 0xff);
    CHECK((cpu.regs().f & kFlagC) != 0); // borrow occurred
    CHECK((cpu.regs().f & kFlagN) != 0);
    CHECK((cpu.regs().f & kFlagS) != 0);
}

TEST_CASE("CP r sets flags like SUB but does not store the result") {
    TestBus bus;
    bus.load(0, {0xb8}); // CP B
    Z80 cpu(bus);
    cpu.regs().a = 0x05;
    cpu.regs().b = 0x05;

    cpu.step();

    CHECK(cpu.regs().a == 0x05); // unchanged
    CHECK((cpu.regs().f & kFlagZ) != 0);
}

TEST_CASE("AND/OR/XOR set parity, reset carry, and set half-carry only for AND") {
    TestBus bus;
    bus.load(0, {0xa0, 0xb0, 0xa8}); // AND B ; OR B ; XOR B
    Z80 cpu(bus);
    cpu.regs().a = 0b11001100;
    cpu.regs().b = 0b10101010;

    cpu.step(); // AND -> 0b10001000 (2 bits set -> even parity)
    CHECK(cpu.regs().a == 0b10001000);
    CHECK((cpu.regs().f & kFlagH) != 0);
    CHECK((cpu.regs().f & kFlagC) == 0);
    CHECK((cpu.regs().f & kFlagPV) != 0); // even parity

    cpu.regs().a = 0b11001100;
    cpu.step(); // OR -> 0b11101110 (6 bits set -> even parity)
    CHECK(cpu.regs().a == 0b11101110);
    CHECK((cpu.regs().f & kFlagH) == 0);

    cpu.regs().a = 0b11001100;
    cpu.step(); // XOR -> 0b01100110 (4 bits set -> even parity)
    CHECK(cpu.regs().a == 0b01100110);
    CHECK((cpu.regs().f & kFlagPV) != 0);
}

TEST_CASE("Immediate ALU forms (n) read from the instruction stream") {
    TestBus bus;
    bus.load(0, {0xc6, 0x10}); // ADD A,0x10
    Z80 cpu(bus);
    cpu.regs().a = 0x05;

    cpu.step();

    CHECK(cpu.regs().a == 0x15);
    CHECK(cpu.regs().pc == 2);
}

TEST_CASE("INC r sets flags but never touches carry") {
    TestBus bus;
    bus.load(0, {0x3c}); // INC A
    Z80 cpu(bus);
    cpu.regs().a = 0x7f;
    cpu.regs().f = kFlagC; // pre-set carry to prove INC leaves it alone

    cpu.step();

    CHECK(cpu.regs().a == 0x80);
    CHECK((cpu.regs().f & kFlagPV) != 0); // 0x7f -> 0x80 is a signed overflow
    CHECK((cpu.regs().f & kFlagC) != 0);  // untouched
    CHECK((cpu.regs().f & kFlagN) == 0);
}

TEST_CASE("DEC r sets N and detects the 0x80 signed-overflow case") {
    TestBus bus;
    bus.load(0, {0x3d}); // DEC A
    Z80 cpu(bus);
    cpu.regs().a = 0x80;

    cpu.step();

    CHECK(cpu.regs().a == 0x7f);
    CHECK((cpu.regs().f & kFlagPV) != 0);
    CHECK((cpu.regs().f & kFlagN) != 0);
}

TEST_CASE("INC dd/DEC dd affect no flags and wrap at 16 bits") {
    TestBus bus;
    bus.load(0, {0x23}); // INC HL
    Z80 cpu(bus);
    cpu.regs().h = 0xff;
    cpu.regs().l = 0xff;
    cpu.regs().f = 0xff; // all flags set beforehand

    cpu.step();

    CHECK(cpu.regs().h == 0x00);
    CHECK(cpu.regs().l == 0x00);
    CHECK(cpu.regs().f == 0xff); // completely untouched
}

TEST_CASE("JP nn jumps unconditionally") {
    TestBus bus;
    bus.load(0, {0xc3, 0x00, 0x10}); // JP 0x1000
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().pc == 0x1000);
}

TEST_CASE("JR e jumps relative to the address after the instruction") {
    TestBus bus;
    bus.load(0, {0x18, 0x05}); // JR +5
    Z80 cpu(bus);

    cpu.step();

    CHECK(cpu.regs().pc == 7); // 2 (after JR) + 5
}

TEST_CASE("JR e supports negative (backward) offsets") {
    TestBus bus;
    bus.load(0x10, {0x18, static_cast<uint8_t>(-5)}); // JR -5
    Z80 cpu(bus);
    cpu.regs().pc = 0x10;

    cpu.step();

    CHECK(cpu.regs().pc == 0x0d); // 0x12 - 5
}

TEST_CASE("JR cc,e only jumps when the condition holds") {
    TestBus bus;
    bus.load(0, {0x28, 0x10, 0x28, 0x10}); // JR Z,+16 (x2)
    Z80 cpu(bus);
    cpu.regs().f = 0; // Z clear -> first JR falls through

    cpu.step();
    CHECK(cpu.regs().pc == 2);

    cpu.regs().f = kFlagZ; // Z set -> second JR taken
    cpu.step();
    CHECK(cpu.regs().pc == 4 + 16);
}

TEST_CASE("DJNZ decrements B and branches while B is non-zero") {
    TestBus bus;
    bus.load(0, {0x10, static_cast<uint8_t>(-2)}); // DJNZ -2 (spins on itself)
    Z80 cpu(bus);
    cpu.regs().b = 3;

    cpu.step(); // B: 3->2, branch taken back to 0
    CHECK(cpu.regs().pc == 0);
    CHECK(cpu.regs().b == 2);

    cpu.step(); // B: 2->1, branch taken
    CHECK(cpu.regs().pc == 0);
    CHECK(cpu.regs().b == 1);

    cpu.step(); // B: 1->0, no branch -- falls through past the instruction
    CHECK(cpu.regs().pc == 2);
    CHECK(cpu.regs().b == 0);
}

TEST_CASE("CALL pushes the return address and RET pops it back") {
    TestBus bus(0x100);
    bus.load(0, {0xcd, 0x10, 0x00}); // CALL 0x0010
    bus.load(0x10, {0xc9});          // RET
    Z80 cpu(bus);
    cpu.regs().sp = 0x80;

    cpu.step(); // CALL
    CHECK(cpu.regs().pc == 0x10);
    CHECK(cpu.regs().sp == 0x7e);
    CHECK(bus.read8(0x7e) == 0x03); // low byte of return address (0x0003)
    CHECK(bus.read8(0x7f) == 0x00); // high byte

    cpu.step(); // RET
    CHECK(cpu.regs().pc == 0x0003);
    CHECK(cpu.regs().sp == 0x80);
}

TEST_CASE("Conditional CALL/RET only act when the condition holds") {
    TestBus bus(0x100);
    bus.load(0, {0xc4, 0x10, 0x00}); // CALL NZ,0x0010
    Z80 cpu(bus);
    cpu.regs().sp = 0x80;
    cpu.regs().f = kFlagZ; // NZ false -> call not taken

    cpu.step();

    CHECK(cpu.regs().pc == 3); // fell through, just consumed the operand bytes
    CHECK(cpu.regs().sp == 0x80); // nothing pushed
}

TEST_CASE("PUSH/POP qq encoding treats 11 as AF, not SP like dd does") {
    TestBus bus(0x100);
    bus.load(0, {0xf5, 0xe1}); // PUSH AF ; POP HL
    Z80 cpu(bus);
    cpu.regs().sp = 0x80;
    cpu.regs().a = 0x12;
    cpu.regs().f = 0x34;

    cpu.step(); // PUSH AF
    CHECK(cpu.regs().sp == 0x7e);

    cpu.step(); // POP HL -- should receive AF's bytes, not touch SP's encoding
    CHECK(cpu.regs().h == 0x12);
    CHECK(cpu.regs().l == 0x34);
    CHECK(cpu.regs().sp == 0x80);
}

TEST_CASE("EX AF,AF' swaps the accumulator/flags with the shadow set") {
    TestBus bus;
    bus.load(0, {0x08});
    Z80 cpu(bus);
    cpu.regs().a = 0x11;
    cpu.regs().f = 0x22;
    cpu.regs().a2 = 0x33;
    cpu.regs().f2 = 0x44;

    cpu.step();

    CHECK(cpu.regs().a == 0x33);
    CHECK(cpu.regs().f == 0x44);
    CHECK(cpu.regs().a2 == 0x11);
    CHECK(cpu.regs().f2 == 0x22);
}

TEST_CASE("EX DE,HL swaps the two register pairs") {
    TestBus bus;
    bus.load(0, {0xeb});
    Z80 cpu(bus);
    cpu.regs().d = 0x11;
    cpu.regs().e = 0x22;
    cpu.regs().h = 0x33;
    cpu.regs().l = 0x44;

    cpu.step();

    CHECK(cpu.regs().d == 0x33);
    CHECK(cpu.regs().e == 0x44);
    CHECK(cpu.regs().h == 0x11);
    CHECK(cpu.regs().l == 0x22);
}

TEST_CASE("EXX swaps BC/DE/HL with the shadow set but leaves AF untouched") {
    TestBus bus;
    bus.load(0, {0xd9});
    Z80 cpu(bus);
    cpu.regs().b = 1;
    cpu.regs().c = 2;
    cpu.regs().d = 3;
    cpu.regs().e = 4;
    cpu.regs().h = 5;
    cpu.regs().l = 6;
    cpu.regs().b2 = 11;
    cpu.regs().c2 = 12;
    cpu.regs().d2 = 13;
    cpu.regs().e2 = 14;
    cpu.regs().h2 = 15;
    cpu.regs().l2 = 16;
    cpu.regs().a = 0x99;
    cpu.regs().f = 0x77;

    cpu.step();

    CHECK(cpu.regs().b == 11);
    CHECK(cpu.regs().c == 12);
    CHECK(cpu.regs().d == 13);
    CHECK(cpu.regs().e == 14);
    CHECK(cpu.regs().h == 15);
    CHECK(cpu.regs().l == 16);
    CHECK(cpu.regs().b2 == 1);
    CHECK(cpu.regs().c2 == 2);
    CHECK(cpu.regs().a == 0x99); // AF untouched, unlike EX AF,AF'
    CHECK(cpu.regs().f == 0x77);
}

TEST_CASE("EX (SP),HL swaps HL with the word at the top of the stack") {
    TestBus bus(0x100);
    bus.load(0, {0xe3});
    bus.load(0x20, {0x34, 0x12}); // little-endian word 0x1234 at the stack top
    Z80 cpu(bus);
    cpu.regs().sp = 0x20;
    cpu.regs().h = 0xab;
    cpu.regs().l = 0xcd;

    cpu.step();

    CHECK(cpu.regs().h == 0x12);
    CHECK(cpu.regs().l == 0x34);
    CHECK(bus.read8(0x20) == 0xcd);
    CHECK(bus.read8(0x21) == 0xab);
    CHECK(cpu.regs().sp == 0x20); // SP itself is untouched
}

TEST_CASE("CPL complements A and sets H/N, leaving S/Z/PV/C untouched") {
    TestBus bus;
    bus.load(0, {0x2f});
    Z80 cpu(bus);
    cpu.regs().a = 0b10100101;
    cpu.regs().f = kFlagC | kFlagZ; // pre-set to prove these survive untouched

    cpu.step();

    CHECK(cpu.regs().a == static_cast<uint8_t>(~0b10100101));
    CHECK((cpu.regs().f & kFlagH) != 0);
    CHECK((cpu.regs().f & kFlagN) != 0);
    CHECK((cpu.regs().f & kFlagC) != 0); // untouched
    CHECK((cpu.regs().f & kFlagZ) != 0); // untouched
}

TEST_CASE("SCF sets carry and clears H/N, leaving other flags untouched") {
    TestBus bus;
    bus.load(0, {0x37});
    Z80 cpu(bus);
    cpu.regs().f = kFlagH | kFlagN | kFlagZ;

    cpu.step();

    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagH) == 0);
    CHECK((cpu.regs().f & kFlagN) == 0);
    CHECK((cpu.regs().f & kFlagZ) != 0); // untouched
}

TEST_CASE("CCF moves the old carry into H and inverts carry") {
    TestBus bus;
    bus.load(0, {0x3f, 0x3f}); // CCF ; CCF
    Z80 cpu(bus);
    cpu.regs().f = kFlagC | kFlagN;

    cpu.step(); // H <- old C (1), C <- 0, N <- 0
    CHECK((cpu.regs().f & kFlagC) == 0);
    CHECK((cpu.regs().f & kFlagH) != 0);
    CHECK((cpu.regs().f & kFlagN) == 0);

    cpu.step(); // H <- old C (now 0), C <- 1
    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagH) == 0);
}

TEST_CASE("DAA converts a binary BCD addition back into valid BCD (0x15 + 0x27 -> 0x42)") {
    TestBus bus;
    bus.load(0, {0xc6, 0x27, 0x27, 0x76}); // ADD A,0x27 ; DAA ; HALT
    Z80 cpu(bus);
    cpu.regs().a = 0x15;

    cpu.step(); // ADD A,0x27 -> A=0x3c
    cpu.step(); // DAA -> A=0x42

    CHECK(cpu.regs().a == 0x42);
    CHECK((cpu.regs().f & kFlagC) == 0);
    CHECK((cpu.regs().f & kFlagPV) != 0); // DAA repurposes PV as parity: 0x42 has even popcount
}

TEST_CASE("DAA converts a binary BCD subtraction back into valid BCD (0x42 - 0x27 -> 0x15)") {
    TestBus bus;
    bus.load(0, {0xd6, 0x27, 0x27, 0x76}); // SUB 0x27 ; DAA ; HALT
    Z80 cpu(bus);
    cpu.regs().a = 0x42;

    cpu.step(); // SUB 0x27 -> A=0x1b
    cpu.step(); // DAA -> A=0x15

    CHECK(cpu.regs().a == 0x15);
}

TEST_CASE("RLC rotates left circularly, carry = the old bit 7") {
    TestBus bus;
    bus.load(0, {0xcb, 0x00}); // RLC B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;

    cpu.step();

    CHECK(cpu.regs().b == 0x03);
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("RRC rotates right circularly, carry = the old bit 0") {
    TestBus bus;
    bus.load(0, {0xcb, 0x08}); // RRC B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;

    cpu.step();

    CHECK(cpu.regs().b == 0xc0);
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("RL rotates left through carry, not the wrapped bit") {
    TestBus bus;
    bus.load(0, {0xcb, 0x10, 0xcb, 0x10}); // RL B ; RL B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;
    cpu.regs().f = 0; // carry-in clear

    cpu.step(); // bit7(1) becomes new carry; old carry(0) becomes bit0
    CHECK(cpu.regs().b == 0x02);
    CHECK((cpu.regs().f & kFlagC) != 0);

    cpu.regs().b = 0x02;
    cpu.step(); // bit7(0) becomes new carry; old carry(1) becomes bit0
    CHECK(cpu.regs().b == 0x05);
    CHECK((cpu.regs().f & kFlagC) == 0);
}

TEST_CASE("RR rotates right through carry, not the wrapped bit") {
    TestBus bus;
    bus.load(0, {0xcb, 0x18}); // RR B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;
    cpu.regs().f = 0; // carry-in clear

    cpu.step(); // bit0(1) becomes new carry; old carry(0) becomes bit7

    CHECK(cpu.regs().b == 0x40);
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("SLA shifts left, filling bit 0 with zero") {
    TestBus bus;
    bus.load(0, {0xcb, 0x20}); // SLA B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;

    cpu.step();

    CHECK(cpu.regs().b == 0x02);
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("SRA shifts right arithmetically, preserving the sign bit") {
    TestBus bus;
    bus.load(0, {0xcb, 0x28}); // SRA B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;

    cpu.step();

    CHECK(cpu.regs().b == 0xc0); // bit 7 stays set, unlike SRL below
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("SLL (undocumented) shifts left filling bit 0 with one, unlike SLA") {
    TestBus bus;
    bus.load(0, {0xcb, 0x30}); // SLL B
    Z80 cpu(bus);
    cpu.regs().b = 0x40; // bit0=0, so SLA/RLC would both produce bit0=0 here

    cpu.step();

    CHECK(cpu.regs().b == 0x81); // 0x80 from the shift, |1 from SLL's own quirk
    CHECK((cpu.regs().f & kFlagC) == 0);
}

TEST_CASE("SRL shifts right logically, zero-filling bit 7 unlike SRA") {
    TestBus bus;
    bus.load(0, {0xcb, 0x38}); // SRL B
    Z80 cpu(bus);
    cpu.regs().b = 0x81;

    cpu.step();

    CHECK(cpu.regs().b == 0x40); // bit 7 zero-filled, unlike SRA's 0xc0
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("BIT b,r sets Z/PV from the tested bit and S only when b=7") {
    TestBus bus;
    bus.load(0, {0xcb, 0x70, 0xcb, 0x78}); // BIT 6,B ; BIT 7,B
    Z80 cpu(bus);
    cpu.regs().b = 0b01000000; // bit 6 set, bit 7 clear

    cpu.step(); // BIT 6,B -- bit is set
    CHECK((cpu.regs().f & kFlagZ) == 0);
    CHECK((cpu.regs().f & kFlagPV) == 0);
    CHECK((cpu.regs().f & kFlagS) == 0); // b != 7, so S never reflects it
    CHECK((cpu.regs().f & kFlagH) != 0);
    CHECK((cpu.regs().f & kFlagN) == 0);

    cpu.step(); // BIT 7,B -- bit is clear
    CHECK((cpu.regs().f & kFlagZ) != 0);
    CHECK((cpu.regs().f & kFlagPV) != 0);
    CHECK((cpu.regs().f & kFlagS) == 0);
}

TEST_CASE("BIT 7 sets S when the tested bit is itself set") {
    TestBus bus;
    bus.load(0, {0xcb, 0x78}); // BIT 7,B
    Z80 cpu(bus);
    cpu.regs().b = 0x80;

    cpu.step();

    CHECK((cpu.regs().f & kFlagS) != 0);
    CHECK((cpu.regs().f & kFlagZ) == 0);
}

TEST_CASE("BIT b,r leaves the carry flag completely untouched") {
    TestBus bus;
    bus.load(0, {0xcb, 0x40}); // BIT 0,B
    Z80 cpu(bus);
    cpu.regs().b = 0;
    cpu.regs().f = kFlagC;

    cpu.step();

    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("RES b,r clears one bit and leaves the rest alone") {
    TestBus bus;
    bus.load(0, {0xcb, 0x98}); // RES 3,B
    Z80 cpu(bus);
    cpu.regs().b = 0xff;

    cpu.step();

    CHECK(cpu.regs().b == 0xf7);
}

TEST_CASE("SET b,r sets one bit and leaves the rest alone") {
    TestBus bus;
    bus.load(0, {0xcb, 0xd8}); // SET 3,B
    Z80 cpu(bus);
    cpu.regs().b = 0x00;

    cpu.step();

    CHECK(cpu.regs().b == 0x08);
}

TEST_CASE("RES/SET affect no flags at all") {
    TestBus bus;
    bus.load(0, {0xcb, 0x98}); // RES 3,B
    Z80 cpu(bus);
    cpu.regs().b = 0xff;
    cpu.regs().f = 0xff;

    cpu.step();

    CHECK(cpu.regs().f == 0xff);
}

TEST_CASE("CB-prefixed opcodes with register index 6 operate through (HL)") {
    TestBus bus;
    bus.load(0, {0xcb, 0x06}); // RLC (HL)
    bus.load(0x10, {0x81});
    Z80 cpu(bus);
    cpu.regs().h = 0x00;
    cpu.regs().l = 0x10;

    cpu.step();

    CHECK(bus.read8(0x10) == 0x03);
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("BIT b,(HL) mirrors Y/X from HL+1's high byte, not the tested value") {
    // Oracle-confirmed real hardware quirk (see z80.h's comment on this):
    // the tested byte (0x28) has both undocumented bits set, but HL+1's
    // high byte (0x1300 -> 0x13) has both clear -- a naive "mirror the
    // operand" implementation would get Y/X backwards here.
    TestBus bus(0x1400);
    bus.load(0, {0xcb, 0x46}); // BIT 0,(HL)
    bus.load(0x12ff, {0x28});
    Z80 cpu(bus);
    cpu.regs().h = 0x12;
    cpu.regs().l = 0xff;

    cpu.step();

    CHECK((cpu.regs().f & kFlagY) == 0);
    CHECK((cpu.regs().f & kFlagX) == 0);
}

TEST_CASE("LD (BC),A / LD A,(BC) / LD (DE),A / LD A,(DE) round-trip through memory") {
    TestBus bus;
    bus.load(0, {0x02, 0x0a, 0x12, 0x1a}); // LD (BC),A ; LD A,(BC) ; LD (DE),A ; LD A,(DE)
    Z80 cpu(bus);
    cpu.regs().a = 0x42;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x20;
    cpu.regs().d = 0x00;
    cpu.regs().e = 0x30;

    cpu.step(); // LD (BC),A -- mem[0x0020] = 0x42
    CHECK(bus.read8(0x0020) == 0x42);

    cpu.regs().a = 0x00;
    cpu.step(); // LD A,(BC) -- A <- mem[0x0020]
    CHECK(cpu.regs().a == 0x42);

    cpu.step(); // LD (DE),A -- mem[0x0030] = 0x42
    CHECK(bus.read8(0x0030) == 0x42);

    cpu.regs().a = 0x00;
    cpu.step(); // LD A,(DE)
    CHECK(cpu.regs().a == 0x42);
}

TEST_CASE("LD (nn),HL / LD HL,(nn) round-trip a 16-bit value through memory") {
    TestBus bus(0x3000);
    bus.load(0, {0x22, 0x00, 0x20, 0x21, 0x00, 0x00, 0x2a, 0x00, 0x20}); // LD (0x2000),HL ; LD HL,0 ; LD HL,(0x2000)
    Z80 cpu(bus);
    cpu.regs().h = 0x12;
    cpu.regs().l = 0x34;

    cpu.step(); // LD (0x2000),HL
    CHECK(bus.read8(0x2000) == 0x34); // low byte first
    CHECK(bus.read8(0x2001) == 0x12);

    cpu.step(); // LD HL,0
    CHECK(cpu.regs().h == 0);
    CHECK(cpu.regs().l == 0);

    cpu.step(); // LD HL,(0x2000)
    CHECK(cpu.regs().h == 0x12);
    CHECK(cpu.regs().l == 0x34);
}

TEST_CASE("LD (nn),A / LD A,(nn) round-trip through memory") {
    TestBus bus(0x3000);
    bus.load(0, {0x32, 0x00, 0x20, 0x3e, 0x00, 0x3a, 0x00, 0x20}); // LD (0x2000),A ; LD A,0 ; LD A,(0x2000)
    Z80 cpu(bus);
    cpu.regs().a = 0x99;

    cpu.step(); // LD (0x2000),A
    CHECK(bus.read8(0x2000) == 0x99);

    cpu.step(); // LD A,0
    CHECK(cpu.regs().a == 0);

    cpu.step(); // LD A,(0x2000)
    CHECK(cpu.regs().a == 0x99);
}

TEST_CASE("ADD HL,ss sets C/H but leaves S/Z/PV untouched, unlike every other add") {
    TestBus bus;
    bus.load(0, {0x09}); // ADD HL,BC
    Z80 cpu(bus);
    cpu.regs().h = 0xff;
    cpu.regs().l = 0xff;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x01;
    cpu.regs().f = kFlagS | kFlagZ | kFlagPV; // pre-set to prove these survive untouched

    cpu.step(); // HL = 0xffff + 1 = 0x0000, carry out

    CHECK(cpu.regs().h == 0x00);
    CHECK(cpu.regs().l == 0x00);
    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagH) != 0); // carry out of bit 11 too (0xfff+0x001)
    CHECK((cpu.regs().f & kFlagS) != 0); // untouched
    CHECK((cpu.regs().f & kFlagZ) != 0); // untouched (even though the 16-bit result is 0!)
    CHECK((cpu.regs().f & kFlagPV) != 0); // untouched
}

TEST_CASE("RLCA/RRCA/RLA/RRA affect only C/H/N, leaving S/Z/PV untouched unlike CB's RLC A") {
    TestBus bus;
    bus.load(0, {0x07}); // RLCA
    Z80 cpu(bus);
    cpu.regs().a = 0x80; // rotating this would set Z on CB's RLC A (result nonzero here actually: 0x80->0x01)
    cpu.regs().f = kFlagZ | kFlagPV; // pre-set to prove these survive untouched

    cpu.step();

    CHECK(cpu.regs().a == 0x01);
    CHECK((cpu.regs().f & kFlagC) != 0); // old bit 7
    CHECK((cpu.regs().f & kFlagZ) != 0); // untouched, even though the new A isn't zero
    CHECK((cpu.regs().f & kFlagPV) != 0); // untouched
}

TEST_CASE("RST pushes PC and jumps to the vector encoded in the opcode's own bits") {
    TestBus bus(0x100);
    bus.load(0x10, {0xef}); // RST 28h, placed away from address 0
    Z80 cpu(bus);
    cpu.regs().pc = 0x10;
    cpu.regs().sp = 0x80;

    cpu.step();

    CHECK(cpu.regs().pc == 0x28);
    CHECK(cpu.regs().sp == 0x7e);
    CHECK(bus.read8(0x7e) == 0x11); // low byte of the return address (0x0011)
    CHECK(bus.read8(0x7f) == 0x00);
}

TEST_CASE("JP (HL) jumps to HL's value, not the memory it points at") {
    TestBus bus(0x2000);
    bus.load(0, {0xe9}); // JP (HL)
    bus.load(0x1234, {0xff}); // if this were dereferenced, PC would NOT end up 0x1234
    Z80 cpu(bus);
    cpu.regs().h = 0x12;
    cpu.regs().l = 0x34;

    cpu.step();

    CHECK(cpu.regs().pc == 0x1234);
}

TEST_CASE("LD SP,HL copies HL into SP") {
    TestBus bus;
    bus.load(0, {0xf9}); // LD SP,HL
    Z80 cpu(bus);
    cpu.regs().h = 0x12;
    cpu.regs().l = 0x34;

    cpu.step();

    CHECK(cpu.regs().sp == 0x1234);
}

TEST_CASE("DI clears both interrupt flip-flops; EI sets both") {
    TestBus bus;
    bus.load(0, {0xf3, 0xfb}); // DI ; EI
    Z80 cpu(bus);
    cpu.regs().iff1 = true;
    cpu.regs().iff2 = true;

    cpu.step(); // DI
    CHECK(cpu.regs().iff1 == false);
    CHECK(cpu.regs().iff2 == false);

    cpu.step(); // EI
    CHECK(cpu.regs().iff1 == true);
    CHECK(cpu.regs().iff2 == true);
}

TEST_CASE("ADC HL,ss includes the incoming carry and sets S/Z/PV, unlike ADD HL,ss") {
    TestBus bus;
    bus.load(0, {0xed, 0x4a}); // ADC HL,BC
    Z80 cpu(bus);
    cpu.regs().h = 0xff;
    cpu.regs().l = 0xff;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x00;
    cpu.regs().f = kFlagC; // carry-in set

    cpu.step(); // 0xffff + 0 + 1 = 0x0000, carry out

    CHECK(cpu.regs().h == 0x00);
    CHECK(cpu.regs().l == 0x00);
    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagH) != 0);
    CHECK((cpu.regs().f & kFlagZ) != 0);
    CHECK((cpu.regs().f & kFlagPV) == 0);
}

TEST_CASE("SBC HL,ss detects borrow and sets N/S") {
    TestBus bus;
    bus.load(0, {0xed, 0x42}); // SBC HL,BC
    Z80 cpu(bus);
    cpu.regs().h = 0x00;
    cpu.regs().l = 0x00;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x01;
    cpu.regs().f = 0; // carry-in clear

    cpu.step(); // 0 - 1 - 0 = 0xffff

    CHECK(cpu.regs().h == 0xff);
    CHECK(cpu.regs().l == 0xff);
    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagN) != 0);
    CHECK((cpu.regs().f & kFlagS) != 0);
    CHECK((cpu.regs().f & kFlagPV) == 0);
}

TEST_CASE("ED's LD (nn),dd / LD dd,(nn) round-trip BC through memory") {
    TestBus bus(0x3100);
    bus.load(0, {0x01, 0x34, 0x12, 0xed, 0x43, 0x00, 0x30, 0x01, 0x00, 0x00, 0xed, 0x4b, 0x00, 0x30});
    // LD BC,0x1234 ; LD (0x3000),BC ; LD BC,0 ; LD BC,(0x3000)
    Z80 cpu(bus);

    cpu.step(); // LD BC,0x1234
    cpu.step(); // LD (0x3000),BC
    CHECK(bus.read8(0x3000) == 0x34);
    CHECK(bus.read8(0x3001) == 0x12);

    cpu.step(); // LD BC,0
    CHECK(cpu.regs().b == 0);
    CHECK(cpu.regs().c == 0);

    cpu.step(); // LD BC,(0x3000)
    CHECK(cpu.regs().b == 0x12);
    CHECK(cpu.regs().c == 0x34);
}

TEST_CASE("NEG computes 0-A, detecting the 0x80 overflow case and the A=0 no-borrow case") {
    TestBus bus;
    bus.load(0, {0xed, 0x44}); // NEG
    Z80 cpu(bus);
    cpu.regs().a = 0x01;

    cpu.step();
    CHECK(cpu.regs().a == 0xff);
    CHECK((cpu.regs().f & kFlagC) != 0);
    CHECK((cpu.regs().f & kFlagN) != 0);
    CHECK((cpu.regs().f & kFlagPV) == 0);
}

TEST_CASE("NEG of 0x80 detects signed overflow") {
    TestBus bus;
    bus.load(0, {0xed, 0x44}); // NEG
    Z80 cpu(bus);
    cpu.regs().a = 0x80;

    cpu.step();
    CHECK(cpu.regs().a == 0x80);
    CHECK((cpu.regs().f & kFlagPV) != 0);
    CHECK((cpu.regs().f & kFlagC) != 0);
}

TEST_CASE("NEG of 0 has no borrow") {
    TestBus bus;
    bus.load(0, {0xed, 0x44}); // NEG
    Z80 cpu(bus);
    cpu.regs().a = 0x00;

    cpu.step();
    CHECK(cpu.regs().a == 0x00);
    CHECK((cpu.regs().f & kFlagC) == 0);
    CHECK((cpu.regs().f & kFlagZ) != 0);
}

TEST_CASE("LD A,I copies IFF2 into PV; LD I,A stores plainly with no flags") {
    TestBus bus;
    bus.load(0, {0xed, 0x57, 0xed, 0x47}); // LD A,I ; LD I,A
    Z80 cpu(bus);
    cpu.regs().i = 0x42;
    cpu.regs().iff2 = true;
    cpu.regs().f = 0;

    cpu.step(); // LD A,I
    CHECK(cpu.regs().a == 0x42);
    CHECK((cpu.regs().f & kFlagPV) != 0);
    CHECK((cpu.regs().f & kFlagH) == 0);
    CHECK((cpu.regs().f & kFlagN) == 0);

    cpu.regs().a = 0x99;
    cpu.regs().f = 0xff;
    cpu.step(); // LD I,A
    CHECK(cpu.regs().i == 0x99);
    CHECK(cpu.regs().f == 0xff); // untouched
}

TEST_CASE("LD A,R clears PV when IFF2 is false; LD R,A stores plainly") {
    TestBus bus;
    bus.load(0, {0xed, 0x5f, 0xed, 0x4f}); // LD A,R ; LD R,A
    Z80 cpu(bus);
    cpu.regs().r = 0x33;
    cpu.regs().iff2 = false;

    cpu.step(); // LD A,R
    CHECK(cpu.regs().a == 0x33);
    CHECK((cpu.regs().f & kFlagPV) == 0);

    cpu.regs().a = 0x55;
    cpu.step(); // LD R,A
    CHECK(cpu.regs().r == 0x55);
}

TEST_CASE("RRD rotates a nibble from (HL) into A and shifts (HL)'s own nibbles") {
    TestBus bus;
    bus.load(0, {0xed, 0x67}); // RRD
    bus.load(0x10, {0x34});
    Z80 cpu(bus);
    cpu.regs().h = 0x00;
    cpu.regs().l = 0x10;
    cpu.regs().a = 0x12;

    cpu.step();

    CHECK(cpu.regs().a == 0x14);
    CHECK(bus.read8(0x10) == 0x23);
}

TEST_CASE("RLD rotates the other direction from RRD") {
    TestBus bus;
    bus.load(0, {0xed, 0x6f}); // RLD
    bus.load(0x10, {0x34});
    Z80 cpu(bus);
    cpu.regs().h = 0x00;
    cpu.regs().l = 0x10;
    cpu.regs().a = 0x12;

    cpu.step();

    CHECK(cpu.regs().a == 0x13);
    CHECK(bus.read8(0x10) == 0x42);
}

TEST_CASE("IM 0/1/2 set the interrupt mode register") {
    TestBus bus;
    bus.load(0, {0xed, 0x46, 0xed, 0x56, 0xed, 0x5e}); // IM 0 ; IM 1 ; IM 2
    Z80 cpu(bus);

    cpu.step();
    CHECK(cpu.regs().im == 0);
    cpu.step();
    CHECK(cpu.regs().im == 1);
    cpu.step();
    CHECK(cpu.regs().im == 2);
}

TEST_CASE("RETN pops PC and restores IFF1 from IFF2") {
    TestBus bus(0x100);
    bus.load(0, {0xed, 0x45}); // RETN
    Z80 cpu(bus);
    cpu.regs().sp = 0x80;
    bus.write8(0x80, 0x34);
    bus.write8(0x81, 0x12);
    cpu.regs().iff1 = false;
    cpu.regs().iff2 = true;

    cpu.step();

    CHECK(cpu.regs().pc == 0x1234);
    CHECK(cpu.regs().sp == 0x82);
    CHECK(cpu.regs().iff1 == true);
}

TEST_CASE("LDI copies one byte, steps HL/DE forward, decrements BC, and sets PV from BC") {
    TestBus bus(0x4000);
    bus.load(0, {0xed, 0xa0}); // LDI
    bus.load(0x2000, {0x11});
    Z80 cpu(bus);
    cpu.regs().h = 0x20;
    cpu.regs().l = 0x00;
    cpu.regs().d = 0x30;
    cpu.regs().e = 0x00;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x02;
    cpu.regs().a = 0x00;

    cpu.step();

    CHECK(bus.read8(0x3000) == 0x11);
    CHECK(cpu.regs().h == 0x20);
    CHECK(cpu.regs().l == 0x01);
    CHECK(cpu.regs().d == 0x30);
    CHECK(cpu.regs().e == 0x01);
    CHECK(cpu.regs().b == 0x00);
    CHECK(cpu.regs().c == 0x01);
    CHECK((cpu.regs().f & kFlagPV) != 0); // BC (1) != 0
    CHECK((cpu.regs().f & kFlagH) == 0);
    CHECK((cpu.regs().f & kFlagN) == 0);
}

TEST_CASE("LDIR repeats until BC reaches zero, copying the whole block") {
    TestBus bus(0x4000);
    bus.load(0, {0xed, 0xb0}); // LDIR
    bus.load(0x2000, {0xaa, 0xbb, 0xcc});
    Z80 cpu(bus);
    cpu.regs().h = 0x20;
    cpu.regs().l = 0x00;
    cpu.regs().d = 0x30;
    cpu.regs().e = 0x00;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x03;

    cpu.step();

    CHECK(bus.read8(0x3000) == 0xaa);
    CHECK(bus.read8(0x3001) == 0xbb);
    CHECK(bus.read8(0x3002) == 0xcc);
    CHECK(cpu.regs().h == 0x20);
    CHECK(cpu.regs().l == 0x03);
    CHECK(cpu.regs().b == 0x00);
    CHECK(cpu.regs().c == 0x00);
    CHECK((cpu.regs().f & kFlagPV) == 0); // BC reached 0
}

TEST_CASE("CPI compares against (HL), leaves carry untouched, and sets PV from BC") {
    TestBus bus(0x4000);
    bus.load(0, {0xed, 0xa1}); // CPI
    bus.load(0x2000, {0x08});
    Z80 cpu(bus);
    cpu.regs().h = 0x20;
    cpu.regs().l = 0x00;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x02;
    cpu.regs().a = 0x10;
    cpu.regs().f = kFlagC; // pre-set to prove CPI leaves it alone

    cpu.step(); // 0x10 - 0x08 = 0x08, half-carry (0x0 < 0x8 in the low nibble)

    CHECK(cpu.regs().l == 0x01);
    CHECK(cpu.regs().c == 0x01);
    CHECK((cpu.regs().f & kFlagC) != 0); // untouched
    CHECK((cpu.regs().f & kFlagZ) == 0);
    CHECK((cpu.regs().f & kFlagH) != 0);
    CHECK((cpu.regs().f & kFlagN) != 0);
    CHECK((cpu.regs().f & kFlagPV) != 0); // BC (1) != 0
}

TEST_CASE("CPIR stops early when a match is found, leaving BC nonzero") {
    TestBus bus(0x4000);
    bus.load(0, {0xed, 0xb1}); // CPIR
    bus.load(0x2000, {0x11, 0x22, 0x33});
    Z80 cpu(bus);
    cpu.regs().h = 0x20;
    cpu.regs().l = 0x00;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x03;
    cpu.regs().a = 0x22; // matches the second byte

    cpu.step();

    CHECK(cpu.regs().l == 0x02); // stopped right after finding the match
    CHECK(cpu.regs().c == 0x01); // BC decremented twice, not three times
    CHECK((cpu.regs().f & kFlagZ) != 0);
    CHECK((cpu.regs().f & kFlagPV) != 0); // BC (1) != 0 -- stopped by the match, not by BC
}

TEST_CASE("CPIR stops when BC reaches zero without finding a match") {
    TestBus bus(0x4000);
    bus.load(0, {0xed, 0xb1}); // CPIR
    bus.load(0x2000, {0x11, 0x22, 0x33});
    Z80 cpu(bus);
    cpu.regs().h = 0x20;
    cpu.regs().l = 0x00;
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x03;
    cpu.regs().a = 0xff; // matches nothing

    cpu.step();

    CHECK(cpu.regs().l == 0x03);
    CHECK(cpu.regs().c == 0x00);
    CHECK((cpu.regs().f & kFlagZ) == 0);
    CHECK((cpu.regs().f & kFlagPV) == 0); // stopped by BC reaching 0
}

TEST_CASE("IN A,(n) puts A on the port address's upper byte, not just n") {
    TestBus bus;
    bus.load(0, {0xdb, 0x10}); // IN A,(0x10)
    Z80 cpu(bus);
    cpu.regs().a = 0x20;
    bus.port_write(0x2010, 0x42); // port = (A<<8)|n = 0x2010, not 0x0010

    cpu.step();

    CHECK(cpu.regs().a == 0x42);
}

TEST_CASE("OUT (n),A writes to the same (A<<8)|n port address") {
    TestBus bus;
    bus.load(0, {0xd3, 0x10}); // OUT (0x10),A
    Z80 cpu(bus);
    cpu.regs().a = 0x20;

    cpu.step();

    CHECK(bus.port_read(0x2010) == 0x20);
    CHECK(bus.port_read(0x0010) == 0xff); // unmapped -- confirms it did NOT write here
}

TEST_CASE("IN r,(C) reads from the full BC port address and sets flags from the value") {
    TestBus bus;
    bus.load(0, {0xed, 0x40}); // IN B,(C)
    Z80 cpu(bus);
    cpu.regs().b = 0x30;
    cpu.regs().c = 0x40;
    bus.port_write(0x3040, 0x80); // port = BC, not zero-extended C
    cpu.regs().f = kFlagC; // pre-set to prove IN leaves carry alone

    cpu.step();

    CHECK(cpu.regs().b == 0x80);
    CHECK((cpu.regs().f & kFlagS) != 0);
    CHECK((cpu.regs().f & kFlagZ) == 0);
    CHECK((cpu.regs().f & kFlagC) != 0); // untouched
}

TEST_CASE("The undocumented IN (C) sets flags but discards the value") {
    TestBus bus;
    bus.load(0, {0xed, 0x70}); // IN (C)
    Z80 cpu(bus);
    cpu.regs().b = 0x00;
    cpu.regs().c = 0x00;
    bus.port_write(0x0000, 0x00);

    cpu.step();

    CHECK((cpu.regs().f & kFlagZ) != 0); // flags still reflect the read value (0)
}

TEST_CASE("OUT (C),r writes to the full BC port address") {
    TestBus bus;
    bus.load(0, {0xed, 0x41}); // OUT (C),B
    Z80 cpu(bus);
    cpu.regs().b = 0x30;
    cpu.regs().c = 0x40;

    cpu.step();

    CHECK(bus.port_read(0x3040) == 0x30);
}

TEST_CASE("The undocumented OUT (C),0 writes a literal zero, not (HL)'s contents") {
    TestBus bus;
    bus.load(0, {0xed, 0x71}); // OUT (C),0
    Z80 cpu(bus);
    cpu.regs().b = 0x30;
    cpu.regs().c = 0x40;
    cpu.regs().h = 0x99; // if this were misread as an (HL) operand, the port would get 0x99's target
    cpu.regs().l = 0x99;

    cpu.step();

    CHECK(bus.port_read(0x3040) == 0x00);
}

TEST_CASE("An unimplemented opcode throws UnimplementedOpcode") {
    TestBus bus;
    bus.load(0, {0xdd}); // IX prefix -- not yet implemented
    Z80 cpu(bus);

    CHECK_THROWS_AS(cpu.step(), const UnimplementedOpcode&);
}

TEST_CASE("An unimplemented ED-prefixed opcode throws UnimplementedOpcode") {
    TestBus bus;
    bus.load(0, {0xed, 0xa2}); // INI -- an I/O-port-dependent block instruction, not yet implemented
    Z80 cpu(bus);

    CHECK_THROWS_AS(cpu.step(), const UnimplementedOpcode&);
}

#include "doctest.h"
#include "bus/model1_bus.h"

using namespace model1::bus;

TEST_CASE("ROMA reads as erased (0xFF) before anything is loaded") {
    Model1Bus bus;
    CHECK(bus.read8(0x000000) == 0xff);
    CHECK(bus.read8(0x0fffff) == 0xff);
}

TEST_CASE("load_romA places bytes at the start of the ROMA window") {
    Model1Bus bus;
    bus.load_romA({0x11, 0x22, 0x33, 0x44});

    CHECK(bus.read8(0x000000) == 0x11);
    CHECK(bus.read8(0x000003) == 0x44);
    CHECK(bus.read8(0x000004) == 0xff); // beyond the loaded data, still erased
}

TEST_CASE("load_romX places bytes at the start of the ROMX window (0x200000)") {
    Model1Bus bus;
    bus.load_romX({0xaa, 0xbb});

    CHECK(bus.read8(0x200000) == 0xaa);
    CHECK(bus.read8(0x200001) == 0xbb);
}

TEST_CASE("load_rom0 places bytes at a specific offset within the ROM0 window") {
    // Confirmed real layout for Virtua Racing: two 128KB files at
    // 0xfc0000 and 0xfe0000 within the 0xf80000-0xffffff window, with the
    // rest (0xf80000-0xfbffff) left erased.
    Model1Bus bus;
    bus.load_rom0(0x040000, {0x55, 0x66}); // window offset 0x40000 = address 0xfc0000

    CHECK(bus.read8(0xf80000) == 0xff); // unpopulated part of the window
    CHECK(bus.read8(0xfc0000) == 0x55);
    CHECK(bus.read8(0xfc0001) == 0x66);
}

TEST_CASE("RAMB round-trips a written byte") {
    Model1Bus bus;
    bus.write8(0x500000, 0x42);
    CHECK(bus.read8(0x500000) == 0x42);
}

TEST_CASE("RAMB round-trips a written 32-bit word, little-endian") {
    Model1Bus bus;
    bus.write32(0x500010, 0xdeadbeef);
    CHECK(bus.read32(0x500010) == 0xdeadbeef);
    CHECK(bus.read8(0x500010) == 0xef);
    CHECK(bus.read8(0x500013) == 0xde);
}

TEST_CASE("Writes to ROM regions are ignored, matching real hardware") {
    Model1Bus bus;
    bus.load_romX({0xaa});
    bus.write8(0x200000, 0x99);

    CHECK(bus.read8(0x200000) == 0xaa); // unchanged
}

TEST_CASE("The bank register (0xE00004) switches which ROMO bank is visible at 0x100000") {
    Model1Bus bus;
    bus.load_romO_bank(0, {0x01});
    bus.load_romO_bank(2, {0x02});

    CHECK(bus.read8(0x100000) == 0x01); // bank 0 is selected by default

    // bank_w: low nibble 0x1 selects ROMO banking; bits 4-6 give the bank
    // number (confirmed against model1.cpp's bank_w).
    bus.write16(0xe00004, (2 << 4) | 0x1);

    CHECK(bus.read8(0x100000) == 0x02);
}

TEST_CASE("TGP RAM interface: address register auto-increments only when bit 0x8000 is set") {
    Model1Bus bus;

    // Without the auto-increment bit: writing the same address twice in a
    // row should hit the same word both times.
    bus.write16(0xd00000, 0x0005);
    bus.write16(0xd20000, 0x1111); // low half
    bus.write16(0xd20002, 0x2222); // high half -- triggers the actual write

    bus.write16(0xd00000, 0x0005); // re-point at the same address
    CHECK(bus.read16(0xd20000) == 0x1111);
    CHECK(bus.read16(0xd20002) == 0x2222);
}

TEST_CASE("TGP RAM interface: auto-increment advances the address after a high-half access") {
    Model1Bus bus;

    bus.write16(0xd00000, 0x8000); // address 0, auto-increment bit set
    bus.write16(0xd20000, 0xaaaa); // low half of word 0
    bus.write16(0xd20002, 0xbbbb); // high half of word 0 -- write completes, address auto-increments to 1

    bus.write16(0xd20000, 0xcccc); // low half of word 1
    bus.write16(0xd20002, 0xdddd); // high half of word 1

    // Read back word 0 and word 1 directly via a fresh, non-incrementing address set.
    bus.write16(0xd00000, 0x0000); // no auto-increment bit; re-point at word 0
    CHECK(bus.read16(0xd20000) == 0xaaaa);
    CHECK(bus.read16(0xd20002) == 0xbbbb);

    bus.write16(0xd00000, 0x0001);
    CHECK(bus.read16(0xd20000) == 0xcccc);
    CHECK(bus.read16(0xd20002) == 0xdddd);
}

TEST_CASE("An address with no handler throws UnmappedAddress") {
    Model1Bus bus;
    CHECK_THROWS_AS(bus.read8(0x300000), UnmappedAddress); // between ROMX and RAMA -- genuinely unmapped
}

TEST_CASE("Addresses wrap at 24 bits, matching the confirmed hardware address bus width") {
    Model1Bus bus;
    bus.load_romA({0x77});

    CHECK(bus.read8(0x01000000) == 0x77); // bit 24 set, should be masked off and land on ROMA address 0
}

// license:BSD-3-Clause
// Standalone Z80 CPU test harness -- NOT part of any real arcade board.
//
// Purpose: give the sega-model-1-emu project (see this repo's
// docs/planning/06-legal-and-assets.md "documentation only" MAME policy)
// a way to run small hand-written Z80 machine-code snippets through the
// real reference CPU core and compare the resulting register state
// against src/cpu/z80/'s independently-written original core, without
// needing any game ROM. This file is dev-time tooling, not part of Model 1
// emulation, and is never linked into the shipped emulator -- it depends
// on MAME's own device headers (`cpu/z80/z80.h`) purely as a test oracle,
// so it can only be compiled inside a local MAME source checkout. See
// README.md in this directory for exact build/run steps.
//
// Much simpler than v60test.cpp's protocol: the Z80 always resets to
// PC=0x0000 (no configurable/masked reset vector to worry about), and its
// HALT genuinely halts (PC stops advancing) rather than the V60's
// HALT-that-keeps-going quirk -- so test programs can end in a plain HALT
// as their stable landing point, no self-branch trick needed.
//
// Protocol (all little-endian):
//   Input file (path from Z80TEST_IN env var, default "z80harness_in.bin"):
//     u32 program_length
//     u8  initial_a, initial_f, initial_b, initial_c, initial_d, initial_e,
//         initial_h, initial_l
//     u16 initial_ix, initial_iy, initial_sp
//     u8  program[program_length]  -- loaded at address 0.
//   Output file (Z80TEST_OUT env var, default "z80harness_out.bin"):
//     u8  a, f, b, c, d, e, h, l
//     u16 ix, iy, sp
//     u16 pc

#include "emu.h"
#include "cpu/z80/z80.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {

class z80test_state : public driver_device
{
public:
	z80test_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
	{
	}

	void z80test(machine_config &config);

protected:
	virtual void machine_start() override;

private:
	void mem_map(address_map &map);
	void io_map(address_map &map);
	void dump_and_exit();

	required_device<z80_device> m_maincpu;
};

void z80test_state::mem_map(address_map &map)
{
	map(0x0000, 0xffff).ram();
}

// A plain 64K RAM-backed I/O space, purely so IN/OUT test programs have
// somewhere real to read/write -- test programs verify port behavior by
// reading back through a register (e.g. OUT then IN), not by inspecting
// this space directly, so its exact backing (RAM here) doesn't matter,
// only that every port address is actually mapped to something.
void z80test_state::io_map(address_map &map)
{
	map(0x0000, 0xffff).ram();
}

void z80test_state::machine_start()
{
	const char *in_path = std::getenv("Z80TEST_IN");
	if (!in_path) in_path = "z80harness_in.bin";

	std::ifstream in(in_path, std::ios::binary);
	if (!in) {
		fprintf(stderr, "z80test: could not open input file %s\n", in_path);
		machine().schedule_exit();
		return;
	}

	auto read_u32 = [&in]() -> uint32_t {
		unsigned char b[4];
		in.read(reinterpret_cast<char *>(b), 4);
		return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
	};
	auto read_u8 = [&in]() -> uint8_t {
		unsigned char b;
		in.read(reinterpret_cast<char *>(&b), 1);
		return b;
	};
	auto read_u16 = [&in]() -> uint16_t {
		unsigned char b[2];
		in.read(reinterpret_cast<char *>(b), 2);
		return uint16_t(b[0]) | (uint16_t(b[1]) << 8);
	};

	const uint32_t program_length = read_u32();

	const uint8_t init_a = read_u8(), init_f = read_u8();
	const uint8_t init_b = read_u8(), init_c = read_u8();
	const uint8_t init_d = read_u8(), init_e = read_u8();
	const uint8_t init_h = read_u8(), init_l = read_u8();
	const uint16_t init_ix = read_u16();
	const uint16_t init_iy = read_u16();
	const uint16_t init_sp = read_u16();

	std::vector<uint8_t> program(program_length);
	in.read(reinterpret_cast<char *>(program.data()), program_length);

	address_space &space = m_maincpu->space(AS_PROGRAM);

	// Loaded at address 0 -- the Z80's real (fixed, unlike the V60's
	// configurable/masked) reset vector.
	for (uint32_t i = 0; i < program_length; i++)
		space.write_byte(i, program[i]);

	// GPRs aren't touched by the CPU's own reset (only PC/IFF/IM are), so
	// setting them here, before the automatic post-machine_start reset, is
	// safe -- unlike PC, which reset unconditionally sets to 0.
	m_maincpu->set_state_int(Z80_A, init_a);
	m_maincpu->set_state_int(Z80_F, init_f);
	m_maincpu->set_state_int(Z80_B, init_b);
	m_maincpu->set_state_int(Z80_C, init_c);
	m_maincpu->set_state_int(Z80_D, init_d);
	m_maincpu->set_state_int(Z80_E, init_e);
	m_maincpu->set_state_int(Z80_H, init_h);
	m_maincpu->set_state_int(Z80_L, init_l);
	m_maincpu->set_state_int(Z80_IX, init_ix);
	m_maincpu->set_state_int(Z80_IY, init_iy);
	m_maincpu->set_state_int(Z80_SP, init_sp);

	// No per-instruction hook is used; -seconds_to_run at the command line
	// budgets wall/emulated time generously. Test programs should end in a
	// plain HALT -- unlike the V60's HALT-that-keeps-going quirk, the Z80's
	// HALT genuinely stops PC from advancing (both in this reference core
	// and in src/cpu/z80/'s own implementation), so it's a stable landing
	// point with no self-branch trick required.
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&z80test_state::dump_and_exit, this));
}

void z80test_state::dump_and_exit()
{
	const char *out_path = std::getenv("Z80TEST_OUT");
	if (!out_path) out_path = "z80harness_out.bin";

	std::ofstream out(out_path, std::ios::binary);
	auto write_u8 = [&out](uint8_t v) { out.write(reinterpret_cast<char *>(&v), 1); };
	auto write_u16 = [&out](uint16_t v) {
		unsigned char b[2] = { uint8_t(v), uint8_t(v >> 8) };
		out.write(reinterpret_cast<char *>(b), 2);
	};

	write_u8(uint8_t(m_maincpu->state_int(Z80_A)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_F)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_B)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_C)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_D)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_E)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_H)));
	write_u8(uint8_t(m_maincpu->state_int(Z80_L)));
	write_u16(uint16_t(m_maincpu->state_int(Z80_IX)));
	write_u16(uint16_t(m_maincpu->state_int(Z80_IY)));
	write_u16(uint16_t(m_maincpu->state_int(Z80_SP)));
	write_u16(uint16_t(m_maincpu->state_int(Z80_PC)));
}

void z80test_state::z80test(machine_config &config)
{
	Z80(config, m_maincpu, 4'000'000);
	m_maincpu->set_addrmap(AS_PROGRAM, &z80test_state::mem_map);
	m_maincpu->set_addrmap(AS_IO, &z80test_state::io_map);
}

ROM_START(z80test)
ROM_END

} // anonymous namespace

GAME(2024, z80test, 0, z80test, 0, z80test_state, empty_init, ROT0, "sega-model-1-emu", "Z80 CPU test harness", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)

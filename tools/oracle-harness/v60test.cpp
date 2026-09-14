// license:BSD-3-Clause
// Standalone V60 CPU test harness -- NOT part of any real arcade board.
//
// Purpose: give the sega-model-1-emu project (see this repo's
// docs/planning/06-legal-and-assets.md "documentation only" MAME policy)
// a way to run small hand-written V60 machine-code snippets through the
// real reference CPU core and compare the resulting register state
// against src/cpu/v60/'s independently-written original core, without
// needing any game ROM. This file is dev-time tooling, not part of Model 1
// emulation, and is never linked into the shipped emulator -- it depends
// on MAME's own device headers (`cpu/v60/v60.h`) purely as a test oracle,
// so it can only be compiled inside a local MAME source checkout. See
// README.md in this directory for exact build/run steps.
//
// Protocol (all little-endian):
//   Input file (path from V60TEST_IN env var, default "v60harness_in.bin"):
//     u32 instruction_budget   -- currently unused, reserved
//     u32 program_length
//     u32 initial_regs[32]     -- R0..R28, AP, FP, SP
//     u8  program[program_length]  -- loaded at the V60's real reset vector.
//                                     v60_device defaults m_start_pc to
//                                     0xFFFFFFF0, but this configuration's
//                                     address bus is masked to 24 bits
//                                     (confirmed both by MAME's own address
//                                     validation error when the map exceeds
//                                     it, and independently by the NEC
//                                     datasheet comment already quoted in
//                                     docs/hardware-notes/07-v60-architecture.md),
//                                     so the vector actually lands at
//                                     0x00FFFFF0. MAME resets the CPU
//                                     automatically after machine_start()
//                                     runs, which sets PC to that vector
//                                     regardless of anything set_pc() was
//                                     called with here, so the program has
//                                     to actually live there for reset to
//                                     find it.
//   Output file (V60TEST_OUT env var, default "v60harness_out.bin"):
//     u32 final_regs[32]
//     u32 final_pc

#include "emu.h"
#include "cpu/v60/v60.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace {

class v60test_state : public driver_device
{
public:
	v60test_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
	{
	}

	void v60test(machine_config &config);

protected:
	virtual void machine_start() override;

private:
	void mem_map(address_map &map);
	void dump_and_exit();

	required_device<v60_device> m_maincpu;
};

void v60test_state::mem_map(address_map &map)
{
	// This v60_device configuration masks addresses to 24 bits (confirmed
	// by MAME's own address-map validation, and matching the NEC
	// datasheet's documented 24-bit address bus) -- so the real reset
	// vector location is 0x00fffff0, not the class default's raw
	// 0xfffffff0.
	map(0x000000, 0x00ffff).ram(); // general scratch RAM (stack, data)
	map(0xff0000, 0xffffff).ram(); // covers the reset vector, 0x00fffff0
}

void v60test_state::machine_start()
{
	const char *in_path = std::getenv("V60TEST_IN");
	if (!in_path) in_path = "v60harness_in.bin";

	std::ifstream in(in_path, std::ios::binary);
	if (!in) {
		fprintf(stderr, "v60test: could not open input file %s\n", in_path);
		machine().schedule_exit();
		return;
	}

	auto read_u32 = [&in]() -> uint32_t {
		unsigned char b[4];
		in.read(reinterpret_cast<char *>(b), 4);
		return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
	};

	const uint32_t instruction_budget = read_u32();
	const uint32_t program_length = read_u32();

	uint32_t initial_regs[32];
	for (auto &r : initial_regs) r = read_u32();

	std::vector<uint8_t> program(program_length);
	in.read(reinterpret_cast<char *>(program.data()), program_length);

	address_space &space = m_maincpu->space(AS_PROGRAM);

	// A stable landing pad at address 0: an infinite self-branch (BR8 +0).
	// Lets test programs that jump/call to a low absolute address (e.g. via
	// an immediate operand) settle into something checkable instead of
	// drifting through uninitialized (HALT-decoding, but not
	// halting -- see the gotcha in README.md) scratch memory for the rest
	// of the run.
	space.write_byte(0, 0x6a);
	space.write_byte(1, 0x00);

	// Loaded at the real (24-bit-masked) reset vector, not address 0 --
	// see the file header comment for why.
	constexpr uint32_t kResetVector = 0x00fffff0;
	for (uint32_t i = 0; i < program_length; i++)
		space.write_byte(kResetVector + i, program[i]);

	// V60_R0 is the first named register in the v60_device state enum
	// (see src/devices/cpu/v60/v60.h); registers are contiguous from
	// there, so V60_R0+i addresses R0..R28, AP, FP, SP in order. GPRs
	// aren't touched by the CPU's own reset (only PC/PSW/flags are), so
	// setting them here, before the automatic post-machine_start reset,
	// is safe -- unlike PC, which reset unconditionally overwrites.
	for (int i = 0; i < 32; i++)
		m_maincpu->set_state_int(V60_R0 + i, initial_regs[i]);

	// No per-instruction hook is used (avoids depending on debugger
	// internals); instead we budget wall/emulated time generously via
	// -seconds_to_run at the command line. Test programs MUST end in an
	// infinite self-branch (BR8 with displacement 0, i.e. literally
	// "branch to myself") -- landing on uninitialized (zero) memory is
	// NOT a safe stopping point, since opcode 0x00 is HALT, and this core
	// (matching real hardware's simplification) just advances PC by 1 and
	// keeps going rather than actually halting, so execution would drift
	// through memory for the rest of the time budget instead of settling
	// on a stable, checkable state.
	(void)instruction_budget;
	machine().add_notifier(MACHINE_NOTIFY_EXIT, machine_notify_delegate(&v60test_state::dump_and_exit, this));
}

void v60test_state::dump_and_exit()
{
	const char *out_path = std::getenv("V60TEST_OUT");
	if (!out_path) out_path = "v60harness_out.bin";

	std::ofstream out(out_path, std::ios::binary);
	auto write_u32 = [&out](uint32_t v) {
		unsigned char b[4] = { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) };
		out.write(reinterpret_cast<char *>(b), 4);
	};

	for (int i = 0; i < 32; i++)
		write_u32(uint32_t(m_maincpu->state_int(V60_R0 + i)));
	write_u32(uint32_t(m_maincpu->pc()));
}

void v60test_state::v60test(machine_config &config)
{
	V60(config, m_maincpu, 16'000'000);
	m_maincpu->set_addrmap(AS_PROGRAM, &v60test_state::mem_map);
}

ROM_START(v60test)
ROM_END

} // anonymous namespace

GAME(2024, v60test, 0, v60test, 0, v60test_state, empty_init, ROT0, "sega-model-1-emu", "V60 CPU test harness", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)

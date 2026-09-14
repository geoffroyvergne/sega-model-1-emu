# Sega Model 1 Emulator

A from-scratch, C++ software emulator for the Sega Model 1 arcade system board,
targeting the three commercially released Model 1 titles:

- **Virtua Racing** (1992)
- **Virtua Fighter** (1993)
- **Star Wars Arcade** (1993)

This is a solo/small-team hobbyist reverse-engineering and emulation project.
It is **not** a hardware clone — see [docs/planning/01-overview.md](docs/planning/01-overview.md)
for scope.

## Status

The NEC V60 main CPU core (`src/cpu/v60/`) covers a substantial
instruction/addressing-mode subset and is validated against a real MAME
`v60_device` reference (no game ROM needed — see `tools/oracle-harness/`,
37/37 hand-written test programs matching exactly). The Model 1 I/O
board's Z80 core (`src/cpu/z80/`) is similarly underway and oracle-
validated. The Geometrizer/rasterizer video pipeline (transform, project,
clip, light, sort, rasterize) is implemented as pure, ROM-independent
math. The bus/memory map, ROM loading, sound board, TGP coprocessor, and
2D tile layer are all still early or not started. See
[docs/planning/05-roadmap.md](docs/planning/05-roadmap.md) for the
authoritative, up-to-date phase-by-phase status — it's updated after every
increment, unlike this summary.

Run the unit tests with:

```
cmake -S . -B build && cmake --build build && ./build/model1_unit_tests
```

## Boot-tracing a real ROM

`model1emu` (built by the same `cmake --build build` above) is not yet a
playable emulator — there's no video/sound/input wired up, and most of the
bus is still stubbed. What it does today: load a real ROM set, reset the
V60 core against it, and trace execution until it hits something this
project doesn't implement yet. This turns "what does the core need next"
into a concrete, reproducible PC/opcode/address instead of a guess:

```
./build/model1emu /path/to/your/vf/roms vf
./build/model1emu /path/to/your/vr/roms vr
```

The ROM directory must contain your own legally-dumped files named exactly
as MAME's driver expects (see `src/board/games/virtua_fighter.cpp` /
`virtua_racing.cpp` for the exact filenames/sizes/CRC32s, sourced from
MAME's ROM_START blocks as fingerprints only — see the legal doc below).
A third, optional argument caps the instruction budget (default 2,000,000).
The trace stops and reports exactly where on: an unimplemented V60 opcode
or addressing mode, an unmapped bus address, or a long run of PC advancing
by exactly +1 (a sign of drifting through uninitialized memory, including
the V60's own `HALT`, which — per `docs/hardware-notes/07-v60-architecture.md`
— doesn't actually halt anything).

Start here for the full picture:

1. [docs/planning/01-overview.md](docs/planning/01-overview.md) — goals, scope, non-goals
2. [docs/planning/02-strategy.md](docs/planning/02-strategy.md) — how we're approaching this
3. [docs/planning/03-challenges-and-risks.md](docs/planning/03-challenges-and-risks.md) — what makes this hard
4. [docs/planning/04-architecture.md](docs/planning/04-architecture.md) — system design
5. [docs/planning/05-roadmap.md](docs/planning/05-roadmap.md) — phased implementation plan
6. [docs/planning/06-legal-and-assets.md](docs/planning/06-legal-and-assets.md) — ROMs, licensing, MAME usage policy
7. [docs/planning/07-testing-and-validation.md](docs/planning/07-testing-and-validation.md) — how we'll know it's correct

Real hardware facts gathered so far (with sources) live in
[docs/hardware-notes/](docs/hardware-notes/00-sources.md) — several already
corrected assumptions made in the planning docs above (e.g. the sound board's
CPU, and the TGP's emulation strategy), so treat hardware-notes as the
up-to-date ground truth when the two disagree.

## Requirements to run (once built)

You must supply your own legally-obtained ROM dumps. None are provided or
linked by this project. See the legal doc above.

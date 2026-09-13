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

Phase 0 (research & tooling) is essentially complete. Phase 1 (the NEC V60
CPU core) is underway: `src/cpu/v60/` implements a register file, flags,
and a growing, unit-tested instruction/addressing-mode slice — HALT, NOP,
MOV, CMP, ADD, SUB, all 15 conditional branches, and JMP/JSR/RSR/RET, over
register-direct/indirect/autoincrement/autodecrement/displacement-8
addressing (see `docs/hardware-notes/07-v60-architecture.md`). Run the unit
tests with:

```
cmake -S . -B build && cmake --build build && ./build/model1_unit_tests
```

Beyond our own unit tests, `tools/oracle-harness/` validates this core
against a real MAME `v60_device` (no game ROM needed) — currently 8/8
hand-written test programs match the reference exactly. See that
directory's README to set it up.

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

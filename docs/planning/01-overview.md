# 01 — Overview

## What we're building

A **software emulator** for the Sega Model 1 arcade board — the hardware that
ran Sega's first real-time 3D polygon games. Target compatibility list, in
priority order:

1. **Virtua Racing** (1992) — the launch title, earliest/simplest board revision
2. **Virtua Fighter** (1993) — the second title, established the fighting-game genre
3. **Star Wars Arcade** *(Star Wars Trilogy Arcade)* (1993) — analog flight-stick shooter

These three are the confirmed commercial Model 1 releases we're scoping for.
There may be one or two more obscure/regional Model 1 board variants (e.g.
twin/deluxe cabinet revisions of the above) — those are **not** goals for v1,
only the standard upright versions of the three games above.

## Why this is worth calling out as "very challenging"

Model 1 is one of the least publicly documented Sega arcade boards, precisely
*because* it was Sega's first real 3D board and used bespoke, short-lived
hardware (superseded a year later by Model 2). Compare this to Model 2/3 or
the Genesis/Mega Drive, which have decades of exhaustive public reverse
engineering. Full compatibility here is a genuine, multi-month-to-multi-year
reverse engineering and emulation effort, not a weekend project.

## Goals (v1)

- Boot all three games to attract mode and let them be played start-to-finish
  with correct-feeling controls, at full speed, with sound.
- Cycle-accurate enough that gameplay, timing-sensitive collision/physics, and
  audio sync feel right — not necessarily bit-exact hardware accuracy on day one.
- A usable frontend: pick a game, configure controls (including analog wheel
  /joystick support), play.
- A codebase organized so accuracy can be incrementally improved over time
  (this is how every serious emulator project actually evolves).

## Non-goals (v1)

- Netplay, rewind, save-state-based TAS tooling — nice stretch goals, not v1.
- Cabinet-specific extras: force-feedback wheel motors, deluxe/moving cabinet
  simulation, dedicated linkage hardware (Virtua Racing twin/link mode).
- Supporting every regional ROM revision/bootleg — one known-good revision per
  game is the v1 target; others come later once the core is solid.
- Building actual FPGA/PCB hardware. This is a *software* emulator (see the
  architecture doc). A hardware recreation is a separate project with a very
  different skill set (RTL/Verilog, PCB design) and was explicitly ruled out
  for this effort.
- Perfect low-level emulation (LLE) of the 3D math coprocessor (the "TGP") on
  day one — see the challenges doc. We start with high-level emulation (HLE)
  of its behavior and only pursue LLE if/when real firmware dumps and time
  allow.

## Who this is for

A solo hobbyist (or very small team) with C++ systems programming experience,
willing to do real reverse-engineering work (reading disassembly, comparing
traces, reading whatever schematics/service-manual scans exist) rather than
just wiring together existing libraries. Expect the research phase to take as
long as, or longer than, the coding phase.

## How to read the rest of these docs

- **Strategy** explains *how* we tackle a board with almost no ground truth.
- **Challenges** is the honest risk register — read it before committing time.
- **Architecture** is the target code structure the roadmap builds toward.
- **Roadmap** breaks the work into ordered, checkpointed phases.
- **Legal & assets** covers ROM sourcing and how we use MAME as a reference.
- **Testing & validation** covers how we prove correctness without a real PCB.

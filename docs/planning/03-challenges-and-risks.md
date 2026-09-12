# 03 — Challenges & Risks

Read this before committing serious time. Ordered roughly by how much they
threaten the project, not by implementation order.

## 1. The TGP (3D math coprocessor) is a near-black-box — **highest risk**

Model 1's polygon math (matrix transforms, projection, clipping) is done by a
dedicated coprocessor separate from the main game CPU. It is one of the
least-documented chips in Sega's arcade history, and even MAME's own Model 1
driver has historically leaned on **high-level emulation** (reimplementing
what the chip *produces* algorithmically) rather than **low-level emulation**
(running the chip's actual dumped microcode), because verified, complete
firmware/microcode dumps for this specific board are hard to source publicly.

- **Impact if unresolved:** wrong-looking 3D geometry, incorrect object
  positioning, or subtly wrong game physics/timing in all three target games,
  since all three are 3D-only — there's no fallback 2D mode to ship instead.
- **Mitigation:** confirm exact current status (HLE vs LLE, and any firmware
  dump availability) during Phase 0 research before estimating further. Budget
  the HLE path as the baseline plan; treat LLE as a stretch goal contingent on
  what's actually findable.

## 2. Multi-processor timing synchronization

The board has at minimum a main CPU, a sound CPU (Z80), and the TGP running
concurrently, all interacting through shared/dual-port memory and interrupts.
Getting relative timing wrong causes race conditions that are invisible on
real, deterministic hardware but manifest as glitches, hangs, or desyncs in
emulation (classic "works on the first frame, hangs on frame 200" bugs).

- **Mitigation:** a single deterministic scheduler (see architecture doc)
  driven by a shared cycle counter, not free-running OS threads per chip.

## 3. Extremely sparse public documentation vs. later boards

Model 1 shipped for roughly a year before Model 2 superseded it, so the
enthusiast reverse-engineering community invested far less effort here than
in, say, the Genesis or even Model 2/3. Expect to hit dead ends where "just
look it up" isn't an option, and where the only ground truth is your own
tracing against MAME's driver or against a real board if you have access to
one.

## 4. Custom quad-based polygon rasterizer

Sega's hardware of this era favored shaded **quads**, not the triangle
pipelines that became standard later. Expect non-obvious behavior around
sorting (painter's-algorithm-style draw order rather than a Z-buffer),
clipping edge cases, and shading modes (flat vs. Gouraud) that don't map
cleanly onto modern GPU-oriented rasterizer designs. Writing this on the CPU
(matching original behavior) rather than "cheating" via a modern GPU pipeline
is important for correctness, especially for sorting-dependent visual bugs
that games may have shipped with and that speedrunners/purists will notice.

## 5. Analog and unusual per-game I/O

- Virtua Racing: analog steering wheel + accelerator/brake pedals, plus a
  view-change button and gear shifter on some cabinet variants.
- Virtua Fighter: 8-way digital joystick + punch/guard/kick buttons — the
  simplest I/O of the three, but hit-detection timing must be right for the
  game to feel correct.
- Star Wars Arcade: analog flight-stick-style controller with trigger.

Each needs its own input board emulation and a sensible modern mapping
(gamepad analog stick/triggers, racing wheel peripherals, keyboard fallback)
without hand-holding players through a control scheme the original cabinet
never needed to explain.

## 6. No real hardware to validate against (likely)

Unless you own an actual Model 1 PCB, every "ground truth" claim comes from
software (MAME) or from scanned service manuals/schematics floating around
preservation communities — never from a logic analyzer on real silicon. That
ceiling on verification is real; be honest with yourself about which bugs are
"probably right, matches the oracle" versus "we have no way to know."

## 7. ROM/board revision differences across the three games

Model 1 wasn't one static board — RAM sizes, ROM board sub-revisions, and I/O
daughterboards varied per title and sometimes per region. The "generic Model
1 core" needs a per-game configuration layer from day one rather than
hard-coded assumptions baked in from whichever game you bring up first (see
architecture doc's per-game config approach).

## 8. Legal exposure around ROMs

Every game's ROM data is copyrighted and cannot be included, linked, or
distributed by this project. This isn't just a checkbox — it shapes how the
project is structured (no ROMs in the repo, no ROM download tooling, clear
docs telling users they must dump their own legally-owned boards). See the
legal doc.

## 9. Performance vs. accuracy tradeoff

An interpreter-based CPU core plus a CPU-side software rasterizer plus
accurate audio synthesis is a lot of work per emulated frame. It should still
be very achievable at full speed on modern hardware (this is *far* less
demanding than, say, a PS2 or GameCube emulator), but don't assume it's free —
profile early once subsystems are wired together, and keep the option of a
recompiler for the main CPU on the table as a later optimization, not a
day-one requirement.

## 10. Realistic timeline

This is a part-time hobby project analog to "write a new SNES-class emulator
from scratch, but for a system with 1/100th the documentation." Expect the
research + bring-up of the first game (Virtua Racing) to plausibly take
several months of part-time effort before it's playable at all, and the full
three-game v1 scope to run 12–24 months part-time. Treat any tighter
estimate with suspicion.

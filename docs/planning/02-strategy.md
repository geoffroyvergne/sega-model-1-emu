# 02 — Strategy

## Core principle: research before code, subsystem-by-subsystem bring-up

You cannot write an accurate CPU core, video renderer, or 3D math emulation
for a board this obscure by guessing. The strategy is:

1. **Build ground truth first.** Before writing our own emulator code, set up
   MAME's existing (GPL) Model 1 driver as a *reference oracle* — build it,
   run it against our own legally-dumped ROMs, and use its debugger to
   extract memory maps, instruction traces, register values, and framebuffer
   dumps. We treat this as documentation, not as code we copy (see the legal
   doc for exactly where that line is).
2. **Emulate bottom-up, one subsystem at a time**, each with its own
   pass/fail checkpoint against the oracle, before wiring subsystems
   together:
   CPU core → memory/bus → interrupts → sound → 2D tile/sprite layer →
   3D math (TGP) → polygon rasterizer → per-game I/O.
3. **Pick the easiest game as the bring-up vehicle.** All three target games
   are full-3D (Model 1's whole reason for existing), so there's no
   "2D-only" game to defer the hard TGP/rasterizer work. Virtua Racing is
   the recommended first target: it was the launch title, the board revision
   is the earliest/best-documented in the community, and its game logic
   (driving physics + track polygons) is simpler to validate frame-by-frame
   than a fighting game's hit-detection or Star Wars Arcade's branching
   rail-shooter logic. Re-confirm this choice during Phase 0 research —
   it's a hypothesis, not a fact yet.
4. **Interpreter first, optimize later.** Write a straightforward interpreter
   for the main CPU (NEC V60). Do not attempt a recompiler/JIT until
   correctness is established and profiling shows interpretation is actually
   the bottleneck. Emulation history is littered with projects that sank
   months into a JIT before the interpreter even worked.
5. **High-level emulation (HLE) before low-level emulation (LLE) for the
   TGP.** The 3D math coprocessor is the single worst-documented chip on this
   board. Rather than blocking the whole project on recovering/emulating its
   actual microcode, first implement *what it produces* (matrix transforms,
   perspective projection, clipping, polygon list generation) as a
   behavioral model validated against the oracle's output. Revisit true LLE
   only as a stretch goal.
6. **Automate the comparison, don't eyeball it.** Every checkpoint below
   should have a script that diffs our emulator's state (registers, memory,
   framebuffer) against MAME's, rather than relying on "it looks right."
   See the testing doc.

## Sequencing philosophy

Depth-first on one game, not breadth-first on three. Get Virtua Racing
completely playable before starting Virtua Fighter or Star Wars Arcade bring-up.
Once one game runs, the second and third games will validate (and likely
break) assumptions baked in from the first — expect real rework, and budget
time for it rather than treating games 2 and 3 as "mostly free" once game 1
works.

## Team/skill strategy

This project draws on several distinct skill sets — plan for needing (or
learning) all of them:

- CPU emulation (instruction decoding, addressing modes, cycle timing)
- Low-level reverse engineering (disassembly reading, trace comparison)
- Real-time graphics (software rasterization, framebuffer/scanline timing)
- Digital audio synthesis (FM synthesis, PCM sample playback)
- Applied linear algebra / fixed-point math (for the TGP's 3D transforms)
- Systems/application programming in C++ for the frontend and build tooling

## Definition of "done" for v1

Each of the three games boots from a clean ROM set to attract mode, is
fully playable start-to-finish with mapped controls (including analog
wheel/stick), has working music and sound effects, and runs at full speed on
a mid-range 2020s desktop/laptop CPU.

# 05 — Roadmap

Phased, checkpointed plan. Each phase has an explicit exit criterion — don't
move on until it's met, even if that means the timeline slips. Durations are
rough part-time-hobbyist estimates, not commitments; recalibrate after
Phase 0.

## Phase 0 — Research & tooling (est. 3–6 weeks)

- Gather every available primary source: service manual scans, schematic
  fragments, prior community reverse-engineering write-ups, MAME's Model 1
  driver source (as documentation — see legal doc).
- Build MAME locally and get it running the target ROMs as the reference
  oracle. Learn its debugger well enough to pull instruction traces, memory
  dumps, and framebuffer captures on demand.
- Nail down, with actual evidence rather than memory/assumption: exact CPU(s)
  and clock speeds, TGP identity and current HLE-vs-LLE status in the
  reference implementation, video chip behavior, sound chip identities,
  per-game memory maps and RAM sizes, I/O board protocols per game.
- Re-validate the "Virtua Racing first" bring-up order decision (strategy
  doc) against what you actually find.
- Scaffold the repo: directory layout, CMake build, CI (build + unit tests),
  license/NOTICE files, `.gitignore` for `roms/`.
- Stand up the trace-comparison harness skeleton (testing doc) even before
  there's anything to compare — you'll use it from Phase 1 onward.

**Exit criterion:** a written hardware-notes document set (in
`docs/hardware-notes/`) covering CPU/TGP/video/sound/IO facts with sources,
plus a working local MAME oracle and empty-but-working project scaffold.

## Phase 1 — Main CPU core (NEC V60) (est. 4–8 weeks)

- Implement the V60 interpreter: full instruction set, addressing modes,
  flags, interrupts, cycle counts.
- Build the debugger UI (disassembly view, register/memory inspector,
  breakpoints, single-step) — you will live in this tool for the rest of the
  project.
- Validate purely as a CPU, independent of the rest of the board, using
  instruction-level unit tests and any available V60 test vectors.

**Exit criterion:** instruction-trace diff against the MAME oracle stays
clean for at least the first several thousand instructions of Virtua
Racing's boot ROM.

## Phase 2 — Bus, memory map, ROM loading (est. 2–4 weeks)

- Implement Virtua Racing's memory map per its game descriptor: ROM loading
  + checksum validation, RAM regions, basic interrupt controller stub.
- Get the main CPU executing real boot code headless (no video/audio yet).

**Exit criterion:** CPU reaches the same "steady state" point in boot code
(e.g. start of the self-test loop, or wherever it legitimately waits for
video/IO that doesn't exist yet) as the oracle does, verified by trace diff.

## Phase 3 — Minimal video: tile/sprite layer (est. 4–6 weeks)

- Implement the 2D tilemap + sprite compositor (deferred: the 3D quad
  rasterizer, which needs the TGP first).
- Get *something* on screen — ideally the self-test/diagnostic screen or
  attract-mode HUD elements, whichever relies least on 3D content.

**Exit criterion:** framebuffer pixel-diff against an oracle screenshot
matches for a frame that's dominated by 2D content.

## Phase 4 — Sound subsystem (est. 3–5 weeks)

- Z80 interpreter, FM synthesis chip, MultiPCM sample playback, wired to the
  shared scheduler.

**Exit criterion:** audio output is recognizably correct (music/SFX play at
the right pitch/tempo) for a captured sequence, spot-checked by ear and, where
feasible, by comparing generated PCM buffers against the oracle's.

## Phase 5 — TGP behavioral model (HLE) (est. 6–12 weeks — highest-risk phase)

- Implement the 3D math coprocessor as a behavioral model per the
  architecture doc: matrix transform, projection, clipping, polygon list
  generation, matching the command protocol the main CPU actually uses.
- This is where Phase 0's research pays off or doesn't — expect this phase's
  estimate to be the least reliable one in this document.

**Exit criterion:** for a given fixed input scene (captured from the oracle),
our TGP model produces the same transformed/projected polygon list (within a
defined numerical tolerance) as the oracle.

## Phase 6 — Quad rasterizer & full video (est. 4–8 weeks)

- Implement the shaded-quad rasterizer consuming Phase 5's polygon lists,
  matching original draw-order/sorting behavior.
- Integrate with the Phase 3 tile/sprite layer for the full composited frame.

**Exit criterion:** framebuffer pixel-diff against oracle screenshots matches
(within tolerance) for representative in-game frames, not just menus.

## Phase 7 — Input/IO for Virtua Racing (est. 2–3 weeks)

- Analog wheel + pedal I/O board emulation, frontend input mapping/
  calibration UI.

**Exit criterion:** Virtua Racing is fully playable start-to-finish with
correctly mapped analog controls.

**→ This is the "Definition of done" checkpoint for game 1 (see strategy
doc). Do not start Phase 8 until this is genuinely solid — the second and
third games will stress-test every assumption made so far.**

## Phase 8 — Virtua Fighter bring-up (est. 6–10 weeks)

- New per-game descriptor, digital joystick + button I/O board.
- Expect and budget for rework in TGP/rasterizer/bus assumptions that turn
  out to have been Virtua-Racing-specific.

**Exit criterion:** same bar as Phase 7's exit criterion, for Virtua Fighter.

## Phase 9 — Star Wars Arcade bring-up (est. 6–10 weeks)

- New per-game descriptor, analog flight-stick I/O board, any unique sound/
  speech requirements this title has that the others didn't exercise.

**Exit criterion:** same bar, for Star Wars Arcade.

## Phase 10 — Polish & performance pass (est. ongoing)

- Profile and optimize (recompiler for the main CPU only if interpretation
  is proven to be the bottleneck).
- Save states, frontend UX polish, control remapping UI, config persistence.
- Broaden automated regression coverage (testing doc) so future changes
  can't silently regress any of the three games.

## Stretch goals (post-v1, no estimate)

- TGP low-level emulation (real microcode) if firmware becomes available.
- Additional Model 1 board variants/regions.
- Netplay, rewind/TAS tooling, deluxe-cabinet extras (force feedback, motion).

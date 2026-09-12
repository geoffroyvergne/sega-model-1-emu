# 07 — Testing & Validation

Without access to real Model 1 hardware, correctness claims are only as good
as your comparison methodology. "It looks right" is not a test. This doc
defines what is.

## The oracle

MAME's existing Model 1 driver, built locally and run against your own
legally-dumped ROMs, is the reference oracle for the whole project (see
strategy and legal docs for exactly how it may/may not be used). Every
checkpoint in the roadmap is defined relative to this oracle.

## Layers of testing

### 1. Unit tests (`tests/unit/`)
- CPU cores (V60, Z80): per-instruction tests covering opcode semantics,
  addressing modes, flag behavior, and cycle counts. Use any available
  public V60/Z80 test vectors; write your own targeted tests for
  instructions/modes that matter most for these three games' actual code.
- Chip modules in isolation (sound chips, I/O boards): feed known inputs,
  assert known outputs, independent of the rest of the board.

### 2. Trace comparison (`tests/trace-compare/`)
- A harness that runs both our emulator and the MAME oracle from the same
  ROM/state, in lockstep, and diffs: CPU registers, memory contents, and
  instruction stream, after each instruction (or each N instructions once
  early divergence risk is low).
- This is the primary tool for Phases 1–2 (CPU/bus bring-up) — first
  divergence point tells you exactly where your instruction decode or memory
  map is wrong.
- Extend the same idea to TGP inputs/outputs in Phase 5: capture the oracle's
  TGP command stream and expected results, replay against your model, diff
  with a defined numerical tolerance (floating/fixed-point results won't
  always be bit-identical, and that's fine as long as it's within tolerance
  and doesn't visibly drift over time).

### 3. Framebuffer / audio comparison
- Pixel-diff our rendered frames against oracle screenshots captured from
  the same input sequence (Phases 3 and 6). Define an acceptable tolerance
  (some anti-aliasing/rounding differences are expected) and flag anything
  beyond it.
- Compare generated PCM audio buffers against the oracle's for a captured
  sequence (Phase 4), plus manual by-ear spot checks — audio bugs are often
  easier to *hear* than to catch numerically.

### 4. Determinism & save-state round-trip tests
- Snapshot the full board state, restore it, and verify bit-identical
  continued execution vs. not restoring at all. Catches serialization bugs
  and hidden non-deterministic state (e.g. an uninitialized field, real
  wall-clock time leaking into emulated timing) early.

### 5. Regression suite (ongoing, from Phase 7 onward)
- Once each game reaches its "fully playable" checkpoint, capture a
  recorded input sequence (a played-through level/round) and its resulting
  framebuffer/audio trace as a fixture.
- Run these fixtures in CI on every change to all three games, not just the
  one you're currently touching — this is what prevents Virtua Fighter
  bring-up work from silently breaking Virtua Racing.

## What "passing" means at each roadmap phase

Each phase in the roadmap doc has its own exit criterion phrased in terms of
one of these comparison layers — treat a phase as blocked, not "mostly done,"
until its specific criterion is met with an automated (not manual/eyeballed)
check backing it up.

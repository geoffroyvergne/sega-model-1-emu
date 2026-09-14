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

**Status:** nearly done. Research: CPU/TGP-Coprocessor/Geometrizer-rasterizer/
sound/IO identities, the main memory map, the TGP interface protocol, and a
full display-list opcode table are sourced and written up in
`docs/hardware-notes/`. This forced several real corrections along the way
(sound CPU is a 68000 not a Z80; the 3D pipeline splits into an LLE
"Coprocessor" role and an HLE "Geometrizer/rasterizer" role, not one thing;
Virtua Fighter and Star Wars Arcade are known-imperfect even in the
oracle — see `docs/hardware-notes/06-mame-oracle-status.md`). A Model
1-scoped MAME oracle binary has been built locally and confirmed working
(`docs/hardware-notes/00-sources.md`). Still open: the trace-comparison
harness skeleton itself, the `copro_data` ROM's contents, and the exact
`push_object` polygon-strip stride.

## Phase 1 — Main CPU core (NEC V60) (est. 4–8 weeks)

- Implement the V60 interpreter: full instruction set, addressing modes,
  flags, interrupts, cycle counts.
- Build the debugger UI (disassembly view, register/memory inspector,
  breakpoints, single-step) — you will live in this tool for the rest of the
  project.
- Validate purely as a CPU, independent of the rest of the board, using
  instruction-level unit tests and any available V60 test vectors.

**Status:** started. `src/cpu/v60/` has a register file, flags (Carry/
Overflow/Sign/Zero — confirmed that's the *complete* flag set the reference
implementation tracks, no parity despite naming, see
`docs/hardware-notes/07-v60-architecture.md`), a `Bus` interface, and a
correct, unit-tested instruction/addressing-mode slice: HALT, NOP,
MOV(B/H/W), CMP(B/H/W), ADD(B/H/W), SUB(B/H/W), AND/OR/XOR/NOT(B/H/W),
INC/DEC(B/H/W), SHL(B/H/W), SHA(B/H/W), MUL/MULU/DIV/DIVU(B/H/W),
ROT/ROTC(B/H/W), all 15 conditional branches (BV/BNV/BL/BNL/BE/BNE/BNH/BH/
BN/BP/BR/BLT/BGE/BLE/BGT, each in 8-bit and 16-bit displacement form),
JMP/JSR/RSR/RET, CALL, PUSH/POP, and PUSHM/POPM — over register-direct,
register-indirect, autoincrement, autodecrement, **8/16/32-bit
displacement**, and **immediate** (both "quick" 0-15 literals and
full-width literals) addressing. **Both of the V60's call/return
conventions are implemented, the CPU can load a constant, do bitwise logic
and arithmetic, both logical and arithmetic bit shifts, rotate (plain and
through carry), multiply and divide, execute a loop, an if-statement, and
save/restore either individual registers or a whole bitmask of them
(including flags) across a call, and every general operand can now reach a
full 32-bit offset from a base register, not just ±127 bytes** — a
genuinely capable general-purpose instruction set at this point, not just
straight-line toy arithmetic. 102 unit tests pass (doctest, vendored in
`third_party/`).

Adding ADD/SUB needed zero new addressing-mode code — they're
read-modify-write on operand 2, and the `Operand` abstraction built for
MOV/CMP already resolves op2 without eagerly reading or writing it, which
turned out to be exactly what read-modify-write needs too. Branches
surfaced a real, easy-to-get-backwards detail instead: the displacement is
relative to the branch opcode's own address, not the following
instruction. JMP/JSR/RSR/RET surfaced that the V60 actually has **two
unrelated call/return conventions** (simple JSR/RSR, and a VAX-style
CALL/RET that links the AP register into a stack frame) — only the simpler
one is fully implemented; CALL itself is deferred since it doesn't fit the
"operand 1 is always read" assumption `decode_format12` currently makes.
All confirmed in `docs/hardware-notes/07-v60-architecture.md`.

**This is no longer validated only by reading source and writing our own
tests against that reading** — `tools/oracle-harness/` now runs real V60
machine-code snippets through an actual MAME `v60_device` (no game ROM
needed: just a minimal standalone driver) and checks the results against
the same expectations as our unit tests. Current result: **37/37 test
programs match the reference core exactly**, across MOV, CMP, ADD/SUB
(including wraparound), AND/OR/XOR/NOT, INC/DEC, SHL, SHA, MUL/MULU/
DIV/DIVU, ROT/ROTC, PUSHM/POPM, both branch outcomes, JSR, CALL/RET,
PUSH/POP, displacement-16, and both immediate addressing modes. Building this
also surfaced two more real, confirmed hardware facts (this configuration's
24-bit address masking, and that the PC register displays a raw/unmasked
value even though bus accesses are masked) — see the hardware note.
Immediate addressing separately surfaced a real per-instruction limitation
caught by our *own* test suite: JMP/JSR hardcode a byte-sized operand
decode regardless of addressing mode, so a literal jump target through
this path can only be 0-255 — confirmed correct (not a bug) by checking
against the reference's `opJMP`/`opJSR`; PUSH/POP hardcode a Long-sized
decode the same way, and **SHL's shift-count operand hardcodes Byte size
even in the 16/32-bit forms** — a bug in an early draft, caught by
re-reading the reference's exact `F12DecodeOperands` call *before* writing
tests, not after a failure. AND/OR/XOR/NOT surfaced a flag-handling gotcha
instead: unlike ADD/SUB/CMP, they leave the carry flag completely untouched
rather than setting it — and INC/DEC swing back the other way, using the
exact same full-carry ADD/SUB flag macros despite being single-operand. A
test written for INC/DEC's register-indirect case initially repeated the
modm/index mistake from earlier in this phase (in the test this time, not
the implementation) — caught immediately by its own assertion. **SHA
produced this phase's most genuinely surprising finding**: its left-shift
overflow flag is mathematically incapable of firing for a 1-bit shift
(the detection formula degenerates to comparing the sign bit against
itself), so a textbook sign-changing 1-bit shift reports no overflow at
all — derived by hand from the flag formula, then confirmed against the
real oracle before being trusted, not just asserted from our own reading.
CALL revealed a real design consequence rather than just a decode-shape
mismatch: since its operands (like JMP/JSR's) can never legitimately be a
bare register, the "one general, one short" decode shape everything else
uses so far would make CALL permanently unusable — some real encoding
needed the "both operands general" case (instflags bit 7), so that was
added too, scoped only to CALL's dedicated raw-operand decoder rather than
generally. **MUL surfaced a similar copy-paste-shaped quirk**: its
overflow check is the literal same "are upper bits set" test MULU
(unsigned) uses, applied without re-checking sign extension — so MUL's
overflow flag fires for almost any negative result, even ones that fit the
destination perfectly (`-5` in a byte), confirmed both in unit tests and
against the real oracle. DIV/DIVU separately confirmed two "doesn't trap"
behaviors: division by zero silently leaves the destination unchanged, and
signed DIV specifically detects `INT_MIN / -1` (the one unrepresentable
signed division) and skips computing it, flagging overflow instead.
**ROT/ROTC caught a stale comment in the reference source itself**: its
own top-of-file comment lists ROTC as unimplemented, but the actual
functions are complete and marked "TRUSTED" — a reminder to verify against
the function body, not a nearby comment, even in a well-regarded
reference. ROT and ROTC also turned out to be genuinely different
operations, not variations on a theme: ROT rotates only the operand's own
bits, ROTC rotates through a `(bits+1)`-wide ring that includes the carry
flag, so identical input produces different results (`0x81` rotated left
by 1 gives `0x03` under ROT but `0x02` under ROTC) — confirmed both ways
against the real oracle. **PUSHM/POPM confirmed bit 31 is repurposed for
PSW** (not SP, which has no bit of its own since it's what's doing the
pushing/popping), that PUSHM pushes PSW first then registers in
descending order while POPM pops registers ascending then PSW last (the
mirror image), and a real asymmetry where POPM only reads back 16 of the
32 bits PUSHM wrote for PSW. **Displacement-16/32 slotted in with no new
concepts needed** — same shape as the already-implemented displacement-8,
just a wider field — closing the addressing-range gap that mattered most
(8-bit displacement's ±127 byte range is too small for many real
struct/stack-frame accesses); two of its own unit tests briefly collided
with the loaded program bytes and the test bus's size limit, caught
immediately by the test framework itself. This was worth doing now, while the core is still small: the addressing-mode
bugs already found this phase were "internally consistent with our
own tests, but wrong" — exactly the failure mode oracle comparison is
positioned to catch that more unit tests against our own understanding
cannot.

Two real bugs were caught and fixed before they could propagate, both
documented in the hardware note as cautionary examples:
1. A subtraction-overflow flag formula with its XOR terms transposed.
2. A more fundamental one: general-operand addressing modes are selected by
   a modifier byte's top 3 bits *and* a separate `modm` bit together — the
   same index means different things in each of two tables (index 3 is
   register-*indirect* in one, register-*direct* in the other). The first
   implementation checked only the modifier byte and was internally
   consistent with its own (wrong) tests — re-reading MAME's `am1.hxx`
   table definition directly is what caught it, not more unit tests against
   the same wrong understanding. Worth remembering as this core grows.

Also confirmed: **V60 cycle timing is unknown even in the reference
implementation** (its own source: "Actual cycles / instruction is unknown",
flat 8-cycle average) — this changes what this phase's exit criterion
below should mean by "cycle counts": matching that same approximation, not
some undiscovered ground truth.

Remaining for this phase: the rest of the addressing modes (16/32-bit
displacement, displacement-indirect, double-displacement, the "both
operands general" case, bit-string/bit-field modes, immediate literals),
ADD/SUB and the rest of the instruction set, interrupts, and the debugger
UI.

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

**Status:** started. `src/bus/model1_bus.{h,cpp}` implements the full main
CPU address map fetched directly from MAME's `model1_mem()` (a more
precise source than the summarized table Phase 0 originally produced —
see `docs/hardware-notes/08-bus-and-rom-loading.md`), with real, confirmed
logic (not stubs) for ROM regions, ROMO bank switching, and the
main-CPU-facing half of the TGP RAM interface — everything whose behavior
was already fully understood — and clearly-marked stubs for whatever
belongs to a later phase (video, sound, the TGP FIFO handshake, real
interrupts). `src/board/rom_set.{h,cpp}` implements generic ROM loading
and CRC32/size validation, and `src/board/games/virtua_racing.{h,cpp}`
records Virtua Racing's real ROM file layout (names/sizes/checksums, as
fingerprints only) for the regions this phase covers.

**Blocked on real ROMs for the actual exit criterion** (executing real
boot code) — this project doesn't have, and won't seek, any game ROM (see
the legal doc). What's built so far is fully exercised by 19 new unit
tests against synthetic data instead, and is ready to load a real dump the
moment one is available.

Building the TGP RAM interface surfaced a real bug, caught by a unit test
rather than by inspection: that register is genuinely 16-bit-wide in
hardware, and this bus's usual approach (build 16-bit access generically
from two 8-bit accesses) silently double-triggered its side effects (the
auto-increment) when applied to it, corrupting the second byte of a write
and advancing the address twice instead of once. Fixed by making
`read16`/`write16` the primary handlers for that specific register instead
of the other way around — worth remembering as more real-behavior
registers get added to this bus: check whether a register's *actual*
hardware width matters before assuming byte-level decomposition is free.

## Phase 3 — Minimal video: tile/sprite layer (est. 4–6 weeks)

- Implement the 2D tilemap + sprite compositor (deferred: the 3D quad
  rasterizer, which needs the TGP first).
- Get *something* on screen — ideally the self-test/diagnostic screen or
  attract-mode HUD elements, whichever relies least on 3D content.

**Exit criterion:** framebuffer pixel-diff against an oracle screenshot
matches for a frame that's dominated by 2D content.

## Phase 4 — Sound subsystem (est. 3–5 weeks)

- 68000 interpreter (the music/FX board's real CPU — see
  `docs/hardware-notes/03-sound.md`), FM synthesis chip (YM3438), and the
  Sega 315-5560 sample-playback chip, wired to the shared scheduler.
- A separate Z80 core (I/O board — needed by all three games regardless of
  sound) is needed too, but is tracked under Phase 7/8/9's I/O work, not
  here, since it's not part of music/FX synthesis.

**Exit criterion:** audio output is recognizably correct (music/SFX play at
the right pitch/tempo) for a captured sequence, spot-checked by ear and, where
feasible, by comparing generated PCM buffers against the oracle's.

## Phase 5 — TGP Coprocessor: low-level emulation (est. 6–12 weeks — highest-risk phase)

*Scope note: this phase covers Role A only (the game-logic "Coprocessor").
Role B (the Geometrizer/rasterizer pipeline that actually produces 3D
graphics) is Phase 6, and is HLE, not LLE — see
`docs/hardware-notes/02-tgp-coprocessor.md` for why these are different
chips emulated two different ways even in MAME.*

- Implement an original MB86233 interpreter (`src/cpu/mb86233/`): real
  instruction decode/register file/ALU, plus the memory-mapped math helper
  tables (sin/cos/inv/isqrt/atan), executing the same per-game dumped
  microcode ROMs (315-5573/5711/5724) the real board runs.
- Wire it to the main CPU via the real interface: the address/data port and
  command FIFOs at `0xd00000-0xd80003`, including the real halt/stall
  handshake (a full/empty FIFO actually halts the other CPU — this is not
  optional polish, it's load-bearing for correct timing).
- Expect some MB86233 opcodes to need trial-and-error against captured
  traces — even the community's own decoding of this chip is admittedly
  incomplete. This is where Phase 0's research pays off or doesn't; expect
  this phase's estimate to be the least reliable one in this document.

**Exit criterion:** instruction-level trace diff against MAME's
`mb86233.cpp` stays clean for representative Coprocessor programs (not just
final output comparison — this role's job is game logic, not just numbers
that "look visually right").

## Phase 6 — Geometrizer/rasterizer pipeline & full video (est. 6–10 weeks)

*Scope note: this is Role B — the part that actually produces the 3D image.
See `docs/hardware-notes/05-video.md`.*

- Implement the pipeline as HLE, following MAME's own proven approach:
  object-space transform → perspective projection → per-edge frustum
  clipping → specular lighting → quad sort (draw-order, not Z-buffer) →
  scanline quad rasterization (flat/Gouraud + investigate the "moiré" fill
  variant's actual purpose).
- Before writing code, transcribe the display-list opcode table
  (`tgp_render`/`tgp_scan`'s `0x41`/`0xa`/`0xb`/`0xc`/`0xf`/etc. cases) into
  `docs/hardware-notes/` — this was flagged as an open item in Phase 0 and
  should be closed here if not already.
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

## Phase 8 — Virtua Fighter bring-up (est. 6–10 weeks, real risk of running longer)

- New per-game descriptor, digital joystick + button I/O board.
- Expect and budget for rework in TGP/rasterizer/bus assumptions that turn
  out to have been Virtua-Racing-specific.
- **Known extra risk (see `docs/hardware-notes/06-mame-oracle-status.md`):**
  MAME's own driver flags Virtua Fighter `MACHINE_NOT_WORKING`, with
  community reports pointing at imperfect TGP RAM port timing breaking
  collision detection when both characters attack simultaneously. The
  oracle cannot fully validate this game — treat "matches MAME" as
  necessary but not sufficient here, and expect to need an independent
  reference (video captures of real hardware, etc.) for final validation.

**Exit criterion:** same bar as Phase 7's exit criterion, for Virtua Fighter,
*plus* explicit sign-off that known oracle-divergent areas (collision
detection timing) have been checked against something other than MAME.

## Phase 9 — Star Wars Arcade bring-up (est. 6–10 weeks)

- New per-game descriptor, analog flight-stick I/O board.
- **New hardware, not just a new I/O board:** this game's ROM set includes a
  Digital Sound Board — a second Z80 plus an MPEG decoder — that Virtua
  Racing and Virtua Fighter don't have (`docs/hardware-notes/03-sound.md`).
  Budget real time for this, not just control remapping.
- MAME flags this game `MACHINE_IMPERFECT_GRAPHICS | MACHINE_IMPERFECT_CONTROLS`
  (ship models periodically disappearing, analog control issues) — same
  oracle-can't-fully-validate caveat as Phase 8.

**Exit criterion:** same bar as Phase 7, for Star Wars Arcade, plus the DSB/
MPEG audio path working and the same "checked beyond the oracle" sign-off
for the known imperfect-graphics/controls areas.

## Phase 10 — Polish & performance pass (est. ongoing)

- Profile and optimize (recompiler for the main CPU only if interpretation
  is proven to be the bottleneck).
- Save states, frontend UX polish, control remapping UI, config persistence.
- Broaden automated regression coverage (testing doc) so future changes
  can't silently regress any of the three games.

## Stretch goals (post-v1, no estimate)

- Additional Model 1 board variants/regions (Wing War, NetMerc, Virtua Cop —
  note these use the *other* "advanced" I/O board, `model1io2.cpp`, not the
  one our three target games share).
- Netplay, rewind/TAS tooling, deluxe-cabinet extras (force feedback, motion).

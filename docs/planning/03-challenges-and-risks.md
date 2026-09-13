# 03 — Challenges & Risks

Read this before committing serious time. Ordered roughly by how much they
threaten the project, not by implementation order.

Several items below were revised after actual Phase 0 research replaced
earlier guesses — see `docs/hardware-notes/` for the sourced evidence behind
each correction.

## 1. The 3D pipeline splits into two roles with two different risk profiles — **still highest risk, now precisely scoped**

*This section has been revised twice. First guess: the whole 3D pipeline was
one undocumented black box needing HLE. Second guess, after finding MAME's
real MB86233 CPU core: assume the whole thing should be LLE. Both were
oversimplified — reading `model1_m.cpp` and `model1_v.cpp` directly
(`docs/hardware-notes/02-tgp-coprocessor.md` and `05-video.md`) shows the
real hardware has two separate roles, genuinely emulated two different ways
even in current MAME. This version reflects that.*

**Role A — "Coprocessor" (game logic math, e.g. physics/collision):** a real
Fujitsu MB86233 DSP running per-game dumped microcode (315-5573/5711/5724),
decapped and extracted by the CAPS0ff effort (2017). MAME emulates this with
genuine low-level emulation (`cpu/mb86233/mb86233.cpp`, real instruction
decode). **Plan: LLE from the start**, per `02-tgp-coprocessor.md`.

**Role B — "Geometrizer" (vertex transform/projection feeding the
rasterizer):** separate chips (315-5571/5572) whose ROMs are dumped but
**not wired to any active device in MAME** — their function is instead a
hand-written C++ behavioral simulation in `model1_v.cpp` (matrix transform →
projection → frustum clipping → lighting → sort → scanline quad fill,
driven by a display-list opcode dispatch). **Plan: HLE, following MAME's own
proven approach**, per `05-video.md`. True LLE of the real 315-5571/5572
microcode is a stretch goal beyond even what MAME does today, not v1 scope.

Remaining risk, now precisely scoped instead of a vague "TGP is scary":

- Role A: the MB86233's own instruction-set documentation is admitted
  incomplete even by the people who reverse-engineered it — some opcodes may
  need our own trial-and-error against captured traces. MAME's core also has
  documented gaps (interrupts, fixed-point mode, stack pointer) that are
  probably fine for our target games but should be checked, not assumed.
- Role A: the shared-RAM auto-increment protocol and FIFO halt semantics
  (`02-tgp-coprocessor.md`) are the strongest suspect for Virtua Fighter's
  known collision-detection bugs — get this exactly right, and treat it as
  a real research task for VF specifically, not just "implement the address
  map."
- Role B: the display-list opcode table (`tgp_render`/`tgp_scan`'s
  `0x41`/`0xa`/`0xb`/`0xc`/`0xf`/etc. cases) isn't transcribed yet — needed
  before Phase 6 can be scoped with confidence.

- **Impact if unresolved:** wrong-looking 3D geometry, incorrect object
  positioning, or subtly wrong game physics/timing in all three target games,
  since all three are 3D-only — there's no fallback 2D mode to ship instead.
- **Mitigation:** LLE for Role A (Coprocessor), HLE-following-MAME for Role B
  (Geometrizer/rasterizer) — don't force both roles into the same emulation
  strategy. Validate Role A instruction-by-instruction against the MAME
  oracle; validate Role B by framebuffer/output comparison. Treat Virtua
  Fighter's TGP-RAM-port timing specifically as a research task that may
  require going beyond what the oracle can verify.

## 2. Multi-processor timing synchronization

The board has at minimum the main V60 CPU, the TGP (MB86233), a 68000-based
sound/music CPU, and a Z80-based I/O board CPU (plus, for Star Wars Arcade
only, a second Z80 on the digital sound board) all running concurrently,
interacting through shared/dual-port memory and interrupts.
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

## 10. The reference oracle itself doesn't fully work for two of the three target games

This wasn't assumed in the original plan — it's a direct finding from
reading MAME's own status flags (`docs/hardware-notes/06-mame-oracle-status.md`):
Virtua Racing is flagged fully working, but **Virtua Fighter is flagged
`MACHINE_NOT_WORKING`** and **Star Wars Arcade is flagged imperfect
graphics and controls**. The testing strategy (testing doc) leans heavily
on diffing against MAME as ground truth — that's solid for Virtua Racing,
but for the other two games, matching the oracle exactly is not the same as
being correct, and some of what we need to get right (e.g. Virtua Fighter's
collision detection) is exactly what the oracle itself hasn't solved.

- **Impact:** Phase 8 and Phase 9 exit criteria (roadmap doc) may need to
  shift from "matches oracle" to "matches real hardware behavior sourced
  another way" for specific subsystems — expect original research, not just
  porting/debugging work, in those phases.
- **Mitigation:** don't discover this mid-Phase-8. Budget for it now, and
  keep an eye out for arcade-preservation video captures or other
  independent references for Virtua Fighter/Star Wars Arcade behavior that
  can serve as a secondary check when the oracle is known-wrong.

## 11. Realistic timeline

This is a part-time hobby project analog to "write a new SNES-class emulator
from scratch, but for a system with 1/100th the documentation." Expect the
research + bring-up of the first game (Virtua Racing) to plausibly take
several months of part-time effort before it's playable at all, and the full
three-game v1 scope to run 12–24 months part-time. Treat any tighter
estimate with suspicion.

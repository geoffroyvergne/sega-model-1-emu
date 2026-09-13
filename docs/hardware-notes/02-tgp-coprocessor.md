# 02 — TGP Coprocessor [verified against MAME source — revised]

*Second revision. The first version of this file (and the original planning
docs) assumed the whole 3D pipeline was one mystery chip. Reading
`model1_m.cpp` and `model1_v.cpp` directly shows the real picture is two
distinct roles, emulated two different ways even in MAME today. This file
now covers the "Coprocessor" role only; the "Geometrizer" role is covered in
`05-video.md`, since in the real emulator it lives entirely in the video
code.*

## Two physically separate roles, both built on the same chip family

The board has (at least) two Fujitsu MB86233 DSPs with different jobs:

1. **"Coprocessor" / TGP proper** — per-game microcode (315-5573 for
   Virtua Racing/Virtua Formula, 315-5711 for Wing War/Star Wars Arcade/
   NetMerc, 315-5724 for Virtua Fighter). This is the chip MAME actually
   instantiates as a device: `MB86233(config, m_tgp_copro, 40_MHz_XTAL)`,
   running real dumped microcode loaded from the `"tgp_copro"` ROM region —
   **confirmed real low-level emulation** for this role.
2. **"Geometrizer"** — fixed-function-looking chips (315-5571, 315-5572),
   whose ROMs (`"315_5571"`/`"315_5572"` regions, 0x2000 bytes each) **are
   dumped and present in every ROM set but are not loaded into any active
   emulated device** (confirmed by grepping the whole driver for
   `memregion("315_5571"...)`/`"315_5572"` — no such reference exists;
   nothing wires these regions to a CPU device). Their real-hardware job
   (transforming/projecting object-space vertices into the screen-space
   "display list" the rasterizer consumes) is instead reproduced by a
   **hand-written C++ simulation** in `model1_v.cpp` — real matrix
   transforms, projection, and clipping math, but not a replay of the
   315-5571/5572 chips' actual microcode. See `05-video.md` for detail.

**So: the game-logic-facing "Coprocessor" is LLE; the rasterizer-facing
"Geometrizer" is HLE, even in current MAME.** My previous framing (both
"undocumented HLE," then over-corrected to "both LLE") was wrong in both
directions — the truth is split down the middle, and it splits exactly along
the same seam our architecture doc already proposed for `src/tgp/` vs.
`src/video/`. That's a useful coincidence, not a coincidence we engineered.

## Coprocessor role: confirmed interface details (from `model1_m.cpp`)

- **Shared RAM:** 0x2000 (8192) × 32-bit words. The main V60 accesses it via
  an address-pointer register (`v60_copro_ram_adr`) plus a 16-bit
  read/write data port with auto-increment when bit `0x8000` of the address
  is set. Source comment, quoted directly: *"Sega Model 1 stores vertices to
  test in RAM at 4 bytes each (X, Y, Z, radius)... Page 4 of the coprocessor
  RAM (0x40000) is used for this purpose, and the low bits of the address
  are used to select which of the 4 values is being read/written."* — the
  TGP side auto-increments by 4 instead of 1 when that page bit is set,
  matching this X/Y/Z/radius layout.
- **FIFOs:** two 16-deep, 32-bit-wide FIFOs (`copro_fifo_in`/`_out`) with
  real hardware handshaking — pushing into a full FIFO or popping an empty
  one actually **halts** the other CPU (`INPUT_LINE_HALT`) until space/data
  is available. This is a real stall/handshake mechanism, not just a
  buffered queue — our scheduler (architecture doc) needs to model this as
  an actual CPU-halting condition, not a fire-and-forget message pass.
- **Program space:** `0x000-0x7ff` (2048 32-bit words), loaded from ROM —
  exactly matches the 0x2000-byte size of the per-game `tgp_copro` ROM.
- **Data/RAM space:** two independent banks, `0x0000-0x00ff` (256 dwords)
  and `0x0200-0x03ff` (512 dwords) — plus a data-in port at `0x0100` (reads
  from `copro_fifo_in`) and a data-out port at `0x0400` (writes to
  `copro_fifo_out`).
- **Hardware math helper tables**, exposed as memory-mapped I/O the
  MB86233 program reads/writes (not instructions — table lookups): sine/
  cosine, reciprocal ("inv"), inverse square root ("isqrt"), and arctangent.
  These are backed by a shared `copro_tables` ROM region (0x40000 bytes) —
  real dumped lookup-table ROM content, not computed. **Our MB86233 core's
  I/O-space handler needs to implement these table reads**, not just
  instruction execution — this is a concrete, scoped piece of work, not an
  open unknown.
- **Known hardware quirk, documented by MAME's own author:** the arctangent
  table read function (`copro_atan_r`) contains an explicit bit-level
  correction with the comment *"Correct for table bug, it seems that the
  hardware does something equivalent somehow, or maybe there are boards
  with updated opr roms."* — i.e., even the real hardware's atan table has
  an oddity that had to be reverse-engineered behaviorally. Copy this
  correction's *behavior* (not the code) once we implement our own version,
  and expect similar small quirks elsewhere.
- **`copro_data` region** (0x200000 bytes, "TGP data ROMs") — a separate
  per-game data ROM the coprocessor reads via `copro_data_r`, indexed by a
  base register plus offset. Likely 3D model/track/course data. Not yet
  investigated in depth.

## What this means for the project

- `src/cpu/mb86233/` (per the architecture doc) is correctly scoped to the
  **Coprocessor** role only — write a real instruction-decoding core against
  the real per-game microcode ROMs, exactly as planned.
- The memory-mapped math tables and the FIFO halt semantics above are
  concrete, closed research items — they can go straight into the Phase 5
  implementation plan rather than staying open questions.
- The **Geometrizer** role is a separate, HLE-based piece of work — see
  `05-video.md` and the updated architecture/roadmap docs. Don't conflate
  it with this file's scope.
- Virtua Fighter's known collision-detection bug is attributed by the
  community specifically to "imperfect TGP RAM port emulation" — the exact
  shared-RAM/auto-increment mechanism described above is a strong candidate
  for where that imperfection lives. Worth targeted extra care and testing
  when Phase 8 gets there (see `06-mame-oracle-status.md`).

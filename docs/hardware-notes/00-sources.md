# 00 — Sources

Research pass 1 (Phase 0). Everything in this `hardware-notes/` folder is
either:

- **[verified]** — read directly from MAME's real source code (comments,
  device configs, address maps, ROM loading) or another primary/technical
  source, quoted or paraphrased faithfully with a citation, or
- **[reported, unverified]** — came from a secondary source (wiki, forum,
  search-engine summary) and has *not* been cross-checked against source
  code or a second independent source yet. Treat these as leads, not facts,
  until verified in a later research pass.

Per the project's [legal policy](../planning/06-legal-and-assets.md), MAME's
source is used here strictly as **documentation** — to understand and cite
hardware facts — not copied into this project's code.

## Primary sources consulted so far

- **MAME source, `src/mame/sega/model1.cpp`** (main driver: memory map, CPU/
  TGP/sound device configuration, ROM sets, per-game `GAME()` status flags).
  https://github.com/mamedev/mame/blob/master/src/mame/sega/model1.cpp
- **MAME source, `src/mame/sega/model1io.cpp`** (standard I/O board used by
  Virtua Racing / Virtua Fighter / Star Wars Arcade).
  https://github.com/mamedev/mame/blob/master/src/mame/sega/model1io.cpp
- **MAME source, `src/mame/sega/model1io2.cpp`** (the *other*, "advanced" I/O
  board — used by Wing War, NetMerc, Virtua Cop. **Not** used by our three
  target games — noted here only to avoid re-fetching it by mistake later).
  https://github.com/mamedev/mame/blob/master/src/mame/sega/model1io2.cpp
- **MAME source, `src/devices/cpu/mb86233/mb86233.cpp`** (the CPU core that
  emulates the Fujitsu MB86233 TGP/geometrizer chip: header comments,
  documented limitations).
  https://github.com/mamedev/mame/blob/master/src/devices/cpu/mb86233/mb86233.cpp
- **MAME source, `src/devices/cpu/v60/*`** (the full V60 core: `v60.h`,
  `v60.cpp`, `am1.hxx`/`am3.hxx`, `op12.hxx`, `optable.hxx` — instruction
  encoding, register/flag layout, per-opcode semantics; read in detail to
  write our own `src/cpu/v60/`, see `07-v60-architecture.md`).
  https://github.com/mamedev/mame/tree/master/src/devices/cpu/v60
- **CAPS0ff blog, "Fujitsu MB86233 'TGP' DSP" (Feb 2017)** — write-up of the
  chip decapping/reverse-engineering effort that produced the real
  MB86233 microcode dumps MAME now uses.
  http://caps0ff.blogspot.com/2017/02/fujitsu-mb86233-tgp-dsp.html
- **MAME source, `src/mame/sega/model1_m.cpp`** (Coprocessor role: shared
  RAM/FIFO interface, math helper tables, full contents read directly).
  https://github.com/mamedev/mame/blob/master/src/mame/sega/model1_m.cpp
- **MAME source, `src/mame/sega/model1_v.cpp`** (Geometrizer/rasterizer
  role: transform/project/clip/light/sort/rasterize pipeline, display-list
  dispatch — read in structural detail, not line-by-line).
  https://github.com/mamedev/mame/blob/master/src/mame/sega/model1_v.cpp

## Secondary/unverified leads to follow up on

- System16.com's Model 1 hardware page (`system16.com/hardware.php?id=712`)
  returned HTTP 403 to automated fetching — need to view manually in a
  browser, it's normally a strong source for arcade board photos/part
  numbers.
- General web-search summaries mentioning a "Sega 837-7894 171-6080D VIDEO
  GPU" and "designed by Yu Suzuki" — plausible but not yet confirmed against
  a primary source; don't cite as fact yet.
- ~~Video pipeline details~~ — done: see `05-video.md` (now covers the
  Geometrizer/rasterizer pipeline in structural detail, sourced from
  `model1_v.cpp`).
- ~~TGP-to-rasterizer split~~ — done: `02-tgp-coprocessor.md` and
  `05-video.md` now document the Coprocessor (LLE) vs. Geometrizer (HLE)
  split, sourced from `model1_m.cpp`/`model1_v.cpp` directly.
- ~~Display-list opcode table~~ — done, see `05-video.md`'s full opcode
  table, transcribed directly from `tgp_render`'s switch statement.
- ~~Whether the 315_5571/315_5572 Geometrizer ROMs are really unused~~ —
  confirmed: a whole-tree `grep -rn "315_5571\|315_5572"` across all of
  `src/` in a full local MAME clone (see below) finds these region names
  used *only* in their own `ROM_REGION`/`ROM_LOAD` declarations — no device
  or handler anywhere reads them. High confidence this is genuinely unused
  in the current driver, not a gap in our earlier single-file search.
- **New open item:** the `copro_data` region (per-game "TGP data ROMs",
  0x200000 bytes) referenced from `model1_m.cpp` hasn't been investigated —
  likely 3D model/track data, not yet confirmed.
- **New open item:** exact byte layout inside `push_object`/`push_direct`
  (the object/polygon record format read from `m_poly_ram`) — needed before
  Phase 6 implementation, not just the display-list opcode dispatch around
  it.

- **MAME source, `model1_mem()`/`model1_io()` in `model1.cpp`** — the exact
  main-CPU address map, fetched directly (not summarized) for Phase 2's
  bus implementation. See `08-bus-and-rom-loading.md`.
- **MAME source, `model1.cpp`'s `bank_w`** — ROMO bank-switching register
  logic.
- **MAME source, `model1.cpp`'s `ROM_START( vr )`** — Virtua Racing's real
  ROM file layout (names/sizes/CRC32, used as fingerprints only).

## Local MAME checkout — the oracle is built and confirmed working

A shallow clone of `mamedev/mame` (`master`, depth 1) was pulled into the
session scratchpad and built scoped to just the Model 1 driver:

```
git clone --depth 1 https://github.com/mamedev/mame.git
cd mame
make SOURCES=src/mame/sega/model1.cpp -j<cores>
```

This produced a working `mame` binary (~86MB, MAME 0.289) confirmed via
`./mame -listfull vr vf swa ...` to contain all 10 drivers in this source
file, including our three targets (`vr`, `vf`, `swa`/`swaj`). The scoped
build compiles in well under the time a full ~1000-driver MAME build would
take, since it skips unrelated systems entirely.

The binary supports `-debug` with a choice of debugger front-end
(`-debugger osx|imgui|gdbstub|none`) — the `gdbstub` option is particularly
relevant for Phase 1+: it exposes MAME's debugger over the GDB remote
protocol, which is scriptable, making it a realistic backend for the
automated trace-comparison harness (testing doc) rather than requiring a
human at the interactive UI debugger for every comparison run.

This checkout and binary live only in the session scratchpad, not the
project repository — see the legal doc's "documentation only" policy on
MAME's source. Reproduce with the commands above whenever a fresh oracle
build is needed (e.g. in a new session); it is not preserved automatically.

**Still required to actually run any game:** a legally-obtained ROM dump,
placed in a `roms/<shortname>/` directory next to the binary. None is
provided by this project or by this research — see the legal doc.

## How to keep this updated

Every subsequent research session should append to "Primary sources
consulted" and move items out of "unverified leads" once checked — don't let
this file go stale while the rest of hardware-notes gets more detailed.

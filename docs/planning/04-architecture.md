# 04 — Architecture

## Design principles

- **Modular per-chip components.** Every physical chip on the board gets its
  own module with a narrow interface (clock it N cycles, read/write its bus).
  This mirrors the real hardware, keeps each piece independently testable
  against the oracle, and avoids one monolithic "the board" blob.
- **One deterministic scheduler, no free-running threads per chip.** All
  chips advance in lockstep against a shared master cycle counter. This
  sidesteps an entire class of race-condition bugs that would otherwise be
  invisible on real (synchronous) hardware but very visible in a naively
  multi-threaded emulator (see challenges doc, #2).
- **Per-game configuration, not per-game forks.** One core codebase; each
  game supplies a data-driven description (ROM layout, memory map, RAM
  sizes, I/O board type, TGP program variant) rather than three divergent
  copies of the emulator.
- **Deterministic and inspectable.** Every subsystem should support
  save/restore of its full state (for save states, but more importantly for
  the automated trace-comparison testing described in the testing doc) and
  should be debuggable via a built-in memory/register/disassembly viewer.
- **Original code, MAME as documentation only.** No verbatim copying of
  MAME's GPL source. See the legal doc for exactly what "documentation only"
  means in practice.

## High-level component map

```
                        ┌─────────────────────┐
                        │      Frontend        │
                        │ (SDL2 window, input, │
                        │  audio out, config,  │
                        │  debugger UI)        │
                        └──────────┬───────────┘
                                   │
                        ┌──────────▼───────────┐
                        │   Board / Scheduler   │
                        │ (per-game config,     │
                        │  master clock, glue)  │
                        └──┬───┬───┬───┬───┬────┘
             ┌─────────────┘   │   │   │   └─────────────┐
             │                 │   │   │                 │
      ┌──────▼─────┐   ┌───────▼─┐ │ ┌─▼──────────┐ ┌────▼─────┐
      │ Main CPU   │   │  TGP    │ │ │ Sound unit │ │ I/O board │
      │ (NEC V60   │   │ (3D math│ │ │ (Z80 +     │ │ (per-game:│
      │ interpreter)│   │ coproc, │ │ │ YM3438 +   │ │ analog    │
      └──────┬─────┘   │ HLE→LLE)│ │ │ MultiPCM)  │ │ wheel/    │
             │         └────┬────┘ │ └─────┬──────┘ │ stick/pad)│
             │              │      │       │        └────┬─────┘
             └──────┬───────┴──────┴───────┴─────────────┘
                    │
             ┌──────▼──────────┐        ┌───────────────────┐
             │  System bus /   │◄──────►│  Video subsystem   │
             │  memory map     │        │ (tile/sprite layer │
             │  (per-game)     │        │  + quad rasterizer │
             └─────────────────┘        │  + framebuffer)    │
                                         └────────────────────┘
```

## Component responsibilities

### Board / Scheduler (`src/board/`)
- Owns the master cycle counter and steps each chip a bounded number of
  cycles per iteration (e.g., one scanline's worth at a time), so video
  timing, interrupts, and CPU execution stay correctly interleaved.
- Loads a **per-game descriptor** (see below) and wires up the right memory
  map, RAM sizes, and I/O board variant.
- Owns save-state serialization: delegates to each component's
  serialize/deserialize, concatenates into one snapshot.

### Per-game descriptor (`src/board/games/*.cpp`)
A small data + glue file per game (`virtua_racing.cpp`, `virtua_fighter.cpp`,
`star_wars_arcade.cpp`) declaring:
- ROM file list + expected sizes/checksums (for the user's own dumps)
- Memory map (address ranges → RAM/ROM/MMIO regions)
- RAM sizes and any per-game quirks
- Which I/O board variant to instantiate
- Any TGP program/behavior variant needed

This is the mechanism that keeps the core generic instead of hard-coding
assumptions from whichever game is brought up first (see challenges doc, #7).

### Main CPU (`src/cpu/v60/`)
- NEC V60 interpreter: instruction decode, addressing modes, flags, cycle
  counts.
- Exposes a clean `step(cycles) -> cycles_consumed` interface plus a
  disassembler and register/state introspection for debugging and for the
  trace-comparison test harness.
- Interpreter first; a later optional recompiler lives behind the same
  interface so it's a drop-in swap, not a rewrite (see strategy doc).

### TGP Coprocessor — game-logic math (`src/tgp/`)
- This is Role A from the challenges doc: the per-game "Coprocessor"
  (315-5573/5711/5724 microcode) that games use for physics/collision/AI
  helper math via the FIFO/RAM interface — see
  `docs/hardware-notes/02-tgp-coprocessor.md`. Real dumped microcode exists
  for it, so this is **LLE from the start**: an original MB86233 interpreter
  (instruction decode, register file, ALU/float ops, plus the memory-mapped
  math helper tables — sin/cos/inv/isqrt/atan) living at `src/cpu/mb86233/`.
- `src/tgp/` itself is thin: it owns the main-CPU-facing interface — the
  address/data port and the two command FIFOs at `0xd00000-0xd80003`,
  including their real halt/stall handshake semantics (pushing a full FIFO
  or popping an empty one actually halts the other CPU) — and wires that up
  to the MB86233 core.
- If some opcodes turn out to need trial-and-error against captured traces
  (expected — even the community's own decoding is admitted incomplete),
  that work stays inside `src/cpu/mb86233/` and doesn't leak into the rest
  of the system.

### Video subsystem (`src/video/`)
- **Tile/sprite layer**: conventional tilemap + sprite compositing, used for
  HUD, backgrounds, and UI — bring this up first since it's far better
  understood than the 3D path and gives an early visual signal that the CPU
  core and memory map are basically correct.
- **Geometrizer + rasterizer pipeline** (Role B from the challenges doc):
  object-space vertex transform → perspective projection → per-edge frustum
  clipping → lighting (specular term) → quad sort (draw-order, not a
  Z-buffer — confirmed, see challenges doc #4) → scanline quad
  rasterization (flat/Gouraud shaded, plus a "moiré" fill variant whose
  exact purpose is still to be confirmed). This whole pipeline is **HLE**,
  following MAME's own proven approach (`docs/hardware-notes/05-video.md`)
  rather than real 315-5571/5572 microcode — true LLE there is a stretch
  goal, not v1 scope. Driven by a double-buffered display-list opcode
  dispatch; the exact opcode table is still to be documented (Phase 0
  follow-up, before Phase 6).
- Runs on the CPU (software implementation) to match original
  ordering/blending semantics; do not offload this to a GPU shader pipeline
  for v1 — correctness before performance.
- Produces one framebuffer per frame that the frontend blits (optionally
  GPU-upscaled/filtered at the presentation stage only, never in the logic
  that decides pixel values).

### Sound unit (`src/sound/`)
- The music/FX board's CPU is a **68000** (`TMP68000N-10`, 10 MHz) — not a
  Z80, correcting an earlier assumption in this doc (see
  `docs/hardware-notes/03-sound.md`). Lives at `src/cpu/m68000/`.
- FM synthesis (YM3438, 8 MHz) and sample playback (Sega custom `315-5560`,
  believed to be a MultiPCM variant — confirm during Phase 4) emulation.
  Prefer building on an existing permissively-licensed, well-regarded
  chip-emulation library rather than reinventing FM synthesis math from
  scratch — confirm the specific library during Phase 4 and record the
  choice + license in the legal doc.
- Runs on the same master scheduler, not a separate audio thread with its
  own notion of time, to keep music/SFX sync correct.
- **Star Wars Arcade only:** an additional Digital Sound Board (a second,
  separate Z80 driving an MPEG decoder) is present in that game's ROM set
  and not the other two — a genuine per-game hardware difference to handle
  via the per-game descriptor, not a core assumption.

### I/O boards (`src/io/`)
- The standard I/O board (used by all three target games, board part
  `837-8950-01`) has its own **Z80** CPU (4 MHz) plus an OKI M6253 ADC for
  analog inputs — lives at `src/cpu/z80/` (shared with the Star Wars Arcade
  digital sound board's Z80, which is a separate CPU instance using the same
  core). See `docs/hardware-notes/04-io-and-controls.md`.
- One control-scheme configuration per game on top of that shared board:
  analog wheel + pedals (Virtua Racing, via CN2), digital 8-way + buttons
  (Virtua Fighter, via CN1), analog stick + trigger (Star Wars Arcade, via
  CN1).
- Each translates real input events (from the frontend's input layer) into
  whatever the game's actual I/O board protocol expects (register reads,
  serial/analog port behavior) — the emulated game code should never know
  it's receiving a mapped gamepad axis instead of a real wheel.

### System bus / memory map (`src/bus/`)
- Central address decode: routes CPU/TGP/sound reads and writes to the right
  RAM, ROM, or MMIO handler based on the active game's descriptor.
- This is where the "per-game differences" (challenges doc, #7) get
  resolved, so individual chip modules stay game-agnostic.

### Frontend (`src/frontend/`)
- SDL2-based window, audio output, and input capture.
- Game selection, ROM path configuration, control mapping/calibration
  (important for analog wheel/stick devices), save-state UI.
- A debugger view (disassembly, memory viewer, framebuffer inspector,
  breakpoints) — not optional polish; this is a primary tool you'll use
  constantly during bring-up, so build it early (see roadmap Phase 1).

## Repository layout

```
sega-model-1-emu/
├── docs/
│   └── planning/          (this planning doc set)
│   └── hardware-notes/    (research findings, memory maps, chip notes — Phase 0+)
├── src/
│   ├── cpu/
│   │   ├── v60/           (main CPU)
│   │   ├── mb86233/       (TGP/geometrizer — real LLE core, see hardware notes)
│   │   ├── m68000/        (sound/music board CPU)
│   │   └── z80/           (I/O board CPU; also used for Star Wars Arcade's DSB)
│   ├── tgp/
│   ├── video/
│   ├── sound/
│   ├── io/
│   ├── bus/
│   ├── board/
│   │   └── games/
│   └── frontend/
├── tests/
│   ├── unit/              (per-instruction / per-chip unit tests)
│   └── trace-compare/     (oracle-diffing harness — see testing doc)
├── third_party/           (vendored permissively-licensed deps, each with its license file)
├── roms/                  (gitignored — user supplies their own dumps)
├── CMakeLists.txt
├── LICENSE
└── NOTICE                 (attributions — see legal doc)
```

## Build & dependencies

- **Build system:** CMake.
- **Windowing/input/audio:** SDL2.
- **Debug UI:** Dear ImGui (permissive MIT license, standard choice for this
  kind of tool overlay).
- **Testing:** Catch2 or GoogleTest for unit tests; a custom small harness for
  trace-comparison against the MAME oracle (see testing doc).
- **Audio chip cores:** evaluate existing permissively-licensed libraries
  during Phase 0 rather than assuming; document the final choice and its
  license in `NOTICE`.

## Save states

Every component implements `serialize(Writer&)` / `deserialize(Reader&)`.
The board aggregates these into one versioned snapshot format. This exists
from day one not primarily as a player-facing feature but because the
trace-comparison testing strategy (testing doc) depends on being able to
snapshot and diff state cheaply and often.

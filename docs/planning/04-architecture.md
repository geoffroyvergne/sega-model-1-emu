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

### TGP — 3D math coprocessor (`src/tgp/`)
- Starts as a **behavioral model**: given the same command stream the real
  chip would receive (matrix loads, vertex data, transform/clip requests),
  reproduce its outputs (transformed/projected/clipped vertices, generated
  polygon lists) using reverse-engineered algorithms rather than emulating
  actual chip microcode.
- Interface is deliberately identical whether backed by HLE or (later) LLE,
  so the rest of the system doesn't care which is active. This isolates the
  project's single biggest risk (challenges doc, #1) behind one seam.

### Video subsystem (`src/video/`)
- **Tile/sprite layer**: conventional tilemap + sprite compositing, used for
  HUD, backgrounds, and UI — bring this up first since it's far better
  understood than the 3D path and gives an early visual signal that the CPU
  core and memory map are basically correct.
- **Quad rasterizer**: consumes polygon lists from the TGP and rasterizes
  Sega's shaded quads (flat/Gouraud) into the frame buffer, respecting the
  original draw-order-based sorting behavior rather than a modern Z-buffer
  (see challenges doc, #4). Runs on the CPU (software rasterization) to
  match original ordering/blending semantics; do not offload this to a GPU
  shader pipeline for v1 — correctness before performance.
- Produces one framebuffer per frame that the frontend blits (optionally
  GPU-upscaled/filtered at the presentation stage only, never in the logic
  that decides pixel values).

### Sound unit (`src/sound/`)
- Z80 interpreter (well-trodden territory; reuse well-understood public
  patterns for the core, but keep it a clean, self-contained implementation
  living in `src/cpu/z80/`).
- FM synthesis chip (YM3438-family) and Sega MultiPCM sample-playback chip
  emulation. Prefer building on an existing permissively-licensed,
  well-regarded chip-emulation library (e.g. a BSD/MIT-licensed FM synth
  core) rather than reinventing FM synthesis math from scratch — confirm
  exact chip identities and best available library options during Phase 0
  research, and record the choice + license in the legal doc.
- Runs on the same master scheduler, not a separate audio thread with its
  own notion of time, to keep music/SFX sync correct.

### I/O boards (`src/io/`)
- One implementation per control scheme: analog wheel + pedals (Virtua
  Racing), digital 8-way + buttons (Virtua Fighter), analog stick + trigger
  (Star Wars Arcade).
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
│   │   ├── v60/
│   │   └── z80/
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

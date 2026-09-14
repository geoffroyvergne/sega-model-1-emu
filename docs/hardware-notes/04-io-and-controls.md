# 04 — I/O Board & Controls [verified against MAME source]

## Standard I/O board (used by Virtua Racing, Virtua Fighter, Star Wars Arcade)

Source: `model1io.cpp`, device `SEGA_MODEL1IO` — board part number
`837-8950-01`. This is the correct file for our three target games. (There
is a second, "advanced" I/O board in `model1io2.cpp` — that one is for Wing
War / NetMerc / Virtua Cop, **not** our targets; don't confuse the two.)

- **CPU:** Zilog Z80 (`Z0840004PSC`), 4 MHz.
- **Sega custom chip `315-5338A`** (QFP100) — handles I/O port
  multiplexing/decoding; treat as fixed logic to reproduce behaviorally, not
  a chip needing its own instruction-level emulation.
- **Analog-to-digital: OKI M6253** — reads analog inputs (this is how the
  steering wheel/pedals and analog stick get digitized for the game code).
- **EEPROM: 93C45** (128 bytes ×8) — stores settings/calibration.
- **MB3771** — master reset supervisor IC (hardware-level reset behavior,
  not gameplay-relevant beyond "board starts in a clean reset state").

## Connectors (tell us what's electrically attached, and to what)

| Connector | Purpose |
|---|---|
| CN1 (50-pin) | Control panel assembly (buttons/joystick — Virtua Fighter's digital 8-way+buttons and Star Wars Arcade's stick+trigger connect here) |
| CN2 (26-pin) | Foot pedal assembly (Virtua Racing's accelerator/brake) |
| CN3 (10-pin) | Power input |
| CN4 (6-pin) | To Sound PCB CN2 — "sound communication from Main PCB to Sound PCB" |
| CN5 (12-pin) | Input/output controls |
| CN6 (12-pin) | To Motor PCB — force-feedback wheel motor (Virtua Racing only; **out of scope**, see overview doc non-goals) |
| TL1 | Network optical link (Virtua Racing twin/link cabinets; **out of scope**) |

This confirms two of the project's already-stated non-goals are correctly
scoped out at the hardware level too: force-feedback (Motor PCB, CN6) and
multi-cabinet link play (TL1) are physically separate subsystems from the
core I/O board we actually need to emulate for single-cabinet play.

## Per-game control scheme (still to fully verify against each game's input port definitions)

- **Virtua Racing:** analog wheel + pedals via CN2/M6253, likely a view-change
  button and gear shifter on the control panel (CN1) — confirm exact button
  layout from the game's `ioport()` definitions in `model1.cpp` during
  Phase 7, not assumed here.
- **Virtua Fighter:** digital 8-way joystick + punch/guard/kick buttons, all
  through CN1 — no analog inputs needed, simplest of the three I/O-wise.
- **Star Wars Arcade:** analog flight-stick + trigger via CN1/M6253.

## The I/O board is a full second computer, not a register bank [confirmed against model1io.cpp/.h and model1.cpp]

Reading `model1io.cpp`/`model1io.h` and the relevant parts of `model1.cpp`
directly (Phase 7 research) confirms the earlier "still to verify" note
above: the standard I/O board isn't a handful of memory-mapped registers
the main V60 reads — it's its own complete Z80-based computer:

- The board's Z80 runs real firmware (`EPR-14869` for Virtua Racing;
  `EPR-14869B` for Virtua Fighter/Star Wars Arcade — same fingerprint-only
  policy as every other ROM, see `docs/planning/06-legal-and-assets.md`)
  that talks to the Sega custom `315-5338A` chip (a fixed-function
  parallel-port-style I/O multiplexer, modeled behaviorally in MAME, not
  microcode-emulated) and the OKI `M6253` ADC (which is what actually
  digitizes the wheel/pedals/stick).
- `315-5338A`'s 8 GPIO "ports" (PA-PG in the source) are individually
  wired to specific jobs, confirmed from `model1io_device`'s callback
  wiring: PA is write-only and drives the 93C45 EEPROM's clock/CS/DI
  lines plus a "which control set is active" flip-flop
  (`m_secondary_controls`); PB/PC/PD read either that active control
  set's analog-adjacent digital inputs or one of 3 DIP-switch banks,
  selected by that same flip-flop; PE is a bidirectional link to a
  "drive board" (Virtua Racing's force-feedback motor controller — out of
  scope, see the overview doc's non-goals); PF is a generic output latch
  (coin counters, lamps); PG reads the EEPROM's data-out line plus 4
  cabinet test/service buttons. The 4 analog channels are also
  double-buffered the same way (2 "primary" + 2 "secondary" per ADC
  channel, switched by the same flip-flop) — confirmed by
  `analog0_r`..`analog3_r` each picking between `m_an_cb[N]` and
  `m_an_cb[N+4]`.
- **The board talks to the main V60 board through a dual-port RAM chip**
  (`mb8421`, 2KB), not a serial link or a bank of discrete registers:
  `model1.cpp` wires the I/O board's `read_callback`/`write_callback` to
  the DPRAM's "left" port, and maps the main CPU's `0xc00000-0xc00fff`
  (byte-wide, `.umask16(0x00ff)` — matches every other byte-wide
  peripheral already confirmed on this bus) to that same DPRAM's "right"
  port (`model1_state::dpram_r` / `mb8421_device::right_w`). The read
  side additionally inserts a 1-cycle wait-state on the V60
  (`adjust_icount(-1)`) — a real hardware detail, but pure timing with no
  effect on the emulator until cycle-accurate scheduling exists.
- **The DPRAM's byte-offset layout (which offset means "wheel angle" vs.
  "brake" vs. "button state") is not defined anywhere in MAME's own
  driver source.** It's a private convention between the I/O board's real
  Z80 firmware and the real game ROM — both proprietary binaries neither
  MAME nor this project has any static knowledge of. MAME gets this right
  purely by *running both ROMs faithfully* and letting them agree with
  each other over shared memory; it never hardcodes or documents the
  convention itself. This rules out reverse-engineering a plausible-
  looking offset table by inspection — there is nothing to inspect, and
  guessing one would be exactly the kind of unverifiable behavior this
  project's research discipline exists to avoid.

**Decision**: build a real Z80 CPU core (`src/cpu/z80/`) and, in a later
increment, the `315-5338A`/`M6253`/DPRAM glue around it, so a user's own
legally-dumped `EPR-14869[B]` can run the real protocol — the same
approach already taken for the main V60/TGP ROMs, just for a second,
much simpler CPU. This is more work than "wire up a register bank," but
it's the only way to get real analog input working without inventing
unverifiable behavior. See `docs/planning/05-roadmap.md` Phase 7.

## Open questions for the next research pass

- Exact `ioport()` port/bit definitions per game in `model1.cpp` (needed to
  build accurate input mapping tables once the I/O board's glue logic is
  wired up).
- The `315-5338A`'s own read/write protocol at the Z80's `0x8000-0x800f`
  (confirmed above) and the `msm6253`'s `0xc000-0xc003` — needed before
  the glue-logic increment; not yet read in detail.

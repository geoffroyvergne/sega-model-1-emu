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

## Open questions for the next research pass

- Exact `ioport()` port/bit definitions per game in `model1.cpp` (needed to
  build accurate input mapping tables for Phase 7/8/9).
- Exact protocol on the I/O board's Z80 ↔ main V60 link (which memory-mapped
  dual-port RAM region, timing/handshake) — the main map's
  `0xc00000-0xc00fff` "I/O — dual-port RAM" region (see `01-cpu-and-bus.md`)
  is the likely channel; confirm by reading the relevant handlers.

# 01 — Main CPU & Bus [verified against MAME source]

## Main CPU

- **NEC V60**, exact part `uPD70615GD-16`.
- Clock: `32_MHz_XTAL / 2` = **16 MHz** (confirmed directly in
  `model1.cpp`'s machine config: `V60(config, m_maincpu, 32_MHz_XTAL / 2);`).
- Runs the main game logic for all games on this driver, including our three
  targets (Virtua Racing, Virtua Fighter, Star Wars Arcade).

## Main CPU address map (from `model1_mem()` in `model1.cpp`)

| Range | Contents |
|---|---|
| `0x000000–0x0fffff` | ROMA — program ROM |
| `0x100000–0x1fffff` | ROMO — banked ROM |
| `0x200000–0x2fffff` | ROMX |
| `0x400000–0x40ffff` | RAMA — NVRAM |
| `0x500000–0x53ffff` | RAMB — work RAM |
| `0x600000–0x61ffff` | TGP display lists (shared with coprocessor) |
| `0x700000–0x7fffff` | SCR — tile/character RAM |
| `0x900000–0x903fff` | COL — palette RAM |
| `0x910000–0x91bfff` | Color translation RAM |
| `0xc00000–0xc00fff` | I/O — dual-port RAM |
| `0xd00000–0xd80003` | Coprocessor (TGP) RAM + FIFO interface — see below |
| `0xe00000–0xe0000f` | Glue logic: interrupts, timers, ROM banking |
| `0xf80000–0xffffff` | ROM0 — fixed boot ROM |

This is the map used by the standard `model1_mem()` handler; treat it as
correct for vr/vf/swa unless a per-game override shows up during Phase 1/2
bring-up (some ROM_START entries define extra regions, e.g. `dsbz80:*` for
Star Wars Arcade's digital sound board — see `04-sound.md`).

## Main CPU ↔ TGP interface (from `model1.cpp` address map, verified)

```
0xd00000-0xd00001   v60_copro_ram_adr_r / _w   — set the TGP-RAM address pointer
0xd20000-0xd20003   v60_copro_ram_r / _w        — read/write TGP RAM at that pointer (auto-increment)
0xd80000-0xd80003   v60_copro_fifo_r / _w       — bidirectional command/data FIFO to the TGP
```

Two `GENERIC_FIFO_U32` devices (`copro_fifo_in`, `copro_fifo_out`) back the
FIFO side of this. This is the channel the main V60 uses to hand the TGP
vertex/matrix data and pull back transformed results — the exact command
protocol (what values get written/read in what order) is the next thing to
document once we're implementing Phase 2/5, by tracing real boot/gameplay
sequences in the MAME oracle rather than guessing from the address map alone.

## Interrupts / timers / banking

`0xe00000-0xe0000f` is described as "glue logic" for interrupts, timers, and
ROM banking. `model1.cpp` implements per-IRQ timers (`m_irq0_timer`) and a
`bank1` memory bank switched via a register write (see e.g. the `netmerc`
banking comment in source: "netmerc has bit 0x80 set when banking, probably
not a bank bit" — a reminder that even MAME's own authors flag some of this
as not fully understood). Full interrupt vector/priority behavior is a
Phase 1/2 research task, not yet detailed here.

## Open questions for the next research pass

- Exact interrupt sources/vectors and their timing relative to video
  scanout (needed for Phase 2's "reaches the same boot steady-state as the
  oracle" exit criterion).
- Whether vr/vf/swa share `model1_mem()` unmodified or install per-game
  address map tweaks (check each `ROM_START`/`MACHINE_CONFIG` block, not
  just the shared default).

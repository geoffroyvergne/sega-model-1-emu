# 08 — Main Bus & ROM Loading [verified against MAME source]

Phase 2 work: `src/bus/model1_bus.{h,cpp}` implements the confirmed main
CPU address map as a `cpu::v60::Bus`, and `src/board/rom_set.{h,cpp}` +
`src/board/games/virtua_racing.{h,cpp}` implement generic ROM
loading/validation plus Virtua Racing's real region layout. This updates
and supersedes `01-cpu-and-bus.md`'s address-map table with the exact
function fetched directly from `model1_mem()`/`model1_io()` in
`model1.cpp` — considerably more precise than the earlier summary.

## The exact address map (confirmed from `model1_mem()`)

| Range | Contents | Status here |
|---|---|---|
| `0x000000–0x0fffff` | ROMA | loadable; unused by Virtua Racing specifically (erased/0xFF) |
| `0x100000–0x1fffff` | ROMO, bank-switched via `0xE00004` | real bank switching implemented |
| `0x200000–0x2fffff` | ROMX | loadable |
| `0x400000–0x40ffff` | RAMA (NVRAM) | plain RAM (persistence not modeled yet) |
| `0x500000–0x53ffff` | RAMB (work RAM) | plain RAM |
| `0x600000–0x60ffff` / `0x610000–0x61ffff` | TGP display lists 0/1 | plain RAM (double-buffered; real content is Phase 6) |
| `0x680000–0x680003` | `listctl` (display-list buffer select) | register stub, stores value |
| `0x700000–0x70ffff` | SCR tile RAM | plain RAM stub, pending Phase 3 |
| `0x720000`/`0x740000`/`0x760000`/`0x770000` | unknown/H-sync/V-sync/video-sync-switch registers | write-only no-ops |
| `0x780000–0x7fffff` | SCR character RAM | plain RAM stub, pending Phase 3 |
| `0x900000–0x903fff` | palette RAM | plain RAM stub, pending Phase 3/6 |
| `0x910000–0x91bfff` | color translation RAM | plain RAM stub, pending Phase 3/6 |
| `0xc00000–0xc00fff` | I/O dual-port RAM | plain RAM stub, pending Phase 7 |
| `0xc40000–0xc40003` | i8251 UART | stub, reads 0 |
| `0xd00000–0xd00001` | `v60_copro_ram_adr` | **real, confirmed logic** (see below) |
| `0xd20000–0xd20003` | `v60_copro_ram` data | **real, confirmed logic** (see below) |
| `0xd80000–0xd80003` | TGP command/result FIFO | stub — real HALT handshake needs a scheduler (Phase 5) |
| `0xdc0000–0xdc0003` | `fifoin_status_r` | stub, reads 0 |
| `0xe00000` | `irq_control_w` | stub — this project's CPU has no interrupt delivery yet either |
| `0xe00002` | `irq_mask_r`/`w` | stub, stores/returns a byte |
| `0xe00004–0xe00005` | `bank_w` | **real, confirmed logic** — see below |
| `0xe00006–0xe0000b` | timer mode/period | stub, stores values |
| `0xe0000c–0xe0000f` | `timer_r` | stub, reads 0 |
| `0xf80000–0xffffff` | ROM0 (fixed boot ROM) | loadable |

A detail not in the earlier summary: `model1_io()` (the V60's **separate**
IO address space — distinct from the program address space, an
architectural feature of the V60 confirmed by its existence here) exposes
the *same* TGP RAM/FIFO addresses again. Not yet modeled (this project's
`Bus` interface doesn't yet distinguish IO-space accesses from
program-space ones) — worth revisiting if a target game is found to use
the IO-space path instead of the program-space one for the TGP interface.

## ROMO bank switching — real, confirmed logic

Confirmed against `model1.cpp`'s `bank_w`: writing to `0xE00004-5`, the low
nibble of the value selects which region's bank changes (`0x1` = ROMO,
`0x2` = ROMX, `0xf` = ROM0 — the latter two are, per the reference's own
comment, **unused by every known ROM set**, so only `0x1` is implemented
here with real effect). For `0x1`, bits 4-6 of the value select one of up
to 8 possible 1MB banks, each drawn from `0x1000000 + 0x100000*N` within
the original combined ROM image. Virtua Racing populates banks 0-3 (from
`mpr-14880` through `mpr-14889`, 8 files in interleaved pairs — see below).

## The TGP RAM interface — real, confirmed logic, and a real bug it caught

Confirmed against `model1_m.cpp`'s `v60_copro_ram_adr_r/w` and
`v60_copro_ram_r/w` (already documented in
`02-tgp-coprocessor.md`, reproduced here as the actual implementation):
a 16-bit address register (`0xd00000-1`) points into a shared array of
`0x2000` 32-bit words; reading/writing the high half of the addressed word
via `0xd20002-3` auto-increments the address register when its bit
`0x8000` is set, and the array itself is exactly what a future TGP core
(Phase 5) will read/write from its own side.

**This register is genuinely 16-bit-wide in hardware** — confirmed by the
reference handlers' own `u16 offset`/`u16 data` signatures. Building this
project's `read16`/`write16` generically from two independent
`read8`/`write8` calls (the pattern used for every plain RAM/ROM region,
where byte decomposition is harmless) is **wrong** for this specific
register: each byte access independently re-triggered the "commit the
32-bit word, maybe auto-increment" logic, so writing a 16-bit value one
byte at a time actually committed twice — the first commit using a
still-stale other-half latch value, and the address auto-incrementing
twice instead of once. **Caught by a unit test exercising two consecutive
auto-incrementing writes** (`model1_bus_test.cpp`), not by inspection — the
symptom was a second write landing on the wrong word entirely. Fixed by
making `read16`/`write16` the *primary* handlers for this register (and
the FIFO/status registers alongside it), with `read8`/`write8` synthesized
from them instead of the other way around. General lesson for the rest of
this bus as more registers get real behavior: check whether a register's
*real* width matters before assuming byte-level decomposition is free.

## Virtua Racing's real ROM layout (fingerprints only — see legal doc)

Confirmed against `model1.cpp`'s `ROM_START( vr )` block. Filenames/sizes/
CRC32s are recorded in `src/board/games/virtua_racing.cpp` purely so a
user's own legally-dumped files can be validated — no ROM bytes are
included or fetched by this project (see
`docs/planning/06-legal-and-assets.md`).

- **ROMX** (`0x200000-0x2fffff`): two 512KB files (`epr-14882.14`,
  `epr-14883.15`), interleaved byte-by-byte — the real hardware's
  `ROM_LOAD16_BYTE` pattern, confirmed pervasive across this ROM set.
- **ROM0** (`0xf80000-0xffffff` window): two 128KB files at fixed offsets
  within the window — `epr-14878a.4` at `0xfc0000`, `epr-14879a.5` at
  `0xfe0000` — with the rest of the window (`0xf80000-0xfbffff`) left
  erased. Not interleaved; each is a plain, separate load.
- **ROMO banks 0-3**: 8 files (`mpr-14880` through `mpr-14889`), each bank
  formed from two 512KB files interleaved the same way as ROMX.
- **Not yet modeled** (belong to later phases): the TGP coprocessor
  program ROM (`315-5573.bin`, Phase 5), the sound/music board's 68000
  program and MultiPCM sample ROMs (Phase 4), the TGP polygon/model ROMs
  and `copro_data` (Phase 5/6), and the I/O board's EEPROM (Phase 7).

## Open items for the next research pass

- The exact ROM layout (filenames/interleaving) for Virtua Fighter and
  Star Wars Arcade — not yet fetched; Virtua Racing was prioritized as the
  lead bring-up target per the strategy doc.
- Whether any target game's boot code actually exercises the V60 IO
  address space's duplicate TGP mapping (`model1_io()`) instead of the
  program-space one.
- Real interrupt vector/timing behavior (`irq_control_w`/timers) — blocked
  on this project's V60 core not implementing interrupt delivery yet
  either; revisit together.

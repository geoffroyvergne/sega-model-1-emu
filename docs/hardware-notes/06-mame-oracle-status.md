# 06 — Reference Oracle Status Per Target Game [verified against MAME source]

Critical finding for how much we can actually lean on MAME as a "ground
truth oracle" (per the testing doc): **MAME's own driver does not claim full
correctness for two of our three target games.** This is read directly from
the `GAME()`/`GAMEL()` macro status flags in `model1.cpp`.

| Game | ROM set | MAME status flags | Meaning |
|---|---|---|---|
| **Virtua Racing** | `vr` | *(none — flags = 0)* | MAME considers this fully working. **Best oracle of the three** — validates the strategy doc's choice to bring this game up first. |
| **Virtua Fighter** | `vf` | `MACHINE_NOT_WORKING` | MAME itself does not consider Virtua Fighter working. |
| **Star Wars Arcade** | `swa` / `swaj` | `MACHINE_IMPERFECT_GRAPHICS \| MACHINE_IMPERFECT_CONTROLS` | Runs, but with known graphics and control inaccuracies (matches community reports of ship models periodically disappearing and analog control issues). |

(For context, also present in the same driver but **not** our targets:
`vformula` = `MACHINE_IMPERFECT_GRAPHICS`, `netmerc` = `MACHINE_NOT_WORKING`.)

## Why this matters

The whole testing strategy (see `docs/planning/07-testing-and-validation.md`)
is built on diffing our emulator against MAME as an oracle. That strategy is
on solid ground for **Virtua Racing**. For **Virtua Fighter** and **Star Wars
Arcade**, it is only partially applicable:

- Trace/state comparison against MAME is still useful early on (CPU
  execution, memory map, boot sequence) since those parts may well be
  correct even if the game isn't "fully working."
- But matching MAME's *output* exactly is not automatically "correct" for
  Virtua Fighter — the oracle itself is known-broken (its own maintainers
  say so), likely in exactly the systems we most need to get right
  (collision detection, TGP RAM port timing — see below).
- Community reports (not yet independently verified, but consistent with
  the `MACHINE_NOT_WORKING` flag) describe Virtua Fighter's collision
  detection as "significantly broken due to imperfect TGP RAM port
  emulation... breaks when both characters attack at the same time," and
  Star Wars Arcade's ship models "periodically disappear for a frame or two"
  plus unspecified analog control problems.

## Revises the roadmap

This changes the risk profile of **Phase 8 (Virtua Fighter)** and
**Phase 9 (Star Wars Arcade)** from "same bar as Phase 7, new I/O board" to:
these phases may require *original* research beyond what the oracle can
verify — we could end up doing reverse-engineering that MAME's own
contributors haven't finished either, particularly around the TGP RAM port
timing implicated in Virtua Fighter's collision bugs (see
`02-tgp-coprocessor.md`'s notes on the main-CPU↔TGP FIFO/RAM interface).
Budget accordingly, and don't be surprised if Phase 8/9 exit criteria need
loosening from "matches oracle" to "matches real observed hardware/video
behavior from other sources" for the specific systems where the oracle is
known-wrong.

# 03 — Sound Hardware [verified against MAME source]

**This corrects an assumption baked into the original architecture doc**,
which assumed a Z80-driven sound board (by analogy to Genesis/many other
Sega systems). Direct source inspection shows that's wrong for Model 1's
*music/FX* board — see below. The architecture doc has been updated.

## Main sound/music board

- **CPU: Motorola 68000 family, exact part `TMP68000N-10`** (Toshiba-made
  68000), running at **10 MHz**. This is the CPU that drives FM synthesis
  and sample playback — **not** a Z80.
- **FM chip: Yamaha YM3438**, running at **8 MHz** (`16_MHz_XTAL / 2` per
  source comment).
- **Sample/PCM chip: Sega custom `315-5560`** (QFP100) — this part number is
  Sega's own branding of the Yamaha-derived MultiPCM sample-playback chip
  used across many Sega arcade boards of this era (System 18 onward).
  Confirm this identification against the actual MAME sound-device include
  (`multipcm` device) during Phase 4, rather than taking the part-number
  correspondence purely on memory.
- Supporting glue: several Lattice GAL16V8A PALs (`315-5577/5578/5579`,
  address decode/glue logic, not independently emulated as "chips" — they're
  just fixed logic MAME's driver reproduces directly in code).
- Output stage: Sanyo `LC78820` (2-channel 18-bit DAC) + NEC `uPC844C` (quad
  op-amp) — analog output stage, not something we emulate; irrelevant once
  we're producing a digital PCM stream for the host audio API.

## Other Z80s on the board — not part of music/FX synthesis

There are real Z80s elsewhere on Model 1, but they do **not** drive FM/PCM
sound — don't conflate them with "the sound CPU":

- **I/O board Z80** (`Zilog Z0840004PSC`, 4 MHz) — drives the standard I/O
  board (analog wheel/pedals, control panel reads, EEPROM). See
  `05-io-and-controls.md`. This Z80 is required for the game to run at all
  (it's how the main V60 reads inputs), just not for audio.
- **Communication board Z80** (4 MHz) — used for Virtua Racing's twin/link
  cabinet mode. Out of scope per the project's non-goals.
- **DSB (Digital Sound Board) Z80** — a separate MPEG-audio playback board
  (`dsbz80` device in MAME), present in the **Star Wars Arcade** (`swa`/
  `swaj`) ROM sets specifically (confirmed by grepping ROM_START entries:
  `swa` and `swaj` both include `dsbz80:mpegcpu` and `dsbz80:mpeg` ROM
  regions; Virtua Racing and Virtua Fighter do not). This is an
  MPEG-decoding add-on likely used for Star Wars Arcade's music/voice —
  **a per-game hardware difference to plan for explicitly in Phase 9**,
  not something the other two games need.

## What this means for our project

- `src/cpu/` needs **both** a 68000 core (main sound/music CPU) and a Z80
  core (I/O board — needed by all three target games — plus the DSB, needed
  only by Star Wars Arcade). The original architecture doc only listed a Z80
  sound core; it's been corrected.
- MultiPCM/YM3438 emulation: prefer an existing permissively-licensed chip
  emulation library over writing FM synthesis math from scratch (per the
  architecture doc's stated preference) — confirm a specific library choice
  during Phase 0/4.
- Star Wars Arcade's DSB requirement means its Phase 9 bring-up has a real
  piece of hardware (and a second Z80 program + MPEG decode path) that
  Virtua Racing and Virtua Fighter never exercised — budget for it rather
  than assuming Phase 9 is "just a new I/O board like Phase 8 was."

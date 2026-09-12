# 06 — Legal & Assets

## ROMs

- Game ROM data (program code, graphics, sound samples) is copyrighted by
  Sega and is **never** included in, downloaded by, or linked from this
  repository or its tooling.
- Users of this emulator must supply their own ROM dumps, obtained legally
  (e.g. dumped from a board/cartridge they own). Document this clearly in
  the top-level README and in any release notes.
- `roms/` is gitignored. No code in this project should fetch, scrape, or
  reference any ROM-hosting site.
- Per-game descriptors (architecture doc) may reference expected filenames/
  sizes/checksums purely to *validate* a user-supplied dump, never to
  source one.

## Use of MAME as a reference

The chosen strategy (per your decision) is **"MAME as documentation only"**:
we study MAME's Model 1 driver to understand hardware behavior, but write
original code ourselves.

Concretely, this means:

- **Allowed:** reading MAME's source to understand memory maps, chip
  behavior, timing, and register semantics; using it as a build-and-run
  oracle for trace/framebuffer comparison (Phase 0 onward); citing it in
  `docs/hardware-notes/` as a source for a given fact.
- **Not allowed:** copy-pasting or closely paraphrasing MAME's C/C++ code
  into this project. If a piece of logic ends up structurally identical to
  MAME's because the hardware genuinely only allows one sane implementation
  (rare, but possible for e.g. a simple address decode), write it
  independently from the *behavior*, not by transcribing their code.
- If this policy is ever revisited toward "fork/adapt MAME's code," be aware
  that MAME is GPLv2+, and any project (or module) incorporating its code
  must itself be GPL-compatible and distributed under compatible terms. That
  is a real, binding decision, not a formality — revisit deliberately if it
  comes up, don't drift into it file by file.
- Give credit regardless: add a `NOTICE`/`CREDITS` file acknowledging the
  MAME project and its contributors' reverse-engineering work on Model 1,
  since this project would not be feasible without that groundwork existing
  as a reference.

## Third-party libraries (`third_party/`)

- Prefer permissively licensed dependencies (MIT/BSD/zlib) for anything
  vendored into the build (SDL2, Dear ImGui, any FM-synthesis/PCM chip
  emulation library, test framework).
- Record each dependency's license in `NOTICE` at the point it's added —
  don't let this accumulate as end-of-project cleanup.

## Trademarks

"Sega," "Virtua Racing," "Virtua Fighter," "Star Wars," and related names are
trademarks of their respective owners. This project is an unaffiliated,
non-commercial fan reverse-engineering/emulation effort. Don't use these
names or logos in ways that imply endorsement (branding, app icons, store
listings) — referring to them descriptively (as this document does) is fine.

## Project license

Pick and add a `LICENSE` file for the original code in this repository
during Phase 0 (e.g. MIT/BSD/GPL — GPL only if you've deliberately decided to
incorporate GPL-licensed code per the MAME policy above; otherwise a
permissive license is the simpler default for original, from-scratch code).

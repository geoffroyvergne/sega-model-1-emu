# V60 oracle trace-comparison harness

Validates `src/cpu/v60/`'s original V60 implementation against MAME's real
`v60_device` core, without needing any game ROM. This is dev-time tooling
only — it depends on MAME's own headers and links against the real
reference core purely as a test oracle, so it can only be built inside a
local MAME checkout. It is **not** part of the CMake build and is never
shipped with the emulator.

Per this project's legal policy (`docs/planning/06-legal-and-assets.md`),
this counts as using MAME's source as a build-time reference tool, not as
copying its code — `v60test.cpp` contains no CPU emulation logic of its
own, only glue to load a test program, run it, and dump register state.

## What's here

- **`v60test.cpp`** — a minimal MAME driver: just a `v60_device` plus RAM,
  no Model 1 specifics. Loads a test program from a file (path/format in
  the file's own header comment), lets it run, and dumps final register
  state to another file.
- **`compare_v60.py`** — builds small V60 machine-code test programs
  (matching the ones in `tests/unit/v60_test.cpp`), runs them through the
  harness above, and checks the resulting registers/PC against expected
  values.

## One-time setup

```sh
git clone --depth 1 https://github.com/mamedev/mame.git /path/to/mame
cp tools/oracle-harness/v60test.cpp /path/to/mame/src/mame/tests/v60test.cpp

# genie's driver scanner requires every GAME() driver to be registered in
# mame.lst, even for a scoped SOURCES= build -- add this once:
printf '\n@source:tests/v60test.cpp\nv60test\n' >> /path/to/mame/src/mame/mame.lst

cd /path/to/mame
make SOURCES=src/mame/tests/v60test.cpp REGENIE=1 -j$(nproc)
```

`REGENIE=1` is only needed the *first* time (or after changing `SOURCES=`
to point at a different file/driver set) — it forces MAME's genie project
generator to re-scan, which a plain `make SOURCES=...` skips if it thinks
nothing changed, silently building the previous driver selection instead.

Building is fast: it's scoped to this one driver, not MAME's full
~1000-driver tree.

## Running

```sh
MAME_DIR=/path/to/mame python3 tools/oracle-harness/compare_v60.py
```

Expect `N/N programs matched the MAME v60_device reference exactly.` Any
mismatch means either our core or the test's expectations are wrong —
treat it the same way the two real bugs earlier in this project were
found: re-derive the fact from source rather than guessing which side is
right.

## A real gotcha this harness surfaced (don't repeat it)

Test programs **must** end in an infinite self-branch (`BR8` with
displacement `0`, i.e. `0x6a, 0x00`), never just "fall off the end." The
V60's `HALT` opcode does not actually halt anything in this core (matching
the real chip's own reference implementation) — it just advances PC by 1
and execution continues into whatever comes next. Landing on uninitialized
(zero) memory means an unbroken stream of `HALT`s, and since the harness
runs for a fixed wall-clock budget (`-seconds_to_run 1`), the final PC
after "falling through" is a moving target that depends on exact timing,
not a stable value you can assert against. Always give a test program a
real stopping point.

Also: the V60's reset vector is `0xFFFFFFF0` by class default, but this
configuration's address bus is masked to 24 bits, so it actually resolves
to `0x00FFFFF0` — test programs are loaded there, not at address 0. See
`docs/hardware-notes/07-v60-architecture.md` for the full story (including
why the *PC register itself* still displays the raw, unmasked value after
execution, even though bus accesses are masked).

For the same reason, `v60test.cpp` pre-fills address `0` with its own
infinite self-branch before loading your test program — a landing pad for
anything that jumps/calls to a low absolute address (e.g. testing an
immediate-addressed `JMP`), so it settles on a checkable state instead of
drifting through uninitialized (HALT-decoding-but-not-halting) memory too.

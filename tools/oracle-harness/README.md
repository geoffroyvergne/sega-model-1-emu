# CPU oracle trace-comparison harnesses

Validates this project's original CPU cores (`src/cpu/v60/`, `src/cpu/z80/`)
against MAME's real reference cores, without needing any game ROM. This is
dev-time tooling only — it depends on MAME's own headers and links against
the real reference cores purely as a test oracle, so it can only be built
inside a local MAME checkout. It is **not** part of the CMake build and is
never shipped with the emulator.

Per this project's legal policy (`docs/planning/06-legal-and-assets.md`),
this counts as using MAME's source as a build-time reference tool, not as
copying its code — neither `v60test.cpp` nor `z80test.cpp` contains any CPU
emulation logic of its own, only glue to load a test program, run it, and
dump register state.

## What's here

- **`v60test.cpp`** / **`compare_v60.py`** — the V60 harness (see the V60
  section below for its own gotchas).
- **`z80test.cpp`** — a minimal MAME driver: a `z80_device` plus RAM on
  both the memory *and* I/O address spaces (the latter purely so IN/OUT
  test programs have somewhere real to read/write -- no I/O board
  specifics, e.g. no `315-5338A`). Loads a test program at address 0 (the
  Z80's fixed reset vector), lets it run, and dumps final register state.
- **`compare_z80.py`** — builds small Z80 machine-code test programs
  (matching the ones in `tests/unit/z80_test.cpp`), runs them through the
  harness above, and checks the resulting registers/flags against expected
  values.

## One-time setup

```sh
git clone --depth 1 https://github.com/mamedev/mame.git /path/to/mame
cp tools/oracle-harness/v60test.cpp /path/to/mame/src/mame/tests/v60test.cpp
cp tools/oracle-harness/z80test.cpp /path/to/mame/src/mame/tests/z80test.cpp

# genie's driver scanner requires every GAME() driver to be registered in
# mame.lst, even for a scoped SOURCES= build -- add both once:
printf '\n@source:tests/v60test.cpp\nv60test\n' >> /path/to/mame/src/mame/mame.lst
printf '\n@source:tests/z80test.cpp\nz80test\n' >> /path/to/mame/src/mame/mame.lst

cd /path/to/mame
make SOURCES=src/mame/tests/v60test.cpp,src/mame/tests/z80test.cpp REGENIE=1 -j$(nproc)
```

`REGENIE=1` is only needed the *first* time (or after changing `SOURCES=`
to point at a different file/driver set) — it forces MAME's genie project
generator to re-scan, which a plain `make SOURCES=...` skips if it thinks
nothing changed, silently building the previous driver selection instead.

Building is fast: it's scoped to these two drivers, not MAME's full
~1000-driver tree.

## Running

```sh
MAME_DIR=/path/to/mame python3 tools/oracle-harness/compare_v60.py
MAME_DIR=/path/to/mame python3 tools/oracle-harness/compare_z80.py
```

Expect `N/N programs matched the MAME <device> reference exactly.` for
each. Any mismatch means either our core or the test's expectations are
wrong — treat it the same way the two real bugs earlier in this project
were found: re-derive the fact from source rather than guessing which side
is right.

Both scripts pass `-video none -window -nomaximize` to every MAME
invocation. `-video none` alone is **not** enough to keep MAME off your
screen: per MAME's own `-showusage` text, `-window` "enable[s] window
mode; otherwise, full screen mode is assumed" — fullscreen is the
default regardless of the video *rendering* backend, so without
`-window` each of the dozens of per-test MAME launches would briefly
seize the whole display. Found the hard way (it made a laptop unusable
mid-run) — keep these flags if you extend either script.

## V60: a real gotcha this harness surfaced (don't repeat it)

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

## Z80: simpler than V60 in two ways worth knowing

- The Z80 always resets to `PC=0x0000` — no configurable or masked reset
  vector to account for, unlike the V60. Test programs load straight at
  address 0.
- The Z80's `HALT` (`0x76`) genuinely halts (PC stops advancing) in both
  this reference core and `src/cpu/z80/`'s own implementation, so a plain
  `HALT` is a perfectly stable landing point — no self-branch trick needed.

`compare_z80.py` checks flag bits via two pseudo-keys, `F_set`/`F_clear`
(bitmasks), rather than exact-matching the whole `F` register the way every
other register is checked. This is deliberate: `F`'s undocumented Y/X bits
(and any flag a given test doesn't care about) would otherwise have to be
predicted exactly by hand for every single test just to make the equality
check pass, which isn't the point of these tests — they're checking the
*documented* flag semantics implemented so far in `src/cpu/z80/`, not
independently re-deriving the undocumented-bit behavior test-by-test.

The suite also includes a couple of deliberately adversarial probes of
SCF/CCF's undocumented Y/X flags (see `z80.h`'s comment on the
"mirror the current accumulator" approximation this core uses) — these
were used to empirically confirm that approximation against real hardware
behavior rather than leaving it as an untested guess, and are kept as
permanent regression tests.

## Confirmed working end-to-end

Both harnesses have been built and run against a real MAME checkout:
**37/37** for `compare_v60.py`, **70/70** for `compare_z80.py`. If you hit
a build error following the setup steps above, it's likely a MAME version
skew (device state enum values, macro names, or address-space APIs
occasionally change upstream) rather than a problem with the harness
files themselves — diff against the current upstream `cpu/v60/v60.h` /
`cpu/z80/z80.h` to see what moved.

This methodology has caught 3 real bugs so far before they shipped: two
in the V60 core (see `compare_v60.py`'s own history) and one in the Z80
core's CB-prefixed group — `BIT b,(HL)`'s undocumented Y/X flags mirror
the high byte of HL+1 (an internal address-latch artifact), not the
tested byte, which this core initially got wrong by reusing the (correct,
for a register operand) "mirror the operand" approximation. See
`z80.h`'s comment on this and the corresponding test in `compare_z80.py`
for the discriminating case that caught it.

## A gotcha in the test *script* itself, not the core (don't repeat it)

When adding the ED-prefixed group's oracle tests, two initially came back
mismatched for a reason that turned out to be `compare_z80.py`'s own bug:
`LD BC,2` needs its immediate encoded low-byte-first as `0x01, 0x02,
0x00` — writing `0x01, 0x00, 0x02` (an easy transposition, especially
when copy-pasting a `LD BC,nn` byte sequence and changing "just the
value") loads `BC=0x0200` instead. Both call sites got this wrong
identically, which is exactly the kind of mistake that's invisible from
re-reading your own test in isolation — only the oracle mismatch caught
it. A third mismatch in the same batch was real but not a bug in either
side: a naive "does `LD R,A` then `LD A,R` round-trip plainly" test came
back 3 higher than expected, which is real hardware's R-register
auto-increment (on every M1/opcode-fetch cycle) doing exactly what real
silicon does — a quirk this core has always listed as not-yet-implemented,
not a new finding. There's no way to write a meaningful oracle test for
`LD R,A`/`LD A,R` specifically until that auto-increment exists, so that
test was removed rather than "fixed."

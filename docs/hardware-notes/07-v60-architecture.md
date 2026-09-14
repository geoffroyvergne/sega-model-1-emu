# 07 — NEC V60 Architecture & Instruction Encoding [verified against MAME source]

Research for actually starting Phase 1 (the main CPU core). Everything here
was read directly from MAME's `src/devices/cpu/v60/*` (used as documentation
only, per the legal doc — our own `src/cpu/v60/` implementation is original).

## Chip identity and design (confirmed from source header comment)

MAME's `v60.cpp` header quotes NEC's own selection-guide description
verbatim, confirming exactly the chip our board notes already named
(`docs/hardware-notes/01-cpu-and-bus.md`): **uPD70615 (V60)**, 16MHz, 24-bit
address bus, 16-bit external data bus, 4GB virtual memory space. Also
confirmed: **32 general-purpose 32-bit registers**, and — architecturally
important — the V60 is described as a **"2-address method"** CISC design
with independently selectable addressing modes per operand, i.e. much
closer in spirit to VAX than to a simple 68000/Z80-style CPU. This matches
why Phase 1 is budgeted 4-8 weeks in the roadmap, not less.

## Register file

32 addressing registers, indexed 0-31 by a 5-bit field used throughout the
instruction encoding: **R0-R28** general purpose, then **AP** (29, Argument
Pointer), **FP** (30, Frame Pointer), **SP** (31, Stack Pointer). PC and PSW
are separate. The reference core additionally tracks a large bank of
system/control registers (memory management, timers, protection levels —
see the `V60_*` enum in `v60.h`, ~68 entries total) that real games almost
certainly don't touch from user-mode code; **our core deliberately excludes
these for now** and will add them only if a target game is found to need
one.

## Flags actually tracked (confirmed, and narrower than you might expect)

MAME's core tracks exactly four flags: **Carry, Overflow, Sign, Zero**
(`m_flags` struct: `CY`, `OV`, `S`, `Z` — plain fields, not packed PSW bits
during execution). Despite instruction/macro names like `SetSZPF_Byte`
implying a Parity flag too, **no parity is actually computed anywhere** —
the "P" in the macro name is vestigial. Our own core matches this (see
`src/cpu/v60/v60.h`'s `Flags` struct) rather than inventing parity tracking
the reference doesn't have.

## Instruction encoding: opcode dispatch + "Format 1/2" operand encoding

- **Primary dispatch:** the first byte at PC selects one of 256 handlers
  (`s_OpCodeTable[256]`), matching a byte-value → mnemonic table read
  directly from `optable.hxx`. Some opcodes are fully self-contained
  (`HALT`=`0x00`, `NOP`=`0xCD`); most two-operand instructions ("Format
  1/2" in NEC's own terminology, hence MAME's `F12...` function names) need
  a second decode pass.
- **The instflags byte** (byte immediately after the opcode) packs, for
  every Format-1/2 instruction:
  - bit 7 (`0x80`) — both operands use "general" addressing (a modifier
    byte follows for each)
  - bit 5 (`0x20`), when bit 7 is clear — selects *which* operand is
    "general" (modifier byte follows) vs. "short" (register number packed
    directly into bits 0-4 of this same byte)
  - bit 6 (`0x40`) — selects between two addressing-mode sub-tables for a
    general operand (only matters once non-register-direct modes are
    implemented)
  - bits 0-4 — register number for whichever operand is "short"
  - A "general" operand's modifier byte itself encodes an addressing mode
    in its top 3 bits (register-direct is mode 0 — top 3 bits zero — with
    the register number in its own low 5 bits) and 7 other modes
    (indirect, autoincrement/decrement, displacement, literal, etc.) in the
    remaining values.
- **Confirmed identical logic in two places**: `F12DecodeFirstOperand`/
  `F12WriteSecondOperand` (used by `MOV`, which reads operand 1 and writes
  operand 2) and `F12DecodeOperands` (used by `CMP`, which reads both) use
  the *exact same* bit7/bit5/bit6 rules for resolving each operand — only
  what happens to operand 2 afterward (write vs. read) differs. This let us
  write one shared decoder (`Cpu::decode_format12` in our own core) for
  both instruction shapes.
- **Byte/word writes to a register preserve its untouched upper bits**
  (confirmed via the reference's `SETREG8`/`SETREG16` macros: `dest = (dest
  & ~mask) | (value & mask)`) — a real, easy-to-get-wrong detail our own
  `write_sized` replicates.

## Confirmed opcodes for this first implementation slice

| Opcode byte | Mnemonic | Notes |
|---|---|---|
| `0x00` | HALT | No operands. Reference comment: "should wait for an interrupt to occur" — not yet meaningful without interrupt support. |
| `0xCD` | NOP | No operands, no effect. |
| `0x09` / `0x1B` / `0x2D` | MOVB / MOVH / MOVW | Byte/word/long move, operand 1 (source) → operand 2 (destination). Does not affect flags. |
| `0xB8` / `0xBA` / `0xBC` | CMPB / CMPH / CMPW | Computes **operand2 − operand1** (destination minus source) for flags only; result discarded. |

Flag formulas for add/sub, confirmed exact (byte/word use native-width
wraparound, long widens to `uint64_t` so the carry-out bit isn't lost —
we mirror both, see `v60.cpp`):
```
carry(byte)  = result bit 8 set        carry(word)  = result bit 16 set       carry(long) = result bit 32 set (64-bit temp)
overflow_add = (result^src) & (result^dst) & sign_bit
overflow_sub = (dst^src)    & (dst^result) & sign_bit      <-- NOT (src^dst)&(src^result); see below
```

**A real bug we caught via testing, worth recording as a cautionary
example**: the subtraction-overflow formula's two XOR terms are *not*
symmetric — `dst^result`, not `src^result` — even though the first term
(`dst^src`) is commutative and looks like it should generalize the same
way. Our first implementation used `src^result` by mistake; a unit test
(`CMPB detects signed overflow`, constructed from first principles — dst
`0x80`/-128, src `0x01`, expecting overflow since -128-1 doesn't fit in an
int8) caught it immediately. Fixed in `src/cpu/v60/v60.cpp`. This is exactly
the kind of subtle, easy-to-transpose detail this whole CPU is full of —
treat every flag formula as something to test, not just something to read
once and trust.

## Cycle timing: genuinely unknown, even in the reference implementation

Real finding, worth its own callout: MAME's `execute_run()` contains this
exact comment and code —
```cpp
// Actual cycles / instruction is unknown
...
m_icount -= 8;  /* fix me -- this is just an average */
```
— i.e. **even the most mature available reference implementation does not
have real per-instruction V60 cycle timing**; it charges a flat average of
8 cycles regardless of opcode. This directly revises what Phase 1's exit
criterion should mean by "cycle counts" (roadmap doc): matching this same
flat-average approximation is the realistic v1 target, not some
undiscovered ground truth we'd be failing to find. Our core does the same
(`Cpu::kApproximateCyclesPerInstruction = 8`). If real timing data ever
surfaces (e.g. from a real board with a logic analyzer), this is a known,
clearly-isolated place to improve — not a sign our approach was wrong.

## A second real bug, caught by going back to the source rather than trusting the first pass

The first implementation of the "general operand" decoder treated a
modifier byte's top 3 bits alone as the addressing mode (checking only
`(modifier >> 5) != 0` for "is this register-direct"). **This is wrong.**
There are genuinely **two different 8-entry addressing-mode tables**
(`s_AMTable1[0]` and `s_AMTable1[1]`, confirmed directly in `am1.hxx`),
selected by a separate `modm` bit (`instflags & 0x40`), and the *same*
index number means different things in each table:

| Index | modm=0 table | modm=1 table |
|---|---|---|
| 0 | Displacement-8 | Double-displacement-8 |
| 1 | Displacement-16 | Double-displacement-16 |
| 2 | Displacement-32 | Double-displacement-32 |
| **3** | **Register-indirect** `[Rn]` | **Register-direct** `Rn` |
| 4 | Displacement-indirect-8 | Autoincrement `[Rn]+` |
| 5 | Displacement-indirect-16 | Autodecrement `[Rn]-` |
| 6 | Displacement-indirect-32 | "Group 6" (further sub-decode) |
| 7 | "Group 7" (further sub-decode) | invalid |

So the original code's check accepted index 0 (actually Displacement-8) as
if it were register-direct, and register-direct's *real* encoding (index 3,
modm=1) would have been rejected as unimplemented. This was internally
consistent — the handwritten unit tests matched the (wrong) implementation
— so the tests alone didn't catch it; going back to read `am1.hxx`'s actual
table definition did. **Lesson for the rest of this CPU core:** a test
suite built only against your own understanding can pass while that
understanding is wrong; re-deriving facts from the primary source (or,
later, from oracle trace comparison) is what actually catches this class of
bug, not more unit tests alone.

Fixed in `src/cpu/v60/v60.cpp`'s `decode_general_operand`, which now takes
`modm` into account and additionally implements register-indirect,
autoincrement, and autodecrement (all confirmed against `am1.hxx`/`am3.hxx`
for both read and write), plus displacement-8 (confirmed: `[Rn + sign_extend(disp8)]`,
consumes one extra byte after the modifier).

## ADD/SUB: confirmed the `Operand` design generalizes for free

ADD/SUB are read-modify-write on operand 2 (`opADDB`: `F12LOADOP2BYTE();
ADDB(appb, op1, 0); F12STOREOP2BYTE();`), unlike MOV (write-only op2) or CMP
(read-only op2, result discarded). The reference core resolves op2 for
these via `ReadAMAddress` (`am2.hxx`) rather than `ReadAM`/`WriteAM` — a
third table (`s_AMTable2`) that, confirmed by reading it, uses the *exact
same* index layout as `am1`/`am3` and the *exact same* `m_flag`-vs-address
distinction our `Operand{is_register, reg, address}` already models. So
implementing `op_add`/`op_sub` needed no new addressing-mode code at all —
`decode_format12`'s existing op2 resolution (which already returns an
`Operand` without eagerly reading or writing it) was already exactly what a
read-modify-write instruction needs. This is a small piece of evidence the
`Operand` abstraction is the right shape for this CPU, not just convenient
for the instructions it was first written for.

Also confirmed opcodes: `ADDB`=`0x80`, `ADDH`=`0x82` (16-bit — see the B/H/W
convention note above), `ADDW`=`0x84` (32-bit); `SUBB`=`0xA8`, `SUBH`=`0xAA`,
`SUBW`=`0xAC`. Flag formulas are exactly the ADD/SUB formulas already
documented above (dst/src roles: op2 is `dst`, op1 is `src`, result written
back into op2).

## Conditional branches: a clean encoding, and a real backward-relative-offset gotcha

Opcodes `0x60`-`0x7F` are all conditional branches, and the encoding is
unusually clean: the low 4 bits of the opcode select one of 15 conditions
(condition code 11 is reserved, at both `0x6B` and `0x7B`), and bit 4
selects an 8-bit (`0x60`-`0x6F`) or 16-bit (`0x70`-`0x7F`) signed
displacement — otherwise identical. Confirmed one-for-one against MAME's
`op4.hxx`, whose own file header reads `/* FULLY TRUSTED */`, i.e. this is
one of the best-validated parts of the reference core:

| CC | Mnemonic | Condition | CC | Mnemonic | Condition |
|---|---|---|---|---|---|
| 0 | BV | overflow | 8 | BN | sign |
| 1 | BNV | !overflow | 9 | BP | !sign |
| 2 | BL | carry (unsigned <) | 10 | BR | always |
| 3 | BNL | !carry (unsigned >=) | 11 | *(reserved)* | — |
| 4 | BE | zero | 12 | BLT | sign != overflow (signed <) |
| 5 | BNE | !zero | 13 | BGE | sign == overflow (signed >=) |
| 6 | BNH | carry\|\|zero (unsigned <=) | 14 | BLE | (sign!=overflow)\|\|zero (signed <=) |
| 7 | BH | !(carry\|\|zero) (unsigned >) | 15 | BGT | !((sign!=overflow)\|\|zero) (signed >) |

**Real gotcha, worth flagging loudly**: the branch displacement is added
directly to the address of the branch opcode **itself** — not the address
of the instruction that follows it, which is the more common convention
(e.g. x86, ARM). Confirmed by reading `opBR8` et al. directly: they never
advance PC before adding the displacement; the "advance past this
instruction" case (branch not taken) is a *separate* code path that adds
2 or 3 depending on displacement width. Getting this backwards (assuming
next-instruction-relative, then subtracting 2 or 3 to compensate, or simply
not realizing the distinction) would silently miscalculate every single
branch target in every game — worth a dedicated test
(`BR8 branches relative to its own opcode address, not the next
instruction`, in `v60_test.cpp`) rather than trusting it by inspection.

## Two parallel call/return conventions -- both now implemented

The V60 has **two unrelated call/return mechanisms**, confirmed by reading
both directly rather than assuming there's only one:

1. **JSR / RSR** (opcodes `0xE8`/`0xE9` and `0xCA`) — the simple pair.
   JSR decodes a single address operand directly at PC+1 (no instflags
   byte at all — `modm` is baked into which opcode, `_0` or `_1`, is used,
   confirmed by the reference's own `opJSR_0()`/`opJSR_1()` one-line
   wrappers), pushes the return address, and jumps. RSR pops it back. Does
   not touch AP.
2. **CALL / RET** (opcodes `0x49` and `0xE2`/`0xE3`) — a VAX-style pair
   that additionally links the AP (Argument Pointer) register into a stack
   frame: CALL pushes the *old* AP, sets AP to a second operand (probably
   the new frame's argument-block address), then pushes the return
   address. RET pops PC, then pops AP back, then skips a caller-specified
   number of extra bytes (its one operand, read as a plain value) — a
   `RET n`-style callee-cleanup convention.

**JMP** (`0xD6`/`0xD7`) is a third, related instruction: same single-operand
address decode as JSR, no push, just jumps. All three (JMP/JSR/RSR) are
marked `/* TRUSTED */` in the reference source.

**All four of JMP, JSR, RSR, and RET were implemented first** (RET was easy
to add once JSR's operand-decode was in place — it just decodes a
`Long`-sized general operand as a *value* via the already-existing
`decode_general_operand` + `read_operand` pair, at PC+1, no instflags byte,
matching the pattern already confirmed for JMP/JSR).

**CALL itself needed a second, dedicated decode path** — confirmed against
the reference's `opCALL` (`F12DecodeOperands(&ReadAMAddress, 0,
&ReadAMAddress, 2)`): both operands are addresses, and *neither* is read as
a value, which doesn't fit `decode_format12`'s "operand 1 is always read"
assumption. Added `decode_format12_raw`, returning both operands as raw
`Operand`s.

That surfaced a real design consequence, not just a decode-shape mismatch:
**CALL's operands can never legitimately be a bare register** (there's no
meaning for "jump to this register's index number" or "set AP to this
register's index number", exactly like JMP/JSR's rejection of a register
target) — but the "one general, one short" encoding `decode_format12_raw`
started with *always* produces a bare register for whichever operand is
short-form. That combination would make CALL **permanently unusable**: no
real encoding could ever avoid hitting the rejected case on one side or the
other. So `decode_format12_raw` also had to implement the "both operands
general" case (instflags bit 7) — confirmed against the reference's
`F12DecodeOperands` bit-7 branch: op2's modifier byte sits immediately
after op1's encoded length, and op2's modm comes from instflags bit 5 in
this branch specifically, not bit 6 (which is op1's modm here — a
different bit gets reused for a different purpose depending on which
branch of the same byte you're in). `decode_format12` itself (used by every
value-reading instruction so far) still doesn't support bit 7 — this was
added only where CALL's own correctness required it, not generally.

Validated against the real oracle: CALL setting a new AP and jumping, and
RET restoring the old AP, both confirmed exactly (27/27 current oracle
total) — including catching a test-expectation bug of our own along the
way (the return address is computed from CALL's *raw*, unmasked PC, same
distinction documented for JSR/JMP above; an early version of the oracle
test used the masked reset vector instead and mismatched by exactly the
mask difference — the implementation was right, the test's arithmetic
wasn't).

A real hardware invariant carried over faithfully: JMP/JSR's target
**cannot be a bare register** (the reference asserts this) — a register
holds a value, not a memory address to jump to. We throw
`UnimplementedAddressingMode` instead of asserting, since a real ROM could
in principle (incorrectly, or via encoding we haven't seen) hit this path
and we'd rather find out than silently do something undefined.

## Validated against the real reference implementation, not just read against it

Everything above this point was validated by *reading* MAME's source
carefully — real, but still just one person's understanding of source code,
which is exactly how the two bugs earlier in this file happened. To get a
stronger check, `tools/oracle-harness/` (in this repo) builds a minimal
standalone MAME driver — just a real `v60_device` plus RAM, no Model 1
specifics — that runs a test program and dumps register state, and
`compare_v60.py` drives it with small hand-written V60 programs and checks
the results against `tests/unit/v60_test.cpp`'s same expectations. No game
ROM is needed for this — see that directory's README for exact setup.

**Result: 8/8 test programs matched the real reference core exactly**,
covering MOVB (partial-register writes), CMPB (carry/zero flags), ADDB/SUBB
(arithmetic, byte wraparound), BE8 (both branch outcomes, verified via a
register side effect), and JSR (jump + landing verified via a register side
effect). This is the strongest validation this core has had so far — real
runtime behavior, not just careful reading.

Building this harness surfaced two more real hardware facts, discovered by
debugging *why* the first two hand-built test programs gave nonsensical
results before the methodology was fixed:

- **This V60 configuration's address bus is masked to 24 bits** — confirmed
  independently two ways: MAME's own address-map validation rejects a
  RAM region placed above `0xFFFFFF`, and this matches the NEC datasheet
  comment already quoted earlier in this file ("Address bus: 24 bits").
  Concretely, the class-default reset vector `0xFFFFFFF0` actually resolves
  to `0x00FFFFF0` once bus masking is applied.
- **The PC register itself displays the raw, unmasked value, even though
  bus accesses are masked.** After executing a 3-byte instruction starting
  at the reset vector, the harness's dumped PC read back as `0xFFFFFFF3` —
  the *unmasked* arithmetic result — not `0x00FFFFF3`. Only when that PC
  value is actually *used* to fetch the next instruction does the 24-bit
  mask apply. This is a real, confirmed hardware/implementation behavior,
  not a bug in the harness — worth remembering if our own core ever needs
  to model address masking, since PC and "the address actually placed on
  the bus" are not always the same number.
- **A methodology lesson, not a hardware fact**: don't compare final PC
  after letting a test program "fall through" past its interesting part —
  `HALT` doesn't actually halt (see the README's gotcha section), so
  execution drifts through memory for whatever time budget you gave it,
  and the exact final PC becomes a function of timing, not of the CPU
  behavior under test. Give every test program an explicit stopping point
  (an infinite self-branch) and check behavior via register side effects
  instead.

## Immediate/literal addressing: "Group 7," and a real per-instruction gotcha

Registers and memory aren't enough — real code needs to load constants.
Confirmed against `am1.hxx`'s 32-entry `s_AMTable1_G7` sub-table (reached
when a general operand's modm=0 and top-3-bits=7, i.e. modifier byte
`0xE0`-`0xFF`, sub-decoded by the *same byte's* low 5 bits):

- **Sub-indices 0-15** (modifier `0xE0`-`0xEF`) are **"immediate quick"** —
  the constant *is* those same low 4 bits, 0-15, with **no extra bytes at
  all**. The single most space-efficient way to encode the small integer
  literals (loop bounds, flags, small offsets) that dominate real code.
- **Sub-index 20** (modifier `0xF4`) is a **full-width immediate** — a
  literal of whatever size `dim` says (1/2/4 bytes) follows the modifier
  byte directly.
- The remaining sub-indices (16-19, 21-31) are PC-relative addressing, an
  absolute "direct address" mode, and their indirect/double-displacement
  variants — not yet implemented (see the deferred list below).
- Confirmed **immediate operands are read-only** by construction (there is
  no real hardware meaning for "write to a literal") — our `Operand` type
  now has a third kind (`Immediate`, alongside `Register`/`Memory`), and
  `write_operand` throws if one is ever used as a destination.
- **Confirmed valid, and genuinely useful**: `am2Immediate`/
  `am2ImmediateQuick` (the `ReadAMAddress` variants, used by JMP/JSR/CALL)
  delegate straight to the same value-producing functions — meaning "jump
  to this literal address" is a real, meaningful encoding, not an error.
  Only a bare register target is actually invalid for JMP/JSR.
- **Real per-instruction gotcha, caught by our own test suite** (not by
  the oracle this time — by just trying it): **JMP/JSR hardcode a
  Byte-sized (`dim=0`) operand decode**, confirmed against the reference's
  `opJMP`/`opJSR` (`m_moddim = 0;`, unconditionally, regardless of what
  addressing mode is actually used). This means a "full immediate" jump
  target through this path can only ever be **0-255** — real code wanting
  to encode an arbitrary 32-bit absolute jump target would need Group 7's
  separate "direct address" sub-mode (index 19, not yet implemented), not
  the general immediate. An earlier version of our test suite assumed a
  4-byte address here and failed against our *own* core — correctly, since
  our core faithfully reproduces this real hardware limitation. The fix
  was to correct the test's expectation, not the code.

Validated against the real oracle too (`tools/oracle-harness/`): both
immediate-quick and full-immediate MOV, and JMP through an immediate, all
match the reference core exactly (11/11 current total).

## AND/OR/XOR/NOT: same shapes as before, one new flag-handling gotcha

Confirmed against `op12.hxx`'s `opANDB`/`opORB`/`opXORB`/`opNOTB` (all
marked "TRUSTED"):

- **AND/OR/XOR** (`0xA0`/`0x88`/`0xB0` for the byte forms, `+2`/`+4` for
  H/W) are read-modify-write on operand 2 — the exact same shape as ADD/SUB,
  so `op_and`/`op_or`/`op_xor` needed zero new decode work, just the
  bitwise operator swapped in.
- **NOT** (`0x38`/`0x3A`/`0x3C`) is, despite sounding unary, a genuine
  two-operand instruction: operand 1 is read, its complement is computed,
  and the result is written to operand 2 — the same shape as MOV.
- **Flag gotcha, confirmed by reading the macros directly rather than
  assuming**: all four **clear overflow** and **set sign/zero from the
  result**, exactly like ADD/SUB/CMP — but **carry is left completely
  untouched**, not even cleared. Every flag-setting instruction implemented
  before this one (ADD, SUB, CMP) *does* set carry, making "leave it alone"
  the one that's easy to get wrong by pattern-matching against those. Test
  coverage specifically establishes a known carry state via a prior CMP,
  then confirms AND leaves it exactly as it found it (`tests/unit/v60_test.cpp`
  and `tools/oracle-harness/compare_v60.py`, both passing against the real
  reference core — 15/15 current oracle total).

## PUSH/POP: another "no new decode work" instruction, plus one ordering subtlety

Confirmed against `op3.hxx`'s `opPUSH`/`opPOP`. Both decode a single
operand directly at PC+1 — same no-instflags shape as JMP/JSR/RET — so
`op_push`/`op_pop` needed no new addressing-mode code, just wiring up
`decode_general_operand` + `read_operand`/`write_operand` again:

- **PUSH** (`0xEE`/`0xEF`) reads the operand as a value and pushes it
  (`SP -= 4; write32(SP, value)`).
- **POP** (`0xE6`/`0xE7`) pops a value (`value = read32(SP); SP += 4`) and
  writes it to the operand — unlike JMP/JSR, POP's operand *is* a valid
  write target (register or memory), decoded the normal way.
- **Both hardcode a Long-sized (`dim=2`) operand decode**, confirmed via
  `m_moddim = 2` in the reference — same pattern already seen for JMP/JSR's
  hardcoded Byte size, just a different fixed width. A `PUSH` of an
  immediate-quick literal (nominally a 4-bit value) still pushes the full
  32-bit value, not something byte-truncated; tested explicitly.
- **Neither touches flags** — confirmed by their absence from `_S`/`_Z`/
  `_OV`/`_CY` in both functions.
- **Ordering subtlety, applied even though it's usually invisible**: POP's
  reference implementation pops the stack value *before* decoding the
  destination operand's address. This only matters if the destination
  operand is itself SP-relative (e.g. an indirect/displacement mode using
  SP as the base register) — a genuinely obscure case — but matching the
  real order costs nothing, so `op_pop` does the same rather than
  reordering for convenience and hoping it never matters.

Validated against the real oracle: PUSH+POP round-tripping a value through
the stack, and PUSH of an immediate, both match exactly (17/17 current
oracle total).

## INC/DEC: single-operand, but full ADD/SUB-style flags (unlike AND/OR/XOR/NOT)

Confirmed against `op3.hxx`'s `opINCB`/`opDECB` (`0xD8`-`0xDD` for INC,
`0xD0`-`0xD5` for DEC, B/H/W × modm-0/1 pairs): a single general operand,
decoded the same no-instflags way as JMP/JSR/PUSH/POP, read-modify-write by
exactly 1. The flags are the interesting part: **INC/DEC compute their
result via the literal same `ADDB`/`SUBB` macros `opADDB`/`opSUBB` use**
(with the other operand hardcoded to 1) — meaning they set the *full*
arithmetic flag set, **including carry**, unlike AND/OR/XOR/NOT (which
leave carry untouched, see above). Two adjacent instructions in this ISA
handle carry in opposite ways depending on whether they're arithmetic or
logical in nature — worth remembering as more instructions get added.

Also worth noting: a test written for this feature initially used the
*wrong* opcode variant for a register-indirect case (`INCB_1`/modm=1 with a
mode-index-3 modifier, which under modm=1 is register-*direct*, not
indirect — the same modm/index distinction documented earlier in this
file). The test's own assertion caught it immediately (the "address"
register itself changed value, which register-indirect should never do).
Fixed by using `INCB_0`/modm=0 instead. Not a new class of bug — the exact
same modm confusion as before, just in a test this time instead of the
implementation, which is exactly why keeping that gotcha table above handy
matters as coverage grows.

Validated against the real oracle: INCB's carry-on-overflow, and DECB
through register-indirect (confirmed the address register itself is left
untouched), both match exactly (19/19 current oracle total).

## SHL: a signed shift count, and a real "caught before shipping" bug

Confirmed against `op12.hxx`'s `opSHLB`/`opSHLH`/`opSHLW` (`0xA9`/`0xAB`/
`0xAD`, all "TRUSTED"). The V60's shift instructions use a compact
VAX-style encoding: operand 1 is a **signed** count — positive shifts
left, negative shifts right, zero recomputes flags without changing the
value — and **both directions of SHL are logical** (zero-fill; there's a
separate instruction, SHA, for arithmetic/sign-preserving right shift,
still not implemented — see the deferred list). Carry gets the last bit
shifted out; overflow is always cleared regardless of direction.

**Real gotcha, caught by re-reading the reference before writing tests
rather than after shipping a bug**: the count operand (op1) is decoded as
**Byte-sized unconditionally**, even in `opSHLH`/`opSHLW` — confirmed by
the literal `F12DecodeOperands(&ReadAM, 0, &ReadAMAddress, dim)` call,
where that first `0` is not a typo mirroring `dim`, it's really hardcoded.
This is specific to the shift instructions; ADD/SUB/AND/OR/XOR use matching
dims for both operands (separately confirmed). A first draft of
`op_shl` passed the instruction's own `dim` for both operands — wrong for
SHLH/SHLW's count decode — caught by grepping for the exact
`F12DecodeOperands` call before writing any test, not by a failing test
after the fact. Worth internalizing as a pattern: **when adding any new
instruction, check whether the reference hardcodes a dim for one operand
independent of the instruction's suffix**, rather than assuming operands
always share the instruction's declared width.

Also handled explicitly, since even the reference's own comment flags it
as uncertain: shifting by a count `>=` the operand's bit width. We define
this as an all-bits-shifted-out zero result with no carry — the most
natural reading — but this is our own choice, not confirmed against real
silicon, exactly because the reference itself doesn't commit to one either.

Validated against the real oracle: positive-count left shift with carry,
negative-count logical (not arithmetic) right shift, and the byte-sized
count applying even to the 16-bit form, all match exactly (22/22 current
oracle total).

## SHA: a genuinely surprising quirk, derived from the formula and then confirmed against real execution

Confirmed against `op12.hxx`'s `opSHAB`/`opSHAH`/`opSHAW` (`0xB9`/`0xBB`/
`0xBD`) and the `SHIFTLEFT_OV`/`SHIFTLEFT_CY`/`SHIFTARITHMETICRIGHT_OV`/
`SHIFTARITHMETICRIGHT_CY` macros. SHA shares SHL's signed-count encoding
(and the same Byte-sized-count-regardless-of-dim gotcha) but differs in
two ways:

- **Right shift (negative count) is arithmetic**, not logical — the sign
  bit is replicated, not zero-filled. `0x81` (`-127` as a signed byte)
  shifted right by 1 gives `0xC0` here, vs. SHL's `0x40` for the identical
  input.
- **Left shift (positive count) computes a genuine overflow flag** instead
  of always clearing it — did any bit shifted past the sign position
  disagree with what the final sign implies (the `SHIFTLEFT_OV` formula:
  mask the top `count` bits, compare against all-sign).

**A real, non-obvious quirk, worth the full story of how it was found**:
working through `SHIFTLEFT_OV`'s mask arithmetic by hand for a **1-bit**
left shift shows the mask covers *only* the single bit already used to
decide which branch of the formula runs (the original sign bit itself) —
so the comparison is tautological and **overflow can never be true for a
1-bit left shift**, regardless of whether the shifted value's sign
actually changes. `0x40` (positive) shifting to `0x80` (negative) — a case
that obviously *should* overflow by any intuitive definition — reports no
overflow at all. This was derived by hand from the macro before writing
any test (not observed by trial and error), then a test was written
specifically to check whether that derivation was right, and it was:
**confirmed against the real MAME reference core**, not just internally
consistent with our own reading. Shift counts of 2 or more use a wider
mask and detect overflow correctly. If a real game's code ever relies on
SHA's overflow flag after a 1-bit shift expecting it to reflect a sign
change, this quirk — not a bug in our emulation — is why it won't.

Also confirmed (matching, not contrasting with, SHL): right-shifting by
the operand's full width or more is a case the reference commits to a
real answer for (unlike SHL's analogous case, which it leaves undefined)
— the result becomes fully sign-extended (`0xFF` or `0x00`), not zero.

Validated against the real oracle, including the overflow quirk itself
(observed indirectly via a `BV8` branch into one of two marker blocks,
the same technique proven for CMPB+BE8): 26/26 current oracle total.

## MUL/DIV: a real quirk carried over from copy-pasted overflow logic, and two "no trap" behaviors

Confirmed against `op12.hxx`'s `opMULB`/`opMULUB`/`opDIVB`/`opDIVUB` (and
H/W forms) — `MUL`=`0x81`/`0x83`/`0x85`, `MULU`=`0x91`/`0x93`/`0x95`,
`DIV`=`0xA1`/`0xA3`/`0xA5`, `DIVU`=`0xB1`/`0xB3`/`0xB5`. All four are
read-modify-write on operand 2, the same shape as ADD/SUB, so no new
decode work was needed.

**Real quirk, replicated exactly rather than corrected**: MUL's (signed)
overflow check is the literal same "are any bits above the destination
width set" test that MULU (unsigned) uses — computed on the full-width
product without re-checking that a sign-extended negative result's upper
bits are *supposed* to be set. The practical effect: MUL's overflow flag
fires for **almost any negative result**, even ones that fit the
destination perfectly (`-5` in a byte, confirmed both in unit tests and
against the real oracle). This looks like the unsigned overflow check was
reused for the signed instruction without accounting for sign extension —
whether that's a genuine silicon quirk or just how this particular
software reference behaves, we replicate it exactly, since matching
confirmed behavior is the point, not "fixing" something that isn't
provably broken.

**Two "doesn't trap" behaviors for DIV/DIVU**, both confirmed directly:
- **Division by zero is a silent no-op** — the destination is left
  completely unchanged (sign/zero flags still get recomputed from that
  unchanged value), not a crash, not a sentinel value.
- **DIV (signed) specifically detects `INT_MIN / -1`** (the one signed
  division whose true result doesn't fit back in the same width) and
  **skips the division** for that case too, flagging overflow instead of
  computing a wrapped/garbage result. DIVU (unsigned) has no equivalent
  case and always clears overflow.

`DIVX` (opcode `0xA6`, a wider-dividend variant per the file's own header
comment: "the second operand should be treated as dword instead of word")
is not yet implemented.

Validated against the real oracle, including the overflow quirk itself:
32/32 current oracle total.

## ROT/ROTC: a stale comment, and a genuine (bits+1)-wide rotate

Confirmed against `op12.hxx`'s `opROTB`/`opROTCB` (and H/W forms) —
`ROT`=`0x89`/`0x8B`/`0x8D`, `ROTC`=`0x99`/`0x9B`/`0x9D`. Same signed-count
encoding as SHL/SHA (positive=left, negative=right, count always
Byte-sized regardless of the operand's own width — confirmed again here,
consistent with the same gotcha already documented for SHL/SHA).

**A stale comment, worth flagging so it doesn't mislead anyone reading the
reference source directly**: `op12.hxx`'s own top-of-file comment lists
`ROTC` under "Unimplemented opcodes" — but `opROTCB`/`H`/`W` are all
present, complete, and marked `/* TRUSTED */`. Comments drift from code
over a project's lifetime; this is a reminder to verify against the actual
function body, not just a nearby comment, even in a well-maintained
reference.

**ROT and ROTC are genuinely different operations, not variations on a
theme**: ROT rotates only the operand's own bits (a mod-`bits` ring). ROTC
rotates *through* the carry flag — a `(bits+1)`-wide ring where carry is an
extra bit alongside the operand. Concretely: for a left rotation, ROT
shifts in whatever bit just fell off the top (so the value's own bits
simply cycle); ROTC instead shifts in the *previous* carry value, and only
afterward does the bit that fell off become the *new* carry. The same
input (`0x81` rotated left by 1) gives `0x03` under ROT but `0x02` under
ROTC (confirmed identical result both in unit tests and against the real
oracle) — a real, non-obvious difference between what look like closely
related mnemonics.

Implemented as a direct per-bit loop mirroring the reference's own loop
structure (not a derived closed-form shortcut) — correctness over
cleverness, consistent with this project's interpreter-first approach; the
loop runs at most 127 times (the largest representable signed count) per
instruction, negligible for an interpreter regardless of how hot the path
gets later.

Validated against the real oracle: 34/34 current oracle total.

## PUSHM/POPM: bit 31 is repurposed for PSW, and a real read-width asymmetry

Confirmed against `op3.hxx`'s `opPUSHM`/`opPOPM` — `PUSHM`=`0xEC`/`0xED`,
`POPM`=`0xE4`/`0xE5`. Same no-instflags single-operand shape as PUSH/POP,
but the operand is read as a **bitmask** rather than a value to move
directly.

- **Bit 31 selects PSW**, not register 31 (SP) — there is no way to
  include SP itself in a PUSHM/POPM list, which makes sense (SP is what's
  doing the pushing/popping). **Bits 0-30 select registers 0-30**, i.e.
  R0-R28, AP, and FP in this project's `Reg` numbering.
- **PUSHM pushes PSW first** (if selected), **then registers in
  descending order** (30 down to 0) — confirmed by the reference's loop
  order. **POPM pops registers in ascending order** (0 up to 30) **first,
  then PSW last** — the mirror image, matching how PUSHM laid them out
  (the register pushed *last* by PUSHM sits at the current SP and is
  popped *first*).
- **A real, confirmed asymmetry**: PUSHM writes PSW as a full 32-bit dword
  (`write_dword_unaligned`), but POPM reads only the **low 16 bits** back
  (`read_word_unaligned`) — while still advancing SP by a full 4 bytes to
  match the push. The current PSW's upper 16 bits are preserved rather
  than zeroed. Replicated exactly. (This asymmetry has no test-observable
  effect in this core specifically, since only bits 0-3 of PSW are modeled
  at all — see below — so a 16-bit vs. 32-bit read can't actually produce
  different flag outcomes here; it's implemented faithfully regardless,
  since a future game or a wider PSW model could depend on it.)

**PSW itself is modeled minimally, matching this project's stated scope**:
only the four flag bits (confirmed against the reference's
`v60ReadPSW`/`v60WritePSW`: bit 0 = zero, bit 1 = sign, bit 2 = overflow,
bit 3 = carry). Everything above bit 3 (privilege level, interrupt state,
...) is out of scope for a game-ROM interpreter and always reads as 0 here
— consistent with this file's earlier note on system/privileged registers.

Validated against the real oracle: descending push order and a full
PUSHM+POPM round-trip (with an explicit clobber in between to prove
restoration actually happened, not just that nothing touched the
registers) both match exactly (36/36 current oracle total).

## Displacement-16/32: the addressing-mode table's remaining gaps closed easily

Confirmed against `am1.hxx`/`am3.hxx`'s `Displacement16`/`Displacement32`
functions: exactly the same shape as the already-implemented
Displacement-8 (`[reg + displacement]`), just a wider displacement field —
16-bit sign-extended, or a full 32-bit value that needs no sign extension
since it's already the whole width. Slotted directly into the existing
`decode_general_operand`/`Operand` machinery as two more `case`s (modm=0,
top-3-bits 1 and 2) — no new concepts needed, unlike almost everything
implemented since the initial addressing-mode table correction.

This closes the practical addressing-range gap that mattered most: 8-bit
displacement caps offsets at -128..127, too small for many real struct
or stack-frame field accesses; every instruction that already uses general
operands (MOV, CMP, ADD, SUB, and everything else built on
`decode_general_operand`) can now reach a full 32-bit offset from a base
register, not just a one-byte-instruction addition.

One test-writing lesson from this increment, not a CPU bug: two new unit
tests initially collided with either the loaded program bytes (writing
test data to address 0, which is also where the instructions themselves
live) or the default `TestBus`'s size (writing past its 4KB bound) —
caught immediately by the test framework itself (a wrong value read back,
and a `vector` out-of-range exception respectively), fixed by choosing
addresses that don't overlap the program or exceed the bus size.

Validated against the real oracle via a write-then-read round-trip through
a displacement-16 address (the harness has no way to pre-seed arbitrary
memory directly, so round-tripping is how it's checked): 37/37 current
oracle total.

## Scope deliberately deferred (not yet implemented)

- Displacement-indirect (8/16/32), double-displacement, "Group 6"
  (register-relative extended encoding), PC-relative addressing, absolute
  "direct address," and bit-string/bit-field modes — each throws
  `UnimplementedAddressingMode` for now. (Displacement-8/16/32 and
  immediate literals — Group 7's quick and full-width sub-modes — *are*
  implemented; see above.)
- The "both operands general" case (instflags bit 7 set) for
  `decode_format12` (value-reading instructions: MOV/CMP/ADD/SUB/AND/OR/
  XOR/NOT/SHL/SHA) — still throws `UnimplementedAddressingMode` there.
  `decode_format12_raw` (used only by CALL so far) *does* support it — see
  the call/return section above.
- Everything else in the ~200-opcode instruction set beyond
  HALT/NOP/MOV/CMP/ADD/SUB/AND/OR/XOR/NOT/PUSH/POP/PUSHM/POPM/INC/DEC/SHL/
  SHA/MUL/MULU/DIV/DIVU/ROT/ROTC/CALL/the 15 conditional branches/
  JMP/JSR/RSR/RET — **DIVX** specifically (see the MUL/DIV section above),
  string/bit-field instructions, and PREPARE (stack-frame setup for CALL's
  convention), interrupts and system/privileged instructions.
- Interrupts, exceptions, privilege levels, the system/control register
  bank, memory protection/paging — all likely irrelevant to a game ROM
  running in whatever mode the boot code sets up, but not yet confirmed;
  revisit if Phase 2 bring-up shows a target game actually uses one.

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

## Two parallel call/return conventions -- confirmed both exist, only implemented the simpler one fully

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

Implemented so far: **JMP, JSR, RSR, and RET** (RET was easy to add once
JSR's operand-decode was in place — it just decodes a `Long`-sized general
operand as a *value* via the already-existing `decode_general_operand` +
`read_operand` pair, at PC+1, no instflags byte, matching the pattern
already confirmed for JMP/JSR). **CALL itself is deliberately not yet
implemented** — it needs an instflags-based two-operand decode where
*neither* operand is read as a value (op1 is a jump address, op2 is a raw
address value assigned into AP), which doesn't fit `decode_format12`'s
current assumption that operand 1 is always read. Revisit CALL specifically
if real ROM code turns out to use it — JSR/RSR may be the more common
convention a C-like compiler targeting this chip would actually emit, but
that's an assumption, not yet confirmed against real disassembly.

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

## Scope deliberately deferred (not yet implemented)

- Displacement-16/32, displacement-indirect (8/16/32), double-displacement,
  "Group 6"/"Group 7" sub-decoded modes, bit-string/bit-field modes,
  immediate literals — each throws `UnimplementedAddressingMode` for now.
- The "both operands general" case (instflags bit 7 set) — throws
  `UnimplementedAddressingMode` until the general addressing-mode table
  exists for both operands simultaneously (op2's modifier byte position
  depends on op1's encoded length).
- **CALL** (`0x49`) — see the call/return section above for why; JMP, JSR,
  RSR, and RET are implemented, CALL specifically is not.
- Everything else in the ~200-opcode instruction set beyond
  HALT/NOP/MOV/CMP/ADD/SUB/the 15 conditional branches/JMP/JSR/RSR/RET —
  multiply/divide, logical ops, shifts, string/bit-field instructions,
  the stack-frame-setup instructions (PUSH/POP/PUSHM/PREPARE), interrupts
  and system/privileged instructions.
- ADD/SUB and most of the rest of the ISA — deferred because they use a
  read-modify-write addressing path (`ReadAMAddress` / `F12LOADOP2*` /
  `F12STOREOP2*` in the reference) that's more involved than MOV/CMP's
  plain read-or-write; not yet traced in enough depth to implement
  correctly. Next concrete step for Phase 1.
- Interrupts, exceptions, privilege levels, the system/control register
  bank, memory protection/paging — all likely irrelevant to a game ROM
  running in whatever mode the boot code sets up, but not yet confirmed;
  revisit if Phase 2 bring-up shows a target game actually uses one.

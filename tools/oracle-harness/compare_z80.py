#!/usr/bin/env python3
"""Run small Z80 test programs through the real MAME z80_device core and
check the resulting register state against expected values -- a trace-
comparison check of sega-model-1-emu's own original src/cpu/z80/ core
against the reference implementation, without needing any game ROM.

See tools/oracle-harness/README.md for setup (this needs a local MAME
checkout with z80test.cpp built in -- it is not run as part of the normal
CMake build).

Set MAME_DIR to your MAME checkout before running, e.g.:
    MAME_DIR=/path/to/mame python3 tools/oracle-harness/compare_z80.py
"""
import struct
import subprocess
import sys
import os

MAME_DIR = os.environ.get("MAME_DIR")
if not MAME_DIR:
    print("Set MAME_DIR to a local MAME checkout with z80test built in "
          "(see tools/oracle-harness/README.md).", file=sys.stderr)
    sys.exit(2)

IN_PATH = os.path.join(MAME_DIR, "z80harness_in.bin")
OUT_PATH = os.path.join(MAME_DIR, "z80harness_out.bin")

# Flag bits, matching src/cpu/z80/z80.h's Flag enum (standard Z80 layout).
C, N, PV, X, H, Y, Z, S = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80

REG8 = ["A", "F", "B", "C", "D", "E", "H", "L"]
REG16 = ["IX", "IY", "SP"]


def run(name, program: bytes, regs: dict, expect: dict) -> bool:
    r8 = {k: 0 for k in REG8}
    r16 = {k: 0 for k in REG16}
    for k, v in regs.items():
        if k in r8:
            r8[k] = v & 0xff
        elif k in r16:
            r16[k] = v & 0xffff
        else:
            raise KeyError(f"unknown register {k}")

    with open(IN_PATH, "wb") as f:
        f.write(struct.pack("<I", len(program)))
        for k in REG8:
            f.write(struct.pack("<B", r8[k]))
        for k in REG16:
            f.write(struct.pack("<H", r16[k]))
        f.write(program)

    if os.path.exists(OUT_PATH):
        os.remove(OUT_PATH)

    subprocess.run(
        [os.path.join(MAME_DIR, "mame"), "z80test",
         "-video", "none", "-sound", "none", "-skip_gameinfo",
         "-window", "-nomaximize",
         "-nothrottle", "-seconds_to_run", "1"],
        cwd=MAME_DIR, env={**os.environ, "Z80TEST_IN": IN_PATH, "Z80TEST_OUT": OUT_PATH},
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30,
    )

    with open(OUT_PATH, "rb") as f:
        data = f.read()
    out8 = struct.unpack("<8B", data[:8])
    out_a, out_f, out_b, out_c, out_d, out_e, out_h, out_l = out8
    out_ix, out_iy, out_sp, out_pc = struct.unpack("<4H", data[8:16])

    out = {
        "A": out_a, "F": out_f, "B": out_b, "C": out_c, "D": out_d, "E": out_e,
        "H": out_h, "L": out_l, "IX": out_ix, "IY": out_iy, "SP": out_sp, "PC": out_pc,
    }

    ok = True
    for k, want in expect.items():
        # F is checked as "these specific bits must be set" (F_set) and/or
        # "these specific bits must be clear" (F_clear), never as full-byte
        # equality -- the undocumented Y/X bits (and any flag a given test
        # doesn't care about) would otherwise have to be predicted exactly
        # by hand for every single test, which isn't the point here.
        if k == "F_set":
            match = (out["F"] & want) == want
            mark = "OK" if match else "MISMATCH"
            print(f"  [{mark}] F bits set: expected 0x{want:02x} all set, got F=0x{out['F']:02x}")
        elif k == "F_clear":
            match = (out["F"] & want) == 0
            mark = "OK" if match else "MISMATCH"
            print(f"  [{mark}] F bits clear: expected 0x{want:02x} all clear, got F=0x{out['F']:02x}")
        else:
            got = out[k]
            mask = 0xff if k in r8 else 0xffff
            match = (got & mask) == (want & mask)
            mark = "OK" if match else "MISMATCH"
            width = 2 if mask == 0xff else 4
            print(f"  [{mark}] {k}: expected 0x{want & mask:0{width}x}, got 0x{got:0{width}x}")
        ok = ok and match

    print(f"{'PASS' if ok else 'FAIL'}: {name}")
    return ok


def main() -> int:
    results = []

    results.append(run(
        "LD r,r' copies between 8-bit registers",
        bytes([0x41, 0x76]),  # LD B,C ; HALT
        {"C": 0x42},
        {"B": 0x42},
    ))

    results.append(run(
        "LD (HL),n then LD A,(HL) round-trips through memory",
        bytes([0x21, 0x00, 0x20, 0x36, 0x42, 0x7e, 0x76]),  # LD HL,0x2000 ; LD (HL),0x42 ; LD A,(HL) ; HALT
        {},
        {"A": 0x42},
    ))

    results.append(run(
        "0x76 is HALT, not LD (HL),(HL) -- execution actually stops",
        bytes([0x06, 0x05, 0x76, 0x06, 0x0a]),  # LD B,5 ; HALT ; LD B,10 (must never run)
        {},
        {"B": 5},
    ))

    results.append(run(
        "LD dd,nn loads a 16-bit immediate",
        bytes([0x21, 0x34, 0x12, 0x76]),  # LD HL,0x1234 ; HALT
        {},
        {"H": 0x12, "L": 0x34},
    ))

    results.append(run(
        "LD SP,nn uses the dd encoding where 11 means SP, not AF",
        bytes([0x31, 0x00, 0x80, 0x76]),  # LD SP,0x8000 ; HALT
        {},
        {"SP": 0x8000},
    ))

    results.append(run(
        "ADD A,r sets carry/half-carry/zero and clears sign/overflow",
        bytes([0x3e, 0xff, 0x06, 0x01, 0x80, 0x76]),  # LD A,0xff ; LD B,1 ; ADD A,B ; HALT
        {},
        {"A": 0x00, "F_set": C | H | Z, "F_clear": S | PV | N},
    ))

    results.append(run(
        "ADD A,r detects signed overflow (0x7f + 0x01)",
        bytes([0x3e, 0x7f, 0x06, 0x01, 0x80, 0x76]),
        {},
        {"A": 0x80, "F_set": PV | S, "F_clear": C},
    ))

    results.append(run(
        "ADC A,r includes the incoming carry",
        bytes([0x3e, 0x01, 0x0e, 0x01, 0x89, 0x76]),  # LD A,1 ; LD C,1 ; ADC A,C ; HALT
        {"F": C},  # carry-in set via initial state, not an SCF instruction
        {"A": 0x03},
    ))

    results.append(run(
        "SUB r detects borrow and sets N",
        bytes([0x3e, 0x00, 0x06, 0x01, 0x90, 0x76]),  # LD A,0 ; LD B,1 ; SUB B ; HALT
        {},
        {"A": 0xff, "F_set": C | N | S},
    ))

    results.append(run(
        "CP r sets zero but leaves A unchanged",
        bytes([0x3e, 0x05, 0x06, 0x05, 0xb8, 0x76]),  # LD A,5 ; LD B,5 ; CP B ; HALT
        {},
        {"A": 0x05, "F_set": Z},
    ))

    results.append(run(
        "AND sets half-carry always and parity from the result",
        bytes([0x3e, 0xcc, 0x06, 0xaa, 0xa0, 0x76]),  # LD A,0xcc ; LD B,0xaa ; AND B ; HALT
        {},
        {"A": 0x88, "F_set": H | PV, "F_clear": C},
    ))

    results.append(run(
        "Immediate ALU form (ADD A,n) reads from the instruction stream",
        bytes([0x3e, 0x05, 0xc6, 0x10, 0x76]),  # LD A,5 ; ADD A,0x10 ; HALT
        {},
        {"A": 0x15},
    ))

    results.append(run(
        "INC r sets overflow/half-carry/sign and never touches carry",
        bytes([0x3e, 0x7f, 0x3c, 0x76]),  # LD A,0x7f ; INC A ; HALT
        {"F": C},  # carry pre-set via initial state to prove INC leaves it alone
        {"A": 0x80, "F_set": C | PV | H | S, "F_clear": N | Z},
    ))

    results.append(run(
        "DEC r sets N and detects the 0x80 signed-overflow case",
        bytes([0x3e, 0x80, 0x3d, 0x76]),  # LD A,0x80 ; DEC A ; HALT
        {},
        {"A": 0x7f, "F_set": N | PV | H, "F_clear": Z | S},
    ))

    results.append(run(
        "INC dd/DEC dd wrap at 16 bits and affect no flags",
        bytes([0x21, 0xff, 0xff, 0x23, 0x76]),  # LD HL,0xffff ; INC HL ; HALT
        {"F": C},  # only carry set beforehand, to prove INC HL leaves every flag alone
        {"H": 0x00, "L": 0x00, "F_set": C, "F_clear": N | PV | H | Z | S},
    ))

    results.append(run(
        "JP nn jumps unconditionally, skipping the instruction in between",
        bytes([0xc3, 0x05, 0x00, 0x06, 0xaa, 0x76]),  # JP 0x0005 ; LD B,0xaa (skipped) ; HALT
        {},
        {"B": 0x00},
    ))

    results.append(run(
        "JR e jumps relative, skipping the instruction in between",
        bytes([0x18, 0x02, 0x06, 0xaa, 0x76]),  # JR +2 ; LD B,0xaa (skipped) ; HALT
        {},
        {"B": 0x00},
    ))

    jr_cond_prog = bytes([
        0xb7,             # OR A (sets Z from A, doesn't change A)
        0x28, 0x03,       # JR Z,+3 -> target = addr(5)+3 = addr8 if taken
        0x06, 0xaa,       # LD B,0xaa  ; not-taken path
        0x76,             # HALT
        0x06, 0xbb,       # LD B,0xbb  ; taken path
        0x76,             # HALT
    ])
    results.append(run(
        "JR Z,e is taken when the condition holds",
        jr_cond_prog, {"A": 0x00}, {"B": 0xbb},
    ))
    results.append(run(
        "JR Z,e falls through when the condition doesn't hold",
        jr_cond_prog, {"A": 0x05}, {"B": 0xaa},
    ))

    results.append(run(
        "DJNZ loops until B reaches zero",
        bytes([0x06, 0x03, 0x10, 0xfe, 0x76]),  # LD B,3 ; DJNZ -2 (spins on itself) ; HALT
        {},
        {"B": 0x00},
    ))

    call_ret_prog = bytes([
        0xcd, 0x06, 0x00,  # CALL 0x0006
        0x06, 0xaa,        # LD B,0xaa  ; marker: returned here
        0x76,              # HALT
        0x0e, 0xcc,        # LD C,0xcc  ; marker: subroutine entered
        0xc9,              # RET
    ])
    results.append(run(
        "CALL pushes the return address and RET pops it back",
        call_ret_prog,
        {"SP": 0x8000},
        {"B": 0xaa, "C": 0xcc, "SP": 0x8000},
    ))

    call_nz_prog = bytes([
        0xc4, 0x06, 0x00,  # CALL NZ,0x0006
        0x06, 0xaa,        # LD B,0xaa
        0x76,              # HALT
        0x0e, 0xcc,        # LD C,0xcc  ; only reached if the call is taken
        0xc9,              # RET
    ])
    results.append(run(
        "Conditional CALL is taken when the condition holds",
        call_nz_prog,
        {"SP": 0x8000, "F": 0x00},  # Z clear -> NZ true
        {"B": 0xaa, "C": 0xcc},
    ))
    results.append(run(
        "Conditional CALL is skipped when the condition doesn't hold",
        call_nz_prog,
        {"SP": 0x8000, "F": Z},  # Z set -> NZ false
        {"B": 0xaa, "C": 0x00},  # subroutine never entered
    ))

    results.append(run(
        "PUSH/POP qq encoding treats 11 as AF, not SP like dd does",
        bytes([0xf5, 0xe1, 0x76]),  # PUSH AF ; POP HL ; HALT
        {"A": 0x12, "F": 0x34, "SP": 0x8000},
        {"H": 0x12, "L": 0x34, "SP": 0x8000},
    ))

    results.append(run(
        "EX DE,HL swaps the two register pairs",
        bytes([0x11, 0x34, 0x12, 0x21, 0x78, 0x56, 0xeb, 0x76]),  # LD DE,0x1234 ; LD HL,0x5678 ; EX DE,HL ; HALT
        {},
        {"D": 0x56, "E": 0x78, "H": 0x12, "L": 0x34},
    ))

    results.append(run(
        # The harness can't set shadow registers directly, so EXX/EX AF,AF'
        # are checked by swapping known values into the shadow set and back
        # again -- only the PRIMARY set is ever visible in the dump, so
        # getting the original value back is proof the round-trip worked.
        "EXX round-trips BC through the shadow set",
        bytes([
            0x01, 0x11, 0x11,  # LD BC,0x1111
            0xd9,              # EXX -> BC2=0x1111, BC=0 (shadow's reset default)
            0x01, 0x22, 0x22,  # LD BC,0x2222
            0xd9,              # EXX -> BC=0x1111 (restored), BC2=0x2222
            0x76,              # HALT
        ]),
        {},
        {"B": 0x11, "C": 0x11},
    ))

    results.append(run(
        "EX AF,AF' round-trips A through the shadow set",
        bytes([
            0x3e, 0x11,  # LD A,0x11
            0x08,        # EX AF,AF' -> A2=0x11, A=0 (shadow's reset default)
            0x3e, 0x22,  # LD A,0x22
            0x08,        # EX AF,AF' -> A=0x11 (restored), A2=0x22
            0x76,        # HALT
        ]),
        {},
        {"A": 0x11},
    ))

    results.append(run(
        "EX (SP),HL swaps HL with the word at the top of the stack",
        bytes([
            0x21, 0x34, 0x12,  # LD HL,0x1234
            0xe5,              # PUSH HL
            0x21, 0x78, 0x56,  # LD HL,0x5678
            0xe3,              # EX (SP),HL
            0x76,              # HALT
        ]),
        {"SP": 0x8000},
        {"H": 0x12, "L": 0x34},
    ))

    results.append(run(
        "CPL complements A and sets H/N",
        bytes([0x3e, 0xa5, 0x2f, 0x76]),  # LD A,0xa5 ; CPL ; HALT
        {},
        {"A": 0x5a, "F_set": H | N},
    ))

    results.append(run(
        "SCF sets carry and clears H/N, leaving Z untouched",
        bytes([0x37, 0x76]),  # SCF ; HALT
        {"F": H | N | Z},
        {"F_set": C | Z, "F_clear": H | N},
    ))

    results.append(run(
        "CCF moves the old carry into H and inverts carry",
        bytes([0x3f, 0x76]),  # CCF ; HALT
        {"F": C},
        {"F_set": H, "F_clear": C | N},
    ))

    results.append(run(
        "DAA converts a binary BCD addition back into valid BCD (0x15 + 0x27 -> 0x42)",
        bytes([0xc6, 0x27, 0x27, 0x76]),  # ADD A,0x27 ; DAA ; HALT
        {"A": 0x15},
        {"A": 0x42, "F_set": PV | H, "F_clear": C},
    ))

    results.append(run(
        "DAA converts a binary BCD subtraction back into valid BCD (0x42 - 0x27 -> 0x15)",
        bytes([0xd6, 0x27, 0x27, 0x76]),  # SUB 0x27 ; DAA ; HALT
        {"A": 0x42},
        {"A": 0x15, "F_clear": C | H | PV},
    ))

    # SCF/CCF's Y/X approximation ("mirror the current accumulator") vs.
    # the real hardware's undocumented "Q register" mechanism (see z80.h's
    # comment on this) -- empirically confirmed against real MAME rather
    # than left as an untested guess. Case 1: A has both undocumented bits
    # set and the preceding instruction (LD, which never touches flags)
    # gives Q nothing to have captured, so "mirror A" is unambiguously
    # correct here. Case 2 is the interesting one: A is reset to 0 (both
    # bits clear) but the immediately preceding CP's own *result*
    # (0x00-0xc8 = 0x38) has both undocumented bits set -- if real hardware
    # carried over "the last flags-affecting op's result" instead of "the
    # current A", this would show Y/X set. It doesn't: real MAME agrees
    # with the mirror-from-A approximation here too.
    results.append(run(
        "SCF's Y/X bits mirror A when the preceding instruction didn't touch flags",
        bytes([0x3e, 0xff, 0x37, 0x76]),  # LD A,0xff ; SCF ; HALT
        {}, {"F_set": Y | X},
    ))
    results.append(run(
        "SCF's Y/X bits mirror current A, not a preceding CP's own result",
        bytes([0x3e, 0x00, 0xfe, 0xc8, 0x37, 0x76]),  # LD A,0 ; CP 0xc8 ; SCF ; HALT
        {}, {"F_clear": Y | X},
    ))

    results.append(run(
        "RLC rotates left circularly, carry = the old bit 7",
        bytes([0x06, 0x81, 0xcb, 0x00, 0x76]),  # LD B,0x81 ; RLC B ; HALT
        {}, {"B": 0x03, "F_set": C},
    ))

    results.append(run(
        "RRC rotates right circularly, carry = the old bit 0",
        bytes([0x06, 0x81, 0xcb, 0x08, 0x76]),  # LD B,0x81 ; RRC B ; HALT
        {}, {"B": 0xc0, "F_set": C},
    ))

    results.append(run(
        "RL rotates left through carry, not the wrapped bit",
        bytes([0x06, 0x81, 0xcb, 0x10, 0x76]),  # LD B,0x81 ; RL B ; HALT
        {"F": 0x00},  # carry-in clear
        {"B": 0x02, "F_set": C},
    ))

    results.append(run(
        "RR rotates right through carry, not the wrapped bit",
        bytes([0x06, 0x81, 0xcb, 0x18, 0x76]),  # LD B,0x81 ; RR B ; HALT
        {"F": 0x00},
        {"B": 0x40, "F_set": C},
    ))

    results.append(run(
        "SLA shifts left, filling bit 0 with zero",
        bytes([0x06, 0x81, 0xcb, 0x20, 0x76]),  # LD B,0x81 ; SLA B ; HALT
        {}, {"B": 0x02, "F_set": C},
    ))

    results.append(run(
        "SRA shifts right arithmetically, preserving the sign bit",
        bytes([0x06, 0x81, 0xcb, 0x28, 0x76]),  # LD B,0x81 ; SRA B ; HALT
        {}, {"B": 0xc0, "F_set": C},
    ))

    results.append(run(
        "SLL (undocumented) shifts left filling bit 0 with one, unlike SLA",
        bytes([0x06, 0x40, 0xcb, 0x30, 0x76]),  # LD B,0x40 ; SLL B ; HALT
        {}, {"B": 0x81, "F_clear": C},
    ))

    results.append(run(
        "SRL shifts right logically, zero-filling bit 7 unlike SRA",
        bytes([0x06, 0x81, 0xcb, 0x38, 0x76]),  # LD B,0x81 ; SRL B ; HALT
        {}, {"B": 0x40, "F_set": C},
    ))

    results.append(run(
        "BIT b,r sets Z/PV from the tested bit and S only when b=7 and set",
        bytes([0x06, 0x80, 0xcb, 0x78, 0x76]),  # LD B,0x80 ; BIT 7,B ; HALT
        {}, {"F_set": S | H, "F_clear": Z | PV | N},
    ))

    results.append(run(
        "RES b,r clears one bit and leaves the rest alone",
        bytes([0x06, 0xff, 0xcb, 0x98, 0x76]),  # LD B,0xff ; RES 3,B ; HALT
        {}, {"B": 0xf7},
    ))

    results.append(run(
        "SET b,r sets one bit and leaves the rest alone",
        bytes([0x06, 0x00, 0xcb, 0xd8, 0x76]),  # LD B,0 ; SET 3,B ; HALT
        {}, {"B": 0x08},
    ))

    results.append(run(
        # A real, oracle-discovered bug this project's own core initially
        # got wrong: BIT b,(HL)'s Y/X flags come from HL+1's high byte (an
        # internal address-latch artifact), not the tested byte -- see
        # z80.h's comment on this. The tested byte (0x28) has both
        # undocumented bits set; HL+1's high byte (0x1300 -> 0x13) has
        # both clear, so a naive "mirror the operand" implementation gets
        # this backwards.
        "BIT b,(HL) mirrors Y/X from HL+1's high byte, not the tested value",
        bytes([0x21, 0xff, 0x12, 0x36, 0x28, 0xcb, 0x46, 0x76]),
        # LD HL,0x12ff ; LD (HL),0x28 ; BIT 0,(HL) ; HALT
        {}, {"F_clear": Y | X},
    ))

    results.append(run(
        "LD (BC),A / LD A,(BC) round-trip through memory",
        bytes([0x01, 0x00, 0x20, 0x3e, 0x42, 0x02, 0x3e, 0x00, 0x0a, 0x76]),
        # LD BC,0x2000 ; LD A,0x42 ; LD (BC),A ; LD A,0 ; LD A,(BC) ; HALT
        {}, {"A": 0x42},
    ))

    results.append(run(
        "LD (DE),A / LD A,(DE) round-trip through memory",
        bytes([0x11, 0x00, 0x20, 0x3e, 0x42, 0x12, 0x3e, 0x00, 0x1a, 0x76]),
        # LD DE,0x2000 ; LD A,0x42 ; LD (DE),A ; LD A,0 ; LD A,(DE) ; HALT
        {}, {"A": 0x42},
    ))

    results.append(run(
        "LD (nn),HL / LD HL,(nn) round-trip a 16-bit value through memory",
        bytes([0x21, 0x34, 0x12, 0x22, 0x00, 0x30, 0x21, 0x00, 0x00, 0x2a, 0x00, 0x30, 0x76]),
        # LD HL,0x1234 ; LD (0x3000),HL ; LD HL,0 ; LD HL,(0x3000) ; HALT
        {}, {"H": 0x12, "L": 0x34},
    ))

    results.append(run(
        "LD (nn),A / LD A,(nn) round-trip through memory",
        bytes([0x3e, 0x99, 0x32, 0x00, 0x30, 0x3e, 0x00, 0x3a, 0x00, 0x30, 0x76]),
        # LD A,0x99 ; LD (0x3000),A ; LD A,0 ; LD A,(0x3000) ; HALT
        {}, {"A": 0x99},
    ))

    results.append(run(
        "ADD HL,ss sets C/H but leaves S/Z/PV untouched",
        bytes([0x21, 0xff, 0xff, 0x01, 0x01, 0x00, 0x09, 0x76]),
        # LD HL,0xffff ; LD BC,1 ; ADD HL,BC ; HALT
        {"F": S | Z | PV},
        {"H": 0x00, "L": 0x00, "F_set": C | H | S | Z | PV},
    ))

    results.append(run(
        "RLCA sets C from the old bit 7 but leaves S/Z/PV untouched, unlike CB's RLC A",
        bytes([0x3e, 0x80, 0x07, 0x76]),  # LD A,0x80 ; RLCA ; HALT
        {"F": Z | PV},  # pre-set, to prove they survive even though the new A isn't zero
        {"A": 0x01, "F_set": C | Z | PV},
    ))

    results.append(run(
        "RST pushes PC and jumps to the vector encoded in the opcode's own bits",
        bytes([0xef]) + bytes(0x27) + bytes([0x06, 0xaa, 0x76]),
        # RST 28h ; (39 bytes of padding, decoding as harmless NOPs) ;
        # LD B,0xaa ; HALT -- landing pad for the RST at address 0x28
        {"SP": 0x8000},
        {"B": 0xaa, "SP": 0x7ffe},
    ))

    results.append(run(
        "JP (HL) jumps to HL's value, not the memory it points at",
        bytes([0x21, 0x10, 0x00, 0xe9]) + bytes(12) + bytes([0x06, 0xaa, 0x76]),
        # LD HL,0x0010 ; JP (HL) ; padding ; LD B,0xaa ; HALT (at 0x0010)
        {}, {"B": 0xaa},
    ))

    results.append(run(
        "LD SP,HL copies HL into SP",
        bytes([0x21, 0x34, 0x12, 0xf9, 0x76]),  # LD HL,0x1234 ; LD SP,HL ; HALT
        {}, {"SP": 0x1234},
    ))

    results.append(run(
        "ADC HL,ss includes the incoming carry and sets S/Z/PV, unlike ADD HL,ss",
        bytes([0x21, 0xff, 0xff, 0x01, 0x00, 0x00, 0xed, 0x4a, 0x76]),
        # LD HL,0xffff ; LD BC,0 ; ADC HL,BC ; HALT
        {"F": C},
        {"H": 0x00, "L": 0x00, "F_set": C | H | Z, "F_clear": PV},
    ))

    results.append(run(
        "SBC HL,ss detects borrow and sets N/S",
        bytes([0x21, 0x00, 0x00, 0x01, 0x01, 0x00, 0xed, 0x42, 0x76]),
        # LD HL,0 ; LD BC,1 ; SBC HL,BC ; HALT
        {"F": 0x00},
        {"H": 0xff, "L": 0xff, "F_set": C | N | S, "F_clear": PV},
    ))

    results.append(run(
        "ED's LD (nn),dd / LD dd,(nn) round-trip BC through memory",
        bytes([0x01, 0x34, 0x12, 0xed, 0x43, 0x00, 0x30, 0x01, 0x00, 0x00, 0xed, 0x4b, 0x00, 0x30, 0x76]),
        # LD BC,0x1234 ; LD (0x3000),BC ; LD BC,0 ; LD BC,(0x3000) ; HALT
        {}, {"B": 0x12, "C": 0x34},
    ))

    results.append(run(
        "NEG computes 0-A, detecting the 0x80 overflow case",
        bytes([0x3e, 0x80, 0xed, 0x44, 0x76]),  # LD A,0x80 ; NEG ; HALT
        {}, {"A": 0x80, "F_set": PV | C},
    ))

    results.append(run(
        "NEG of 0 has no borrow",
        bytes([0x3e, 0x00, 0xed, 0x44, 0x76]),  # LD A,0 ; NEG ; HALT
        {}, {"A": 0x00, "F_clear": C, "F_set": Z},
    ))

    results.append(run(
        "LD A,I copies IFF2 into PV; LD I,A stores plainly (checked via a later LD A,I)",
        bytes([0x3e, 0x42, 0xed, 0x47, 0x3e, 0x00, 0xed, 0x57, 0x76]),
        # LD A,0x42 ; LD I,A ; LD A,0 ; LD A,I ; HALT
        {}, {"A": 0x42},
    ))

    # No oracle test for "LD R,A then LD A,R round-trips plainly" (unlike
    # the LD A,I/LD I,A version above, which does): real hardware
    # auto-increments R on every M1 (opcode fetch) cycle, including
    # ED-prefixed instructions' own fetch, so a real round-trip through R
    # picks up a few unrelated increments from the instructions executed
    # in between -- confirmed by trying exactly this (A came back 3 higher
    # than stored) before concluding it's not a bug in LD R,A/LD A,R
    # themselves. R's auto-increment isn't implemented in this core (see
    # z80.h's scope comment), so there's no way to write a meaningful
    # oracle test for R specifically until it is; tests/unit/z80_test.cpp
    # covers LD I,A/LD R,A/LD A,I/LD A,R directly instead, unaffected by
    # this since it reads/writes `regs().r` with no hardware to increment it.

    results.append(run(
        "RRD rotates a nibble from (HL) into A and shifts (HL)'s own nibbles",
        bytes([0x21, 0x00, 0x30, 0x36, 0x34, 0x3e, 0x12, 0xed, 0x67, 0x46, 0x76]),
        # LD HL,0x3000 ; LD (HL),0x34 ; LD A,0x12 ; RRD ; LD B,(HL) ; HALT
        {}, {"A": 0x14, "B": 0x23},
    ))

    results.append(run(
        "RLD rotates the other direction from RRD",
        bytes([0x21, 0x00, 0x30, 0x36, 0x34, 0x3e, 0x12, 0xed, 0x6f, 0x46, 0x76]),
        # LD HL,0x3000 ; LD (HL),0x34 ; LD A,0x12 ; RLD ; LD B,(HL) ; HALT
        {}, {"A": 0x13, "B": 0x42},
    ))

    results.append(run(
        "RETN pops PC and restores IFF1 from IFF2 (checked via a landing-pad marker)",
        bytes([0x31, 0x00, 0x80, 0xcd, 0x09, 0x00, 0x06, 0xaa, 0x76, 0xed, 0x45]),
        # LD SP,0x8000 ; CALL 0x0009 ; LD B,0xaa ; HALT ; (0x0009:) RETN
        {}, {"B": 0xaa},
    ))

    results.append(run(
        "LDI copies one byte, steps HL/DE forward, decrements BC, and sets PV from BC",
        bytes([0x21, 0x00, 0x30, 0x36, 0x11, 0x11, 0x00, 0x40, 0x01, 0x02, 0x00, 0xed, 0xa0, 0x76]),
        # LD HL,0x3000 ; LD (HL),0x11 ; LD DE,0x4000 ; LD BC,2 ; LDI ; HALT
        {}, {"H": 0x30, "L": 0x01, "D": 0x40, "E": 0x01, "C": 0x01, "F_set": PV},
    ))

    results.append(run(
        # Source memory content doesn't matter here (and isn't readable
        # back through this harness's register-only dump anyway) -- this
        # checks only the pointer arithmetic and loop-termination
        # behavior, which the unit tests' direct memory access already
        # complements with actual copied-byte verification.
        "LDIR repeats until BC reaches zero, advancing HL/DE by the full count",
        bytes([0x21, 0x00, 0x30, 0x11, 0x00, 0x40, 0x01, 0x03, 0x00, 0xed, 0xb0, 0x76]),
        # LD HL,0x3000 ; LD DE,0x4000 ; LD BC,3 ; LDIR ; HALT
        {}, {"H": 0x30, "L": 0x03, "D": 0x40, "E": 0x03, "C": 0x00, "F_clear": PV},
    ))

    results.append(run(
        "CPI compares against (HL), leaves carry untouched, and sets PV from BC",
        bytes([0x21, 0x00, 0x30, 0x36, 0x08, 0x3e, 0x10, 0x01, 0x02, 0x00, 0x37, 0xed, 0xa1, 0x76]),
        # LD HL,0x3000 ; LD (HL),8 ; LD A,0x10 ; LD BC,2 ; SCF ; CPI ; HALT
        {}, {"L": 0x01, "C": 0x01, "F_set": C | H | N | PV, "F_clear": S | Z},
    ))

    results.append(run(
        # Discriminating test: OUT (n),A to the same n=0x10 with two
        # different A values, then IN A,(n) with A back at the first
        # value. If the port address is really (A<<8)|n (real hardware),
        # the two OUTs land at different addresses (0x2010, 0x3010) and
        # the final IN correctly reads back 0x20. If a buggy
        # implementation ignored A and used n alone as an 8-bit port, both
        # OUTs would collide on "port 0x10" and the IN would read back the
        # second OUT's value (0x30) instead.
        "IN A,(n) / OUT (n),A put A on the port address's upper byte, not just n",
        bytes([0x3e, 0x20, 0xd3, 0x10, 0x3e, 0x30, 0xd3, 0x10, 0x3e, 0x20, 0xdb, 0x10, 0x76]),
        # LD A,0x20 ; OUT (0x10),A ; LD A,0x30 ; OUT (0x10),A ; LD A,0x20 ; IN A,(0x10) ; HALT
        {}, {"A": 0x20},
    ))

    results.append(run(
        # Same discriminating idea as above, but for IN r,(C)/OUT (C),r:
        # the full BC pair is the port address, not zero-extended C.
        "IN r,(C) / OUT (C),r put the full BC pair on the port address, not just C",
        bytes([
            0x01, 0x40, 0x30, 0x3e, 0x11, 0xed, 0x79,  # LD BC,0x3040 ; LD A,0x11 ; OUT (C),A
            0x01, 0x40, 0x50, 0x3e, 0x22, 0xed, 0x79,  # LD BC,0x5040 ; LD A,0x22 ; OUT (C),A
            0x01, 0x40, 0x30, 0xed, 0x78,              # LD BC,0x3040 ; IN A,(C)
            0x76,                                      # HALT
        ]),
        {}, {"A": 0x11, "F_set": PV, "F_clear": S | Z | H | N},
    ))

    print()
    print(f"{sum(results)}/{len(results)} programs matched the MAME z80_device reference exactly.")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

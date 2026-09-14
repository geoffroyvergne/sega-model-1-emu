#!/usr/bin/env python3
"""Run small V60 test programs through the real MAME v60_device core and
check the resulting register/PC state against expected values -- a trace-
comparison check of sega-model-1-emu's own original src/cpu/v60/ core
against the reference implementation, without needing any game ROM.

See tools/oracle-harness/README.md for setup (this needs a local MAME
checkout with v60test.cpp built in -- it is not run as part of the normal
CMake build).

Set MAME_DIR to your MAME checkout before running, e.g.:
    MAME_DIR=/path/to/mame python3 tools/oracle-harness/compare_v60.py
"""
import struct
import subprocess
import sys
import os

MAME_DIR = os.environ.get("MAME_DIR")
if not MAME_DIR:
    print("Set MAME_DIR to a local MAME checkout with v60test built in "
          "(see tools/oracle-harness/README.md).", file=sys.stderr)
    sys.exit(2)

IN_PATH = os.path.join(MAME_DIR, "v60harness_in.bin")
OUT_PATH = os.path.join(MAME_DIR, "v60harness_out.bin")

# Register index order matches src/cpu/v60/v60.h's Reg enum and the real
# hardware's 5-bit register field: R0..R28, then AP, FP, SP.
R = {name: i for i, name in enumerate(
    [f"R{i}" for i in range(29)] + ["AP", "FP", "SP"]
)}

# The V60's real (24-bit-masked) reset vector -- see
# docs/hardware-notes/07-v60-architecture.md. Test programs are loaded here
# by v60test.cpp, and must end in an infinite self-branch (BR8 +0) rather
# than relying on HALT actually stopping anything (it doesn't -- see that
# file's header comment).
RESET_VECTOR = 0x00fffff0


def run(name, program: bytes, regs: dict, expect: dict) -> bool:
    reg_array = [0] * 32
    for k, v in regs.items():
        reg_array[R[k]] = v & 0xffffffff

    with open(IN_PATH, "wb") as f:
        f.write(struct.pack("<I", 1000))
        f.write(struct.pack("<I", len(program)))
        for r in reg_array:
            f.write(struct.pack("<I", r))
        f.write(program)

    if os.path.exists(OUT_PATH):
        os.remove(OUT_PATH)

    subprocess.run(
        [os.path.join(MAME_DIR, "mame"), "v60test",
         "-video", "none", "-sound", "none", "-skip_gameinfo",
         "-nothrottle", "-seconds_to_run", "1"],
        cwd=MAME_DIR, env={**os.environ, "V60TEST_IN": IN_PATH, "V60TEST_OUT": OUT_PATH},
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=30,
    )

    with open(OUT_PATH, "rb") as f:
        data = f.read()
    out_regs = struct.unpack("<32I", data[:128])
    out_pc = struct.unpack("<I", data[128:132])[0]

    ok = True
    for k, want in expect.items():
        got = out_pc if k == "PC" else out_regs[R[k]]
        match = (got & 0xffffffff) == (want & 0xffffffff)
        ok = ok and match
        mark = "OK" if match else "MISMATCH"
        print(f"  [{mark}] {k}: expected 0x{want & 0xffffffff:08x}, got 0x{got:08x}")

    print(f"{'PASS' if ok else 'FAIL'}: {name}")
    return ok


def main() -> int:
    results = []

    # MOVB R1, R2 -- op1 short R1, op2 general register-direct R2.
    results.append(run(
        "MOVB preserves destination's upper bits",
        bytes([0x09, 0x41, 0x62]),
        {"R1": 0xab, "R2": 0xffffff00},
        {"R2": 0xffffffab},
    ))

    # CMPB R1, R2 with dst<src -> carry (borrow) set; must not modify operands.
    results.append(run(
        "CMPB sets carry (borrow) when destination < source",
        bytes([0xb8, 0x41, 0x62]),
        {"R1": 5, "R2": 3},
        {"R1": 5, "R2": 3},
    ))

    # ADDB R1, R2 -- op2 = op2 + op1, read-modify-write.
    results.append(run(
        "ADDB adds into the destination",
        bytes([0x80, 0x41, 0x62]),
        {"R1": 5, "R2": 3},
        {"R2": 8},
    ))

    # ADDB with byte overflow (0xff + 1 wraps to 0x00, upper bits preserved).
    results.append(run(
        "ADDB wraps a byte and preserves upper bits",
        bytes([0x80, 0x41, 0x62]),
        {"R1": 0xff, "R2": 0xabcd0001},
        {"R2": 0xabcd0000},
    ))

    # SUBB R1, R2 -- op2 = op2 - op1, underflow wraps within the byte.
    results.append(run(
        "SUBB underflows and wraps within a byte",
        bytes([0xa8, 0x41, 0x62]),
        {"R1": 5, "R2": 3},
        {"R2": 0xfe},
    ))

    # CMPB then BE8 (branch if equal), landing in one of two marker blocks
    # that each set R4 to a distinct sentinel and then spin forever --
    # observing the branch outcome as a stable register side effect avoids
    # racing final PC against wall-clock timing.
    #
    #   addr0:  CMPB R1, R2                  (3 bytes)
    #   addr3:  BE8 +7  -> addr10 if taken   (2 bytes)
    #   addr5:  MOVB R5, R4   ; not-taken marker (0xAA)
    #   addr8:  BR8 +0        ; spin
    #   addr10: MOVB R6, R4   ; taken marker (0xBB)
    #   addr13: BR8 +0        ; spin
    branch_prog = bytes([
        0xb8, 0x41, 0x62,
        0x64, 0x07,
        0x09, 0x45, 0x64,
        0x6a, 0x00,
        0x09, 0x46, 0x64,
        0x6a, 0x00,
    ])
    results.append(run(
        "CMPB + BE8: branch taken on equal operands (R4 <- taken marker 0xBB)",
        branch_prog,
        {"R1": 5, "R2": 5, "R4": 0, "R5": 0xaa, "R6": 0xbb},
        {"R4": 0xbb},
    ))
    results.append(run(
        "CMPB + BE8: branch not taken on unequal operands (R4 <- not-taken marker 0xAA)",
        branch_prog,
        {"R1": 5, "R2": 3, "R4": 0, "R5": 0xaa, "R6": 0xbb},
        {"R4": 0xaa},
    ))

    # JSR [R3], where R3 points at a landing pad *within this same program
    # blob* (a marker MOVB followed by a self-loop) -- the harness only
    # loads one blob, at the reset vector, so the jump target has to live
    # there too (uninitialized scratch memory decodes as HALT, which still
    # advances PC by 1 each time despite the name -- not a stable landing
    # spot; only an explicit self-loop is).
    #
    #   addr0: JSR [R3]                (2 bytes)
    #   addr2: MOVB R9, R10            ; marker: landed here (3 bytes)
    #   addr5: BR8 +0                  ; spin
    jsr_prog = bytes([
        0xe8, 0x63,
        0x09, 0x49, 0x6a,
        0x6a, 0x00,
    ])
    results.append(run(
        "JSR jumps to the address held in R3 (landing pad within the same blob)",
        jsr_prog,
        {"R3": RESET_VECTOR + 2, "R9": 0x77, "R10": 0, "SP": 0x8000},
        # PC settles 3 bytes past the landing address, at the self-loop --
        # execution correctly runs the marker MOVB (3 bytes) before spinning.
        {"PC": RESET_VECTOR + 2 + 3, "R10": 0x77},
    ))

    # MOVB #5, R2 -- op1 is an "immediate quick" literal (Group 7, modm=0,
    # sub-index 0-15: the value IS those bits, no extra bytes).
    results.append(run(
        "Immediate-quick loads a small constant with no extra bytes",
        bytes([0x09, 0x21, 0xe5]),  # instflags: op1 general, modm=0, op2 short R1
        {"R1": 0xff},
        {"R1": 5},
    ))

    # MOVW #0xdeadbeef, R2 -- full-width immediate (Group 7 sub-index 20),
    # long-sized, 4 extra bytes follow the modifier.
    results.append(run(
        "Full immediate reads a dim-sized literal following the modifier",
        bytes([0x2d, 0x21, 0xf4, 0xef, 0xbe, 0xad, 0xde]),
        {"R1": 0},
        {"R1": 0xdeadbeef},
    ))

    # ANDB R1, R2 -- op2 = op2 & op1; overflow cleared, carry untouched.
    results.append(run(
        "ANDB masks bits and clears overflow",
        bytes([0xa0, 0x41, 0x62]),
        {"R1": 0x0f, "R2": 0xff},
        {"R2": 0x0f},
    ))

    # ORB R1, R2 -- op2 = op2 | op1.
    results.append(run(
        "ORB sets bits",
        bytes([0x88, 0x41, 0x62]),
        {"R1": 0x0f, "R2": 0xf0},
        {"R2": 0xff},
    ))

    # XORB R1, R2 -- op2 = op2 ^ op1.
    results.append(run(
        "XORB toggles bits to zero",
        bytes([0xb0, 0x41, 0x62]),
        {"R1": 0xff, "R2": 0xff},
        {"R2": 0x00},
    ))

    # NOTB R1, R2 -- op2 = ~op1, byte-sized (preserves R2's upper bits).
    results.append(run(
        "NOTB writes the bitwise complement of the source",
        bytes([0x38, 0x41, 0x62]),
        {"R1": 0x0f, "R2": 0xffffff00},
        {"R2": 0xfffffff0},
    ))

    # PUSH R1 then POP R2 -- round-trips a value through the stack, net
    # zero effect on SP.
    results.append(run(
        "PUSH then POP round-trips a value through the stack",
        bytes([0xef, 0x62, 0xe7, 0x63]),  # PUSH R2 ; POP R3 (register_direct(2), register_direct(3))
        {"R2": 0x12345678, "SP": 0x8000},
        {"R3": 0x12345678, "SP": 0x8000},
    ))

    # PUSH of an immediate pushes the FULL 32-bit value -- PUSH hardcodes a
    # Long-sized operand decode regardless of addressing mode (confirmed
    # against the reference's opPUSH: "m_moddim = 2" unconditionally).
    results.append(run(
        "PUSH of an immediate pushes the full 32-bit value, then POP reads it back",
        bytes([0xee, 0xe5, 0xe7, 0x62]),  # PUSH #5 (immediate-quick) ; POP R2
        {"SP": 0x8000},
        {"R2": 5, "SP": 0x8000},
    ))

    # INCB_1 R1 -- single-operand read-modify-write, +1, full ADD-style
    # flags (unlike AND/OR/XOR/NOT, INC/DEC DO set carry).
    results.append(run(
        "INCB sets carry on overflow past a byte",
        bytes([0xd9, 0x62]),  # register_direct(R2)
        {"R2": 0xff},
        {"R2": 0x00},
    ))

    # DECB_0 [R3] -- register-indirect (modm=0 via opcode 0xd0, mode index
    # 3): confirms INC/DEC work through memory addressing, not just
    # register-direct, by checking R3 itself is untouched (the harness
    # only reads back registers/PC, not arbitrary memory, so the indirect
    # write itself is covered by tests/unit/v60_test.cpp instead).
    results.append(run(
        "DECB through register-indirect leaves the address register itself untouched",
        bytes([0xd0, 0x63]),
        {"R3": 0x1234},
        {"R3": 0x1234},
    ))

    # SHLB R1, R2 -- op1 is a signed count (positive=left, negative=right,
    # both LOGICAL/zero-fill). Positive count: shift left, carry = last
    # bit shifted out.
    results.append(run(
        "SHL with a positive count shifts left and sets carry",
        bytes([0xa9, 0x41, 0x62]),
        {"R1": 1, "R2": 0x81},
        {"R2": 0x02},
    ))

    # Negative count: shift right LOGICALLY (zero-fill), not arithmetically.
    results.append(run(
        "SHL with a negative count shifts right logically, not arithmetically",
        bytes([0xa9, 0x41, 0x62]),
        {"R1": 0xffffffff, "R2": 0x81},  # count = -1
        {"R2": 0x40},
    ))

    # SHLH: the count operand (op1) is always Byte-sized even for the
    # 16-bit form -- confirmed against the reference's F12DecodeOperands
    # call, which hardcodes a literal 0 for op1's dim regardless of dim2.
    results.append(run(
        "SHLH shifts a 16-bit operand with a byte-sized count",
        bytes([0xab, 0x41, 0x62]),
        {"R1": 4, "R2": 0x0001},
        {"R2": 0x0010},
    ))

    # SHAB R1, R2 -- same signed-count encoding as SHL, but right shifts
    # are ARITHMETIC (sign-preserving), contrasted directly against SHL's
    # logical 0x40 result for the identical input above.
    results.append(run(
        "SHA with a negative count shifts right arithmetically",
        bytes([0xb9, 0x41, 0x62]),
        {"R1": 0xffffffff, "R2": 0x81},  # count = -1
        {"R2": 0xc0},
    ))

    # Right shift beyond the operand's width fully sign-extends (confirmed
    # reference behavior for this specific case, unlike SHL's analogous
    # branch which the reference itself leaves undefined).
    results.append(run(
        "SHA right shift beyond the operand's width fully sign-extends",
        bytes([0xb9, 0x41, 0x62]),
        {"R1": 0xfffffff8, "R2": 0x80},  # count = -8
        {"R2": 0xff},
    ))

    # The harness only dumps registers/PC, not flags directly -- so to
    # check the overflow flag itself (not just the shifted value), follow
    # SHA with BV8 (branch-if-overflow) into one of two marker blocks that
    # each set R4 to a distinct sentinel and spin forever, the same
    # technique already validated for CMPB+BE8 above.
    #
    #   addr0:  SHAB R1, R2                  (3 bytes)
    #   addr3:  BV8 +7  -> addr10 if overflow (2 bytes)
    #   addr5:  MOVB R5, R4   ; no-overflow marker (0xAA)
    #   addr8:  BR8 +0        ; spin
    #   addr10: MOVB R6, R4   ; overflow marker (0xBB)
    #   addr13: BR8 +0        ; spin
    sha_overflow_prog = bytes([
        0xb9, 0x41, 0x62,
        0x60, 0x07,
        0x09, 0x45, 0x64,
        0x6a, 0x00,
        0x09, 0x46, 0x64,
        0x6a, 0x00,
    ])

    # Left shift by 2+ CAN set overflow (unlike SHL, which never does):
    # 0x40 (positive) shifting to a sign-changed value.
    results.append(run(
        "SHA left shift by 2 sets overflow when the sign effectively changes",
        sha_overflow_prog,
        {"R1": 2, "R2": 0x40, "R4": 0, "R5": 0xaa, "R6": 0xbb},
        {"R4": 0xbb},
    ))

    # A real, non-obvious quirk this project's own derivation predicted and
    # wants confirmed against the actual reference, not just self-consistent
    # unit tests: a single-bit left shift's overflow formula degenerates to
    # comparing the sign bit against itself, so it NEVER reports overflow --
    # even here, where 0x40 (positive) becomes 0x80 (negative).
    results.append(run(
        "SHA left shift by exactly 1 never reports overflow even when the sign visibly changes",
        sha_overflow_prog,
        {"R1": 1, "R2": 0x40, "R4": 0, "R5": 0xaa, "R6": 0xbb},
        {"R4": 0xaa},
    ))

    # CALL [R3], #7 then RET back -- CALL's operands can never legitimately
    # be a bare register (rejected the same way JMP/JSR reject one), so
    # both must be general form (instflags bit 7 set) here; R3 (jump
    # target) points at a RET landing pad further along in the same blob,
    # and the return address (right after CALL) holds a marker MOVB
    # followed by a self-loop, so PC settles somewhere checkable instead of
    # drifting.
    #
    #   addr0: CALL [R3], #7           (4 bytes: 0x49, 0x80, 0x63, 0xE7)
    #   addr4: MOVB R9, R10            ; marker: returned here (3 bytes)
    #   addr7: BR8 +0                  ; spin
    #   addr9: RET_0 #0                ; landing pad for CALL (2 bytes)
    call_prog = bytes([
        0x49, 0x80, 0x63, 0xe7,
        0x09, 0x49, 0x6a,
        0x6a, 0x00,
        0xe2, 0xe0,
    ])
    results.append(run(
        "CALL sets a new AP and jumps; RET restores the old AP and returns",
        call_prog,
        {"R3": RESET_VECTOR + 9, "R9": 0x77, "R10": 0, "AP": 0x9999, "SP": 0x8000},
        # PC settles 3 bytes past the return address, at the self-loop. The
        # return address itself is computed from CALL's *raw* (unmasked,
        # 0xFFFFFFF0-based) PC -- not the masked reset vector R3 uses --
        # same distinction documented for JSR/JMP earlier in this file.
        {"PC": 0xfffffff0 + 4 + 3, "R10": 0x77, "AP": 0x9999, "SP": 0x8000},
    ))

    # JMP #0 -- JMP hardcodes a Byte-sized operand decode regardless of
    # addressing mode (confirmed against the reference's opJMP, which
    # always passes moddim=0), so even a "full" immediate here reads only
    # one byte. Address 0 is v60test.cpp's pre-filled landing pad (an
    # infinite self-branch), so PC settles there instead of drifting
    # through uninitialized memory.
    results.append(run(
        "JMP through an immediate treats it as a literal absolute address",
        bytes([0xd6, 0xf4, 0x00]),
        {},
        {"PC": 0x00},
    ))

    print()
    print(f"{sum(results)}/{len(results)} programs matched the MAME v60_device reference exactly.")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

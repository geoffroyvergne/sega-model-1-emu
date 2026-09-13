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

    print()
    print(f"{sum(results)}/{len(results)} programs matched the MAME v60_device reference exactly.")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())

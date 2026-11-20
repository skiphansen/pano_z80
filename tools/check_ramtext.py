#!/usr/bin/env python3
"""Verify that the RAM-resident SPI flash code never depends on flash.

The routines in the firmware's .ramtext section reprogram the same SPI flash
the CPU normally executes from, so while they run nothing in flash can be
read.  A single call into flash-resident code (printf, memcpy, a libgcc helper
such as __mulsi3), or a constant fetched from .rodata, would hang the update
half way through and leave the board needing a JTAG programmer.

This inspects the linked image and fails the build if any instruction in
.ramtext transfers control outside .ramtext, makes an indirect call, or forms
an address inside the firmware's flash window.

It also insists that .ramtext holds no compressed instructions, so that every
instruction is one aligned 32-bit word.  The RAM is DDR memory behind
fpga/mig_picorv_bridge.v, and that bridge keeps its ready signal up until the
CPU drops mem_valid.  picorv32 does not drop it between the two halves of a
32-bit instruction that straddles a word boundary when it has just jumped
there, so the second half is "answered" at once with the first word again and
the CPU executes garbage.  Code in flash is not affected (the cache in front
of the flash handles it), data accesses never do this, and neither does code
made of aligned 32-bit instructions only.

    check_ramtext.py firmware.elf [objdump-prefix]
"""
import re
import subprocess
import sys

FLASH_LO, FLASH_HI = 0x0E000000, 0x0E020000   # XIP firmware window

BRANCHES = {"beq", "bne", "blt", "bge", "bltu", "bgeu", "beqz", "bnez", "bgez",
            "bltz", "blez", "bgtz", "bgt", "ble", "bgtu", "bleu", "j", "jal"}


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    elf = sys.argv[1]
    prefix = sys.argv[2] if len(sys.argv) > 2 else "riscv32-unknown-elf-"

    syms = {}
    for line in run([prefix + "nm", elf]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    try:
        lo, hi = syms["_sramtext"], syms["_eramtext"]
    except KeyError:
        print("check_ramtext: _sramtext/_eramtext not found in %s" % elf)
        return 1
    if hi <= lo:
        print("check_ramtext: .ramtext is empty - RAM flash routines missing")
        return 1
    if FLASH_LO <= lo < FLASH_HI:
        print("check_ramtext: .ramtext is linked INTO flash (0x%08x)" % lo)
        return 1

    dis = run([prefix + "objdump", "-d", "-M", "no-aliases",
               "--start-address=0x%x" % lo, "--stop-address=0x%x" % hi, elf])
    insn_re = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(\S+)\s*(.*)$")
    problems = []
    compressed = []
    count = 0
    calls = 0
    func = "?"
    pending = None                   # (reg, value, where) from an auipc
    for line in dis.splitlines():
        m = re.match(r"^[0-9a-f]+ <(\S+)>:$", line)
        if m:
            func = m.group(1)
            if pending:
                problems.append("dangling auipc: " + pending[2])
            pending = None
            continue
        m = insn_re.match(line)
        if not m:
            continue
        addr, op, args = int(m.group(1), 16), m.group(3), m.group(4)
        args = args.split("#")[0].strip()
        if len(m.group(2)) != 8 or op.startswith("c.") or addr & 3:
            compressed.append("%s @ 0x%08x: %s %s" % (func, addr, op, args))
        op = op[2:] if op.startswith("c.") else op
        count += 1
        where = "%s @ 0x%08x: %s %s" % (func, addr, op, args)

        # A far call is "auipc rX,hi ; jalr ra,lo(rX)".  That is the only use
        # of auipc allowed here, and its target must stay inside .ramtext.
        if pending:
            reg, value, pwhere = pending
            pending = None
            m2 = re.match(r"^(\w+),(-?\d+)\((\w+)\)$", args.replace(" ", ""))
            if op == "jalr" and m2 and m2.group(3) == reg:
                target = (value + int(m2.group(2))) & 0xFFFFFFFF
                calls += 1
                if not lo <= target < hi:
                    problems.append("call leaves .ramtext (0x%08x): %s"
                                    % (target, where))
                continue
            problems.append("auipc not used for a call: " + pwhere)

        if op == "auipc":
            r, _, imm = args.partition(",")
            try:
                pending = (r.strip(), (addr + (int(imm, 16) << 12)) & 0xFFFFFFFF,
                           where)
            except ValueError:
                problems.append("cannot decode auipc: " + where)
        elif op in BRANCHES:
            t = re.search(r"\b([0-9a-f]{5,8})\b\s*(<|$)", args)
            if not t:
                problems.append("cannot decode target: " + where)
            else:
                if op == "jal":
                    calls += 1
                if not lo <= int(t.group(1), 16) < hi:
                    problems.append("leaves .ramtext: " + where)
        elif op in ("jalr", "jr"):
            # anything other than a plain return through ra is an indirect jump
            a = args.replace(" ", "")
            if a not in ("ra", "zero,0(ra)", "zero,ra,0", "0(ra)"):
                problems.append("indirect jump/call: " + where)
        elif op in ("ecall", "ebreak", "mret", "sret", "wfi"):
            problems.append("trap/system instruction: " + where)
        elif op == "lui":
            r, _, imm = args.partition(",")
            try:
                v = (int(imm, 16) << 12) & 0xFFFFFFFF
                if FLASH_LO <= v < FLASH_HI:
                    problems.append("forms a flash address: " + where)
            except ValueError:
                problems.append("cannot decode lui: " + where)
        if re.search(r"\((gp|tp)\)|\b(gp|tp),", args) and op != "lui":
            problems.append("gp/tp-relative access: " + where)
    if pending:
        problems.append("dangling auipc: " + pending[2])

    if compressed:
        problems.append("%d compressed or unaligned instructions, the first is %s"
                        % (len(compressed), compressed[0]))
        problems.append("(code that runs from DDR must be built with "
                        "-march=rv32i: see the comment at the top of this script)")
    if lo & 3:
        problems.append(".ramtext does not start on a word boundary")
    if count == 0:
        problems.append("no instructions disassembled from .ramtext")
    if problems:
        print("check_ramtext: FAILED - .ramtext (0x%08x..0x%08x) is not self "
              "contained:" % (lo, hi))
        for p in problems:
            print("   " + p)
        return 1
    print("check_ramtext: OK - %d instructions and %d calls in .ramtext "
          "(0x%08x..0x%08x), nothing leaves it, none compressed"
          % (count, calls, lo, hi))
    return 0


if __name__ == "__main__":
    sys.exit(main())

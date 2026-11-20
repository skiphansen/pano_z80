#!/usr/bin/env python3
"""Run the flash-update simulations and check their results."""
import subprocess, sys

def vvp(image, *plusargs, fix=False):
    cmd = ["vvp", "-n", "build/tb_fix.vvp" if fix else "build/tb.vvp", "+fw=build/%s.hex" % image]
    return subprocess.run(cmd + list(plusargs), capture_output=True, text=True).stdout

RUNS = [
    # name, image, plusargs, fixed fault detector?, must contain, must not contain
    ("full update: both regions rewritten", "fw", ["+check_flash"], False,
     ["REPORT 00202014", "REPORT a5a4a7a6", "REPORT a1a0a3a2", "LED <= 3", "LED <= 2", "SE=2 PP=5", "ignored(busy)=0 ignored(no WEL)=0",
      "config_en=1", "flash content check: 0 mismatching bytes"], ["REPORT bad", "REPORT dead", "TIMEOUT"]),
    ("unchanged bitstream region is left alone", "fw", ["+check_flash", "+preload_bit"], False,
     ["REPORT 5a5b5c5d", "REPORT 5e5f6061", "LED <= 2", "SE=1 PP=2", "flash content check: 0 mismatching bytes"], ["REPORT bad", "REPORT dead", "TIMEOUT"]),
    ("full update with the bus-fault detector fix", "fw", ["+check_flash"], True,
     ["LED <= 2", "SE=2 PP=5", "cpu_irq_latched=0", "flash content check: 0 mismatching bytes"], ["REPORT bad", "REPORT dead", "TIMEOUT"]),
    ("ID read returns into flash code (XIP + cache)", "fw_unmask", [], True,
     ["REPORT 00202014", "REPORT a5a4a7a6", "REPORT 33330000", "REPORT 0d0e0000", "cpu_irq_latched=0"], ["REPORT dead"]),
    ("old bitstream: unmasking IRQs after a cfgreg access faults", "fw_unmask", [], False,
     ["REPORT 00202014", "REPORT dead", "cpu_irq_latched=1"], ["REPORT 33330000"]),
    ("negative control: original config_en polarity cannot work", "fw_oldpol", ["+maxcycles=400000"], False,
     ["REPORT 00ffffff", "RDID=0 WREN=0 SE=0 PP=0", "TIMEOUT"], ["REPORT 00202014", "LED <= 2"]),
    ("negative control: compressed code in RAM hangs behind the DDR bridge", "fw_rvc", ["+maxcycles=400000"], False,
     ["REPORT 11110000", "CPU TRAP", "RDID=0", "config_en=0"], ["REPORT 00202014"]),
]

fails = 0
prefix = sys.argv[1] if len(sys.argv) > 1 else "riscv32-unknown-elf-"
chk = subprocess.run([sys.executable, "../../tools/check_ramtext.py", "build/fw_rvc.elf", prefix],
                     capture_output=True, text=True)
ok = chk.returncode != 0 and "compressed" in chk.stdout
print("%-4s %s" % ("ok" if ok else "FAIL", "check_ramtext rejects compressed code in RAM"), flush=True)
if not ok:
    fails += 1
    print("     " + chk.stdout.strip().replace("\n", "\n     "))
for name, image, args, fix, must, mustnot in RUNS:
    out = vvp(image, *args, fix=fix)
    ok = all(m in out for m in must) and not any(m in out for m in mustnot)
    print("%-4s %s" % ("ok" if ok else "FAIL", name), flush=True)
    if not ok:
        fails += 1
        print("     " + "\n     ".join(l for l in out.splitlines() if "cache ready" not in l and "WARNING" not in l))
print("%d/%d checks passed" % (len(RUNS) + 1 - fails, len(RUNS) + 1))
sys.exit(1 if fails else 0)

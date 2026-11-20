#!/usr/bin/env python3
"""Tests of ../setup.c (settings file, setup menu) through build/setup_harness."""
import subprocess, sys

def run(*args):                     # (text mode: the file's CR LF arrive here as LF)
    r = subprocess.run(["./build/setup_harness"] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr

GREEN, WHITE, PAPER, AMBER = ("color=0 fg=00ff00 bg=000000", "color=1 fg=ffffff bg=000000",
                              "color=2 fg=000000 bg=ffffff", "color=3 fg=ffb000 bg=000000")
T = [  # name, args, expected exit code, must contain, must NOT contain
 ("settings file is applied", ["load", r"color=amber\ncharset=german\ncapslock=capslock\n"], 0,
  [AMBER + " charset=1 caps=0"], []),
 ("CR LF, blanks, case, comments, unknown keys, CP/M end of file",
  ["load", r"# comment\r\n  Colour = PAPER  \r\nCHARSET=German\r\nbogus=1\r\ncapslock=ctrl\zcolor=amber"], 0,
  [PAPER + " charset=1 caps=1"], []),
 ("nonsense leaves the defaults", ["load", r"color=purple\ncharset=\n=x\nnoequals\ncolor"], 0,
  [GREEN + " charset=0 caps=1"], []),
 ("no settings: green, US ASCII, Caps Lock swapped - as before there were settings", ["load", ""], 0,
  [GREEN + " charset=0 caps=1"], []),
 ("menu: change colour, Enter: saved, screen restored", ["menu", r"1\r"], 0,
  [WHITE, "saved=1 restored=1 forgot=0 writes=1", "color=white\ncharset=us\ncapslock=ctrl\n"], []),
 ("menu: every setting, Esc", ["menu", r"11123\e"], 0,
  [AMBER + " charset=1 caps=0", "restored=1", "color=amber\ncharset=german\ncapslock=capslock\n"], []),
 ("menu: the colours come round to green again", ["menu", r"1111\r", r"color=green\n"], 0,
  [GREEN, "writes=1", "color=green\n"], []),
 ("menu: nothing changed, nothing written", ["menu", r"x\r", r"color=amber\n"], 0,
  [AMBER, "restored=1 forgot=0 writes=0"], []),
 ("menu: a cursor key is not Esc", ["menu", r"2\e+[+Aq"], 0,
  ["charset=1", "restored=1", "keys-left=0"], ["would wait"]),
 ("menu: update comes back to the menu", ["menu", r"u\r\r"], 0,
  ["[update menu]", "Press a key.", "restored=1"], []),
 ("menu: reset the Z80 leaves the screen to the reset", ["menu", "r"], 0,
  ["reset=1", "restored=0 forgot=1"], []),
 ("menu: disk swapped - leave, keep the queued Ctrl-C for CP/M", ["menu", "d", "swapkey"], 0,
  ["[swap menu]", "restored=0 forgot=1", "keys-left=1"], ["Press a key."]),
 ("menu: disk swap cancelled - back to the menu", ["menu", r"d\r\r"], 0,
  ["[swap menu]", "Press a key.", "restored=1 forgot=0"], []),
 ("menu: settings cannot be saved", ["menu", r"1\r\r", "failwrite"], 0,
  ["Could not save the settings to PANOZ80.CFG (error 1)", WHITE, "restored=1"], []),
 ("menu: waits for a key", ["menu", ""], 3, ["would wait forever"], []),
]
fails = 0
for name, args, code, must, mustnot in T:
    rc, out = run(*args)
    ok = rc == code and all(m in out for m in must) and not any(m in out for m in mustnot)
    print("%-4s %s" % ("ok" if ok else "FAIL", name))
    if not ok:
        fails += 1
        print("     exit %d\n     %s" % (rc, out.replace("\n", "\n     ")))
print("%d/%d passed" % (len(T) - fails, len(T)))
sys.exit(1 if fails else 0)

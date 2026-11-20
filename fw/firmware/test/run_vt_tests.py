#!/usr/bin/env python3
"""Tests of the console (../vt100.c) through build/vt_harness: the ITT 3030
control codes, as an ITT 3030 BASIC program sends them (two screens modelled
on those of a real one, with other names in them), and
the screen save/restore the setup menu relies on."""
import subprocess, sys

def screen(data, *args):
    r = subprocess.run(["./build/vt_harness"] + list(args), input=bytes(data), capture_output=True)
    lines = r.stdout.decode("latin-1").splitlines()
    s = {int(l[:2]): l[3:] for l in lines if l[:2].isdigit()}
    last = 0
    for l in lines:                 # "  |   ^^^" marks the inverse cells of the line above
        if l[:2].isdigit(): last = int(l[:2])
        elif l.startswith("  |"): s[-last] = l[3:]
    return s, lines[-1] if lines else "", r.returncode

class Prog:                          # what MBASIC sends to the console
    def __init__(self): self.out = bytearray()
    def P(self, *parts, nl=True):
        for p in parts: self.out += p if isinstance(p, (bytes, bytearray)) else p.encode("latin-1")
        if nl: self.out += b"\r\n"
    def pos(self, z, s):             # the cursor subroutine such a program has
        self.P(bytes([27, 31, z + 32, s + 32]), nl=False)
        self.P(bytes([27, 16, 6]))
        self.P(bytes([27, 31, z + 32, s + 32]), nl=False)

RULE = "_" * 79
def title():
    p = Prog()
    p.P(bytes([12])); p.P(bytes([27, 18]))
    p.pos(4, 60); p.P("Erika Mustermann")
    p.pos(6, 0); p.P(bytes([143]) * 79)
    p.pos(10, 27); p.P("Verwaltungsprogramm")
    p.pos(12, 35); p.P("f}r")
    p.pos(19, 0); p.P(bytes([143]) * 79)
    return p.out
def menu():
    p = Prog()
    p.P(bytes([27, 19])); p.P(bytes([12, 27, 17]))
    p.pos(1, 0); p.P("SPORTVEREIN" + " " * 17 + "Gesch{ftsstelle G|ttingen")
    p.pos(1, 67); p.P(bytes([28]), " 11.11.9999 ", bytes([18]))
    p.P(bytes([143]) * 79)
    p.pos(6, 30); p.P("stale text that the next screen erases")
    p.pos(4, 0); p.P(bytes([27, 23]))
    p.pos(6, 30); p.P("Programme")
    p.pos(19, 51)
    return p.out

fails = 0
def check(name, cond, detail=""):
    global fails
    print("%-4s %s" % ("ok" if cond else "FAIL", name))
    if not cond:
        fails += 1
        if detail: print("     " + str(detail).replace("\n", "\n     "))

s, cur, rc = screen(title(), "itt", "german")
check("ITT: a title screen",
      s[4] == " " * 60 + "Erika Mustermann" and s[6] == RULE and s[10] == " " * 27 + "Verwaltungsprogramm"
      and s[12] == " " * 35 + "fur" and s[19] == RULE and all(s[r] == "" for r in (1, 2, 3, 5, 7, 20, 24))
      and cur == "[cursor row 20 col 0 off]", (s, cur))
s, cur, rc = screen(menu(), "itt", "german")
check("ITT: header, rule, erase to end of screen, cursor at the input field",
      s[1] == "SPORTVEREIN" + " " * 17 + "Geschaftsstelle Gottingen" + " " * 15 + "11.11.9999" and s[2] == RULE
      and s[6] == " " * 30 + "Programme" and cur == "[cursor row 19 col 51 on]", (s, cur))
s, cur, rc = screen(b"abc\x0cxyz", "itt")
check("ITT: clear screen leaves the cursor at the bottom left", s[24] == "xyz" and s[1] == "" and cur.startswith("[cursor row 24 col 3 "), (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11top\x1b\x16\x05\x0afive\x1b\x1d\x03*\x1b\x10\x02!\x1b\x1a>", "itt")
check("ITT: home, binary cursor address, repeat character, blanks, cursor right",
      s[1] == "top" and s[5] == " " * 10 + "five***  ! >", (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11" + b"x" * 80 + b"y" + b"\x1b\x16\x03\x05abcdef\x1b\x16\x03\x07\x1b\x18\x1b\x1c\x1c!", "itt")
check("ITT: line wrap, erase to end of line, cursor up",
      s[1] == "x" * 80 and s[3] == "     ab" and s[2] == "y      !", (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11" + b"\x80\x83 \xc1\x1cB\x12C" + bytes([27, 31, 35 + 32, 2 + 32]) + b"Z", "itt")
check("ITT: graphics, characters with the inverse bit, a row beyond the screen wraps",
      s[1] == "## ABC" and s[11] == "  Z", (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11" + b"\x8f\x80\x8f|\x81\x80\x82|\x03\x00\x0f|\x1c\x0f\x00\x12|\x1a\x9c|", "itt")
check("ITT: the block mosaic and its inverse (chess piece rows), unknown glyphs",
      s[1] == "_#_|###|# #|_#| #|", (s, cur))
s, cur, rc = screen(b"\x1b[2J\x1b[H" + b"menu\r\n\x1b[5;10Hhere", "itt")
check("ITT: ANSI sequences still work (the firmware's own menus)", s[1] == "menu" and s[5] == " " * 9 + "here", (s, cur))
s, cur, rc = screen(b"\x1b[2J\x1b[Hab\x0c\x1b\x11\x8f", )
check("VT100: the ITT codes are not interpreted", s[1].startswith("ab") and cur.startswith("[cursor row 1 "), (s, cur))
s, cur, rc = screen(b"\x1b[2J\x1b[Hline one\r\nline two\r\n   A>" + b"\xf0" + b"\x1b[H\x1b[2J== menu ==\r\nstuff\r\n" + b"\xf1" + b"dir")
check("screen save / restore around a menu", s[1] == "line one" and s[2] == "line two" and s[3] == "   A>dir", (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11\x1b\x12hidden" + b"\xf0" + b"\x1b[H\x1b[2Jmenu" + b"\xf1" + b"!", "itt")
check("screen save / restore keeps a hidden cursor hidden", s[1] == "hidden!" and cur.endswith("off]"), (s, cur))
# a display with inverse video and the mosaic glyphs
s, cur, rc = screen(title(), "itt", "german", "attr")
check("with attributes: the rule is the rule glyph", s[6] == "=" * 79 and s[19] == "=" * 79 and -6 not in s, (s, cur))
s, cur, rc = screen(menu(), "itt", "german", "attr")
check("with attributes: the date in the header is in inverse video",
      s[1].endswith(" 11.11.9999") and s[-1] == " " * 67 + "^" * 12 and s[2] == "=" * 79 and -2 not in s, (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11" + b"\x8f\x80\x8f|\x81\x80\x82|\x87\x80\x8b|\x03\x00\x0f|\x1a\x9c|\xc1B", "itt", "attr")
check("with attributes: the mosaic (chess piece rows), inverse letters",
      s[1] == "=f=|efd|8f4|3 f| f|AB" and s[-1] == " " * 19 + "^", (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11" + b"\x1cINV\x12 plain" + b"\r\n" * 30 + b"\x1b\x11\x1cX\x12", "itt", "attr")
check("with attributes: inverse cells scroll with their text and are cleared",
      s[1] == "X" and s[-1] == "^" and all(k > 0 or k == -1 for k in s) and all(s[r] == "" for r in range(2, 25)), (s, cur))
s, cur, rc = screen(b"\x0c\x1b\x11" + b"\x1c HI \x12" + b"\xf0" + b"\x1b[H\x1b[2Jmenu" + b"\xf1", "itt", "attr")
check("with attributes: screen save / restore keeps them", s[1] == " HI" and s[-1] == "^^^^", (s, cur))
s, cur, rc = screen(b"\x1b[2J\x1b[H\xc1\x8f|", "attr")
check("with attributes: VT100 mode still ignores bit 7", s[1].startswith("A") and -1 not in s, (s, cur))
s, cur, rc = screen("Gr\u00f6\u00dfe zur\u00fcck \u00c4\u00d6\u00dc\u00e4 \u00a7 {|} caf\u00e9".encode("utf-8"), "utf8")
check("firmware text is UTF-8: umlauts whatever the character set, ? for what the font lacks",
      s[1] == "Grose zuruck AOUa P {|} caf?", (s, cur))
s, cur, rc = screen("\u00e4{".encode("utf-8"), "utf8", "german")
check("firmware text is UTF-8: and ASCII still goes through the character set", s[1] == "aa", (s, cur))
n = 19
print("%d/%d passed" % (n - fails, n))
sys.exit(1 if fails else 0)

#!/usr/bin/env python3
"""Checks of the firmware's translations (../lang/*.h).

A translation file is a list of {"English text", "translated text"} pairs.
The English text is the format string of a message exactly as the firmware
source has it; that is how the message is found at run time.  So for every
pair this makes sure that

  - the English text really is a message of the firmware (or one of the words
    it passes to Translate()), so that a reworded message does not silently
    lose its translation;
  - the translation takes the same printf arguments, in the same order;
  - the translation only uses characters the display has a glyph for.

It also lists the messages a language has no translation for (those are
shown in English), which is not an error.

    run_lang_tests.py [--list]      --list prints every translatable message
"""
import glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..")
MACROS = {"ANSI_HOME": "\033[H", "ANSI_CLS": "\033[2J"}
GLYPHS = set("äöüÄÖÜß§")                 # beyond ASCII: what vt100_put_utf8() can show

STR = r'"(?:[^"\\]|\\.)*"'

def unescape(lit):
    """the value of a C string literal (without the quotes)"""
    out = []; i = 0
    simple = {"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"', "'": "'", "0": "\0", "a": "\a", "b": "\b"}
    while i < len(lit):
        c = lit[i]
        if c != "\\": out.append(c); i += 1; continue
        n = lit[i + 1]
        if n in "01234567":
            m = re.match(r"[0-7]{1,3}", lit[i + 1:]); out.append(chr(int(m.group(), 8))); i += 1 + len(m.group())
        elif n == "x":
            m = re.match(r"[0-9a-fA-F]+", lit[i + 2:]); out.append(chr(int(m.group(), 16))); i += 2 + len(m.group())
        else:
            out.append(simple[n]); i += 2
    return "".join(out)

def first_argument(text, pos):
    """the string a call starts with: adjacent literals and known macros"""
    parts = []; found = False
    while True:
        m = re.compile(r"\s*(?:(%s)|([A-Za-z_]\w*))" % STR).match(text, pos)
        if not m: break
        if m.group(1): parts.append(unescape(m.group(1)[1:-1])); found = True
        elif m.group(2) in MACROS: parts.append(MACROS[m.group(2)])
        else: return None
        pos = m.end()
    return "".join(parts) if found else None

def messages():
    """every format string of an ALOG_R() and every argument of TR() / Translate()"""
    found = {}
    for path in sorted(glob.glob(os.path.join(SRC, "*.c"))):
        text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group()), open(path, encoding="latin-1").read(), flags=re.S)
        for m in re.finditer(r"\b(ALOG_R|TR|Translate)\s*\(", text):
            s = first_argument(text, m.end())
            if s is not None and s.strip("\n\r\033[HJ2 %c") != "":
                found.setdefault(s, os.path.basename(path))
        # words handed over in tables:  TR_N("...")  marks them
        for m in re.finditer(r"\bTR_N\s*\(\s*(%s)\s*\)" % STR, text):
            found.setdefault(unescape(m.group(1)[1:-1]), os.path.basename(path))
    return found

def pairs(path):
    text = open(path, encoding="utf-8").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    out = []
    for m in re.finditer(r"\{\s*((?:%s\s*)+),\s*((?:%s\s*)+)\}" % (STR, STR), text):
        join = lambda g: "".join(unescape(x[1:-1]) for x in re.findall(STR, g))
        out.append((join(m.group(1)), join(m.group(2))))
    return out

def conversions(fmt):
    return [m.group(1) + m.group(2) for m in
            re.finditer(r"%(?!%)[-+ #0]*\d*(?:\.\d+)?(l{0,2}|h{0,2}|z)?([diuxXcsp])", fmt)]

def main():
    msgs = messages()
    if "--list" in sys.argv:
        for s, f in msgs.items(): print("%-16s %r" % (f, s))
        return 0
    fails = 0
    def check(name, ok, detail=()):
        nonlocal fails
        print("%-4s %s" % ("ok" if ok else "FAIL", name))
        if not ok:
            fails += 1
            for d in list(detail)[:12]: print("     " + d)
    files = sorted(glob.glob(os.path.join(SRC, "lang", "*.h")))
    check("there is at least one translation", len(files) > 0)
    for path in files:
        lang = os.path.basename(path)[:-2]
        p = pairs(path)
        check("%s: has translations" % lang, len(p) > 10)
        check("%s: every English text is a message of the firmware" % lang,
              all(en in msgs for en, _ in p), ["not a message: %r" % en for en, _ in p if en not in msgs])
        dup = {en for en, _ in p if [e for e, _ in p].count(en) > 1}
        check("%s: no message is translated twice" % lang, not dup, ["twice: %r" % d for d in dup])
        bad = [(en, tr) for en, tr in p if conversions(en) != conversions(tr)]
        check("%s: translations take the same printf arguments" % lang, not bad,
              ["%r -> %r" % b for b in bad])
        bad = [(en, tr) for en, tr in p if en.endswith("\n") != tr.endswith("\n") or en.startswith("\n") != tr.startswith("\n")]
        check("%s: translations begin and end a line where the English does" % lang, not bad,
              ["%r -> %r" % b for b in bad])
        bad = sorted({c for _, tr in p for c in tr if ord(c) > 126 and c not in GLYPHS})
        check("%s: only characters the display has" % lang, not bad, ["no glyph for %r" % c for c in bad])
        long = [(en, tr) for en, tr in p if any(len(l) > 80 for l in tr.split("\n"))]
        check("%s: no translated line is longer than the screen is wide" % lang, not long,
              ["%r" % tr for _, tr in long])
        missing = [s for s in msgs if s not in dict(p)]
        print("     (%s: %d of %d messages translated%s)" % (lang, len(msgs) - len(missing), len(msgs),
              "; in English: " + ", ".join(repr(m[:28]) for m in missing[:6]) + (" ..." if len(missing) > 6 else "") if missing else ""))
    print("%d checks failed" % fails if fails else "translations: all checks passed")
    return 1 if fails else 0

if __name__ == "__main__":
    sys.exit(main())

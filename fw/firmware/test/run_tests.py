#!/usr/bin/env python3
"""Exercise the F11 update menu (flash_update.c) natively: one good image and a
series of deliberately damaged ones.  See Makefile."""
import datetime, os, re, struct, subprocess, sys, zlib
# usage: run_tests.py <bitstream.bit> <firmware.bin>   (run from fw/firmware/test)
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
BIT = os.path.abspath(sys.argv[1]); FW = os.path.abspath(sys.argv[2])
MK = [sys.executable, os.path.join(REPO, "tools", "mkupdate.py")]
os.makedirs(os.path.join(HERE, "build"), exist_ok=True)
os.chdir(os.path.join(HERE, "build"))
crc = lambda b: zlib.crc32(b) & 0xFFFFFFFF
fw = open(FW, "rb").read()

# The tests must not depend on whether the bitstream they are given was built
# with a build ID (xilinx/build.tcl) or without (ISE GUI): make both variants.
ID_A, ID_B = 0x6AFE1234, 0x6AFD0000
UID = re.compile(rb"UserID=0x[0-9A-Fa-f]{8}")
raw = open(BIT, "rb").read()
assert len(UID.findall(raw)) == 1, "%s: no UserID in the .bit header" % BIT
bit = UID.sub(b"UserID=0xFFFFFFFF", raw)                    # no build ID
open("bit_noid.bit", "wb").write(bit)
open("bit_id.bit", "wb").write(UID.sub(b"UserID=0x%08X" % ID_A, raw))
BIT = "bit_noid.bit"

def mk(name, *args):
    r = subprocess.run(MK + ["-o", name, "--version", "test-1.0", "--date", "2026-11-19 12:00"] + list(args),
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
def rehdr(d):                       # recompute header crc after tampering
    return d[:84] + struct.pack("<I", crc(d[:84])) + d[88:]
def patch(src, dst, fn):
    d = bytearray(open(src, "rb").read()); d = fn(d); open(dst, "wb").write(bytes(d))
def run(*args):
    r = subprocess.run(["./harness"] + list(args), capture_output=True, text=True, errors="replace")
    return r.returncode, r.stdout + r.stderr

# only the configuration data of the .bit goes into the flash, not its header
def bit_parts(b):                   # -> header fields, configuration data
    p = 2 + struct.unpack(">H", b[0:2])[0] + 2; fields = {}
    for key in b"abcd":
        assert b[p] == key; n = struct.unpack(">H", b[p+1:p+3])[0]
        fields[chr(key)] = b[p+3:p+3+n].rstrip(b"\0").decode(); p += 3 + n
    assert b[p] == ord("e"); n = struct.unpack(">I", b[p+1:p+5])[0]
    return fields, b[p+5:p+5+n]
bitfields, cfg = bit_parts(bit)
i = fw.find(b"PANOFWID")            # what the menu must say about the firmware in the image
fwline = "  firmware:  %s, built %s\n" % (fw[i+8:i+32].split(b"\0")[0].decode(), fw[i+32:i+56].split(b"\0")[0].decode()) \
         if i >= 0 else "  firmware:  %u bytes, no version information\n" % len(fw)

mk("good.bin", "--bit", BIT, "--fw", FW)
def when(build_id):                 # what FormatBuildId() must print for a build ID
    return datetime.datetime.fromtimestamp(build_id, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
# an image with a bitstream that has a build ID and a "firmware" that is the one the harness claims to be running
own = subprocess.run(["./harness", "printinfo"], capture_output=True).stdout
open("fw_same.bin", "wb").write(b"\x13\x00\x00\x00" * 16 + own + b"\x00" * 64)
mk("withid.bin", "--bit", "bit_id.bit", "--fw", "fw_same.bin")
mk("fwonly.bin", "--fw", FW)
mk("bitonly.bin", "--bit", BIT)
good = open("good.bin", "rb").read()

def setb(d, off, val): d[off] = val; return d
def set32(d, off, val): d[off:off+4] = struct.pack("<I", val & 0xFFFFFFFF); return d
patch("good.bin", "t_magic.bin",   lambda d: setb(d, 0, ord('X')))
patch("good.bin", "t_hdrcrc.bin",  lambda d: setb(d, 20, d[20] ^ 1))
patch("good.bin", "t_hver.bin",    lambda d: bytearray(rehdr(bytes(set32(d, 8, 2)))))
patch("good.bin", "t_bitcrc.bin",  lambda d: setb(d, 88 + 5000, d[88 + 5000] ^ 0x10))
patch("good.bin", "t_fwcrc.bin",   lambda d: setb(d, len(d) - 100, d[len(d) - 100] ^ 0x10))
patch("good.bin", "t_trunc.bin",   lambda d: d[:len(d) - 100])
patch("good.bin", "t_bitoff.bin",  lambda d: bytearray(rehdr(bytes(set32(d, 60, 0xFFFFFFF0)))))
patch("good.bin", "t_fwoff.bin",   lambda d: bytearray(rehdr(bytes(set32(d, 72, 0xFFFFFF00)))))
patch("good.bin", "t_fwlenwrap.bin", lambda d: bytearray(rehdr(bytes(set32(d, 76, 0xFFFFFFFF)))))
patch("good.bin", "t_hdroverlap.bin", lambda d: bytearray(rehdr(bytes(set32(d, 60, 4)))))
# wrong-device bitstream with fully consistent CRCs (only the firmware's own check can catch it)
i = bit.index(b"3s1600e"); wb = bit[:i] + b"3s500e " + bit[i+7:]
hdr = good[:60] + struct.pack("<6I", 88, len(wb), crc(wb), 88 + len(wb), len(fw), crc(fw))
open("t_wrongdev.bin", "wb").write(hdr + struct.pack("<I", crc(hdr)) + wb + fw)
# oversized firmware with consistent CRCs
bigfw = fw + b"\0" * (0x20000 - len(fw) + 4)
hdr = good[:60] + struct.pack("<6I", 88, len(bit), crc(bit), 88 + len(bit), len(bigfw), crc(bigfw))
open("t_bigfw.bin", "wb").write(hdr + struct.pack("<I", crc(hdr)) + bit + bigfw)
open("t_empty.bin", "wb").write(b"")
open("t_short.bin", "wb").write(good[:50])

flashline = "bit base=00000 len=%u crc=%08x | fw base=c0000 len=%u crc=%08x | no_return=1 | flushed=1 unlinked=1" \
            % (len(cfg), crc(cfg), len(fw), crc(fw))
nbig, nsmall = len(good) // 4096, 1 if len(good) % 4096 > 512 else 0        # how the image is read
nlast = 1 if 0 < len(good) % 4096 <= 512 else 0
k = 300000 // 4096                  # multi-sector reads that work before "failbig=300000"
T = [  # name, args, expected exit code, must contain, must NOT contain
 ("image is read a cluster at a time", ["file=good.bin", "keys=y"], 0,
  ["[reads: %d multi-sector, %d single]" % (nbig + nsmall, nlast), flashline], []),
 ("multi-sector reads never work: single sectors", ["file=good.bin", "keys=y", "failbig=0"], 0,
  ["[reads: 0 multi-sector, %d single]" % ((len(good) + 511) // 512), flashline], ["read of PANOUPD.BIN failed"]),
 ("multi-sector reads fail mid-file: single sectors from there", ["file=good.bin", "keys=y", "failbig=300000"], 0,
  ["[reads: %d multi-sector, %d single]" % (k, (len(good) - k * 4096 + 511) // 512), flashline], ["read of PANOUPD.BIN failed"]),
 ("good image, confirm y",  ["file=good.bin", "keys=y"], 0,
  ["Installed now:", "  firmware:  host-test-1.0, built ", "  bitstream: unknown (it has no build ID)",
   "Update test-1.0 (packaged 2026-11-19 12:00):", fwline,
   "  bitstream: built %s %s (no build ID)" % (bitfields["c"], bitfields["d"]), "Flash: M25P80 detected", flashline], ["same as installed"]),
 ("installed bitstream has a build ID", ["file=good.bin", "keys=n", "fpgaid=%08X" % ID_A], 1,
  ["  bitstream: built %s\n" % when(ID_A), "Cancelled."], ["same as installed", "[flash:"]),
 ("update identical to what is installed", ["file=withid.bin", "keys=n", "fpgaid=%08X" % ID_A], 1,
  ["  firmware:  host-test-1.0, built ", "(same as installed)\n  bitstream: built %s  (same as installed)" % when(ID_A)], ["[flash:"]),
 ("update bitstream differs from installed", ["file=withid.bin", "keys=n", "fpgaid=%08X" % ID_B], 1,
  ["  bitstream: built %s\n" % when(ID_B), "  bitstream: built %s\n" % when(ID_A)], ["UTC  (same as installed)", "[flash:"]),
 ("flash does not start like a bitstream", ["file=good.bin", "keys=y", "flash0=00090ff00ff00ff0"], 1,
  ["starts with 00 09 0f f0 0f f0 0f f0", "Nothing has been written"], ["[unlink", "[flash:", "Proceed?"]),
 ("good image, confirm Y",  ["file=good.bin", "keys=Y"], 0, [flashline], []),
 ("good image, answer n",   ["file=good.bin", "keys=n"], 1, ["Cancelled."], ["[unlink", "[flash:"]),
 ("good image, ESC",        ["file=good.bin", "keys=\x1b"], 1, ["Cancelled."], ["[unlink", "[flash:"]),
 ("key wait polls USB",     ["file=good.bin", "keys="], 3, ["would wait forever"], ["[unlink", "[flash:"]),
 ("no file on stick",       [], 1, ["No PANOUPD.BIN found"], ["[flash:"]),
 ("firmware-only image",    ["file=fwonly.bin", "keys=y"], 0, ["bit base=00000 len=0 crc=00000000 | fw base=c0000 len=%u" % len(fw), "  bitstream: not included"], ["starts with"]),
 ("bitstream-only image",   ["file=bitonly.bin", "keys=y"], 0, ["bit base=00000 len=%u" % len(cfg), "fw base=c0000 len=0 ", "  firmware:  not included"], []),
 ("bad magic",              ["file=t_magic.bin", "keys=y"], 1, ["not a Pano update image"], ["[unlink", "[flash:"]),
 ("header bit flipped",     ["file=t_hdrcrc.bin", "keys=y"], 1, ["header CRC mismatch"], ["[unlink", "[flash:"]),
 ("future header version",  ["file=t_hver.bin", "keys=y"], 1, ["Unsupported update header version 2"], ["[unlink", "[flash:"]),
 ("bitstream bit flipped",  ["file=t_bitcrc.bin", "keys=y"], 1, ["Bitstream CRC mismatch"], ["[unlink", "[flash:"]),
 ("firmware bit flipped",   ["file=t_fwcrc.bin", "keys=y"], 1, ["Firmware CRC mismatch"], ["[unlink", "[flash:"]),
 ("truncated file",         ["file=t_trunc.bin", "keys=y"], 1, ["bad offsets/sizes"], ["[unlink", "[flash:"]),
 ("bitstream offset wild",  ["file=t_bitoff.bin", "keys=y"], 1, ["bad offsets/sizes"], ["[unlink", "[flash:"]),
 ("firmware offset wild",   ["file=t_fwoff.bin", "keys=y"], 1, ["bad offsets/sizes"], ["[unlink", "[flash:"]),
 ("firmware length wraps",  ["file=t_fwlenwrap.bin", "keys=y"], 1, ["bad offsets/sizes"], ["[unlink", "[flash:"]),
 ("offset inside header",   ["file=t_hdroverlap.bin", "keys=y"], 1, ["bad offsets/sizes"], ["[unlink", "[flash:"]),
 ("wrong-FPGA bitstream",   ["file=t_wrongdev.bin", "keys=y"], 1, ["wrong device (part: 3s500e", "refusing to flash"], ["[unlink", "[flash:"]),
 ("firmware too large",     ["file=t_bigfw.bin", "keys=y"], 1, ["bad offsets/sizes"], ["[unlink", "[flash:"]),
 ("empty file",             ["file=t_empty.bin", "keys=y"], 1, ["Could not read"], ["[unlink", "[flash:"]),
 ("50-byte file",           ["file=t_short.bin", "keys=y"], 1, ["not a Pano update image"], ["[unlink", "[flash:"]),
 ("USB read error mid-file",["file=good.bin", "keys=y", "failread=300000"], 1, ["read of PANOUPD.BIN failed", "Could not read"], ["[unlink", "[flash:"]),
 ("flash does not identify",["file=good.bin", "keys=y", "id=ffffff"], 1, ["did not identify (ID ffffff", "Nothing has been written"], ["[unlink", "[flash:", "Proceed?"]),
 ("flasher pre-flight fails",["file=good.bin", "keys=y", "flashret=2"], 1, [flashline, "error 2", "nothing was written"], []),
]
fails = 0
for name, args, rc, must, mustnot in T:
    code, out = run(*args)
    ok = code == rc and all(m in out for m in must) and not any(m in out for m in mustnot) \
         and "ERROR: AddressSanitizer" not in out and "runtime error" not in out
    print("%-4s %s" % ("ok" if ok else "FAIL", name))
    if not ok:
        fails += 1; print("     exit=%d expected %d\n     " % (code, rc) + out.replace("\n", "\n     ")[-900:])
print("%d/%d passed" % (len(T) - fails, len(T)))
# the builder's own verifier must agree
for f, want in (("good.bin", 0), ("t_bitcrc.bin", 1), ("t_wrongdev.bin", 1), ("t_hdrcrc.bin", 1)):
    r = subprocess.run(MK + ["--info", f], capture_output=True, text=True)
    print("mkupdate --info %-15s -> %s" % (f, "ok" if r.returncode == want else "UNEXPECTED rc=%d" % r.returncode))
sys.exit(fails != 0)

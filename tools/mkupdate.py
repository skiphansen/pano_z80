#!/usr/bin/env python3
"""Build or inspect a Pano Z80 combined USB update image (PANOUPD.BIN).

A PANOUPD.BIN carries the FPGA bitstream and/or the RISC-V firmware behind a
small header.  Copy it to the root of the USB drive and choose "Update" in the
Pano's setup menu (F12) to install it.

The two parts identify themselves: the firmware contains an info block with its
version and build date (fw/firmware/fwinfo.h), and a bitstream built with
xilinx/build.tcl has its build time in the .bit header as the UserID.  The
updater shows those next to what is installed; --info shows them here.

    mkupdate.py -o PANOUPD.BIN --bit pano_top.bit --fw firmware.bin \\
                [--version STR] [--date STR]
    mkupdate.py --info PANOUPD.BIN

Header layout (88 bytes, little-endian, no padding) - must match
fw/firmware/flash_update.c:

    off  size  field
      0     8  magic "PANO-UPD"
      8     4  header format version (= 1)
     12    24  label for the image (NUL padded) - by default the firmware version
     36    24  date the image was packaged (NUL padded)
     60     4  bitstream offset in file
     64     4  bitstream length (0 = none)
     68     4  bitstream CRC-32
     72     4  firmware offset in file
     76     4  firmware length (0 = none)
     80     4  firmware CRC-32
     84     4  header CRC-32 over bytes [0..84)
"""
import argparse
import datetime
import re
import struct
import subprocess
import sys
import zlib

MAGIC = b"PANO-UPD"
HDR_VERSION = 1
HDR_SIZE = 88
STR_LEN = 24
FLASH_BIT_MAXSIZE = 0x000C0000   # 768 KB: flash sectors 0..11
FLASH_FW_SIZE = 0x00020000       # 128 KB: flash sectors 12..13
PART = b"3s1600e"                # Pano G1 FPGA (xc3s1600e)
SYNC = b"\xaa\x99\x55\x66"


def crc(data):
    return zlib.crc32(data) & 0xFFFFFFFF


FW_MAGIC = b"PANOFWID"            # struct fw_info in fw/firmware/fwinfo.h


def fw_ident(fw):
    """(version, built) from the firmware's embedded info block, or None."""
    i = fw.find(FW_MAGIC)
    if i < 0 or i + 8 + 2 * STR_LEN > len(fw):
        return None
    version = fw[i + 8:i + 8 + STR_LEN].split(b"\0")[0].decode("ascii", "replace")
    built = fw[i + 8 + STR_LEN:i + 8 + 2 * STR_LEN].split(b"\0")[0].decode("ascii", "replace")
    return version, built


def bit_ident(bit):
    """Describe a .bit file: part, build date from its header, and the build
    ID (the bitgen UserID that xilinx/build.tcl sets to the build time)."""
    part, fields, _ = parse_bit(bit)
    text = lambda k: fields[k].rstrip(b"\0").decode("ascii", "replace")
    out = "%s, built %s %s" % (part, text("c"), text("d"))
    m = re.search(rb"UserID=0x([0-9A-Fa-f]{8})", fields["a"])
    uid = int(m.group(1), 16) if m else 0xFFFFFFFF
    if uid not in (0, 0xFFFFFFFF):
        when = datetime.datetime.fromtimestamp(uid, datetime.timezone.utc)
        out += ", build ID %08X = %s UTC" % (uid, when.strftime("%Y-%m-%d %H:%M"))
    else:
        out += ", no build ID"
    return out


def check_bitstream(bit):
    """Same checks the firmware makes; returns the part name."""
    return parse_bit(bit)[0]


def parse_bit(bit):
    """-> (part name, header fields, length of the configuration data)"""
    if len(bit) < 128 or bit[0:2] != b"\x00\x09":
        raise ValueError("not a Xilinx .bit file (bad header)")
    p = 2 + struct.unpack(">H", bit[0:2])[0] + 2
    fields = {}
    for key in b"abcd":
        if bit[p] != key:
            raise ValueError("malformed .bit header (expected field '%c')" % key)
        n = struct.unpack(">H", bit[p + 1:p + 3])[0]
        fields[chr(key)] = bit[p + 3:p + 3 + n]
        p += 3 + n
    if bit[p] != ord("e"):
        raise ValueError("malformed .bit header (no data field)")
    dlen = struct.unpack(">I", bit[p + 1:p + 5])[0]
    p += 5
    if p + dlen != len(bit):
        raise ValueError(".bit data length does not match file size")
    if PART not in fields["b"]:
        raise ValueError("bitstream is for %r, not the Pano G1's xc%s"
                         % (fields["b"].rstrip(b"\0").decode("ascii", "replace"),
                            PART.decode()))
    if SYNC not in bit[p:p + 64]:
        raise ValueError(".bit sync word not found")
    # only the configuration data is written to the flash, not the header
    if dlen > FLASH_BIT_MAXSIZE:
        raise ValueError("bitstream is %d bytes, larger than the %d byte flash "
                         "region" % (dlen, FLASH_BIT_MAXSIZE))
    return fields["b"].rstrip(b"\0").decode("ascii", "replace"), fields, dlen


def check_firmware(fw):
    if len(fw) < 16:
        raise ValueError("firmware image is implausibly small")
    if len(fw) > FLASH_FW_SIZE:
        raise ValueError("firmware is %d bytes, larger than the %d byte flash "
                         "region" % (len(fw), FLASH_FW_SIZE))


def field(text, what):
    raw = text.encode("ascii", "replace")
    if len(raw) > STR_LEN:
        raise ValueError("%s %r is longer than %d characters" % (what, text, STR_LEN))
    return raw.ljust(STR_LEN, b"\0")


def default_version():
    try:
        out = subprocess.run(["git", "describe", "--always", "--dirty"],
                             capture_output=True, text=True, check=True).stdout.strip()
        return out[:STR_LEN] or "unknown"
    except Exception:
        return "unknown"


def build(args):
    bit = open(args.bit, "rb").read() if args.bit else b""
    fw = open(args.fw, "rb").read() if args.fw else b""
    if not bit and not fw:
        raise ValueError("need --bit and/or --fw")
    if bit:
        check_bitstream(bit)
    if fw:
        check_firmware(fw)
    ident = fw_ident(fw) if fw else None
    version = args.version or (ident[0] if ident else default_version())
    # UTC, like the build times of the firmware and the bitstream it is shown with
    date = args.date or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    bit_off = HDR_SIZE if bit else 0
    fw_off = HDR_SIZE + len(bit) if fw else 0
    hdr = (MAGIC + struct.pack("<I", HDR_VERSION) + field(version, "version")
           + field(date, "date")
           + struct.pack("<6I", bit_off, len(bit), crc(bit) if bit else 0,
                         fw_off, len(fw), crc(fw) if fw else 0))
    assert len(hdr) == HDR_SIZE - 4
    hdr += struct.pack("<I", crc(hdr))
    with open(args.output, "wb") as f:
        f.write(hdr + bit + fw)
    print("%s: %r, packaged %r" % (args.output, version, date))
    if bit:
        print("  bitstream: %d bytes (%s)" % (len(bit), bit_ident(bit)))
    if fw:
        print("  firmware:  %d bytes (%s)" % (len(fw), "%s, built %s" % ident if ident
                                              else "no version information"))


def info(path):
    d = open(path, "rb").read()
    if len(d) < HDR_SIZE or d[0:8] != MAGIC:
        raise ValueError("not a Pano update image")
    hver, = struct.unpack("<I", d[8:12])
    version = d[12:36].split(b"\0")[0].decode("ascii", "replace")
    date = d[36:60].split(b"\0")[0].decode("ascii", "replace")
    bit_off, bit_len, bit_crc, fw_off, fw_len, fw_crc, hdr_crc = \
        struct.unpack("<7I", d[60:88])
    ok = True

    def verdict(good):
        nonlocal ok
        ok = ok and good
        return "OK" if good else "BAD"

    print("file:       %s (%d bytes)" % (path, len(d)))
    print("header:     format %d, CRC %s"
          % (hver, verdict(hver == HDR_VERSION and crc(d[0:84]) == hdr_crc)))
    print("label:      %s" % version)
    print("packaged:   %s" % date)

    def describe_fw(blob):
        check_firmware(blob)
        ident = fw_ident(blob)
        return "%s, built %s" % ident if ident else "no version information"

    for name, off, length, want, check in (
            ("bitstream", bit_off, bit_len, bit_crc, bit_ident),
            ("firmware", fw_off, fw_len, fw_crc, describe_fw)):
        if not length:
            print("%-11s none" % (name + ":"))
            continue
        blob = d[off:off + length]
        good = off >= HDR_SIZE and len(blob) == length and crc(blob) == want
        note = ""
        if good:
            try:
                r = check(blob)
                note = (" (%s)" % r) if r else ""
            except ValueError as e:
                good = False
                note = " (%s)" % e
        print("%-11s %d bytes at offset %d, CRC %s%s"
              % (name + ":", length, off, verdict(good), note))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--info", metavar="IMAGE", help="verify and describe an image")
    ap.add_argument("-o", "--output", default="PANOUPD.BIN")
    ap.add_argument("--bit", help="FPGA bitstream (.bit)")
    ap.add_argument("--fw", help="firmware.bin")
    ap.add_argument("--version", help="label for the image (default: the firmware's own version)")
    ap.add_argument("--date", help="packaging date string (default: now, in UTC)")
    args = ap.parse_args()
    try:
        if args.info:
            return 0 if info(args.info) else 1
        build(args)
        return 0
    except (ValueError, OSError) as e:
        print("mkupdate: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

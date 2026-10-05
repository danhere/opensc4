#!/usr/bin/env python3
"""Extract SimCity 4's built-in ID-to-name table.

The Windows 1.1.641 exe carries a debug table of {uint32 id, const char* name}
entries in .data, naming class IDs (cSC4PollutionSimulator, ...), message IDs
and other constants. It is the only source of class names in the Windows exe,
which has no RTTI for game classes.

The table is found by scanning .data for the longest run of 8-byte entries whose
second dword points to an identifier string. The pairing direction was settled
against gzcom-dll's message constants (kCanvasMessageType, kMouseMessageType).

Usage: extract_name_table.py "SimCity 4.exe" > name-table.csv

The output describes the game and must not be committed.
"""

import re
import struct
import sys

IDENTIFIER = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{2,99}")


class PeImage:
    def __init__(self, data: bytes):
        self.data = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.sections = {}
        for i in range(count):
            o = pe + 24 + optional_size + 40 * i
            name = data[o:o + 8].rstrip(b"\0").decode()
            _vsize, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, o + 8)
            self.sections[name] = (image_base + rva, raw_offset, raw_size)

    def offset(self, va: int):
        for va_start, raw_offset, raw_size in self.sections.values():
            if va_start <= va < va_start + raw_size:
                return raw_offset + va - va_start
        return None

    def identifier_at(self, va: int):
        o = self.offset(va)
        if o is None:
            return None
        end = self.data.find(b"\0", o, o + 101)
        if end <= o or not IDENTIFIER.fullmatch(self.data[o:end]):
            return None
        return self.data[o:end].decode()


def extract(image: PeImage):
    """Returns [(id, name)] for the longest {id, name pointer} run in .data."""
    va_start, raw_offset, raw_size = image.sections[".data"]
    best = []
    for phase in range(0, 8, 4):
        run = []
        for o in range(raw_offset + phase, raw_offset + raw_size - 8, 8):
            ident, pointer = struct.unpack_from("<II", image.data, o)
            name = image.identifier_at(pointer)
            if name:
                run.append((ident, name))
                continue
            if len(run) > len(best):
                best = run
            run = []
        if len(run) > len(best):
            best = run
    return best


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = PeImage(open(sys.argv[1], "rb").read())
    entries = extract(image)
    print("id,name")
    for ident, name in sorted(entries):
        print(f"0x{ident:08X},{name}")
    print(f"{len(entries)} entries", file=sys.stderr)


if __name__ == "__main__":
    main()

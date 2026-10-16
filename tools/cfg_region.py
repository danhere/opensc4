#!/usr/bin/env python3
"""Make the game open another region: copy SimCity 4.cfg with the name of the
last region changed (the game opens the region it remembers there).

    cfg_region.py <in SimCity 4.cfg> <out .cfg> <region folder name>

The cfg is a DBPF 1.0 package with one record (A9DD6E06/49DD6E08/00000001,
QFS-compressed, 5,942 bytes) whose zero-padded name field at +0xEBE holds the
region's folder name (char[64]). The copy holds that record uncompressed and
no compression directory; the game reads it as is and rewrites it on exit.
Used by scripts/run-save-test.sh for regions other than Timbuktu.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 're'))
import savediff  # noqa: E402  (package reader: load())

PREFS = (0xA9DD6E06, 0x49DD6E08, 0x00000001)
NAME_AT, NAME_SIZE = 0xEBE, 64


def main():
    if len(sys.argv) != 4:
        print(__doc__.strip().splitlines()[3].strip(), file=sys.stderr)
        return 2
    src, dst, region = sys.argv[1:]
    raw = open(src, 'rb').read()
    records = savediff.load(src)
    if PREFS not in records or len(records[PREFS]) <= NAME_AT + NAME_SIZE:
        print('cfg_region: no preferences record in %s' % src, file=sys.stderr)
        return 1
    name = region.encode('mbcs' if os.name == 'nt' else 'latin-1')
    if len(name) >= NAME_SIZE:
        print('cfg_region: region name too long: %s' % region, file=sys.stderr)
        return 1
    data = bytearray(records[PREFS])
    data[NAME_AT:NAME_AT + NAME_SIZE] = name.ljust(NAME_SIZE, b'\0')
    header = bytearray(raw[:0x60])
    index_at = 0x60 + len(data)
    struct.pack_into('<III', header, 0x24, 1, index_at, 20)
    with open(dst, 'wb') as out:
        out.write(header)
        out.write(data)
        out.write(struct.pack('<5I', *PREFS, 0x60, len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main())

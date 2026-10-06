#!/usr/bin/env python3
"""Show where two saved cities differ inside records, word by word.

    savediff.py <a.sc4> <b.sc4> [type ...]

Uses opensc4-dbpf to extract nothing: reads the packages itself (DBPF 1.0,
index 7, QFS) so it can run anywhere Python does. For each record present in
both with the same size but different bytes, prints the differing 32-bit
words (offset, A, B), up to 12 per record. Records of the listed types only,
if any are given.
"""
import struct
import sys

DIR = (0xE86B1EEF, 0xE86B1EEF, 0x286B1F03)


def refpack(data, size):
    out = bytearray()
    i = 0
    while True:
        b0 = data[i]
        if b0 < 0x80:
            b1 = data[i + 1]
            lit, copy, off, n = b0 & 3, ((b0 & 0x1C) >> 2) + 3, ((b0 & 0x60) << 3) + b1 + 1, 2
        elif b0 < 0xC0:
            b1, b2 = data[i + 1], data[i + 2]
            lit, copy, off, n = (b1 >> 6) & 3, (b0 & 0x3F) + 4, ((b1 & 0x3F) << 8) + b2 + 1, 3
        elif b0 < 0xE0:
            b1, b2, b3 = data[i + 1], data[i + 2], data[i + 3]
            lit, copy, off, n = b0 & 3, ((b0 & 0x0C) << 6) + b3 + 5, ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1, 4
        elif b0 < 0xFC:
            lit, copy, off, n = ((b0 & 0x1F) << 2) + 4, 0, 0, 1
        else:
            lit, copy, off, n = b0 & 3, 0, 0, 1
        i += n
        out += data[i:i + lit]
        i += lit
        for _ in range(copy):
            out.append(out[-off])
        if b0 >= 0xFC:
            break
    assert len(out) == size
    return bytes(out)


def load(path):
    raw = open(path, 'rb').read()
    count, ioff = struct.unpack_from('<II', raw, 0x24)
    entries = {}
    for k in range(count):
        t, g, i, off, size = struct.unpack_from('<5I', raw, ioff + 20 * k)
        entries[(t, g, i)] = raw[off:off + size]
    packed = {}
    if DIR in entries:
        d = entries[DIR]
        for k in range(0, len(d), 16):
            t, g, i, usize = struct.unpack_from('<4I', d, k)
            packed[(t, g, i)] = usize
    out = {}
    for key, data in entries.items():
        if key == DIR:
            continue
        out[key] = refpack(data[9:], packed[key]) if key in packed else data
    return out


def pointerlike(v):
    # Heap addresses in a 32-bit process; small integers and flags are not.
    return 0x00100000 <= v < 0x80000000


def pointer_diff(a, b):
    """Explains differences as heap addresses that moved between the runs.

    Every differing byte must lie inside a 4-byte window (aligned or not) whose
    values in A and B both look like addresses and map A->B consistently
    across the whole save (one-to-one). Bytes 4-7 of each record (its CRC) are
    excluded. Prints what is left unexplained."""
    forward, backward = {}, {}

    def consistent(va, vb):
        return forward.get(va, vb) == vb and backward.get(vb, va) == va

    total_unexplained = 0
    unexplained_records = []
    for key in sorted(a):
        if key not in b or a[key] == b[key]:
            continue
        da, db = a[key], b[key]
        if len(da) != len(db):
            unexplained_records.append((key, 'size %d vs %d' % (len(da), len(db))))
            continue
        covered = bytearray(len(da))
        covered[4:8] = b'\x01\x01\x01\x01'
        for p in range(len(da)):
            if da[p] == db[p] or covered[p]:
                continue
            for start in range(max(0, p - 3), min(p, len(da) - 4) + 1):
                va = struct.unpack_from('<I', da, start)[0]
                vb = struct.unpack_from('<I', db, start)[0]
                if pointerlike(va) and pointerlike(vb) and consistent(va, vb):
                    forward[va], backward[vb] = vb, va
                    covered[start:start + 4] = b'\x01\x01\x01\x01'
                    break
        # Embedded objects repeat the record header: size, CRC, own address.
        # A differing word right before a matched address, after an equal
        # size word, is such a CRC.
        for s in range(8, len(da) - 3):
            if (covered[s] and not covered[s - 1] and da[s - 4:s] != db[s - 4:s] and
                    da[s - 8:s - 4] == db[s - 8:s - 4] and forward.get(struct.unpack_from('<I', da, s)[0]) ==
                    struct.unpack_from('<I', db, s)[0]):
                covered[s - 4:s] = b'\x01\x01\x01\x01'
        left = [p for p in range(len(da)) if da[p] != db[p] and not covered[p]]
        if left:
            total_unexplained += len(left)
            unexplained_records.append((key, '%d bytes unexplained, first at %d' % (len(left), left[0])))
    print('%d addresses mapped between the saves' % len(forward))
    for key, what in unexplained_records:
        print('%08X %08X %08X  %s' % (*key, what))
    print('%d records with differences not explained by moved addresses (%d bytes)' %
          (len(unexplained_records), total_unexplained))


def main():
    if sys.argv[1] == '--pointers':
        pointer_diff(load(sys.argv[2]), load(sys.argv[3]))
        return
    a, b = load(sys.argv[1]), load(sys.argv[2])
    types = {int(t, 16) for t in sys.argv[3:]}
    for key in sorted(a):
        if types and key[0] not in types:
            continue
        if key not in b or a[key] == b[key] or len(a[key]) != len(b[key]):
            continue
        da, db = a[key], b[key]
        words = [o for o in range(0, len(da) - 3, 4) if da[o:o + 4] != db[o:o + 4]]
        print('%08X %08X %08X  %d bytes, %d words differ' % (*key, len(da), len(words)))
        for o in words[:12]:
            print('    +%-6d %08X  %08X' % (o, struct.unpack_from('<I', da, o)[0], struct.unpack_from('<I', db, o)[0]))


if __name__ == '__main__':
    main()

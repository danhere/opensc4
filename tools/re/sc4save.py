#!/usr/bin/env python3
"""Decode saved-city records whose layout is known (P4 save schemas).

    sc4save.py <city.sc4> [type ...]       decode records (all known types by default)
    sc4save.py --check <city.sc4|folder...> decode every known record; report any
                                           that do not consume exactly their bytes
    sc4save.py --compare <a.sc4> <b.sc4>   compare the known records of two saves field
                                           by field (exit 1 if any differ)

Schemas come from the classes' own cIGZSerializable::Write functions and are
documented in docs/save-format.md. Every record starts with a header written by
the save framework: size (uint32, the whole record), CRC (uint32) and the
object's address in memory when saved (uint32, its ID for references), then the
class's own fields. References to other serialised objects are written as that
object's address-ID and class ID (8 bytes).
"""
import glob
import os
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0] if '/' in __file__ else '.')
import savediff  # noqa: E402  (package reader: load())
from save_schemas import SCHEMAS, Reader, header  # noqa: E402,F401  (one module per simulator)


def decode(data, decoder):
    r = Reader(data)
    fields = decoder(r)
    return fields, r.pos


def comparable(value):
    """What two runs can agree on: references (the only tuples) keep whether
    they are set and the class, not the heap address they were saved as."""
    if isinstance(value, tuple):
        return (value[0] != 0, value[1])
    if isinstance(value, list):
        return [comparable(v) for v in value]
    if isinstance(value, dict):
        return {k: comparable(v) for k, v in value.items()}
    return value


def differences(a, b, path=''):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in list(a) + [k for k in b if k not in a]:
            yield from differences(a.get(k), b.get(k), '%s.%s' % (path, k) if path else str(k))
    elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        bad = [i for i in range(len(a)) if a[i] != b[i]]
        if bad:
            i = bad[0]
            yield '%s: %d of %d entries differ, first [%d] %r vs %r' % (path, len(bad), len(a), i, a[i], b[i])
    elif a != b:
        ta, tb = repr(a), repr(b)
        yield '%s: %s vs %s' % (path, ta[:100], tb[:100])


def compare(path_a, path_b):
    """One line per known record type; the header's CRC and ID are skipped
    (records with no framework header, like the regional city's, have neither)."""
    found = 0
    decoded = []
    for path in (path_a, path_b):
        records = {}
        for key, data in savediff.load(path).items():
            if key[0] in SCHEMAS:
                fields, _ = decode(data, SCHEMAS[key[0]][1])
                fields.pop('crc', None), fields.pop('id', None)
                records[key] = comparable(fields)
        decoded.append(records)
    a, b = decoded
    for key in sorted(a.keys() | b.keys()):
        name = SCHEMAS[key[0]][0]
        if key not in a or key not in b:
            print('%s %08X/%08X/%08X: only in %s' % (name, *key, 'a' if key in a else 'b'))
            found += 1
            continue
        diffs = list(differences(a[key], b[key]))
        print('%s %08X/%08X/%08X: %s' % (name, *key, 'identical' if not diffs else '%d fields differ' % len(diffs)))
        for line in diffs:
            print('  ' + line)
        found += bool(diffs)
    return 1 if found else 0


def main():
    args = sys.argv[1:]
    if args and args[0] == '--compare':
        return compare(args[1], args[2])
    if args and args[0] == '--check':
        bad = 0
        paths = []
        for arg in args[1:]:  # folders are searched for .sc4 files
            paths += sorted(glob.glob(os.path.join(arg, '**', '*.sc4'), recursive=True)) if os.path.isdir(arg) else [arg]
        for path in paths:
            for key, data in savediff.load(path).items():
                if key[0] not in SCHEMAS:
                    continue
                name, decoder = SCHEMAS[key[0]]
                try:
                    _, used = decode(data, decoder)
                    ok = used == len(data)
                except struct.error:
                    used, ok = -1, False
                bad += not ok
                print('%s %s %s: %d of %d bytes' % ('ok  ' if ok else 'FAIL', path.rsplit('\\', 1)[-1], name, used,
                                                    len(data)))
        return 1 if bad else 0
    records = savediff.load(args[0])
    types = {int(t, 16) for t in args[1:]} or set(SCHEMAS)
    for key, data in sorted(records.items()):
        if key[0] in types and key[0] in SCHEMAS:
            name, decoder = SCHEMAS[key[0]]
            fields, used = decode(data, decoder)
            print('%s %08X/%08X/%08X (%d of %d bytes)' % (name, *key, used, len(data)))
            for k, v in fields.items():
                text = repr(v)
                print('  %-10s %s' % (k, text if len(text) < 200 else text[:200] + '...'))
    return 0


if __name__ == '__main__':
    sys.exit(main())

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


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, fmt):
        value = struct.unpack_from('<' + fmt, self.data, self.pos)
        self.pos += struct.calcsize('<' + fmt)
        return value[0] if len(value) == 1 else value

    def u8(self): return self.take('B')
    def s8(self): return self.take('b')
    def u16(self): return self.take('H')
    def s32(self): return self.take('i')
    def u32(self): return self.take('I')
    def s64(self): return self.take('q')
    def f32(self): return self.take('f')
    def f64(self): return self.take('d')

    def ref(self):
        # A reference to another serialised object (0x005D99A0): its
        # address-ID when saved and its class ID (0, 0 for none).
        return self.take('II')

    def gzstr(self):
        # cIGZIStream::GetGZStr: uint32 length, then the characters.
        n = self.u32()
        text = self.data[self.pos:self.pos + n]
        if len(text) != n:
            raise struct.error('string runs past the record')
        self.pos += n
        return text.decode('latin-1')

    def ref_list(self):
        # 0x005DAFF0: count, then references.
        return [self.ref() for _ in range(self.u32())]


def header(r):
    return {'size': r.u32(), 'crc': r.u32(), 'id': r.u32()}


def pollution_simulator(r):
    """cSC4PollutionSimulator (0x8990C065): Write at 0x006E3380 (version 4),
    Read at 0x006E5270 (versions 2-4). Field names are ours. Offsets in
    the names are relative to the cIGZSerializable part, which sits at object
    offset +0xC (so uf8 is the object's +0x104)."""
    out = header(r)
    out['version'] = v = r.u16()
    if not 2 <= v <= 4:
        raise struct.error('version %d not known' % v)
    out['grids_34'] = [r.ref() for _ in range(4)]      # this+0x34: 4 grid objects
    out['grids_44'] = [r.ref() for _ in range(4)]      # this+0x44: 4 grid objects
    out['u54'] = r.u32()
    out['u58'] = r.u32()
    out['lists_5c'] = [r.ref_list() for _ in range(4)]  # this+0x5c..0x68: 4 lists of objects
    out['uf0'] = r.u32()
    out['avg_air'], out['avg_water'] = r.u32(), r.u32()  # object +0x100, +0x104 (getters)
    out['sfc_110'] = [r.s32() for _ in range(6)]       # this+0xfc..0x110
    out['u114'] = r.u32()
    if v < 3:
        out['str_v2'] = r.gzstr()  # read and dropped by versions below 3
    out['avg_garbage'] = r.u16()  # object +0x128 (GetAverageGarbageValue)
    out['u120'], out['u124'] = r.u32(), r.u32()
    out['f128'] = r.f32()
    out['b12c'] = r.u8()
    out['u130'] = r.u32()
    out['q1b4'], out['q1bc'] = r.s64(), r.s64()
    if v > 3:
        out['u1ac'] = r.u32()
    out['q16c'] = [r.s64() for _ in range(3)]
    out['d184'] = r.f64()
    out['q18c'] = [r.s64() for _ in range(3)]
    out['q134'] = [r.s64() for _ in range(3)]
    out['d14c'] = r.f64()
    out['q154'] = [r.s64() for _ in range(3)]
    out['u1a8'] = r.u32()
    out['s1a4'] = r.s32()
    out['u1b0'], out['u1c8'] = r.u32(), r.u32()
    # this+0x1cc, written by 0x005DAB70
    s = {'a': r.s8(), 'b': r.s8(), 'c': r.u8(), 'ints': [r.s32() for _ in range(5)]}
    s['values'] = [r.u16() for _ in range(r.u32())]
    s['tail'] = [r.s32() for _ in range(3)]
    out['s1cc'] = s
    out['flags_1fc'] = [r.u8() for _ in range(4)]
    if v < 3:
        out['u_v2'] = r.u32()  # read and dropped by versions below 3
    out['f21c'] = r.f32()
    out['s220'], out['s224'] = r.s32(), r.s32()
    out['ref_cc'] = r.ref()
    return out


SCHEMAS = {0x8990C065: ('cSC4PollutionSimulator', pollution_simulator)}


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
            yield from differences(a.get(k), b.get(k), path + '.' + k if path else k)
    elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        bad = [i for i in range(len(a)) if a[i] != b[i]]
        if bad:
            i = bad[0]
            yield '%s: %d of %d entries differ, first [%d] %r vs %r' % (path, len(bad), len(a), i, a[i], b[i])
    elif a != b:
        ta, tb = repr(a), repr(b)
        yield '%s: %s vs %s' % (path, ta[:100], tb[:100])


def compare(path_a, path_b):
    """One line per known record type; the header's CRC and ID are skipped."""
    found = 0
    decoded = []
    for path in (path_a, path_b):
        records = {}
        for key, data in savediff.load(path).items():
            if key[0] in SCHEMAS:
                fields, _ = decode(data, SCHEMAS[key[0]][1])
                del fields['crc'], fields['id']
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

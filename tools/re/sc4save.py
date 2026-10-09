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
    def s16(self): return self.take('h')
    def s32(self): return self.take('i')
    def u32(self): return self.take('I')
    def s64(self): return self.take('q')
    def f32(self): return self.take('f')
    def f64(self): return self.take('d')

    def ref(self):
        # A reference to another serialised object (0x005D99A0): its
        # address-ID when saved and its class ID; none is a single 0.
        address = self.u32()
        return (address, self.u32()) if address else (0, 0)

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


def power_simulator(r):
    """cSC4PowerSimulator (0x0990C005): Write at 0x006EB6A0 (version 6),
    Read at 0x006F0880 (versions up to 6). Names give the object offsets;
    the cIGZSerializable part sits at +0x8 (docs/power.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 6:
        raise struct.error('version %d not known' % v)
    out['department_20'] = r.ref()
    out['month_38'], out['produced_3c'], out['needed_40'] = r.s32(), r.s32(), r.s32()
    out['ratio_64'] = r.f32() if v >= 6 else r.s32()
    out['consumers_68_74'] = [r.s32() for _ in range(4)]
    out['demand_total_78'] = r.s64()
    out['monthly_50_60'] = [r.s32() for _ in range(5)]
    out['supply_grid_80'] = r.ref()
    rows, columns = r.u32(), r.u32()
    out['consumption_84'] = [[r.take('h') for _ in range(columns)] for _ in range(rows)]
    out['usage_90'] = [r.s32() for _ in range(120)]
    out['annual_270'] = [r.s32() for _ in range(10)]
    out['plants_2dc'] = n = r.s32()
    out['plants_by_type_2e0'] = [r.s32() for _ in range(10)]
    out['plant_list_298'] = [{'state': r.u32(), 'plant': r.ref()} for _ in range(max(n, 0))]
    out['pieces_2b8'] = n = r.u32()
    out['on_2bc'] = r.u32()
    out['pieces_29c'] = [r.ref() for _ in range(n)]
    words = r.u32()
    out['bits_2c4_2c8_2d4_2d0'] = [[r.u32() for _ in range(words)] for _ in range(4)]
    out['b2d8'], out['b330'] = r.u8(), r.u8()
    out['s334'] = r.s32()
    out['dirty_338'], out['b339'] = r.u8(), r.u8()
    out['state_34c'] = r.u32()
    return out


def water_simulator(r):
    """cSC4PlumbingSimulator (0x0990C075): Write at 0x006DB090, Read at
    0x006DB620 (version 4 only). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/water.md)."""
    def producer():
        return {'cell': (r.s32(), r.s32()), 'lot': [r.s32() for _ in range(4)], 'capacity': r.u32(),
                'output': r.u32(), 'building': r.ref()}
    out = header(r)
    out['version'] = v = r.u16()
    if v != 4:
        raise struct.error('version %d not known' % v)
    out['grid_30'], out['grid_34'] = r.ref(), r.ref()
    out['stride_8c'] = stride = r.u32()
    out['height_90'] = height = r.u32()
    out['cells_88'] = [r.u8() for _ in range(stride * height)]
    out['months_9c'], out['pollution_a0'] = r.s32(), r.u32()
    out['usage_148'] = r.s64()
    count = r.u32()
    out['polluted_198'] = r.u32()
    out['producers_bc'] = [producer() for _ in range(count)]
    out['current_140'] = producer()
    out['iterator_144'] = {'state': r.s8(), 'fields': [r.s32() for _ in range(7)]}
    out['demand_130'] = r.s64()
    out['pipes_174'] = pipes = r.u32()
    out['distressed_178'], out['burst_17c'] = r.u32(), r.u32()
    out['pipes_170'] = [r.ref() for _ in range(pipes)]
    out['department_a4'] = r.ref()
    out['totals_110'] = [r.s64() for _ in range(4)]
    return out


def aura_simulator(r):
    """cSC4AuraSimulator (0x0990BDD8): Write at 0x0068CC50 (version 9), Read
    at 0x0068E340 (versions 8 and 9). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/aura.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v not in (8, 9):
        raise struct.error('version %d not known' % v)
    for name in ('aura_40', 'park_map_58', 'park_sources_54', 'smoothed_48', 'transient_4c', 'landmark_sources_50',
                 'landmark_map_68'):
        out[name] = r.ref()
    if v < 9:
        out['old_grid'] = r.ref()
    else:
        out['departments_5c'] = [[r.u32(), r.ref()] for _ in range(r.u32())]
    out['flag_80'], out['rating_84'] = r.u32(), r.s32()
    out['best_148'] = [r.s32(), r.s32(), r.s16()]
    out['worst_154'] = [r.s32(), r.s32(), r.s16()]
    out['clusters_d0'] = [[r.s32(), r.s32(), r.s32()] for _ in range(r.u32())]
    return out


def traffic_simulator(r):
    """cSC4TrafficSimulator (0x6990C1AA): Write at 0x00712400 (version 41).
    Field names are ours; offsets in them are from the object's start (the
    cIGZSerializable part sits at +0x8). See docs/traffic.md."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 41:
        raise struct.error('version %d not known' % v)
    out['counts'] = [r.s32() for _ in range(4)]          # 13, 1, 13, 9
    out['grids_30'] = [r.ref() for _ in range(4)]        # +0x30 to +0x3C
    out['grids_44'] = [r.ref() for _ in range(2)]        # +0x44, +0x48
    rows, cols = r.u32(), r.u32()
    out['bytes_4c'] = (rows, cols, [r.u8() for _ in range(rows * cols)])
    out['grids_164'] = [r.ref() for _ in range(r.u32())]
    out['grids_170'] = [r.ref() for _ in range(2)]

    def density():
        return {'key': r.u32(), 'last': [r.u16() for _ in range(16)], 'sum': r.u16(),
                'now': [r.u16() for _ in range(16)]}
    out['density_17c'] = [density() for _ in range(r.u32())]
    out['trips_1c0'] = [(r.u32(), r.u32(), r.u32()) for _ in range(3)]
    out['grids_700'] = [r.ref() for _ in range(2)]
    out['u708'] = r.u32()

    def cost():
        node = {'key': r.u32(), 'ref': r.ref(), 'f': [r.f32() for _ in range(3)],
                'b': [r.u8() for _ in range(3)], 'g': [r.f32() for _ in range(2)]}
        node['parts'] = [(r.u8(), [r.u8() for _ in range(4)], r.u16(), r.u16()) for _ in range(r.u32())]
        node['h'] = r.f32()
        node['c'] = r.u8()
        return node
    out['costs_70c'] = [cost() for _ in range(r.u32())]
    out['departments'] = [r.ref() for _ in range(2)]
    out['traffic_778'] = r.s64()
    out['f780'] = [r.f32() for _ in range(3)]
    out['d790'] = [r.f64() for _ in range(2)]
    out['day_7a0'] = r.u32()
    out['strike_7a4'] = r.u8()
    out['months_7a8'] = r.s32()
    out['first_7ac'] = (r.u32(), r.u32(), r.f32())
    out['set_7b8'] = [r.u16() for _ in range(r.u32())]
    out['set_7cc'] = [r.u16() for _ in range(r.u32())]
    out['second_7e0'] = (r.u32(), r.u32(), r.f32())
    out['set_7ec'] = [r.u16() for _ in range(r.u32())]
    out['set_800'] = [r.u16() for _ in range(r.u32())]
    out['phase_814'] = r.u8()
    out['iterator_818'] = {'state': r.s8(), 'fields': [r.s32() for _ in range(7)]}
    out['pending_81c'] = (r.u8(), [r.s32() for _ in range(4)])
    out['u920'] = r.u8()
    out['u924'] = (r.u32(), r.u32())
    out['cell_90c'] = (r.u16(), r.u16())
    out['iterator_910'] = {'state': (r.u8(), r.u8()), 'fields': [r.s32() for _ in range(7)]}
    out['b880'] = (r.u8(), r.u8())
    out['f864'] = r.f32()
    out['cell_83c'] = (r.s32(), r.s32())
    out['u87c'] = r.u32()
    out['u854'] = [r.u32() for _ in range(4)]
    out['b868'] = r.u8()
    out['u850'] = r.u32()
    out['s86c'] = [r.s32() for _ in range(4)]
    out['u894'] = [r.u32() for _ in range(27)]
    out['b900'] = (r.u8(), r.u8())
    out['parts_914'] = [r.s32() for _ in range(3)]
    out['u92c'] = [r.u32() for _ in range(4)]
    out['counts_93c'] = [r.s32() for _ in range(3)]
    out['u948'] = [r.u32() for _ in range(10)]

    def stats():
        block = {'kinds': [(r.f32(), r.u32(), r.s32(), r.s32(), r.f32()) for _ in range(13)],
                 'other': (r.f32(), r.u32(), r.s32(), r.s32(), r.f32()),
                 'counts': [r.u16() for _ in range(13)], 'w': (r.u16(), r.u16())}
        n = r.s32()
        block['records'] = [(r.u16(), r.f32(), r.s32(), r.s32(), r.f32()) for _ in range(n)]
        block['tail'] = (r.f32(), r.f32(), r.u8(), r.f32(), r.u8())
        return block
    out['stats_1e4'] = stats()
    out['stats_440'] = stats()

    def link():
        return {'key': r.u16(), 'b': (r.u8(), r.u8()), 'w': (r.u16(), r.u16()), 'f': [r.f32() for _ in range(9)]}
    out['links_c8'] = [link() for _ in range(r.u32())]
    out['f420'] = r.f32()
    out['trips_6ec'] = (r.f32(), r.u32(), r.f32(), r.u32())
    out['u740'] = [r.u32() for _ in range(13)]
    out['u1b0'] = [r.u32() for _ in range(r.u32())]
    n = r.u32()
    if n:
        raise struct.error('the map at +0xB8 has %d entries; their objects write themselves (not decoded)' % n)
    out['by_type'] = [(r.u32(), r.u32()) for _ in range(9)]
    out['total_43c'] = r.u32()

    def route():
        out = {'b': [r.u8() for _ in range(7)], 'w': [r.u16() for _ in range(3)], 'c': (r.u8(), r.u8())}
        out['a'] = [{'f': r.f32(), 'p': (r.s16(), r.s16()), 'w': r.u16(), 'bytes': [r.u8() for _ in range(r.u32())],
                     'u': [r.u16() for _ in range(4)]} for _ in range(r.u32())]
        out['b_list'] = []
        for _ in range(r.u32()):
            entry = {'f': r.f32(), 'p': [r.s16() for _ in range(4)]}
            entry['bytes1'] = [r.u8() for _ in range(r.u32())]
            entry['bytes2'] = [r.u8() for _ in range(r.u32())]
            entry['u'] = [r.u16() for _ in range(4)]
            out['b_list'].append(entry)
        return out
    out['routes_104'] = [route() for _ in range(r.u32())]
    return out


SCHEMAS = {0x8990C065: ('cSC4PollutionSimulator', pollution_simulator),
           0x0990C005: ('cSC4PowerSimulator', power_simulator),
           0x0990C075: ('cSC4PlumbingSimulator', water_simulator),
           0x6990C1AA: ('cSC4TrafficSimulator', traffic_simulator),
           0x0990BDD8: ('cSC4AuraSimulator', aura_simulator)}


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

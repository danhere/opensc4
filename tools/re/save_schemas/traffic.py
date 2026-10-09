"""Save decoders of traffic (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


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


SCHEMAS = {0x6990C1AA: ('cSC4TrafficSimulator', traffic_simulator)}

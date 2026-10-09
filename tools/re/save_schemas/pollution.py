"""Save decoders of pollution (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


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

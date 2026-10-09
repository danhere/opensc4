"""Save decoders of flora (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def flora_simulator(r):
    """cSC4FloraSimulator (0xC990C034): Write at 0x006AFEE0, Read at
    0x006AFFE0 (version 2 only). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/flora.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    out['flags_2c'] = [r.u8() for _ in range(4)]
    out['size_30'] = [r.f32(), r.f32()]
    out['cells_38'] = [r.s32(), r.s32()]
    out['u40'] = [r.u32() for _ in range(4)]
    out['b50'] = r.u8()
    # The tract walk at +0x54, as the pollution simulator's (0x005DAB70).
    s = {'a': r.s8(), 'b': r.s8(), 'c': r.u8(), 'ints': [r.s32() for _ in range(5)]}
    s['values'] = [r.u16() for _ in range(r.u32())]
    s['tail'] = [r.s32() for _ in range(3)]
    out['walk_54'] = s
    out['random_88'] = [r.u32(), r.u32(), r.s32(), r.f64()]
    out['dying_a4'] = r.ref_list()
    out['f_a8'] = [r.f32(), r.f32()]
    return out


SCHEMAS = {0xC990C034: ('cSC4FloraSimulator', flora_simulator)}

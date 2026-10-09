"""Save decoders of crime (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def crime_simulator(r):
    """cSC4CrimeSimulator (0x8990C09A): Write at 0x0069F600, Read at
    0x006A05E0 (version 6 only). Names give the object offsets; the
    cIGZSerializable part sits at +0xC (docs/crime.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 6:
        raise struct.error('version %d not known' % v)
    out['grid_5c'], out['grid_60'] = r.ref(), r.ref()
    # The tract walk at +0x18, as the pollution simulator's (0x005DAB70).
    s = {'a': r.s8(), 'b': r.s8(), 'c': r.u8(), 'ints': [r.s32() for _ in range(5)]}
    s['values'] = [r.u16() for _ in range(r.u32())]
    s['tail'] = [r.s32() for _ in range(3)]
    out['walk_18'] = s
    out['u64'], out['u68'] = r.u32(), r.u32()
    out['average_6c'], out['ordinance_8c'] = r.f32(), r.f32()
    out['crimes_7c'] = r.u32()
    out['hotspot_80'] = [r.s32() for _ in range(3)]  # x, z, value (a list: tuples are references)
    out['incidents_70'] = [{'occupant': r.ref(), 'type': r.u32(), 'name': r.gzstr(),
                            'key': [r.u32() for _ in range(3)]} for _ in range(r.s32())]
    return out


SCHEMAS = {0x8990C09A: ('cSC4CrimeSimulator', crime_simulator)}

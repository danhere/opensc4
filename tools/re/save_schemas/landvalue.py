"""Save decoders of landvalue (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def land_value_simulator(r):
    """cSC4LandValueSimulator (0xE990C18E): Write at 0x006BD8D0, Read at
    0x006BD950 (version 1 only). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/landvalue.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['intrinsic_24'], out['total_28'], out['slope_30'] = r.ref(), r.ref(), r.ref()
    out['pending_34'] = r.u8()  # dirty, or the step not finished (state not 9)
    out['average_74'] = r.u8()
    out['altitude_78'] = r.f32()
    return out


SCHEMAS = {0xE990C18E: ('cSC4LandValueSimulator', land_value_simulator)}

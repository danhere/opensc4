"""Save decoder of the seaport developer (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def seaport_developer(r):
    """cSC4SeaportDeveloper (0xEA230A28): Write at 0x0068B3E0 (version 2,
    then the port base's Write 0x006E9850, version 1), Read at 0x007010B0
    (version 2 only) and 0x006EAA60; all get the object + 0xC. Names give
    the object offsets (src/core/seaport_sim.cpp)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    out['base_version'] = b = r.u16()
    if b != 1:
        raise struct.error('port base version %d not known' % b)
    out['id_18'] = r.u32()
    out['value_38'] = r.u16()
    # The port list +0x48: stage (+0x4), the type's index (+0x8 of the type
    # at the port's +0x8) and the lot's cell (+0xE, +0x10).
    out['ports'] = [[r.u32(), r.u16(), r.s16(), r.s16()] for _ in range(r.u16())]
    return out


SCHEMAS = {0xEA230A28: ('cSC4SeaportDeveloper', seaport_developer)}

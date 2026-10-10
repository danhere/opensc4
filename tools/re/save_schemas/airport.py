"""Save decoder of the airport developer (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def port_developer(r, out):
    """The port base's record (cSC4PortDeveloper: Write at 0x006E9850, Read
    at 0x006EAA60, version 1 only), which the airport and the seaport
    developers both write after their own version. Names give the offsets in
    the base part and in a port (src/core/airport_sim.h)."""
    out['port_version'] = v = r.u16()
    if v != 1:
        raise struct.error('port version %d not known' % v)
    out['types_exemplar_14'] = r.u32()
    out['lot_group_34'] = r.u16()
    # Per port: stage +0x4, the index of its type +0x8, its cell +0xE, +0x10.
    out['ports'] = [[r.u32(), r.u16(), r.s16(), r.s16()] for _ in range(r.u16())]
    return out


def airport_developer(r):
    """cSC4AirportDeveloper (0xAA2049A1): Write at 0x0068B3E0 (version 2,
    then the port base's record), Read at 0x007010B0 (version 2 only; both
    get the object + 0xC)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    return port_developer(r, out)


SCHEMAS = {0xAA2049A1: ('cSC4AirportDeveloper', airport_developer)}

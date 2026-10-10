"""Save decoder of the zone developer (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def zone_developer(r):
    """cSC4ZoneDeveloper (0x498F9B01): Write at 0x0072AB40, Read at
    0x00734380 (version 1 only; both get the object + 0xC). The record is
    the lot manager's grid of lots by cell (src/core/zone_sim.h): a
    reference per cell, column by column (x outer, z inner); every cell of
    a lot refers to the same lot."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['cells_x'] = nx = r.u32()
    out['cells_z'] = nz = r.u32()
    out['lots_54'] = [r.ref() for _ in range(nx * nz)]
    return out


SCHEMAS = {0x498F9B01: ('cSC4ZoneDeveloper', zone_developer)}

"""Save decoder of the growth developers (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .common import header


def growth_developer(r):
    """cSC4GrowthDeveloper (0x0990C138): Write at 0x006B3960, Read at
    0x006B39F0 (version 1 only; both get the object + 8). Names give the object offsets
    (docs/growth.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['demand_sent_104'] = r.s32()
    out['total_capacity_108'] = r.u32()
    # The grids +0x10C, +0x114, +0x118, +0x11C, +0x120 (not +0x110).
    out['grids'] = [r.ref() for _ in range(5)]
    return out


# One record holds every developer, one after the other.
SCHEMAS = {0x0990C138: ('cSC4GrowthDeveloper', every(growth_developer))}

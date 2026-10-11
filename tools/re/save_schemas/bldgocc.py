"""Save decoder of the city's buildings (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def building(r):
    """cSC4BuildingOccupant (0xA9BD882D): Write at 0x00458A90 (version 2),
    Read at 0x00458B10 (version 2 only); both get the object + 8. After the
    version comes the occupant base's record (cSC4Occupant, Write 0x005E8CD0,
    version 4; every kind of occupant starts with it), whose property holder
    is written by 0x005FE070: version 2, the holder's own properties (as the
    lot's, 0x005FD990), then the exemplar's resource key. Names give the
    object offsets (src/core/bldgocc_sim.h)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    base = r.u16()
    if base != 4:
        raise struct.error('occupant version %d not known' % base)
    out['removal_30'] = [r.u8(), r.u8()]
    out['highlight_32'] = r.u8()
    out['flags_33'] = r.u8()
    out['type_28'] = r.u32()
    out['manager_cells_2c'] = [r.u8() for _ in range(4)]
    exemplar = r.u16()
    if exemplar != 2:
        raise struct.error('exemplar holder version %d not known' % exemplar)
    out['holder_c'] = holder(r)
    # Group, type (0x6534284A, an exemplar) and instance; zeros for none.
    out['exemplar_24'] = [r.u32(), r.u32(), r.u32()]
    out['building_type_38'] = r.u32()
    out['lit_55'] = r.u8()
    # The night lights follow a timed step through the buildings, not game
    # days (docs/lot.md). Not compared.
    out['lit_55'] = None
    out['min_3c'] = [r.f32(), r.f32(), r.f32()]
    out['max_48'] = [r.f32(), r.f32(), r.f32()]
    out['orientation_54'] = r.s8()
    out['completion_58'] = r.f32()
    if out['completion_58'] != 1.0:
        # A building under construction: the value follows a timer in
        # milliseconds (the corpus has 4584/10000, 1288/3000, 2146/7000 ...),
        # not game days. Not compared.
        out['completion_58'] = None
    return out


# One record holds every building, one after the other.
SCHEMAS = {0xA9BD882D: ('cSC4BuildingOccupant', every(building))}

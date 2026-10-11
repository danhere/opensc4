"""Save decoder of the city's building foundations (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def foundation(r):
    """cSC4FoundationOccupant (0x49C05C8F): Write at 0x005D6250 (version 2),
    Read at 0x005D62D0 (version 2 only); both get the object + 8. After the
    version comes the occupant base's record (cSC4Occupant, Write 0x005E8CD0,
    version 4; every kind of occupant starts with it), whose property holder
    is written by 0x005FE070: version 2, the holder's own properties, then
    the exemplar's resource key. Names give the object offsets
    (src/core/foundocc_sim.h). Every field the class has is saved and every
    one is set by the constructor: no pool leftovers, nothing driven by
    frames. The base's highlight is the interface's, but the game saves it
    and it is zero unless a tool is over the occupant: kept."""
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
    out['foundation_type_38'] = r.u32()
    out['max_height_3c'] = r.f32()
    # The middle of the slab's top, then its width, height and depth.
    out['position_40'] = [r.f32(), r.f32(), r.f32()]
    out['size_4c'] = [r.f32(), r.f32(), r.f32()]
    out['usage_58'] = r.s8()
    out['orientation_59'] = r.s8()
    return out


# One record holds every foundation, one after the other.
SCHEMAS = {0x49C05C8F: ('cSC4FoundationOccupant', every(foundation))}

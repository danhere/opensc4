"""Save decoder of the city's flora (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def flora(r):
    """cSC4FloraOccupant (0xA9C05C85): Write at 0x00472990 (version 3), Read
    at 0x00472A20 (version 3 only); both get the object + 8. After the
    version comes the occupant base's record (cSC4Occupant, Write 0x005E8CD0,
    version 4; every kind of occupant starts with it), whose property holder
    is written by 0x005FE070: version 2, the holder's own properties, then
    the exemplar's resource key. Names give the object offsets
    (src/core/floraocc_sim.h). The static flora data (+0x54) is not saved:
    the bounding box query finds it again by the flora type."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 3:
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
    out['flora_type_38'] = r.u32()
    out['position_40'] = [r.f32(), r.f32(), r.f32()]
    out['birth_date_4c'] = r.u32()
    out['last_seeding_date_50'] = r.u32()
    out['state_58'] = r.s8()
    out['orientation_59'] = r.u8()
    # The lot object's ID for a lot's flora (occupant slot 28). The
    # constructor does not set it: a tree nothing tagged has what the pool's
    # memory held (src/core/floraocc_sim.h).
    out['lot_tag_3c'] = r.u32()
    return out


# One record holds every tree and plant, one after the other.
SCHEMAS = {0xA9C05C85: ('cSC4FloraOccupant', every(flora))}

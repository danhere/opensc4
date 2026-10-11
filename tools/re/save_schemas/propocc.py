"""Save decoder of the city's props (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def prop(r):
    """cSC4PropOccupant (0x2977AA47): Write at 0x005ECE90 (version 6), Read
    at 0x005EF360 (any version: it is never tested, only "above 5" decides
    whether the timer byte is in the record); both get the object + 0xC.
    After the version comes the occupant base's record (cSC4Occupant, Write
    0x005E8CD0, version 4), whose property holder is written by 0x005FE070:
    version 2, the holder's own properties, then the exemplar's resource
    key. Names give the object offsets (src/core/propocc_sim.h).

    Not compared (None), because frames or the view drive them and two runs
    of the original need not agree (src/core/propocc_sim.h, "What follows
    frames"): the state, the timer bits 1, 4 and 8 (bit 2, the date range's,
    is kept), and where the box lies (its size is kept, in sixteenths of a
    metre)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 6:
        raise struct.error('version %d not known' % v)
    base = r.u16()
    if base != 4:
        raise struct.error('occupant version %d not known' % base)
    out['removal_34'] = [r.u8(), r.u8()]
    out['highlight_36'] = r.u8()
    out['flags_37'] = r.u8()
    out['type_2c'] = r.u32()
    out['manager_cells_30'] = [r.u8() for _ in range(4)]
    exemplar = r.u16()
    if exemplar != 2:
        raise struct.error('exemplar holder version %d not known' % exemplar)
    out['holder_10'] = holder(r)
    # Group, type (0x6534284A, an exemplar) and instance; zeros for none.
    out['exemplar_28'] = [r.u32(), r.u32(), r.u32()]
    # The prop type's ID (the object keeps a 16-bit index into the table of
    # IDs at 0x00B231A4; Write looks the ID up).
    out['prop_type_5c'] = r.u32()
    low = [r.f32(), r.f32(), r.f32()]
    high = [r.f32(), r.f32(), r.f32()]
    out['min_3c'] = None
    out['max_48'] = None
    out['size'] = [round((high[i] - low[i]) * 16) for i in range(3)]
    out['orientation_61'] = r.s8()
    out['state_63'] = r.u8()
    out['state_63'] = None
    # Start and end hour in tenths.
    out['hours_5e'] = [r.u8(), r.u8()]
    if r.u8():
        # The date range (+0x54, 0x28 bytes): interval and duration in days,
        # the next start and the end as day numbers.
        out['dates_54'] = [r.u32(), r.u32(), r.u32(), r.u32()]
    else:
        out['dates_54'] = []
    out['chance_62'] = r.u8()
    out['usage_64'] = r.u8()
    out['lot_tag_58'] = r.u32()
    out['timer_60'] = r.u8() & 2
    return out


# One record holds every prop, one after the other.
SCHEMAS = {0x2977AA47: ('cSC4PropOccupant', every(prop))}

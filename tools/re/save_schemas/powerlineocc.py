"""Save decoder of the city's power lines (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def power_line(r):
    """cSC4PowerLineOccupant (0xC9C05C5D): Write at 0x0064A310 (version 2),
    Read at 0x0064A3A0 (version 2 only); both get the object + 8. After the
    version comes the occupant base's record (cSC4Occupant, Write 0x005E8CD0,
    version 4; every kind of occupant starts with it), whose property holder
    is written by 0x005FE070: version 2, the holder's own properties, then
    the exemplar's resource key. Names give the object offsets
    (src/core/powerlineocc_sim.h). The preview byte (+0x55) is not saved."""
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
    # Group, type and instance of the exemplar; zeros for none.
    out['exemplar_24'] = [r.u32(), r.u32(), r.u32()]
    # The wire's position (the middle between the poles), then its cell.
    out['position_38'] = [r.f32(), r.f32(), r.f32()]
    out['cell_44'] = [r.s32(), r.s32()]
    out['flag_54'] = r.u8()
    # The two poles: references (address-ID and class ID; none is a 0).
    out['pole_4c'] = r.ref()
    out['pole_50'] = r.ref()
    return out


# One record holds every power line, one after the other.
SCHEMAS = {0xC9C05C5D: ('cSC4PowerLineOccupant', every(power_line))}

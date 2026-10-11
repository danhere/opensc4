"""Save decoder of the city's landfill occupants (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def landfill_occupant(r):
    """cSC4LandfillOccupant (0x8A22BEBE): Write at 0x00477350 (version 3),
    Read at 0x00477560 (version 3 only); both get the object + 8. After the
    version comes the occupant base's record (cSC4Occupant, Write 0x005E8CD0,
    version 4; every kind of occupant starts with it), whose property holder
    is written by 0x005FE070: version 2, the holder's own properties, then
    the exemplar's resource key. Names give the object offsets
    (src/core/landfillocc_sim.h). The box around the position (+0x44 to
    +0x58) is not saved: Read makes it from the position. Nothing of the
    record is pool memory or frame-driven, so nothing is masked."""
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
    # SetTileID keeps the instance equal to the tile ID below.
    out['exemplar_24'] = [r.u32(), r.u32(), r.u32()]
    # The tile's exemplar instance ID: which heap of garbage stands there.
    out['tile_5c'] = r.u32()
    out['orientation_60'] = r.u32()
    out['position_38'] = [r.f32(), r.f32(), r.f32()]
    # One byte: whether +0x64 was not zero; Read takes only 1 as set.
    out['plain_64'] = r.u8()
    return out


# One record holds every landfill occupant, one after the other.
SCHEMAS = {0x8A22BEBE: ('cSC4LandfillOccupant', every(landfill_occupant))}

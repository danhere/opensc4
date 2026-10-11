"""Save decoder of the lots' retaining walls (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def wall(r):
    """cSC4LotRetainingWallOccupant (0x49C05C9F): Write at 0x005E2AB0
    (version 2), Read at 0x005E2B40 (version 2 only); both get the object +
    8. After the version comes the occupant base's record (cSC4Occupant,
    Write 0x005E8CD0, version 4; every kind of occupant starts with it),
    whose property holder is written by 0x005FE070: version 2, the holder's
    own properties, then the exemplar's resource key. Names give the object
    offsets (src/core/wallocc_sim.h). Every field the class has is saved and
    every one is set by the constructor: no pool leftovers. The base's
    highlight is the interface's, but the game saves it and it is zero
    unless a tool is over the occupant: kept.
    Not compared (None), because the view drives it: the edge flags (+0x40).
    The code that builds the walls' drawables (0x005E3400) sets and clears a
    side's bit by whether its geometry could be made (94 of the 184 walls
    of Rush Hour d0365 have a hash on a side whose bit is clear), and Read
    sets the bit of every side with a hash again."""
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
    out['wall_type_38'] = r.u32()
    # Bit n: side n has its geometry (the view's; see above).
    r.s32()
    out['edge_flags_40'] = None
    # Per side a hash of the neighbouring lots and the terrain's heights
    # along it (0x005E2CE0); zero for a side without a wall.
    out['edge_hashes_44'] = [r.u32() for _ in range(4)]
    # The middle of the lot at the lot's height, then the lot's width, the
    # walls' height (the highest edge vertex above the lot) and its depth.
    out['position_54'] = [r.f32(), r.f32(), r.f32()]
    out['size_60'] = [r.f32(), r.f32(), r.f32()]
    out['lot_3c'] = r.ref()
    return out


# One record holds every retaining wall, one after the other.
SCHEMAS = {0x49C05C9F: ('cSC4LotRetainingWallOccupant', every(wall))}

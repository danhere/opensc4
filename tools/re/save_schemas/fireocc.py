"""Save decoder of the city's fires (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header
from .constrocc import transform


def fire(r):
    """cSC4FireOccupant (0x29D25F1D): Write at 0x00470130 (version 3), Read
    at 0x00471A70 (version 3 only); both get the object + 8. After the
    version comes the occupant base's record (cSC4Occupant, Write 0x005E8CD0,
    version 4; every kind of occupant starts with it), whose property holder
    is written by 0x005FE070: version 2, the holder's own properties, then
    the exemplar's resource key. Names give the object offsets
    (src/core/fireocc_sim.h).

    Compared: what the simulation keeps (the heat, the stage, the stage
    count, the target). Not compared (None): the flames, which are what is
    seen of the fire. How many there are and their kinds, turns and spots
    depend on whether the target had a model object when the stage first
    changed (the model's spots, drawn with a generator seeded at that
    moment, or one flame more at places drawn with the class's own
    generator 0x00B21648), and a view message moves them to new spots
    (0x00472790). They are still read to the end, so a record that does not
    hold together fails.

    Not in the record at all: the two flame numbers (+0x38, +0x3C), the
    hosed count (+0x4C), the end time (+0x50) and the family (+0x68; the
    constructor does not set it, so a fire read from a save has what the
    heap's memory held)."""
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
    # Group, type and instance; zeros for none (a fire has no exemplar).
    out['exemplar_24'] = [r.u32(), r.u32(), r.u32()]
    out['heat_40'] = r.u32()
    out['stage_44'] = r.s32()
    out['stage_count_48'] = r.u32()
    # The burning occupant (a building, a lot object or a tree).
    out['target_54'] = r.ref()
    # The flames (0x004700A0 reads one): version 2 or 3, the kind (a bool),
    # the turn, the spot, then nine floats of a matrix (version 2) or a
    # transform (version 3; identity unless a save gave another). The
    # view's: read and left out.
    for _ in range(r.u32()):
        flame = r.u16()
        if flame not in (2, 3):
            raise struct.error('flame version %d not known' % flame)
        r.u8()
        r.u32()
        for _ in range(3):
            r.f32()
        if flame == 2:
            for _ in range(9):
                r.f32()
        else:
            transform(r)
    out['flames_58'] = None
    return out


# One record holds every fire, one after the other.
SCHEMAS = {0x29D25F1D: ('cSC4FireOccupant', every(fire))}

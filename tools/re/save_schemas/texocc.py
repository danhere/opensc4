"""Save decoder of the city's lot base textures (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def base_texture(r):
    """cSC4LotBaseTextureOccupant (0xC97F987C): Write at 0x005DF1E0 (version
    2), Read at 0x005E10B0 (version 2 only); both get the object + 8. After
    the version comes the occupant base's record (cSC4Occupant, Write
    0x005E8CD0, version 4; every kind of occupant starts with it), whose
    property holder is written by 0x005FE070: version 2, the holder's own
    properties, then the exemplar's resource key. Names give the object
    offsets (src/core/texocc_sim.h). Then the box (its low and high corner),
    the usage and the texture entries, 14 of each entry's 16 bytes: the last
    two are never set and not saved. Nothing saved follows frames or the
    view; the box's heights follow the terrain under the cells. The base's
    highlight is the interface's, but the game saves it and it is zero
    unless a tool is over the occupant: kept."""
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
    # Group, type and instance; zeros for none (a base texture has none).
    out['exemplar_24'] = [r.u32(), r.u32(), r.u32()]
    out['box_low_38'] = [r.f32(), r.f32(), r.f32()]
    out['box_high_44'] = [r.f32(), r.f32(), r.f32()]
    # 1: a landfill cell's, 2: a lot's.
    out['usage_50'] = r.u8()
    count = r.u32()
    entries = []
    for _ in range(count):
        entries.append({
            'texture_0': r.u32(),
            'cell_4': [r.u8(), r.u8()],
            'orientation_6': r.u8(),
            'mirror_7': r.u8(),
            'colour_8': [r.u8(), r.u8(), r.u8()],
            # 0xFF: the cell is fully covered.
            'alpha_b': r.u8(),
            'byte_c': r.u8(),
            # Which of the texture's pictures: drawn with the class's own
            # generator when the entry came with 0xFF.
            'picture_d': r.u8(),
        })
    out['entries_54'] = entries
    return out


# One record holds every base texture, one after the other.
SCHEMAS = {0xC97F987C: ('cSC4LotBaseTextureOccupant', every(base_texture))}

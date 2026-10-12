"""Save decoder of the city's power poles (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header
from .constrocc import transform
from .netocc import corner


def vec3(r):
    return [r.f32(), r.f32(), r.f32()]


def outer(r):
    """A vector of vectors of three floats (0x0064B010 writes it, 0x0064EA00
    reads it): the outer count, then for each inner vector its count and its
    triples."""
    return [[vec3(r) for _ in range(r.u32())] for _ in range(r.u32())]


def corners(r):
    """A corner vector (0x0065B070 writes it, 0x006574A0 reads it): the count,
    then the 0x18-byte records as the network occupant's corners."""
    return [corner(r) for _ in range(r.u32())]


def record(r):
    """One record, the wires to one other tower (the object's vector at
    +0xAC, 0x2C bytes each): a reference to the other tower (a 0x09C05C6A
    object), the number of list items and each item as a reference (class
    0xC9C05C5D), two bools, two int32 (a cell), and two vectors of vectors of
    three floats (the record's +0x14 and +0x20)."""
    target = r.ref()
    items = [r.ref() for _ in range(r.u32())]
    flags = [r.u8(), r.u8()]
    cell = [r.s32(), r.s32()]
    return {'target': target, 'items': items, 'flags_8_9': flags, 'cell_c_10': cell, 'points_14': outer(r),
            'points_20': outer(r)}


def pole(r):
    """cSC4PowerPoleOccupant (0x09C05C6A): Write at 0x0064B010 (version 5),
    Read at 0x0064EA00 (version 4 and up; 4 has the transform as nine
    floats); both get the object + 0xC. After the version comes the occupant
    base's record (cSC4Occupant, Write 0x005E8CD0, version 4; every kind of
    occupant starts with it), whose property holder is written by 0x005FE070:
    version 2, the holder's own properties, then the exemplar's resource key.
    Names give the object offsets (src/core/powerpoleocc_sim.h). The two
    highlight bytes (+0x54, +0x55) are saved but belong to the view."""
    start = r.pos
    out = header(r)
    out['version'] = v = r.u16()
    if not 4 <= v <= 5:
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
    # +0x68: the flags byte, then the 3 by 3 matrix, the translation and the
    # scale. Version 4 has the nine floats.
    out['transform_68'] = transform(r) if v >= 5 else {'flags': 4, 'matrix': [r.f32() for _ in range(9)]}
    out['position_a0'] = vec3(r)
    out['records_ac'] = [record(r) for _ in range(r.u32())]
    out['count_b8'] = r.u32()
    out['ints_c0'] = [r.s32() for _ in range(4)]
    # The four triples of the table the object points to at +0xE4.
    out['table_e4'] = [vec3(r) for _ in range(4)]
    out['corners_e8'] = corners(r)
    out['corners_f4'] = corners(r)
    out['corners_100'] = corners(r)
    out['float_10c'] = r.f32()
    out['bool_110'] = r.u8()
    out['highlight_54'] = [r.u8(), r.u8()]
    out['float_64'] = r.f32()
    out['byte_112'] = r.u8()
    if r.pos - start != out['size']:
        raise struct.error('power pole used %d bytes of %d' % (r.pos - start, out['size']))
    return out


# One record holds every power pole, one after the other.
SCHEMAS = {0x09C05C6A: ('cSC4PowerPoleOccupant', every(pole))}

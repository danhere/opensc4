"""Save decoder of the city's network occupants (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header
from .constrocc import transform


def corner(r):
    """A 0x18-byte record of the corners and of the path records, in the
    order Write emits it: three floats (x, y, z), two more floats (the
    record's +0x10 and +0x14), then the bytes at +0xE, +0xD, +0xC and +0xF."""
    xyz = [r.f32(), r.f32(), r.f32()]
    tail = [r.f32(), r.f32()]
    # The four bytes are the corner's own and the lighting changes them with
    # the view; the last one (+0xF) is 0xFF after a load.
    return {'xyz': xyz, 'tail': tail, 'bytes_e_d_c_f': [r.u8(), r.u8(), r.u8(), r.u8()]}


def edge_store(r):
    """The edge store (0x00619C10 writes it, 0x00619C90 reads it): version
    2, the number of further entries, the first entry's key (a byte) and
    value, then each further entry's key and value."""
    v = r.u16()
    if v != 2:
        raise struct.error('edge store version %d not known' % v)
    count = r.u8()
    first = [r.u8(), r.u32()]
    return {'first': first, 'more': [[r.u8(), r.u32()] for _ in range(count)]}


def network(r):
    """cSC4NetworkOccupant (0xC9C05C6E): Write at 0x0061A190 (version 8),
    Read at 0x0061CC00 (version 4 and up; the older ones differ in the
    transform, the flags, the complex piece origin and the retrofit flag);
    both get the object + 8. After the version comes the occupant base's
    record (cSC4Occupant, Write 0x005E8CD0, version 4; every kind of
    occupant starts with it), whose property holder is written by 0x005FE070:
    version 2, the holder's own properties, then the exemplar's resource key.
    Names give the object offsets (src/core/netocc_sim.h).

    The derived classes (bridge, tunnel, pipe ...) write their own version
    first and then this whole record, then their own fields (their Writes
    call 0x0061A190): they are not decoded here."""
    start = r.pos
    out = header(r)
    out['version'] = v = r.u16()
    if not 4 <= v <= 8:
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
    # +0x64: the flags byte, then the 3 by 3 matrix with flag 4, the
    # translation with 1, the scale with 2. Version 4 has the nine floats.
    out['transform_64'] = transform(r) if v >= 5 else {'flags': 4, 'matrix': [r.f32() for _ in range(9)]}
    out['corners_9c'] = [corner(r) for _ in range(4)]
    out['unknown_fc'] = r.u32()
    out['variation_105'] = r.u8()
    out['under_texture_100'] = r.u32()
    out['rotation_flip_104'] = r.u8()
    out['edge_store_108'] = edge_store(r)
    # Path records (+0x118): a uint32 and three corner-shaped records each.
    out['paths_118'] = [[r.u32(), corner(r), corner(r), corner(r)] for _ in range(r.u32())]
    # Three pairs of floats: the object's +0x124/+0x130, +0x128/+0x134 and
    # +0x12C/+0x138.
    out['pairs_124'] = [[r.f32(), r.f32()] for _ in range(3)]
    out['flags_13c'] = r.u32()
    out['path_id_override_164'] = pid = r.u32()
    if pid and v < 7:
        # Two floats Read turns into the complex piece origin.
        out['origin_floats'] = [r.f32(), r.f32()]
    # The resource the object at +0x140 stands for (0x005D9940): type, group
    # and instance, three zeros for none.
    out['resource_140'] = [r.u32(), r.u32(), r.u32()]
    out['cost_170'] = r.s64()
    if r.pos - start != out['size']:
        raise struct.error('network occupant used %d bytes of %d' % (r.pos - start, out['size']))
    return out


# One record holds every network occupant, one after the other.
SCHEMAS = {0xC9C05C6E: ('cSC4NetworkOccupant', every(network))}

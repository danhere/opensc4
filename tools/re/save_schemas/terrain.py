"""Save decoders of the terrain (tools/re/sc4save.py). SCHEMAS maps record
type IDs to (name, decoder).

cSTETerrain (class 0xE98F9525) is saved in two layers (docs/terrain.md,
src/core/terrain_save.cpp): its own object record with the framework's header
(type = the class ID), which holds only a version and a reference to the
terrain view; and four kinds of data records of their own in the same file
(group = the class ID), written through the game's record streams
(0x005D9F00) WITHOUT the framework's header: a uint16 version, then the
fields. Their 'size', 'crc' and 'id' are None here."""
import struct
import zlib

from .common import header


def no_header():
    return {'size': None, 'crc': None, 'id': None}


def rest(r, fmt, width):
    """Every remaining value of one type; an error when bytes are left over."""
    left = len(r.data) - r.pos
    if left % width:
        raise struct.error('%d bytes do not divide by %d' % (left, width))
    return [r.take(fmt) for _ in range(left // width)]


def terrain(r):
    """cSTETerrain (0xE98F9525 / 0x299B2D1B / 0): Write at 0x00745300 (version
    2), Read at 0x00745380 (versions up to 2; both get the object + 8). The
    view +0xE8 is written as a reference to its +0x14 part (class
    0xC9B84E10); everything else is in the records below."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 2:
        raise struct.error('version %d not known' % v)
    out['view_e8'] = r.ref()
    return out


def terrain_header(r):
    """0x8A91E7E3 / 0xE98F9525 / 3: written at 0x007453E0 (version 2), read at
    0x00745490 (any version; below 2 a terrain type of 1 or more is read as
    0). Names give the object offsets."""
    out = no_header()
    out['version'] = v = r.u16()
    if v > 2:
        raise struct.error('version %d not known' % v)
    out['verts_x_38'] = r.u32()
    out['verts_z_3c'] = r.u32()
    out['terrain_type_18'] = r.s32()
    out['min_height_70'] = r.f32()
    out['max_height_74'] = r.f32()
    out['size_type_float_78'] = r.f32()
    return out


def terrain_altitudes(r):
    """0xA9DD6FF4 / 0xE98F9525: two records by instance, told apart here by
    their version.
    Instance 1, version 2 (0x00745560; read at 0x007455F0, version 2 only):
    every vertex's altitude, rows first (verts_z rows of verts_x).
    Instances 0x64 to 0x67, version 1 (0x00745800; not read back by this
    city, the neighbours read them): one edge: a count, then that many
    altitudes: 0x64 the column x = 0, 0x66 the column x = cells x (count =
    rows), 0x65 the row z = 0, 0x67 the row z = cells z (count = vertices
    per row). The checksum is of the altitudes' bytes (it also tells -0
    from 0)."""
    out = no_header()
    out['version'] = v = r.u16()
    if v == 2:
        start = r.pos
        out['altitudes'] = rest(r, 'f', 4)
    elif v == 1:
        out['count'] = n = r.u32()
        if n > 0x100000:
            raise struct.error('%d altitudes in an edge' % n)
        start = r.pos
        out['altitudes'] = [r.f32() for _ in range(n)]
    else:
        raise struct.error('version %d not known' % v)
    out['checksum'] = zlib.crc32(r.data[start:r.pos])
    return out


def terrain_flags(r):
    """0x8A91E7E0 / 0xE98F9525 / 2: written at 0x007456D0 (version 1), read at
    0x00745760 (version 1 only): every vertex's flags word, rows first (bits:
    src/core/terrain_sim.h, namespace flag)."""
    out = no_header()
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['flags'] = rest(r, 'H', 2)
    return out


SCHEMAS = {
    0xE98F9525: ('cSTETerrain', terrain),
    0x8A91E7E3: ('cSTETerrain header', terrain_header),
    0xA9DD6FF4: ('cSTETerrain altitudes', terrain_altitudes),
    0x8A91E7E0: ('cSTETerrain flags', terrain_flags),
}

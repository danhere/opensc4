"""Save decoder of the city's lots (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .common import header

# Sizes of the property values the holder part writes (the stream's variant
# writer behind 0x005DA120): 1, 2, 3 and 8 are seen in the corpus (uint8,
# uint16, uint32, sint64); 7, 9 and 0xB are taken from the exemplar format's
# numbering (sint32, float32, bool), which the seen ones follow.
VALUE_SIZES = {1: 'B', 2: 'H', 3: 'I', 7: 'i', 8: 'q', 9: 'f', 0xB: 'B'}


def holder(r):
    """A property holder as 0x005FD990 writes it (0x005FF280 reads it):
    version 2, count, then per property its ID, the property's own record
    (0x005FD870: the ID again and the value: a zero marker, the type with
    0x8000 for an array, a zero word, the count of an array, the values)."""
    v = r.u16()
    if v != 2:
        raise struct.error('holder version %d not known' % v)
    properties = []
    for _ in range(r.u32()):
        key, again, marker, kind, zero = r.u32(), r.u32(), r.u32(), r.u16(), r.u16()
        if key != again or marker or zero or (kind & 0x7FFF) not in VALUE_SIZES:
            raise struct.error('property %08X/%08X: marker %X, type %X, %X' % (key, again, marker, kind, zero))
        fmt = VALUE_SIZES[kind & 0x7FFF]
        values = [r.take(fmt) for _ in range(r.u32())] if kind & 0x8000 else r.take(fmt)
        properties.append([key, kind, values])
    return properties


def trip(r):
    """One record of the lot's trip list (0x0070E0B0 writes version 2,
    0x0070E160 reads 1 and 2): two byte vectors, then the fields at +0x18 to
    +0x28 of the 0x2C-byte record."""
    out = {'version': r.u32()}
    if out['version'] > 2:
        raise struct.error('trip version %d not known' % out['version'])
    for name in ('bytes_0', 'bytes_c'):
        n = r.u32()
        if r.pos + n > len(r.data):
            raise struct.error('trip bytes run past the record')
        out[name] = r.data[r.pos:r.pos + n].hex()
        r.pos += n
    out['u32_18'] = r.u32()
    out['u8_1c'] = [r.u8() for _ in range(4)]
    out['u16_20'], out['u16_22'] = r.u16(), r.u16()
    out['f32_24'] = r.f32()
    out['u32_28'] = r.u32() if out['version'] > 1 else 0
    if not out['bytes_0'] and not out['bytes_c']:
        # A record the lot made itself and no trip has filled yet: the game
        # never writes +0x20, +0x22 and +0x28 of it (0x006C27E0), so the save
        # holds whatever its stack had (docs/citylot.md). Not compared.
        out['u16_20'] = out['u16_22'] = out['u32_28'] = None
    return out


def lot(r):
    """cSC4Lot (0xC9BD5D4A): Write at 0x006C0080 (version 8), Read at
    0x006C35F0 (up to 8); both get the object + 4. Names give the object
    offsets (src/core/citylot_sim.h). Before version 6 the cell bytes and
    three flags were written as uint32s and bools; the corpus has only
    version 8, so earlier versions are refused here."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 8:
        raise struct.error('version %d not known' % v)
    out['config_id_34'] = r.u32()
    out['flags_79'] = r.u8()
    out['rect_20'] = [r.u8() for _ in range(4)]
    out['cell_7a'] = [r.u8(), r.u8()]
    out['altitude_24'], out['built_slope_2c'], out['slope_28'] = r.f32(), r.f32(), r.f32()
    out['size_7c'] = [r.u8(), r.u8()]
    out['facing_7e'], out['state_7f'], out['condition_80'] = r.u8(), r.u8(), r.u8()
    out['zone_81'], out['wealth_82'] = r.u8(), r.u8()
    out['state_day_38'], out['building_type_3c'] = r.u32(), r.u32()
    out['vacate_reason_83'] = r.u8()
    if out['state_7f'] != 4:
        # Written only when a lot enters state 4 (SetState); a lot
        # made since has whatever the pool's memory held (docs/citylot.md).
        out['vacate_reason_83'] = None
    out['unit_4c'], out['designate_50'] = r.ref(), r.ref()
    out['populations_54'] = [[[r.u32(), r.u16()] for _ in range(r.u8())] for _ in range(r.u8())]
    out['travel_60'] = [[r.u32(), r.u16()] for _ in range(r.u8())]
    out['travel_jobs_6c'] = [r.f32(), r.f32(), r.f32()]
    out['holder_8'] = holder(r)
    out['trips_84'] = [trip(r) for _ in range(r.u32())]
    out['random_88'] = r.u8()
    return out


def business_unit(r):
    """cSC4BusinessUnit (0x4A232DA8): Write at 0x006C0370 (version 1; it
    gets the object + 4): the member lots of the list at +0x1C."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['lots_1c'] = [r.ref() for _ in range(r.u32())]
    return out


# One record holds every lot (or every business unit), one after the other.
SCHEMAS = {0xC9BD5D4A: ('cSC4Lot', every(lot)), 0x4A232DA8: ('cSC4BusinessUnit', every(business_unit))}

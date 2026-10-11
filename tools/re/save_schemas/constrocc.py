"""Save decoder of the city's construction sites (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .citylot import holder
from .common import header


def transform(r):
    """A 0x38-byte transform as 0x005DA8C0 writes it: its flags byte, then
    the 3 by 3 matrix with flag 4, the translation with flag 1, the scale
    with flag 2."""
    flags = r.u8()
    out = {'flags': flags}
    if flags & 4:
        out['matrix'] = [r.f32() for _ in range(9)]
    if flags & 1:
        out['translation'] = [r.f32() for _ in range(3)]
    if flags & 2:
        out['scale'] = r.f32()
    return out


def construction(r):
    """cSC4ConstructionOccupant (0xA97F909E): Write at 0x005CD250 (version
    5), Read at 0x005CE770 (versions 4 and 5; 4 has nine floats in place of
    the transform); both get the object + 0xC. After the version comes the
    occupant base's record (cSC4Occupant, Write 0x005E8CD0, version 4; every
    kind of occupant starts with it), whose property holder is written by
    0x005FE070: version 2, the holder's own properties, then the exemplar's
    resource key. Names give the object offsets (src/core/constrocc_sim.h).

    Compared: what the simulation keeps (the stage, the days, the floors)
    and what it is made from (the boxes, the two heights). Not compared
    (None), each with its reason below: what the frame's animation moves
    (real milliseconds) and what the drawing's setup makes with draws from
    the class's own random generator (0x00B22FB0), which the frame's
    animation draws from too, so its sequence follows frames."""
    out = header(r)
    out['version'] = v = r.u16()
    if v not in (4, 5):
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
    # Group, type and instance; zeros for none.
    out['exemplar_28'] = [r.u32(), r.u32(), r.u32()]
    out['lot_68'] = r.ref()
    out['building_6c'] = r.ref()
    out['transform_70'] = transform(r) if v >= 5 else {'flags': 4, 'matrix': [r.f32() for _ in range(9)]}
    # The occupant's box: low, then high.
    out['box_a8'] = [r.f32() for _ in range(6)]
    # A second box. Its high y (+0xD0) is the scaffolding's height now: the
    # frame's animation sets it (0x005CC020). Not compared.
    inner = [r.f32() for _ in range(6)]
    inner[4] = None
    out['inner_box_c0'] = inner
    # Whether the drawing's pointer at +0x104 is set (Read stores the bool
    # at +0xD8), and the place at +0x108, whose y follows the height: made
    # by the drawing's setup with random draws. Not compared.
    r.u8()
    out['has_104'] = None
    [r.f32() for _ in range(3)]
    out['place_108'] = None
    # Two lists of props (+0x128, +0x12C), a vector of visual effect props
    # (+0x130) and one more (+0x13C), each a count and references: the
    # setup's random draws decide how many there are. Not compared.
    for name in ('props_128', 'stage_props_12c', 'effects_130'):
        r.ref_list()
        out[name] = None
    r.ref()
    out['effect_13c'] = None
    # 0 start, 1 between floors, 2 a floor, 3 finishing, 4 done.
    out['stage_140'] = r.s8()
    out['start_day_144'] = r.u32()
    out['unused_148'] = r.u32()
    out['next_day_14c'] = r.u32()
    out['floors_150'] = r.u32()
    out['floor_154'] = r.u32()
    out['height_158'] = r.f32()
    out['first_height_15c'] = r.f32()
    out['first_days_160'] = r.s32()
    out['pause_days_164'] = r.s32()
    out['last_days_168'] = r.s32()
    out['floor_days_16c'] = r.u32()
    # Milliseconds since the stage began, counted by the frame's animation
    # (0x005CE110). Not compared.
    r.u32()
    out['elapsed_170'] = None
    # The crane: its state, two angles from the setup's random draws, a
    # bool, two times and two targets, moved by the frame's animation
    # (0x005CD570). Not compared.
    r.s8()
    [r.f32() for _ in range(2)]
    r.u8()
    [r.u32() for _ in range(2)]
    [r.f32() for _ in range(2)]
    out['crane_174'] = None
    # A byte the drawing's setup chooses (0x005CD710, with random draws);
    # the constructor leaves it unset (the pool's memory) until then. Not
    # compared.
    r.u8()
    out['sides_124'] = None
    # The network pieces next to the site (+0x194): two bools and a
    # reference each, found by the setup after random draws. Not compared.
    for _ in range(r.u32()):
        r.u8()
        r.u8()
        r.ref()
    out['network_194'] = None
    return out


# One record holds every construction site, one after the other.
SCHEMAS = {0xA97F909E: ('cSC4ConstructionOccupant', every(construction))}

"""Save decoder of the lot developer (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def lot_developer(r):
    """cSC4LotDeveloper (0xA990BFE0): Write at 0x006C66F0, Read at
    0x006CD120 (version 1 only; both get the object + 8). Names give the
    object offsets (src/core/lot_sim.h)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    # The terrain's size as Init read it: vertices in a row, metres.
    out['vertices_x_40'] = r.u32()
    out['size_x_44'] = r.f32()
    out['size_z_48'] = r.f32()
    # The deque of buildings, in its order (shuffled at each day or night
    # change), then the night step: the index of its next building, its
    # state (0 idle) and a flag. The step follows the timer, not game days.
    out['buildings_64'] = r.ref_list()
    out['next_building_8c'] = r.u32()
    out['night_state_9c'] = r.u8()
    out['flag_a8'] = r.u8()
    return out


SCHEMAS = {0xA990BFE0: ('cSC4LotDeveloper', lot_developer)}

"""Save decoders of aura (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def aura_simulator(r):
    """cSC4AuraSimulator (0x0990BDD8): Write at 0x0068CC50 (version 9), Read
    at 0x0068E340 (versions 8 and 9). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/aura.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v not in (8, 9):
        raise struct.error('version %d not known' % v)
    for name in ('aura_40', 'park_map_58', 'park_sources_54', 'smoothed_48', 'transient_4c', 'landmark_sources_50',
                 'landmark_map_68'):
        out[name] = r.ref()
    if v < 9:
        out['old_grid'] = r.ref()
    else:
        out['departments_5c'] = [[r.u32(), r.ref()] for _ in range(r.u32())]
    out['flag_80'], out['rating_84'] = r.u32(), r.s32()
    out['best_148'] = [r.s32(), r.s32(), r.s16()]
    out['worst_154'] = [r.s32(), r.s32(), r.s16()]
    out['clusters_d0'] = [[r.s32(), r.s32(), r.s32()] for _ in range(r.u32())]
    return out


SCHEMAS = {0x0990BDD8: ('cSC4AuraSimulator', aura_simulator)}

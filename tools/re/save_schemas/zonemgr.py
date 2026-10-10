"""Save decoder of the zone manager (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def failure_map(r):
    """One of the two maps keyed by a pair of signed bytes (ordered by the
    second, then the first): count, then (first, second, value) in order."""
    return [[r.s8(), r.s8(), r.u32()] for _ in range(r.u32())]


def zone_manager(r):
    """cSC4ZoneManager (0x298F9B2D): Write at 0x004DC7A0 (version 1), Read
    at 0x004DD400 (version 1 only; both get the object + 8). Names give the
    object offsets (src/core/zonemgr_sim.h). The zone grid itself is its own
    record (cSC4SimGridSint8, referenced first); the slopes (+0x19C) and the
    default alpha (+0x361) are not saved."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['grid_48'] = r.ref()
    out['zoned_4c'] = [r.s32() for _ in range(16)]
    out['undeveloped_8c'] = [r.s32() for _ in range(16)]
    out['counts_cc'] = [r.s32() for _ in range(16)]
    out['total_undeveloped_10c'] = r.s32()
    out['count_110'] = r.s32()
    out['abandoned_114'] = r.s32()
    out['count_118'] = r.s32()
    out['min_size_11c'] = [r.s32() for _ in range(16)]
    out['max_size_15c'] = [r.s32() for _ in range(16)]
    out['zone_cost_1e0'] = [r.s64() for _ in range(16)]
    out['dezone_cost_260'] = [r.s64() for _ in range(16)]
    out['textures_2e0'] = [r.u32() for _ in range(16)]
    out['colors_320'] = [r.u32() for _ in range(16)]
    out['alpha_360'] = r.u8()
    out['failures_364'] = failure_map(r)
    out['map_370'] = failure_map(r)
    out['city_1c'] = r.ref()
    out['occupants_20'] = r.ref()
    out['terrain_24'] = r.ref()
    out['pollution_28'] = r.ref()
    out['budget_2c'] = r.ref()
    return out


SCHEMAS = {0x298F9B2D: ('cSC4ZoneManager', zone_manager)}

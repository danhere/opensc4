"""Save decoder of the building development simulator (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def building_development_simulator(r):
    """cSC4BuildingDevelopmentSimulator (0x8990BE7A): Write at 0x00697800,
    Read at 0x0069ACB0 (version 4 only; Read refuses any other). Names give
    the object offsets (docs/development.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 4:
        raise struct.error('version %d not known' % v)
    out['grid_48'] = r.ref()
    out['demand_70'] = r.ref()
    out['developers_5c'] = [[r.u32(), r.ref()] for _ in range(r.u32())]
    out['list_4c'] = r.ref_list()
    out['flags_50'] = [[r.u32(), r.u8()] for _ in range(r.u32())]
    # The process-wide map at 0x00B485E0: key -> set of (int32, int32).
    out['global_b485e0'] = [[r.u32(), [[r.s32(), r.s32()] for _ in range(r.u32())]] for _ in range(r.u32())]
    # The hash table at +0x9C: (developer, x, z, byte) -> uint32.
    out['hash_9c'] = [[r.ref(), r.s32(), r.s32(), r.u8(), r.u32()] for _ in range(r.u32())]
    out['flags_dd'] = [r.u8(), r.u8(), r.u8()]
    return out


SCHEMAS = {0x8990BE7A: ('cSC4BuildingDevelopmentSimulator', building_development_simulator)}

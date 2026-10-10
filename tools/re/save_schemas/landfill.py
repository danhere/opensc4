"""Save decoder of the landfill developer (tools/re/sc4save.py). SCHEMAS maps
class IDs to (name, decoder)."""
import struct

from .common import header


def landfill_tile(r):
    """One 0x20-byte tile of a zone (written inline at 0x006BA9D8, read by
    0x006BA2A0, version 2 only). Names give the tile offsets."""
    v = r.u16()
    if v != 2:
        raise struct.error('landfill tile version %d not known' % v)
    out = {'cell_x_4': r.u32(), 'cell_z_8': r.u32()}
    # Two flags as uint16 (1 = set; Read tests for exactly 1).
    out['flag_c'], out['flag_d'] = r.u16(), r.u16()
    out['used_18'], out['capacity_14'] = r.u32(), r.u32()
    return out


def landfill_zone(r):
    """cSC4LandfillZone (not a serialised object of its own): Write at
    0x006BA900 (version 4), Read at 0x006BBEF0 (any version; 3 has the active
    flag as a byte, 4 and later a bool and the reference). Names give the
    zone offsets."""
    out = {'version': r.u16()}
    v = out['version']
    x0, z0, x1, z1 = r.s32(), r.s32(), r.s32(), r.s32()
    out['region_c'] = [x0, z0, x1, z1]
    # One byte per cell of the region's bit map, x outside. The loops stop
    # before x1 and z1 (unsigned compares): the saved zones hold x1 - x0 by
    # z1 - z0 tiles.
    nx = x1 - x0 if 0 <= x0 < x1 else 0
    nz = z1 - z0 if 0 <= z0 < z1 else 0
    out['cells'] = [bytes(r.u8() for _ in range(nz)).hex() for _ in range(nx)]
    out['tiles_x_18'], out['tiles_z_14'] = r.s32(), r.s32()
    # Tiles along x and along z; Read sets the tile count +0x10 to the product.
    out['tiles_1c'] = [landfill_tile(r) for _ in range(max(0, out['tiles_x_18'] * out['tiles_z_14']))]
    if v == 3:
        out['active_29'] = r.u8()
    if v >= 4:
        out['active_29'] = r.u8()
        out['object_44'] = r.ref()
    return out


def landfill_developer(r):
    """cSC4LandfillDeveloper (0xEA09D4AE): Write at 0x006B9880 (version 3),
    Read at 0x006B9900 (version 3 only; both get the object + 8): the zones
    of the list +0x30, each written inline (src/core/landfill_sim.cpp)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 3:
        raise struct.error('version %d not known' % v)
    out['zones_30'] = [landfill_zone(r) for _ in range(max(0, r.s32()))]
    return out


SCHEMAS = {0xEA09D4AE: ('cSC4LandfillDeveloper', landfill_developer)}

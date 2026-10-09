"""Save decoders of power (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def power_simulator(r):
    """cSC4PowerSimulator (0x0990C005): Write at 0x006EB6A0 (version 6),
    Read at 0x006F0880 (versions up to 6). Names give the object offsets;
    the cIGZSerializable part sits at +0x8 (docs/power.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 6:
        raise struct.error('version %d not known' % v)
    out['department_20'] = r.ref()
    out['month_38'], out['produced_3c'], out['needed_40'] = r.s32(), r.s32(), r.s32()
    out['ratio_64'] = r.f32() if v >= 6 else r.s32()
    out['consumers_68_74'] = [r.s32() for _ in range(4)]
    out['demand_total_78'] = r.s64()
    out['monthly_50_60'] = [r.s32() for _ in range(5)]
    out['supply_grid_80'] = r.ref()
    rows, columns = r.u32(), r.u32()
    out['consumption_84'] = [[r.take('h') for _ in range(columns)] for _ in range(rows)]
    out['usage_90'] = [r.s32() for _ in range(120)]
    out['annual_270'] = [r.s32() for _ in range(10)]
    out['plants_2dc'] = n = r.s32()
    out['plants_by_type_2e0'] = [r.s32() for _ in range(10)]
    out['plant_list_298'] = [{'state': r.u32(), 'plant': r.ref()} for _ in range(max(n, 0))]
    out['pieces_2b8'] = n = r.u32()
    out['on_2bc'] = r.u32()
    out['pieces_29c'] = [r.ref() for _ in range(n)]
    words = r.u32()
    out['bits_2c4_2c8_2d4_2d0'] = [[r.u32() for _ in range(words)] for _ in range(4)]
    out['b2d8'], out['b330'] = r.u8(), r.u8()
    out['s334'] = r.s32()
    out['dirty_338'], out['b339'] = r.u8(), r.u8()
    out['state_34c'] = r.u32()
    return out


SCHEMAS = {0x0990C005: ('cSC4PowerSimulator', power_simulator)}

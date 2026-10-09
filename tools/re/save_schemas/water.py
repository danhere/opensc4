"""Save decoders of water (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def water_simulator(r):
    """cSC4PlumbingSimulator (0x0990C075): Write at 0x006DB090, Read at
    0x006DB620 (version 4 only). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/water.md)."""
    def producer():
        return {'cell': (r.s32(), r.s32()), 'lot': [r.s32() for _ in range(4)], 'capacity': r.u32(),
                'output': r.u32(), 'building': r.ref()}
    out = header(r)
    out['version'] = v = r.u16()
    if v != 4:
        raise struct.error('version %d not known' % v)
    out['grid_30'], out['grid_34'] = r.ref(), r.ref()
    out['stride_8c'] = stride = r.u32()
    out['height_90'] = height = r.u32()
    out['cells_88'] = [r.u8() for _ in range(stride * height)]
    out['months_9c'], out['pollution_a0'] = r.s32(), r.u32()
    out['usage_148'] = r.s64()
    count = r.u32()
    out['polluted_198'] = r.u32()
    out['producers_bc'] = [producer() for _ in range(count)]
    out['current_140'] = producer()
    out['iterator_144'] = {'state': r.s8(), 'fields': [r.s32() for _ in range(7)]}
    out['demand_130'] = r.s64()
    out['pipes_174'] = pipes = r.u32()
    out['distressed_178'], out['burst_17c'] = r.u32(), r.u32()
    out['pipes_170'] = [r.ref() for _ in range(pipes)]
    out['department_a4'] = r.ref()
    out['totals_110'] = [r.s64() for _ in range(4)]
    return out


SCHEMAS = {0x0990C075: ('cSC4PlumbingSimulator', water_simulator)}

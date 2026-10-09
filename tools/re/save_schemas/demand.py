"""Save decoders of demand (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def demand_simulator(r):
    """cSC4DemandSimulator (0x2990BFF2): Write at 0x006A2680, Read at
    0x006A3C50 (versions 0 to 2; before 2, four ignored words first). Names
    give the object offsets; the cIGZSerializable part sits at +0x8
    (docs/demand.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 2:
        raise struct.error('version %d not known' % v)
    if v < 2:
        out['old'] = [r.u32() for _ in range(4)]
    out['saved_a0'] = [r.f32() for _ in range(4)]
    out['demands_50'] = [[r.u32(), r.ref()] for _ in range(r.u32())]
    out['counted_28'] = [[r.u32(), r.f32()] for _ in range(r.u32())]
    return out


def demand(r):
    """cSC4Demand (0x8990C12D): Write at 0x005D5080, Read at 0x005D5150
    (versions 0 to 2; version 1 has the cap as a percentage byte). Names give
    the object offsets; the cIGZSerializable part sits at +0x4
    (docs/demand.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 2:
        raise struct.error('version %d not known' % v)
    out['id_10'] = r.u32()
    for name in ('supply_14', 'demand_18', 'new_supply_1c', 'new_demand_20', 'f24', 'f28', 'min_30', 'max_2c',
                 'active_34'):
        out[name] = r.f32()
    out['flag_38'] = r.u8()
    out['economy_3c'], out['tax_40'] = r.f32(), r.f32()
    out['cap_44'] = r.u8() if v == 1 else r.f32()
    return out


def demands(r):
    """Every cSC4Demand of the city is in one entry, one record after the
    other. Their CRCs and IDs are dropped, so two runs compare."""
    items = []
    while r.pos < len(r.data):
        item = demand(r)
        del item['crc'], item['id']
        items.append(item)
    # The save writes the objects in an order of its own (heap addresses):
    # compared in ID order.
    items.sort(key=lambda item: (item['id_10'], repr(item)))
    return {'crc': 0, 'id': 0, 'demands': items}


SCHEMAS = {0x2990BFF2: ('cSC4DemandSimulator', demand_simulator),
           0x8990C12D: ('cSC4Demand', demands)}

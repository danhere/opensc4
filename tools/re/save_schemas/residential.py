"""Save decoders of the residential (education and health) simulator
(tools/re/sc4save.py). SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def block(r):
    """A school or health block (0x006F7B00): offsets are the block's."""
    return {'f0': [r.f32(), r.f32(), r.f32()], 'n0c': r.s32(), 'f10': r.f32(), 'strike_44': r.u8(),
            'day_48': r.s32(), 'refs_4c': [r.ref(), r.ref()], 'f54': [r.f32(), r.f32()], 'n5c': r.u32()}


def residential_simulator(r):
    """cSC4ResidentialSimulator (0x4990C013): Write at 0x006F8C40, Read at
    0x007009A0 (version 7; Read takes 1 to 7). Names give the object
    offsets (docs/residential.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 7:
        raise struct.error('version %d not known' % v)
    out['map_flag_34'] = r.s32()
    out['proximity_38'] = [r.ref() for _ in range(3)]
    out['cell_size_44'] = r.s32()
    out['population_48'] = [r.ref() for _ in range(3)]
    out['grid_54'] = r.ref()
    out['grids_58'] = [r.ref() for _ in range(r.u32())]
    out['grids_64'] = [r.ref() for _ in range(r.u32())]
    out['grids_70'] = [r.ref(), r.ref()]
    out['quality_cell_size_78'] = r.s32()
    out['eq_7c'] = [r.ref() for _ in range(3)]
    out['hq_88'] = [r.ref() for _ in range(3)]
    out['floats_98'] = [r.f32(), r.f32()]
    out['state_94'] = r.u32()
    out['pending'] = r.u8()
    out['population_a4'] = r.s32()
    out['health_d4'] = block(r)
    out['school_134'] = block(r)
    out['lists_19c'] = [r.ref_list() for _ in range(3)]
    out['school_types_1a8'] = [r.ref_list() for _ in range(r.u32())]
    out['hospital_types_1b4'] = [r.ref_list() for _ in range(r.u32())]
    out['list_1c0'] = r.ref_list()
    out['iterator_328'] = [r.u8(), r.s32(), r.s32(), r.u32(), r.s32(), r.s32(), r.s32(), r.s32()]
    out['step_348'] = r.s64()
    out['a8'] = r.s32()
    out['auto_budget_3c8'] = [r.u8(), r.u8()]
    return out


SCHEMAS = {0x4990C013: ('cSC4ResidentialSimulator', residential_simulator)}

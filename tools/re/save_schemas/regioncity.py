"""Save decoder of the regional city (tools/re/sc4save.py, tools/sc4report.py).
SCHEMAS maps record types to (name, decoder)."""
import struct

from .citylot import holder

# The commodities of the figures map (docs/region.md, +0x180).
COMMODITIES = {1: 'power', 2: 'water', 3: 'garbage'}


def connection(r):
    """cSC4NeighborConnection (0x0A134F24) as it writes itself into the
    regional city's record: Write at 0x006CF080, version 1 (docs/neighbors.md)."""
    out = {'version': r.u16()}
    if out['version'] != 1:
        raise struct.error('connection version %d not known' % out['version'])
    out['type'], out['x'], out['z'], out['tile_x'], out['tile_z'] = r.u32(), r.s32(), r.s32(), r.s32(), r.s32()
    out['flags'] = r.u8()
    out['properties'] = holder(r)
    return out


def deal(r):
    """cSC4NeighborDeal (0x2A134F42): Write at 0x006CE400 (version 5), Read at
    0x006CE4E0 (the suspension date from version 5; docs/neighbors.md)."""
    out = {'version': r.u16()}
    if out['version'] > 5:
        raise struct.error('deal version %d not known' % out['version'])
    out['type'], out['other_x'], out['other_z'] = r.u32(), r.s32(), r.s32()
    out['amount'], out['amount_used'], out['cost'], out['flags'] = r.s32(), r.s32(), r.s64(), r.u32()
    out['city_x'], out['city_z'], out['inc_amount'], out['last_amount'] = r.s32(), r.s32(), r.s32(), r.s32()
    out['amount_used_again'] = r.s32()
    out['current'], out['start'], out['expiration'] = r.u32(), r.u32(), r.u32()
    out['properties'] = holder(r)
    out['suspension'] = r.u32() if out['version'] > 4 else 0
    return out


def number_map(r):
    count = r.u32()
    if count > 0x20000000:
        raise struct.error('map count %d out of range' % count)
    return {r.u32(): r.s32() for _ in range(count)}


def regional_city(r):
    """cSC4RegionalCity (record 0xCA027EDB / 0xCA027EE1 / 0 of a city file):
    Write at 0x004A7D10 (version 13), Read at 0x004AC5B0 (up to 13). The
    record is the object's own fields with no framework header (SaveToFile
    0x005DC2E0 opens a stream on it), but real saves start with a uint16 1
    before the version, from the segment stream (inferred, 491 forum saves).
    Names give the object offsets (docs/region.md, "The regional city")."""
    out = {'stream_version': r.u16()}
    if out['stream_version'] != 1:
        raise struct.error('stream version %d not known' % out['stream_version'])
    out['version'] = r.u16()
    v = out['version']
    if v > 13:
        raise struct.error('version %d not known' % v)
    out['x_14'], out['y_18'], out['size_x_1c'], out['size_y_20'] = r.s32(), r.s32(), r.s32(), r.s32()
    out['population_24'], out['commercial_jobs_28'], out['industrial_jobs_2c'] = r.s32(), r.s32(), r.s32()
    if v >= 11:
        out['workforce_30'] = r.f32()
    elif v == 10:
        out['workforce_30'] = r.f32() * 0.01  # saved in percent
    else:
        out['workforce_30'] = 0.5
    out['mayor_rating_34'] = r.s8()
    out['difficulty_38'] = r.u8() if v >= 13 else 2
    out['moved_a0'] = r.u8()
    out['serial_a4'] = r.u32()
    out['language_a8'] = [r.s32() for _ in range(4)]
    out['birth_date_b8'] = r.u32()
    out['established_168'] = r.u8()
    out['city_name_d0'], out['string_e4'], out['mayor_name_f8'] = r.gzstr(), r.gzstr(), r.gzstr()
    out['string_10c'], out['utility_advisor_154'], out['description_120'] = r.gzstr(), r.gzstr(), r.gzstr()
    out['string_134'] = r.gzstr()
    out['world_148'] = [r.f32() for _ in range(3)]
    out['tutorial_16c'] = r.s32()
    out['population_3c'] = number_map(r)
    out['extrapolated_48'] = number_map(r)
    out['limit_54'] = number_map(r)
    taxes = r.u32()
    if taxes > 12:
        raise struct.error('%d tax rates' % taxes)
    out['tax_rates_60'] = [r.f32() for _ in range(taxes)]
    count = r.u32()
    if count > 0x100000:
        raise struct.error('%d connections' % count)
    out['connections_90'] = [connection(r) for _ in range(count)]
    count = r.u32()
    if count > 0x100000:
        raise struct.error('%d connection changes' % count)
    out['changes_94'] = [{'type': r.u32(), 'x': r.s32(), 'z': r.s32(), 'made': r.u8()} for _ in range(count)]
    out['budget_170'], out['income_178'] = r.f64(), r.f64()
    count = r.s32()
    if not 0 <= count <= 0x100000:
        raise struct.error('%d commodities' % count)
    commodities = {}
    for _ in range(count):
        key = r.s32()
        produced, demanded, imported, exported, cost = (r.f64() for _ in range(5))
        commodities[key] = {'produced': produced, 'demanded': demanded, 'exported': exported, 'imported': imported,
                            'cost': cost}
    out['commodities_180'] = commodities
    # Read puts both runs on +0x18C; Write saves +0x18C and +0x190 apart.
    out['deals_18c'] = [deal(r) for _ in range(r.s32())]
    out['deals_190'] = [deal(r) for _ in range(r.s32())]
    points = {}
    if v >= 12:
        for _ in range(r.u32()):
            kind = r.u32()
            points[kind] = [(r.f32(), r.f32(), r.f32()) for _ in range(r.u32())]
    out['points_194'] = points
    return out


SCHEMAS = {0xCA027EDB: ('cSC4RegionalCity', regional_city)}

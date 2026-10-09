"""Save decoders of civic (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def civic_simulator(r):
    """cSC4CivicBuildingSimulator (0x0A413481): Write at 0x0069D920, Read at
    0x0069E820 (version 6 only). Names give the object offsets; the
    cIGZSerializable part sits at +0x8 (docs/civic.md). The hash tables are
    in bucket order."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 6:
        raise struct.error('version %d not known' % v)
    out['excluded_18'] = [r.u32() for _ in range(r.u32())]
    out['statuses_2c'] = [[r.u32(), r.u8(), r.u16(), r.gzstr()] for _ in range(r.u32())]
    out['department_14'] = r.ref()
    out['zoo_40'] = r.ref_list()
    out['landmarks_44'] = r.ref_list()
    return out


SCHEMAS = {0x0A413481: ('cSC4CivicBuildingSimulator', civic_simulator)}

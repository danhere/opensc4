"""Save decoder of the lot configuration manager (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def lot_configuration_manager(r):
    """cSC4LotConfigurationManager (0x299062C0): Write at 0x00489850
    (version 1), Read at 0x004898A0 (version 1 only; both get the object +
    8). Only the three services Init would take: the configurations
    themselves are static data loaded from the exemplars, not saved. Names
    give the object offsets (src/core/lotconfig_sim.h)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['development_14'] = r.ref()
    out['flora_18'] = r.ref()
    out['props_1c'] = r.ref()
    return out


SCHEMAS = {0x299062C0: ('cSC4LotConfigurationManager', lot_configuration_manager)}

"""Save decoders of the commercial simulator (tools/re/sc4save.py). SCHEMAS
maps class IDs to (name, decoder). The industrial simulator saves no record."""
import struct

from .common import header


def commercial_simulator(r):
    """cSC4CommercialSimulator (0x8990C080): Write at 0x0069EC50, Read at
    0x0069ECB0 (version 1): whether a cycle is pending, then the two
    proximity maps as references (docs/commercial-industrial.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['pending'] = r.u8()
    out['map_3320'] = r.ref()
    out['map_3330'] = r.ref()
    return out


SCHEMAS = {0x8990C080: ('cSC4CommercialSimulator', commercial_simulator)}

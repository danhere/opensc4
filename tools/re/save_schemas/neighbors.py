"""Save decoders of neighbors (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def neighbors_simulator(r):
    """cSC4NeighborsSimulator (0xC990C0A4): Write at 0x006CFB60, Read at
    0x006CFDC0 (version 2): the connections as references
    (docs/neighbors.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    out['connections'] = r.ref_list()
    return out


SCHEMAS = {0xC990C0A4: ('cSC4NeighborsSimulator', neighbors_simulator)}

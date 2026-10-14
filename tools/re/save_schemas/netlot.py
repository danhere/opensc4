"""Save decoder of the network lot manager (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def network_lot_manager(r):
    """cSC4NetworkLotManager (0xE9A6CD3B): Write at 0x0060D6B0 (version 2),
    Read at 0x0060DAC0 (version 2 only; both get the object + 8). The record
    is the lot map (the object's +0xC) and the pothole map (+0x58), each in
    its hash table's order: a count, then per entry the uint16 cell key
    ((x << 8) | z: a city side of at most 256 cells), and a reference: a
    lot's network piece (the lot's +4; Read asks it for interface
    0xA821EF94), a pothole's occupant (interface 0x09820792). A lot also has
    the ID of its configuration (the configuration's slot 4); Read keeps it
    in the lot's +0xC and Init looks the configuration up afterwards. The
    configurations themselves, the configuration forced for the next piece
    and the set of cells waiting for an update are not saved
    (src/core/netlot_sim.h, docs/netlot.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    out['lots_0C'] = [{'cell': r.u16(), 'piece': r.ref(), 'configuration': r.u32()} for _ in range(r.u32())]
    out['potholes_58'] = [{'cell': r.u16(), 'occupant': r.ref()} for _ in range(r.u32())]
    return out


SCHEMAS = {0xE9A6CD3B: ('cSC4NetworkLotManager', network_lot_manager)}

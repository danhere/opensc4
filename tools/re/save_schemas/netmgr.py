"""Save decoder of the network manager (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def network_manager(r):
    """cSC4NetworkManager (0xC990BD46): Write at 0x006111E0 (version 1), Read
    at 0x006124C0 (version 1 only; both get the object + 8). The record is
    the vector at the object's +0x24 and nothing else: the pieces that are
    "managed globally" (a piece's slot 5 says so when it is inserted), as
    references in the vector's order. Read appends each reference it resolves
    (interface 0xA821EF94) to the vector and keeps the reference the read
    gave. The tools, the remap tables, the queue, the retrofit list and the
    crews are not saved (src/core/netmgr_sim.h, docs/netmgr.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 1:
        raise struct.error('version %d not known' % v)
    out['managed_24'] = [r.ref() for _ in range(r.u32())]
    return out


SCHEMAS = {0xC990BD46: ('cSC4NetworkManager', network_manager)}

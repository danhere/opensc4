"""Save decoder of the city's bridge pieces (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .common import header
from .prebuiltocc import prebuilt_record


def bridge(r):
    """cSC4NetworkBridgeOccupant (0x49CC1BCD). The class overrides only
    GetGZCLSID (0x00604BB0) of the serializable part: Write and Read are the
    pre-built-model network occupant's, 0x0061D8E0 (version 4) and
    0x0061D9B0 (versions 3 and 4); both get the object + 8. So a bridge's
    record is that class's with the bridge's class ID: the version, the
    network occupant's whole record (0x0061A190), then that class's fields
    (tools/re/save_schemas/prebuiltocc.py decodes all of it). The bridge's
    own member (the object at +0x1D0) is not saved."""
    start = r.pos
    out = header(r)
    out.update(prebuilt_record(r))
    if r.pos - start != out['size']:
        raise struct.error('bridge occupant used %d bytes of %d' % (r.pos - start, out['size']))
    return out


# One record holds every bridge piece, one after the other.
SCHEMAS = {0x49CC1BCD: ('cSC4NetworkBridgeOccupant', every(bridge))}

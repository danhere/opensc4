"""Save decoder of the city's tunnel entrances (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder).

One save of the parity corpora holds a tunnel (class 0x8A4BD52B):
parity/saves-networks/Rush Hour Tutorial/t0000.sc4, one road tunnel, so two
objects of 282 bytes that refer to each other; both decode with every byte
consumed. The 12 saves of parity/saves, the other two of saves-networks and
the saves of saves-plopped and saves-ports have no record of the class. The
record it embeds, the pre-built-model network occupant's (class 0x49C1A034,
Write 0x0061D8E0), is decoded by tools/re/save_schemas/prebuiltocc.py's
`prebuilt_record`."""
import struct

from .budget import every
from .common import header
from .prebuiltocc import prebuilt_record


def tunnel(r):
    """cSC4NetworkTunnelOccupant (0x8A4BD52B): Write at 0x00647580 (version
    2), Read at 0x00647610 (version 2 only); both get the object + 8. After
    the version comes the pre-built-model network occupant's record (its
    own version, the network occupant's record and its fields, under
    'prebuilt'; when its Write fails nothing follows), then a byte: 1 when
    the tunnel has its other end (the pointer at +0x1D0, which answers the
    serializable IID 0xE4FDA3D4), followed by the reference to it
    (0x005D99A0: its address-ID and its class ID, 0x8A4BD52B again; Read
    gets it with 0x005D9730 for that ID), or 0 and nothing."""
    start = r.pos
    out = header(r)
    out['version'] = v = r.u16()
    if v != 2:
        raise struct.error('version %d not known' % v)
    out['prebuilt'] = prebuilt_record(r)
    out['has_other_end'] = has = r.u8()
    if has > 1:
        raise struct.error('other end byte %d not known' % has)
    if has:
        out['other_end_1d0'] = r.ref()
    if r.pos - start != out['size']:
        raise struct.error('tunnel occupant used %d bytes of %d' % (r.pos - start, out['size']))
    return out


# One record holds every tunnel entrance, one after the other.
SCHEMAS = {0x8A4BD52B: ('cSC4NetworkTunnelOccupant', every(tunnel))}

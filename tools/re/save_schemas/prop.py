"""Save decoders of the prop developer and its wealth requester
(tools/re/sc4save.py). SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .budget import every
from .common import header


def prop_developer(r):
    """cSC4PropDeveloper (0x89C48F47): Write at 0x006F1930 (version 5), Read
    at 0x006F3620 (versions 4 and 5; both get the object + 8). Names give
    the object offsets (src/core/prop_sim.h)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v not in (4, 5):
        raise struct.error('version %d not known' % v)
    out['optional_state_14'] = r.u8()
    # The optional props' cell iterator (0x005DA480): state, x, z, step and
    # the rectangle.
    out['iterator_18'] = {'state': r.s8(), 'fields': [r.s32() for _ in range(7)]}
    out['requester_added_a00'] = r.u8()
    out['requesters_a04'] = r.ref_list()
    out['lot_manager_a10'] = r.ref()
    out['prop_service_a14'] = r.ref()
    out['network_lots_a1c'] = r.ref()
    # The day and night stepping; it follows frames, not game days.
    out['timed_index_a2c'] = r.u32()
    out['timed_state_a30'] = r.u32()
    out['timed_step_a34'] = r.u32()
    out['timed_ms_a38'] = r.u32()
    out['night_a3c'] = r.u8()
    out['timed_props_a20'] = r.ref_list()
    # The table +0xA48, in bucket order: key, then its list of props.
    out['table_a48'] = [[r.u32(), r.ref_list()] for _ in range(r.u32())]
    out['list_a60'] = r.ref_list()
    out['list_a5c'] = r.ref_list()
    out['list_a64'] = r.ref_list()
    if v >= 5:
        out['signs_visible_a68'] = r.u8()
        out['signs_a6c'] = r.ref_list()
    return out


def wealth_requester(r):
    """cSC4WealthRequester (0x89C49986): Write and Read are the shared stub
    0x004FC900, so an object is only its header."""
    return header(r)


SCHEMAS = {
    0x89C48F47: ('cSC4PropDeveloper', prop_developer),
    0x89C49986: ('cSC4WealthRequester', every(wealth_requester)),
}

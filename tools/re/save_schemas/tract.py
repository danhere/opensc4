"""Save decoder of the tract developer (tools/re/sc4save.py).
SCHEMAS maps class IDs to (name, decoder)."""
import struct

from .common import header


def tract_developer(r):
    """cSC4TractDeveloper (0x2990C142): Write at 0x00704B60 (version 3),
    Read at 0x00707950 (versions 0 to 3; both get the object + 8). Names
    give the object offsets (docs/tract.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 3:
        raise struct.error('version %d not known' % v)
    out['style_index_124'] = r.s32()
    out['style_years_128'] = r.s32()
    if v >= 2:
        out['one_style_12c'] = r.u8()
        out['active_styles_118'] = [r.u32() for _ in range(r.u32())]
    if v >= 3:
        out['years_between_7c'] = r.s32()
    return out


SCHEMAS = {0x2990C142: ('cSC4TractDeveloper', tract_developer)}

"""Save decoders of ordinance (tools/re/sc4save.py). SCHEMAS maps class IDs to
(name, decoder)."""
import struct

from .common import header


def ordinance_simulator(r):
    """cSC4OrdinanceSimulator (0xA990C08E): Write at 0x0080D780, Read at
    0x0080E8B0 (version 4 only); the cIGZSerializable part sits at +0x8
    (docs/ordinance.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 4:
        raise struct.error('version %d not known' % v)
    out['expense_18'], out['income_20'] = r.s64(), r.s64()
    out['ordinances_30'] = [r.ref() for _ in range(r.u32())]
    out['pending_28'] = r.s64()
    return out


def ordinance(r):
    """cSC4OrdinanceSimple and its derived classes (21 class IDs): Write at
    0x0080CB30, Read at 0x0080D0B0 (version 4 written; Read knows 1 to 4).
    The property holder's own record follows, kept as bytes
    (docs/ordinance.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    out['initialised_28'] = r.u8()
    out['id_38'] = r.u32()
    out['name_3c'], out['description_50'] = r.gzstr(), r.gzstr()
    out['year_64'] = r.u32()
    out['chance_68'] = r.f32()
    out['enactment_70'], out['repeal_78'], out['constant_80'] = r.s64(), r.s64(), r.s64()
    out['factor_88'] = r.f32()
    out['advisor_8c'] = r.u32()
    out['available_91'], out['on_92'], out['enabled_93'] = r.u8(), r.u8(), r.u8()
    out['adjusted_98'] = r.s64()
    if v >= 2:
        out['income_ordinance_90'] = r.u8()
    out['holder'] = r.data[r.pos:].hex()
    r.pos = len(r.data)
    return out


ORDINANCE_CLASSES = (0x8BD4FC74, 0xA2BF1DDC, 0xA2BF1E43, 0x00D0723D, 0x40D07236, 0x815B4CEF, 0x22F6E80C,
                     0xA0D07129, 0x62BF1DAA, 0x62BF1DB9, 0xE0D0722E, 0x22F6E81B, 0x82B9999B, 0xE0D07233,
                     0xA2BF1DE5, 0xC2F6E81F, 0x62F6E7CF, 0x42BF1E18, 0xC2BF1E04, 0x02BF1DFA, 0xC2BF1DC5)


SCHEMAS = {0xA990C08E: ('cSC4OrdinanceSimulator', ordinance_simulator)}
SCHEMAS.update({clsid: ('ordinance %08X' % clsid, ordinance) for clsid in ORDINANCE_CLASSES})

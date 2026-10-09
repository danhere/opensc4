"""Save decoders of the budget simulator (tools/re/sc4save.py). SCHEMAS maps
class IDs to (name, decoder)."""
import struct

from .common import header


def budget_simulator(r):
    """cSC4BudgetSimulator (0xE990BE01): Write at 0x00690530 (version 5), Read
    at 0x00694B00 (versions 4 and 5); the cIGZSerializable part sits at +0x8.
    Names give the object offsets (docs/budget.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v not in (4, 5):
        raise struct.error('version %d not known' % v)
    out['funds_28'], out['funds_30'], out['bonds_owed_38'], out['tax_670'] = r.s64(), r.s64(), r.s64(), r.s64()
    out['departments_54'] = [{'key': r.u32(), 'department': r.ref()} for _ in range(r.u32())]
    groups = []
    for _ in range(12):
        group = {'rate': r.f32(), 'value': r.f32(), 'history': [r.s64() for _ in range(12)]}
        group['d8'], group['assessed'], group['tax'] = r.s64(), r.s64(), r.s64()
        groups.append(group)
    out['tax_groups_70'] = groups
    out['trade_680'], out['trade_688'] = r.s64(), r.s64()
    out['trade_expense_690'] = [r.s64() for _ in range(12)]
    out['trade_income_6f0'] = [r.s64() for _ in range(12)]
    out['year_750_778'] = [r.s64() for _ in range(6)]
    bonds = []
    for _ in range(r.u32()):
        bond = {'month': r.s32(), 'year': r.s32()}
        for name in ('principal', 'cost', 'owed', 'paid', 'payment', 'balance'):
            bond[name] = r.s64()
        bonds.append(bond)
    out['bonds_780'] = bonds
    out['bond_payments_788'] = r.s64()
    out['totals_830'] = [r.s64() for _ in range(4)]
    groups = []
    for _ in range(r.u32()):
        group = {'key': r.u32(), 'id': r.u32(), 'name': r.gzstr(), 'expense': r.s64(), 'income': r.s64()}
        group['departments'] = [r.u32() for _ in range(r.u32())]
        groups.append(group)
    out['groups_60'] = groups
    return out


SCHEMAS = {0xE990BE01: ('cSC4BudgetSimulator', budget_simulator)}

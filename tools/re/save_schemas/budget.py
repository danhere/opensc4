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


def one_department(r):
    """cSC4DepartmentBudget (0xE990BFFC): Write at 0x006A9D70 (version 10),
    Read at 0x006AC1D0 (up to 10; before 10 the fundings are in percent); the
    cIGZSerializable part sits at +0x4 (docs/budget.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v > 10:
        raise struct.error('version %d not known' % v)
    out['initialised_8'], out['id_10'], out['name_14'], out['group_30'] = r.u8(), r.u32(), r.gzstr(), r.u32()
    out['funding_34'], out['max_funding_38'], out['spending_40'] = r.f32(), r.f32(), r.s64()
    out['fixed_48'] = r.u8()
    out['lines_4c'] = [r.ref() for _ in range(r.u32())]
    out['local_58'] = [{'holder': r.ref(), 'purpose': r.u32()} for _ in range(r.u32())]
    out['name_instance_28'], out['name_group_2c'] = r.u32(), r.u32()
    return out


def one_line_item(r):
    """cSC4LineItem (0xAA313C9F): Write at 0x006A9AA0, Read at 0x006A9B60
    (version 4); the cIGZSerializable part sits at +0x4 (docs/budget.md)."""
    out = header(r)
    out['version'] = v = r.u16()
    if v != 4:
        raise struct.error('version %d not known' % v)
    out['name_c'] = r.gzstr()
    out['secondary_30'], out['full_38'], out['current_40'], out['income_48'] = r.s64(), r.s64(), r.s64(), r.s64()
    out['id_54'], out['flags_58'], out['local_5c'], out['type_64'] = r.u32(), r.u32(), r.u8(), r.u16()
    out['department_60'] = r.ref()
    out['name_instance_20'], out['name_group_24'] = r.u32(), r.u32()
    return out


def without_addresses(value):
    """References (tuples) without the heap address they were saved as."""
    if isinstance(value, tuple):
        return (value[0] != 0, value[1])
    if isinstance(value, list):
        return [without_addresses(v) for v in value]
    if isinstance(value, dict):
        return {k: without_addresses(v) for k, v in value.items()}
    return value


def every(one):
    """A record of many objects of one class, one after the other (in an
    order that follows heap addresses, so they are sorted here). The
    objects' CRCs and address-IDs are left out (sc4save.py --compare skips
    them only at the top), the top's are zero."""
    def decode(r):
        objects = []
        while r.pos < len(r.data):
            fields = one(r)
            del fields['crc'], fields['id']
            objects.append(fields)
        objects.sort(key=lambda o: repr(sorted((k, without_addresses(v)) for k, v in o.items())))
        return {'crc': 0, 'id': 0, 'objects': objects}
    return decode


SCHEMAS = {
    0xE990BE01: ('cSC4BudgetSimulator', budget_simulator),
    0xE990BFFC: ('cSC4DepartmentBudget', every(one_department)),
    0xAA313C9F: ('cSC4LineItem', every(one_line_item)),
}

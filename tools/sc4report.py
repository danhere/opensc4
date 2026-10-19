#!/usr/bin/env python3
"""Reports from save files, without the game (docs/sc4report.md).

    sc4report.py region <region folder> [-o file.csv]
        One CSV row per city file (`City - *.sc4`) of the folder, from each
        file's regional city record: the figures the region view shows.

    sc4report.py budget <city.sc4 | folder> [--departments] [-o file.csv]
        One CSV row per city from its budget record: funds, bonds and taxes;
        with --departments, one row per department budget instead.

    sc4report.py demand <city.sc4 | folder> [--all] [-o file.csv]
        One CSV row per city from its demand records: the RCI demand of the
        twelve groups (what the RCI bars show) and their caps; with --all,
        one row per demand object instead.

    sc4report.py ordinances <city.sc4 | folder> [--all] [-o file.csv]
        One CSV row per city from its ordinance records: how many ordinances
        are available and enacted, which are enacted, and last month's
        ordinance expense and income; with --all, one row per ordinance.

More commands can read the other records tools/re/save_schemas decodes.
"""
import argparse
import csv
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 're'))
import savediff  # noqa: E402  (package reader: load())
from save_schemas import Reader  # noqa: E402
from save_schemas.budget import budget_simulator, one_department, one_line_item  # noqa: E402
from save_schemas.demand import demands  # noqa: E402
from save_schemas.ordinance import ORDINANCE_CLASSES, ordinance, ordinance_simulator  # noqa: E402
from save_schemas.regioncity import COMMODITIES, regional_city  # noqa: E402

CITY_RECORD = (0xCA027EDB, 0xCA027EE1, 0)

# The population types of the regional city's maps: the developer types
# (src/core/tool_query_5b.cpp) with 1 in the low digit.
POPULATION_TYPES = [(0x1011, 'R$'), (0x1021, 'R$$'), (0x1031, 'R$$$'), (0x3111, 'Cs$'), (0x3121, 'Cs$$'),
                    (0x3131, 'Cs$$$'), (0x3321, 'Co$$'), (0x3331, 'Co$$$'), (0x4101, 'Ir'), (0x4201, 'Id'),
                    (0x4301, 'Im'), (0x4401, 'Ih')]

COLUMNS = (['file', 'status', 'city', 'mayor', 'x', 'y', 'size_km', 'established', 'residents', 'commercial_jobs',
            'industrial_jobs'] + ['pop_' + name for _, name in POPULATION_TYPES] +
           ['pop_other', 'mayor_rating', 'difficulty', 'budget', 'income'] + ['tax_%d' % i for i in range(12)] +
           ['%s_%s' % (c, f) for c in COMMODITIES.values() for f in ('produced', 'demanded')] +
           ['connections'])


def text(value):
    """A save string as the game wrote it: UTF-8 (Reader.gzstr gives latin-1)."""
    return value.encode('latin-1').decode('utf-8', 'replace')


def city_row(fields):
    """The CSV row of one decoded regional city record."""
    population = fields['population_3c']
    known = {key for key, _ in POPULATION_TYPES}
    row = {
        'status': 'ok' if fields['established_168'] else 'unfounded',
        'city': text(fields['city_name_d0']),
        'mayor': text(fields['mayor_name_f8']),
        'x': fields['x_14'],
        'y': fields['y_18'],
        'size_km': fields['size_x_1c'],
        'established': fields['established_168'],
        'residents': fields['population_24'],
        'commercial_jobs': fields['commercial_jobs_28'],
        'industrial_jobs': fields['industrial_jobs_2c'],
        'pop_other': ';'.join('%04X=%d' % (k, n) for k, n in sorted(population.items()) if k not in known),
        'mayor_rating': fields['mayor_rating_34'],
        'difficulty': fields['difficulty_38'],
        'budget': fields['budget_170'],
        'income': fields['income_178'],
        'connections': len(fields['connections_90']),
    }
    for key, name in POPULATION_TYPES:
        row['pop_' + name] = population.get(key, 0)
    for i in range(12):
        rates = fields['tax_rates_60']
        row['tax_%d' % i] = round(rates[i], 4) if i < len(rates) else ''
    for key, name in COMMODITIES.items():
        figures = fields['commodities_180'].get(key, {})
        row[name + '_produced'] = figures.get('produced', 0.0)
        row[name + '_demanded'] = figures.get('demanded', 0.0)
    return row


def census(folder):
    """One row per city file of the folder, in name order. A file that cannot
    be read or decoded gets a row with the reason in `status`."""
    rows = []
    for path in sorted(glob.glob(os.path.join(folder, 'City - *.sc4'))):
        row = {'file': os.path.basename(path)}
        try:
            data = savediff.load(path).get(CITY_RECORD)
        except (OSError, struct.error, KeyError, IndexError) as error:
            data, row['status'] = None, 'unreadable: %s' % error
        if data is not None:
            r = Reader(data)
            try:
                row.update(city_row(regional_city(r)))
            except struct.error as error:
                row['status'] = 'undecoded: %s' % error
        elif 'status' not in row:
            row['status'] = 'no regional city record'
        rows.append(row)
    return rows


BUDGET = 0xE990BE01
DEPARTMENTS = 0xE990BFFC
LINE_ITEMS = 0xAA313C9F

BUDGET_COLUMNS = (['file', 'status', 'funds', 'bonds', 'bonds_owed', 'bond_payments', 'tax_income',
                   'department_expense', 'department_income'] +
                  ['tax_%d' % i for i in range(12)] + ['assessed_%d' % i for i in range(12)])

DEPARTMENT_COLUMNS = ['file', 'group', 'department', 'id', 'name_instance', 'funding', 'max_funding', 'fixed',
                      'full_expense', 'expense', 'income', 'lines', 'local_objects']


def records(data, type_id):
    """The record of every key of that type (a type can be saved under more
    than one group and instance)."""
    return [value for key, value in sorted(data.items()) if key[0] == type_id]


def objects(data, type_id, decoder):
    """Every object of the records of a many-object class, by address-ID."""
    out = {}
    for record in records(data, type_id):
        r = Reader(record)
        while r.pos < len(r.data):
            fields = decoder(r)
            out[fields['id']] = fields
    return out


def departments(budget, data):
    """One row per department of the budget, in its order: the department
    record with its line items summed (expense lines' full and current
    expense, every line's income) and its group's name."""
    budgets = objects(data, DEPARTMENTS, one_department)
    lines = objects(data, LINE_ITEMS, one_line_item)
    group_of = {}
    for group in budget['groups_60']:
        for department in group['departments']:
            group_of[department] = group['name']
    rows = []
    for entry in budget['departments_54']:
        department = budgets.get(entry['department'][0])
        if department is None:
            rows.append({'id': '%08X' % entry['key'], 'department': '', 'group': group_of.get(entry['key'], '')})
            continue
        items = [lines[ref[0]] for ref in department['lines_4c'] if ref[0] in lines]
        expenses = [item for item in items if item['type_64'] == 0]
        rows.append({
            'group': group_of.get(department['id_10'], ''),
            'department': department['name_14'],
            'id': '%08X' % department['id_10'],
            'name_instance': '%08X' % department['name_instance_28'],
            'funding': round(department['funding_34'], 4),
            'max_funding': round(department['max_funding_38'], 4),
            'fixed': department['fixed_48'],
            'full_expense': sum(item['full_38'] for item in expenses),
            'expense': sum(item['current_40'] for item in expenses),
            'income': sum(item['income_48'] for item in items),
            'lines': len(department['lines_4c']),
            'local_objects': len(department['local_58']),
        })
    return rows


def budget_row(budget, rows):
    """The CSV row of one decoded budget record and its department rows."""
    groups = budget['tax_groups_70']
    row = {
        'status': 'ok',
        'funds': budget['funds_28'],
        'bonds': len(budget['bonds_780']),
        'bonds_owed': budget['bonds_owed_38'],
        'bond_payments': sum(bond['payment'] for bond in budget['bonds_780']),
        'tax_income': budget['tax_670'],
        'department_expense': sum(r.get('expense', 0) for r in rows),
        'department_income': sum(r.get('income', 0) for r in rows),
    }
    for i, group in enumerate(groups):
        row['tax_%d' % i] = round(group['rate'], 4)
        row['assessed_%d' % i] = group['assessed']
    return row


def budget_report(paths, by_department):
    """A summary row per city file (or its department rows). A file that
    cannot be read or decoded gets a row with the reason in `status`, which
    the department report keeps in its `department` column."""
    out = []
    for path in paths:
        name = os.path.basename(path)
        try:
            data = savediff.load(path)
        except (OSError, struct.error, KeyError, IndexError) as error:
            data, status = {}, 'unreadable: %s' % error
        else:
            status = 'no budget record'
        found = records(data, BUDGET)
        try:
            if found:
                budget = budget_simulator(Reader(found[0]))
                rows = departments(budget, data)
        except struct.error as error:
            found, status = None, 'undecoded: %s' % error
        if found:
            if by_department:
                out.extend(dict(row, file=name) for row in rows)
            else:
                out.append(dict(budget_row(budget, rows), file=name))
            continue
        out.append({'file': name, 'department' if by_department else 'status': status})
    return out


DEMAND = 0x8990C12D

# The city's own demand of each RCI group (docs/demand.md: the group, plus
# 0x20000), the one its bar shows.
DEMAND_GROUPS = [(0x21010, 'R$'), (0x21020, 'R$$'), (0x21030, 'R$$$'), (0x23110, 'Cs$'), (0x23120, 'Cs$$'),
                 (0x23130, 'Cs$$$'), (0x23320, 'Co$$'), (0x23330, 'Co$$$'), (0x24100, 'Ir'), (0x24200, 'Id'),
                 (0x24300, 'Im'), (0x24400, 'Ih')]

DEMAND_COLUMNS = (['file', 'status', 'demands'] + [name for _, name in DEMAND_GROUPS] +
                  [name + '_cap' for _, name in DEMAND_GROUPS])

DEMAND_ALL_COLUMNS = ['file', 'id', 'supply', 'demand', 'min', 'max', 'active', 'economy', 'tax', 'cap']


def cap(item):
    """The cap as a fraction (version 1 saves it as a percentage byte)."""
    return item['cap_44'] / 100.0 if item['version'] == 1 else item['cap_44']


def demand_rows(items, every):
    """The summary row of a city's demand objects, or with every, a row per
    object in ID order."""
    if every:
        return [{'id': '%05X' % item['id_10'], 'supply': round(item['supply_14'], 3),
                 'demand': round(item['demand_18'], 3), 'min': item['min_30'], 'max': item['max_2c'],
                 'active': round(item['active_34'], 3), 'economy': round(item['economy_3c'], 4),
                 'tax': round(item['tax_40'], 4), 'cap': round(cap(item), 4)} for item in items]
    by_id = {item['id_10']: item for item in items}
    row = {'status': 'ok', 'demands': len(items)}
    for key, name in DEMAND_GROUPS:
        item = by_id.get(key)
        row[name] = round(item['active_34'], 1) if item else ''
        row[name + '_cap'] = round(cap(item), 4) if item else ''
    return [row]


def demand_report(paths, every):
    """A row per city file (or a row per demand object). A file that cannot
    be read or decoded gets a row with the reason in `status`, which the
    --all report keeps in its `id` column."""
    out = []
    for path in paths:
        name = os.path.basename(path)
        try:
            data = savediff.load(path)
        except (OSError, struct.error, KeyError, IndexError) as error:
            data, status = {}, 'unreadable: %s' % error
        else:
            status = 'no demand record'
        items = []
        try:
            for record in records(data, DEMAND):
                items.extend(demands(Reader(record))['demands'])
        except struct.error as error:
            items, status = [], 'undecoded: %s' % error
        if items:
            out.extend(dict(row, file=name) for row in demand_rows(items, every))
        else:
            out.append({'file': name, 'id' if every else 'status': status})
    return out


ORDINANCE_SIMULATOR = 0xA990C08E

ORDINANCE_COLUMNS = ['file', 'status', 'ordinances', 'available', 'enacted', 'expense', 'income', 'pending',
                     'enacted_names']

ORDINANCE_ALL_COLUMNS = ['file', 'id', 'name', 'available', 'on', 'enacted', 'enabled', 'year', 'enactment',
                         'repeal', 'constant', 'factor', 'adjusted', 'income_ordinance']


def enacted(item):
    """IsOn: available and on (docs/ordinance.md)."""
    return bool(item['available_91'] and item['on_92'])


def ordinance_rows(simulator, items, every):
    """The summary row of a city's ordinances, or with every, a row per
    ordinance in ID order."""
    if every:
        return [{'id': '%08X' % item['id_38'], 'name': text(item['name_3c']), 'available': item['available_91'],
                 'on': item['on_92'], 'enacted': int(enacted(item)), 'enabled': item['enabled_93'],
                 'year': item['year_64'], 'enactment': item['enactment_70'], 'repeal': item['repeal_78'],
                 'constant': item['constant_80'], 'factor': round(item['factor_88'], 6),
                 'adjusted': item['adjusted_98'], 'income_ordinance': item.get('income_ordinance_90', '')}
                for item in items]
    row = {'status': 'ok', 'ordinances': len(items), 'available': sum(1 for item in items if item['available_91']),
           'enacted': sum(1 for item in items if enacted(item)),
           'enacted_names': ';'.join(text(item['name_3c']) for item in items if enacted(item))}
    if simulator:
        row.update(expense=simulator['expense_18'], income=simulator['income_20'], pending=simulator['pending_28'])
    return [row]


def ordinance_report(paths, every):
    """A row per city file (or a row per ordinance). A file that cannot be
    read or decoded gets a row with the reason in `status`, which the --all
    report keeps in its `id` column."""
    out = []
    for path in paths:
        name = os.path.basename(path)
        try:
            data = savediff.load(path)
        except (OSError, struct.error, KeyError, IndexError) as error:
            data, status = {}, 'unreadable: %s' % error
        else:
            status = 'no ordinance records'
        simulator, items = None, []
        try:
            found = records(data, ORDINANCE_SIMULATOR)
            if found:
                simulator = ordinance_simulator(Reader(found[0]))
            for clsid in ORDINANCE_CLASSES:
                items.extend(ordinance(Reader(record)) for record in records(data, clsid))
        except struct.error as error:
            simulator, items, status = None, [], 'undecoded: %s' % error
        items.sort(key=lambda item: item['id_38'])
        if simulator or items:
            out.extend(dict(row, file=name) for row in ordinance_rows(simulator, items, every))
        else:
            out.append({'file': name, 'id' if every else 'status': status})
    return out


def city_paths(parser, path):
    """The city files of the path: the file itself, or every *.sc4 of a folder."""
    if os.path.isdir(path):
        return sorted(glob.glob(os.path.join(path, '*.sc4')))
    if os.path.isfile(path):
        return [path]
    parser.error('no such file or folder: %s' % path)


def write_csv(args, columns, rows):
    out = open(args.output, 'w', newline='', encoding='utf-8') if args.output else sys.stdout
    try:
        writer = csv.DictWriter(out, fieldnames=columns, restval='', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    finally:
        if args.output:
            out.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    region = commands.add_parser('region', help='census of a region folder as CSV')
    region.add_argument('folder')
    region.add_argument('-o', '--output', help='CSV file to write (default: standard output)')
    budget = commands.add_parser('budget', help='funds, bonds, taxes and departments of cities as CSV')
    budget.add_argument('path', help='a city file, or a folder of them (every *.sc4)')
    budget.add_argument('--departments', action='store_true', help='one row per department budget')
    budget.add_argument('-o', '--output', help='CSV file to write (default: standard output)')
    demand = commands.add_parser('demand', help='RCI demand of cities as CSV')
    demand.add_argument('path', help='a city file, or a folder of them (every *.sc4)')
    demand.add_argument('--all', action='store_true', help='one row per demand object')
    demand.add_argument('-o', '--output', help='CSV file to write (default: standard output)')
    ordinances = commands.add_parser('ordinances', help='ordinances of cities as CSV')
    ordinances.add_argument('path', help='a city file, or a folder of them (every *.sc4)')
    ordinances.add_argument('--all', action='store_true', help='one row per ordinance')
    ordinances.add_argument('-o', '--output', help='CSV file to write (default: standard output)')
    args = parser.parse_args(argv)

    if args.command == 'budget':
        write_csv(args, DEPARTMENT_COLUMNS if args.departments else BUDGET_COLUMNS,
                  budget_report(city_paths(parser, args.path), args.departments))
        return 0
    if args.command == 'demand':
        write_csv(args, DEMAND_ALL_COLUMNS if args.all else DEMAND_COLUMNS,
                  demand_report(city_paths(parser, args.path), args.all))
        return 0
    if args.command == 'ordinances':
        write_csv(args, ORDINANCE_ALL_COLUMNS if args.all else ORDINANCE_COLUMNS,
                  ordinance_report(city_paths(parser, args.path), args.all))
        return 0
    if not os.path.isdir(args.folder):
        parser.error('not a folder: %s' % args.folder)
    write_csv(args, COLUMNS, census(args.folder))
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""Reports from save files, without the game (docs/sc4report.md).

    sc4report.py region <region folder> [-o file.csv]
        One CSV row per city file (`City - *.sc4`) of the folder, from each
        file's regional city record: the figures the region view shows.

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


def city_row(fields):
    """The CSV row of one decoded regional city record."""
    population = fields['population_3c']
    known = {key for key, _ in POPULATION_TYPES}
    row = {
        'status': 'ok' if fields['established_168'] else 'unfounded',
        'city': fields['city_name_d0'],
        'mayor': fields['mayor_name_f8'],
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    region = commands.add_parser('region', help='census of a region folder as CSV')
    region.add_argument('folder')
    region.add_argument('-o', '--output', help='CSV file to write (default: standard output)')
    args = parser.parse_args(argv)

    if not os.path.isdir(args.folder):
        parser.error('not a folder: %s' % args.folder)
    rows = census(args.folder)
    out = open(args.output, 'w', newline='', encoding='utf-8') if args.output else sys.stdout
    try:
        writer = csv.DictWriter(out, fieldnames=COLUMNS, restval='', lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    finally:
        if args.output:
            out.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())

"""tools/sc4report.py region: a version 13 regional city record built by hand,
put in a city file of a temporary region folder, and every column checked.

    python3 -m unittest discover -s tests/python
"""
import csv
import io
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import sc4report  # noqa: E402

from save_schemas import SCHEMAS  # noqa: E402  (sc4report put tools/re on the path)


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def gzstr(text):
    data = text.encode('latin-1')
    return pack('I', len(data)) + data


def holder(*properties):
    """A property holder (0x005FD990): version 2 and uint32 properties."""
    out = pack('HI', 2, len(properties))
    for key, value in properties:
        out += pack('IIIHHI', key, key, 0, 3, 0, value)
    return out


def connection():
    return pack('HIiiiiB', 1, 0x0A, 64, 0, 1, 0, 0) + holder()


def deal():
    out = pack('HIii', 5, 1, 1, 0) + pack('iiqI', 500, 400, 1200, 2) + pack('iiiii', 0, 0, 10, 390, 400)
    return out + pack('III', 730000, 729000, 740000) + holder((0x12345678, 7)) + pack('I', 0)


def record(established=1):
    """Version 13, as Write (0x004A7D10) gives it."""
    out = pack('H', 13)
    out += pack('7i', 4, 8, 2, 2, 12345, 3000, 2500)  # x, y, size x, size y, residents, jobs
    out += pack('f', 0.45) + pack('bB', -3, 1)  # workforce, mayor rating, difficulty
    out += pack('B', 0) + pack('I', 0xABCDEF0) + pack('4i', 0, 0, 0, 0) + pack('I', 2451545)
    out += pack('B', established)
    for text in ('Springfield', '', 'Dan', '', 'Utility Bob', 'A fine town', ''):
        out += gzstr(text)
    out += pack('3f', 1.0, 2.0, 250.0) + pack('i', -1)
    out += pack('I', 3) + pack('Ii', 0x1011, 9000) + pack('Ii', 0x3111, 800) + pack('Ii', 0x7777, 5)
    out += pack('I', 1) + pack('Ii', 0x1011, 9100)
    out += pack('I', 0)
    out += pack('I', 12) + pack('12f', *[9.0 + i for i in range(12)])
    out += pack('I', 2) + connection() + connection()
    out += pack('I', 1) + pack('IiiB', 0x0A, 64, 0, 1)
    out += pack('dd', 250000.0, -1500.5)
    out += pack('i', 2)
    out += pack('i', 1) + pack('5d', 6000.0, 4500.0, 100.0, 0.0, 0.01)  # produced, demanded, imported, exported, cost
    out += pack('i', 2) + pack('5d', 3000.0, 3200.0, 0.0, 200.0, 0.02)
    out += pack('i', 1) + deal()
    out += pack('i', 0)
    out += pack('I', 1) + pack('II', 0, 1) + pack('3f', 10.0, 20.0, 30.0)
    return out


def city_file(path, data):
    """The smallest DBPF savediff.load reads: the header with the index's
    count at 0x24 and offset at 0x28, one uncompressed entry."""
    header = bytearray(0x60)
    header[0:4] = b'DBPF'
    offset = len(header)
    index = pack('5I', 0xCA027EDB, 0xCA027EE1, 0, offset, len(data))
    struct.pack_into('<II', header, 0x24, 1, offset + len(data))
    with open(path, 'wb') as out:
        out.write(bytes(header) + data + index)


class RegionCensus(unittest.TestCase):
    def test_record_is_consumed_exactly(self):
        name, decoder = SCHEMAS[0xCA027EDB]
        self.assertEqual(name, 'cSC4RegionalCity')
        data = record()
        r = sc4report.Reader(data)
        fields = decoder(r)
        self.assertEqual(r.pos, len(data))
        self.assertEqual(fields['commodities_180'][1]['imported'], 100.0)
        self.assertEqual(fields['deals_18c'][0]['properties'], [[0x12345678, 3, 7]])
        self.assertEqual(fields['points_194'], {0: [(10.0, 20.0, 30.0)]})
        self.assertEqual(fields['changes_94'], [{'type': 0x0A, 'x': 64, 'z': 0, 'made': 1}])

    def test_every_column(self):
        with tempfile.TemporaryDirectory() as folder:
            city_file(os.path.join(folder, 'City - Springfield.sc4'), record())
            city_file(os.path.join(folder, 'City - New City.sc4'), record(established=0))
            with open(os.path.join(folder, 'City - Broken.sc4'), 'wb') as broken:
                broken.write(b'DBPF')
            with open(os.path.join(folder, 'region.ini'), 'w') as ini:
                ini.write('[Regional Settings]\n')
            out = io.StringIO()
            with redirect_stdout(out):
                self.assertEqual(sc4report.main(['region', folder]), 0)
        rows = list(csv.DictReader(io.StringIO(out.getvalue())))
        self.assertEqual([row['file'] for row in rows],
                         ['City - Broken.sc4', 'City - New City.sc4', 'City - Springfield.sc4'])
        self.assertTrue(rows[0]['status'].startswith('unreadable'))
        self.assertEqual(rows[1]['status'], 'unfounded')
        row = rows[2]
        expected = {
            'status': 'ok', 'city': 'Springfield', 'mayor': 'Dan', 'x': '4', 'y': '8', 'size_km': '2',
            'established': '1', 'residents': '12345', 'commercial_jobs': '3000', 'industrial_jobs': '2500',
            'pop_R$': '9000', 'pop_Cs$': '800', 'pop_R$$': '0', 'pop_Ih': '0', 'pop_other': '7777=5',
            'mayor_rating': '-3', 'difficulty': '1', 'budget': '250000.0', 'income': '-1500.5',
            'tax_0': '9.0', 'tax_11': '20.0', 'power_produced': '6000.0', 'power_demanded': '4500.0',
            'water_produced': '3000.0', 'water_demanded': '3200.0', 'garbage_produced': '0.0',
            'garbage_demanded': '0.0', 'connections': '2',
        }
        for column, value in expected.items():
            self.assertEqual(row[column], value, column)
        self.assertEqual(list(row), sc4report.COLUMNS)


if __name__ == '__main__':
    unittest.main()

"""tools/sc4report.py utilities: a power simulator record (version 6) and a
water simulator record (version 4) built by hand in a city file, and a city
with only the power record, both checked.

    python3 -m unittest discover -s tests/python
"""
import csv
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sc4report  # noqa: E402
from test_sc4report_budget import city_file, pack, with_header  # noqa: E402


def power():
    """As Write (0x006EB6A0) gives it: two plants, no poles, no bit words."""
    out = pack('H', 6) + pack('II', 0x01000001, 0xE990BFFC)  # department
    out += pack('iii', 3, 900, 800) + pack('f', 0.0) + pack('4i', 40, 37, 3, 850) + pack('q', 850)
    out += pack('5i', 1000, 700, 50, 0, 760)  # last month: produced, used, imported, exported, demand
    out += pack('I', 0) + pack('II', 0, 0) + pack('120i', *[0] * 120) + pack('10i', *[0] * 10)
    out += pack('i', 2) + pack('10i', *[0] * 10)
    for plant in (0x02000001, 0x02000002):
        out += pack('I', 1) + pack('II', plant, 0x278128A0)
    out += pack('II', 0, 0) + pack('I', 0) + pack('BB', 0, 0) + pack('i', 0) + pack('BB', 0, 0) + pack('I', 0)
    return with_header(0x01000100, out)


def water():
    """As Write (0x006DB090) gives it: a 2 x 2 cell grid, one pump, one pipe."""
    def producer(capacity, output):
        return pack('ii', 1, 1) + pack('4i', 0, 0, 1, 1) + pack('II', capacity, output) + pack('I', 0)
    out = pack('H', 4) + pack('I', 0) + pack('I', 0) + pack('II', 2, 2) + bytes(4)
    out += pack('iI', 5, 0) + pack('q', 4000) + pack('I', 1) + pack('I', 0) + producer(6000, 5000)
    out += producer(0, 0) + pack('b', 0) + pack('7i', *[0] * 7)
    out += pack('q', 6500)  # the buildings' demand
    out += pack('III', 1, 1, 0) + pack('II', 0x03000001, 0xC772BF98) + pack('I', 0)
    out += pack('4q', 6000, 5000, 0, 200)  # last month: capacity, used, imported, exported
    return with_header(0x01000200, out)


def run(*argv):
    out = io.StringIO()
    with redirect_stdout(out):
        sc4report.main(['utilities', *argv])
    return list(csv.DictReader(io.StringIO(out.getvalue())))


class UtilitiesReport(unittest.TestCase):
    def test_city(self):
        with tempfile.TemporaryDirectory() as folder:
            city_file(os.path.join(folder, 'City - Springfield.sc4'), [(0x0990C005, power()), (0x0990C075, water())])
            city_file(os.path.join(folder, 'City - Dry.sc4'), [(0x0990C005, power())])
            with open(os.path.join(folder, 'City - Broken.sc4'), 'wb') as broken:
                broken.write(b'DBPF')
            rows = run(folder)
            self.assertEqual(run(os.path.join(folder, 'City - Springfield.sc4')), rows[2:])
        self.assertEqual([row['file'] for row in rows],
                         ['City - Broken.sc4', 'City - Dry.sc4', 'City - Springfield.sc4'])
        self.assertTrue(rows[0]['status'].startswith('unreadable'))
        self.assertEqual((rows[1]['status'], rows[1]['power_produced'], rows[1]['water_capacity']),
                         ('ok (no water record)', '1000', ''))
        row = rows[2]
        self.assertEqual(list(row), sc4report.UTILITIES_COLUMNS)
        expected = {'status': 'ok', 'power_plants': '2', 'power_produced': '1000', 'power_used': '700',
                    'power_imported': '50', 'power_exported': '0', 'power_demand': '760', 'power_surplus': '240',
                    'power_needed': '850', 'power_consumers': '40', 'power_unpowered': '3',
                    'water_producers': '1', 'water_capacity': '6000', 'water_used': '5000', 'water_imported': '0',
                    'water_exported': '200', 'water_demand': '6500', 'water_surplus': '-500', 'water_pipes': '1',
                    'water_distressed_pipes': '1', 'water_burst_pipes': '0'}
        for column, value in expected.items():
            self.assertEqual(row[column], value, column)


if __name__ == '__main__':
    unittest.main()

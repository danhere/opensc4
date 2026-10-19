"""tools/sc4report.py demand: three demand objects built by hand (two of
version 2, one of version 1 with its percentage-byte cap) in one entry of a
city file, and both reports checked.

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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sc4report  # noqa: E402
from test_sc4report_budget import city_file, pack, with_header  # noqa: E402


def demand(address, version, ident, supply, demand, active, cap):
    """As Write (0x005D5080) gives it: the ID, nine floats (min before max),
    the byte, the economy and tax modifiers, the cap."""
    out = pack('HI', version, ident) + pack('9f', supply, demand, 0, 0, 0, 0, -6000, 6000, active)
    out += pack('B', 1) + pack('ff', 0.0625, -0.0078125)
    out += pack('B', cap) if version == 1 else pack('f', cap)
    return with_header(address, out)


def city(path):
    # In the save's own order, not by ID.
    city_file(path, [(0x8990C12D, demand(0x01000020, 2, 0x24400, 100, 50, -50, 0.25) +
                      demand(0x01000010, 2, 0x21010, 1000, 3000, 2000, 0.5) +
                      demand(0x01000030, 1, 0x1010, 7, 8, 1, 40))])


def run(*argv):
    out = io.StringIO()
    with redirect_stdout(out):
        sc4report.main(['demand', *argv])
    return list(csv.DictReader(io.StringIO(out.getvalue())))


class DemandReport(unittest.TestCase):
    def test_city(self):
        with tempfile.TemporaryDirectory() as folder:
            city(os.path.join(folder, 'City - Springfield.sc4'))
            with open(os.path.join(folder, 'City - Broken.sc4'), 'wb') as broken:
                broken.write(b'DBPF')
            rows = run(folder)
            self.assertEqual(run(os.path.join(folder, 'City - Springfield.sc4')), rows[1:])
        self.assertEqual([row['file'] for row in rows], ['City - Broken.sc4', 'City - Springfield.sc4'])
        self.assertTrue(rows[0]['status'].startswith('unreadable'))
        row = rows[1]
        self.assertEqual(list(row), sc4report.DEMAND_COLUMNS)
        expected = {'status': 'ok', 'demands': '3', 'R$': '2000.0', 'R$_cap': '0.5', 'Ih': '-50.0',
                    'Ih_cap': '0.25', 'R$$': '', 'Ir_cap': ''}
        for column, value in expected.items():
            self.assertEqual(row[column], value, column)

    def test_all(self):
        with tempfile.TemporaryDirectory() as folder:
            city(os.path.join(folder, 'City - Springfield.sc4'))
            rows = run(folder, '--all')
        self.assertEqual([row['id'] for row in rows], ['01010', '21010', '24400'])
        old, r, _ = rows
        self.assertEqual(list(r), sc4report.DEMAND_ALL_COLUMNS)
        self.assertEqual((r['supply'], r['demand'], r['min'], r['max'], r['active']),
                         ('1000.0', '3000.0', '-6000.0', '6000.0', '2000.0'))
        self.assertEqual((r['economy'], r['tax'], r['cap']), ('0.0625', '-0.0078', '0.5'))
        self.assertEqual(old['cap'], '0.4')


if __name__ == '__main__':
    unittest.main()

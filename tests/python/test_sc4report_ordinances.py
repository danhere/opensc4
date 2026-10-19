"""tools/sc4report.py ordinances: an ordinance simulator record (version 4)
and three ordinances built by hand (one enacted, one on but not available,
one available and off), each under its own class ID in a city file, and both
reports checked.

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
from test_sc4report_budget import city_file, gzstr, pack, with_header  # noqa: E402

GAMBLING, CURFEW, SMOKE = 0xA0D07129, 0xC2BF1DC5, 0x62BF1DAA


def simulator(refs):
    """Version 4, as Write (0x0080D780) gives it: expense, income, the
    ordinances, the pending income."""
    out = pack('H', 4) + pack('qq', 150, 100) + pack('I', len(refs))
    out += b''.join(pack('II', address, clsid) for address, clsid in refs) + pack('q', -20)
    return with_header(0x01000000, out)


def ordinance(address, ident, name, available, on, constant, adjusted):
    """Version 4, as Write (0x0080CB30) gives it, then a property holder
    record (kept as bytes)."""
    out = pack('H', 4) + pack('BI', 1, ident) + gzstr(name) + gzstr('about ' + name) + pack('If', 0, 100.0)
    out += pack('3q', 0, -50, constant) + pack('fI', -0.005, 2) + pack('BBB', available, on, 1)
    out += pack('qB', adjusted, ident == GAMBLING)
    return with_header(address, out + b'\x01\x00\x00\x00\x00\x00')


def city(path):
    entries = [(GAMBLING, ordinance(0x01000030, GAMBLING, 'Legalize Gambling', 1, 1, 100, 100)),
               (CURFEW, ordinance(0x01000020, CURFEW, 'Youth Curfew Act', 0, 1, -30, 0)),
               (SMOKE, ordinance(0x01000010, SMOKE, 'Smoke Detector Program', 1, 0, -40, 0))]
    refs = [(0x01000010, SMOKE), (0x01000030, GAMBLING), (0x01000020, CURFEW)]
    city_file(path, [(0xA990C08E, simulator(refs))] + entries)


def run(*argv):
    out = io.StringIO()
    with redirect_stdout(out):
        sc4report.main(['ordinances', *argv])
    return list(csv.DictReader(io.StringIO(out.getvalue())))


class OrdinanceReport(unittest.TestCase):
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
        self.assertEqual(list(row), sc4report.ORDINANCE_COLUMNS)
        expected = {'status': 'ok', 'ordinances': '3', 'available': '2', 'enacted': '1', 'expense': '150',
                    'income': '100', 'pending': '-20', 'enacted_names': 'Legalize Gambling'}
        for column, value in expected.items():
            self.assertEqual(row[column], value, column)

    def test_all(self):
        with tempfile.TemporaryDirectory() as folder:
            city(os.path.join(folder, 'City - Springfield.sc4'))
            rows = run(folder, '--all')
        # In ID order.
        self.assertEqual([row['id'] for row in rows], ['62BF1DAA', 'A0D07129', 'C2BF1DC5'])
        smoke, gambling, curfew = rows
        self.assertEqual(list(gambling), sc4report.ORDINANCE_ALL_COLUMNS)
        self.assertEqual((gambling['name'], gambling['enacted'], gambling['constant'], gambling['adjusted'],
                          gambling['income_ordinance']), ('Legalize Gambling', '1', '100', '100', '1'))
        self.assertEqual((curfew['available'], curfew['on'], curfew['enacted']), ('0', '1', '0'))
        self.assertEqual((smoke['available'], smoke['on'], smoke['enacted']), ('1', '0', '0'))
        self.assertEqual((smoke['repeal'], smoke['factor'], smoke['year']), ('-50', '-0.005', '0'))


if __name__ == '__main__':
    unittest.main()

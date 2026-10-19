"""tools/sc4report.py budget: a budget record (version 5), two department
budgets (version 10) and three line items (version 4) built by hand, put in a
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
import sc4report  # noqa: E402

FIRE, POLICE = 0x1000, 0x2000  # the departments' IDs
FIRE_AT, POLICE_AT = 0x01000010, 0x01000020  # their address-IDs
LINES_AT = (0x02000010, 0x02000020, 0x02000030)


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def gzstr(text):
    data = text.encode('latin-1')
    return pack('I', len(data)) + data


def with_header(address, body):
    """The save framework's header: the whole size, a CRC, the address-ID."""
    return pack('III', 12 + len(body), 0, address) + body


def budget():
    """Version 5, as Write (0x00690530) gives it."""
    out = pack('H', 5) + pack('4q', 123456, 123456, 50000, 2100)  # funds, copy, bonds owed, tax income
    out += pack('I', 2) + pack('III', FIRE, FIRE_AT, 0xE990BFFC) + pack('III', POLICE, POLICE_AT, 0xE990BFFC)
    for i in range(12):
        out += pack('ff', 9.0 + i, 1.0) + pack('12q', *[0] * 12) + pack('3q', 0, 1000 * (i + 1), 10 * i)
    out += pack('qq', 0, 0) + pack('24q', *[0] * 24) + pack('6q', *[0] * 6)
    out += pack('I', 2)
    for payment in (250, 125):
        out += pack('ii', 1, 2010) + pack('6q', 25000, 30000, 25000, 0, payment, 25000)
    out += pack('q', 0) + pack('4q', 0, 0, 0, 0)
    out += pack('I', 1) + pack('II', 7, 7) + gzstr('Safety') + pack('qq', 900, 0) + pack('III', 2, FIRE, POLICE)
    return with_header(0x01000000, out)


def department(address, ident, name, funding, lines):
    out = pack('H', 10) + pack('BI', 1, ident) + gzstr(name) + pack('I', 7)
    out += pack('ffq', funding, 1.2, 0) + pack('B', 0)
    out += pack('I', len(lines)) + b''.join(pack('II', line, 0xAA313C9F) for line in lines)
    out += pack('I', 1) + pack('II', 0x03000000, 0x12345678) + pack('I', 0x5555)
    return with_header(address, out + pack('II', 0xABC, 0xDEF))


def line_item(address, full, current, income, kind, owner):
    out = pack('H', 4) + gzstr('line') + pack('4q', 0, full, current, income)
    out += pack('IIBH', 1, 5, 0, kind) + pack('II', owner, 0xE990BFFC)
    return with_header(address, out + pack('II', 0, 0))


def city_file(path, records):
    """The smallest DBPF savediff.load reads: the header with the index's
    count at 0x24 and offset at 0x28, uncompressed entries."""
    header = bytearray(0x60)
    header[0:4] = b'DBPF'
    body, index = b'', b''
    for type_id, data in records:
        index += pack('5I', type_id, 0, 0, len(header) + len(body), len(data))
        body += data
    struct.pack_into('<II', header, 0x24, len(records), len(header) + len(body))
    with open(path, 'wb') as out:
        out.write(bytes(header) + body + index)


def city(path):
    city_file(path, [
        (0xE990BE01, budget()),
        (0xE990BFFC, department(POLICE_AT, POLICE, 'Police', 0.8, [LINES_AT[2]]) +
         department(FIRE_AT, FIRE, 'Fire', 1.0, LINES_AT[:2])),
        (0xAA313C9F, line_item(LINES_AT[0], 500, 500, 0, 0, FIRE_AT) + line_item(LINES_AT[1], 0, 0, 40, 1, FIRE_AT) +
         line_item(LINES_AT[2], 400, 320, 0, 0, POLICE_AT)),
    ])


def run(*argv):
    out = io.StringIO()
    with redirect_stdout(out):
        sc4report.main(['budget', *argv])
    return list(csv.DictReader(io.StringIO(out.getvalue())))


class BudgetReport(unittest.TestCase):
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
        expected = {
            'status': 'ok', 'funds': '123456', 'bonds': '2', 'bonds_owed': '50000', 'bond_payments': '375',
            'tax_income': '2100', 'department_expense': '820', 'department_income': '40',
            'tax_0': '9.0', 'tax_11': '20.0', 'assessed_0': '1000', 'assessed_11': '12000',
        }
        for column, value in expected.items():
            self.assertEqual(row[column], value, column)
        self.assertEqual(list(row), sc4report.BUDGET_COLUMNS)

    def test_departments(self):
        with tempfile.TemporaryDirectory() as folder:
            city(os.path.join(folder, 'City - Springfield.sc4'))
            rows = run(folder, '--departments')
        self.assertEqual([row['department'] for row in rows], ['Fire', 'Police'])
        fire, police = rows
        self.assertEqual(list(fire), sc4report.DEPARTMENT_COLUMNS)
        self.assertEqual((fire['group'], fire['id'], fire['name_instance']), ('Safety', '00001000', '00000ABC'))
        self.assertEqual((fire['full_expense'], fire['expense'], fire['income'], fire['lines']), ('500', '500', '40', '2'))
        self.assertEqual((police['funding'], police['full_expense'], police['expense']), ('0.8', '400', '320'))
        self.assertEqual(police['local_objects'], '1')


if __name__ == '__main__':
    unittest.main()

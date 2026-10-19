"""tools/sc4report.py all: a region folder with a city that has the regional
city, budget, power and water records, an unreadable city and a file that is
not a city; each CSV must equal its own report's output.

    python3 -m unittest discover -s tests/python
"""
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
import test_sc4report  # noqa: E402
import test_sc4report_budget  # noqa: E402
import test_sc4report_utilities  # noqa: E402


def city_file(path, records):
    """The smallest DBPF savediff.load reads, with each record's full key."""
    header = bytearray(0x60)
    header[0:4] = b'DBPF'
    body, index = b'', b''
    for (type_id, group, instance), data in records:
        index += struct.pack('<5I', type_id, group, instance, len(header) + len(body), len(data))
        body += data
    struct.pack_into('<II', header, 0x24, len(records), len(header) + len(body))
    with open(path, 'wb') as out:
        out.write(bytes(header) + body + index)


def report(*argv):
    out = io.StringIO()
    with redirect_stdout(out):
        sc4report.main(list(argv))
    return out.getvalue()


class AllReports(unittest.TestCase):
    def test_region(self):
        with tempfile.TemporaryDirectory() as root:
            region = os.path.join(root, 'Region')
            os.mkdir(region)
            city_file(os.path.join(region, 'City - Springfield.sc4'), [
                (sc4report.CITY_RECORD, test_sc4report.record()),
                ((0xE990BE01, 0, 0), test_sc4report_budget.budget()),
                ((0x0990C005, 0, 0), test_sc4report_utilities.power()),
                ((0x0990C075, 0, 0), test_sc4report_utilities.water()),
            ])
            with open(os.path.join(region, 'City - Broken.sc4'), 'wb') as broken:
                broken.write(b'DBPF')
            with open(os.path.join(region, 'Notes.sc4'), 'wb') as other:
                other.write(b'DBPF')
            out = os.path.join(root, 'out')
            self.assertEqual(report('all', region, '-o', out).split()[0], '2')
            self.assertEqual(sorted(os.listdir(out)),
                             ['budget.csv', 'demand.csv', 'ordinances.csv', 'region.csv', 'utilities.csv'])
            for name, command in (('region.csv', ['region', region]), ('budget.csv', ['budget']),
                                  ('demand.csv', ['demand']), ('ordinances.csv', ['ordinances']),
                                  ('utilities.csv', ['utilities'])):
                with open(os.path.join(out, name), encoding='utf-8') as written:
                    lines = written.read().splitlines()
                if command[0] == 'region':
                    expected = report(*command).splitlines()
                else:
                    # The single reports take every *.sc4; all takes only the cities.
                    expected = [line for line in report(*command, region).splitlines()
                                if not line.startswith('Notes.sc4')]
                self.assertEqual(lines, expected, name)
                self.assertEqual(len(lines), 3, name)


if __name__ == '__main__':
    unittest.main()

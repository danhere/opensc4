#!/usr/bin/env python3
"""Compare the daily simulation fingerprints of two autotest logs.

    fpdiff.py <a.log> <b.log>

The logs come from runs with OPENSC4_TEST_FINGERPRINT=1 (src/core/autotest.h).
Prints the first day on which any field differs, which fields, and how many
of the common days match."""
import re
import sys

LINE = re.compile(r'fp: day (\d+) (.*)$')


def parse(path):
    days = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        m = LINE.search(line)
        if m:
            days[int(m.group(1))] = m.group(2).strip()
    return days


def main():
    a, b = parse(sys.argv[1]), parse(sys.argv[2])
    common = sorted(set(a) & set(b))
    if not common:
        print('no common days')
        return 2
    same = 0
    first = None
    for day in common:
        if a[day] == b[day]:
            same += 1
        elif first is None:
            first = day
    print('%d of %d common days identical (days %d to %d)' % (same, len(common), common[0], common[-1]))
    if first is not None:
        print('first difference on day %d (%d days in):' % (first, first - common[0]))
        ta, tb = a[first].replace('|', ' ').split(), b[first].replace('|', ' ').split()
        for i, (x, y) in enumerate(zip(ta, tb)):
            if x != y:
                print('  token %d (after "%s"): %s vs %s' % (i, ta[i - 1] if i else '', x, y))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())

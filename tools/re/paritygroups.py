#!/usr/bin/env python3
"""Group the runs of one parity save by outcome.

    paritygroups.py <save results dir>

<save results dir> is one save's folder from scripts/run-parity.sh: a.log,
a.sc4, b.log, b.sc4, and the same in try1, try2... Two runs have the same
outcome when their daily fingerprints (tools/re/fpdiff.py) and every decoded
saved record (tools/re/sc4save.py, without CRCs and heap addresses) are equal.
Prints the groups, then a verdict: "same" when every B outcome is also an A
outcome (B did nothing the original does not also do), "different" when no B
outcome is an A outcome, "partly" otherwise. Exit 0 only for "same"."""
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fpdiff  # noqa: E402
import savediff  # noqa: E402
import sc4save  # noqa: E402


def outcome(log, save):
    records = {}
    for key, data in savediff.load(save).items():
        if key[0] in sc4save.SCHEMAS:
            fields, _ = sc4save.decode(data, sc4save.SCHEMAS[key[0]][1])
            fields.pop('crc', None), fields.pop('id', None)  # the regional city record has neither
            records['%08X/%08X/%08X' % key] = sc4save.comparable(fields)
    return json.dumps({'fp': fpdiff.parse(log), 'records': records}, sort_keys=True)


def main():
    folder = sys.argv[1]
    tries = [folder] + sorted(glob.glob(os.path.join(folder, 'try*')), key=lambda p: int(p.rsplit('try', 1)[1]))
    groups = {}  # outcome -> [run names]
    sides = {'a': set(), 'b': set()}
    for i, path in enumerate(tries):
        for side in 'ab':
            log, save = os.path.join(path, side + '.log'), os.path.join(path, side + '.sc4')
            if not (os.path.exists(log) and os.path.exists(save)):
                continue
            key = outcome(log, save)
            groups.setdefault(key, []).append('%s%d' % (side, i))
            sides[side].add(key)
    names = {key: chr(ord('A') + n) if n < 26 else str(n) for n, key in enumerate(groups)}
    for key, runs in groups.items():
        print('outcome %s: %s' % (names[key], ' '.join(runs)))
    a, b = sides['a'], sides['b']
    if not a or not b:
        print('verdict: incomplete')
        return 2
    if b <= a:
        verdict = 'same'
    elif not (b & a):
        verdict = 'different'
    else:
        verdict = 'partly'
    print('verdict: %s (A has %d outcome(s), B %d, shared %d)' % (verdict, len(a), len(b), len(a & b)))
    return 0 if verdict == 'same' else 1


if __name__ == '__main__':
    sys.exit(main())

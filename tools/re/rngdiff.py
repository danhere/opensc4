#!/usr/bin/env python3
"""Find where two runs' simulations part, from their random number draws.

    rngdiff.py <a.dump> <b.dump> <control.dump>...

The dumps come from runs with diagnostics.trace.rng, OPENSC4_RNG_DUMP_DAY=all
and OPENSC4_RNG_DUMP_FILE (src/core/rng_trace.cpp), under the determinism
replacements. Frame-level code (particles, automata, the interface) draws a
varying number of times even when two runs agree, so the control runs (runs
known to agree with a) define the noise: callers whose draw counts differ
between a and a control, and every generator object such a caller uses. What
is left are the simulation's own draws, which must match one for one; the
first mismatch is printed with its day and caller, plus context.

Dump lines: t<thread> <n> k<kind> <ret> <up1> <up2> <up3> <value> <object>;
kind 0 = cRZRandom::Advance, 1 = cRZRandom::NextFloat (value: the new state),
2 = cRZRandomSuper::Next. See docs/experiments/determinism.md, third round."""
import collections
import sys


def load(path):
    rows = []
    day = 'load'
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == 'day':
            day = p[1]
            continue
        if len(p) == 9 and p[0] == 't0':
            rows.append((day, p[2], p[3], p[4], p[7], p[8]))  # day, kind, ret, up1, value, object
    return rows


def key(row):
    return row[1], row[2], row[3]


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    a, b = load(sys.argv[1]), load(sys.argv[2])
    counts_a = collections.Counter(map(key, a))
    noise = set()
    for path in sys.argv[3:]:
        counts_c = collections.Counter(map(key, load(path)))
        noise |= {k for k in set(counts_a) | set(counts_c) if counts_a[k] != counts_c[k]}

    def clean(rows):
        noisy_objects = {r[5] for r in rows if key(r) in noise}
        return [r for r in rows if r[5] not in noisy_objects]

    fa, fb = clean(a), clean(b)
    print('noise callers: %d; simulation draws: %d vs %d' % (len(noise), len(fa), len(fb)))
    first = next((i for i in range(min(len(fa), len(fb))) if fa[i][1:5] != fb[i][1:5]), None)
    if first is None:
        print('identical over the common length')
        return 0
    print('first difference at simulation draw %d (day %s)' % (first, fa[first][0]))
    for i in range(max(0, first - 5), min(first + 8, len(fa), len(fb))):
        mark = '>>' if i >= first else '  '
        print('%s %6d A day %s %s %s<-%s %s' % (mark, i, *fa[i][:5]))
        print('%s %6s B day %s %s %s<-%s %s' % (mark, '', *fb[i][:5]))
    return 1


if __name__ == '__main__':
    sys.exit(main())

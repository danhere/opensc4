#!/usr/bin/env python3
"""Summarise a coverage run (diagnostics.coverage) against the function list.

Usage: coverage_report.py <functions.csv> <opensc4-coverage.csv> [top N classes]

functions.csv comes from tools/ghidra/ExportFunctions.java, the coverage file
from core.dll. Prints overall coverage, coverage of named vs. unnamed
functions, and per-class coverage for classes named in the Ghidra project
("cSC4Foo::Method" names), most-executed classes first.
"""

import collections
import csv
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25

    with open(sys.argv[1], newline="") as f:
        functions = {int(r["address"], 16): r for r in csv.DictReader(f)}
    with open(sys.argv[2], newline="") as f:
        hits = {int(r["address"], 16): int(r["first_hit_ms"]) for r in csv.DictReader(f)}

    def bucket(name):
        if name.startswith("FUN_"):
            return "unnamed"
        if "::" in name:
            return "class method"
        return "library or other named"

    total = collections.Counter()
    ran = collections.Counter()
    classes_total = collections.Counter()
    classes_ran = collections.Counter()
    for address, row in functions.items():
        kind = bucket(row["name"])
        total[kind] += 1
        ran[kind] += address in hits
        if kind == "class method":
            cls = row["name"].split("::")[0]
            classes_total[cls] += 1
            classes_ran[cls] += address in hits

    n_total, n_ran = sum(total.values()), sum(ran.values())
    print(f"Functions that ran: {n_ran} of {n_total} ({100 * n_ran / n_total:.1f}%)")
    if hits:
        print(f"Last first hit after {max(hits.values()) / 1000:.1f} s")
    print()
    print("| Functions | Ran | Total | Share |")
    print("|---|---|---|---|")
    for kind in ("class method", "library or other named", "unnamed"):
        if total[kind]:
            print(f"| {kind} | {ran[kind]} | {total[kind]} | {100 * ran[kind] / total[kind]:.1f}% |")

    print()
    print(f"Named classes with code that ran: {sum(1 for c in classes_total if classes_ran[c])} of {len(classes_total)}")
    print()
    print("| Class | Methods ran | Named methods |")
    print("|---|---|---|")
    ranked = sorted(classes_total, key=lambda c: (-classes_ran[c], c))
    for cls in ranked[:top]:
        print(f"| {cls} | {classes_ran[cls]} | {classes_total[cls]} |")
    idle = [c for c in classes_total if classes_ran[c] == 0]
    if idle:
        print()
        print(f"Named classes with no code run ({len(idle)}): " + ", ".join(sorted(idle)))


if __name__ == "__main__":
    main()

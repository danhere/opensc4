#!/usr/bin/env python3
"""Check the patch census's entry/mid placements against Ghidra's functions.

Usage: census_placement.py <docs/patch-census.md> <functions.csv> [coverage.csv]

Reads every table row in section 3 of the census (one subsection per DLL),
takes the addresses in its first column, and classifies each against the
function list from tools/ghidra/ExportFunctions.java:
  entry        the address is a function's first byte
  mid          inside a function (between its entry and the next function)
  no function  in .text but outside every function body (code Ghidra left
               out of functions); the preceding function is shown
  data         outside .text
Prints a Markdown report: totals per DLL, rows where the census said
"unknown" or "(inferred)" or disagrees, and the functions patched by more
than one DLL. With a coverage file, also marks functions that ran in that run.
"""

import bisect
import csv
import re
import sys

TEXT_START, TEXT_END = 0x00407000, 0x00A7FA2D  # .text of the 641 exe
ADDRESS = re.compile(r"0x[0-9A-Fa-f]{5,8}")


def load_functions(path):
    with open(path, newline="") as f:
        rows = sorted((int(r["address"], 16), int(r["size"]), r["name"]) for r in csv.DictReader(f))
    return rows, [r[0] for r in rows]


def classify(address, functions, starts):
    if not (TEXT_START <= address < TEXT_END):
        return "data", None
    i = bisect.bisect_right(starts, address) - 1
    if i < 0:
        return "no function", None
    entry, size, name = functions[i]
    if entry == address:
        return "entry", functions[i]
    # Inside the body (size = number of addresses in Ghidra's body; for the
    # rare non-contiguous bodies this undercounts, so a site past it is
    # reported as a gap rather than guessed).
    if address < entry + size:
        return "mid", functions[i]
    return "no function", functions[i]


def parse_census(path):
    """Yields (dll, first cell, census placement, purpose) for section 3 rows.

    Tables differ in their columns; the placement and purpose columns are
    found by header ("Entry/mid", "Purpose"). Tables without an Entry/mid
    column yield "not stated".
    """
    dll = None
    in_section = False
    placement_col = purpose_col = None
    for line in open(path, encoding="utf-8"):
        if line.startswith("## "):
            in_section = line.startswith("## 3.")
            continue
        if not in_section:
            continue
        if line.startswith("### "):
            dll = re.sub(r"^### 3\.\d+ ", "", line.strip())
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if line.startswith("| Address"):
            placement_col = next((i for i, c in enumerate(cells) if c.lower().startswith("entry/mid")), None)
            purpose_col = next((i for i, c in enumerate(cells) if c.lower().startswith("purpose")), None)
            continue
        if not line.startswith("| 0x") and not line.startswith("| (0x"):
            continue
        placement = cells[placement_col] if placement_col is not None and placement_col < len(cells) else "not stated"
        purpose = cells[purpose_col] if purpose_col is not None and purpose_col < len(cells) else ""
        yield dll, cells[0], placement, purpose


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    functions, starts = load_functions(sys.argv[2])
    ran = set()
    if len(sys.argv) > 3:
        with open(sys.argv[3], newline="") as f:
            ran = {int(r["address"], 16) for r in csv.DictReader(f)}

    per_dll = {}
    notes = []
    patched_functions = {}
    for dll, cell, census, purpose in parse_census(sys.argv[1]):
        not_installed = cell.startswith("(")
        for text in ADDRESS.findall(cell):
            address = int(text, 16)
            kind, function = classify(address, functions, starts)
            counts = per_dll.setdefault(dll, {"entry": 0, "mid": 0, "data": 0, "no function": 0})
            counts[kind] += 1
            name = function[2] if function else ""
            if function and kind != "no function" and not not_installed:
                patched_functions.setdefault(function[0], (name, set()))[1].add(dll.split(" ")[0])
            claimed = census.lower()
            claimed_kind = ("entry" if claimed.startswith("entry") else "mid" if claimed.startswith("mid")
                            else "data" if claimed.startswith("data") else "unknown")
            # Data sites the census did not place need no confirmation.
            if kind == "data" and claimed_kind == "unknown":
                continue
            if claimed_kind != kind or "inferred" in claimed or claimed_kind == "unknown":
                where = (f"{name} (entry 0x{function[0]:08X})" if kind != "no function" else
                         f"after {name} (entry 0x{function[0]:08X})") if function else "-"
                ran_note = ("yes" if function and function[0] in ran else "no") if ran and function else ""
                notes.append((dll, text, census, kind, where, ran_note, purpose))

    print("| DLL | Sites | Entry | Mid | Data | No function |")
    print("|---|---|---|---|---|---|")
    totals = {"entry": 0, "mid": 0, "data": 0, "no function": 0}
    for dll, c in per_dll.items():
        print(f"| {dll} | {sum(c.values())} | {c['entry']} | {c['mid']} | {c['data']} | {c['no function']} |")
        for k in totals:
            totals[k] += c[k]
    print(f"| **Total** | **{sum(totals.values())}** | **{totals['entry']}** | **{totals['mid']}** | "
          f"**{totals['data']}** | **{totals['no function']}** |")

    print()
    print("Rows the census marked unknown or inferred, or where Ghidra disagrees:")
    print()
    header = "| DLL | Address | Census | Ghidra | Function |" + (" Ran in test |" if ran else "")
    print(header)
    print("|" + "---|" * (header.count("|") - 1))
    for dll, address, census, kind, where, ran_note, purpose in notes:
        print(f"| {dll.split(' ')[0]} | {address} | {census} | {kind} | {where} |" + (f" {ran_note} |" if ran else ""))

    shared = {e: v for e, v in patched_functions.items() if len(v[1]) > 1}
    print()
    print(f"Functions patched by more than one DLL: {len(shared)}")
    if shared:
        print()
        print("| Function | Entry | DLLs |" + (" Ran in test |" if ran else ""))
        print("|---|---|---|" + ("---|" if ran else ""))
        for entry, (name, dlls) in sorted(shared.items()):
            print(f"| {name} | 0x{entry:08X} | {', '.join(sorted(dlls))} |" + (f" {'yes' if entry in ran else 'no'} |" if ran else ""))
    if ran:
        patched_and_ran = sum(1 for e in patched_functions if e in ran)
        print()
        print(f"Patched functions that ran in the coverage run: {patched_and_ran} of {len(patched_functions)}")


if __name__ == "__main__":
    main()

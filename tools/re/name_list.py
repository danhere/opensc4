#!/usr/bin/env python3
"""Merge OpenSC4's function names into one CSV for Ghidra.

    name_list.py <addresses-641.toml> <names-641.txt>  > names.csv

Output columns: address,name,note. Address database entries of kind
"function" come first; a names-641.txt line for the same address must agree
or the script fails, so the two files cannot drift apart. Ghidra cannot take
"::" in a plain label, so the Java side splits the name into namespace and
function name.
"""
import csv
import re
import sys
import tomllib


def main():
    with open(sys.argv[1], 'rb') as f:
        db = tomllib.load(f)
    names = {}
    for entry in db.get('entry', []):
        if entry.get('kind') != 'function':
            continue
        names[int(entry['address'])] = (entry['name'], 'address database: ' + entry.get('source', ''))
    for number, line in enumerate(open(sys.argv[2], encoding='utf-8'), 1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = re.match(r'(0x[0-9A-Fa-f]+)\s+(\S+)\s*(.*)$', line)
        if not m:
            sys.exit('%s:%d: expected "<address> <name> <note>"' % (sys.argv[2], number))
        address, name, note = int(m.group(1), 16), m.group(2), m.group(3)
        if address in names and names[address][0] != name:
            sys.exit('%s:%d: 0x%08X is %s in the address database' % (sys.argv[2], number, address, names[address][0]))
        names.setdefault(address, (name, note))
    out = csv.writer(sys.stdout, lineterminator='\n')
    out.writerow(['address', 'name', 'note'])
    for address in sorted(names):
        name, note = names[address]
        out.writerow(['0x%08X' % address, name, note])


if __name__ == '__main__':
    main()

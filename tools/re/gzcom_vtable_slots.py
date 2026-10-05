#!/usr/bin/env python3
"""List the vtable slots of every GZCOM interface declared in gzcom-dll.

Reads the interface headers (classes named cI*) and writes one CSV row per
slot: interface, slot index, method name. Slot numbers include inherited
methods (cIGZUnknown's QueryInterface, AddRef and Release are slots 0-2).

Only single inheritance is followed, which is what GZCOM interfaces use.
MSVC's ordering quirk for overloaded virtuals (it groups overloads) is not
modelled; the slot table is used together with a vtable-length check.

Usage: gzcom_vtable_slots.py <gzcom-dll include dir> > interface-slots.csv
"""

import pathlib
import re
import sys

# A class body ends at a "};" in column 0; nested enums and structs are indented.
CLASS = re.compile(r"\bclass\s+(cI\w+)\s*:\s*public\s+(\w+)\s*\{(.*?)\n\};", re.S)
VIRTUAL = re.compile(r"virtual\s+[^;{]*?\b(~?\w+)\s*\([^;{]*?\)\s*(?:const\s*)?=\s*0\s*;", re.S)
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def parse(include_dir: pathlib.Path):
    interfaces = {"cIGZUnknown": ("", ["QueryInterface", "AddRef", "Release"])}
    for header in sorted(include_dir.glob("*.h")):
        text = COMMENT.sub("", header.read_text(errors="ignore"))
        for name, base, body in CLASS.findall(text):
            if name == "cIGZUnknown":
                continue
            interfaces[name] = (base, VIRTUAL.findall(body))
    return interfaces


def slots(name, interfaces, seen=()):
    """Full slot list for `name`, or None if its base chain is unknown."""
    if name not in interfaces or name in seen:
        return None
    base, own = interfaces[name]
    if not base:
        return list(own)
    inherited = slots(base, interfaces, seen + (name,))
    return None if inherited is None else inherited + own


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    interfaces = parse(pathlib.Path(sys.argv[1]))
    print("interface,slot,method")
    skipped = 0
    for name in sorted(interfaces):
        table = slots(name, interfaces)
        if table is None:
            skipped += 1
            continue
        for index, method in enumerate(table):
            print(f"{name},{index},{method}")
    print(f"{len(interfaces) - skipped} interfaces, {skipped} skipped (unknown base)", file=sys.stderr)


if __name__ == "__main__":
    main()

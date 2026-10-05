#!/usr/bin/env python3
"""Generate addresses.h from the address database (data/addresses-*.toml).

Usage: gen_addresses.py <addresses.toml> <output header>

Each entry becomes a constant in namespace opensc4::addr (name with "::" and
other non-identifier characters replaced by "_"), plus one row in kEntries,
which core.dll walks at startup to check the expected bytes.

Can also check a database against an exe:  gen_addresses.py --check <toml> <exe>
"""

import re
import struct
import sys
import tomllib

KINDS = {"function", "vtable", "data"}
CONVENTIONS = {"cdecl", "stdcall", "thiscall", "fastcall", "custom"}


def load(path):
    with open(path, "rb") as f:
        entries = tomllib.load(f).get("entry", [])
    seen = set()
    for e in entries:
        where = f"{path}: entry {e.get('name', '?')}"
        for field in ("name", "address", "kind", "source"):
            if field not in e:
                sys.exit(f"{where}: missing {field}")
        if e["kind"] not in KINDS:
            sys.exit(f"{where}: kind must be one of {sorted(KINDS)}")
        if e["kind"] == "function" and e.get("convention") not in CONVENTIONS:
            sys.exit(f"{where}: functions need a convention in {sorted(CONVENTIONS)}")
        e["ident"] = re.sub(r"\W+", "_", e["name"].replace("::", "_")).strip("_")
        if e["ident"] in seen:
            sys.exit(f"{where}: duplicate identifier {e['ident']}")
        seen.add(e["ident"])
        e["bytes"] = bytes.fromhex(e.get("expect", ""))
    return sorted(entries, key=lambda e: e["address"])


def cpp_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def generate(entries, toml_name):
    out = [
        f"// Generated from {toml_name} by tools/gen_addresses.py. Do not edit.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace opensc4::addr {",
        "",
        "enum class Kind { Function, Vtable, Data };",
        "",
        "struct Entry {",
        "    const char* name;",
        "    std::uintptr_t address;",
        "    Kind kind;",
        "    const unsigned char* expect;",
        "    std::size_t expectSize;",
        "};",
        "",
    ]
    for e in entries:
        out.append(f"// {e['name']}" + (f": {e['convention']} {e['signature']}" if e["kind"] == "function" and "signature" in e else ""))
        out.append(f"inline constexpr std::uintptr_t {e['ident']} = 0x{e['address']:08X};")
    out.append("")
    for e in entries:
        if e["bytes"]:
            values = ", ".join(f"0x{b:02X}" for b in e["bytes"])
            out.append(f"inline constexpr unsigned char {e['ident']}_expect[] = {{{values}}};")
    out += ["", "inline constexpr Entry kEntries[] = {"]
    for e in entries:
        kind = {"function": "Function", "vtable": "Vtable", "data": "Data"}[e["kind"]]
        expect = f"{e['ident']}_expect, sizeof({e['ident']}_expect)" if e["bytes"] else "nullptr, 0"
        out.append(f"    {{{cpp_string(e['name'])}, {e['ident']}, Kind::{kind}, {expect}}},")
    out += ["};", "", "} // namespace opensc4::addr", ""]
    return "\n".join(out)


def check(entries, exe_path):
    data = open(exe_path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    sections = []
    for i in range(count):
        o = pe + 24 + optional + 40 * i
        _vsize, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, o + 8)
        sections.append((base + rva, raw_offset, raw_size))
    bad = 0
    for e in entries:
        if not e["bytes"]:
            continue
        offset = next((ro + e["address"] - va for va, ro, rs in sections if va <= e["address"] < va + rs), None)
        actual = data[offset:offset + len(e["bytes"])] if offset is not None else b""
        if actual != e["bytes"]:
            bad += 1
            print(f"MISMATCH {e['name']} 0x{e['address']:08X}: expected {e['bytes'].hex(' ')}, found {actual.hex(' ')}")
    print(f"{sum(1 for e in entries if e['bytes']) - bad} of {sum(1 for e in entries if e['bytes'])} expected byte sequences match")
    return bad == 0


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--check":
        sys.exit(0 if check(load(sys.argv[2]), sys.argv[3]) else 1)
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    entries = load(sys.argv[1])
    header = generate(entries, sys.argv[1].replace("\\", "/").split("/")[-1])
    with open(sys.argv[2], "w", newline="\n") as f:
        f.write(header)


if __name__ == "__main__":
    main()

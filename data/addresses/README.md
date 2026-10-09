# Address database

SimCity 4 Deluxe 1.1.641 for Windows (Steam, GOG and EA share this build). Virtual addresses, image base 0x400000.

One entry per address that OpenSC4 code or tooling relies on. src/core gets these as constants from the generated `addresses.h` (`tools/gen_addresses.py`, which reads every `*.toml` here); core.dll checks each `expect` against memory at startup and logs mismatches, which reveal a different build or another DLL patching that address.

## One file per simulator

Each simulator keeps its addresses in its own file (`power.toml`, `traffic.toml`, ...), so work on two simulators never edits the same file. `core.toml` holds game code that several simulators or the harness use: allocators, containers, streams, random numbers, services, the app and the simulator.

- A new simulator adds `<sim>.toml`; the build picks it up (CMake re-globs this folder at build time).
- An address only your simulator uses goes in your file, even if its class is generic. Move it to `core.toml` once a second simulator needs it.
- Names must be unique across all files: the generator fails on a name (or identifier) that two files both define, and says which files. Check `core.toml` before adding a generic helper; it may already be there under another name.
- Entries are kept in any order; the generated header sorts them by address.

## Fields

| Field | Meaning |
|---|---|
| `name` | C++-style name; becomes `opensc4::addr::<name with :: -> _>` |
| `address` | virtual address |
| `kind` | `"function"` (hookable at its entry), `"vtable"` or `"data"` |
| `convention` | `cdecl` \| `stdcall` \| `thiscall` \| `fastcall` \| `custom` (functions only) |
| `signature` | C++ prototype, `this` first for thiscall |
| `expect` | first bytes at the address, hex, to verify the build |
| `source` | how the address was identified; link the evidence |

Never record mid-function addresses as `"function"`: hooks go at entries only ([docs/architecture.md](../../docs/architecture.md), "Hooking layer").

`python3 tools/gen_addresses.py --check data/addresses "<SimCity 4.exe>"` checks every entry's bytes against an exe.

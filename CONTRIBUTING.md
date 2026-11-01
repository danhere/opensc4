# Contributing to OpenSC4

Thanks for helping. OpenSC4 runs inside a commercial game that EA still sells, so a few rules keep the project safe to publish. Please read them before opening a pull request.

## Getting set up

Follow "Getting started on Windows" in the [README](README.md); Linux, macOS and Ghidra setup are in [docs/dev-environment.md](docs/dev-environment.md). `core.dll` only runs inside the game on Windows, so check a change there before you open a pull request. There are no automatic CI builds yet.

## Reporting bugs

Open an issue with what you did, what happened, and your game version (OpenSC4 needs SimCity 4 Deluxe 1.1.641). Attach `opensc4.log` from your `Plugins` folder and, after a crash, the `opensc4-crash-*.dmp` next to it or the game's own report in `Documents\SimCity 4\Exception Reports`. Turn off other plugin DLLs first if you can, and say which ones you use.

## What never goes in the repository

- **Game files.** No executables, DLLs, DBPF files, textures, models, sounds, music, saves, region folders or screenshots of other people's work. OpenSC4 always needs the player's own installed copy.
- **Machine-made copies of the game's code.** No decompiler output (Ghidra, IDA or similar) pasted in, and no code generated from the game's machine code, instruction by instruction. Every machine-made translation was removed on 2026-10-22 ([docs/renderer.md](docs/renderer.md)); keep it that way.
- **Dumps of the game's data.** Tools may read the game at run time or on your machine, but their output (name tables, extracted records, decompiled listings) stays on your machine. `tools/re/extract_name_table.py` says so at its top; new tools should too.
- **Secrets and personal details.** No tokens, keys, passwords, or paths and machine names you'd rather not publish.

## How replacement code is written

- Read the disassembly or decompiler output to **understand** a function, then write it in your own words: our names, our structure, our comments. Addresses, offsets, sizes and class or message IDs are facts and are fine to use.
- Each replacement sits behind a switch in `opensc4.ini`, off by default until it has been checked against the original ([docs/parity.md](docs/parity.md)).
- Put every game address in `data/addresses/` (see its [README](data/addresses/README.md)), never as a bare number in code. That includes data: floats, tables and globals are `kind = "data"` entries used as `addr::<name>`, not `constexpr` copies in a header. Search the toml files for the address first (`grep -rn 0x00A8FE78 data/addresses`): common constants such as `SC4::FloatMinusOne` are often already there.
- Reuse the shared helpers before writing one: `Global<T>`, `At<T>` and `Virtual<T>` (`src/core/game_access.h`; `Global` already returns a reference). Don't copy a file-local helper from another part; when a second file needs it, move it to a shared header.
- Call the game's own helpers where the original calls them (`RZFile`, `RZDirectory`, `cRZIniFile`, its allocators), with the same arguments in the same order, not CRT, Win32 or STL substitutes. That includes the game's own C runtime: where the original calls its `sprintf`, `strtoul` and the like, ours calls the same function through `addr::` (several are already in the database, e.g. `SC4::sprintf`, `SC4::strtoul`), not `<cstdio>`/`<cstdlib>`. Replacing one needs a stated reason in the part's doc section: why the game's can't be used, not just what ours does.
- Calls between replaced functions go through their `addr::` entries, never as direct C++ calls, so any mix of groups works and a parity difference can be bisected by group. Keep the ours and game's lists in the file header current.
- A part's doc section follows [docs/html.md](docs/html.md), "Ours: the cache's entries (h7a)": a groups table with a link to the source, how it works, the kept quirks, "Still the game's" (what in the subsystem stays the game's, the functions ours calls included) and its test. Its mutation check and its rows in the results table go in the doc's "Testing" section (rows, not a prose line).

## Code from other projects

- Third-party libraries go in `third_party/` as submodules, used as published under their own licenses ([third_party/README.md](third_party/README.md)).
- Code adapted from another mod must have a license compatible with LGPL-2.1-or-later (MIT, BSD, LGPL). Keep the original copyright and license notice at the top of the file, as `src/core/game_growify.cpp` does, and credit the author in the README's table.
- An idea taken from another mod without its code gets a credit in a comment and in the docs.

## AI assistance

Allowed, and disclosed: say so in every commit an AI wrote (a `Co-Authored-By` line naming the tool that actually wrote it, such as OpenCode, not the reviewer, is enough) and in the pull request. The rules above apply the same way to code an AI wrote. Never push AI-assisted changes to another project's repository uninvited ([docs/decisions.md](docs/decisions.md#ai-use-and-community-etiquette)).

## Pull requests

- One piece of work per pull request, with what it replaces and how it was checked. A fix found along the way that isn't part of the piece (a broken test, a harness bug) gets its own pull request, so it can merge without waiting for the piece.
- Open the pull request as a draft until the part is complete (the list below); say in the body what is still to do. Use GitHub's draft state, not just the word in the body.
- Build and run the tests (`cmake --build --preset debug`, `ctest --preset debug`); a change that touches game code also needs its parity run in the game.
- A replacement part is complete only when its doc section records, from the pull request's own head:
  - a mutation check;
  - every replaced function reached ("all N reached");
  - the test run with the part's groups off against all of the subsystem's groups on;
  - a named 7-day parity run, `<part>-r1` ([docs/parity.md](docs/parity.md)).

  An automata part also records an independent instruction-level audit of its functions against the disassembly, as [docs/automata.md](docs/automata.md) does.
- Build, test and parity results come from the pull request's own head commit, stated as "build of <sha> on origin/main <sha>", and must not include another unmerged branch. Counts such as the `gen_addresses.py --check` total must match that head.
- Commit with an email you're happy to publish, such as your GitHub `noreply` address.

## License

By contributing you agree that your work is licensed under the [GNU Lesser General Public License v2.1 or later](LICENSE), like the rest of OpenSC4.

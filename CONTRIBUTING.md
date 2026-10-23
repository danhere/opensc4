# Contributing to OpenSC4

Thanks for helping. OpenSC4 runs inside a commercial game that EA still sells, so a few rules keep the project safe to publish. Please read them before opening a pull request.

## What never goes in the repository

- **Game files.** No executables, DLLs, DBPF files, textures, models, sounds, music, saves, region folders or screenshots of other people's work. OpenSC4 always needs the player's own installed copy.
- **Machine-made copies of the game's code.** No decompiler output (Ghidra, IDA or similar) pasted in, and no code generated from the game's machine code, instruction by instruction. Every machine-made translation was removed on 2026-10-22 ([docs/renderer.md](docs/renderer.md)); keep it that way.
- **Dumps of the game's data.** Tools may read the game at run time or on your machine, but their output (name tables, extracted records, decompiled listings) stays on your machine. `tools/re/extract_name_table.py` says so at its top; new tools should too.
- **Secrets and personal details.** No tokens, keys, passwords, or paths and machine names you'd rather not publish.

## How replacement code is written

- Read the disassembly or decompiler output to **understand** a function, then write it in your own words: our names, our structure, our comments. Addresses, offsets, sizes and class or message IDs are facts and are fine to use.
- Each replacement sits behind a switch in `opensc4.ini`, off by default until it has been checked against the original ([docs/parity.md](docs/parity.md)).
- Put every game address in `data/addresses/` (see its [README](data/addresses/README.md)), never as a bare number in code.

## Code from other projects

- Third-party libraries go in `third_party/` as submodules, used as published under their own licenses ([third_party/README.md](third_party/README.md)).
- Code adapted from another mod must have a license compatible with LGPL-2.1-or-later (MIT, BSD, LGPL). Keep the original copyright and license notice at the top of the file, as `src/core/game_growify.cpp` does, and credit the author in the README's table.
- An idea taken from another mod without its code gets a credit in a comment and in the docs.

## AI assistance

Allowed, and disclosed: say so in the commit or pull request (a `Co-Authored-By` line is enough). The rules above apply the same way to code an AI wrote. Never push AI-assisted changes to another project's repository uninvited ([docs/decisions.md](docs/decisions.md#ai-use-and-community-etiquette)).

## Pull requests

- One piece of work per pull request, with what it replaces and how it was checked.
- Build and run the tests (`cmake --build --preset debug`, `ctest --preset debug`); a change that touches game code also needs its parity run in the game.
- Commit with an email you're happy to publish, such as your GitHub `noreply` address.

## License

By contributing you agree that your work is licensed under the [GNU Lesser General Public License v2.1 or later](LICENSE), like the rest of OpenSC4.

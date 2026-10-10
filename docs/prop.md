# The prop developer (cSC4PropDeveloper)

Places and removes the props of the city that come and go: the optional props of lots (cars, people, clutter, by the wealth around them), the props that depend on a lot's condition, the timed ones (day and night, hours, dates), and the signs and labels. Requesters (the wealth requester, a small class of its own, and two others, one of them the pollution simulator) say how many optional props a block of the city should have. Reimplemented like the other developers ([tract.md](tract.md), [ports.md](ports.md)): two writers, the argument count of every virtual call checked against the callee's own code, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/prop.toml).

## The class

Class ID 0x89C48F47. Object size 0xA70 (a Mersenne Twister of 0x9C8 bytes at +0x38 takes most of it). Constructor 0x006F3410, factory 0x006F3600. Code 0x006F0D70 to 0x006F5570. The city makes one (its slot 41).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB29F8 | the primary | 12: QueryInterface, AddRef, Release, Init 0x006F1F30, Shutdown 0x006F2C60, AddPropRequester, RemovePropRequester, UpdateAllOptionalProps 0x006F4DB0, ShowSignsAndLabels, ToggleSignsAndLabels, AreSignsAndLabelsVisible, the deleting destructor |
| +0x4 | 0x00AB29E8 | `cIGZMessageTarget2` | DoMessage 0x006F4E90 (16 message types) |
| +0x8 | 0x00AB29D0 | `cIGZSerializable` | Write 0x006F1930, Read 0x006F3620 |

cSC4WealthRequester (class ID 0x89C49986, 12 bytes, factory 0x006F1500, vtables 0x00AB29B0 and 0x00AB2998) lives in the same code.

Fields (src/core/prop_sim.h has the list with its evidence): +0x14 the state of the optional props' walk, +0x18 its cell iterator; +0x38 the generator; +0xA04 the requesters; +0xA10 to +0xA1C services; +0xA20 the timed props, +0xA2C to +0xA3C the day and night stepping; +0xA48 a hash table of key to list of props; +0xA5C, +0xA60, +0xA64 lists of props by kind; +0xA68 signs visible, +0xA6C the signs.

## What happens

- **Optional props**, one 8 by 8 cell block per simulator message (0x66956814, 0x6695681A): every requester gives a weight for the block; the slots for optional props come from the lots' configurations (or the bare cell's); props whose requester no longer wants them go; the target is the mean lot value times the slots, shared by weight; a requester over its target loses a prop picked at random, one under it gets free slots drawn at random. The draws come from the developer's generator.
- **A new prop** (a message) is sorted into the lists by its exemplar's properties; a removed one is taken out.
- **Timed props** are switched in steps that follow frames (message 0x86AD10EE), not game days.
- The save record is version 5 (Read takes 4 and 5): the walk's state and iterator, the requesters, three service references, the stepping, the timed props, the table and the lists. `tools/re/save_schemas/prop.py` decodes it in every corpus save; the wealth requester's record is its header only.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.prop.sim` | Init, Shutdown, the requester and signs slots | prop_object.cpp |
| `simulators.prop.save` | Write | prop_object.cpp |
| `simulators.prop.handlers` | what the messages call: the timed props' steps, the signs, a prop placed (0x006F1620), a lot's optional and condition props, the props of a rectangle, a prop removed (nine detours) | prop_object.cpp |
| `simulators.prop.requester` | the wealth requester's slots and factory | prop_object.cpp |
| `simulators.prop.containers` | five container functions of the class | prop_object.cpp |
| `simulators.prop.object` | our own object: a detour on the constructor, the destructor, identity, copies of the three vtables | prop_object.cpp |
| `simulators.prop.props` | the block update of the optional props (0x006F4080) and UpdateAllOptionalProps | prop_props.cpp |
| `simulators.prop.props_messages` | DoMessage, a prop added, the walk's step | prop_props.cpp |
| `simulators.prop.props_load` | Read | prop_props.cpp |

Still the game's: container, list and hash helpers other classes call too, the random generators' own code, the cell iterator. The constructor seeds the generator from the time of day; ours keeps the game's calls under the fixed seed's site (0x006F34B6), so both sides are seeded alike. Copied on purpose from the game and commented in the code: placing a prop with neither a lot nor a network lot at the cell goes on with its argument as the prop; a null entry at the end of one list walk is freed and the walk goes on from it; the division by the weights' sum is not guarded.

## Test cities

The fingerprint's `prop` part: the flags, the walk's state and cell, the counts of requesters and timed props, the night flag, the table (count and an order-free sum), the lists' lengths, the signs, and a checksum of the wealth requester's static generator. The developer's own generator is left out by default: it differs between two runs of the original from the first day; `OPENSC4_TEST_PROP=2` adds it with the rest of the frame-driven state. `OPENSC4_TEST_PROP=1` adds queries no unattended run makes (the signs flag, the requesters' types, the wealth requester's answers for an empty list of candidates).

## Parity

All runs on 2026-10-09 and 10 with every `simulators.prop.*` group on and `OPENSC4_TEST_PROP=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for a week, first try (`pr-all7`) | 2 of 12, the original parting from itself on nearly every save: the fingerprint then had the developer's own generator in it, which differs between two runs of the original from the first day. Taken out of the default fingerprint |
| 12 saves for a week (`pr-all7b`) | 11 of 12; Making Money d1095 partly: one B run in the traffic record's other state (the two states the original also reaches on such saves; [ports.md](ports.md)), its three retries same |
| `saves-plopped`, 30 days (`pr-pl30b`) | 3 of 3, all on the first pair |
| `saves-ports`, 30 days (`pr-ports30`) | 3 of 3, all on the first pair |

Before the first run all 266 virtual calls in the two files had their argument counts checked against the callees' own code (no mismatch). Reached: Init, Read, Write, Shutdown, the messages, the optional props' walk and block update with props placed, props added, the timed props, the signs settled, the wealth requester. Not seen in these runs: the props of a changed rectangle, a lot's condition props, a prop removed, a network occupant removed, the signs toggled, UpdateAllOptionalProps (nothing in the game calls it).

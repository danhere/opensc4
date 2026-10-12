# The flammability simulator (cSC4FlammabilitySimulator)

Done after fire and police ([fire-police.md](fire-police.md)) at Dan's request, the same way: function by function under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout (data/addresses/flammability.toml).

## The class

Class ID 0xE990C045, 0xB0 bytes, factory 0x006AEAD0, constructor 0x006AE6D0, code 0x006AE380 to 0x006AFBD0 (the function at 0x006AFBD0 belongs to the flora class). Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB128C | `cISC4FlammabilitySimulator` | QueryInterface 0x006AE390 (1 and 0x48F41617 here, 0x452294AA at +4, 0xE4FDA3D4 at +8), AddRef (shared), Release 0x006AE4A0, Init 0x006AF540, Shutdown 0x006AED40, a shared stub, IsCellFlammable, GetFlammabilityHotspot, GetAverageFlammability (4 bytes, too short to hook: our object's vtable has ours), the deleting destructor |
| 0x00AB127C | `cIGZMessageTarget2` (+4) | DoMessage 0x006AFA60 |
| 0x00AB1264 | `cIGZSerializable` (+8) | Write 0x006AE620, Read 0x006AE830 (version 2), GetGZCLSID 0x006AE380 |

Fields: +0xC initialised, +0x10 references, +0x18 random numbers (seed 0), +0x30 the walk (a tract iterator, 0x30 bytes), +0x34 the city, +0x38/+0x3C the city's size in cells, +0x40 the message server, +0x44/+0x48/+0x4C/+0x50/+0x54 services (0x00B43DAC, the water simulator's 0x00B43D94, 0x00B43D90, the network service, the lot manager), +0x58 the flammability grid and +0x5C the occupants' raw values (uint8 sim grids, block size +0x8C), +0x60 the average, +0x64 the multiplier, +0x68/+0x6C the walk's sum and count, +0x70 the month's hot cells (12-byte records), +0x7C clusters (list of x, z, count), then the tuning.

## What it does

- A cell's raw value (0x006AEE90) is the highest flammability (property 0x29244DB5, x 1.1 for a lot of kind 4, 0x006AE4D0) of the buildings in its block, else of its lots, else of the others (flora), found through the network service with a type filter. Buildings added raise the cells under them; removed or changed ones recompute them (0x006AF380, messages 0x99EF1142/43/45, 0xE99B3D4D).
- The multiplier (0x006AE410): a value from the 0x00B43D90 service (0x28F42AA0), x 1.2 in season 2 (0x00B43DAC slot 14); refreshed by messages 0x6716A4E5 and 0xC2C09554.
- The walk (0x006AF8D0, each step message 0x66956814): +0x94 cells a step, the flammability grid = raw x the multiplier (x 1.1 more where there is no water), at most 255; cells over 196 are noted as hot; developed cells are summed. At the walk's end (0x006AF810): the average, message 0x08F415FE, figure 0x2A11CB48, hot cells merged into clusters within 8 cells (0x006AE940; clusters under 16 cells dropped), then the month's fire (0x006AF150): a draw of 0-100 under +0xA4 (0.01) sets a random lot's building alight, else the clusters in a shuffled order, the first whose curve value (+0xA8 at its size) beats the draw; the fire goes to city slot 72's object 0x621CDA33.
- Tuning (0x006AEB50, exemplar 0xA8F41EE3).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.flammability.sim` | every function above but the 4-byte getter (19 functions, flammability_sim.cpp) |
| `simulators.flammability.object` | our own object under the class ID: factory, constructor, destructor, identity, the average getter, our copies of the vtables |

Bisect inside the group with `OPENSC4_FLAMMABILITY_SKIP` (function addresses in hex). The fingerprint has a `flam` part (both grids, average, hotspot, multiplier, the walk's sums, hot cells, clusters). `OPENSC4_TEST_FLAMMABILITY=<day>` (autotest) makes every cell hot, keeps clusters of one cell, sets the lot fire chance to 100 and walks the whole city each step, so the walk's end, clusters and fires happen in short runs. `OPENSC4_TEST_FLAM_DUMP=<n>` logs every cell's raw value, grid value and water answer at the n-th fingerprint, to find where two runs differ (it found the walk's start depending on the sound code's `rand()` calls, [experiments/determinism.md](experiments/determinism.md)).

## Parity

With both groups on: 12 of 12 saves and 3 of 3 for a week, 2 of 2 for 90 days on Making Money, and 2 of 2 for 40 days with `OPENSC4_TEST_FLAMMABILITY` (over 300 clusters, a hotspot and a fire chance of 100 at every walk's end); the fingerprint's `flam` part is identical in every pair.

Found by parity on the way: in the 0xE99B3D4D handler the building's slot 3 takes no argument; the 0 pushed before it is UpdateOccupant's second argument (passing it to slot 3 left the stack 4 bytes off and crashed a few days in).

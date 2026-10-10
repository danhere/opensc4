# The airport and seaport developers (cSC4PortDeveloper, cSC4AirportDeveloper, cSC4SeaportDeveloper)

The two port developers keep the city's airports and seaports: each port's lot, type and growth stage, its efficiency from funding and usage, its monthly cost, and its growth to the next stage (a bigger lot built over the old one). Both derive from cSC4PortDeveloper, which holds nearly all of that; the airport adds the usage from traffic, the seaport the map of which water an edge of the city reaches, its capacity and the terrain under a new port. Reimplemented like the growth and tract developers ([growth.md](growth.md), [tract.md](tract.md)): one writer per class, an audit against the game's code, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/airport.toml, seaport.toml).

## The classes

| Class | ID | Size | Code | Constructor, factory | Vtables |
| --- | --- | --- | --- | --- | --- |
| cSC4PortDeveloper (base, at +0x4 of a subclass object) | 0x0A2C4EEE | 0x60 | 0x006E8F10 to 0x006EABA6 | 0x006E9C60 | its own (0x00AB2790, 0x00AB2780, 0x00AB2768) only while it is constructed and destroyed |
| cSC4AirportDeveloper | 0xAA2049A1 | 0x64 | 0x0068AFD0 to 0x0068B95B | 0x0068B4C0, 0x0068B520 | 0x00AB0714 (15 slots), +0x4 the port part 0x00AB06C8 (19), +0x8 the message target 0x00AB06B4, +0xC the serializable 0x00AB069C |
| cSC4SeaportDeveloper | 0xEA230A28 | 0x7C | 0x00700E80 to 0x00702023 | 0x007018F0, 0x007019F0 | 0x00AB2E10 (17 slots), +0x4 0x00AB2DC0 (19), +0x8 0x00AB2DAC, +0xC 0x00AB2D94 |

The city's slot 77 gives the airport developer, slot 76 the seaport developer. Seven functions are one piece of code for both classes (the linker folded them; they sit in the seaport's range): the average efficiency, the usage values, the trips, the maintenance cost, the maintenance update, Read and a jump to the base's DeleteThis.

A port is a 0x28-byte object (stage, type, cell, efficiency, lot, usage); a port type (0x30 bytes) comes from an exemplar listed in the developer's type list (the airport's is 0xCA270E9D) and has stages (0x20 bytes: the lot configuration, the monthly cost, the capacity, the cost of growing to it). Field lists with their evidence are in src/core/airport_sim.h and the header of seaport_sim.cpp.

## What happens

- A port building appearing (message 0x99EF1142, an occupant of the developer's occupant group) on a lot whose configuration is the first stage of a type makes a port; its lot going (0xE8BDA2E7) removes it.
- Each simulator update the base works out every port's efficiency (the funding ratio of the department's line item, and usage against capacity), tells the demand, posts advice, and grows the ports that are at their limit: the lot is replaced by one of the next stage's configuration on the same ground (the lot manager's slot 23, then the lot developer).
- The airport counts the trips to each port from the traffic simulator's trip tables; the seaport gives each port's capacity to the freight service and, when a port is placed, sets the terrain under it from a table by the lot's orientation. It flood-fills the sea access map from the four edges of the city.
- The save record (airport and seaport alike): a version (2), the base's (1), the type list's ID or a class constant, the lot group, then per port its stage, type index and cell. `tools/re/save_schemas/airport.py` and `seaport.py` decode them.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.airport.port` | the base: Init, Shutdown, the messages (ports added and removed, the update, growth), Write, Read and the queries, as detours (the subclasses call them directly) | airport_sim.cpp |
| `simulators.airport.portobject` | the base's constructor, destructor, QueryInterface, Release, DeleteThis | airport_sim.cpp |
| `simulators.airport.sim` | the airport's slots in its four vtables (30) | airport_object.cpp |
| `simulators.airport.object` | our own airport developer: a detour on the constructor, identity, copies of the four vtables | airport_object.cpp |
| `simulators.seaport.sim`, `.save` | the seaport's slots (23), Write and Read | seaport_sim.cpp |
| `simulators.seaport.object` | our own seaport developer | seaport_sim.cpp |

Still the game's: the base's own vtable entries nobody can reach, the flood fill (0x005E3AC0, generic), and shared container, string and property helpers. Copied on purpose from the game: GetLeastEfficientPort (slot 7) never returns with two or more ports (its loop compares the first two without advancing); stack slots the game leaves unset before out-parameter calls are zero in ours.

## Test cities

No test city had a port, so `OPENSC4_TEST_PLOP_PORTS=<n>` (src/core/ports_autotest.cpp) builds one of each on the n-th day of a run, with the game's own calls for a port's growth: the first stage's configuration of the developer's first type, at the first place the lot manager accepts (its slot 20 answering 8; the seaport next to water an edge of the map reaches), a lot from the lot manager's slot 23, built by the lot developer. The original game's code then makes the ports from the building's message. The corpus `%LOCALAPPDATA%\opensc4\parity\saves-ports` was made this way with the original code only: Big City (from `saves-plopped/p0000`) two days after the ports were built (`q0000`) and 90 days on (`q0090`), and Rush Hour (from d0365, `q0000`).

Two more switches drive what a month of play does not: `OPENSC4_TEST_GROW_PORTS=<n>` gives every airport its capacity as usage and sends both developers the growth message; `OPENSC4_TEST_REMOVE_PORTS=<n>` sends each developer the lot message for its first port's lot, as when the lot is taken down (the port leaves the demand and the list, its ground is dezoned; the lot itself stays).

The fingerprint parts `airport` and `seaport` print the ports and their state (the seaport's also the sea access map); `OPENSC4_TEST_AIRPORT=1` and `OPENSC4_TEST_SEAPORT=1` add the queries no city run makes.

## Parity

All runs on 2026-10-09 with every airport, seaport and landfill group on and the three query switches, two games at a time, the machine busy with other threads' games (the original against itself parted on several saves that evening). Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for a week (`pt-all7`) | 10 of 12. Big City d0000: fingerprints identical, the traffic record in one of two states; the original against itself splits the same way there (`pt-b-ctl`). Making Money d0090: one B run off in the flammability part, its three retries same |
| `saves-ports`, 30 days, first try (`pt-ports30`) | 0 of 3: with a port in the city our code stopped the game without a report. A single run with traces found it in the seaport's trip count: the game pushes a 3 before calling the lot's slot 3 (its property holder, which takes no argument), and the 3 is the following call's second argument (the same at 0x00701344 in SetCapacity). The writer and the audit had both read it as slot 3's argument. Fixed |
| `saves-ports`, 30 days (`pt-ports30b`) | 3 of 3, all on the first pair; the airport and seaport records with their ports identical |
| 12 saves for a week with the ports built on day 2 (`pt-plop7b`) | 9 of 12; in the other three one B run parted (Big City d0000 in the traffic record as above; Rush Hour d0000 in the flammability part, d0365 in the trips). Reruns (`pt-plop-re-*`): the two Rush Hour saves same on the first pair, Big City d0000 again in the original's own two states |
| `saves-ports` for a week with growth asked on day 2 and the ports removed on day 4 (`pt-rm7`) | 3 of 3, all on the first pair |
| 12 saves for 35 days (`pt-all35`) | 8 of 12 same, 3 partly on saves where the original reached 3 to 4 outcomes in 4 runs that evening, and one B run on Rush Hour d1095 hit the rare game crash at 0x0096DA20 |
| `saves-plopped`, 35 days (`pt-pl35`) | 3 of 3 |

After the fix, every virtual call in these files (288) had its argument count checked against the callee's own code: the receiver's class vtable, found through its QueryInterface, and the slot's RET size. No other mismatch. The scripts are kept in `%LOCALAPPDATA%\opensc4\analysis\va-tools`.

Reached with ports in the city: ports made from a new building (both developers), the update with efficiency and maintenance, the usage and trips, the capacity, the terrain under a new seaport, saving and loading ports, the growth test, removing a port. Not reached: a port actually growing to its next stage (the growth test says no for the ports the switch builds: their lot's slot 33 answers 2), so the lot replacement and the growth bookkeeping have only been read, not run; the seaport's notices to neighbouring occupants.

# The neighbors simulator (cSC4NeighborsSimulator)

Reimplemented after flora ([flora.md](flora.md)) the same way: function by function on the original objects under the parity harness ([parity.md](parity.md)), then our own objects under the class IDs. Build 641 addresses throughout. The simulator keeps a city's connections to its neighbors (roads, rails, pipes, power lines and so on at the city's edge) and its neighbor deals (power, water and garbage bought or sold), and shares the deals with the region. It owns two more classes, `cSC4NeighborConnection` and `cSC4NeighborDeal`.

## The class

Class ID 0xC990C0A4. Object size 0x40. Factory 0x006D38D0 (`SC4::operator_new(0x40)`, constructor 0x006D0930). Code from 0x006CDEF0 to about 0x006D5040, with the two other classes. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB1EE0 | `cISC4NeighborsSimulator` (IID 0x0773FEF0) | 39: QueryInterface 0x006CDF00, AddRef (shared), Release 0x006CE770, Init 0x006D38F0, Shutdown 0x006CFBC0, connections (5-23), deals (24-33), commodities (34-36), the tuning exemplar (37, shared getter), the deleting destructor (38, 0x006D09D0) |
| 0x00AB1ED0 | `cIGZMessageTarget2` | DoMessage 0x006D42B0 |
| 0x00AB1EB8 | `cIGZSerializable` | Write 0x006CFB60, Read 0x006CFDC0, GetGZCLSID 0x006CDEF0 |

gzcom-dll has `cISC4NeighborsSimulator.h`, `cISC4NeighborConnection.h` and `cISC4NeighborDeal.h`; the slots are in their order, overloads the other way round. `GetDealCost` and `GetDealMaxAmount` (35, 36) return a double in ST0, not the `int64_t` gzcom-dll declares.

### Fields

| Offset | Field |
| --- | --- |
| +0xC | set by message 0x6AC284F3 (the network tools); the monthly pass and the automatic offers wait for it |
| +0xD | initialised |
| +0x10 | reference count |
| +0x14, +0x18 | references: the city, the message server |
| +0x1C | the connections (a list, nodes from 0x00B207DC, referenced) |
| +0x20 | the deals (a list, nodes from 0x00B20D58, referenced) |
| +0x24 | deals to take out at the end of the monthly pass (a list of list nodes, STL allocator) |
| +0x28 | the tuning exemplar 6534284A-7A4A8458-6A469021 |
| +0x2C | the price curve (property 0x0A469353; garbage's cost curve 0x4B61FFC9 is loaded into it too) |
| +0x3C, +0x3D | deals to share with the region; deals to save there |
| +0x3E | the UI is off (slot 27): the monthly pass skips the deals |
| +0x3F | the monthly pass is running (deal news during it is posted again) |

Connection types are those of `cISC4NeighborsSimulator::ConnectionType` (1 rail, 2 road, 3 highway, 4 power, 5 pipe, 6 subway, 7 light rail, 8 avenue, 9 ferry, 10 dirt road, 11 monorail, 12 ground highway). Deal types are 0x4773FFE0 to E5 (power, water, garbage; even: the city buys, odd: it sells).

### Messages

| Message | What happens |
| --- | --- |
| 0x278E6003 | network or terrain changed over a cell rectangle: connection markers there back on the ground (0x006D0BE0) |
| 0x26D31EC4 / 0x26D31EC5 | the deals cleared and shared |
| 0x66956814, 0x6695681A | the daily sharing (0x006D0A40) with +0x3C and +0x3D |
| 0x66956816 MonthPassed | the monthly pass (0x006D0E50) |
| 0x6AC284F3 | +0xC set |
| 0x99EF1143 | a power pole removed (not one of interface 0xA8C86142's slot 13): its connections taken off (slot 8) |
| 0xAA7A014D | an occupant's connections taken off (slot 8) |
| 0xEA418D8F | deal news: +0x3D (codes 0 and 8) or +0x3C set, then the monthly pass |

## Connections and deals

A connection's cell lies just outside the city; `OtherSideTile` (0x006CE820) finds the region tile across the edge, `OtherSideCell` (0x006CE920) the city there and the cell on its side. A new connection (slot 6) gets a marker occupant (0x29F10000, property 0xCAA46595) on the ground inside the edge, turned to face out, 15.5 m up for elevated kinds, and message 0x8A0D15DA; taking one off (0x006D17B0) removes its markers and sends 0xEA0D163E.

A deal is offered over a connection that suits it (slot 23: power lines, pipes, or the first of highways, ground highways, avenues, roads, dirt roads). Its price (slot 35) comes from the seller's spare supply and the buyer's unmet demand through the price curve, between the buyer's cost times the tuning's two factors; its largest amount (slot 36) is the smaller of what the seller can spare and what the buyer lacks. `ProposeDeals` (slot 25) offers, for each established adjacent city and commodity, a deal to buy and one to sell when the tuning's smallest amount fits and the paying side's budget allows; a deal runs for a random number of years between the tuning's two (0x006D0520).

The monthly pass (0x006D0E50) updates the commodity statistics (slot 34: what the city makes, its capacity, demand and supply, and the cost ratio, per commodity, into the region view and city properties), then each active deal's state: garbage sold above what the city can spare, ending deals (news codes 2 to 5) queued out, pending deals started (1), suspended ones waiting out the tuning's days, deals due renewed for their length when the payer can afford them (7) or marked (0x10), others marked due at their end (6). Init (0x006D38F0) rebuilds the deals from the adjacent cities' copies (each turned round to this city's side, its dates moved on by the time since their last update).

## The two other classes

`cSC4NeighborConnection` (class ID 0x0A134F24, 0x44 bytes; vtables 0x00AB2034, 0x00AB201C serializable, 0x00AB1FB8 property holder, 0x00AB1FA0 traffic source): type +0x24, cell +0x28, region tile across +0x30, flags +0x38 (bit 0 visited, bit 1 inactive), trips +0x3C (0x34-byte records) and their dirty flag +0x40. As a traffic source (slot 4, 0x006D4C10) it brings its trips to the counts the region data asks for (properties 0xAA9EA81D to 0xAA9EA81F, nine slots each). The game copies three fields of a new trip record from stack it never writes; ours are 0. Save record version 1.

`cSC4NeighborDeal` (class ID 0x2A134F42, 0x80 bytes; vtables 0x00AB1DD8, 0x00AB1DBC serializable, 0x00AB1D58 property holder): type +0x20, region tiles +0x24 (across) and +0x2C (ours), amounts +0x34 to +0x48, cost +0x50, dates +0x58 (start), +0x60 (expiration), +0x68 (current), +0x70 (suspension), flags +0x78 (1 pending, 2 active, 4 ending, 8 no connection, 0x10 cannot pay, 0x20 garbage over, 0x40 due, 0x80 not added, 0x100 suspended, 0x200 waiting). The constructor sets its dates to today by the clock (`time()`), as the game's. Save record version 5.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.neighbors.interface` | slots 10-23, 27-33 and the region-cell helpers (0x006CE820, 0x006CE920) |
| `simulators.neighbors.connections` | slots 5-9 and the removal 0x006D17B0 |
| `simulators.neighbors.deals` | slots 24-26, 34-36 and the deal maker 0x006D0520 |
| `simulators.neighbors.messages` | DoMessage, the daily and monthly passes, the markers update, the date comparison 0x006CE6A0 (the deal news 0x006CF5C0 has its own convention and is copied inline, not hooked) |
| `simulators.neighbors.init`, `.shutdown` | Init, Shutdown |
| `simulators.neighbors.save`, `.load` | Write, Read (version 2: the connections as references) |
| `simulators.neighbors.deal`, `.connection` | every deal and connection function but getters folded with other classes |
| `simulators.neighbors.object` | our own simulator object: constructor, destructor, identity, our copies of the three vtables |
| `simulators.neighbors.classobjects` | our own deal and connection objects under their class IDs, and the game's constructors detoured to ours |

Still the game's: the generic list, set, vector, string, date and stream helpers.

## Test cities

The tutorial cities have connections (13 in Big City, 6 of them roads) and adjacent cities that are not established, so no deals. `OPENSC4_TEST_NEIGHBORS_MONTH=<n>` runs the monthly pass every n days. The fingerprint's `nb` part: the connections through slot 16 (count, and a checksum of each one's type, cells, flags, whether it has a city across, and what slots 10-13 answer for it), the road connections (slot 17), the adjacent cities with and without unestablished ones (slot 20), the deals (slot 28), monthly income and expense (32, 33), and the flags +0xC, +0x3C, +0x3D, +0x3F.

## Parity

All runs on 2026-10-09, two games at a time, other threads' games running too. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`. The week-long runs use `OPENSC4_TEST_OCCUPANT_CYCLE=3` and `OPENSC4_TEST_NEIGHBORS_MONTH=2`.

| Run | Result |
| --- | --- |
| interface (`n-iface7`) | 12 of 12 |
| interface and connections (`n-conn7`) | 12 of 12 |
| interface, connections, deals, messages, shutdown, save, load (`n-msg7`, without the monthly switch) | 12 of 12 |
| everything on, our objects included (`n-all7`) | 8 of 12: on all four Rush Hour saves the deals-to-share flag +0x3C stayed set from day 2. The game's daily pass clears +0x3C when it shares and +0x3D when it saves; ours cleared both only when saving. Fixed |
| everything on after the fix (`n-all8`) | 12 of 12 |
| everything on `saves-plopped` (`n-all8-pl`) | 3 of 3 |
| everything for 90 days on `saves-mm2`, the real monthly passes (`n-all8-mm90`) | 2 of 2 (d0090 91 of 91 days; d0365 on the second pair, the original giving two outcomes there) |

Our simulator object was made in every B run, and the neighbors records were identical in every compared pair.

Not reached: everything that needs a deal. The tutorial regions have no established adjacent city, so no deal was proposed, made, renewed or ended, and the deal class ran only as far as its constructor, Read and Write paths for empty lists. Nor did the network tools message (+0xC stays 0, so the monthly pass returns at once), the connection request dialog, or a connection being made or removed. A region with two established cities and a power or water line between them would reach them.

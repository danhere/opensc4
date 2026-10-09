# The ordinance simulator (cSC4OrdinanceSimulator) and the ordinances

Reimplemented after land value ([landvalue.md](landvalue.md)), the same way: function by function on the original objects under the parity harness ([parity.md](parity.md)), then our own objects under the class IDs. Build 641 addresses throughout. Code: [src/core/ordinance_sim.cpp](../src/core/ordinance_sim.cpp) (the simulator), [src/core/ordinance_class.cpp](../src/core/ordinance_class.cpp) (the ordinances), [src/core/ordinance_object.cpp](../src/core/ordinance_object.cpp) and [src/core/ordinance_objects.cpp](../src/core/ordinance_objects.cpp) (our objects).

The simulator keeps the city's ordinances, makes them available or not as their conditions change, enacts and repeals them, and totals their monthly cost and income for the budget. What an ordinance does to the city is not here: other simulators ask for the sum or product of a property over the enacted ordinances (`GetCumulativeOffset`, `GetCumulativeFactor`).

## The simulator

Class ID 0xA990C08E, interface 0x41193C3A. Object size 0x40. Factory 0x0080E890 (`SC4::operator_new(0x40)`, then the constructor 0x0080E6E0). Code from 0x0080D340 to 0x0080E8B0, about 30 functions. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00ABEAC0 | `cISC4OrdinanceSimulator` (primary) | 26 in gzcom-dll's order, then the scalar deleting destructor (slot 26). AddRef is shared (0x004F7BA0) |
| 0x00ABEAB0 | `cIGZMessageTarget2` | DoMessage 0x0080E000 |
| 0x00ABEA98 | `cIGZSerializable` | Write 0x0080D780, Read 0x0080E8B0, GetGZCLSID 0x0080E230 |

### Fields

| Offset | What |
| --- | --- |
| +0xC | initialised (Init ran) |
| +0xD | loaded: Read ran, so Init keeps the saved ordinances |
| +0x10 | reference count |
| +0x18 | int64: last month's ordinance expense (positive) |
| +0x20 | int64: last month's ordinance income |
| +0x28 | int64: the enactment and repeal income since the month began |
| +0x30, +0x34 | the ordinances: a `std::map<uint32_t, cISC4Ordinance*>` by ordinance ID (header, size); nodes +0x10 key, +0x14 ordinance |

### What it does

- **Init** (0x0080E760): once, registers with the simulator service (0x00B43D5C slot 19, phase 6) under its class name, and once per run a callback with the service at 0x00B43C94 (`ClearCreated` 0x0080D3C0). For a new city (not loaded): the incomes cleared and the ordinances created from the exemplars: `CreateOrdinances` (0x0080E4A0) hands every resource key of type 0x6534284A to `OnOrdinanceExemplar` (0x0080E430), which passes ordinance exemplars (property 0x10, exemplar type, is 0xE) to `CreateOrdinance` (0x0080E360): the GZCOM class object of the key's instance ID with interface 0x8BD4FC74, its key set, Init, into the map under its ID.
- **MonthPassed** (0x0080D950, message 0x66956816): the month's enactment and repeal income (+0x28) starts the income if positive, else the expense; then per ordinance in ID order: an enacted one is simulated (its slot 23) and its adjusted income counted (income if positive, expense otherwise); one whose conditions (slot 21) changed becomes available or unavailable (`SetOrdinanceAvailable`). Then message 0xC8EA5018.
- **Recount** (0x0080D840, message 0x66956810): the same without the pending income and without simulating: availability first, then the enacted ordinances' adjusted income. No test run sends this message; the coverage switch below does.
- **SetOrdinanceOn** (slot 13): the ordinance's own flag, then its enactment income (on) or repeal income (off) added to +0x28; message 0xC2C09554 (`kSC4MessageOrdinanceUpdated`) with the ID and the flag.
- **SetOrdinanceAvailable** (slot 10): an ordinance made unavailable is repealed first; the flag; message 0xA2D1C5B9 (`kSC4MessageOrdinanceAvailable`); one made available that is still on is enacted again through the simulator, so its enactment counts.
- **AddOrdinance** / **RemoveOrdinance** (slots 5, 6): replace an ordinance of the same ID; adding applies what the ordinance brings (available, on, or available now); removing repeals it and makes it unavailable first.
- **GetCumulativeOffset** (slot 24) (property, index): the sum over the enacted ordinances of their property (misc properties, slot 16): an sint32 or uint32 (variant types 6 and 7), or the index-th of an array of them (0x8006, 0x8007). 32 bits in EAX; gzcom-dll declares int64.
- **GetCumulativeFactor** (slot 25) (property, index): the product of a float property (type 10) or the index-th of a float array (0x800A), else 1, rounded to float after each factor.
- **GetOrdinanceByName** (slot 21): the first ordinance in ID order whose name matches, both copied and lower-cased (`cRZBaseString::MakeLower` 0x0090EAC5).
- The queries (slots 7-9, 11, 12, 14-20, 22, 23) read the ordinances' flags and the totals. The arrays write as many IDs as fit and return how many there are.

## The ordinances

`cSC4OrdinanceSimple` (class ID 0x8BD4FC74, interface `cISC4Ordinance` 0xAA5E2247) holds an ordinance exemplar's values; 20 classes derived from it add their own income or conditions. Each is registered under its own class ID, which is also the ordinance's ID and its exemplar's instance ID; the saved city's ordinance records are written under those class IDs.

Object size 0xA0 (0xA8 for the classes with references at +0xA0 and +0xA4). Four interfaces: `cISC4Ordinance` (32 slots, gzcom-dll's order, then `SetKey` and `LoadFromExemplar`), `cIGZSerializable` at +0x4, a `cSCPropertyHolder` at +0x8 (the exemplar's properties; its slot 22 is the deleting destructor) and the holder's second interface at +0x1C.

| Offset | What |
| --- | --- |
| +0x8 | the property holder (+0x20 its exemplar) |
| +0x24 | reference count |
| +0x28 | initialised |
| +0x2C | the exemplar's key (type, group, instance) |
| +0x38 | ID (property 0x28EBBC30) |
| +0x3C, +0x50 | name and description (strings; localised text keys 0x6A416B21 and 0xCA416B2A, else string properties 0x28EBBC32 and 0x28EBBC34, else the ID as `0x%08x`) |
| +0x64 | year first available (0x28EBBC35) |
| +0x68 | chance of availability (0x28EBBC36, a percentage; unused) |
| +0x70, +0x78, +0x80 | int64: enactment, repeal and monthly constant income (0x28EBBC37 to 0x28EBBC39) |
| +0x88 | float: monthly income factor (0x28EBBC3A) |
| +0x8C | advisor (0x28EBBC3B) |
| +0x90 | income ordinance (0x28EBBC3C) |
| +0x91, +0x92, +0x93 | available, on, enabled. `IsOn` is available and on |
| +0x98 | int64: this month's adjusted income |
| +0xA0, +0xA4 | derived classes: references to game objects they read |

- **CheckConditions** (0x0080CAB0): enabled, and the year (the simulator service's slot 6) at least the first available one.
- **GetCurrentMonthlyIncome** (0x0080CB00): the count from the service at 0x00B43DA4 (slot 16) times the factor plus the constant, truncated to int64 (x87 at 53 bits, so doubles).
- **Simulate** (0x0080C0F0): the adjusted income becomes the current monthly income.
- **SetOn** (0x0080CA50): enacting adds the size of the enactment income to the adjusted income, repealing subtracts the size of the repeal income.
- **LoadFromExemplar** (0x0080CDE0): the properties above; **Read** (version 4; below 4 the chance was saved in percent, below 2 the income flag is set for 0xA0D07129) loads the exemplar again afterwards, so the exemplar's values win over the saved ones.

### The derived classes

| Class ID | Ordinance | Own methods |
| --- | --- | --- |
| 0x40D07236 | Free Clinic Program | income: budget department 0x09188F42's line 0x03110000 times the factor plus the constant (0x0080BE90) |
| 0x815B4CEF | Junior Sports Programs | income: department 0x8A4260A6's total (0x0080BF90); conditions: also that total is not 0 (0x0080C020) |
| 0x22F6E80C | Landfill Gas Recovery Program | Init references the objects at 0x00B43DA0 and 0x00B43D28 (0x0080C120); income per the second count of the first one's slot 10 (0x0080C1D0); conditions: that count, and the second object's slot 23 (3) positive (0x0080C240) |
| 0xA0D07129 | Legalize Gambling | income is the constant only (0x0080C350); repeal (0x0080C3A0) demolishes the casinos (lots whose slot 46 gives 0x033A0000) through the service at 0x00B43D00, resets building group 0x033A0000 at the service at 0x00B43DB4 and drops tool 0x88F154FB if it is the current one |
| 0x62BF1DB9 | Neighborhood Watch Program | conditions: the police simulator (0x00B43D98) has a count (slot 24, police stations presumably) (0x0080C590) |
| 0xE0D07233 | Pro-Reading Campaign | income: department 0x09188F4C's lines 0x03190000 and 0x0034A000 (0x0080C780) |
| 0x62F6E7CF | Tourism Promotion Program | income: department 0x8A416368's total times the factor, truncated, plus the constant (0x0080EA90) |
| 0x42BF1E18 | Trash Presort Requirement | income: as 0x62F6E7CF with department 0x6A42A8F5 (0x0080EBE0); conditions: the pollution simulator's value (slot 33) above 0 (0x0080EC90) |

The other twelve only have their own class ID: 0xA2BF1DDC Carpool Incentive Program, 0xA2BF1E43 Clean Air Act, 0x00D0723D Community CPR Training Program, 0x62BF1DAA Smoke Detector Program, 0xE0D0722E Nuclear Free Zone, 0x22F6E81B Paper Waste Reduction Program, 0x82B9999B Power Conservation Act (the only one with a first year, 1970), 0xA2BF1DE5 Commuter Shuttle Service, 0xC2F6E81F Tire Recycling Program, 0xC2BF1E04 Automobile Emission Reduction Act, 0x02BF1DFA Water Conservation Program, 0xC2BF1DC5 Youth Curfew Act. Names and values as the tutorial cities' saves hold them (from the game's exemplars). Legalize Gambling is the only income ordinance (constant 100 a month); the others cost a constant plus a factor (-0.001 to -0.01) times a city figure: the count from 0x00B43DA4 for the base class, the budget lines and counts above for the derived ones. The classes with references release them in their destructors.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.ordinance.interface` | the simulator's slots 5-25 |
| `simulators.ordinance.month` | `MonthPassed`, `Recount` |
| `simulators.ordinance.messages` | `DoMessage` (+0x4 slot 3) |
| `simulators.ordinance.init`, `.shutdown` | `Init` with `CreateOrdinances`, `OnOrdinanceExemplar`, `CreateOrdinance`; `Shutdown` |
| `simulators.ordinance.save`, `.load` | the simulator's `Write`, `Read` (version 4) |
| `simulators.ordinance.ordinances` | `cSC4OrdinanceSimple`'s methods (slots 3-31 and `Write`, `Read`) in all 21 ordinance classes that use them |
| `simulators.ordinance.special` | the derived classes' own methods (22 slots) |
| `simulators.ordinance.object` | our own simulator object under 0xA990C08E |
| `simulators.ordinance.objects` | our own ordinance objects under the 21 class IDs: factories, constructor, destructors, identity, our copies of their vtables |

Still the game's: the map's insert, find, erase and index (generic containers), the property holder (`cSCPropertyHolder`: its constructor, destructor, QueryInterface, Read and Write, and its property getters), the string helpers, GetGZCLSID.

The fingerprint's `ord` part: last month's expense and income, the pending enactment and repeal income, how many ordinances are available and enacted, how many there are, and a checksum of each one's ID, flags and adjusted income in ID order.

## Save records

The simulator (version 4): expense, income (int64), the ordinance count and a reference to each in ID order, the pending income (int64). Each ordinance writes its own record under its class ID (version 4): initialised, ID, name, description, year, chance (float), enactment, repeal and constant income (int64), factor (float), advisor, available, on, enabled, adjusted income (int64), the income flag, then the property holder's record. `tools/re/sc4save.py` decodes both.

## Coverage switches

The test cities enact no ordinances. Three autotest switches (both sides of a parity run get them):

- `OPENSC4_TEST_ORDINANCES=<n>`: on the n-th day every ordinance is made available and enacted.
- `OPENSC4_TEST_ORDINANCES_OFF=<n>`: every ordinance repealed (legalised gambling's repeal demolishes casinos).
- `OPENSC4_TEST_ORDINANCES_CYCLE=<n>`: the new city's creation from the exemplars (the ordinances exist already, so each new one is released again), every ordinance found by name, removed and added back, and the recount message.

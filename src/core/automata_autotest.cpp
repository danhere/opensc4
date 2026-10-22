// The automata controller manager's part of the autotest
// (src/core/autotest_hooks.h; the manager in src/core/automata_ctlmgr.cpp).
//
// OPENSC4_TEST_AUTOMATA=1: a fingerprint part on every day,
//   ` | auto n<occupants>/<controllers> c<checksum> g<side>,<filled>,<entries>,<checksum>`:
//   the controller table (+0x58): the occupants and the controllers, each
//   occupant by its type, (not for automata) its cells and (not for props
//   and automata) its position in x and z, each controller by its kind (attractor, generator,
//   other) and its template (class ID, name, flags, radius), mixed per
//   occupant and summed (the table's order follows addresses); the grid
//   (+0x1C): its side, the cells with a controller, the entries and a sum
//   of (cell index, template). The controller time (+0x34..+0x43) follows
//   frames (A vs A differs from day 4 on): the log only. Nothing
//   of a controller's own state is read (the attractor and generator code
//   is frame-driven). The automaton table (+0x6C) is written by that code
//   on frames: it goes to the log only (`autotest: automata: ...`).
// OPENSC4_TEST_AUTOMATA=2: also, on the run day OPENSC4_TEST_AUTOMATA_DAY
//   (default 3), Write (the game's address: ours when
//   simulators.automata.ctlsave is on) into a stream of the test's own; the
//   fingerprint then carries ` w<answer>,<bytes>,<checksum>` from that day
//   on: the version (the controller time's 12 bytes left out: frames), the occupant count and
//   the sorted per-occupant controller counts (references write nothing to
//   this stream: SC4Stream::WriteReference asks the stream for the save
//   file's segment interface first). The automaton table's records go to
//   the log. Read-only: nothing of the manager or the controllers changes.
//   On the same day it also calls part a2's queries at their game addresses
//   (ours when simulators.automata.ctlquery is on): for up to 16 (occupant,
//   controller) pairs of the controller table, occupants neither automata
//   nor props (their positions follow frames), sorted by occupant and
//   controller hash (address-free): GetController by template (slot 18) and
//   by the controller's name (slot 19), slot 18 with no template, and
//   GetController near the occupant's position by name (slot 17, radius
//   64); the answers by kind and template go into the fingerprint as
//   ` q<pairs>,<checksum>` from that day on. At the same positions
//   GetRelevantControllersByWorldPos for attractors and generators (each
//   list released through ReleaseControllerList, its head freed after), the
//   composite attraction effect and, for network occupants, the commotion:
//   they read the controllers' flags, the controller time and the attractor
//   state, which follow frames: the log only (`autotest: automata: query
//   ...`). Nothing adds, removes or moves a controller or sends a message
//   (the relevant lists' references are given back).
// OPENSC4_TEST_AUTOMATA=3 (part a3c): also, on the same day, for up to 8
//   attractors and 8 generators of the controller table (occupants neither
//   automata nor props, sorted as the query pairs), at the game's addresses
//   (ours when the controller groups are on): the +4 serializable part's
//   QueryInterface 0x00517F30 (three interfaces; the AddRef it makes is
//   taken back from +0x20) and GetGZCLSID 0x00517F00; Write (the part's slot
//   3) into a stream of the test's own; a round trip: a new controller of
//   the same class from its factory reads those bytes (slot 4) and writes
//   them again, which must give the same bytes with an empty name (Read puts
//   the name into the controller's own name, Write saves the template's),
//   and its own name must be the template's; on that new controller SetLimit
//   (slot 19), SetTemplate (15) and ResetAccumulators (26), and for a
//   generator a Read of a stream with version 0 (answers 0); then it is
//   released. And the position from the occupant's lot (slot 22) of the
//   picked controller. The bytes' fixed part (versions, name, position,
//   +0x3C, the attractor's curve, the generator's +0x90) goes into the
//   fingerprint as ` s<controllers>,<checksum>,<state>`, the state checksum
//   (+0x50, +0x54, the flags, the generator's clock pairs and +0x94, the
//   automaton count: tick- and frame-driven) after it; the field values go
//   to the log (`autotest: automata: save ...`). Then the spawn (slot 41,
//   curve 1.0) of up to 4 of those generators, called directly (the tick
//   only calls it with flag 4, the camera view, which the harness never
//   sets): the automata it makes (count, +0x88, +0x90, +0x94, +0x98, each
//   new automaton's type and position) as ` p<generators>,<made>,<checksum>`;
//   with the first new automaton the generator's events 29 (kinds 4 and 0)
//   and 30 (kind 4, which takes back what 29 added), and the first
//   attractor's (one whose template keeps automata, if any) events 29 and
//   30 with kind 0 (in and out of its own list,
//   then 30 again when it is not there), the answers and list counts mixed.
//   The spawn adds automata to the city: from that day on the run is not
//   the one without the test (on both sides alike).
// OPENSC4_TEST_AUTOMATA_VEHMGR=1 (part v1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day the vehicle and pedestrian managers (city slots
//   43 and 44) are queried at the game's addresses (ours when
//   simulators.automata.vehmgr is on), each manager into the fingerprint as
//   ` v<identity>,<kinds>,<scale>,<cells>,<records>,<hits>,<groups>` (the
//   vehicle manager) and ` <...>` (the pedestrian manager) from that day
//   on: QueryInterface for four interfaces on the object and through the
//   serializable part (each AddRef given back) and GetGZCLSID; for kinds 0
//   to 12 slots 73, 74, 85 (the curve's address), 86, 87 and 88 (a 16-word
//   block of the test's own, scales 1.25 and -0.75); slot 82 for the four
//   directions and the record list's hit test (0x00551630) at 12 x 12 cells
//   spread over the city (sizes from the city); the group tests 0x0054D1A0,
//   0x0054D210 and 0x0054D230 over groups 0x4000..0x4240. Slot 75 (it reads
//   the 24-hour clock), the automata of the manager's list with slots 68
//   and 41 and 0x0054D150 (which automata there are follows frames), and
//   those two tests with null go to the log only (`autotest: automata:
//   vehicle manager: ...`). Read-only.
// OPENSC4_TEST_AUTOMATA_VEHMGR=2 (part v2): also, on the same day, for each
//   manager at the game's addresses (ours when the simulators.automata.vehmgr
//   groups are on): Write (the serializable part's slot 3) into a stream of
//   the test's own; into the fingerprint as ` x<fixed>,<flags>` (the vehicle
//   manager) and ` <fixed>,<flags>` (the pedestrian manager) from that day
//   on, where fixed is the versions, the most automata, the two bools,
//   +0xA8C, the kind and the record list (the automata count and the common
//   base's table, frame-driven, go to the log only). Then a round trip: a new
//   manager of the class from its factory (0x005145A0, 0x005145D0), held by
//   one reference, reads those bytes (slot 4; the automata count zeroed in
//   the copy, the references read back as nothing) and writes them again,
//   which must give the copy with its own +0xA8C (Read drops the saved one);
//   reads of version 0, of version 2 (no record list) and of a record list
//   of version 9; AddAutomaton with none while full (its count set to the
//   most for the call, so nothing is made) and RemoveAutomaton with none
//   (flags 0 and 1); a message of a type no one handles and, for the vehicle
//   manager, 0xC9DA96E8 (its list is empty) to its message target, and the
//   vehicle manager's +4 slot 3 with a zeroed message; then the release
//   (Shutdown, not initialized, and the deleting destructor). Then the
//   pedestrian manager's gathering 0x00544F30 on the city's manager around
//   its first pedestrian with distance 0 (none is sent; the sound message
//   0xA9E3B6E3 goes out), the pedestrian's send 0x005448D0 on that
//   pedestrian to its own position (its mode changes: frame-driven state,
//   alike on both sides), and the vehicle manager's overlay stop 0x00562D60
//   on a zeroed object (nothing). The answers' AL and whether the bytes came
//   back the same are the flags. Nothing else of the city's managers
//   changes.
// OPENSC4_TEST_AUTOMATA_VEHMGR=3 (part v3): also, on the same day, for each
//   manager at the game's addresses (ours when the simulators.automata.vehmgr
//   groups are on), into the fingerprint as ` y<fixed>,<flags>` (the vehicle
//   manager) and ` <fixed>,<flags>` (the pedestrian manager) from that day
//   on: on a new manager of the class from its factory, three records by
//   slot 70 (and none without an object) with two building occupants of the
//   controller table (the least by OccupantHash), slot 55 taking the first
//   occupant's records out (0x0054DB50 moves the rest down) and then the
//   second's, the records hashed after each step with the occupants as
//   indices; slot 69 and slot 52 (with the first of two network occupants,
//   the cells' occupants at 12 x 12 places spread over the cells object's
//   rectangle) on its empty automata list (their AL and
//   whether EAX's upper bytes are the list head's); slot 72 for kinds 0, 7,
//   8 and 11 (zero, or slot 88 with a 16-word block of the test's own); then
//   the release. The record copy 0x0054DB50 on records of the test's own.
//   For the vehicle manager, the tuning map's insert and lookup (0x00555350,
//   0x00555400) on a map of the test's own (two records; no key that is not
//   there, which would add a record of stack leftovers in the game), then
//   the lookup of each key of the game's map 0x00B21D60 (read-only), its
//   keys and records hashed. The log only (`autotest: automata: ... manager
//   frames`), on the city's manager, since which automata there are, the
//   cells' contents and the manager's generator follow frames:
//   GetRandomCreationPoint (slot 16) at four places spread over the city
//   (sizes from the city) with the generator (+0x50) seeded for the calls
//   and put back after; the occupant's position (0x0054DD50) and slot 72 for
//   the kinds of +0xA88's tables at the two network occupants; then AddAutomaton
//   with none while there is room (the branch that makes one from the
//   class's pool) and the one made, the list's last, removed again (slot 8
//   with 1). Then the frame work the harness's saves leave off (+0x4C, the
//   manager's own generation, is clear in them and they have no records):
//   the kind sweep (slot 76) through each kind of the list, with the
//   generator seeded for it and put back after (the harness's stopwatch
//   counts its reads, so a call walks a few cells: it is called again until
//   the kind's walk is through, at most 40000 calls), and records as slot 53
//   makes them (the IDs of property 0xE9AEE3DA / 0xE9AEE3DC of the first
//   building occupant of the controller table that has it, by
//   OccupantHash; for the vehicle manager also the drawn record 0x4206)
//   spent by slot 84 with a time of 1, then what is left of them released
//   and cleared. These add automata to the city and take some away: from
//   that day on the run is not the one without the test (on both sides
//   alike).
// OPENSC4_TEST_AUTOMATA_SCRIPTS=1 (part s1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day the city's automata script system (city slot
//   0xC0) is queried at the game's addresses (ours when the
//   simulators.automata.scripts groups are on), into the fingerprint as
//   ` S<identity>,<maps>,<groups>,<matches>,<fresh>` from that day on:
//   QueryInterface for six interfaces on the object and through the +4, +8
//   and +0xC adjustors (each AddRef given back through the answer's slot
//   2, our Release), GetGZCLSID and the +8 part's ID test; the six map
//   getters (as offsets); every name of the name map (+0x4C) through
//   GetOccupantGroup and GetAutomataGroup by name (slots 12, 7), every ID
//   of the ID map (+0x74) through slots 11 and 6 and three IDs not there,
//   the special filter and track building lists, every drive command (slot
//   14) and two keys not there, every controller template by name (slot
//   17), all summed so that the maps' order does not count; per group its
//   ID, class, percentage, words, parent's ID and the group queries
//   (Depth 0x0051C4A0, GetClass 0x0051C3B0, IsInGroup 0x0051C380 for its
//   own, its parent's and 0x4000, FindName 0x0051D970 on its own name map's
//   names and one not there); Matches (the group's slot 3) of up to 16
//   groups (the ID map's, sorted by ID) for up to 8 occupants of the
//   controller table (neither automata nor props, sorted by their hash);
//   it seeds a cRZRandom from the clock for groups below 100 percent (the
//   same on both sides). Then a fresh script system from the factory
//   (0x005148A0) with one reference: QueryInterface, three groups made by
//   CreateOccupantGroup (an automata group with an ID, one without an ID,
//   one with an ID whose parent is set to the first) and the first name
//   once more (refused), the lookups and group queries on them (the first
//   group's class and the third's percentage, 0.5, set for the test), then
//   ReleaseTemplates 0x00520120 and ReleaseGroups 0x005201D0 on it (the
//   groups' last references, so their destructors run) and its Release
//   (Shutdown, not initialized, the destructor body). The city's script
//   system is not changed.
// OPENSC4_TEST_AUTOMATA_SCRIPTS=2 (part s2): also, after the ` S` token,
//   ` T<templates>,<helpers>,<records>`: the city's controller templates
//   (kind, class, +0x20, +0x34, name), summed, and a fresh script system's
//   CreateControllerTemplate (slot 16) for an attractor and a generator, the
//   first name again and a class with no template (both refused), its
//   template lookup and ReleaseTemplates; for the city's groups of the ID
//   map the cached ID list 0x0051CD90 (it fills an empty cache, alike on
//   both sides) and the record lookup 0x005204D0 for every key byte into a
//   three-string record (0x0051D040, 0x0051D0D0); std::sort 0x00522800 of
//   40 behaviour records of the test's own (keyed so that the comparator is
//   a strict weak order; above 16 it partitions), partial_sort 0x00522590 of
//   40 with a middle of 20 (the heap functions), and the full record
//   vector's insert 0x00521350 of 3 copies in the middle and of one at the
//   end. The template names go to the log. The Lua functions are reached
//   with OPENSC4_TEST_LUA_SCRIPT=tests/scripts/automata.lua (an absolute
//   path), which calls sc4game.automata.* (the ` | lua` part's script hash).
// OPENSC4_TEST_AUTOMATA_SCRIPTS=3 (part s3a): also ` L<city>,<fresh>,<load>`:
//   the city's script system's loaded state (every template: its fields,
//   curves, ID lists and vectors, behaviour records; every group's loaded
//   fields; the drive commands; the track buildings; summed by name) and
//   the same of a fresh script system from the factory into which
//   LoadScripts (0x00523B10, at the game's address) loads the scripts, and
//   that load's answer; the fresh system's templates and groups are then
//   released and the object released. The two hashes are equal when the
//   load at the test day reads what the city's load read.
// OPENSC4_TEST_AUTOMATA_AWMGR=1 (part aw1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day, before the parts v1-v3 drives, the city's four
//   automata managers (aircraft, watercraft, vehicle, pedestrian: city
//   slots 45, 46, 43, 44) are queried at the game's addresses (ours when
//   simulators.automata.awmgr is on), into the fingerprint as
//   ` A<aircraft>,<watercraft>,<vehicle>,<pedestrian>,<fresh>,<flags>` from
//   that day on: QueryInterface for the four class IDs and six interfaces on
//   the object and through the +4, +8 and +0xC parts (AL and the answer's
//   offset; each AddRef given back through the answer's Release), AddRef
//   and Release (which must give the count back), GetGZCLSID through the
//   serializable part and the common base's 0x00512740, IsActive, IsPaused,
//   IsDrivingMode, GetMinZoom, IsCameraFollowUpdating, GetExemplarData and
//   its map (as offsets), the five-argument Dispatch stub 0x005053F0; every
//   exemplar record found by its own key (slot 44) and a key not there;
//   every key of the exemplar map through CanCreateGroup (slot 30) and keys
//   not there; every key of the source building table (+0xA64, sorted)
//   through slot 36 and a key not there, and slot 37 over groups
//   0x4000..0x4240. Slot 36 for 0x1402 and 0x1406 and slot 37 for groups
//   whose class is one of those (they ask the object at 0x00B43D28), the
//   reference counts and the automata counts go to the log only. Then a
//   new aircraft and a new watercraft manager from their factories (held
//   by one reference): the same queries, Pause and Resume, three map nodes
//   of the test's own in the aircraft manager's +0xA88 and nodes in the
//   watercraft manager's +0xAA0 and +0xA8C tables (nothing referenced), so
//   that the release (Shutdown, not initialized, and the deleting
//   destructor) walks them; and a bare common base (0xA88 bytes) built by
//   0x005125E0 and deleted by its slot 38 0x00512CD0. The city's managers
//   are not changed.
// OPENSC4_TEST_AUTOMATA_AWMGR=2 (part aw2): also, on the same day, at the
//   game's addresses (ours when the simulators.automata.awmgr groups are on),
//   into the fingerprint as ` B<aircraft>,<watercraft>,<vehicle>,<pedestrian>,
//   <fresh>,<cheats>,<flags>` from that day on: per city manager the common
//   base's Write 0x0050CB80 into a stream of the test's own (the version,
//   the most, the two bools and the source building table; the automata
//   count, which follows frames, to the log), and TrackAutomataGroupInstances
//   (slot 31) for the first two groups of the exemplar map that are not
//   tracked, tracked and untracked again (the AL answers and whether the
//   entry is gone; the set's size, frame-driven, to the log), and for a
//   group the map does not hold. Then a new aircraft manager from its
//   factory, held by one reference: Read 0x005108E0 of the first manager's
//   bytes (the count zeroed) and Write again, which must give the same bytes;
//   reads of versions 7 (refused), 4 and 5; SetMaxCount with -5, 30, 30 and
//   9000; Activate off and on; ReleaseAutomatonSet on an empty set of the
//   test's own; DoMessage with thirteen messages of the test's own (the
//   scale switch, the camera hold and its release, the camera moves, slots
//   48, 50, 57, 58, the null-object branches of 0x89D3E9F3 and 0xEBFF18A3, a
//   type no one handles); then the release. The group instance table's
//   insert 0x00512030 and erase 0x00510F10 on a table of the test's own (two
//   keys in one bucket, the second in the chain erased first, through
//   0x00510450), and the exemplar record copy 0x00512C80 / 0x005120E0 on two
//   records of the test's own. Then the cheat target 0x0050DD30
//   on the city's aircraft manager: another message, another cheat,
//   0xCC478B79 without a word, with the word at 0x00A82B94 and with another
//   (the flag 0x00B09968 put back after), and, when there is no route line
//   yet, 0xE97FCBC8 with the count 7 (the route line made, prepared and
//   shown) and 0xE97FCBC9 (taken away). The route line's place and the new
//   manager's camera point go to the log only.
// OPENSC4_TEST_AUTOMATA_AWMGR=3 (part aw3): also, on the same day, at the
//   game's addresses or through the common base's vtable (ours when the
//   simulators.automata.awmgr groups are on), into the fingerprint as
//   ` D<calls>,<choices>,<points>,<sources>,<camera>,<flags>` from that day
//   on. On a bare common base (0x005125E0, its vtable the base's), lent the
//   city's vehicle manager's cells object and script system (no reference),
//   with fake automata of the test's own (every slot the base calls on them
//   goes into <calls> with its value arguments, a pointer as the fake it
//   is): two tracked groups put in the group instance table; AddAutomaton
//   with none and with three; slot 59; GetInstancesOfGroup (a tracked
//   group, a list group, one fake flagged 1) with each set released by slot
//   33; the chase (slot 22's six refusals, slot 21 for the nearest and for
//   all within a radius); slots 61 and 62 under each state; slot 47 under
//   two most counts (slot 23); slots 57, 50, 60, 48 and 49; slots 19 and 20
//   for each kind of location (<points>); slot 65 and the source building
//   counts of slots 53, 54 and 55 with a building and a network occupant of
//   the city (<sources>); slots 39 and 40 (the counter stays above 0); slot
//   43 (a message nobody takes); slot 46 refusing twice; the frame update
//   (slot 63) through its branches (removals at once and not) and paused;
//   with a view, SetCameraFollow and the camera follow's update (slot 64)
//   three times as the fake moves (<camera>: the camera's centre and the
//   follow's globals; the camera's centre, offset and zoom put back after),
//   then the follow ended; slot 56 after a load (one fake flagged 0x8000,
//   the old save's recount); slot 45. Then the base deleted (slot 38). On
//   the city's four managers, with the generator (+0x50) seeded for the
//   calls and put back after: the exemplar choices (slots 25 to 27) for
//   each group of the exemplar map, the default and a group not there
//   (<choices>, as record indexes), GetRandomCreationPoint (slot 16) at
//   four places (<points>); the kind rates (slot 42, the clock's) and slot
//   41 on a fake to the log only. Then slot 46 making one (priority 0xFF)
//   on the city's vehicle manager, its class into <choices>, the automaton
//   shut down and released (it is in no list). The flags: the automata
//   filter, the cheat object or a followed automaton there (the bare base
//   part is skipped), no building or network occupant, no view, a fake
//   called at a slot it does not know, a set's bucket count not known
//   (0x100; the fakes are put at an address that is 0 modulo it, so that a
//   set's order does not follow where the DLL was loaded). A fake's unknown
//   slot answers 0; the
//   fakes' occupants give a property holder without properties (the
//   automata filter asks for it).
// OPENSC4_TEST_AUTOMATA_AWMGR=4 (part aw4): also, on the same day, at the
//   game's addresses or through the managers' vtables (ours when the
//   simulators.automata.awmgr.air and .water groups are on), into the
//   fingerprint as ` E<aircraft>,<watercraft>,<flags>` from that day on,
//   with fake automata and occupants of the test's own (part aw3's, with
//   slots 10 and 67 in a vtable of their own, part aw3's fakes keeping
//   their traps there, and a property holder that can answer a type and
//   the byte property 0x8BFB1072). A new aircraft manager from its
//   factory, held by one reference: Init twice (the filter, the generator
//   global 0x00B21AF4, the tuning's most and least zoom) and slot 24; Write,
//   Read of those bytes with two list records (Write must give them back),
//   of version 6 (refused), 4 (the common base's body) and of a stream cut
//   in a record; four map entries of the test's own (fakes and the first
//   building occupant of the controller table by its hash); DoMessage with
//   an occupant inserted and removed of the type 0x029244DB, inserted of
//   another (SC4UIScale's 0x004FBFE0, which finds no property), removed
//   twice (0x004FB8D0: the entry and the list record of the property byte),
//   a type no one handles; slot 17 (no automaton, a refused location, a
//   point, no object, the building with the automaton's occupant in the
//   group 0x4301 and not) and slot 18 (a point, no object, the building with
//   a refused and a good location, then without its entry), the answers and
//   the automaton's +0x1E4; the list find 0x004FB2B0 on nodes of the test's
//   own (another byte, an axis 0.001 off, a NaN, one 0.00005 off: found, an
//   empty range); AddAutomaton with a fake and with none, the generator
//   seeded until a helicopter and the other aircraft were made, each removed
//   again (slot 8); Shutdown twice and the release. The city's watercraft
//   manager's Write (the answer and its tables' sizes; the bytes to the
//   log), then a new watercraft manager: Init twice and slot 24; a route of
//   the test's own (three records, ferry route words 0, 1 and 2) by the
//   route insert 0x00574830 (twice), a second pair table node and the
//   reference vector; Write, Read of those bytes (the references come back
//   as nothing) and Write again, reads of versions 7 (refused) and 5; the
//   route, node and vector again; RemoveAutomaton of the kinds 0x4400 (in a
//   record) and 0x4402 (not), of the groups 0x4411 (the node) and 0x4418
//   (the vector's middle, then its end), of none of them and none; the
//   occupant handlers without one (at their addresses: the common base's
//   DoMessage does not test it); DoMessage with 0x2C06BA82, occupants added
//   (of the type 0x029244DB, of a terminal type twice, the dock type, the
//   pair type) and removed (the
//   route's, the terminal and pair ones, 0x029244DB), a type no one handles;
//   AddAutomaton with a fake and with none (a boat, left to Shutdown: the
//   game's RemoveAutomaton reads a block a new boat has not); on tables of
//   the test's own with one bucket the pair and route erases of a node
//   behind another and of the first, and the route pair's constructor and
//   the records' clear on a value of the test's own; Shutdown
//   twice and the release. The tables' sizes after each step. The flags: no
//   building occupant, a manager or bytes missing, a byte round trip that
//   did not come back, a fake called at a slot it does not know. The
//   generator globals are put back; the city's managers are not changed.
// OPENSC4_TEST_AUTOMATA_AWMGR=5 (part aw5): also, on the same day, at the
//   game's addresses or through the managers' vtables (ours when the
//   simulators.automata.awmgr.water.create, .routes and .frame groups are
//   on), into the fingerprint as ` W<create>,<routes>,<frame>,<flags>` from
//   that day on. Fake boats of the test's own (part aw4's fakes in a
//   0x400-byte block, with slots 16, 17 (the radius), 35 and 59; their
//   occupants answer slot 35 and, when given one, the dock interface
//   0xE9793A65 of the test's own: an orientation, a model and its box). The
//   leaf helpers on data of the test's own: the leg's constructor; the
//   crossing rank (none, the kinds 0x4400 and 0x4402, the groups 0x4414,
//   0x4413, none) and the path group test (0x440F, 0x440A, 0x4411, none);
//   the step direction at each index of a path of the eight unit steps and
//   a longer one, and the path cut (-1, past the end, at 3); the route
//   records' copy (empty and of three), fill (and at a null place), erase
//   (the middle, the last) and growing insert (into an empty vector, inside
//   a full one with one and three, at the end); the city's edge cells
//   (sides 0 to 3, 5, -1). The city's watercraft manager, queries only: the
//   lot at no occupant and at the controller table's first building, its
//   water body; the dock search (none, without the interface, the
//   building, the interface at four orientations); open water at four
//   cells; the creation point (slot 16) at three places for three groups,
//   near and not nearer than 200. A new watercraft manager from the
//   factory, Init, its generator seeded, its vtable a copy whose slot 15
//   answers the test's fake boats (or none): two routes, a pair and a
//   terminal of the test's own; CreateAutomaton refused near a listed boat
//   and outside the city; open water; the creation point at the building's
//   lot with a route of it; the boat's path (a target, none) and its legs
//   (start 0 and 1); a record's two legs (0x0056FD60: to a route there and
//   not, with and without a boat, walking an empty path); a new ferry
//   (0x00570DF0: two records, a refused creation); a boat for the terminal
//   (0x005710A0: three kinds, a refused creation, no interface); the edge
//   spawn until a boat is made or 40 calls; the frame update (slot 63) with
//   two listed boats whose paths cross, both timers due, then with the
//   city's ferry routes asked (+0xA88); the game's vtable back, Shutdown
//   and the release. The globals the part changes (the generator, the edge
//   spawn's side, timer and bit, the frame's timers) are put back. The
//   flags: no building, no city watercraft manager, no new manager, a
//   route not made, a fake boat (0x10) or a made one (0x20) called at a
//   slot it does not know, the route table's bucket count not known (0x40;
//   the boats are put at an address that is 0 modulo it, so that the
//   table's order does not follow where the DLL was loaded).
// OPENSC4_TEST_AUTOMATA_SITUATIONS=1 (part c1, with OPENSC4_TEST_AUTOMATA=2 or
//   more): on the same day the city's situation manager (the city's slot
//   0xC4) is called at the game's addresses (ours when the
//   simulators.automata.situations groups are on), into the fingerprint as
//   ` C<identity>,<queries>,<save>,<fresh>,<flags>` from that day on:
//   QueryInterface for six interfaces on the object and through the +4, +8,
//   +0xC and +0x10 adjustors (each AddRef given back), AddRef and Release
//   (the difference), GetGZCLSID; slots 5 (every ID of the success map and
//   one not there; AX), 7, 10 to 16 (the name's text), 17, 18, 27, 30 and
//   32, the pointers of 6, 8 and 9 as null or not (slot 28's count, the
//   pointers and the reference count go to the log only); Write (the
//   serializable part's slot 3) into a stream of the test's own, its answer
//   and bytes. Then a fresh manager from the factory (0x005148C0) with one
//   reference: Read of those bytes and Write again (the same bytes), Read of
//   versions 3 and 1 and of a cut stream; the success map's operator[]
//   0x00527110 (a new key, inserted by 0x005264A0, and an old one) and
//   insert 0x00525F10 (an old key and a new one), slot 5 on them; the
//   getters with the target set to a building of the controller table
//   (slot 16 asks it for its name) and to null again, slots 17 and 18 with
//   a situation of the test's own (statuses 2 and 4), the time and
//   countdown slots over fields set by the test, slots 26 to 31 (the most
//   CSIs put back after); the tracked automata set (+0xA08), the object map
//   (+0xAC4) and the list (+0xA04) filled with objects of the test's own and
//   emptied by 0x00526E60, 0x00527800, 0x005263D0 and 0x00525EC0 (their
//   AddRef, Release and stop counts); ResetTutorials (slot 33) when the
//   preferences' done tutorials are all zero (it saves the preferences);
//   then its Release (Shutdown, not initialized, the deleting destructor and
//   the destructor body). The city's manager is not changed.
// OPENSC4_TEST_AUTOMATA_SITUATIONS=2 (part c2) adds, on the same day,
//   ` D<lua>,<available>,<starts>,<helpers>,<flags>`: a fresh manager whose
//   vtable is a copy with slots 24 (StartJoyride), 36 (the start) and 38
//   (the end) the test's, so no situation starts and no message is sent; only
//   objects of the test's own are handed to the functions (as arguments or in
//   the fresh manager's fields), each recording its calls into a hash. The
//   Lua callbacks (with a manager, without one; activate_situation only with
//   an ID of 0 or no manager, so it posts nothing) and the variables'
//   accessor; slots 19 and 20 for every automata group and GUID of the real
//   advices (up to 8 and 4), 0, an unknown ID, two buildings and null; slots
//   21 to 23 with two services of the test's own (the real advices by GUID,
//   an unknown one; candidates that start or not, slot 22's path, made
//   automata); then 0x005245F0, 0x00525640, 0x005292F0, 0x00528360 (also by
//   +0xAF8), 0x00526380, 0x00524A50, 0x00524A00, 0x005245A0, 0x00528ED0
//   (never with a template, so no icon is added), slot 25 with 0x005291C0
//   (the automaton tracked, so no icon either), 0x005256E0, 0x00526A20 (the
//   city edge, a cell, straight, lot rects), 0x00526EF0 (a script context of
//   the test's own), slot 39, slot 37 without a situation and slot 35 off.
//   StartJoyride (0x00524E20) and 0x00524D80 are not called: they send a
//   message to the city view, and the joyride starts the U-Drive. Flags: 1
//   ran, 2 GUIDs found, 4 groups found, 0x10 a field left set (an error).
// OPENSC4_TEST_AUTOMATA_SITUATIONS=3 (part c3) adds, on the same day,
//   ` E<messages>,<frame>,<handlers>,<list>,<flags>`: a fresh manager as part
//   c2's (slots 24, 35, 36 and 38 the test's), objects of the test's own.
//   DoMessage for every message that posts nothing and opens no popup (an
//   unknown one, 0x8A09FA67, the cheat with no ID or an ID of 0, the end, the
//   automata's events 2, 9, 11, 12 and 5, the load without +0x9F1, an advice
//   gone, the frame, occupants added and removed, a start by an unknown GUID,
//   the driver); the frame 0x0052E300 (the joyride's end unless the U-Drive's
//   camera would be asked, the countdown, the driver, the drive's time, the
//   warning, the target update with the status 4 and 3, the target check);
//   the handlers directly (0x0052E670, 0x0052E710, 0x0052C020, 0x0052A7D0,
//   0x00524DF0, 0x00524CC0, 0x00524580, 0x00525130, 0x00525100, 0x0052C190's
//   early returns, 0x00526720 with the rewards' slots false, 0x00525430,
//   0x0052BF00 without a situation, 0x0052BFB0, 0x0052CAB0 for 0xA2BF8AD5,
//   0xA2BF8AD6 and two it refuses); the list (advices added and removed by
//   message, untracking, a take-over, the most CSIs, 0x0052A1E0, and a list
//   of the test's own made, filled, sorted and merged). Not called:
//   0x0052E910, 0x00525330, 0x00524D10 (they open or close a real popup or
//   post a message) and 0x0052C190's body (it tells the advisor service).
//   Flags: 1 ran, 2 the joyride's end ran, 4 the most-CSIs step ran, 0x10 a
//   field left set. OPENSC4_TEST_C3_TRACE=1 logs each step's hash.
// OPENSC4_TEST_AUTOMATA_SITUATIONS=4 (part c4) adds, on the same day,
//   ` F<targets>,<steps>,<sets>,<filters>,<flags>`: a fresh manager as part
//   c3's, objects of the test's own (a situation whose slots answer the
//   test's floats, automata, a script context with tables and references).
//   The status (0x00524C40 over the statuses, the drive's time from slot 11
//   with the flag 0x8000000) and the time up (0x00524CF0); the overlay's
//   intensity 0x00524660 over ten distances; cell sets and a cell map of the
//   test's own (0x005278E0, 0x005260C0, 0x00525A50, 0x00526510, 0x005271D0,
//   0x00526180, 0x00525AA0 walking them, 0x00526250, 0x005278A0 on a referenced set); the
//   manager's cells (+0xAA8): 0x00526960 and 0x00529560 (the target a fake,
//   at cells of the city's middle); 0x0052BBC0 (never with a next target)
//   and 0x0052C0E0 (no script by the kinds 1 to 4, a script with targets
//   that are neither 0x5005 nor 0x5006); the kinds' checks 0x0052BC50,
//   0x0052BD30, 0x00528C70 (the middle, the edges heading out and in) and
//   0x00529B80 (the cells near the automaton, none with an object, so no
//   camera effect is made); the update 0x0052C8F0 by the kinds, the escort,
//   the check_success script's numbers and the time limit; 0x00525800 and
//   0x005254D0 with the fields found and missing; 0x00525070; 0x00529AF0;
//   0x00524F00 (the real framework's service asked); the cell filter
//   0x00524E90; 0x005294C0 without a situation; the two occupant filters'
//   slot 4 (0x00524AB0, 0x00526650). Not called: 0x005294C0 with a situation
//   (it sends 0xABBBCA64 through 0x00524D80), 0x0052BBC0 and 0x0052C0E0 when
//   a next target would start (0x0052AD00), 0x00529560 without a target (it
//   makes a prop controller for 0x00528580), 0x00529B80 over a cell with an
//   object (the camera effect). Flags: 1 ran, 2 the city's cells used, 0x10
//   a field left set. OPENSC4_TEST_C4_TRACE=1 logs each step's hash, 2 also
//   the fakes' calls.
// OPENSC4_TEST_AUTOMATA_SITUATIONS=5 (part c5) adds, on the same day,
//   ` G<cells>,<fills>,<targets>,<starts>,<flags>`: a fresh manager as part
//   c4's, objects of the test's own; the city's grids, networks and path
//   finder asked as the game asks them. The cell sets by kind (0x0052C4E0:
//   none, the rectangle, the offsets, the zone regions of 0x0052B450 for the
//   kinds 1, 5 and 6 with the radii and counts varied, the lot of 0x005299C0
//   with 0x00528140, the network flood of 0x00529680) with their markers;
//   0x00527EA0 over a set and a cell object of the test's own; 0x00527D30
//   and 0x00528140 directly; 0x0052B200 (no place, none made, made, the
//   script); 0x0052AD00 by kind (the +0xAF8 site, candidates by distance,
//   the network's occupants, the nearest link of 0x00554EA0, the script) and
//   0x00554EA0 directly; slot 36 (0x0052E750) for every kind and flag;
//   StartJoyride (0x00524E20) and 0x00524D80; 0x0052BBC0 and 0x0052C0E0 when
//   a next target starts. For the calls that post a message, start the
//   U-Drive or make a marker or ask a lot, the global they use (the message
//   server 0x00B43CCC, the U-Drive's view 0x00B21D74, the effects service
//   0x00B43D1C, the lot manager 0x00B43D08) is the test's own during the
//   call and put back after it. 0x00528580 is never reached with a new
//   target (the target found is the one already set). Flags: 1 ran, 2 the
//   city's cells used, 4 the joyride ran, 8 a network cell found near the
//   middle (the floods and links start there), 0x10 a field left set.
//   OPENSC4_TEST_C5_TRACE=1 logs each step's hash, 2 also the fakes' calls.
// A remove and add round trip of one occupant's controllers is not done:
// removing sends 0xEA298D50 to the automata, destroys the controllers and
// can drop the occupant's automaton entry (its automata released), and the
// add makes new controllers through the COM; the city would not be the
// same afterwards.
#include "core/automata_awmgr.h"
#include "core/automata_ctlmgr.h"
#include "core/automata_scripts.h"
#include "core/automata_situations.h"
#include "core/automata_vehmgr.h"
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cIGZIStream.h>
#include <cIGZOStream.h>
#include <cIGZString.h>
#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

using automata_ctlmgr::AutomatonNode;
using automata_ctlmgr::ControllerNode;
using automata_ctlmgr::Grid;
using automata_ctlmgr::HashTable;
using automata_ctlmgr::ListNode;
namespace field = automata_ctlmgr::field;
namespace tmpl = automata_ctlmgr::tmpl;
namespace controller = automata_ctlmgr::controller;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixBytes(const void* data, std::size_t size)
    {
        const auto* const b = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ b[i]) * 16777619u;
    }
};

// The occupant's slots (cISC4Occupant): type, position (out), cells (out).
constexpr std::size_t kGetType = 0x1C, kGetPosition = 0x20, kGetCells = 0x64;
// The automata managers' list of their automata (the common base's +0x44:
// head of 0xC-byte nodes, the automaton at +8).
constexpr std::size_t kManagerAutomata = 0x44;
constexpr uint32_t kTypeAutomaton = 0xC7CC75F1, kTypeAutomaton2 = 0x496E5783, kTypeProp = 0xA823821E;

char* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    return Virtual<char*>(city, automata_ctlmgr::kCitySlotManager);
}

HashTable* Table(char* manager, std::size_t offset) { return reinterpret_cast<HashTable*>(manager + offset); }

template <typename Node>
void ForEach(HashTable* table, auto&& fn)
{
    const uint32_t buckets = static_cast<uint32_t>(table->end - table->begin);
    for (uint32_t i = 0; i < buckets; ++i)
        for (auto* node = static_cast<Node*>(table->begin[i]); node; node = node->next)
            fn(node);
}

uint32_t OccupantHash(void* occupant)
{
    Hash hash;
    if (!occupant) {
        hash.Mix(0);
        return hash.h;
    }
    const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
    hash.Mix(type);
    // An automaton's cells move with it on frames: left out with its
    // position.
    if (type != kTypeAutomaton && type != kTypeAutomaton2) {
        uint8_t rect[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        Virtual<uint8_t>(occupant, kGetCells, static_cast<uint8_t*>(rect));
        hash.MixBytes(rect, sizeof rect);
    }
    if (type != kTypeProp && type != kTypeAutomaton && type != kTypeAutomaton2) {
        float position[3] = {};
        Virtual<uint8_t>(occupant, kGetPosition, static_cast<float*>(position));
        hash.MixBytes(&position[0], 4);
        hash.MixBytes(&position[2], 4);
    }
    return hash.h;
}

// A controller by its kind and its template (nothing of its own state).
uint32_t ControllerHash(void* object)
{
    Hash hash;
    if (!object) {
        hash.Mix(0);
        return hash.h;
    }
    const std::uintptr_t vtable = *static_cast<std::uintptr_t*>(object);
    hash.Mix(vtable == controller::kAttractorVtable ? 1u : vtable == controller::kGeneratorVtable ? 2u : 3u);
    char* const t = Virtual<char*>(object, controller::kTemplate);
    if (!t) {
        hash.Mix(0);
        return hash.h;
    }
    hash.Mix(At<uint32_t>(t, tmpl::kClassId));
    hash.Mix(At<uint8_t>(t, tmpl::kFlags));
    hash.Mix(At<uint32_t>(t, tmpl::kRadius));
    const char* const begin = At<const char*>(t, tmpl::kNameBegin);
    const char* const end = At<const char*>(t, tmpl::kNameEnd);
    if (begin && end > begin && end - begin < 256)
        hash.MixBytes(begin, static_cast<std::size_t>(end - begin));
    return hash.h;
}

// An output stream that keeps the bytes written to it (as
// src/core/netmgr_autotest.cpp's). It answers no interface, so the game's
// SC4Stream::WriteReference writes nothing for a reference.
class ByteOStream final : public cIGZOStream {
public:
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    void Flush() override {}
    bool SetSint8(int8_t v) override { return Put(v); }
    bool SetUint8(uint8_t v) override { return Put(v); }
    bool SetSint16(int16_t v) override { return Put(v); }
    bool SetUint16(uint16_t v) override { return Put(v); }
    bool SetSint32(int32_t v) override { return Put(v); }
    bool SetUint32(uint32_t v) override { return Put(v); }
    bool SetSint64(int64_t v) override { return Put(v); }
    bool SetUint64(uint64_t v) override { return Put(v); }
    bool SetFloat32(float v) override { return Put(v); }
    bool SetFloat64(double v) override { return Put(v); }
    bool SetRZCharStr(char const* text) override { return SetVoid(text, static_cast<uint32_t>(std::strlen(text))); }
    bool SetGZStr(cIGZString const& text) override
    {
        const uint32_t length = text.Strlen();
        Put(length);
        return SetVoid(text.ToChar(), length);
    }
    bool SetGZSerializable(cIGZSerializable const&) override { return false; }
    bool SetVoid(void const* data, uint32_t size) override
    {
        const uint8_t* const from = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), from, from + size);
        return true;
    }
    int32_t GetError() override { return 0; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;

private:
    template <typename T>
    bool Put(const T& value)
    {
        return SetVoid(&value, sizeof value);
    }
};

// An input stream over the bytes a ByteOStream kept: a read past the end
// stores nothing and leaves the stream in error. It answers no interface, so
// SC4Stream::ReadReference reads nothing from it and fails (the references
// ByteOStream wrote nothing for). A string is its length and its bytes, as
// ByteOStream::SetGZStr writes it.
class ByteIStream final : public cIGZIStream {
public:
    explicit ByteIStream(std::vector<uint8_t> data) : bytes(std::move(data)) {}
    bool QueryInterface(uint32_t, void**) override { return false; }
    uint32_t AddRef() override { return 1; }
    uint32_t Release() override { return 1; }
    bool Skip(uint32_t count) override
    {
        if (bytes.size() - at < count)
            return Fail();
        at += count;
        return true;
    }
    bool GetSint8(int8_t& v) override { return Get(v); }
    bool GetUint8(uint8_t& v) override { return Get(v); }
    bool GetSint16(int16_t& v) override { return Get(v); }
    bool GetUint16(uint16_t& v) override { return Get(v); }
    bool GetSint32(int32_t& v) override { return Get(v); }
    bool GetUint32(uint32_t& v) override { return Get(v); }
    bool GetSint64(int64_t& v) override { return Get(v); }
    bool GetUint64(uint64_t& v) override { return Get(v); }
    bool GetFloat32(float& v) override { return GetVoid(&v, sizeof v); }
    bool GetFloat64(double& v) override { return GetVoid(&v, sizeof v); }
    bool GetRZCharStr(char*, uint32_t) override { return Fail(); }
    bool GetGZStr(cIGZString& text) override
    {
        uint32_t length = 0;
        if (!Get(length) || bytes.size() - at < length)
            return Fail();
        text.FromChar(reinterpret_cast<const char*>(bytes.data() + at), length);
        at += length;
        return true;
    }
    bool GetGZSerializable(cIGZSerializable&) override { return Fail(); }
    bool GetVoid(void* out, uint32_t size) override
    {
        if (bytes.size() - at < size)
            return Fail();
        std::memcpy(out, bytes.data() + at, size);
        at += size;
        return true;
    }
    int32_t GetError() override { return error; }
    int32_t SetUserData(cIGZVariant*) override { return 0; }
    int32_t GetUserData() override { return 0; }

    std::vector<uint8_t> bytes;
    std::size_t at = 0;
    int32_t error = 0;

private:
    bool Fail()
    {
        error = 1;
        return false;
    }
    template <typename T>
    bool Get(T& value)
    {
        return GetVoid(&value, sizeof value);
    }
};

// What the Write drive left for the fingerprint.
bool g_written = false;
std::string g_writeText;

uint32_t U32(const std::vector<uint8_t>& bytes, std::size_t at)
{
    uint32_t v = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

void WriteDrive(char* manager)
{
    ByteOStream out;
    using WriteFn = uint32_t(__thiscall*)(void* part, void* stream);
    const uint32_t answer = reinterpret_cast<WriteFn>(addr::cSC4AutomataControllerManager_Write)(
        manager + field::kSerializable, static_cast<cIGZOStream*>(&out));
    const std::vector<uint8_t>& b = out.bytes;
    // version (2), the time (4 + 2 + 4 + 2), the occupant count, its counts.
    constexpr std::size_t kHeader = 14;
    Hash hash;
    std::size_t at = kHeader;
    bool fits = b.size() >= kHeader + 4;
    // Bytes 0-1 the version; 2-13 the controller time (frame-driven, left out).
    if (fits)
        hash.MixBytes(b.data(), 2);
    const uint32_t occupants = U32(b, at);
    at += 4;
    std::vector<uint32_t> counts;
    for (uint32_t i = 0; fits && i < occupants; ++i, at += 4) {
        if (at + 4 > b.size()) {
            fits = false;
            break;
        }
        counts.push_back(U32(b, at));
    }
    std::sort(counts.begin(), counts.end());
    hash.Mix(occupants);
    for (const uint32_t c : counts)
        hash.Mix(c);
    const std::size_t first = at;
    // The automaton table: a count, then per entry a count and 25 bytes.
    const uint32_t entries = U32(b, at);
    at += 4;
    std::vector<uint32_t> records;
    for (uint32_t i = 0; fits && i < entries; ++i) {
        if (at + 4 + 25 > b.size()) {
            fits = false;
            break;
        }
        Hash record;
        record.MixBytes(b.data() + at, 4 + 25);
        records.push_back(record.h);
        at += 4 + 25;
    }
    std::sort(records.begin(), records.end());
    Hash automata;
    for (const uint32_t r : records)
        automata.Mix(r);
    char text[96];
    std::snprintf(text, sizeof text, " w%u,%u,%08X%s", answer, static_cast<unsigned>(first), hash.h,
                  fits && at == b.size() ? "" : "?");
    g_writeText = text;
    g_written = true;
    float timeA = 0, timeC = 0;
    int16_t timeB = 0, timeD = 0;
    if (b.size() >= kHeader) {
        std::memcpy(&timeA, b.data() + 2, 4);
        std::memcpy(&timeB, b.data() + 6, 2);
        std::memcpy(&timeC, b.data() + 8, 4);
        std::memcpy(&timeD, b.data() + 12, 2);
    }
    log::Write("autotest: automata: write answer %u, %u bytes, time %.9g %d %.9g %d, %u occupants, %u automaton "
               "entries (%08X)%s",
               answer, static_cast<unsigned>(b.size()), static_cast<double>(timeA), timeB,
               static_cast<double>(timeC), timeD, occupants, entries, automata.h,
               fits && at == b.size() ? "" : ", not the expected layout");
}

// What the query drive left for the fingerprint.
std::string g_queryText;

uint32_t Bits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// A list head as the game makes one (12 bytes, linked to itself).
ListNode* NewHead()
{
    auto* const head =
        reinterpret_cast<ListNode*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(static_cast<uint32_t>(sizeof(ListNode)));
    head->next = head;
    head->prev = head;
    return head;
}

// The controllers of an answered list by kind and template, order-free.
uint32_t ListSum(ListNode* head, uint32_t& count)
{
    uint32_t sum = 0;
    for (ListNode* e = head->next; e != head; e = e->next) {
        ++count;
        sum += ControllerHash(e->value);
    }
    return sum;
}

void QueryDrive(char* manager)
{
    using ByTemplateFn = void*(__thiscall*)(void*, void*, void*);
    using ByNameFn = void*(__thiscall*)(void*, void*, void*);
    using NearFn = void*(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, void*, uint32_t);
    using RelevantFn = uint8_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, ListNode**);
    using ReleaseFn = void(__thiscall*)(void*, ListNode**);
    using EffectFn = void*(__thiscall*)(void*, void*, float*, uint32_t*, uint32_t*);
    using CommotionFn = float(__thiscall*)(void*, void*);
    struct Pick {
        uint32_t key;
        void* occupant;
        void* controller;
    };
    std::vector<Pick> picks;
    ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
        void* const occupant = node->occupant;
        if (!occupant)
            return;
        const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
        if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
            return;
        const uint32_t occupantHash = OccupantHash(occupant);
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            if (!e->value || !Virtual<void*>(e->value, controller::kTemplate))
                continue;
            Hash key;
            key.Mix(occupantHash);
            key.Mix(ControllerHash(e->value));
            picks.push_back({key.h, occupant, e->value});
        }
    });
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.key < b.key; });
    if (picks.size() > 16)
        picks.resize(16);
    const auto byTemplate = reinterpret_cast<ByTemplateFn>(addr::cSC4AutomataControllerManager_GetControllerByTemplate);
    const auto byName = reinterpret_cast<ByNameFn>(addr::cSC4AutomataControllerManager_GetControllerByName);
    const auto nearFn = reinterpret_cast<NearFn>(addr::cSC4AutomataControllerManager_GetControllerNear);
    const auto relevant =
        reinterpret_cast<RelevantFn>(addr::cSC4AutomataControllerManager_GetRelevantControllersByWorldPos);
    const auto release = reinterpret_cast<ReleaseFn>(addr::cSC4AutomataControllerManager_ReleaseControllerList);
    const auto effect = reinterpret_cast<EffectFn>(addr::cSC4AutomataControllerManager_GetCompositeAttractionEffect);
    const auto commotion =
        reinterpret_cast<CommotionFn>(addr::cSC4AutomataControllerManager_GetCompositeAttractionCommotion);
    const auto freeFn = reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free);
    Hash answers, frames;
    uint32_t found = 0, relevantCount = 0, effects = 0, commotions = 0;
    for (const Pick& pick : picks) {
        void* const t = Virtual<void*>(pick.controller, controller::kTemplate);
        void* const name = Virtual<void*>(pick.controller, controller::kName);
        void* const first = byTemplate(manager, pick.occupant, t);
        void* const named = name ? byName(manager, pick.occupant, name) : nullptr;
        void* const none = byTemplate(manager, pick.occupant, nullptr);
        answers.Mix(pick.key);
        answers.Mix(ControllerHash(first));
        answers.Mix(ControllerHash(named));
        answers.Mix(none ? 1u : 0u);
        found += first ? 1u : 0u;
        float position[3] = {};
        Virtual<uint8_t>(pick.occupant, kGetPosition, static_cast<float*>(position));
        void* const close = name ? nearFn(manager, Bits(position[0]), Bits(position[1]), Bits(position[2]), name,
                                          Bits(64.0f))
                                 : nullptr;
        answers.Mix(ControllerHash(close));
        // Frame-driven from here: the log only.
        for (const uint32_t type : {controller::kTypeAttractor, controller::kTypeGenerator}) {
            ListNode* list = NewHead();
            const uint8_t any = relevant(manager, Bits(position[0]), Bits(position[2]), type, &list);
            uint32_t count = 0;
            frames.Mix(any);
            frames.Mix(ListSum(list, count));
            frames.Mix(count);
            relevantCount += count;
            release(manager, &list);
            freeFn(list);
        }
        float vector[3] = {};
        uint32_t value = 0, strength = 0;
        void* const strongest = effect(manager, pick.occupant, static_cast<float*>(vector), &value, &strength);
        effects += strongest ? 1u : 0u;
        frames.Mix(ControllerHash(strongest));
        frames.MixBytes(vector, sizeof vector);
        frames.Mix(value);
        frames.Mix(strength);
        void* network = nullptr;
        if (Virtual<uint8_t>(pick.occupant, 0x0, 0xA821EF94u, &network) && network) {
            frames.Mix(Bits(commotion(manager, network)));
            ++commotions;
            Virtual<uint32_t>(network, 0x8);
        }
    }
    char text[64];
    std::snprintf(text, sizeof text, " q%u,%08X", static_cast<unsigned>(picks.size()), answers.h);
    g_queryText = text;
    log::Write("autotest: automata: query %u pairs, %u found, answers %08X; relevant %u, effects %u, commotions %u "
               "(%08X)",
               static_cast<unsigned>(picks.size()), found, answers.h, relevantCount, effects, commotions, frames.h);
}

// Part a3c's drive (OPENSC4_TEST_AUTOMATA=3), see the top of the file.
std::string g_controllerText;

// The +4 serializable part's slots, and the controller's slots and fields.
namespace serial {
constexpr std::size_t kWrite = 0x0C, kRead = 0x10;
} // namespace serial
namespace ctl {
constexpr std::size_t kRelease = 0x08, kSetTemplate = 0x3C, kSetLimit = 0x4C, kPositionFromBox = 0x58;
constexpr std::size_t kResetAccumulators = 0x68, kEventB = 0x74, kEventC = 0x78, kSpawn = 0xA4;
constexpr std::size_t kInitialized = 0x08, kTemplate = 0x10, kReferences = 0x20, kNameBegin = 0x2C, kNameEnd = 0x30;
constexpr std::size_t kFlags = 0x40, kAutomata = 0x44, kCount = 0x48, k54 = 0x54, k58 = 0x58;
constexpr std::size_t kGen88 = 0x88, kGen90 = 0x90, kGen94 = 0x94, kGen98 = 0x98;
} // namespace ctl

uint32_t U16(const std::vector<uint8_t>& bytes, std::size_t at)
{
    uint16_t v = 0;
    if (at + 2 <= bytes.size())
        std::memcpy(&v, bytes.data() + at, 2);
    return v;
}

float F32(const std::vector<uint8_t>& bytes, std::size_t at)
{
    float v = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&v, bytes.data() + at, 4);
    return v;
}

uint32_t ListCount(char* controller)
{
    ListNode* const head = At<ListNode*>(controller, ctl::kAutomata);
    uint32_t n = 0;
    for (ListNode* e = head->next; e != head; e = e->next)
        ++n;
    return n;
}

// One controller's save, its round trip and the queries on the new one,
// mixed into `fixed` and `state`. False when the bytes are not laid out as
// expected.
bool SaveOne(char* c, bool generator, Hash& fixed, Hash& state, unsigned index)
{
    using QueryFn = uint8_t(__thiscall*)(void*, uint32_t, void**);
    using ClsidFn = uint32_t(__thiscall*)(void*);
    using CreateFn = char*(__cdecl*)();
    char* const part = c + 4;
    const auto query = reinterpret_cast<QueryFn>(addr::cSC4AutomataController_SerializableQueryInterface);
    for (const uint32_t iid : {0xE4FDA3D4u, 0x6A1857F8u, 0x12345678u}) {
        void* out = nullptr;
        const uint8_t answer = query(part, iid, &out);
        fixed.Mix(answer);
        fixed.Mix(out == part ? 1u : out == c ? 2u : out ? 3u : 0u);
        if (answer)
            --At<uint32_t>(c, ctl::kReferences); // the AddRef QueryInterface made
    }
    fixed.Mix(reinterpret_cast<ClsidFn>(addr::cSC4AutomataController_SerializableGetGZCLSID)(part));

    ByteOStream out;
    const uint8_t written = Virtual<uint8_t>(part, serial::kWrite, static_cast<cIGZOStream*>(&out));
    const std::vector<uint8_t>& b = out.bytes;
    // [generator: version 4] version 5, the name (length, bytes), the
    // position (12 bytes), +0x50, +0x54, +0x3C, the flags, the count; then
    // the attractor's version 1 and curve, or the generator's 20 bytes.
    const std::size_t nameAt = generator ? 4 : 2;
    const uint32_t nameLength = U32(b, nameAt);
    const std::size_t at = nameAt + 4 + nameLength;
    const std::size_t baseEnd = at + 12 + 4 * 5;
    fixed.Mix(written);
    fixed.Mix(static_cast<uint32_t>(b.size()));
    if (nameLength >= 256 || b.size() < baseEnd + (generator ? 20 : 2)) {
        fixed.MixBytes(b.data(), b.size());
        log::Write("autotest: automata: save %u: %u bytes, not the expected layout", index,
                   static_cast<unsigned>(b.size()));
        return false;
    }
    fixed.MixBytes(b.data(), nameAt + 4 + nameLength); // the versions and the name
    fixed.MixBytes(b.data() + at, 12);                 // the position
    fixed.Mix(U32(b, at + 20));                        // +0x3C
    state.Mix(U32(b, at + 12));                        // +0x50
    state.Mix(U32(b, at + 16));                        // +0x54
    state.Mix(U32(b, at + 24));                        // the flags
    state.Mix(U32(b, at + 28));                        // the count
    if (generator) {
        state.MixBytes(b.data() + baseEnd, 12); // the clock pairs
        fixed.Mix(U32(b, baseEnd + 12));        // +0x90
        state.Mix(U32(b, baseEnd + 16));        // +0x94
    } else {
        fixed.MixBytes(b.data() + baseEnd, b.size() - baseEnd); // version 1 and the curve
    }

    // The round trip through a new controller of the same class.
    const auto create = reinterpret_cast<CreateFn>(generator ? addr::cSC4AutomataGenerator_Create
                                                             : addr::cSC4AutomataAttractor_Create);
    char* const copy = create();
    uint32_t readAnswer = 0, consumed = 0, readError = 0, same = 0, nameSame = 0, limit = 0;
    uint32_t flagsAfter = 0, value54 = 0, value58 = 0, refused = 0;
    if (copy) {
        ByteIStream in(b);
        readAnswer = Virtual<uint8_t>(copy + 4, serial::kRead, static_cast<cIGZIStream*>(&in));
        consumed = static_cast<uint32_t>(in.at);
        readError = static_cast<uint32_t>(in.error);
        ByteOStream again;
        Virtual<uint8_t>(copy + 4, serial::kWrite, static_cast<cIGZOStream*>(&again));
        std::vector<uint8_t> expected(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(nameAt));
        expected.insert(expected.end(), 4, 0);
        expected.insert(expected.end(), b.begin() + static_cast<std::ptrdiff_t>(at), b.end());
        same = again.bytes == expected ? 1u : 0u;
        const char* const nameBegin = At<const char*>(copy, ctl::kNameBegin);
        const char* const nameEnd = At<const char*>(copy, ctl::kNameEnd);
        nameSame = nameEnd - nameBegin == static_cast<std::ptrdiff_t>(nameLength) &&
                           std::memcmp(nameBegin, b.data() + nameAt + 4, nameLength) == 0
                       ? 1u
                       : 0u;
        Virtual<void>(copy, ctl::kSetLimit, Bits(7.5f));
        limit = At<uint32_t>(copy, ctl::k54);
        Virtual<void>(copy, ctl::kSetTemplate, At<void*>(c, ctl::kTemplate));
        Virtual<void>(copy, ctl::kResetAccumulators);
        flagsAfter = At<uint32_t>(copy, ctl::kFlags);
        value54 = At<uint32_t>(copy, ctl::k54);
        value58 = At<uint32_t>(copy, ctl::k58);
        Virtual<void>(copy, ctl::kSetTemplate, static_cast<void*>(nullptr));
        if (generator) {
            ByteIStream bad(std::vector<uint8_t>{0, 0});
            refused = Virtual<uint32_t>(copy + 4, serial::kRead, static_cast<cIGZIStream*>(&bad)) & 0xFF;
            refused |= static_cast<uint32_t>(bad.at) << 8;
        }
        Virtual<uint32_t>(copy, ctl::kRelease); // the deleting destructor
    }
    for (const uint32_t v : {readAnswer, consumed == b.size() ? 1u : 0u, readError, same, nameSame, limit, flagsAfter,
                             value54, value58, refused})
        fixed.Mix(v);

    float place[3] = {};
    Virtual<void>(c, ctl::kPositionFromBox, static_cast<float*>(place));
    fixed.MixBytes(place, sizeof place);

    log::Write("autotest: automata: save %u %s '%.*s' answer %u, %u bytes, at %.9g %.9g %.9g, 50 %.9g, 54 %.9g, "
               "3C %u, flags %08X, count %u",
               index, generator ? "generator" : "attractor", static_cast<int>(nameLength),
               reinterpret_cast<const char*>(b.data() + nameAt + 4), written, static_cast<unsigned>(b.size()),
               static_cast<double>(F32(b, at)), static_cast<double>(F32(b, at + 4)),
               static_cast<double>(F32(b, at + 8)), static_cast<double>(F32(b, at + 12)),
               static_cast<double>(F32(b, at + 16)), U32(b, at + 20), U32(b, at + 24), U32(b, at + 28));
    if (generator)
        log::Write("autotest: automata: save %u clock %.9g %d %.9g %d, 90 %08X, 94 %u", index,
                   static_cast<double>(F32(b, baseEnd)), static_cast<int>(static_cast<int16_t>(U16(b, baseEnd + 4))),
                   static_cast<double>(F32(b, baseEnd + 6)),
                   static_cast<int>(static_cast<int16_t>(U16(b, baseEnd + 10))), U32(b, baseEnd + 12),
                   U32(b, baseEnd + 16));
    log::Write("autotest: automata: save %u round trip read %u, %u of %u bytes, error %u, same %u, name %u; limit "
               "%08X, reset flags %08X 54 %08X 58 %u, refused %04X; place %.9g %.9g %.9g",
               index, readAnswer, consumed, static_cast<unsigned>(b.size()), readError, same, nameSame, limit,
               flagsAfter, value54, value58, refused, static_cast<double>(place[0]), static_cast<double>(place[1]),
               static_cast<double>(place[2]));
    return true;
}

void ControllerDrive(char* manager)
{
    struct Pick {
        uint32_t key;
        char* controller;
    };
    std::vector<Pick> attractors, generators;
    ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
        void* const occupant = node->occupant;
        if (!occupant)
            return;
        const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
        if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
            return;
        const uint32_t occupantHash = OccupantHash(occupant);
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            char* const c = static_cast<char*>(e->value);
            if (!c || !At<void*>(c, ctl::kTemplate))
                continue;
            const std::uintptr_t vtable = *reinterpret_cast<std::uintptr_t*>(c);
            if (vtable != controller::kAttractorVtable && vtable != controller::kGeneratorVtable)
                continue;
            Hash key;
            key.Mix(occupantHash);
            key.Mix(ControllerHash(c));
            (vtable == controller::kGeneratorVtable ? generators : attractors).push_back({key.h, c});
        }
    });
    for (auto* list : {&attractors, &generators})
        std::sort(list->begin(), list->end(), [](const Pick& a, const Pick& b) { return a.key < b.key; });
    Hash fixed, state;
    unsigned index = 0, odd = 0;
    for (auto* list : {&attractors, &generators})
        for (std::size_t i = 0; i < list->size() && i < 8; ++i) {
            fixed.Mix((*list)[i].key);
            if (!SaveOne((*list)[i].controller, list == &generators, fixed, state, index++))
                ++odd;
        }

    // The spawn of up to 4 initialized generators of the whole table, the
    // first of each (count kind, flags 0x80 and 0x800) first, so the spawn's
    // other branches run where a city has them; then the events with its
    // first new automaton.
    std::vector<Pick> spawners;
    std::vector<uint32_t> kinds;
    for (const int pass : {0, 1})
        for (const Pick& pick : generators) {
            if (spawners.size() == 4)
                break;
            char* const g = pick.controller;
            if (!At<uint8_t>(g, ctl::kInitialized))
                continue;
            const uint32_t kind = (At<uint32_t>(At<char*>(g, ctl::kTemplate), 0x8C) << 16) |
                                  (At<uint32_t>(g, ctl::kFlags) & 0x880u);
            const bool known = std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
            const bool taken = std::find_if(spawners.begin(), spawners.end(), [&](const Pick& p) {
                                   return p.controller == g;
                               }) != spawners.end();
            if (taken || (pass == 0 && known))
                continue;
            kinds.push_back(kind);
            spawners.push_back(pick);
        }
    Hash spawn;
    unsigned spawned = 0, made = 0;
    void* first = nullptr;
    char* firstGenerator = nullptr;
    for (const Pick& pick : spawners) {
        char* const g = pick.controller;
        ++spawned;
        std::vector<void*> before;
        ListNode* const head = At<ListNode*>(g, ctl::kAutomata);
        for (ListNode* e = head->next; e != head; e = e->next)
            before.push_back(e->value);
        const uint32_t countBefore = At<uint32_t>(g, ctl::kCount);
        uint32_t day = 0;
        Virtual<void>(g, ctl::kSpawn, static_cast<void*>(&day), Bits(1.0f));
        uint32_t fresh = 0;
        for (ListNode* e = head->next; e != head; e = e->next) {
            if (std::find(before.begin(), before.end(), e->value) != before.end())
                continue;
            ++fresh;
            void* const automaton = e->value;
            if (!first) {
                first = automaton;
                firstGenerator = g;
            }
            // The list holds automaton interfaces; the type and the position
            // are the occupant's.
            uint32_t type = 0;
            float position[3] = {};
            void* occupant = nullptr;
            if (Virtual<uint8_t>(automaton, 0x0, automata_ctlmgr::kIidOccupant, &occupant) && occupant) {
                type = Virtual<uint32_t>(occupant, kGetType);
                Virtual<uint8_t>(occupant, kGetPosition, static_cast<float*>(position));
                Virtual<uint32_t>(occupant, ctl::kRelease);
            }
            spawn.Mix(type);
            spawn.MixBytes(position, sizeof position);
            log::Write("autotest: automata: spawn %u made type %08X at %.9g %.9g %.9g", spawned, type,
                       static_cast<double>(position[0]), static_cast<double>(position[1]),
                       static_cast<double>(position[2]));
        }
        made += fresh;
        spawn.Mix(pick.key);
        spawn.Mix(At<uint32_t>(g, ctl::kCount) - countBefore);
        spawn.Mix(fresh);
        for (const std::size_t offset : {ctl::kGen88, ctl::kGen90, ctl::kGen94, ctl::kGen98})
            spawn.Mix(At<uint32_t>(g, offset));
        char* const t = At<char*>(g, ctl::kTemplate);
        log::Write("autotest: automata: spawn %u: %u made (count %u to %u), 88 %u, 90 %08X, 94 %u, 98 %u; flags "
                   "%08X, kind %u",
                   spawned, fresh, countBefore, At<uint32_t>(g, ctl::kCount), At<uint32_t>(g, ctl::kGen88),
                   At<uint32_t>(g, ctl::kGen90), At<uint32_t>(g, ctl::kGen94), At<uint32_t>(g, ctl::kGen98),
                   At<uint32_t>(g, ctl::kFlags), At<uint32_t>(t, 0x8C));
    }
    if (first) {
        spawn.Mix(Virtual<uint8_t>(firstGenerator, ctl::kEventB, first, 4u));
        spawn.Mix(Virtual<uint8_t>(firstGenerator, ctl::kEventB, first, 0u));
        spawn.Mix(Virtual<uint8_t>(firstGenerator, ctl::kEventC, first, 4u));
        if (!attractors.empty()) {
            // An attractor whose template keeps its automata (+0x54), so 29
            // adds and 30 takes out; else the first.
            char* a = attractors.front().controller;
            for (const Pick& pick : attractors)
                if (At<uint32_t>(At<char*>(pick.controller, ctl::kTemplate), 0x54)) {
                    a = pick.controller;
                    break;
                }
            const uint32_t c0 = ListCount(a), n0 = At<uint32_t>(a, ctl::kCount);
            spawn.Mix(Virtual<uint8_t>(a, ctl::kEventB, first, 0u));
            const uint32_t c1 = ListCount(a), n1 = At<uint32_t>(a, ctl::kCount);
            spawn.Mix(Virtual<uint8_t>(a, ctl::kEventC, first, 0u));
            const uint32_t c2 = ListCount(a), n2 = At<uint32_t>(a, ctl::kCount);
            spawn.Mix(Virtual<uint8_t>(a, ctl::kEventC, first, 0u));
            const uint32_t c3 = ListCount(a), n3 = At<uint32_t>(a, ctl::kCount);
            for (const uint32_t v : {c1 - c0, n1 - n0, c2 - c0, n2 - n0, c3 - c0, n3 - n0})
                spawn.Mix(v);
            log::Write("autotest: automata: events: attractor list %u %u %u %u, count %u %u %u %u", c0, c1, c2, c3, n0,
                       n1, n2, n3);
        }
    }
    char text[128];
    std::snprintf(text, sizeof text, " s%u,%08X,%08X%s p%u,%u,%08X", index, fixed.h, state.h, odd ? "?" : "",
                  spawned, made, spawn.h);
    g_controllerText = text;
    log::Write("autotest: automata: controllers%s", text);
}

// ====================================================================================
// Part v1's drive (OPENSC4_TEST_AUTOMATA_VEHMGR=1), see the top of the file.

std::string g_managerText;

uint32_t Bits64(double value)
{
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return static_cast<uint32_t>(bits) ^ (static_cast<uint32_t>(bits >> 32) * 31u);
}

void ManagerDrive(cISC4City* city)
{
    namespace vm = automata_vehmgr;
    std::string text;
    const uint32_t cellsX = city->CellCountX(), cellsZ = city->CellCountZ();
    for (int m = 0; m < 2; ++m) {
        char* const manager = Virtual<char*>(city, m ? vm::kCitySlotPedestrians : vm::kCitySlotVehicles);
        if (!manager) {
            text += m ? " -" : " v-";
            continue;
        }
        Hash identity, kinds, scale, cells, records, groups, rate, automata;
        // QueryInterface on the object and through the serializable part's
        // adjustor; each AddRef given back through the answer's slot 2.
        const uint32_t own = m ? vm::kPedestrianClassId : vm::kVehicleClassId;
        const uint32_t other = m ? vm::kVehicleClassId : vm::kPedestrianClassId;
        const uint32_t iids[] = {own, other, 1u, 0x12345678u};
        for (char* part : {manager, manager + vm::field::kSerializable})
            for (const uint32_t iid : iids) {
                void* out = nullptr;
                const uint8_t answer = Virtual<uint8_t>(part, 0x00, iid, &out);
                identity.Mix(answer);
                identity.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - manager) : 0xFFFFFFFFu);
                if (answer && out)
                    Virtual<uint32_t>(out, 0x08);
            }
        identity.Mix(Virtual<uint32_t>(manager + vm::field::kSerializable, 0x14)); // GetGZCLSID
        // The kind tables (73, 74, 85, 86), the scale (87), the matrix (88)
        // with a 16-word block of the test's own, and the rate (75).
        char data[0x54] = {};
        for (int i = 0; i < 16; ++i)
            At<uint16_t>(data, 0x34 + 2 * i) = static_cast<uint16_t>(i * 37 + 5);
        for (uint32_t kind = 0; kind <= 12; ++kind) {
            kinds.Mix(Virtual<uint32_t>(manager, vm::slot::kKindClass, kind));
            kinds.Mix(Virtual<uint32_t>(manager, vm::slot::kKindGroup, kind));
            kinds.Mix(static_cast<uint32_t>(
                reinterpret_cast<std::uintptr_t>(Virtual<void*>(manager, vm::slot::kGetKindCurve, kind))));
            kinds.Mix(Bits(Virtual<float>(manager, vm::slot::kGetKindValue, kind)));
            scale.Mix(Bits64(Virtual<double>(manager, vm::slot::kGetScale, kind, 7u)));
            for (const float s : {1.25f, -0.75f}) {
                float out[16] = {};
                Virtual<void>(manager, vm::slot::kScaledMatrix, kind, static_cast<const char*>(data), s,
                              static_cast<float*>(out));
                scale.MixBytes(out, sizeof out);
            }
            rate.Mix(Bits64(Virtual<double>(manager, vm::slot::kKindRate, kind)));
        }
        // The neighbour cell test (82) and the record list's hits at 12 x 12
        // cells spread over the city, its edges included.
        const bool cellsThere = At<void*>(manager, vm::field::kCells) != nullptr;
        uint32_t recordCount = 0;
        char* const recordHead = At<char*>(manager, vm::field::kRecordList);
        for (char* n = At<char*>(recordHead, 0); n != recordHead; n = At<char*>(n, 0))
            ++recordCount;
        using Hits = uint32_t(__thiscall*)(void*, int32_t, int32_t);
        for (uint32_t i = 0; i < 12 && cellsX && cellsZ; ++i)
            for (uint32_t j = 0; j < 12; ++j) {
                const int32_t x = static_cast<int32_t>(i * (cellsX - 1) / 11);
                const int32_t z = static_cast<int32_t>(j * (cellsZ - 1) / 11);
                if (cellsThere)
                    for (uint32_t direction = 0; direction < 4; ++direction)
                        cells.Mix(Virtual<uint8_t>(manager, vm::slot::kTestNeighbourCell, 0u, x, z, direction));
                records.Mix(reinterpret_cast<Hits>(addr::cSC4VehPedManagerBase_RecordListHits)(
                    manager + vm::field::kRecordList, x, z));
            }
        // The group tests, at the game's addresses.
        using GroupTest = uint32_t(__thiscall*)(void*, uint32_t);
        for (uint32_t group = 0x4000; group <= 0x4240; ++group) {
            const uint32_t a = reinterpret_cast<GroupTest>(addr::cSC4VehPedManagerBase_IsServiceGroup)(manager, group);
            const uint32_t b = reinterpret_cast<GroupTest>(addr::cSC4VehPedManagerBase_IsGroup422C)(manager, group);
            const uint32_t c = reinterpret_cast<GroupTest>(addr::cSC4VehPedManagerBase_IsGroup422E)(manager, group);
            if (a | b | c)
                groups.Mix(group * 8 + a + 2 * b + 4 * c);
        }
        // The automata of the manager's list (+0x44; which there are follows
        // frames): their class (68), the occupant test and the bounds test
        // (41); the log only. Then both tests with null.
        using OccupantTest = uint32_t(__thiscall*)(void*, void*);
        const auto occupantTest = reinterpret_cast<OccupantTest>(addr::cSC4VehPedManagerBase_IsAutomatonOccupant);
        uint32_t count = 0;
        char* const head = At<char*>(manager, kManagerAutomata);
        for (char* n = At<char*>(head, 0); n != head && count < 64; n = At<char*>(n, 0)) {
            void* const automaton = At<void*>(n, 8);
            if (!automaton)
                continue;
            ++count;
            automata.Mix(Virtual<uint32_t>(manager, vm::slot::kAutomatonClass, automaton));
            automata.Mix(occupantTest(manager, automaton));
            automata.Mix(Virtual<uint8_t>(manager, vm::slot::kInExtendedBounds, automaton));
        }
        const uint32_t nulls =
            occupantTest(manager, nullptr) +
            2u * Virtual<uint8_t>(manager, vm::slot::kInExtendedBounds, static_cast<void*>(nullptr));
        char part[160];
        std::snprintf(part, sizeof part, " %s%08X,%08X,%08X,%08X,%u,%08X,%08X", m ? "" : "v", identity.h, kinds.h,
                      scale.h, cells.h, recordCount, records.h, groups.h);
        text += part;
        log::Write("autotest: automata: %s manager: identity %08X kinds %08X scale %08X cells %08X (%s) records %u "
                   "%08X groups %08X rate %08X nulls %u automata %u %08X",
                   m ? "pedestrian" : "vehicle", identity.h, kinds.h, scale.h, cells.h, cellsThere ? "grid" : "none",
                   recordCount, records.h, groups.h, rate.h, nulls, count, automata.h);
    }
    g_managerText = text;
}

// ====================================================================================
// Part v2's drive (OPENSC4_TEST_AUTOMATA_VEHMGR=2), see the top of the file.

void Put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t v)
{
    if (at + 4 <= bytes.size())
        std::memcpy(bytes.data() + at, &v, 4);
}

void Put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t v)
{
    if (at + 2 <= bytes.size())
        std::memcpy(bytes.data() + at, &v, 2);
}

// A cGZMessage of the type, made as the game makes one (0x00544F30), sent to
// a message target part's slot 3; AL of the answer.
uint32_t SendMessageTo(void* target, uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ConstructFn = void*(__thiscall*)(void*);
    void* const raw = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (!raw)
        return 0xFF;
    void* const msg = reinterpret_cast<ConstructFn>(addr::cGZMessage_cGZMessage)(raw);
    Virtual<uint32_t>(msg, 0x04);
    Virtual<uint32_t>(msg, 0x14, type);
    const uint32_t answer = Virtual<uint32_t>(target, automata_vehmgr::slot::kDoMessage, msg) & 0xFF;
    Virtual<uint32_t>(msg, 0x08);
    return answer;
}

void ManagerSaveDrive(cISC4City* city)
{
    namespace vm = automata_vehmgr;
    std::string text;
    for (int m = 0; m < 2; ++m) {
        char* const manager = Virtual<char*>(city, m ? vm::kCitySlotPedestrians : vm::kCitySlotVehicles);
        if (!manager) {
            text += m ? " -" : " x-";
            continue;
        }
        char* const part = manager + vm::field::kSerializable;
        ByteOStream out;
        const uint32_t written = Virtual<uint32_t>(part, vm::slot::kWrite, static_cast<cIGZOStream*>(&out)) & 0xFF;
        const std::vector<uint8_t>& b = out.bytes;
        // u16 3, the common base's (u16 6, s32 the most, two bools, u32 the
        // count, references (none in this stream), u32 +0xA74 and as many
        // pairs of words), u32 +0xA8C, u32 the kind, the record list (u16 1,
        // u32 the count, 49 bytes a record).
        const std::size_t pairsAt = 18;
        const uint32_t pairs = U32(b, 14);
        const std::size_t ownAt = pairsAt + 8 * static_cast<std::size_t>(pairs);
        const std::size_t listAt = ownAt + 8;
        const uint32_t records = U32(b, listAt + 2);
        const bool layout = b.size() >= listAt + 6 && b.size() == listAt + 6 + 49 * static_cast<std::size_t>(records);
        Hash fixed, table;
        fixed.Mix(written);
        fixed.Mix(layout ? 1u : 0u);
        fixed.MixBytes(b.data(), std::min<std::size_t>(b.size(), 10)); // versions, the most, the bools
        if (layout)
            fixed.MixBytes(b.data() + ownAt, b.size() - ownAt);
        if (b.size() >= ownAt)
            table.MixBytes(b.data() + pairsAt, ownAt - pairsAt);
        // The round trip into a new manager of the class, from its factory
        // (the object + 8), held by one reference. The automata count is
        // zeroed in its copy (the references read back as nothing).
        using Factory = void*(__cdecl*)();
        char* const freshPart = static_cast<char*>(
            reinterpret_cast<Factory>(m ? addr::cSC4PedestrianManager_Create : addr::cSC4VehicleManager_Create)());
        uint32_t flags = 0;
        bool same = false;
        if (freshPart && layout) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            std::vector<uint8_t> in = b;
            Put32(in, 10, 0);
            ByteIStream reader(in);
            const uint32_t read = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&reader)) & 0xFF;
            ByteOStream again;
            const uint32_t rewritten =
                Virtual<uint32_t>(freshPart, vm::slot::kWrite, static_cast<cIGZOStream*>(&again)) & 0xFF;
            // Read drops the saved +0xA8C: the new manager writes its own.
            std::vector<uint8_t> expect = in;
            Put32(expect, ownAt, At<uint32_t>(fresh, vm::field::kDefaultClass));
            same = again.bytes == expect;
            std::size_t firstDiff = 0;
            while (firstDiff < again.bytes.size() && firstDiff < expect.size() &&
                   again.bytes[firstDiff] == expect[firstDiff])
                ++firstDiff;
            log::Write("autotest: automata: %s manager round trip: %u bytes back of %u, first difference at %u "
                       "(the own part from %u)",
                       m ? "pedestrian" : "vehicle", static_cast<unsigned>(again.bytes.size()),
                       static_cast<unsigned>(expect.size()), static_cast<unsigned>(firstDiff),
                       static_cast<unsigned>(ownAt));
            // Reads refused or cut short: version 0; version 2 (no record
            // list); the record list's version 9.
            ByteIStream zero(std::vector<uint8_t>{0, 0});
            const uint32_t v0 = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&zero)) & 0xFF;
            std::vector<uint8_t> two(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(listAt));
            Put16(two, 0, 2);
            ByteIStream twoReader(two);
            const uint32_t v2 = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&twoReader)) & 0xFF;
            std::vector<uint8_t> nine = in;
            Put16(nine, listAt, 9);
            ByteIStream nineReader(nine);
            const uint32_t v9 = Virtual<uint32_t>(freshPart, vm::slot::kRead, static_cast<cIGZIStream*>(&nineReader)) & 0xFF;
            // AddAutomaton with none when full (the count set to the most for
            // the call, so nothing is made), RemoveAutomaton with none.
            const uint32_t count = At<uint32_t>(fresh, vm::field::kCount);
            At<uint32_t>(fresh, vm::field::kCount) = At<uint32_t>(fresh, vm::field::kMaxCount);
            const uint32_t add =
                Virtual<uint32_t>(fresh, vm::slot::kAddAutomaton, static_cast<void*>(nullptr)) & 0xFF;
            At<uint32_t>(fresh, vm::field::kCount) = count;
            const uint32_t remove0 =
                Virtual<uint32_t>(fresh, vm::slot::kRemoveAutomaton, static_cast<void*>(nullptr), 0u) & 0xFF;
            const uint32_t remove1 =
                Virtual<uint32_t>(fresh, vm::slot::kRemoveAutomaton, static_cast<void*>(nullptr), 1u) & 0xFF;
            // Messages to its target (+0xC): a type no one handles and, for
            // the vehicle manager, 0xC9DA96E8 (its automata list is empty);
            // the vehicle manager's +4 slot 3 with a zeroed message.
            const uint32_t msgA = SendMessageTo(fresh + vm::field::kMessage, 0x12345678u);
            const uint32_t msgB = m ? 1u : SendMessageTo(fresh + vm::field::kMessage, 0xC9DA96E8u);
            uint32_t zeroMessage[3] = {};
            const uint32_t four =
                m ? 0u
                  : Virtual<uint32_t>(fresh + vm::field::kVtable4, vm::slot::kDoMessage4, static_cast<void*>(zeroMessage)) &
                        0xFF;
            flags = read | rewritten << 1 | (same ? 1u : 0u) << 2 | v0 << 3 | v2 << 4 | v9 << 5 | add << 6 |
                    remove0 << 7 | remove1 << 8 | msgA << 9 | msgB << 10 | four << 11;
            // Release: Shutdown (slot 4, not initialized) and the deleting
            // destructor.
            Virtual<uint32_t>(fresh, 0x08);
        } else if (freshPart) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            Virtual<uint32_t>(fresh, 0x08);
        }
        // The pedestrian manager's gathering (0x00544F30) on the city's
        // manager, around its first pedestrian with distance 0 (so none is
        // sent; the sound message 0xA9E3B6E3 still goes out), and the
        // pedestrian's send (0x005448D0) called on that pedestrian with its
        // own position (it changes the pedestrian's mode: frame-driven state,
        // alike on both sides). The vehicle manager's overlay stop
        // (0x00562D60) on a zeroed object (+0xC clear: nothing). The log
        // only, but the answers' AL go into the flags.
        uint32_t live = 0;
        if (m) {
            char* const head = At<char*>(manager, kManagerAutomata);
            char* const first = At<char*>(head, 0) != head ? At<char*>(At<char*>(head, 0), 8) : nullptr;
            void* occupant = nullptr;
            if (first)
                Virtual<uint8_t>(first, 0x00, 0x26BB767Eu, &occupant);
            if (occupant) {
                using GatherFn = uint32_t(__thiscall*)(void*, void*, uint32_t, uint32_t);
                const uint32_t gathered =
                    reinterpret_cast<GatherFn>(addr::cSC4PedestrianManager_Gather)(manager, occupant, 1u, 0u) & 0xFF;
                float position[3] = {};
                Virtual<uint32_t>(first, 0x1C, static_cast<float*>(position));
                using SendFn = uint32_t(__thiscall*)(void*, const float*, uint32_t);
                const uint32_t sent =
                    reinterpret_cast<SendFn>(addr::cSC4PedestrianManager_SendTo)(first, position, 1u) & 0xFF;
                live = 1u | gathered << 1 | sent << 2;
                Virtual<uint32_t>(occupant, 0x08);
            }
        } else {
            alignas(4) char overlay[0x60] = {};
            using StopFn = void(__thiscall*)(void*);
            reinterpret_cast<StopFn>(addr::cSC4VehicleManager_OverlayStop)(overlay);
            live = 1;
        }
        flags |= live << 12;
        char partText[96];
        std::snprintf(partText, sizeof partText, " %s%08X,%X", m ? "" : "x", fixed.h, flags);
        text += partText;
        log::Write("autotest: automata: %s manager save: %u bytes written %u layout %d count %u table %u %08X kind %u "
                   "records %u fixed %08X flags %X",
                   m ? "pedestrian" : "vehicle", static_cast<unsigned>(b.size()), written, layout ? 1 : 0, U32(b, 10),
                   pairs, table.h, U32(b, ownAt + 4), records, fixed.h, flags);
    }
    g_managerText += text;
}

// ====================================================================================
// Part v3's drive (OPENSC4_TEST_AUTOMATA_VEHMGR=3), see the top of the file.

// Two building occupants of the controller table (neither automata nor
// props), the least by OccupantHash: an order that does not follow
// addresses.
void PickOccupants(char* controllers, void*& first, void*& second)
{
    first = second = nullptr;
    uint32_t h1 = 0xFFFFFFFFu, h2 = 0xFFFFFFFFu;
    if (!controllers)
        return;
    ForEach<ControllerNode>(Table(controllers, field::kControllers), [&](ControllerNode* node) {
        void* const occupant = node->occupant;
        if (!occupant)
            return;
        const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
        if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
            return;
        const uint32_t h = OccupantHash(occupant);
        if (h < h1 || (h == h1 && !first)) {
            second = first;
            h2 = h1;
            first = occupant;
            h1 = h;
        } else if (h < h2 && occupant != first) {
            second = occupant;
            h2 = h;
        }
    });
}

// Two network occupants (the cells' occupants, the interface 0xA821EF94 that
// slots 52, 72 and 0x0054DD50 take): the first two found at 12 x 12 places
// spread over the cells object's rectangle (its slot 0x44), in that order.
void PickNetworks(char* manager, void*& first, void*& second)
{
    first = second = nullptr;
    void* const cells = At<void*>(manager, automata_vehmgr::field::kCells);
    if (!cells)
        return;
    const int32_t* const area = Virtual<const int32_t*>(cells, 0x44);
    for (int32_t i = 0; i < 12 && !second; ++i)
        for (int32_t j = 0; j < 12 && !second; ++j) {
            const int32_t x = area[0] + i * (area[2] - area[0]) / 11;
            const int32_t z = area[1] + j * (area[3] - area[1]) / 11;
            char* const cell = Virtual<char*>(cells, 0x34, x, z);
            void* const network = cell ? At<void*>(cell, 0) : nullptr;
            if (!network || network == first)
                continue;
            (first ? second : first) = network;
        }
}

// The +0xAA8 vector's records, each object as its index in objects (0 for
// another), into the hash.
void MixRecords(Hash& hash, char* manager, void* const* objects, int count)
{
    namespace vm = automata_vehmgr;
    char* const begin = At<char*>(manager, vm::field::kRecordsBegin);
    char* const end = At<char*>(manager, vm::field::kRecordsEnd);
    hash.Mix(static_cast<uint32_t>((end - begin) / 0x1C));
    for (char* r = begin; r < end; r += 0x1C) {
        uint32_t words[7];
        std::memcpy(words, r, sizeof words);
        uint32_t index = 0;
        for (int i = 0; i < count; ++i)
            if (reinterpret_cast<void*>(static_cast<std::uintptr_t>(words[1])) == objects[i])
                index = static_cast<uint32_t>(i + 1);
        words[1] = index;
        hash.MixBytes(words, sizeof words);
    }
}

void ManagerFrameDrive(cISC4City* city)
{
    namespace vm = automata_vehmgr;
    using Factory = void*(__cdecl*)();
    std::string text;
    void* a = nullptr;
    void* b = nullptr;
    PickOccupants(Manager(city), a, b);
    for (int m = 0; m < 2; ++m) {
        char* const manager = Virtual<char*>(city, m ? vm::kCitySlotPedestrians : vm::kCitySlotVehicles);
        if (!manager) {
            text += m ? " -" : " y-";
            continue;
        }
        void* n1 = nullptr;
        void* n2 = nullptr;
        PickNetworks(manager, n1, n2);
        Hash fixed;
        uint32_t flags = 0;
        // On a new manager of the class (held by one reference): records by
        // slot 70 (none without an object), slot 55 taking them out again
        // (0x0054DB50 moves the rest down), slots 69 and 52 on its empty
        // automata list, slot 72's zero kinds and slot 88 kinds.
        char* const freshPart = static_cast<char*>(
            reinterpret_cast<Factory>(m ? addr::cSC4PedestrianManager_Create : addr::cSC4VehicleManager_Create)());
        if (freshPart && a && b && n1) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            void* const objects[2] = {a, b};
            const uint32_t none = Virtual<uint32_t>(fresh, 0x118, 0x1111u, static_cast<void*>(nullptr), 3u, 1u, 2u,
                                                    0x10u) & 0xFF;
            uint32_t added = 0;
            added |= Virtual<uint32_t>(fresh, 0x118, 0xAu, a, 3u, 1u, 2u, 0x10u) & 0xFF;
            added |= (Virtual<uint32_t>(fresh, 0x118, 0xBu, b, 4u, 0u, 3u, 0x20u) & 0xFF) << 1;
            added |= (Virtual<uint32_t>(fresh, 0x118, 0xCu, a, 5u, 1u, 0u, 0x40u) & 0xFF) << 2;
            MixRecords(fixed, fresh, objects, 2);
            const uint32_t removeA = Virtual<uint32_t>(fresh, 0xDC, a) & 0xFF;
            MixRecords(fixed, fresh, objects, 2);
            const uint32_t removeB = Virtual<uint32_t>(fresh, 0xDC, b) & 0xFF;
            MixRecords(fixed, fresh, objects, 2);
            char* const head = At<char*>(fresh, kManagerAutomata);
            const uint32_t forget = Virtual<uint32_t>(fresh, 0x114, static_cast<void*>(nullptr));
            const uint32_t gone = Virtual<uint32_t>(fresh, 0xD0, n1);
            const uint32_t headBits = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(head));
            const uint32_t upper = ((forget ^ headBits) & 0xFFFFFF00u) == 0 ? 1u : 0u;
            const uint32_t upper2 = ((gone ^ headBits) & 0xFFFFFF00u) == 0 ? 1u : 0u;
            char data[0x54] = {};
            for (int i = 0; i < 16; ++i)
                At<uint16_t>(data, 0x34 + 2 * i) = static_cast<uint16_t>(i * 29 + 3);
            for (const uint32_t kind : {0u, 7u, 8u, 11u}) {
                float out[16];
                std::memset(out, 0xCD, sizeof out);
                Virtual<void>(fresh, 0x120, static_cast<void*>(reinterpret_cast<char*>(static_cast<std::uintptr_t>(kind))),
                              static_cast<void*>(data), kind, Bits(kind == 8 ? -0.5f : 1.5f), static_cast<float*>(out));
                fixed.MixBytes(out, sizeof out);
            }
            flags = none | added << 1 | removeA << 4 | removeB << 5 | upper << 7 |
                    (gone & 0xFF) << 8 | upper2 << 9;
            Virtual<uint32_t>(fresh, 0x08);
        } else if (freshPart) {
            char* const fresh = freshPart - vm::field::kSerializable;
            Virtual<uint32_t>(fresh, 0x04);
            Virtual<uint32_t>(fresh, 0x08);
            flags = 0x800;
        }
        // The record copy 0x0054DB50 on records of the test's own: three
        // moved down by one, and an empty range.
        {
            using CopyFn = char*(__cdecl*)(const char*, const char*, char*, void*, uint32_t);
            uint32_t buffer[7 * 4];
            for (int i = 0; i < 28; ++i)
                buffer[i] = static_cast<uint32_t>(i * 0x01010101u);
            uint32_t tag = 0;
            char* const base = reinterpret_cast<char*>(buffer);
            char* const end = reinterpret_cast<CopyFn>(addr::cSC4VehPedManagerBase_CopyRecords)(
                base + 0x1C, base + 0x70, base, &tag, 0u);
            char* const same = reinterpret_cast<CopyFn>(addr::cSC4VehPedManagerBase_CopyRecords)(
                base + 0x1C, base + 0x1C, base + 0x38, &tag, 0u);
            fixed.MixBytes(buffer, sizeof buffer);
            flags |= (end == base + 0x54 ? 1u : 0u) << 10 | (same == base + 0x38 ? 1u : 0u) << 11;
        }
        // The tuning map's insert and lookup (0x00555350, 0x00555400) on a
        // map of the test's own: two records, both found, a third key not
        // asked (it would add a record of stack leftovers in the game).
        if (m == 0) {
            using InsertFn = char*(__thiscall*)(void*, const uint32_t*);
            using AtFn = char*(__thiscall*)(void*, const uint32_t*);
            alignas(4) char map[0x14] = {};
            uint32_t pairs[2][9] = {};
            for (int i = 0; i < 2; ++i) {
                pairs[i][0] = 0x1234u + 97u * static_cast<uint32_t>(i);
                for (int j = 1; j < 5; ++j)
                    pairs[i][j] = Bits(static_cast<float>(i * 4 + j) * 0.25f);
            }
            char* const in0 = reinterpret_cast<InsertFn>(addr::cSC4VehPedManagerBase_MapInsert)(map, pairs[0]);
            char* const in1 = reinterpret_cast<InsertFn>(addr::cSC4VehPedManagerBase_MapInsert)(map, pairs[1]);
            const uint32_t key0 = pairs[0][0], key1 = pairs[1][0];
            char* const at0 = reinterpret_cast<AtFn>(addr::cSC4VehPedManagerBase_MapAt)(map, &key0);
            char* const at1 = reinterpret_cast<AtFn>(addr::cSC4VehPedManagerBase_MapAt)(map, &key1);
            const uint32_t count = At<uint32_t>(map, 0x10);
            const uint32_t buckets = static_cast<uint32_t>((At<char*>(map, 8) - At<char*>(map, 4)) / 4);
            fixed.Mix(count);
            fixed.Mix(buckets);
            if (in0 && in1) {
                fixed.MixBytes(in0, 0x24);
                fixed.MixBytes(in1, 0x24);
            }
            flags |= (at0 == in0 + 4 ? 1u : 0u) << 12 | (at1 == in1 + 4 ? 1u : 0u) << 13;
            // Freed as the game frees its nodes (the curves are empty).
            for (uint32_t i = 0; i < buckets; ++i)
                for (char* node = At<char**>(map, 4)[i]; node;) {
                    char* const next = At<char*>(node, 0);
                    if (At<void*>(node, 0x18))
                        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<void*>(node, 0x18));
                    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
                    node = next;
                }
            if (At<void*>(map, 4))
                reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<void*>(map, 4));
            // The game's tuning map (0x00B21D60, loaded with the tuning):
            // each key's record by the lookup, read-only.
            char* const tuning = reinterpret_cast<char*>(0x00B21D60);
            const uint32_t tuningBuckets = static_cast<uint32_t>((At<char*>(tuning, 8) - At<char*>(tuning, 4)) / 4);
            uint32_t keys = 0, sum = 0, found = 1;
            for (uint32_t i = 0; i < tuningBuckets && i < 4096; ++i)
                for (char* node = At<char**>(tuning, 4)[i]; node; node = At<char*>(node, 0)) {
                    const uint32_t key = At<uint32_t>(node, 4);
                    char* const record = reinterpret_cast<AtFn>(addr::cSC4VehPedManagerBase_MapAt)(tuning, &key);
                    found &= record == node + 8 ? 1u : 0u;
                    Hash h;
                    h.Mix(key);
                    h.MixBytes(node + 8, 0x10);
                    h.Mix(At<uint32_t>(node, 0x24)); // the curve's count
                    sum += h.h;
                    ++keys;
                }
            fixed.Mix(keys);
            fixed.Mix(sum);
            flags |= found << 14;
        }
        // On the city's manager, the log only (which automata there are,
        // the cells' contents and the manager's generator follow frames):
        // GetRandomCreationPoint (slot 16) at four places spread over the
        // city, with the generator (+0x50) seeded for the call and put back
        // after; the occupant's position (0x0054DD50) and slot 72 for kinds
        // 1 to 10 at the two occupants; then AddAutomaton with none (the
        // branch that makes one from the class's pool) while there is room,
        // and the one made (the list's last) removed again (slot 8, 1).
        Hash live;
        uint32_t made = 0, removed = 0;
        {
            constexpr std::size_t kGenerator = 0x50, kGeneratorSize = 0x9C8;
            std::vector<uint8_t> saved(manager + kGenerator, manager + kGenerator + kGeneratorSize);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cRZRandomSuper_Seed)(manager + kGenerator, 12345u);
            const uint32_t cellsX = city->CellCountX(), cellsZ = city->CellCountZ();
            for (uint32_t i = 0; i < 4 && cellsX && cellsZ; ++i) {
                const float position[3] = {static_cast<float>((i % 2 ? 3 : 1) * cellsX * 4), 0.0f,
                                           static_cast<float>((i / 2 ? 3 : 1) * cellsZ * 4)};
                float out[3] = {};
                const uint32_t answer =
                    Virtual<uint32_t>(manager, 0x40, m ? 0x4001u : 0x4002u, static_cast<const float*>(position),
                                      Bits(0.0f), Bits(200.0f), static_cast<float*>(out), 0u, 0u) & 0xFF;
                live.Mix(answer);
                live.MixBytes(out, sizeof out);
            }
            std::memcpy(manager + kGenerator, saved.data(), kGeneratorSize);
            using PositionFn = void*(__thiscall*)(void*, uint32_t, void*, uint32_t, float*);
            for (void* occupant : {n1, n2}) {
                if (!occupant)
                    continue;
                float out[3] = {};
                void* const point = reinterpret_cast<PositionFn>(addr::cSC4VehPedManagerBase_OccupantPosition)(
                    manager, m ? 0x4001u : 0x4002u, occupant, 0u, out);
                live.Mix(point ? 1u : 0u);
                live.MixBytes(out, sizeof out);
                for (uint32_t kind = 1; kind <= 10; ++kind) {
                    if (kind == 7 || kind == 8)
                        continue;
                    float table[16] = {};
                    Virtual<void>(manager, 0x120, occupant, 0u, kind, Bits(1.0f), static_cast<float*>(table));
                    live.MixBytes(table, sizeof table);
                }
            }
            if (At<uint32_t>(manager, vm::field::kCount) < At<uint32_t>(manager, vm::field::kMaxCount)) {
                char* const list = At<char*>(manager, kManagerAutomata);
                char* const lastBefore = At<char*>(list, 4);
                made = Virtual<uint32_t>(manager, vm::slot::kAddAutomaton, static_cast<void*>(nullptr)) & 0xFF;
                char* const last = At<char*>(list, 4);
                if (made && last != lastBefore && last != list)
                    removed = Virtual<uint32_t>(manager, vm::slot::kRemoveAutomaton, At<void*>(last, 8), 1u) & 0xFF;
            }
            // The frame work the harness's saves leave off (+0x4C, the
            // manager's own generation, is clear in them, and they have no
            // records): the kind sweep (slot 76) once per kind of the list,
            // with the generator seeded for it and put back after (the
            // stopwatch is on simulation time, so a sweep goes through the
            // whole rectangle); then records as slot 53 makes them for the
            // first building occupant of the controller table with the
            // property (0xE9AEE3DA / 0xE9AEE3DC; for the vehicle manager also
            // the drawn record 0x4206), spent by slot 84 with a time of 1,
            // and what is left of them released and cleared.
            const uint32_t before = At<uint32_t>(manager, vm::field::kCount);
            std::memcpy(saved.data(), manager + kGenerator, kGeneratorSize);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::cRZRandomSuper_Seed)(manager + kGenerator, 54321u);
            uint32_t kinds = 0;
            char* const kindHead = At<char*>(manager, vm::field::kKinds);
            for (char* n = At<char*>(kindHead, 0); n != kindHead && kinds < 16; n = At<char*>(n, 0))
                ++kinds;
            // The sweep's gates, to the log: the day or night (0x00B43DC4's
            // slot 0x58), the kinds' mask (0x00B21D5C) and each kind's slot 42.
            char gates[160];
            int at = std::snprintf(gates, sizeof gates, "light %u mask %X",
                                   Virtual<uint32_t>(*reinterpret_cast<void**>(0x00B43DC4), 0x58),
                                   *reinterpret_cast<const uint32_t*>(0x00B21D5C));
            for (char* n = At<char*>(kindHead, 0); n != kindHead && at > 0 && at < 140; n = At<char*>(n, 0))
                at += std::snprintf(gates + at, sizeof gates - static_cast<std::size_t>(at), " %u:%.3g", At<uint32_t>(n, 8),
                                    Virtual<double>(manager, 0xA8, At<uint32_t>(n, 8)));
            // Each call stops after five reads of its stopwatch (the
            // harness's runs on simulation time and counts each read): it is
            // called again until the kind's walk is through (the cursor back
            // to -1), at most 40000 calls in all.
            const uint8_t generation = At<uint8_t>(manager, 0x4C);
            At<uint8_t>(manager, 0x4C) = 1;
            uint32_t calls = 0;
            for (uint32_t i = 0; i < kinds; ++i)
                do {
                    Virtual<uint32_t>(manager, 0x130, Bits(1.0f));
                } while (++calls < 40000 && At<int32_t>(manager, vm::field::kA9C) >= 0);
            const uint32_t swept = At<uint32_t>(manager, vm::field::kCount);
            void* holder = nullptr;
            const uint32_t property = m ? 0xE9AEE3DCu : 0xE9AEE3DAu;
            uint32_t bestHash = 0xFFFFFFFFu;
            ForEach<ControllerNode>(Table(Manager(city), field::kControllers), [&](ControllerNode* node) {
                void* const occupant = node->occupant;
                if (!occupant)
                    return;
                const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
                if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
                    return;
                void* const properties = Virtual<void*>(occupant, 0x18);
                if (!properties || !Virtual<void*>(properties, 0x24, property))
                    return;
                const uint32_t h = OccupantHash(occupant);
                if (h < bestHash) {
                    bestHash = h;
                    holder = occupant;
                }
            });
            uint32_t records = 0;
            if (holder) {
                void* const data = Virtual<void*>(Virtual<void*>(Virtual<void*>(holder, 0x18), 0x24, property), 0x14);
                const uint32_t* const ids = Virtual<const uint32_t*>(data, 0x19C);
                const uint32_t count = Virtual<uint32_t>(data, 0x10);
                for (uint32_t i = 0; i < count && i < 8; ++i)
                    records += Virtual<uint32_t>(manager, 0x118, ids[i], holder, 1u, m ? 0u : 1u, m ? 2u : 5u, 2u) & 0xFF;
                if (!m)
                    records += Virtual<uint32_t>(manager, 0x118, 0x4206u, holder, 1u, 0u, 8u, 2u) & 0xFF;
                Virtual<uint32_t>(manager, 0x150, Bits(1.0f));
            }
            char* const begin = At<char*>(manager, vm::field::kRecordsBegin);
            const uint32_t left = static_cast<uint32_t>((At<char*>(manager, vm::field::kRecordsEnd) - begin) / 0x1C);
            for (char* r = begin; r < At<char*>(manager, vm::field::kRecordsEnd); r += 0x1C)
                if (void* const object = At<void*>(r, 4))
                    Virtual<uint32_t>(object, 0x08);
            At<char*>(manager, vm::field::kRecordsEnd) = begin;
            At<uint8_t>(manager, 0x4C) = generation;
            std::memcpy(manager + kGenerator, saved.data(), kGeneratorSize);
            // Slot 53 on a new manager with the holder's building interface
            // (what the building's completion message 0x2977AE2F carries; its
            // slot 0xC gives the occupant back): its records, hashed with the
            // occupant as index 1, then released and cleared; room for eight
            // made for the call (+0x3C), put back after.
            // Without an occupant of the property, the first building
            // occupant (slot 53 then makes no record).
            void* const building = holder ? holder : a;
            char* const freshPart2 = building ? static_cast<char*>(reinterpret_cast<Factory>(
                                                   m ? addr::cSC4PedestrianManager_Create : addr::cSC4VehicleManager_Create)())
                                            : nullptr;
            if (freshPart2) {
                char* const fresh = freshPart2 - vm::field::kSerializable;
                Virtual<uint32_t>(fresh, 0x04);
                // The holder's building interface (cISC4BuildingOccupant,
                // 0x87DDA3A9), the reference given back after.
                void* lot = nullptr;
                Virtual<uint8_t>(building, 0x00, 0x87DDA3A9u, &lot);
                if (lot && Virtual<void*>(lot, 0x0C) != building)
                    flags |= 1u << 20;
                if (lot && Virtual<void*>(lot, 0x0C) == building) {
                    const uint32_t most = At<uint32_t>(fresh, vm::field::kMaxCount);
                    At<uint32_t>(fresh, vm::field::kMaxCount) = 8;
                    const uint32_t added = Virtual<uint32_t>(fresh, 0xD4, lot) & 0xFF;
                    At<uint32_t>(fresh, vm::field::kMaxCount) = most;
                    void* const objects[1] = {building};
                    MixRecords(fixed, fresh, objects, 1);
                    char* const fb = At<char*>(fresh, vm::field::kRecordsBegin);
                    for (char* r = fb; r < At<char*>(fresh, vm::field::kRecordsEnd); r += 0x1C)
                        if (void* const object = At<void*>(r, 4))
                            Virtual<uint32_t>(object, 0x08);
                    At<char*>(fresh, vm::field::kRecordsEnd) = fb;
                    flags |= (1u | added << 1) << 16;
                }
                if (lot)
                    Virtual<uint32_t>(lot, 0x08);
                Virtual<uint32_t>(fresh, 0x08);
            }
            log::Write("autotest: automata: %s manager frames: %u kinds swept (%u calls), automata %u -> %u -> %u, %u records "
                       "(%u left), holder %d; %s",
                       m ? "pedestrian" : "vehicle", kinds, calls, before, swept, At<uint32_t>(manager, vm::field::kCount),
                       records, left, holder ? 1 : 0, gates);
        }
        char partText[96];
        std::snprintf(partText, sizeof partText, " %s%08X,%X", m ? "" : "y", fixed.h, flags);
        text += partText;
        log::Write("autotest: automata: %s manager frames: fixed %08X flags %X live %08X made %u removed %u "
                   "occupants %d networks %d",
                   m ? "pedestrian" : "vehicle", fixed.h, flags, live.h, made, removed, (a ? 1 : 0) + (b ? 1 : 0),
                   (n1 ? 1 : 0) + (n2 ? 1 : 0));
    }
    g_managerText += text;
}

// ====================================================================================
// Part s1's drive (OPENSC4_TEST_AUTOMATA_SCRIPTS=1), see the top of the file.

std::string g_scriptText;

// An SC4String of the test's own (vtable 0x00A80810, the text in a buffer
// from the game's allocator, as the game makes them).
struct TestString {
    std::uintptr_t vtable = addr::SC4String_Vtable;
    char* begin = nullptr;
    char* end = nullptr;
    char* capacity = nullptr;
    uint32_t extra = 0;
    explicit TestString(const char* text)
    {
        const std::size_t length = std::strlen(text);
        using Allocate = void*(__cdecl*)(uint32_t);
        begin = static_cast<char*>(
            reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(static_cast<uint32_t>(length + 1)));
        std::memcpy(begin, text, length + 1);
        end = begin + length;
        capacity = begin + length + 1;
    }
    ~TestString() { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin); }
    TestString(const TestString&) = delete;
    TestString& operator=(const TestString&) = delete;
};

uint32_t NameHash(const char* begin, const char* end)
{
    Hash h;
    if (begin && end >= begin && end - begin < 512)
        h.MixBytes(begin, static_cast<std::size_t>(end - begin));
    return h.h;
}

// The nodes of a script system hash map (the field: +4 the buckets' begin,
// +8 their end), in bucket order.
template <typename Fn>
void ForEachScriptNode(char* table, Fn&& fn)
{
    char** const begin = At<char**>(table, 4);
    char** const end = At<char**>(table, 8);
    for (char** b = begin; b < end; ++b)
        for (char* node = *b; node; node = At<char*>(node, 0))
            fn(node);
}

// A group's own values and its queries at the game's addresses.
uint32_t GroupHash(char* g)
{
    namespace sg = automata_scripts::group;
    Hash h;
    if (!g) {
        h.Mix(0);
        return h.h;
    }
    using Get = uint32_t(__thiscall*)(void*);
    using Test = uint8_t(__thiscall*)(void*, uint32_t);
    using Find = uint8_t(__thiscall*)(void*, const void*, uint32_t*);
    const auto isIn = reinterpret_cast<Test>(addr::cSC4OccupantGroupTemplate_IsInGroup);
    char* const parent = At<char*>(g, sg::kParent);
    h.Mix(At<uint32_t>(g, sg::kId));
    h.Mix(At<uint32_t>(g, sg::kClass));
    h.Mix(At<uint32_t>(g, sg::kPercentage));
    h.Mix(At<uint16_t>(g, sg::kWord4C) | static_cast<uint32_t>(At<uint16_t>(g, sg::kWord4E)) << 16);
    h.Mix(parent ? At<uint32_t>(parent, sg::kId) : 0xFFFFFFFFu);
    h.Mix(reinterpret_cast<Get>(addr::cSC4OccupantGroupTemplate_Depth)(g));
    h.Mix(reinterpret_cast<Get>(addr::cSC4OccupantGroupTemplate_GetClass)(g));
    h.Mix(isIn(g, At<uint32_t>(g, sg::kId)) | isIn(g, parent ? At<uint32_t>(parent, sg::kId) : 0u) << 1 |
          isIn(g, 0x4000u) << 2);
    // FindName on the group's own name map, each name and one not there;
    // the answers summed (the map's order does not count). A node stands
    // for an SC4String: its key is at +4, where the lookups read a name's.
    const auto find = reinterpret_cast<Find>(addr::cSC4OccupantGroupTemplate_FindName);
    uint32_t names = 0, sum = 0;
    ForEachScriptNode(g + sg::kNames, [&](char* node) {
        uint32_t out = 0;
        const uint8_t found = find(g, node, &out);
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(found);
        e.Mix(out == At<uint32_t>(node, 0x10) ? 1u : 0u);
        sum += e.h;
        ++names;
    });
    TestString missing("opensc4 no such name");
    uint32_t out = 0;
    h.Mix(names);
    h.Mix(sum);
    h.Mix(find(g, &missing, &out));
    return h.h;
}

void ScriptsDrive(cISC4City* city)
{
    namespace ss = automata_scripts;
    char* const scripts = Virtual<char*>(city, 0xC0); // GetAutomataScriptSystem
    if (!scripts) {
        g_scriptText = " S-";
        return;
    }
    Hash identity, maps, groups, matches, fresh;
    // QueryInterface on the object and through the adjustors of its +4, +8
    // and +0xC parts; each AddRef given back through the answer's slot 2.
    const uint32_t iids[] = {ss::kScriptSystemIid, 1u, 0x452294AAu, 0xE98B2F57u, 0xE4FDA3D4u, 0x12345678u};
    for (const std::size_t at : {std::size_t{0}, ss::field::kVtable4, ss::field::kVtable8, ss::field::kSerializable})
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            const uint8_t answer = Virtual<uint8_t>(scripts + at, 0x00, iid, &out);
            identity.Mix(answer);
            identity.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - scripts) : 0xFFFFFFFFu);
            if (answer && out)
                Virtual<uint32_t>(out, 0x08);
        }
    identity.Mix(Virtual<uint32_t>(scripts + ss::field::kSerializable, 0x14)); // GetGZCLSID
    const uint32_t ids[] = {0x87163754u, 0u};
    for (const uint32_t& id : ids)
        identity.Mix(Virtual<uint8_t>(scripts + ss::field::kVtable8, 0x0C, &id));
    identity.Mix(At<uint8_t>(scripts, ss::field::kInitialized));
    // The map getters (slots 5, 8, 9, 10, 15, 18), as offsets.
    for (const std::size_t s : {0x14u, 0x20u, 0x24u, 0x28u, 0x3Cu, 0x48u})
        maps.Mix(static_cast<uint32_t>(Virtual<char*>(scripts, s) - scripts));
    // The name map: each name by slot 12 and slot 7, each group's hash.
    uint32_t named = 0, namedSum = 0;
    ForEachScriptNode(scripts + ss::field::kGroupsByName, [&](char* node) {
        char* const g = At<char*>(node, 0x10);
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(Virtual<char*>(scripts, 0x30, static_cast<void*>(node)) == g ? 1u : 0u);
        char* const automata = Virtual<char*>(scripts, 0x1C, static_cast<void*>(node));
        e.Mix(automata == g ? 1u : automata ? 2u : 0u);
        e.Mix(GroupHash(g));
        namedSum += e.h;
        ++named;
    });
    maps.Mix(named);
    maps.Mix(namedSum);
    uint32_t automataCount = 0;
    ForEachScriptNode(scripts + ss::field::kAutomataGroups, [&](char*) { ++automataCount; });
    maps.Mix(automataCount);
    // The ID map: each ID by slots 11 and 6, three IDs not there; the groups
    // kept for Matches, sorted by ID.
    std::vector<std::pair<uint32_t, char*>> byId;
    uint32_t idSum = 0;
    ForEachScriptNode(scripts + ss::field::kGroupsById, [&](char* node) {
        const uint32_t id = At<uint32_t>(node, 4);
        char* const g = At<char*>(node, 8);
        Hash e;
        e.Mix(id);
        e.Mix(Virtual<char*>(scripts, 0x2C, id) == g ? 1u : 0u);
        e.Mix(Virtual<char*>(scripts, 0x18, id) == g ? 1u : 0u);
        e.Mix(GroupHash(g));
        idSum += e.h;
        byId.emplace_back(id, g);
    });
    maps.Mix(static_cast<uint32_t>(byId.size()));
    maps.Mix(idSum);
    for (const uint32_t id : {0u, 0xFFFFFFFFu, 0x7A5A0000u})
        maps.Mix(Virtual<uint32_t>(scripts, 0x2C, id) | Virtual<uint32_t>(scripts, 0x18, id));
    // The special filter and track building lists (0xC-byte nodes, the
    // value at +8), in order.
    for (const std::size_t at : {ss::field::kSpecialGroups, ss::field::kTrackGroups}) {
        char* const head = At<char*>(scripts, at);
        uint32_t count = 0;
        for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0)) {
            ++count;
            maps.Mix(at == ss::field::kSpecialGroups ? GroupHash(At<char*>(n, 8)) : At<uint32_t>(n, 8));
        }
        maps.Mix(count);
    }
    // The drive commands (slot 14): each key and two not there.
    uint32_t drives = 0, driveSum = 0;
    ForEachScriptNode(scripts + ss::field::kDriveCommands, [&](char* node) {
        Hash e;
        e.Mix(At<uint32_t>(node, 4));
        e.Mix(At<uint32_t>(node, 8));
        e.Mix(Virtual<uint32_t>(scripts, 0x38, At<uint32_t>(node, 4)));
        driveSum += e.h;
        ++drives;
    });
    maps.Mix(drives);
    maps.Mix(driveSum);
    maps.Mix(Virtual<uint32_t>(scripts, 0x38, 0x7A5A0000u));
    maps.Mix(Virtual<uint32_t>(scripts, 0x38, 0xFFFFFFFFu));
    // The controller templates by name (slot 17).
    uint32_t templates = 0, templateSum = 0;
    ForEachScriptNode(scripts + ss::field::kTemplates, [&](char* node) {
        char* const t = At<char*>(node, 0x10);
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(Virtual<char*>(scripts, 0x44, static_cast<void*>(node)) == t ? 1u : 0u);
        e.Mix(t ? At<uint32_t>(t, tmpl::kClassId) : 0u);
        templateSum += e.h;
        ++templates;
    });
    maps.Mix(templates);
    maps.Mix(templateSum);
    TestString missing("opensc4 no such name");
    maps.Mix(Virtual<uint32_t>(scripts, 0x44, static_cast<void*>(&missing)) |
             Virtual<uint32_t>(scripts, 0x30, static_cast<void*>(&missing)) |
             Virtual<uint32_t>(scripts, 0x1C, static_cast<void*>(&missing)));
    groups.Mix(named);
    groups.Mix(namedSum ^ idSum);
    // Matches (slot 3) of up to 16 groups for up to 8 occupants.
    std::sort(byId.begin(), byId.end());
    if (byId.size() > 16)
        byId.resize(16);
    std::vector<std::pair<uint32_t, void*>> occupants;
    if (char* const manager = Manager(city))
        ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
            if (!node->occupant)
                return;
            const uint32_t type = Virtual<uint32_t>(node->occupant, kGetType);
            if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
                return;
            occupants.emplace_back(OccupantHash(node->occupant), node->occupant);
        });
    std::sort(occupants.begin(), occupants.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (occupants.size() > 8)
        occupants.resize(8);
    uint32_t matched = 0, matchedId = 0;
    for (const auto& [hash, occupant] : occupants)
        for (const auto& [id, g] : byId) {
            const uint8_t m = Virtual<uint8_t>(g, ss::slot::kGroupMatches, occupant);
            matches.Mix(m);
            matched += m;
            if (m && !matchedId)
                matchedId = id;
        }
    matches.Mix(static_cast<uint32_t>(occupants.size()));
    // A fresh script system from the factory, held by one reference.
    using Factory = char*(__cdecl*)();
    char* const made = reinterpret_cast<Factory>(addr::cSC4AutomataScriptSystem_Create)();
    uint32_t flags = 0;
    if (made) {
        Virtual<uint32_t>(made, 0x04);
        void* out = nullptr;
        const uint8_t qi = Virtual<uint8_t>(made, 0x00, ss::kScriptSystemIid, &out);
        if (qi && out)
            Virtual<uint32_t>(out, 0x08);
        TestString nameA("opensc4 test a"), nameB("opensc4 test b"), nameC("opensc4 test c");
        char* const a =
            Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameA), 0x7A5A0001u, 1u);
        char* const b = Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameB), 0u, 0u);
        char* const c =
            Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameC), 0x7A5A0003u, 0u);
        char* const again =
            Virtual<char*>(made, ss::slot::kCreateOccupantGroup, static_cast<void*>(&nameA), 0x7A5A0009u, 1u);
        flags = qi | (a ? 1u : 0u) << 1 | (b ? 1u : 0u) << 2 | (c ? 1u : 0u) << 3 | (again ? 1u : 0u) << 4;
        if (a && b && c) {
            At<char*>(c, ss::group::kParent) = a;
            At<uint32_t>(a, ss::group::kClass) = 0x12345u;
            At<float>(c, ss::group::kPercentage) = 0.5f;
            fresh.Mix(Virtual<char*>(made, 0x30, static_cast<void*>(&nameA)) == a ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x30, static_cast<void*>(&nameB)) == b ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x1C, static_cast<void*>(&nameA)) == a ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x1C, static_cast<void*>(&nameC)) ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x2C, 0x7A5A0003u) == c ? 1u : 0u);
            fresh.Mix(Virtual<char*>(made, 0x18, 0x7A5A0001u) == a ? 1u : 0u);
            fresh.Mix(Virtual<uint32_t>(made, 0x2C, 0x7A5A0009u));
            fresh.Mix(Virtual<uint32_t>(made, 0x38, 5u));
            char* const head = At<char*>(made, ss::field::kSpecialGroups);
            fresh.Mix(At<char*>(head, 0) != head && At<char*>(At<char*>(head, 0), 8) == b ? 1u : 0u);
            fresh.Mix(GroupHash(a));
            fresh.Mix(GroupHash(b));
            fresh.Mix(GroupHash(c));
            // Matches of the third group (50 percent) with the ID of a city
            // group that matched above, so that it draws; its own ID back
            // after.
            if (matchedId) {
                At<uint32_t>(c, ss::group::kId) = matchedId;
                for (const auto& [hash, occupant] : occupants)
                    fresh.Mix(Virtual<uint8_t>(c, ss::slot::kGroupMatches, occupant));
                At<uint32_t>(c, ss::group::kId) = 0x7A5A0003u;
            }
        }
        using Life = void(__thiscall*)(void*);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseTemplates)(made);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseGroups)(made);
        char* const head = At<char*>(made, ss::field::kSpecialGroups);
        fresh.Mix(At<char*>(head, 0) == head ? 1u : 0u);
        fresh.Mix(At<uint32_t>(made + ss::field::kGroupsByName, 0x10) |
                  At<uint32_t>(made + ss::field::kGroupsById, 0x10));
        fresh.Mix(Virtual<uint32_t>(made, 0x08)); // Release: Shutdown (not initialized), the destructor body
    }
    fresh.Mix(flags);
    char text[128];
    std::snprintf(text, sizeof text, " S%08X,%08X,%08X,%08X,%08X", identity.h, maps.h, groups.h, matches.h, fresh.h);
    g_scriptText = text;
    log::Write("autotest: automata: scripts: identity %08X maps %08X (names %u automata %u ids %u drives %u "
               "templates %u) groups %08X matches %08X (%u of %u x %u) fresh %08X flags %X",
               identity.h, maps.h, named, automataCount, static_cast<unsigned>(byId.size()), drives, templates,
               groups.h, matches.h, matched, static_cast<unsigned>(occupants.size()),
               static_cast<unsigned>(byId.size()), fresh.h, flags);
}

// Part s2's drive (OPENSC4_TEST_AUTOMATA_SCRIPTS=2), see the top of the file.

// A template's own values: its kind by vtable, class (+0x18, +0x1C), +0x20,
// +0x34 and name.
uint32_t TemplateHash(char* t)
{
    Hash h;
    if (!t) {
        h.Mix(0);
        return h.h;
    }
    const std::uintptr_t vtable = At<std::uintptr_t>(t, 0);
    h.Mix(vtable == addr::cSC4AttractorTemplate_Vtable ? 1u : vtable == addr::cSC4GeneratorTemplate_Vtable ? 2u : 3u);
    h.Mix(At<uint32_t>(t, 0x18));
    h.Mix(At<uint32_t>(t, 0x1C));
    h.Mix(At<uint32_t>(t, 0x20));
    h.Mix(At<uint32_t>(t, 0x34));
    h.Mix(NameHash(At<const char*>(t, 0x08), At<const char*>(t, 0x0C)));
    return h.h;
}

constexpr std::size_t kRecord = 0x7C;

// A test vector of behaviour records (0x0050BD40), keyed so that the
// comparator is a strict weak order (the float +4 and the uint32 +0xC from
// one rank, ties included), the index at +0x28.
char* NewRecords(uint32_t count, uint32_t seed)
{
    using Allocate = void*(__cdecl*)(uint32_t);
    using Construct = void*(__thiscall*)(void*);
    char* const block =
        static_cast<char*>(reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(count * static_cast<uint32_t>(kRecord)));
    for (uint32_t i = 0; i < count; ++i) {
        char* const r = block + i * kRecord;
        reinterpret_cast<Construct>(addr::SC4BehaviourRecord_Construct)(r);
        const uint32_t rank = (i * 7919u + seed) % (count / 2 + 3);
        At<float>(r, 4) = static_cast<float>(rank) * 0.5f;
        At<uint32_t>(r, 0xC) = 1000u - rank;
        At<uint32_t>(r, 0x28) = i;
    }
    return block;
}

uint32_t RecordsHash(const char* first, uint32_t count)
{
    Hash h;
    for (uint32_t i = 0; i < count; ++i) {
        char* const r = const_cast<char*>(first) + i * kRecord;
        h.Mix(At<uint32_t>(r, 4));
        h.Mix(At<uint32_t>(r, 0xC));
        h.Mix(At<uint32_t>(r, 0x28));
        h.Mix(At<uint32_t>(r, 0x40));
    }
    return h.h;
}

void FreeRecords(char* first, char* last)
{
    uint8_t tag = 0;
    reinterpret_cast<void(__cdecl*)(char*, char*, void*)>(addr::cSC4AutomataControllerTemplate_DestroyRecords)(
        first, last, &tag);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(first);
}

void ScriptsDrive2(cISC4City* city)
{
    namespace ss = automata_scripts;
    char* const scripts = Virtual<char*>(city, 0xC0);
    if (!scripts) {
        g_scriptText += " T-";
        return;
    }
    Hash templates, helpers, records;
    // The city's templates (static after the load), summed.
    uint32_t count = 0, sum = 0;
    std::vector<std::string> names;
    ForEachScriptNode(scripts + ss::field::kTemplates, [&](char* node) {
        sum += TemplateHash(At<char*>(node, 0x10));
        ++count;
        const char* const b = At<const char*>(node, 4);
        const char* const e = At<const char*>(node, 8);
        if (b && e > b && e - b < 128)
            names.emplace_back(b, e);
    });
    templates.Mix(count);
    templates.Mix(sum);
    std::sort(names.begin(), names.end());
    std::string listed;
    for (std::size_t i = 0; i < names.size() && i < 6; ++i)
        listed += (i ? ", " : "") + names[i];
    log::Write("autotest: automata: scripts: templates %u, first names %s", count, listed.c_str());
    // On a fresh script system: CreateControllerTemplate (slot 16) for an
    // attractor and a generator, the first name again (refused), a class
    // with no template (the factory's refusal); then the templates
    // destroyed by ReleaseTemplates and the object released.
    using Factory = char*(__cdecl*)();
    char* const made = reinterpret_cast<Factory>(addr::cSC4AutomataScriptSystem_Create)();
    if (made) {
        Virtual<uint32_t>(made, 0x04);
        TestString first("opensc4 test template 1"), second("opensc4 test template 2");
        char* const a = Virtual<char*>(made, 0x40, static_cast<void*>(&first), 0x6A1628BAu);
        char* const g = Virtual<char*>(made, 0x40, static_cast<void*>(&second), 0x4A1628C5u);
        char* const again = Virtual<char*>(made, 0x40, static_cast<void*>(&first), 0x4A1628C5u);
        TestString third("opensc4 test template 3");
        char* const none = Virtual<char*>(made, 0x40, static_cast<void*>(&third), 0x12345678u);
        templates.Mix(TemplateHash(a));
        templates.Mix(TemplateHash(g));
        templates.Mix((again ? 1u : 0u) | (none ? 2u : 0u));
        templates.Mix(Virtual<char*>(made, 0x44, static_cast<void*>(&second)) == g ? 1u : 0u);
        using Life = void(__thiscall*)(void*);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseTemplates)(made);
        templates.Mix(At<uint32_t>(made + ss::field::kTemplates, 0x10));
        Virtual<uint32_t>(made, 0x08);
    }
    // The helpers on the city's groups (the ID map's, sorted by ID): the
    // cached ID list (0x0051CD90; it fills an empty cache, alike on both
    // sides) and the record lookup (0x005204D0) for every key byte into a
    // three-string record (0x0051D040, 0x0051D0D0).
    std::vector<std::pair<uint32_t, char*>> byId;
    ForEachScriptNode(scripts + ss::field::kGroupsById,
                      [&](char* node) { byId.emplace_back(At<uint32_t>(node, 4), At<char*>(node, 8)); });
    std::sort(byId.begin(), byId.end());
    using Cache = void*(__thiscall*)(void*);
    using Find = uint8_t(__thiscall*)(void*, uint32_t, void*);
    using Record3 = void*(__thiscall*)(void*);
    uint32_t found = 0;
    for (const auto& [id, g] : byId) {
        void* const part = reinterpret_cast<Cache>(addr::cSC4OccupantGroupTemplate_CacheIds)(g);
        helpers.Mix(static_cast<uint32_t>(static_cast<char*>(part) - g));
        helpers.Mix(Virtual<uint32_t>(part, 0x10));
        alignas(4) char record[0x40] = {};
        reinterpret_cast<Record3>(addr::cSC4OccupantGroupTemplate_Record3Construct)(record);
        for (uint32_t key = 0; key < 256; ++key) {
            const uint8_t hit = reinterpret_cast<Find>(addr::cSC4OccupantGroupTemplate_FindRecord)(g, key, record);
            if (!hit)
                continue;
            ++found;
            helpers.Mix(key);
            for (const std::size_t at : {std::size_t{0}, std::size_t{0x14}, std::size_t{0x28}})
                helpers.Mix(NameHash(At<const char*>(record, at + 4), At<const char*>(record, at + 8)));
            helpers.Mix(At<uint32_t>(record, 0x3C));
        }
        reinterpret_cast<Record3>(addr::cSC4OccupantGroupTemplate_Record3Destroy)(record);
    }
    helpers.Mix(found);
    // The behaviour records: std::sort (0x00522800) of 40 records (the
    // introsort's partition, median and unguarded insertion run above 16),
    // partial_sort (0x00522590: the heap functions) of 40 with a middle of
    // 20, and the full vector's insert (0x00521350) of 3 copies in the
    // middle and of one at the end.
    using Sort = void(__cdecl*)(char*, char*, uint32_t);
    using PartialSort = void(__cdecl*)(char*, char*, char*, uint32_t, uint32_t);
    char* const sorted = NewRecords(40, 11);
    reinterpret_cast<Sort>(addr::SC4BehaviourRecord_Sort)(sorted, sorted + 40 * kRecord, 0u);
    records.Mix(RecordsHash(sorted, 40));
    FreeRecords(sorted, sorted + 40 * kRecord);
    char* const partial = NewRecords(40, 5);
    reinterpret_cast<PartialSort>(addr::SC4BehaviourRecord_PartialSort)(partial, partial + 20 * kRecord,
                                                                       partial + 40 * kRecord, 0u, 0u);
    records.Mix(RecordsHash(partial, 40));
    FreeRecords(partial, partial + 40 * kRecord);
    using Insert = void(__thiscall*)(void*, char*, const char*, uint32_t, uint32_t, uint32_t);
    for (const uint32_t atEnd : {0u, 1u}) {
        char* vector[3];
        vector[0] = NewRecords(5, 3 + atEnd);
        vector[1] = vector[0] + 5 * kRecord;
        vector[2] = vector[1];
        char* const value = NewRecords(1, 9);
        At<uint32_t>(value, 0x40) = 0x1234u + atEnd;
        char* const position = atEnd ? vector[1] : vector[0] + 2 * kRecord;
        reinterpret_cast<Insert>(addr::SC4BehaviourRecord_VectorInsertOverflow)(vector, position, value, 0u,
                                                                               atEnd ? 1u : 3u, atEnd);
        const uint32_t size = static_cast<uint32_t>((vector[1] - vector[0]) / static_cast<std::ptrdiff_t>(kRecord));
        records.Mix(size);
        records.Mix(static_cast<uint32_t>((vector[2] - vector[0]) / static_cast<std::ptrdiff_t>(kRecord)));
        records.Mix(RecordsHash(vector[0], size));
        FreeRecords(vector[0], vector[1]);
        FreeRecords(value, value + kRecord);
    }
    char text[96];
    std::snprintf(text, sizeof text, " T%08X,%08X,%08X", templates.h, helpers.h, records.h);
    g_scriptText += text;
    log::Write("autotest: automata: scripts 2: templates %08X helpers %08X (%u groups, %u records found) records %08X",
               templates.h, helpers.h, static_cast<unsigned>(byId.size()), found, records.h);
}

// Part s3a's drive (OPENSC4_TEST_AUTOMATA_SCRIPTS=3), see the top of the file.

void MixIdList(Hash& h, char* head)
{
    uint32_t count = 0;
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        h.Mix(At<uint32_t>(n, 8));
    h.Mix(count);
}

void MixIdVector(Hash& h, char* vector)
{
    const uint32_t* const begin = At<const uint32_t*>(vector, 0);
    const uint32_t* const end = At<const uint32_t*>(vector, 4);
    uint32_t count = 0;
    for (const uint32_t* p = begin; p && p < end && count < 4096; ++p, ++count)
        h.Mix(*p);
    h.Mix(count);
}

// A curve's points (SC4Curve: the point vector's begin and end at +0, +4).
void MixCurve(Hash& h, char* curve)
{
    const uint32_t* const begin = At<const uint32_t*>(curve, 0);
    const uint32_t* const end = At<const uint32_t*>(curve, 4);
    uint32_t count = 0;
    for (const uint32_t* p = begin; p && p < end && count < 4096; ++p, ++count)
        h.Mix(*p);
    h.Mix(count);
}

// A behaviour record's fields (not its pointers).
uint32_t BehaviourHash(char* r)
{
    Hash h;
    for (const std::size_t at : {0x0u, 0x4u, 0x8u, 0xCu, 0x28u, 0x30u, 0x34u, 0x38u, 0x3Cu, 0x40u})
        h.Mix(At<uint32_t>(r, at));
    h.Mix(At<uint8_t>(r, 0x2C));
    for (std::size_t at = 0x44; at <= 0x49; ++at)
        h.Mix(At<uint8_t>(r, at));
    for (std::size_t at = 0x4C; at <= 0x78; at += 4)
        h.Mix(At<uint32_t>(r, at));
    // The anims (SC4Strings of 0x14 bytes) and the sound IDs.
    char* const sb = At<char*>(r, 0x10);
    char* const se = At<char*>(r, 0x14);
    uint32_t strings = 0;
    for (char* s = sb; s && s < se && strings < 256; s += 0x14, ++strings)
        h.Mix(NameHash(At<const char*>(s, 4), At<const char*>(s, 8)));
    h.Mix(strings);
    MixIdVector(h, r + 0x1C);
    return h.h;
}

// Everything the loading writes into a template.
uint32_t LoadedTemplateHash(char* t)
{
    Hash h;
    h.Mix(TemplateHash(t));
    for (const std::size_t at : {0x4Cu, 0x50u, 0x54u, 0x58u, 0x5Cu})
        h.Mix(At<uint32_t>(t, at));
    MixCurve(h, t + 0x24);
    MixIdList(h, At<char*>(t, 0x44));
    MixIdList(h, At<char*>(t, 0x48));
    MixIdVector(h, t + 0x60);
    MixIdVector(h, t + 0x6C);
    const uint32_t classId = At<uint32_t>(t, 0x18);
    if (classId == 0x6A1628BAu || classId == 0x4A1628C5u) {
        MixIdVector(h, t + 0x78);
        uint32_t names = 0;
        char* const head = At<char*>(t, 0x84);
        for (char* n = At<char*>(head, 0); n != head && names < 4096; n = At<char*>(n, 0))
            ++names;
        h.Mix(names);
    }
    if (classId == 0x6A1628BAu)
        MixCurve(h, t + 0x88);
    if (classId == 0x4A1628C5u) {
        for (std::size_t at = 0x88; at <= 0xAC; at += 4)
            h.Mix(At<uint32_t>(t, at));
        h.Mix(At<uint8_t>(t, 0xB0));
    }
    char* const rb = At<char*>(t, 0x38);
    char* const re = At<char*>(t, 0x3C);
    uint32_t records = 0;
    for (char* r = rb; r && r < re && records < 1024; r += kRecord, ++records)
        h.Mix(BehaviourHash(r));
    h.Mix(records);
    return h.h;
}

// What the loading writes into a group (not the parts filled at run time).
uint32_t LoadedGroupHash(char* g)
{
    namespace sg = automata_scripts::group;
    Hash h;
    char* const parent = At<char*>(g, sg::kParent);
    h.Mix(At<uint32_t>(g, sg::kId));
    h.Mix(At<uint32_t>(g, sg::kClass));
    h.Mix(At<uint32_t>(g, sg::kPercentage));
    h.Mix(parent ? At<uint32_t>(parent, sg::kId) : 0xFFFFFFFFu);
    h.Mix(At<uint16_t>(g, sg::kWord4C) | static_cast<uint32_t>(At<uint16_t>(g, sg::kWord4E)) << 16);
    MixIdList(h, At<char*>(g, sg::kList50));
    h.Mix(At<uint32_t>(g + sg::kNames, 0x10));
    h.Mix(At<uint32_t>(g + sg::kRecords, 0x10));
    // Part s3b's fields: the controller templates (+0x30, in order), the
    // models (+0x48, three IDs a node), the trailers (+0x54, SC4Strings at
    // +8 of 0x1C-byte nodes), the anims (+0x34, name -> value) and the key
    // effects (+0x70, byte key -> three strings and flags) summed, the
    // speeds, the drive parameter and driving blocks (raw; the allocations
    // are zeroed under determinism.zero_alloc), the image and the source
    // building.
    uint32_t count = 0;
    char* head = At<char*>(g, sg::kList30);
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        h.Mix(At<char*>(n, 8) ? TemplateHash(At<char*>(n, 8)) : 0u);
    h.Mix(count);
    count = 0;
    head = At<char*>(g, sg::kList48);
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        for (const std::size_t at : {0x8u, 0xCu, 0x10u})
            h.Mix(At<uint32_t>(n, at));
    h.Mix(count);
    count = 0;
    head = At<char*>(g, sg::kList54);
    for (char* n = At<char*>(head, 0); n != head && count < 4096; n = At<char*>(n, 0), ++count)
        h.Mix(NameHash(At<const char*>(n, 0xC), At<const char*>(n, 0x10)));
    h.Mix(count);
    uint32_t sum = 0;
    ForEachScriptNode(g + sg::kNames, [&](char* node) {
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(At<uint32_t>(node, 0x10));
        sum += e.h;
    });
    h.Mix(sum);
    sum = 0;
    ForEachScriptNode(g + sg::kRecords, [&](char* node) {
        Hash e;
        e.Mix(At<uint8_t>(node, 4));
        for (const std::size_t at : {0x8u, 0x1Cu, 0x30u})
            e.Mix(NameHash(At<const char*>(node, at + 4), At<const char*>(node, at + 8)));
        e.Mix(At<uint32_t>(node, 0x44));
        sum += e.h;
    });
    h.Mix(sum);
    for (const std::size_t at : {0x58u, 0x5Cu, 0x68u, 0x6Cu})
        h.Mix(At<uint32_t>(g, at));
    struct Block {
        std::size_t at, size;
    };
    for (const Block b : {Block{0x60, 0xA0}, Block{0x64, 0x94}}) {
        const uint32_t* const block = At<const uint32_t*>(g, b.at);
        h.Mix(block ? 1u : 0u);
        for (std::size_t i = 0; block && i < b.size / 4; ++i)
            h.Mix(block[i]);
    }
    return h.h;
}

// A script system's loaded state: the templates and groups by name (summed),
// the drive commands (summed), the track building list (in order).
uint32_t LoadedSystemHash(char* system, uint32_t& templates, uint32_t& groups, uint32_t& drives)
{
    namespace ss = automata_scripts;
    Hash h;
    uint32_t sum = 0;
    templates = 0;
    ForEachScriptNode(system + ss::field::kTemplates, [&](char* node) {
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(LoadedTemplateHash(At<char*>(node, 0x10)));
        sum += e.h;
        ++templates;
    });
    h.Mix(templates);
    h.Mix(sum);
    sum = 0;
    groups = 0;
    ForEachScriptNode(system + ss::field::kGroupsByName, [&](char* node) {
        Hash e;
        e.Mix(NameHash(At<const char*>(node, 4), At<const char*>(node, 8)));
        e.Mix(LoadedGroupHash(At<char*>(node, 0x10)));
        sum += e.h;
        ++groups;
    });
    h.Mix(groups);
    h.Mix(sum);
    sum = 0;
    drives = 0;
    ForEachScriptNode(system + ss::field::kDriveCommands, [&](char* node) {
        Hash e;
        e.Mix(At<uint32_t>(node, 4));
        e.Mix(At<uint32_t>(node, 8));
        sum += e.h;
        ++drives;
    });
    h.Mix(drives);
    h.Mix(sum);
    MixIdList(h, At<char*>(system, ss::field::kTrackGroups));
    return h.h;
}

void ScriptsDrive3(cISC4City* city)
{
    char* const scripts = Virtual<char*>(city, 0xC0);
    if (!scripts) {
        g_scriptText += " L-";
        return;
    }
    uint32_t cityTemplates = 0, cityGroups = 0, cityDrives = 0;
    const uint32_t cityHash = LoadedSystemHash(scripts, cityTemplates, cityGroups, cityDrives);
    // A fresh script system from the factory with the scripts loaded into it
    // (LoadScripts at the game's address), then its templates and groups
    // released and the object released.
    using Factory = char*(__cdecl*)();
    using Life = void(__thiscall*)(void*);
    using Load = uint8_t(__thiscall*)(void*);
    char* const made = reinterpret_cast<Factory>(addr::cSC4AutomataScriptSystem_Create)();
    uint32_t freshHash = 0, loaded = 0xFF, freshTemplates = 0, freshGroups = 0, freshDrives = 0;
    if (made) {
        Virtual<uint32_t>(made, 0x04);
        loaded = reinterpret_cast<Load>(addr::cSC4AutomataScriptSystem_LoadScripts)(made);
        freshHash = LoadedSystemHash(made, freshTemplates, freshGroups, freshDrives);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseTemplates)(made);
        reinterpret_cast<Life>(addr::cSC4AutomataScriptSystem_ReleaseGroups)(made);
        Virtual<uint32_t>(made, 0x08);
    }
    char text[96];
    std::snprintf(text, sizeof text, " L%08X,%08X,%X", cityHash, freshHash, loaded);
    g_scriptText += text;
    log::Write("autotest: automata: scripts 3: city %08X (%u templates, %u groups, %u drives) fresh %08X (%u, %u, %u) "
               "load %u",
               cityHash, cityTemplates, cityGroups, cityDrives, freshHash, freshTemplates, freshGroups, freshDrives,
               loaded);
}

// ====================================================================================
// Part aw1's drive (OPENSC4_TEST_AUTOMATA_AWMGR=1), see the top of the file.

std::string g_awText;

// The keys of a std::map's subtree (null children), in order.
void TreeKeys(char* node, std::vector<uint32_t>& keys)
{
    namespace aw = automata_awmgr;
    while (node && keys.size() < 4096) {
        TreeKeys(At<char*>(node, aw::tree::kLeft), keys);
        keys.push_back(At<uint32_t>(node, aw::tree::kKey));
        node = At<char*>(node, aw::tree::kRight);
    }
}

// The class slot 37 (GetSourceBuildingCountForAutomataGroup) passes on for
// a group ID, as the game finds it; 0xFFFFFFFF for none.
uint32_t GroupClassOf(char* manager, uint32_t id)
{
    namespace aw = automata_awmgr;
    void* const scripts = At<void*>(manager, aw::field::kScripts);
    if (!scripts)
        return 0xFFFFFFFFu;
    char* const group = Virtual<char*>(scripts, 0x18, id);
    if (!group)
        return 0xFFFFFFFFu;
    if (const uint32_t c = At<uint32_t>(group, 0x6C))
        return c;
    if (char* const parent = At<char*>(group, 0xC))
        return reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::cSC4AutomataManager_GroupClass)(parent);
    return 0;
}

// One of the city's four managers; the identity and the queries into
// `hash`, the frame- and source-dependent values into `logged`.
void AwQuery(char* manager, Hash& hash, Hash& logged, uint32_t& flags)
{
    namespace aw = automata_awmgr;
    const uint32_t classes[] = {aw::kAircraftClassId, aw::kWatercraftClassId, automata_vehmgr::kVehicleClassId,
                                automata_vehmgr::kPedestrianClassId};
    const uint32_t iids[] = {1u, aw::kManagerIid, aw::kSerializableIid, aw::kMessageIid, 0x12345678u, 0xF0000001u};
    for (const std::size_t at : {aw::field::kVtable, aw::field::kVtable4, aw::field::kSerializable, aw::field::kMessage}) {
        char* const part = manager + at;
        auto ask = [&](uint32_t iid) {
            void* out = nullptr;
            const uint32_t answer = Virtual<uint32_t>(part, aw::slot::kQueryInterface, iid, &out) & 0xFF;
            hash.Mix(answer);
            hash.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - manager) : 0xFFFFFFFFu);
            if (answer && out)
                Virtual<uint32_t>(out, aw::slot::kRelease);
        };
        for (const uint32_t iid : classes)
            ask(iid);
        for (const uint32_t iid : iids)
            ask(iid);
    }
    // The count: AddRef and Release give it back.
    const uint32_t count = At<uint32_t>(manager, aw::field::kRefCount);
    const uint32_t up = Virtual<uint32_t>(manager, aw::slot::kAddRef);
    const uint32_t down = Virtual<uint32_t>(manager, aw::slot::kRelease);
    hash.Mix(up - count);
    if (down != count)
        flags |= 1;
    logged.Mix(count);
    hash.Mix(Virtual<uint32_t>(manager + aw::field::kSerializable, aw::slot::kGetGZCLSID));
    using Get = uint32_t(__thiscall*)(void*);
    hash.Mix(reinterpret_cast<Get>(addr::cSC4AutomataManager_GetGZCLSID)(manager + aw::field::kSerializable));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsActive));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsPaused));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsDrivingMode));
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kGetMinZoom));
    hash.Mix(Virtual<uint8_t>(manager, aw::slot::kIsCameraFollowUpdating));
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(manager, aw::slot::kGetExemplarData) - manager));
    hash.Mix(static_cast<uint32_t>(Virtual<char*>(manager, aw::slot::kGetExemplarDataMap) - manager));
    using Dispatch = uint32_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    hash.Mix(reinterpret_cast<Dispatch>(addr::cSC4AutomataManager_Dispatch5)(manager, 0, 0, 0, 0, 0));
    // The exemplar vector: each record found by its own key (slot 44) and a
    // key not there.
    char* const begin = At<char*>(manager, aw::field::kExemplarsBegin);
    char* const end = At<char*>(manager, aw::field::kExemplarsEnd);
    const uint32_t records = begin ? static_cast<uint32_t>((end - begin) / aw::exemplar::kSize) : 0;
    hash.Mix(records);
    for (uint32_t i = 0; i < records && i < 512; ++i) {
        const uint32_t* const key = &At<uint32_t>(begin + i * aw::exemplar::kSize, aw::exemplar::kKey);
        hash.MixBytes(key, 12);
        char* const found = Virtual<char*>(manager, aw::slot::kFindExemplar, key);
        hash.Mix(found ? static_cast<uint32_t>((found - begin) / aw::exemplar::kSize) : 0xFFFFFFFFu);
    }
    const uint32_t none[3] = {0x7A5A0001u, 0x7A5A0002u, 0x7A5A0003u};
    hash.Mix(Virtual<char*>(manager, aw::slot::kFindExemplar, static_cast<const uint32_t*>(none)) ? 1u : 0u);
    // The exemplar map: every key (slot 30) and keys not there.
    std::vector<uint32_t> keys;
    char* const header = At<char*>(manager, aw::field::kExemplarMap);
    TreeKeys(At<char*>(header, aw::tree::kParent), keys);
    hash.Mix(At<uint32_t>(manager, aw::field::kExemplarMapSize));
    hash.Mix(static_cast<uint32_t>(keys.size()));
    for (const uint32_t key : keys) {
        hash.Mix(key);
        hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, key));
        hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, key ^ 0x7A5A0000u));
    }
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, 0u));
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kCanCreateGroup, 0xFFFFFFFFu));
    // The source building counts: every key of the table (slot 36, sorted)
    // and keys not there; slot 37 over groups 0x4000..0x4240. Counts that
    // come from the object at 0x00B43D28 (0x1402 and 0x1406) go to the log.
    std::vector<std::pair<uint32_t, uint32_t>> counts;
    char* const t = manager + aw::field::kSourceCounts;
    for (char** b = At<char**>(t, aw::table::kBegin); b && b != At<char**>(t, aw::table::kEnd); ++b)
        for (char* node = *b; node && counts.size() < 4096; node = At<char*>(node, 0))
            counts.emplace_back(At<uint32_t>(node, 4), At<uint32_t>(node, 8));
    std::sort(counts.begin(), counts.end());
    hash.Mix(static_cast<uint32_t>(counts.size()));
    for (const auto& [key, value] : counts) {
        hash.Mix(key);
        hash.Mix(value);
        if (key != 0x1402 && key != 0x1406)
            hash.Mix(Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCount, key));
    }
    hash.Mix(Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCount, 0x7A5A0001u));
    for (const uint32_t special : {0x1402u, 0x1406u})
        logged.Mix(Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCount, special));
    for (uint32_t id = 0x4000; id <= 0x4240; ++id) {
        const uint32_t c = GroupClassOf(manager, id);
        const uint32_t answer = Virtual<uint32_t>(manager, aw::slot::kGetSourceBuildingCountForGroup, id);
        if (c == 0x1402 || c == 0x1406) {
            logged.Mix(id);
            logged.Mix(answer);
        } else if (answer || c != 0xFFFFFFFFu) {
            hash.Mix(id);
            hash.Mix(c);
            hash.Mix(answer);
        }
    }
    logged.Mix(At<uint32_t>(manager, aw::field::kCount));
}

void AwManagerDrive(cISC4City* city)
{
    namespace aw = automata_awmgr;
    std::string text = " A";
    Hash logged;
    uint32_t flags = 0;
    const std::size_t slots[] = {aw::kCitySlotAircraft, aw::kCitySlotWatercraft, automata_vehmgr::kCitySlotVehicles,
                                 automata_vehmgr::kCitySlotPedestrians};
    const char* const names[] = {"aircraft", "watercraft", "vehicle", "pedestrian"};
    for (int m = 0; m < 4; ++m) {
        char* const manager = Virtual<char*>(city, slots[m]);
        char part[16];
        if (!manager) {
            text += m ? ",-" : "-";
            continue;
        }
        Hash hash;
        AwQuery(manager, hash, logged, flags);
        std::snprintf(part, sizeof part, "%s%08X", m ? "," : "", hash.h);
        text += part;
        log::Write("autotest: automata: %s manager (aw1): %08X refs %u automata %u exemplars %u map %u counts %u",
                   names[m], hash.h, At<uint32_t>(manager, aw::field::kRefCount), At<uint32_t>(manager, aw::field::kCount),
                   static_cast<unsigned>((At<char*>(manager, aw::field::kExemplarsEnd) -
                                          At<char*>(manager, aw::field::kExemplarsBegin)) /
                                         aw::exemplar::kSize),
                   At<uint32_t>(manager, aw::field::kExemplarMapSize),
                   At<uint32_t>(manager + aw::field::kSourceCounts, aw::table::kCount));
    }
    // New managers from the two factories, held by one reference, with
    // nodes of the test's own in their containers so that the destructors'
    // clears walk them; then released (Shutdown, not initialized, and the
    // deleting destructor). And a bare common base built and destroyed by
    // its slot 38.
    Hash fresh;
    using Factory = void*(__cdecl*)();
    using Alloc = void*(__cdecl*)(uint32_t);
    const auto allocate = reinterpret_cast<Alloc>(addr::SC4_stl_allocate);
    for (int m = 0; m < 2; ++m) {
        void* const serial =
            reinterpret_cast<Factory>(m ? addr::cSC4WatercraftManager_Create : addr::cSC4AircraftManager_Create)();
        if (!serial) {
            flags |= 2;
            continue;
        }
        char* const made = static_cast<char*>(serial) - aw::field::kSerializable;
        fresh.Mix(Virtual<uint32_t>(made, aw::slot::kAddRef));
        Hash ignored;
        AwQuery(made, fresh, ignored, flags);
        fresh.Mix(At<uint32_t>(made, aw::field::kGroup));
        fresh.Mix(At<uint32_t>(made, aw::field::kAutomatonClass));
        fresh.Mix(At<uint32_t>(made, aw::field::kA40));
        Virtual<void>(made, aw::slot::kPause);
        fresh.Mix(Virtual<uint8_t>(made, aw::slot::kIsPaused));
        Virtual<void>(made, aw::slot::kResume);
        fresh.Mix(Virtual<uint8_t>(made, aw::slot::kIsPaused));
        if (m == 0) {
            // Three map nodes (a root with two children; nothing in them
            // is referenced) for the map clear and its erase.
            char* const header = At<char*>(made, aw::field::kAirMap);
            char* nodes[3];
            for (char*& n : nodes) {
                n = static_cast<char*>(allocate(0x18));
                std::memset(n, 0, 0x18);
            }
            At<char*>(nodes[0], aw::tree::kParent) = header;
            At<char*>(nodes[0], aw::tree::kLeft) = nodes[1];
            At<char*>(nodes[0], aw::tree::kRight) = nodes[2];
            At<char*>(nodes[1], aw::tree::kParent) = nodes[0];
            At<char*>(nodes[2], aw::tree::kParent) = nodes[0];
            At<char*>(header, aw::tree::kParent) = nodes[0];
            At<char*>(header, aw::tree::kLeft) = nodes[1];
            At<char*>(header, aw::tree::kRight) = nodes[2];
            At<uint32_t>(made, aw::field::kAirMapSize) = 3;
        } else {
            // Two nodes in the first bucket of +0xAA0 and one in +0xA8C,
            // nothing referenced, for the two table clears.
            for (const std::size_t at : {aw::field::kWaterTable2, aw::field::kWaterTable}) {
                char* const tb = made + at;
                char** const buckets = At<char**>(tb, aw::table::kBegin);
                if (!buckets || buckets == At<char**>(tb, aw::table::kEnd)) {
                    flags |= 4;
                    continue;
                }
                const int n = at == aw::field::kWaterTable2 ? 2 : 1;
                for (int i = 0; i < n; ++i) {
                    const uint32_t size = at == aw::field::kWaterTable2 ? 0xC : 0x20;
                    char* const node = static_cast<char*>(allocate(size));
                    std::memset(node, 0, size);
                    At<char*>(node, 0) = buckets[0];
                    buckets[0] = node;
                    ++At<uint32_t>(tb, aw::table::kCount);
                }
            }
        }
        fresh.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
    }
    {
        using Op = void*(__cdecl*)(std::size_t);
        char* const bare = static_cast<char*>(reinterpret_cast<Op>(addr::SC4_operator_new)(0xA88));
        if (bare) {
            using Ctor = char*(__thiscall*)(void*);
            using Dtor = char*(__thiscall*)(void*, uint32_t);
            char* const self = reinterpret_cast<Ctor>(addr::cSC4AutomataManager_Construct)(bare);
            fresh.Mix(self == bare ? 1u : 0u);
            fresh.Mix(static_cast<uint32_t>(*reinterpret_cast<std::uintptr_t*>(self) == addr::cSC4AutomataManager_Vtable));
            fresh.Mix(Virtual<uint8_t>(self, aw::slot::kIsActive));
            fresh.Mix(Virtual<uint32_t>(self + aw::field::kSerializable, aw::slot::kGetGZCLSID));
            fresh.Mix(reinterpret_cast<Dtor>(addr::cSC4AutomataManager_DeletingDestructor)(self, 1) == bare ? 1u : 0u);
        }
    }
    char part[48];
    std::snprintf(part, sizeof part, ",%08X,%X", fresh.h, flags);
    text += part;
    g_awText = text;
    log::Write("autotest: automata: aw1%s logged %08X", text.c_str(), logged.h);
}

// ====================================================================================
// Part aw2's drive (OPENSC4_TEST_AUTOMATA_AWMGR=2), see the top of the file.

std::string g_aw2Text;

// A message object of the test's own: slot 0x10 the type, slot 0x24 a
// value, every other slot (up to 0x84) 0.
struct FakeMessage {
    void** vtable;
    uint32_t type;
    uint32_t value;
};
uint32_t __fastcall FakeZero(void*, void*) { return 0; }
uint32_t __fastcall FakeType(FakeMessage* m, void*) { return m->type; }
uint32_t __fastcall FakeValue(FakeMessage* m, void*) { return m->value; }
void** FakeMessageVtable()
{
    static void* table[34];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&FakeZero);
        table[0x10 / 4] = reinterpret_cast<void*>(&FakeType);
        table[0x24 / 4] = reinterpret_cast<void*>(&FakeValue);
    }
    return table;
}

// A cIGZString of the test's own: slot 5 (ToChar) the text.
struct FakeString {
    void** vtable;
    const char* text;
};
const char* __fastcall FakeToChar(FakeString* s, void*) { return s->text; }
void** FakeStringVtable()
{
    static void* table[8];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&FakeZero);
        table[5] = reinterpret_cast<void*>(&FakeToChar);
    }
    return table;
}

void AwLifeDrive(cISC4City* city)
{
    namespace aw = automata_awmgr;
    std::string text = " B";
    Hash logged, fresh, cheats;
    uint32_t flags = 0;
    using Io = uint32_t(__thiscall*)(void*, void*);
    const auto write = reinterpret_cast<Io>(addr::cSC4AutomataManager_Write);
    const auto read = reinterpret_cast<Io>(addr::cSC4AutomataManager_Read);
    const std::size_t slots[] = {aw::kCitySlotAircraft, aw::kCitySlotWatercraft, automata_vehmgr::kCitySlotVehicles,
                                 automata_vehmgr::kCitySlotPedestrians};
    std::vector<uint8_t> sample;
    for (int m = 0; m < 4; ++m) {
        char* const manager = Virtual<char*>(city, slots[m]);
        char part[16];
        if (!manager) {
            text += m ? ",-" : "-";
            continue;
        }
        // The common base's Write: the version, the most, the two bools and
        // the table go into the token; the count (bytes 8..11) follows frames.
        ByteOStream out;
        Hash hash;
        hash.Mix(write(manager + aw::field::kSerializable, &out) & 0xFF);
        hash.Mix(static_cast<uint32_t>(out.bytes.size()));
        if (out.bytes.size() >= 12) {
            hash.MixBytes(out.bytes.data(), 8);
            hash.MixBytes(out.bytes.data() + 12, out.bytes.size() - 12);
            logged.Mix(U32(out.bytes, 8));
        }
        if (m == 0)
            sample = out.bytes;
        // TrackAutomataGroupInstances for the first two groups of the
        // exemplar map that are not tracked: AL into the token, the set's
        // size to the log, then untracked again (the references given back).
        std::vector<uint32_t> keys;
        TreeKeys(At<char*>(At<char*>(manager, aw::field::kExemplarMap), aw::tree::kParent), keys);
        int tried = 0;
        for (const uint32_t key : keys) {
            if (tried == 2)
                break;
            struct {
                char* node;
                void* table;
            } it = {};
            uint32_t k = key;
            using Find = void(__thiscall*)(void*, void*, const uint32_t*);
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(manager + aw::field::kGroupInstances, &it, &k);
            if (it.node)
                continue;
            ++tried;
            hash.Mix(key);
            hash.Mix(Virtual<uint8_t>(manager, 0x7C, key, 1u));
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(manager + aw::field::kGroupInstances, &it, &k);
            logged.Mix(it.node ? At<uint32_t>(it.node + 8, aw::table::kCount) : 0xFFFFFFFFu);
            hash.Mix(Virtual<uint8_t>(manager, 0x7C, key, 0u));
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(manager + aw::field::kGroupInstances, &it, &k);
            hash.Mix(it.node ? 1u : 0u);
        }
        hash.Mix(Virtual<uint8_t>(manager, 0x7C, 0x7A5A0001u, 1u)); // a group the map does not hold
        std::snprintf(part, sizeof part, "%s%08X", m ? "," : "", hash.h);
        text += part;
    }
    // A new aircraft manager, held by one reference: the round trip of the
    // first city manager's bytes (its count zeroed: the references read
    // back as nothing), reads of versions 7, 4 and 5, SetMaxCount, Activate,
    // ReleaseAutomatonSet on a set of the test's own, messages, then the
    // release.
    using Factory = void*(__cdecl*)();
    if (void* const serial = reinterpret_cast<Factory>(addr::cSC4AircraftManager_Create)()) {
        char* const made = static_cast<char*>(serial) - aw::field::kSerializable;
        Virtual<uint32_t>(made, aw::slot::kAddRef);
        if (sample.size() >= 12) {
            std::vector<uint8_t> copy = sample;
            Put32(copy, 8, 0);
            ByteIStream in(copy);
            fresh.Mix(read(serial, &in) & 0xFF);
            ByteOStream again;
            write(serial, &again);
            if (again.bytes != copy)
                flags |= 1;
            fresh.Mix(At<uint8_t>(made, 0xA78));
            fresh.Mix(At<uint8_t>(made, 0x10));
            for (const uint16_t version : {uint16_t{7}, uint16_t{4}, uint16_t{5}}) {
                std::vector<uint8_t> v = copy;
                Put16(v, 0, version);
                if (version == 5)
                    v.resize(12 + 4), Put32(v, 12, 0); // no table records
                ByteIStream vin(v);
                fresh.Mix(read(serial, &vin) & 0xFF);
                fresh.Mix(At<uint8_t>(made, 0xA78));
                fresh.Mix(At<uint32_t>(made + aw::field::kSourceCounts, aw::table::kCount));
            }
        } else {
            flags |= 2;
        }
        for (const int32_t most : {-5, 30, 30, 9000}) {
            Virtual<void>(made, 0x5C, most);
            fresh.Mix(At<uint32_t>(made, aw::field::kMaxCount));
            fresh.Mix(At<uint32_t>(made, aw::field::kShare));
        }
        for (const uint32_t on : {0u, 1u})
            fresh.Mix(Virtual<uint8_t>(made, 0x14, on));
        {
            alignas(4) char set[aw::table::kSize] = {};
            using Ctor = void*(__thiscall*)(void*, uint32_t);
            reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(set, 0x64);
            Virtual<void>(made, 0x84, static_cast<void*>(set));
            fresh.Mix(At<uint32_t>(set, aw::table::kCount));
            using Free = void(__cdecl*)(void*);
            if (void* const buckets = At<void*>(set, aw::table::kBegin))
                reinterpret_cast<Free>(addr::SC4_Free)(buckets);
        }
        // Messages to the message target (+0xC).
        FakeMessage message{FakeMessageVtable(), 0, 0};
        auto send = [&](uint32_t type, uint32_t value) {
            message.type = type;
            message.value = value;
            fresh.Mix(Virtual<uint8_t>(made + aw::field::kMessage, 0x0C, static_cast<void*>(&message)));
            fresh.Mix(At<uint8_t>(made, aw::field::kA3D));
            fresh.Mix(At<uint8_t>(made, 0xA4C));
        };
        send(0x4A7FB807, 1);
        send(0x4A7FB807, 0);
        send(0xCAD61E00, 0);
        send(0xA6B79612, 0); // held: nothing
        send(0xEAD61E16, 0);
        send(0xA6B79602, 0);
        send(0xA6B79612, 0);
        send(0xC9F775BB, 0);
        send(0x06CDB65A, 0);
        send(0x89D3E9F3, 1); // no object: nothing
        send(0xEA298D50, 0);
        send(0xEBFF18A3, 0); // no object: nothing
        send(0x12345678, 0);
        logged.MixBytes(made + 0xA30, 12);
        fresh.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
    } else {
        flags |= 4;
    }
    // The group instance table's insert and erase on a table of the test's
    // own: two keys in one bucket, the older (second in the chain) erased
    // first, which goes through 0x00510450. And the exemplar record copy
    // 0x00512C80 / 0x005120E0 on two records of the test's own (no string).
    {
        using Ctor = void*(__thiscall*)(void*, uint32_t);
        using Insert = void(__thiscall*)(void*, void*, const void*);
        using Erase = void(__thiscall*)(void*, void*);
        using Clear = void(__thiscall*)(void*);
        using Free = void(__cdecl*)(void*);
        alignas(4) char table[aw::table::kSize] = {};
        reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(table, 0x64);
        const uint32_t buckets = static_cast<uint32_t>(At<char**>(table, aw::table::kEnd) -
                                                       At<char**>(table, aw::table::kBegin));
        void* nodes[2] = {};
        for (int i = 0; i < 2 && buckets; ++i) {
            alignas(4) char pair[4 + aw::table::kSize] = {};
            At<uint32_t>(pair, 0) = 7 + static_cast<uint32_t>(i) * buckets;
            reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(pair + 4, 0x64);
            alignas(4) char result[12] = {};
            reinterpret_cast<Insert>(addr::cSC4AutomataManager_InsertInstances)(table, result, pair);
            nodes[i] = At<void*>(result, 0);
            fresh.Mix(At<uint8_t>(result, 8));
            reinterpret_cast<Clear>(addr::SC4Uint32HashSet_Clear)(pair + 4);
            if (void* const b = At<void*>(pair + 4, aw::table::kBegin))
                reinterpret_cast<Free>(addr::SC4_Free)(b);
        }
        fresh.Mix(At<uint32_t>(table, aw::table::kCount));
        for (int i = 0; i < 2; ++i) {
            void* iterator[2] = {nodes[i], table};
            reinterpret_cast<Erase>(addr::cSC4AutomataManager_EraseInstances)(table, iterator);
            fresh.Mix(At<uint32_t>(table, aw::table::kCount));
        }
        if (void* const b = At<void*>(table, aw::table::kBegin))
            reinterpret_cast<Free>(addr::SC4_Free)(b);
        alignas(4) char records[2][aw::exemplar::kSize] = {};
        for (std::size_t at = 0; at < aw::exemplar::kSize; at += 4)
            if (at < 0x1C || at >= 0x30)
                At<uint32_t>(records[0], at) = 0x01010101u * static_cast<uint32_t>(at);
        // Their strings (+0x1C: a cIGZString, the buffer at +0x20) made by
        // SC4String::FromText, as the game's records have them.
        using FromText = void*(__thiscall*)(void*, const char*);
        reinterpret_cast<FromText>(addr::SC4String_FromText)(records[0] + 0x1C, "copied");
        reinterpret_cast<FromText>(addr::SC4String_FromText)(records[1] + 0x1C, "");
        using Copy = char*(__cdecl*)(char*, char*, char*, void*, uint32_t);
        char tag = 0;
        char* const end = reinterpret_cast<Copy>(addr::cSC4AutomataManager_CopyExemplars)(
            records[0], records[0] + aw::exemplar::kSize, records[1], &tag, 0);
        fresh.Mix(end == records[1] + aw::exemplar::kSize ? 1u : 0u);
        fresh.MixBytes(records[1], 0x1C);
        fresh.MixBytes(records[1] + 0x30, aw::exemplar::kSize - 0x30);
        const char* const copied = At<const char*>(records[1], 0x20);
        const char* const copiedEnd = At<const char*>(records[1], 0x24);
        if (copied && copiedEnd > copied && copiedEnd - copied < 64)
            fresh.MixBytes(copied, static_cast<std::size_t>(copiedEnd - copied));
        for (auto& record : records)
            if (void* const b = At<void*>(record, 0x20))
                reinterpret_cast<Free>(addr::SC4_Free)(b);
    }
    // The cheat target (the city's aircraft manager's +4 part; its this is
    // not read): another message, another cheat, 0xCC478B79 with and without
    // the word it compares (0x00B09968 put back after), and the route line's
    // 0xE97FCBC8 (shown, count 7) and 0xE97FCBC9 (taken away).
    if (char* const manager = Virtual<char*>(city, aw::kCitySlotAircraft)) {
        using Cheat = uint32_t(__thiscall*)(void*, uint32_t*);
        const auto cheat = reinterpret_cast<Cheat>(addr::cSC4AutomataManager_DoMessage4);
        void* const target = manager + aw::field::kVtable4;
        static std::string line;
        FakeString string{FakeStringVtable(), ""};
        uint32_t message[3] = {0x12345678u, 0xCC478B79u, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&string))};
        cheats.Mix(cheat(target, message) & 0xFF);
        message[0] = 0x230E27ACu;
        message[1] = 0x7A5A0001u;
        cheats.Mix(cheat(target, message) & 0xFF);
        const uint8_t parked = *reinterpret_cast<uint8_t*>(0x00B09968);
        message[1] = 0xCC478B79u;
        for (int i = 0; i < 3; ++i) {
            line = i == 0 ? "cheat" : i == 1 ? std::string("cheat ") + reinterpret_cast<const char*>(0x00A82B94)
                                              : "cheat other";
            string.text = line.c_str();
            cheats.Mix(cheat(target, message) & 0xFF);
            cheats.Mix(*reinterpret_cast<uint8_t*>(0x00B09968));
        }
        *reinterpret_cast<uint8_t*>(0x00B09968) = parked;
        void*& route = *reinterpret_cast<void**>(0x00B21BA0);
        if (!route) {
            line = "route 7";
            string.text = line.c_str();
            message[1] = 0xE97FCBC8u;
            cheats.Mix(cheat(target, message) & 0xFF);
            cheats.Mix(route ? At<uint32_t>(route, 0x18) : 0xFFFFFFFEu);
            cheats.Mix(route ? At<uint8_t>(route, 0xC) : 0xFFu);
            message[1] = 0xE97FCBC9u;
            cheats.Mix(cheat(target, message) & 0xFF);
            cheats.Mix(route ? 1u : 0u);
        } else {
            flags |= 8;
        }
    }
    char part[64];
    std::snprintf(part, sizeof part, ",%08X,%08X,%X", fresh.h, cheats.h, flags);
    text += part;
    g_aw2Text = text;
    log::Write("autotest: automata: aw2%s logged %08X", text.c_str(), logged.h);
}

// ====================================================================================
// Part aw3's drive (OPENSC4_TEST_AUTOMATA_AWMGR=3), see the top of the file.

std::string g_aw3Text;

// A fake automaton of the test's own: a 0x200-byte block with the parts the
// common base calls (+0 the automaton, +4 its occupant, +0x20, +0x18C), the
// byte +0x33 and its state at +0x1F0. Every slot the base calls on it goes
// into the drive's hash (the slot, its part and its value arguments; a
// pointer argument as the fake it is, else as a pointer), and it answers
// from its state. A slot it does not know sets a bit of `unexpected`.
struct FakeState {
    Hash* calls = nullptr;
    uint32_t id = 0;
    uint32_t refs = 0;
    uint32_t flags = 0;             // slots 30 / 31 (0x78 / 0x7C)
    uint32_t groups[2] = {};        // the occupant's IsOccupantGroup
    float position[3] = {};         // slot 7 and the occupant's GetPosition
    float heading[3] = {};          // slot 15
    uint32_t occupantFlags = 0;     // the occupant's GetFlags (slot 33)
    uint32_t type = 0x7A5A0F00;     // the occupant's GetType
    uint8_t priority = 0;           // slot 63
    uint32_t answer52 = 1, answer57a = 0, answer57b = 0, answer70 = 1, answer72 = 1, answer85 = 0, answer101 = 1;
    uint32_t answerOccupant5 = 0;   // the occupant's slot 5
    double value32 = 0.0;           // slot 32 (ST0)
    char* controller = nullptr;     // slot 56
    char* messageTarget = nullptr;  // QI 0x452294AA
    void* holder = nullptr;         // the occupant's slot 6 (null: FakeHolder; part aw4)
    uint32_t kind[8] = {};          // slot 67's block (+0x10 the kind; part aw4)
    void* dock = nullptr;           // the occupant's QI 0xE9793A65 (part aw5)
    uint32_t unexpected = 0;
};

constexpr std::size_t kFakeSize = 0x200, kFakeState = 0x1F0;

std::vector<std::pair<const void*, uint32_t>> g_known; // pointers the hash names

uint32_t Canon(uint32_t value)
{
    for (const auto& [p, id] : g_known)
        if (reinterpret_cast<uint32_t>(p) == value)
            return 0xC0DE0000u + id;
    return value ? 0xFFFFFFF0u : 0u;
}

// Fakes whose state is not in their block (part aw5's boats, whose fields
// cover +0x1F0).
std::vector<std::pair<const char*, FakeState*>> g_fakeStates;

FakeState* StateOf(char* base)
{
    for (const auto& [b, s] : g_fakeStates)
        if (b == base)
            return s;
    return At<FakeState*>(base, kFakeState);
}

// The fake occupant's property holder (what the automata filter of
// 0x00B43DC4 asks for): no property (slots 3 and 9 answer 0).
uint32_t __fastcall HolderNone(void*, void*, uint32_t) { return 0; }
void* FakeHolder()
{
    static void* table[16];
    static void* holder[1] = {table};
    if (!table[0])
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&HolderNone);
    return holder;
}

uint32_t Answer(char* base, std::size_t part, std::size_t slot, const uint32_t* args, int n, uint32_t pointers)
{
    FakeState* const s = StateOf(base);
    Hash& h = *s->calls;
    h.Mix(0xFA000000u | s->id << 16 | static_cast<uint32_t>(part) << 8 | static_cast<uint32_t>(slot / 4));
    for (int i = 0; i < n; ++i)
        h.Mix(pointers >> i & 1 ? Canon(args[i]) : args[i]);
    auto write3 = [](uint32_t to, const float* from) {
        if (to)
            std::memcpy(reinterpret_cast<void*>(to), from, 12);
    };
    if (part == 0) {
        switch (slot) {
        case 0x00:
            if (args[0] == 0x452294AAu && s->messageTarget) {
                *reinterpret_cast<void**>(args[1]) = s->messageTarget;
                ++StateOf(s->messageTarget)->refs;
                return 1;
            }
            return 0;
        case 0x04: return ++s->refs;
        case 0x08: return --s->refs;
        case 0x14: return reinterpret_cast<uint32_t>(base + 4);
        case 0x1C: write3(args[0], s->position); return 1;
        case 0x28: // part aw4: the point it is sent to, kept as its position
            if (args[0])
                std::memcpy(s->position, reinterpret_cast<const void*>(args[0]), 12);
            return 1;
        case 0x3C: write3(args[0], s->heading); return 1;
        case 0x78:
            if (args[1] & 0xFF)
                s->flags |= args[0];
            else
                s->flags &= ~args[0];
            return s->flags;
        case 0x7C: return (s->flags & args[0]) ? 1u : 0u;
        case 0xD0: return s->answer52;
        case 0xE0: return reinterpret_cast<uint32_t>(s->controller);
        case 0xE4: return args[0] == 0x10 ? s->answer57a : s->answer57b;
        case 0xFC: return s->priority;
        case 0x10C: return reinterpret_cast<uint32_t>(s->kind);
        case 0x118: return s->answer70;
        case 0x120: return s->answer72;
        case 0x154: return s->answer85;
        case 0x194: return s->answer101;
        default: return 0;
        }
    }
    if (part == 4) {
        switch (slot) {
        case 0x04: return ++s->refs;
        case 0x08: return --s->refs;
        case 0x14: return s->answerOccupant5;
        case 0x1C: return s->type;
        case 0x20: write3(args[0], s->position); return 1;
        case 0x18: return reinterpret_cast<uint32_t>(s->holder ? s->holder : FakeHolder());
        case 0x58: return (args[0] && (args[0] == s->groups[0] || args[0] == s->groups[1])) ? 1u : 0u;
        case 0x84: return s->occupantFlags;
        case 0x00: // part aw5: the dock interface, referenced
            if (args[0] == 0xE9793A65u && s->dock && args[1]) {
                *reinterpret_cast<void**>(args[1]) = s->dock;
                Virtual<uint32_t>(s->dock, 0x04);
                return 1;
            }
            return 0;
        default: return 0;
        }
    }
    return 0;
}

template <std::size_t P, std::size_t S>
uint32_t __fastcall FakeTrap(char* part, void*)
{
    FakeState* const s = StateOf(part - P);
    s->unexpected |= 1u << (S / 4 % 32);
    s->calls->Mix(0xDEAD0000u | static_cast<uint32_t>(P) << 8 | static_cast<uint32_t>(S / 4));
    return 0;
}
template <std::size_t P, std::size_t S, uint32_t Ptr = 0>
uint32_t __fastcall Fake0(char* part, void*)
{
    return Answer(part - P, P, S, nullptr, 0, Ptr);
}
template <std::size_t P, std::size_t S, uint32_t Ptr = 0>
uint32_t __fastcall Fake1(char* part, void*, uint32_t a)
{
    const uint32_t v[] = {a};
    return Answer(part - P, P, S, v, 1, Ptr);
}
template <std::size_t P, std::size_t S, uint32_t Ptr = 0>
uint32_t __fastcall Fake2(char* part, void*, uint32_t a, uint32_t b)
{
    const uint32_t v[] = {a, b};
    return Answer(part - P, P, S, v, 2, Ptr);
}
template <std::size_t P, std::size_t S>
uint32_t __fastcall Fake7(char* part, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                          uint32_t g)
{
    const uint32_t v[] = {a, b, c, d, e, f, g};
    return Answer(part - P, P, S, v, 7, 0);
}
template <std::size_t P, std::size_t S>
double __fastcall FakeDouble(char* part, void*)
{
    Answer(part - P, P, S, nullptr, 0, 0);
    return StateOf(part - P)->value32;
}

template <std::size_t P, std::size_t... I>
void FillTraps(void** table, std::index_sequence<I...>)
{
    ((table[I] = reinterpret_cast<void*>(&FakeTrap<P, I * 4>)), ...);
}

#define OPENSC4_FAKE(table, part, slot, fn) table[(slot) / 4] = reinterpret_cast<void*>(&fn<part, slot>)
#define OPENSC4_FAKEP(table, part, slot, fn, ptr) table[(slot) / 4] = reinterpret_cast<void*>(&fn<part, slot, ptr>)

void** FakeAutomatonVtable()
{
    static void* table[0x1A0 / 4];
    if (!table[0]) {
        FillTraps<0>(table, std::make_index_sequence<0x1A0 / 4>{});
        OPENSC4_FAKEP(table, 0, 0x00, Fake2, 2u);
        OPENSC4_FAKE(table, 0, 0x04, Fake0);
        OPENSC4_FAKE(table, 0, 0x08, Fake0);
        OPENSC4_FAKE(table, 0, 0x0C, Fake0);
        OPENSC4_FAKE(table, 0, 0x10, Fake0);
        OPENSC4_FAKE(table, 0, 0x14, Fake0);
        OPENSC4_FAKEP(table, 0, 0x1C, Fake1, 1u);
        OPENSC4_FAKE(table, 0, 0x24, Fake1);
        OPENSC4_FAKE(table, 0, 0x34, Fake1);
        OPENSC4_FAKEP(table, 0, 0x3C, Fake1, 1u);
        OPENSC4_FAKE(table, 0, 0x78, Fake2);
        OPENSC4_FAKE(table, 0, 0x7C, Fake1);
        table[0x80 / 4] = reinterpret_cast<void*>(&FakeDouble<0, 0x80>);
        OPENSC4_FAKE(table, 0, 0x84, Fake2);
        OPENSC4_FAKEP(table, 0, 0x9C, Fake1, 1u);
        table[0xD0 / 4] = reinterpret_cast<void*>(&Fake7<0, 0xD0>);
        OPENSC4_FAKE(table, 0, 0xE0, Fake0);
        OPENSC4_FAKE(table, 0, 0xE4, Fake1);
        OPENSC4_FAKE(table, 0, 0xFC, Fake0);
        OPENSC4_FAKE(table, 0, 0x100, Fake1);
        OPENSC4_FAKE(table, 0, 0x118, Fake1);
        OPENSC4_FAKE(table, 0, 0x11C, Fake1);
        OPENSC4_FAKE(table, 0, 0x120, Fake2);
        OPENSC4_FAKE(table, 0, 0x128, Fake2);
        OPENSC4_FAKE(table, 0, 0x144, Fake1);
        OPENSC4_FAKE(table, 0, 0x154, Fake0);
        OPENSC4_FAKE(table, 0, 0x164, Fake1);
        OPENSC4_FAKEP(table, 0, 0x17C, Fake1, 1u);
        OPENSC4_FAKE(table, 0, 0x194, Fake0);
        OPENSC4_FAKE(table, 0, 0x198, Fake2);
    }
    return table;
}

// Part aw4's fakes: the same with slots 10 (the point it is sent to) and 67
// (the kind block). Part aw3's keep their traps there.
void** FakeAutomatonVtable4()
{
    static void* table[0x1A0 / 4];
    if (!table[0]) {
        std::memcpy(table, FakeAutomatonVtable(), sizeof table);
        OPENSC4_FAKEP(table, 0, 0x28, Fake1, 1u);
        OPENSC4_FAKE(table, 0, 0x10C, Fake0);
    }
    return table;
}

void** FakeOccupantVtable()
{
    static void* table[0x90 / 4];
    if (!table[0]) {
        FillTraps<4>(table, std::make_index_sequence<0x90 / 4>{});
        OPENSC4_FAKEP(table, 4, 0x00, Fake2, 2u);
        OPENSC4_FAKE(table, 4, 0x04, Fake0);
        OPENSC4_FAKE(table, 4, 0x08, Fake0);
        OPENSC4_FAKE(table, 4, 0x14, Fake0);
        OPENSC4_FAKE(table, 4, 0x18, Fake0);
        OPENSC4_FAKE(table, 4, 0x1C, Fake0);
        OPENSC4_FAKEP(table, 4, 0x20, Fake1, 1u);
        OPENSC4_FAKE(table, 4, 0x58, Fake1);
        OPENSC4_FAKE(table, 4, 0x84, Fake0);
    }
    return table;
}

void** FakePart20Vtable()
{
    static void* table[4];
    if (!table[0]) {
        FillTraps<0x20>(table, std::make_index_sequence<4>{});
        OPENSC4_FAKE(table, 0x20, 0x0C, Fake1);
    }
    return table;
}

void** FakePart18CVtable()
{
    static void* table[8];
    if (!table[0]) {
        FillTraps<0x18C>(table, std::make_index_sequence<8>{});
        OPENSC4_FAKE(table, 0x18C, 0x08, Fake1);
        OPENSC4_FAKE(table, 0x18C, 0x1C, Fake1);
    }
    return table;
}

#undef OPENSC4_FAKE
#undef OPENSC4_FAKEP

struct Fake {
    alignas(16) char block[kFakeSize] = {};
    FakeState state;
    char* Base() { return block; }
    void Init(uint32_t id, Hash& calls)
    {
        std::memset(block, 0, sizeof block);
        At<void**>(block, 0) = FakeAutomatonVtable();
        At<void**>(block, 4) = FakeOccupantVtable();
        At<void**>(block, 0x20) = FakePart20Vtable();
        At<void**>(block, 0x18C) = FakePart18CVtable();
        At<FakeState*>(block, kFakeState) = &state;
        state = FakeState{};
        state.calls = &calls;
        state.id = id;
        g_known.push_back({block, id});
        g_known.push_back({block + 4, id | 0x100});
    }
};

// The automata of a hash set (+4 of its nodes) as the fakes they are,
// sorted, with the set's count.
void MixSet(Hash& hash, char* set)
{
    namespace aw = automata_awmgr;
    std::vector<uint32_t> ids;
    const uint32_t buckets = static_cast<uint32_t>(At<char**>(set, aw::table::kEnd) - At<char**>(set, aw::table::kBegin));
    for (uint32_t i = 0; i < buckets && i < 4096; ++i)
        for (char* n = At<char**>(set, aw::table::kBegin)[i]; n; n = At<char*>(n, 0))
            ids.push_back(Canon(At<uint32_t>(n, 4)));
    std::sort(ids.begin(), ids.end());
    hash.Mix(At<uint32_t>(set, aw::table::kCount));
    for (const uint32_t id : ids)
        hash.Mix(id);
}

// The keys and counts of a table of counts (+0xA64), sorted by key.
void MixCounts(Hash& hash, char* t)
{
    namespace aw = automata_awmgr;
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    const uint32_t buckets = static_cast<uint32_t>(At<char**>(t, aw::table::kEnd) - At<char**>(t, aw::table::kBegin));
    for (uint32_t i = 0; i < buckets && i < 4096; ++i)
        for (char* n = At<char**>(t, aw::table::kBegin)[i]; n; n = At<char*>(n, 0))
            pairs.push_back({At<uint32_t>(n, 4), At<uint32_t>(n, 8)});
    std::sort(pairs.begin(), pairs.end());
    hash.Mix(static_cast<uint32_t>(pairs.size()));
    for (const auto& [k, v] : pairs) {
        hash.Mix(k);
        hash.Mix(v);
    }
}

// The list's automata (+0x44) as the fakes they are, in order.
void MixList(Hash& hash, char* manager)
{
    namespace aw = automata_awmgr;
    char* const head = At<char*>(manager, aw::field::kAutomata);
    hash.Mix(At<uint32_t>(manager, aw::field::kCount));
    uint32_t n = 0;
    for (char* node = At<char*>(head, 0); node != head && n < 64; node = At<char*>(node, 0), ++n)
        hash.Mix(Canon(At<uint32_t>(node, 8)));
}

void AwAutoDrive(cISC4City* city)
{
    namespace aw = automata_awmgr;
    std::string text = " D";
    Hash calls, choices, points, sources, camera, logged;
    uint32_t flags = 0;
    using Factory = void*(__cdecl*)();
    (void)static_cast<Factory>(nullptr);
    using Op = void*(__cdecl*)(std::size_t);
    using Ctor = char*(__thiscall*)(void*);
    using Dtor = char*(__thiscall*)(void*, uint32_t);
    using SetCtor = void*(__thiscall*)(void*, uint32_t);
    using SetClear = void(__thiscall*)(void*);
    using Insert = void(__thiscall*)(void*, void*, const void*);
    using FreeFn = void(__cdecl*)(void*);
    using Seed = void(__thiscall*)(void*, uint32_t);
    const auto free_ = reinterpret_cast<FreeFn>(addr::SC4_Free);
    char* const vehicles = Virtual<char*>(city, automata_vehmgr::kCitySlotVehicles);
    if (!vehicles) {
        g_aw3Text = " D-";
        return;
    }
    g_known.clear();
    // What the drive needs to know: the automata filter (it asks the fakes'
    // occupants for their groups, type and property holder), the cheat
    // object and the followed automaton (globals of the game's: with either
    // the bare base part is skipped).
    void* filter = nullptr;
    Virtual<void>(*reinterpret_cast<void**>(addr::SC4_Service43DC4), 0x48, static_cast<void*>(&filter));
    if (filter) {
        Virtual<uint32_t>(filter, 0x08);
        flags |= 1;
    }
    const bool cheatObject = *reinterpret_cast<void**>(0x00B21B98) != nullptr;
    const bool followed = *reinterpret_cast<void**>(0x00B21B8C) != nullptr;
    if (cheatObject)
        flags |= 2;
    if (followed)
        flags |= 4;

    // ---- On a bare common base (its vtable the base's), with the city's
    // vehicle manager's cells object and script system lent (no reference)
    // and fakes of the test's own.
    char* const bare = static_cast<char*>(reinterpret_cast<Op>(addr::SC4_operator_new)(0xA88));
    char* const base = bare ? reinterpret_cast<Ctor>(addr::cSC4AutomataManager_Construct)(bare) : nullptr;
    // The fakes at an address that is 0 modulo a set's bucket count: the
    // tracked groups' sets and GetInstancesOfGroup's are keyed by the fakes'
    // addresses (a key's bucket is its address modulo the count), and the
    // base calls the fakes in a set's order (the chase, slot 33's releases).
    // With the fakes where the DLL happened to be loaded, <calls> took one
    // of several values run to run, on both sides.
    uint32_t buckets = 0;
    {
        alignas(4) char probe[aw::table::kSize] = {};
        reinterpret_cast<SetCtor>(addr::SC4Uint32HashSet_Construct)(probe, 0x64);
        buckets = static_cast<uint32_t>(At<char**>(probe, aw::table::kEnd) - At<char**>(probe, aw::table::kBegin));
        if (void* const b = At<void*>(probe, aw::table::kBegin))
            free_(b);
    }
    constexpr uint32_t kMostBuckets = 1031;
    alignas(16) static char arena[5 * sizeof(Fake) + 16 * kMostBuckets];
    std::size_t offset = 0;
    if (buckets && buckets <= kMostBuckets)
        while ((reinterpret_cast<uintptr_t>(arena) + offset) % buckets)
            offset += 16;
    else
        flags |= 0x100;
    Fake* const fakes = reinterpret_cast<Fake*>(arena + offset);
    for (uint32_t i = 0; i < 5; ++i)
        new (&fakes[i]) Fake;
    log::Write("autotest: automata: aw3 fakes at %p (%u buckets, offset %X)", static_cast<void*>(fakes), buckets,
               static_cast<uint32_t>(offset));
    if (base && !cheatObject && !followed) {
        At<void*>(base, aw::field::kCells) = At<void*>(vehicles, aw::field::kCells);
        At<void*>(base, aw::field::kScripts) = At<void*>(vehicles, aw::field::kScripts);
        At<uint32_t>(base, aw::field::kGroup) = At<uint32_t>(vehicles, aw::field::kGroup);
        for (uint32_t i = 0; i < 5; ++i)
            fakes[i].Init(i + 1, calls);
        char* const f1 = fakes[0].Base();
        char* const f2 = fakes[1].Base();
        char* const f3 = fakes[2].Base();
        char* const target = fakes[3].Base();
        char* const message = fakes[4].Base();
        g_known.push_back({base, 0x40});
        constexpr uint32_t kG1 = 0x7A5A0010u, kG2 = 0x7A5A0011u, kG3 = 0x7A5A0012u;
        fakes[0].state.groups[0] = kG1;
        fakes[1].state.groups[0] = kG1;
        fakes[1].state.groups[1] = kG2;
        fakes[2].state.groups[0] = kG3;
        const float spots[4][3] = {{40.0f, 3.0f, 40.0f}, {64.0f, 5.0f, 72.0f}, {20.0f, 1.0f, 24.0f}, {48.0f, 4.0f, 44.0f}};
        for (int i = 0; i < 4; ++i)
            std::memcpy(fakes[i].state.position, spots[i], 12);
        fakes[0].state.priority = 7;
        fakes[1].state.priority = 3;
        fakes[2].state.priority = 9;
        alignas(4) static char controllerBlock[0x40];
        std::memset(controllerBlock, 0, sizeof controllerBlock);
        fakes[0].state.controller = controllerBlock;
        fakes[1].state.controller = controllerBlock;
        fakes[0].state.messageTarget = message;
        fakes[1].state.messageTarget = message;

        // Two tracked groups (empty sets of 100 buckets) in the group
        // instance table, as slot 31 makes them.
        char* const instances = base + aw::field::kGroupInstances;
        for (const uint32_t g : {kG1, kG2}) {
            alignas(4) char pair[4 + aw::table::kSize] = {};
            At<uint32_t>(pair, 0) = g;
            reinterpret_cast<SetCtor>(addr::SC4Uint32HashSet_Construct)(pair + 4, 0x64);
            using Resize = void(__thiscall*)(void*, uint32_t);
            reinterpret_cast<Resize>(addr::SC4Uint32HashSet_Resize)(instances, At<uint32_t>(instances, aw::table::kCount) + 1);
            alignas(4) char result[12] = {};
            reinterpret_cast<Insert>(addr::cSC4AutomataManager_InsertInstances)(instances, result, pair);
            reinterpret_cast<SetClear>(addr::SC4Uint32HashSet_Clear)(pair + 4);
            if (void* const b = At<void*>(pair + 4, aw::table::kBegin))
                free_(b);
        }
        auto tracked = [&](uint32_t g) -> char* {
            struct {
                char* node;
                void* table;
            } it = {};
            using Find = void(__thiscall*)(void*, void*, const uint32_t*);
            reinterpret_cast<Find>(addr::SC4Uint32HashTable_Find)(instances, &it, &g);
            return it.node ? it.node + 8 : nullptr;
        };
        auto mixTracked = [&]() {
            for (const uint32_t g : {kG1, kG2})
                if (char* const set = tracked(g))
                    MixSet(calls, set);
                else
                    calls.Mix(0xBADu);
        };
        // AddAutomaton (slot 7): none, then three; slot 59 for the third
        // once it is in a tracked group too.
        calls.Mix(Virtual<uint32_t>(base, 0x1C, static_cast<void*>(nullptr)) & 0xFF);
        for (char* f : {f1, f2, f3})
            calls.Mix(Virtual<uint32_t>(base, 0x1C, static_cast<void*>(f)) & 0xFF);
        MixList(calls, base);
        mixTracked();
        fakes[2].state.groups[1] = kG2;
        Virtual<void>(base, 0xEC, static_cast<void*>(f3));
        Virtual<void>(base, 0xEC, static_cast<void*>(f3)); // in already
        mixTracked();
        // GetInstancesOfGroup (slot 32): a tracked group, a group of the list
        // only, and one with the second fake flagged 1; each set released by
        // slot 33.
        for (int round = 0; round < 3; ++round) {
            const uint32_t g = round == 0 ? kG1 : kG3;
            if (round == 2)
                fakes[2].state.flags |= 1;
            alignas(4) char set[aw::table::kSize] = {};
            reinterpret_cast<SetCtor>(addr::SC4Uint32HashSet_Construct)(set, 0x64);
            calls.Mix(Virtual<uint32_t>(base, 0x80, g, static_cast<void*>(set)));
            MixSet(calls, set);
            Virtual<void>(base, 0x84, static_cast<void*>(set));
            calls.Mix(At<uint32_t>(set, aw::table::kCount));
            if (void* const b = At<void*>(set, aw::table::kBegin))
                free_(b);
        }
        fakes[2].state.flags &= ~1u;
        // The chase: slot 22 refusals, then slot 21 (the nearest, then all
        // within the radius), the controller's reference given back after.
        calls.Mix(Virtual<uint32_t>(base, 0x58, static_cast<void*>(nullptr), static_cast<void*>(target)) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0x58, static_cast<void*>(f1), static_cast<void*>(f1)) & 0xFF);
        fakes[2].state.answer57a = 1;
        calls.Mix(Virtual<uint32_t>(base, 0x58, static_cast<void*>(f3), static_cast<void*>(target)) & 0xFF);
        fakes[2].state.answer57a = 0;
        fakes[2].state.flags |= 0x40;
        calls.Mix(Virtual<uint32_t>(base, 0x58, static_cast<void*>(f3), static_cast<void*>(target)) & 0xFF);
        fakes[2].state.flags &= ~0x40u;
        fakes[2].state.answer52 = 0;
        calls.Mix(Virtual<uint32_t>(base, 0x58, static_cast<void*>(f3), static_cast<void*>(target)) & 0xFF);
        fakes[2].state.answer52 = 1;
        calls.Mix(Virtual<uint32_t>(base, 0x58, static_cast<void*>(f3), static_cast<void*>(target)) & 0xFF); // no controller
        calls.Mix(Virtual<uint32_t>(base, 0x54, kG1, Bits(30.0f), static_cast<void*>(nullptr), 0u) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0x54, kG1, Bits(30.0f), static_cast<void*>(target), 0u) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0x54, kG1, Bits(100.0f), static_cast<void*>(target), 1u) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0x54, kG2, Bits(1.0f), static_cast<void*>(target), 1u) & 0xFF);
        calls.Mix(Canon(At<uint32_t>(controllerBlock, 0x28)));
        if (void*& held = At<void*>(controllerBlock, 0x28)) {
            Virtual<uint32_t>(held, 0x08);
            held = nullptr;
        }
        // Shown and hidden (slots 61, 62).
        for (const uint32_t bits : {0u, 1u, 2u, 3u}) {
            At<uint8_t>(f1, 0x33) = static_cast<uint8_t>(bits);
            for (const uint32_t occupantFlags : {0u, 4u}) {
                fakes[0].state.occupantFlags = occupantFlags;
                for (const uint32_t answer : {0u, 1u}) {
                    fakes[0].state.answer70 = answer;
                    calls.Mix(Virtual<uint32_t>(base, 0xF4, static_cast<void*>(f1)) & 0xFF);
                }
            }
            calls.Mix(Virtual<uint32_t>(base, 0xF8, static_cast<void*>(f1)) & 0xFF);
        }
        calls.Mix(Virtual<uint32_t>(base, 0xF4, static_cast<void*>(nullptr)) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0xF8, static_cast<void*>(nullptr)) & 0xFF);
        At<uint8_t>(f1, 0x33) = 0;
        fakes[0].state.occupantFlags = 0;
        fakes[0].state.answer70 = 1;
        // Room by priority (slot 47) under two most counts (slot 23).
        for (const int32_t most : {10, 2}) {
            Virtual<void>(base, 0x5C, most);
            for (const uint32_t level : {0u, 2u, 5u, 0x105u})
                calls.Mix(Virtual<uint32_t>(base, 0xBC, level) & 0xFF);
        }
        Virtual<void>(base, 0x5C, 100);
        // Slots 57, 50, 60, 48, 49.
        calls.Mix(Virtual<uint32_t>(base, 0xE4, 0x5A5Au) & 0xFF);
        {
            void* none = nullptr;
            calls.Mix(Virtual<uint32_t>(base, 0xC8, static_cast<void*>(&none), 0u) & 0xFF);
            calls.Mix(Virtual<uint32_t>(base, 0xC8, static_cast<void*>(nullptr), 0u) & 0xFF);
            calls.Mix(At<uint8_t>(base, aw::field::kActive));
        }
        calls.Mix(Virtual<uint32_t>(base, 0xF0) & 0xFF);
        calls.Mix(At<uint8_t>(base, aw::field::kActive));
        calls.Mix(Virtual<uint32_t>(base, 0xC0) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0xC4, 1u, 1u) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0xC4, 1u, 2u) & 0xFF);
        logged.MixBytes(base + aw::field::kRect, 32);
        // The dispatch locations (slots 19 and 20) of each kind, with the
        // first fake's occupant as the object.
        {
            void* const cells = At<void*>(vehicles, aw::field::kCells);
            const int32_t* const area = cells ? Virtual<const int32_t*>(cells, 0x44) : nullptr;
            const int32_t cx = area ? (area[0] + area[2]) / 2 : 8, cz = area ? (area[1] + area[3]) / 2 : 8;
            const uint32_t locations[][5] = {
                {0, 0, 0, 0, 0},
                {0, reinterpret_cast<uint32_t>(f1 + 4), 0, 0, 0},
                {1, Bits(100.0f), Bits(7.0f), Bits(260.25f), 0},
                {1, Bits(-16.0f), Bits(0.0f), Bits(48.0f), 0},
                {2, static_cast<uint32_t>(cx), static_cast<uint32_t>(cz), 0, 0},
                {3, static_cast<uint32_t>(cx - 2), static_cast<uint32_t>(cz - 1), static_cast<uint32_t>(cx + 3),
                 static_cast<uint32_t>(cz + 2)},
                {4, 1, 2, 3, 4},
            };
            for (const auto& location : locations) {
                float point[3] = {-1.0f, -1.0f, -1.0f};
                int32_t rect[4] = {-1, -1, -1, -1};
                points.Mix(Virtual<uint32_t>(base, 0x4C, static_cast<const void*>(location), static_cast<void*>(point)) & 0xFF);
                points.MixBytes(point, sizeof point);
                points.Mix(Virtual<uint32_t>(base, 0x50, static_cast<const void*>(location), static_cast<void*>(rect)) & 0xFF);
                points.MixBytes(rect, sizeof rect);
            }
        }
        // The source building table: slot 65, then occupants counted in and
        // out (slots 53, 54, 55) with a building and a network occupant of
        // the city.
        Virtual<void>(base, 0x104);
        MixCounts(sources, base + aw::field::kSourceCounts);
        {
            // The first building and network occupants (types 0x278128A0,
            // 0xC772BF98) of the controller table by their hash.
            void* building = nullptr;
            void* n1 = nullptr;
            uint32_t best = 0xFFFFFFFFu, bestNetwork = 0xFFFFFFFFu;
            ForEach<ControllerNode>(Table(Manager(city), field::kControllers), [&](ControllerNode* node) {
                void* const occupant = node->occupant;
                if (!occupant)
                    return;
                const uint32_t type = Virtual<uint32_t>(occupant, kGetType);
                if (type != 0x278128A0u && type != 0xC772BF98u)
                    return;
                const uint32_t h = OccupantHash(occupant);
                if (type == 0x278128A0u && h < best) {
                    best = h;
                    building = occupant;
                } else if (type == 0xC772BF98u && h < bestNetwork) {
                    bestNetwork = h;
                    n1 = occupant;
                }
            });
            if (building) {
                sources.Mix(Virtual<uint32_t>(base, 0xD8, building) & 0xFF);
                MixCounts(sources, base + aw::field::kSourceCounts);
                void* lot = nullptr;
                Virtual<uint8_t>(building, 0x00, 0x87DDA3A9u, &lot);
                if (lot) {
                    sources.Mix(Virtual<uint32_t>(base, 0xD4, lot) & 0xFF);
                    MixCounts(sources, base + aw::field::kSourceCounts);
                    Virtual<uint32_t>(lot, 0x08);
                }
                sources.Mix(Virtual<uint32_t>(base, 0xDC, building) & 0xFF);
                MixCounts(sources, base + aw::field::kSourceCounts);
            } else {
                flags |= 0x10;
            }
            if (n1) {
                sources.Mix(Virtual<uint32_t>(base, 0xDC, n1) & 0xFF);
                MixCounts(sources, base + aw::field::kSourceCounts);
            } else {
                flags |= 0x20;
            }
            sources.Mix(Virtual<uint32_t>(base, 0xD8, static_cast<void*>(f1 + 4)) & 0xFF);
            sources.Mix(Virtual<uint32_t>(base, 0xDC, static_cast<void*>(f1 + 4)) & 0xFF);
            MixCounts(sources, base + aw::field::kSourceCounts);
        }
        // The cheats' registration and release (slots 39, 40; the counter
        // stays above 0) and a message nobody takes (slot 43).
        calls.Mix(Virtual<uint32_t>(base, 0x9C) & 0xFF);
        calls.Mix(Virtual<uint32_t>(base, 0xA0) & 0xFF);
        Virtual<uint32_t>(base, 0xAC, 0x7A5A0003u, 1u, 2u, 3u);
        // Slot 46 refusing (priority 0 while inactive; full without room).
        {
            uint32_t start[3] = {0, 0, 0};
            At<uint8_t>(base, aw::field::kActive) = 0;
            calls.Mix(Virtual<uint32_t>(base, 0xB8, kG1, 0u, static_cast<void*>(start), static_cast<void*>(nullptr), 0u) ? 1u : 0u);
            const uint32_t most = At<uint32_t>(base, aw::field::kMaxCount);
            At<uint32_t>(base, aw::field::kMaxCount) = 1;
            calls.Mix(Virtual<uint32_t>(base, 0xB8, kG1, 0u, static_cast<void*>(start), static_cast<void*>(nullptr), 1u) ? 1u : 0u);
            At<uint32_t>(base, aw::field::kMaxCount) = most;
        }
        // The frame update (slot 63) through each branch, then the camera
        // follow (slots 34, 64) when there is a view; the removals end the
        // follow.
        fakes[0].state.occupantFlags = 4;  // through: flagged, slot 72, in view or removed (not at once)
        fakes[1].state.answer85 = 1;       // removed at once
        fakes[2].state.answer85 = 0;       // left, or through with 0x210040
        fakes[2].state.flags |= 0x200000;
        fakes[0].state.answer101 = 0;
        fakes[0].state.answerOccupant5 = 1;
        fakes[0].state.answer72 = 0;
        Virtual<void>(base, 0xFC, 0x11u, Bits(0.5f));
        MixList(calls, base);
        mixTracked();
        fakes[0].state.flags |= 0x10000;
        fakes[0].state.answer85 = 1;
        fakes[0].state.value32 = -1.0;
        Virtual<void>(base, 0xFC, 0x12u, Bits(0.25f));
        MixList(calls, base);
        fakes[0].state.answer85 = 0;
        At<uint8_t>(base, aw::field::kPaused) = 1;
        Virtual<void>(base, 0xFC, 0x13u, Bits(0.25f));
        At<uint8_t>(base, aw::field::kPaused) = 0;
        MixList(calls, base);
        // Back in: the second fake again for the camera and the rest.
        calls.Mix(Virtual<uint32_t>(base, 0x1C, static_cast<void*>(f2)) & 0xFF);
        fakes[1].state.answer85 = 0;
        if (void* const view = *reinterpret_cast<void**>(addr::SC4_View43DD0)) {
            char* const cam = Virtual<char*>(view, 0x40);
            float centre[3], offset[3];
            std::memcpy(centre, cam + 0x44, 12);
            std::memcpy(offset, cam + 0x50, 12);
            const int32_t zoom = At<int32_t>(cam, 0x108);
            Virtual<void>(base, 0x88, static_cast<void*>(f2));
            camera.MixBytes(cam + 0x44, 12);
            camera.MixBytes(reinterpret_cast<void*>(0x00B21BA8), 0x1C);
            const float moves[3][3] = {{70.0f, 5.0f, 75.0f}, {90.0f, 6.0f, 60.0f}, {91.0f, 6.0f, 61.0f}};
            for (const auto& move : moves) {
                std::memcpy(fakes[1].state.position, move, 12);
                fakes[1].state.heading[0] = move[0] * 0.01f;
                fakes[1].state.heading[2] = -move[2] * 0.01f;
                Virtual<void>(base, 0x100, Bits(33.0f));
                camera.MixBytes(cam + 0x44, 12);
                camera.MixBytes(reinterpret_cast<void*>(0x00B21BA8), 0x1C);
            }
            Virtual<void>(base, 0x88, static_cast<void*>(f2)); // the same: nothing
            Virtual<void>(base, 0x88, static_cast<void*>(nullptr));
            camera.Mix(At<int32_t>(cam, 0x108));
            using Centre = void(__thiscall*)(void*, const float*);
            using Zoom = void(__thiscall*)(void*, uint32_t);
            if (At<int32_t>(cam, 0x108) != zoom)
                reinterpret_cast<Zoom>(addr::cSC4CameraControl_SetZoom)(cam, Bits(static_cast<float>(zoom)));
            reinterpret_cast<Centre>(addr::cSC4CameraControl_SetCenter)(cam, centre);
            reinterpret_cast<Centre>(addr::cSC4CameraControl_SetCenterOffset)(cam, offset);
        } else {
            flags |= 0x40;
        }
        // After a load (slot 56): the loaded flag with one fake flagged
        // 0x8000, and the old save's recount of the source buildings.
        At<uint8_t>(base, 0x10) = 1;
        At<uint8_t>(base, aw::field::kA78) = 1;
        fakes[2].state.flags |= 0x8000;
        calls.Mix(Virtual<uint32_t>(base, 0xE0) & 0xFF);
        MixList(calls, base);
        MixCounts(sources, base + aw::field::kSourceCounts);
        At<uint8_t>(base, 0x10) = 0;
        At<uint8_t>(base, aw::field::kA78) = 0;
        // Slot 45: every automaton removed.
        calls.Mix(Virtual<uint32_t>(base, 0xB4) & 0xFF);
        MixList(calls, base);
        calls.Mix(At<uint32_t>(instances, aw::table::kCount));
        uint32_t unexpected = 0;
        for (int i = 0; i < 5; ++i) {
            const Fake& f = fakes[i];
            calls.Mix(f.state.refs);
            calls.Mix(f.state.flags);
            unexpected |= f.state.unexpected;
        }
        if (unexpected)
            flags |= 0x80;
        logged.Mix(unexpected);
        At<void*>(base, aw::field::kCells) = nullptr;
        At<void*>(base, aw::field::kScripts) = nullptr;
    } else if (base) {
        flags |= 8;
    }
    if (base)
        reinterpret_cast<Dtor>(addr::cSC4AutomataManager_DeletingDestructor)(base, 1);

    // ---- On the city's four managers, the generator (+0x50) seeded for the
    // calls and put back after: the exemplar choices (slots 25 to 27) for
    // every group of the exemplar map, a group not there and the default;
    // GetRandomCreationPoint (slot 16) at four places; the kind rates (slot
    // 42, the clock's: the log only).
    const std::size_t slots[] = {aw::kCitySlotAircraft, aw::kCitySlotWatercraft, automata_vehmgr::kCitySlotVehicles,
                                 automata_vehmgr::kCitySlotPedestrians};
    for (int m = 0; m < 4; ++m) {
        char* const manager = Virtual<char*>(city, slots[m]);
        if (!manager) {
            choices.Mix(0xBAD0u + static_cast<uint32_t>(m));
            continue;
        }
        constexpr std::size_t kGenerator = 0x50, kGeneratorSize = 0x9C8;
        std::vector<uint8_t> saved(manager + kGenerator, manager + kGenerator + kGeneratorSize);
        reinterpret_cast<Seed>(addr::cRZRandomSuper_Seed)(manager + kGenerator, 777u + static_cast<uint32_t>(m));
        char* const records = At<char*>(manager, aw::field::kExemplarsBegin);
        auto index = [&](char* record) {
            return record ? static_cast<uint32_t>((record - records) / aw::exemplar::kSize) : 0xFFFFFFFFu;
        };
        std::vector<uint32_t> keys;
        TreeKeys(At<char*>(At<char*>(manager, aw::field::kExemplarMap), aw::tree::kParent), keys);
        keys.push_back(0);
        keys.push_back(0x7A5A0001u);
        void* const cells = At<void*>(manager, aw::field::kCells);
        const int32_t* const area = cells ? Virtual<const int32_t*>(cells, 0x44) : nullptr;
        for (const uint32_t key : keys) {
            for (const uint32_t bit : {0u, 1u, 3u}) {
                choices.Mix(index(Virtual<char*>(manager, 0x64, key, bit, -2, -2)));
                if (area)
                    for (int i = 0; i < 3; ++i) {
                        const int32_t x = area[0] + i * (area[2] - area[0]) / 2;
                        const int32_t z = area[1] + (2 - i) * (area[3] - area[1]) / 2;
                        choices.Mix(index(Virtual<char*>(manager, 0x64, key, bit, x, z)));
                    }
                choices.Mix(index(Virtual<char*>(manager, 0x6C, key, 0u, bit)));
                choices.Mix(index(Virtual<char*>(manager, 0x6C, key, 0x7A5Au, bit)));
            }
            choices.Mix(index(Virtual<char*>(manager, 0x68, key, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu)));
            choices.Mix(index(Virtual<char*>(manager, 0x68, key, 2u, 0xFFFFFFFFu, 0xFFFFFFFFu, 1u)));
        }
        const uint32_t cellsX = city->CellCountX(), cellsZ = city->CellCountZ();
        for (uint32_t i = 0; i < 4 && cellsX && cellsZ; ++i) {
            const float position[3] = {static_cast<float>((i % 2 ? 3 : 1) * cellsX * 4), 10.0f,
                                       static_cast<float>((i / 2 ? 3 : 1) * cellsZ * 4)};
            for (const uint32_t group : {keys.empty() ? 0u : keys.front(), 0x7A5A0001u}) {
                for (const uint32_t lift : {0u, 0x40000u}) {
                    float out[3] = {};
                    points.Mix(Virtual<uint32_t>(manager, 0x40, group, static_cast<const float*>(position),
                                                 Bits(i == 3 ? 50.0f : 5.0f), Bits(i == 2 ? 1.0e9f : 120.0f),
                                                 static_cast<float*>(out), lift, 0u) & 0xFF);
                    points.MixBytes(out, sizeof out);
                }
            }
        }
        std::memcpy(manager + kGenerator, saved.data(), kGeneratorSize);
        for (uint32_t kind = 0; kind <= 12; ++kind) {
            const double rate = Virtual<double>(manager, 0xA8, kind);
            logged.MixBytes(&rate, sizeof rate);
        }
        // The common base's in-view test (slot 41, at its address) on a fake
        // of the test's own.
        static Fake viewed;
        viewed.Init(9, logged);
        const float spot[3] = {static_cast<float>(cellsX * 8), 0.0f, static_cast<float>(cellsZ * 8)};
        std::memcpy(viewed.state.position, spot, 12);
        using InViewFn = uint32_t(__thiscall*)(void*, void*);
        logged.Mix(reinterpret_cast<InViewFn>(addr::cSC4AutomataManager_InView)(manager, viewed.Base()) & 0xFF);
    }
    // ---- Slot 46 making one (priority 0xFF) on the city's vehicle manager,
    // its generator seeded and put back: what it made (its class) to the
    // fingerprint; then its slot 4 and released (it is in no list).
    {
        constexpr std::size_t kGenerator = 0x50, kGeneratorSize = 0x9C8;
        std::vector<uint8_t> saved(vehicles + kGenerator, vehicles + kGenerator + kGeneratorSize);
        reinterpret_cast<Seed>(addr::cRZRandomSuper_Seed)(vehicles + kGenerator, 4242u);
        std::vector<uint32_t> keys;
        TreeKeys(At<char*>(At<char*>(vehicles, aw::field::kExemplarMap), aw::tree::kParent), keys);
        uint32_t start[3] = {0, 0, 0};
        const uint32_t group = keys.empty() ? 0u : keys.front();
        void* const made = Virtual<void*>(vehicles, 0xB8, group, 0u, static_cast<void*>(start), static_cast<void*>(nullptr), 0xFFu);
        choices.Mix(made ? 1u : 0u);
        if (made) {
            choices.Mix(Virtual<uint32_t>(Virtual<void*>(made, 0x14), kGetType));
            Virtual<void>(made, 0x10);
            Virtual<uint32_t>(made, 0x08);
        }
        std::memcpy(vehicles + kGenerator, saved.data(), kGeneratorSize);
    }
    g_known.clear();
    char part[96];
    std::snprintf(part, sizeof part, "%08X,%08X,%08X,%08X,%08X,%X", calls.h, choices.h, points.h, sources.h, camera.h,
                  flags);
    text += part;
    g_aw3Text = text;
    log::Write("autotest: automata: aw3%s logged %08X", text.c_str(), logged.h);
}

// ====================================================================================
// Part aw4's drive (OPENSC4_TEST_AUTOMATA_AWMGR=4), see the top of the file.

std::string g_aw4Text;

// A message of the test's own with an object: slot 0x10 the type, slot 0x24
// a value, slot 0x28 the object, every other slot (up to 0x84) 0. The first
// fields are FakeMessage's.
struct FakeObjectMessage {
    void** vtable;
    uint32_t type;
    uint32_t value;
    void* object;
};
void* __fastcall FakeObjectOf(FakeObjectMessage* m, void*) { return m->object; }
void** FakeObjectMessageVtable()
{
    static void* table[34];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&FakeZero);
        table[0x10 / 4] = reinterpret_cast<void*>(&FakeType);
        table[0x24 / 4] = reinterpret_cast<void*>(&FakeValue);
        table[0x28 / 4] = reinterpret_cast<void*>(&FakeObjectOf);
    }
    return table;
}

// A fake occupant's property holder of the test's own (FakeState::holder),
// which is its own property and variant: slot 3 answers whether it is of
// the type 0x029244DB (`isType`) or of `type`, slot 9 for 0x8BFB1072 itself
// when `byte` is not 0, slot 5 (the variant) itself, slot 13 (the byte
// read) `byte`. Other slots take one argument and answer 0.
struct FakeProps {
    void** vtable;
    uint8_t isType;
    uint8_t byte;
    uint32_t type;
};
uint32_t __fastcall PropsIsType(FakeProps* p, void*, uint32_t type)
{
    return (p->isType && type == 0x029244DBu) || (p->type && type == p->type);
}
void* __fastcall PropsGet(FakeProps* p, void*, uint32_t id) { return id == 0x8BFB1072u && p->byte ? p : nullptr; }
void* __fastcall PropsVariant(FakeProps* p, void*) { return p; }
uint32_t __fastcall PropsByte(FakeProps* p, void*, uint8_t* out)
{
    if (out)
        *out = p->byte;
    return 1;
}
void** FakePropsVtable()
{
    static void* table[16];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&HolderNone);
        table[3] = reinterpret_cast<void*>(&PropsIsType);
        table[5] = reinterpret_cast<void*>(&PropsVariant);
        table[9] = reinterpret_cast<void*>(&PropsGet);
        table[13] = reinterpret_cast<void*>(&PropsByte);
    }
    return table;
}

// The nodes of a circular list (the next pointer at +0), the head left out.
uint32_t RingCount(char* head)
{
    uint32_t n = 0;
    for (char* node = At<char*>(head, 0); node != head && n < 4096; node = At<char*>(node, 0))
        ++n;
    return n;
}

// The aircraft manager's part: on a new manager from its factory, held by
// one reference.
void AwAirDrive(void* building, Hash& air, Hash& logged, uint32_t& flags)
{
    namespace aw = automata_awmgr;
    using Factory = void*(__cdecl*)();
    void* const serial = reinterpret_cast<Factory>(addr::cSC4AircraftManager_Create)();
    if (!serial) {
        flags |= 0x100;
        return;
    }
    char* const made = static_cast<char*>(serial) - aw::field::kSerializable;
    Virtual<uint32_t>(made, aw::slot::kAddRef);
    void*& generator = *reinterpret_cast<void**>(0x00B21AF4);
    void* const savedGenerator = generator;
    static Fake fakes[8];
    for (uint32_t i = 0; i < 8; ++i)
        fakes[i].Init(0x21 + i, air), At<void**>(fakes[i].block, 0) = FakeAutomatonVtable4();
    g_known.push_back({made, 0x60});
    if (building)
        g_known.push_back({building, 0x61});
    static FakeProps typed, plain, marked;
    typed = {FakePropsVtable(), 1, 0, 0};
    plain = {FakePropsVtable(), 0, 0, 0};
    marked = {FakePropsVtable(), 0, 3, 0};
    char* const f[8] = {fakes[0].Base(), fakes[1].Base(), fakes[2].Base(), fakes[3].Base(),
                        fakes[4].Base(), fakes[5].Base(), fakes[6].Base(), fakes[7].Base()};
    fakes[0].state.holder = &typed;
    fakes[1].state.holder = &marked;
    const float spot1[3] = {72.0f, 2.0f, 88.0f};
    std::memcpy(fakes[1].state.position, spot1, 12);

    // Init twice (the second does nothing), slot 24 again.
    air.Mix(Virtual<uint32_t>(made, 0x0C) & 0xFF);
    air.Mix(At<char*>(made, aw::field::kAirFilter) ? At<uint32_t>(At<char*>(made, aw::field::kAirFilter), 0) : 0u);
    air.Mix(generator == made + aw::field::kRandom ? 1u : 0u);
    air.Mix(At<uint32_t>(made, aw::field::kMaxCount));
    air.Mix(At<uint8_t>(made, aw::field::kMinZoom));
    air.Mix(Virtual<uint32_t>(made, 0x0C) & 0xFF);
    air.Mix(Virtual<uint32_t>(made, 0x60) & 0xFF);
    air.Mix(At<uint8_t>(made, aw::field::kMinZoom));

    // The save: Write of the new manager, then Read of it with two list
    // records (the first at fake 2's occupant's position with the byte 3),
    // which Write must give back; a read of version 6 (refused), of version 4
    // (the common base's body), and of a stream cut in the second record.
    auto write = [&](ByteOStream& out) { return Virtual<uint32_t>(serial, 0x0C, static_cast<void*>(&out)) & 0xFF; };
    auto read = [&](const std::vector<uint8_t>& bytes) {
        ByteIStream in(bytes);
        return Virtual<uint32_t>(serial, 0x10, static_cast<void*>(&in)) & 0xFF;
    };
    ByteOStream empty;
    air.Mix(write(empty));
    air.Mix(static_cast<uint32_t>(empty.bytes.size()));
    if (empty.bytes.size() >= 16) {
        air.MixBytes(empty.bytes.data(), 2);
        std::vector<uint8_t> two(empty.bytes.begin(), empty.bytes.end() - 4);
        const auto put = [&](uint32_t v) {
            for (int i = 0; i < 4; ++i)
                two.push_back(static_cast<uint8_t>(v >> (8 * i)));
        };
        put(2);
        for (const float v : spot1) {
            uint32_t w;
            std::memcpy(&w, &v, 4);
            put(w);
        }
        two.push_back(3);
        put(0x42000000u); // (32, 33, 34)
        put(0x42040000u);
        put(0x42080000u);
        two.push_back(1);
        air.Mix(read(two));
        air.Mix(RingCount(At<char*>(made, aw::field::kAirList)));
        ByteOStream again;
        air.Mix(write(again));
        if (again.bytes != two)
            flags |= 0x200;
        air.Mix(read(std::vector<uint8_t>{6, 0}));
        std::vector<uint8_t> v4 = {4, 0};
        v4.insert(v4.end(), empty.bytes.begin() + 4, empty.bytes.begin() + 4 + 4 + 2 + 4);
        Put32(v4, 2, 17); // the most
        Put32(v4, 8, 0);  // no automata
        air.Mix(read(v4));
        air.Mix(At<uint32_t>(made, aw::field::kMaxCount));
        air.Mix(At<uint8_t>(made, aw::field::kA78));
        std::vector<uint8_t> cut(two.begin(), two.end() - 7);
        air.Mix(read(cut));
        air.Mix(RingCount(At<char*>(made, aw::field::kAirList)));
        logged.MixBytes(empty.bytes.data(), empty.bytes.size());
    } else {
        flags |= 0x400;
    }

    // Map entries of the test's own (each holding a reference to its key
    // and its value, as Read's are): fake 2's occupant to fake 3, the
    // building to fake 4, fake 7's occupant to fake 8 and fake 8's to fake 7.
    using MapAt = uint32_t*(__thiscall*)(void*, const uint32_t*);
    char* const map = made + aw::field::kAirMap;
    auto enter = [&](void* key, char* value) {
        const uint32_t k = reinterpret_cast<uint32_t>(key);
        *reinterpret_cast<MapAt>(addr::SC4Uint32Map_At)(map, &k) = reinterpret_cast<uint32_t>(value);
        Virtual<uint32_t>(key, 0x04);
        Virtual<uint32_t>(value, 0x04);
    };
    enter(f[1] + 4, f[2]);
    if (building)
        enter(building, f[3]);
    enter(f[6] + 4, f[7]);
    enter(f[7] + 4, f[6]);
    air.Mix(At<uint32_t>(made, aw::field::kAirMapSize));

    // Messages to the message target: inserted and removed of an occupant
    // of the type 0x029244DB (nothing but the answer), inserted of fake 2's
    // (SC4UIScale's 0x004FBFE0, which finds no property and ends), removed
    // of fake 2's twice (0x004FB8D0: its entry erased and its list record of
    // the property byte 3 unlinked; then neither is there), a type no one
    // handles.
    FakeObjectMessage message{FakeObjectMessageVtable(), 0, 0, nullptr};
    auto send = [&](uint32_t type, void* object) {
        message.type = type;
        message.object = object;
        air.Mix(Virtual<uint32_t>(made + aw::field::kMessage, 0x0C, static_cast<void*>(&message)) & 0xFF);
        air.Mix(At<uint32_t>(made, aw::field::kAirMapSize));
        air.Mix(RingCount(At<char*>(made, aw::field::kAirList)));
    };
    send(0x99EF1142, f[0] + 4);
    send(0x99EF1143, f[0] + 4);
    fakes[1].state.holder = &plain;
    send(0x99EF1142, f[1] + 4);
    fakes[1].state.holder = &marked;
    send(0x99EF1143, f[1] + 4);
    send(0x99EF1143, f[1] + 4);
    send(0x12345678, nullptr);
    air.Mix(fakes[1].state.refs);
    air.Mix(fakes[2].state.refs);

    // The dispatch slots. Locations: a point (kind 1), the building (kind
    // 0), no object (kind 0, null) and a kind slot 19 refuses (9).
    const uint32_t kOff = 0x7A5A0F0Fu;
    const uint32_t toPoint[4] = {1, 0x42C00000u, 0, 0x42D00000u}; // (96, 0, 104)
    const uint32_t toBad[4] = {9, 0, 0, 0};
    const uint32_t fromPoint[4] = {1, 0x42800000u, 0, 0x42800000u};
    const uint32_t fromBuilding[4] = {0, reinterpret_cast<uint32_t>(building), 0, 0};
    const uint32_t fromNone[4] = {0, 0, 0, 0};
    using Slot17 = void*(__thiscall*)(void*, void*, const void*, const void*, uint32_t, uint32_t);
    using Slot18 = void*(__thiscall*)(void*, uint32_t, const void*, const void*, uint32_t, uint32_t, uint32_t,
                                      uint32_t);
    void** const vtable = *reinterpret_cast<void***>(made);
    auto slot17 = [&](void* automaton, const void* from, const void* to) {
        At<uint32_t>(f[4], 0x1E4) = kOff;
        void* const answer = reinterpret_cast<Slot17>(vtable[17])(made, automaton, from, to, 0, 0);
        air.Mix(Canon(reinterpret_cast<uint32_t>(answer)));
        air.Mix(At<uint32_t>(f[4], 0x1E4));
        air.MixBytes(fakes[4].state.position, 12);
    };
    slot17(nullptr, fromPoint, toPoint);
    slot17(f[4], fromPoint, toBad);
    slot17(f[4], fromPoint, toPoint);
    slot17(f[4], fromNone, toPoint);
    for (const uint32_t group : {0x4301u, 0u}) {
        fakes[4].state.groups[0] = group;
        if (building)
            slot17(f[4], fromBuilding, toPoint);
    }
    auto slot18 = [&](const void* from, const void* to) {
        At<uint32_t>(f[3], 0x1E4) = kOff;
        void* const answer = reinterpret_cast<Slot18>(vtable[18])(made, kOff, from, to, 0, 0, 0, 0);
        air.Mix(Canon(reinterpret_cast<uint32_t>(answer)));
        air.Mix(At<uint32_t>(f[3], 0x1E4));
        air.MixBytes(fakes[3].state.position, 12);
    };
    slot18(fromPoint, toPoint);
    slot18(fromNone, toPoint);
    if (building) {
        slot18(fromBuilding, toBad);
        slot18(fromBuilding, toPoint);
        // The building's entry taken out (0x004FB8D0 at its address; it gives
        // back the entry's references), then slot 18 finds nothing.
        using Removed = void(__thiscall*)(void*, void*);
        reinterpret_cast<Removed>(addr::cSC4AircraftManager_OccupantRemoved)(made, building);
        air.Mix(At<uint32_t>(made, aw::field::kAirMapSize));
        slot18(fromBuilding, toPoint);
    } else {
        flags |= 0x10;
    }

    // The list find 0x004FB2B0 on nodes of the test's own: a record at (1, 2,
    // 3) with the byte 7; nodes with another byte, x 0.001 off, y NaN, z
    // 0.00005 off (found), one more, and the end.
    {
        alignas(4) static char nodes[6][0x18];
        std::memset(nodes, 0, sizeof nodes);
        const float base3[3] = {1.0f, 2.0f, 3.0f};
        for (int i = 0; i < 6; ++i) {
            At<char*>(nodes[i], 0) = nodes[i + 1 < 6 ? i + 1 : 5];
            std::memcpy(nodes[i] + 8, base3, 12);
            At<uint8_t>(nodes[i], 0x14) = 7;
        }
        At<uint8_t>(nodes[0], 0x14) = 8;
        At<float>(nodes[1], 8) = 1.001f;
        At<float>(nodes[2], 0xC) = std::numeric_limits<float>::quiet_NaN();
        At<float>(nodes[3], 0x10) = 3.00005f;
        alignas(4) char record[0x10] = {};
        std::memcpy(record, base3, 12);
        At<uint8_t>(record, 0xC) = 7;
        using Find = char**(__cdecl*)(char**, char*, char*, const char*);
        const auto find = reinterpret_cast<Find>(addr::cSC4AircraftManager_FindRecord);
        auto index = [&](char* n) {
            for (uint32_t i = 0; i < 6; ++i)
                if (n == nodes[i])
                    return i;
            return 0xFFu;
        };
        char* out = nullptr;
        air.Mix(index(*find(&out, nodes[0], nodes[5], record)));
        air.Mix(index(*find(&out, nodes[4], nodes[5], record)));
        air.Mix(index(*find(&out, nodes[5], nodes[5], record)));
        At<uint8_t>(record, 0xC) = 8;
        air.Mix(index(*find(&out, nodes[0], nodes[5], record)));
    }

    // AddAutomaton: a fake (the common base's AddAutomaton puts it in the
    // list), then none with the generator seeded until both kinds were made
    // (a helicopter for a draw below 0x7FFFFFFF, else the 0x200-byte
    // aircraft), each taken out again (slot 8 with 1).
    air.Mix(Virtual<uint32_t>(made, 0x1C, static_cast<void*>(f[5])) & 0xFF);
    air.Mix(RingCount(At<char*>(made, aw::field::kAutomata)));
    {
        using Seed = void(__thiscall*)(void*, uint32_t);
        uint32_t kinds = 0;
        for (uint32_t seed = 1; seed <= 8 && kinds != 3; ++seed) {
            reinterpret_cast<Seed>(addr::cRZRandomSuper_Seed)(made + aw::field::kRandom, seed);
            air.Mix(Virtual<uint32_t>(made, 0x1C, static_cast<void*>(nullptr)) & 0xFF);
            char* const head = At<char*>(made, aw::field::kAutomata);
            char* const last = At<char*>(head, 4);
            void* const automaton = last != head ? At<void*>(last, 8) : nullptr;
            if (!automaton || automaton == f[5]) {
                flags |= 0x800;
                break;
            }
            const uint32_t kind = At<uint32_t>(automaton, 0) == 0x00A991E0u   ? 1u
                                  : At<uint32_t>(automaton, 0) == 0x00A97188u ? 2u
                                                                              : 4u;
            kinds |= kind;
            air.Mix(seed);
            air.Mix(kind);
            air.Mix(Virtual<uint32_t>(made, 0x20, automaton, 1u) & 0xFF);
            air.Mix(RingCount(At<char*>(made, aw::field::kAutomata)));
        }
        air.Mix(kinds);
    }

    // Shutdown (the map's two fake entries released and erased, the list
    // freed, the fake automaton removed by slot 45), again (nothing), then
    // the release; the generator global put back.
    air.Mix(Virtual<uint32_t>(made, 0x10) & 0xFF);
    air.Mix(generator ? 1u : 0u);
    air.Mix(At<uint32_t>(made, aw::field::kAirMapSize));
    air.Mix(RingCount(At<char*>(made, aw::field::kAirList)));
    air.Mix(Virtual<uint32_t>(made, 0x10) & 0xFF);
    for (const Fake& fake : fakes) {
        air.Mix(fake.state.refs);
        if (fake.state.unexpected)
            flags |= 0x1000;
    }
    generator = savedGenerator;
    air.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
}

// The watercraft manager's part: the city's manager's Write (its tables'
// sizes into the token, the bytes to the log), then a new manager from its
// factory, held by one reference.
void AwWaterDrive(cISC4City* city, Hash& water, Hash& logged, uint32_t& flags)
{
    namespace aw = automata_awmgr;
    auto write = [&](void* serial, ByteOStream& out) {
        return Virtual<uint32_t>(serial, 0x0C, static_cast<void*>(&out)) & 0xFF;
    };
    auto read = [&](void* serial, const std::vector<uint8_t>& bytes) {
        ByteIStream in(bytes);
        return Virtual<uint32_t>(serial, 0x10, static_cast<void*>(&in)) & 0xFF;
    };
    if (char* const manager = Virtual<char*>(city, aw::kCitySlotWatercraft)) {
        ByteOStream out;
        water.Mix(write(manager + aw::field::kSerializable, out));
        for (const std::size_t at : {aw::field::kWaterTable, aw::field::kWaterTable2, aw::field::kWaterTable3})
            water.Mix(At<uint32_t>(manager + at, aw::table::kCount));
        water.Mix(static_cast<uint32_t>(At<char*>(manager, aw::field::kWaterRefs + 4) -
                                        At<char*>(manager, aw::field::kWaterRefs)));
        water.Mix(At<uint8_t>(manager, aw::field::kWaterByte));
        logged.Mix(static_cast<uint32_t>(out.bytes.size()));
        logged.MixBytes(out.bytes.data(), out.bytes.size());
    } else {
        flags |= 0x10000;
    }

    using Factory = void*(__cdecl*)();
    void* const serial = reinterpret_cast<Factory>(addr::cSC4WatercraftManager_Create)();
    if (!serial) {
        flags |= 0x20000;
        return;
    }
    char* const made = static_cast<char*>(serial) - aw::field::kSerializable;
    Virtual<uint32_t>(made, aw::slot::kAddRef);
    void*& generator = *reinterpret_cast<void**>(0x00B22A78);
    void* const savedGenerator = generator;
    static Fake fakes[12];
    for (uint32_t i = 0; i < 12; ++i)
        fakes[i].Init(0x31 + i, water), At<void**>(fakes[i].block, 0) = FakeAutomatonVtable4();
    g_known.push_back({made, 0x70});
    char* f[12];
    for (int i = 0; i < 12; ++i)
        f[i] = fakes[i].Base();
    constexpr uint32_t kTerminal = 0x4C18336Du, kDock = 0xEC39DD14u, kPair = 0x8C392D39u;
    static FakeProps typed, plain, terminal, dock, pairType;
    typed = {FakePropsVtable(), 1, 0, 0};
    plain = {FakePropsVtable(), 0, 0, 0};
    terminal = {FakePropsVtable(), 0, 0, kTerminal};
    dock = {FakePropsVtable(), 0, 0, kDock};
    pairType = {FakePropsVtable(), 0, 0, kPair};
    fakes[0].state.holder = &typed;
    fakes[1].state.holder = &terminal;
    fakes[2].state.holder = &dock;
    fakes[3].state.holder = &pairType;
    fakes[7].state.holder = &plain;
    const float spots[4][3] = {{40.0f, 3.0f, 40.0f}, {64.0f, 5.0f, 72.0f}, {20.0f, 1.0f, 24.0f}, {48.0f, 4.0f, 44.0f}};
    for (int i = 0; i < 4; ++i)
        std::memcpy(fakes[i].state.position, spots[i], 12);

    // Init twice (the second does nothing): the generator global, the most
    // (500, then the tuning's), the least zoom; slot 24 again.
    water.Mix(Virtual<uint32_t>(made, 0x0C) & 0xFF);
    water.Mix(generator == made + aw::field::kRandom ? 1u : 0u);
    water.Mix(At<uint32_t>(made, aw::field::kMaxCount));
    water.Mix(At<uint8_t>(made, aw::field::kMinZoom));
    water.Mix(Virtual<uint32_t>(made, 0x0C) & 0xFF);
    water.Mix(Virtual<uint32_t>(made, 0x60) & 0xFF);
    water.Mix(At<uint8_t>(made, aw::field::kMinZoom));

    auto counts = [&]() {
        for (const std::size_t at : {aw::field::kWaterTable, aw::field::kWaterTable2, aw::field::kWaterTable3})
            water.Mix(At<uint32_t>(made + at, aw::table::kCount));
        water.Mix(static_cast<uint32_t>(At<char*>(made, aw::field::kWaterRefs + 4) -
                                        At<char*>(made, aw::field::kWaterRefs)));
    };

    // A route of the test's own put in by the route insert 0x00574830: its
    // key fake 7, its +0x1C fake 8's occupant, three records (the automata
    // fakes 1, 2 and 3; ferry route words 0, 1 and 2 and their fields).
    {
        alignas(4) static char records[3][0x3C];
        std::memset(records, 0, sizeof records);
        for (uint32_t i = 0; i < 3; ++i) {
            char* const r = records[i];
            At<void*>(r, 0) = f[i];
            char* const fr = r + 4;
            At<uint32_t>(fr, 8) = i;
            for (std::size_t at = 0xC; at <= 0x12; at += 2)
                At<uint16_t>(fr, at) = static_cast<uint16_t>(0x100 * (i + 1) + at);
            At<uint8_t>(fr, 0x14) = static_cast<uint8_t>(i & 1);
            At<uint16_t>(fr, 0x16) = static_cast<uint16_t>(0x7000 + i);
            At<uint16_t>(fr, 0x18) = static_cast<uint16_t>(0x8000 + i);
            At<uint8_t>(r, 0x38) = 1;
        }
        alignas(4) char pair[0x1C] = {};
        At<void*>(pair, 0) = f[6];
        At<char*>(pair, 0xC) = records[0];
        At<char*>(pair, 0x10) = records[0] + 3 * 0x3C;
        At<char*>(pair, 0x14) = records[0] + 3 * 0x3C;
        At<void*>(pair, 0x18) = f[7] + 4;
        alignas(4) char result[12] = {};
        using InsertFn = void*(__thiscall*)(void*, void*, const void*);
        reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRoute)(made + aw::field::kWaterTable, result,
                                                                            pair);
        water.Mix(At<uint8_t>(result, 8));
        reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRoute)(made + aw::field::kWaterTable, result,
                                                                            pair);
        water.Mix(At<uint8_t>(result, 8));
        // A second pair table node whose +8 is fake 5 (its occupant's key),
        // and the reference vector of fakes 6, 7 and 8 (one reference each).
        void* p3[2] = {f[4] + 4, f[4]};
        using Resize = void(__thiscall*)(void*, uint32_t);
        char* const pairs3 = made + aw::field::kWaterTable3;
        reinterpret_cast<Resize>(addr::SC4Uint32HashSet_Resize)(pairs3, At<uint32_t>(pairs3, aw::table::kCount) + 1);
        reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertPair)(pairs3, result, p3);
        water.Mix(At<uint8_t>(result, 8));
        using Alloc = void*(__cdecl*)(uint32_t);
        void** const refs = static_cast<void**>(reinterpret_cast<Alloc>(addr::SC4_stl_allocate)(12));
        for (int i = 0; i < 3; ++i) {
            refs[i] = f[5 + i];
            Virtual<uint32_t>(f[5 + i], 0x04);
        }
        At<void**>(made, aw::field::kWaterRefs) = refs;
        At<void**>(made, aw::field::kWaterRefs + 4) = refs + 3;
        At<void**>(made, aw::field::kWaterRefs + 8) = refs + 3;
        counts();
    }

    // The save: Write with the route, Read of those bytes (the references
    // come back as nothing; the record word 2, written as -1, comes back as
    // the one before it), Write again; reads of version 7 (refused) and 5
    // (the common base's body).
    ByteOStream first;
    water.Mix(write(serial, first));
    water.Mix(static_cast<uint32_t>(first.bytes.size()));
    water.MixBytes(first.bytes.data(), first.bytes.size());
    if (first.bytes.size() >= 16) {
        water.Mix(read(serial, first.bytes));
        counts();
        ByteOStream again;
        water.Mix(write(serial, again));
        water.Mix(static_cast<uint32_t>(again.bytes.size()));
        water.MixBytes(again.bytes.data(), again.bytes.size());
        if (again.bytes.size() != first.bytes.size())
            flags |= 0x40000;
        water.Mix(read(serial, std::vector<uint8_t>{7, 0}));
        std::vector<uint8_t> v5 = {5, 0};
        v5.insert(v5.end(), first.bytes.begin() + 4, first.bytes.begin() + 4 + 4 + 2 + 4);
        Put32(v5, 2, 23); // the most
        Put32(v5, 8, 0);  // no automata
        // No source building records. Without the count the common base's
        // body would read it from a failed stream into a slot holding the
        // stream's address and add records of a stack leftover (part aw2).
        v5.insert(v5.end(), 4, uint8_t{0});
        water.Mix(read(serial, v5));
        water.Mix(At<uint32_t>(made, aw::field::kMaxCount));
        water.Mix(At<uint8_t>(made, aw::field::kA78));
    } else {
        flags |= 0x80000;
    }
    counts();

    // The route again (the round trip left one with a null key and no
    // records' automata), the second pair table's node and the vector.
    {
        alignas(4) static char records2[2][0x3C];
        std::memset(records2, 0, sizeof records2);
        At<void*>(records2[0], 0) = f[0];
        At<void*>(records2[1], 0) = f[1];
        alignas(4) char pair[0x1C] = {};
        At<void*>(pair, 0) = f[6];
        At<char*>(pair, 0xC) = records2[0];
        At<char*>(pair, 0x10) = records2[0] + 2 * 0x3C;
        At<char*>(pair, 0x14) = records2[0] + 2 * 0x3C;
        At<void*>(pair, 0x18) = f[7] + 4;
        alignas(4) char result[12] = {};
        using InsertFn = void*(__thiscall*)(void*, void*, const void*);
        reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRoute)(made + aw::field::kWaterTable, result,
                                                                            pair);
        void* p3[2] = {f[4] + 4, f[4]};
        using Resize = void(__thiscall*)(void*, uint32_t);
        char* const pairs3 = made + aw::field::kWaterTable3;
        reinterpret_cast<Resize>(addr::SC4Uint32HashSet_Resize)(pairs3, At<uint32_t>(pairs3, aw::table::kCount) + 1);
        reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertPair)(pairs3, result, p3);
        using Alloc = void*(__cdecl*)(uint32_t);
        void** const refs = static_cast<void**>(reinterpret_cast<Alloc>(addr::SC4_stl_allocate)(12));
        for (int i = 0; i < 3; ++i) {
            refs[i] = f[5 + i];
            Virtual<uint32_t>(f[5 + i], 0x04);
        }
        // Read erased the vector's references; its storage is the game's to
        // free, and is given back here before ours goes in.
        using FreeFn = void(__cdecl*)(void*);
        if (void* const old = At<void*>(made, aw::field::kWaterRefs))
            reinterpret_cast<FreeFn>(addr::SC4_Free)(old);
        At<void**>(made, aw::field::kWaterRefs) = refs;
        At<void**>(made, aw::field::kWaterRefs + 4) = refs + 3;
        At<void**>(made, aw::field::kWaterRefs + 8) = refs + 3;
        counts();
    }

    // RemoveAutomaton (slot 8, with 1): fake 1 of the kind 0x4400 (in the
    // route's records), fake 3 of the kind 0x4402 (not there), fake 5 in the
    // group 0x4411 (the second pair table's node), fake 7 and then fake 8 in
    // the group 0x4418 (the vector's middle, then its end), fake 10 in none,
    // and none.
    auto remove = [&](char* automaton) {
        water.Mix(Virtual<uint32_t>(made, 0x20, static_cast<void*>(automaton), 1u) & 0xFF);
        counts();
    };
    fakes[0].state.kind[4] = 0x4400;
    fakes[2].state.kind[4] = 0x4402;
    fakes[4].state.groups[0] = 0x4411;
    fakes[6].state.groups[0] = 0x4418;
    fakes[7].state.groups[0] = 0x4418;
    remove(f[0]);
    remove(f[2]);
    remove(f[4]);
    remove(f[6]);
    remove(f[7]);
    remove(f[9]);
    remove(nullptr);

    // Messages to the message target: 0x2C06BA82 (+0xA88), an occupant
    // added without one, of the type 0x029244DB, of a terminal type (twice:
    // put in, then found), of the dock type (no route: the dock search finds
    // no lot interface on a fake), of the pair type; removed: the route's
    // +0x1C (its records' automata stopped, the route erased), the terminal
    // and pair ones; a type no one handles. Without an occupant the
    // handlers are called at their addresses.
    FakeObjectMessage message{FakeObjectMessageVtable(), 0, 0, nullptr};
    auto send = [&](uint32_t type, void* object) {
        message.type = type;
        message.object = object;
        water.Mix(Virtual<uint32_t>(made + aw::field::kMessage, 0x0C, static_cast<void*>(&message)) & 0xFF);
        counts();
    };
    send(0x2C06BA82, nullptr);
    water.Mix(At<uint8_t>(made, aw::field::kWaterByte));
    {
        // Without an occupant, at the handlers' addresses: the common base's
        // DoMessage does not test it.
        using Handler = void(__thiscall*)(void*, void*);
        reinterpret_cast<Handler>(addr::cSC4WatercraftManager_OccupantAdded)(made, nullptr);
        reinterpret_cast<Handler>(addr::cSC4WatercraftManager_OccupantRemoved)(made, nullptr);
        counts();
    }
    send(0x99EF1142, f[0] + 4);
    send(0x99EF1142, f[1] + 4);
    send(0x99EF1142, f[1] + 4);
    send(0x99EF1142, f[2] + 4);
    send(0x99EF1142, f[3] + 4);
    send(0x99EF1143, f[7] + 4);
    send(0x99EF1143, f[1] + 4);
    send(0x99EF1143, f[3] + 4);
    send(0x99EF1143, f[0] + 4);
    send(0x12345678, nullptr);

    // AddAutomaton: a fake (in the list), then none (a new boat). The boat
    // stays in the list for Shutdown's slot 45: the watercraft manager's
    // RemoveAutomaton reads its slot 67's block, which a new boat has not.
    water.Mix(Virtual<uint32_t>(made, 0x1C, static_cast<void*>(f[10])) & 0xFF);
    water.Mix(Virtual<uint32_t>(made, 0x1C, static_cast<void*>(nullptr)) & 0xFF);
    {
        char* const head = At<char*>(made, aw::field::kAutomata);
        char* const last = At<char*>(head, 4);
        void* const boat = last != head ? At<void*>(last, 8) : nullptr;
        if (boat && boat != f[10]) {
            water.Mix(At<uint32_t>(boat, 0) == 0 ? 0u : 1u);
        } else {
            flags |= 0x100000;
        }
        water.Mix(RingCount(At<char*>(made, aw::field::kAutomata)));
    }

    // On tables of the test's own with one bucket (so that the second key
    // goes in front of the first): the pair table's and the route table's
    // erase of the older node (its release, 0x0056C910 and 0x00574230) and
    // then of the first; and the route pair's constructor and the records'
    // clear on a route value of the test's own, as the dock branch of the
    // occupant's addition makes them.
    {
        using Ctor = void*(__thiscall*)(void*, uint32_t);
        using InsertFn = void*(__thiscall*)(void*, void*, const void*);
        using EraseFn = void(__thiscall*)(void*, void*);
        using Alloc = void*(__cdecl*)(uint32_t);
        using FreeFn = void(__cdecl*)(void*);
        const auto oneBucket = [&](char* t) {
            reinterpret_cast<Ctor>(addr::SC4Uint32HashSet_Construct)(t, 0x64);
            if (void* const old = At<void*>(t, aw::table::kBegin))
                reinterpret_cast<FreeFn>(addr::SC4_Free)(old);
            char** const bucket = static_cast<char**>(reinterpret_cast<Alloc>(addr::SC4_stl_allocate)(4));
            bucket[0] = nullptr;
            At<char**>(t, aw::table::kBegin) = bucket;
            At<char**>(t, aw::table::kEnd) = bucket + 1;
            At<char**>(t, aw::table::kCapacity) = bucket + 1;
        };
        const auto erase = [&](uintptr_t fn, char* t, char* node) {
            void* iterator[2] = {node, t};
            reinterpret_cast<EraseFn>(fn)(t, iterator);
            water.Mix(At<uint32_t>(t, aw::table::kCount));
            water.Mix(At<char**>(t, aw::table::kBegin)[0] ? 1u : 0u);
        };
        alignas(4) char pairs[aw::table::kSize] = {};
        oneBucket(pairs);
        char* nodes[2] = {};
        for (int i = 0; i < 2; ++i) {
            void* pair[2] = {f[8 + i] + 4, f[8 + i]};
            alignas(4) char result[12] = {};
            reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertPair)(pairs, result, pair);
            nodes[i] = At<char*>(result, 0);
        }
        erase(addr::cSC4WatercraftManager_ErasePair, pairs, nodes[0]);
        erase(addr::cSC4WatercraftManager_ErasePair, pairs, nodes[1]);
        reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(pairs, aw::table::kBegin));
        alignas(4) char routes[aw::table::kSize] = {};
        oneBucket(routes);
        for (int i = 0; i < 2; ++i) {
            alignas(4) char pair[0x1C] = {};
            At<void*>(pair, 0) = f[8 + i];
            At<void*>(pair, 0x18) = f[8 + i] + 4;
            alignas(4) char result[12] = {};
            reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRouteNode)(routes, result, pair);
            nodes[i] = At<char*>(result, 0);
        }
        erase(addr::cSC4WatercraftManager_EraseRoute, routes, nodes[0]);
        erase(addr::cSC4WatercraftManager_EraseRoute, routes, nodes[1]);
        reinterpret_cast<FreeFn>(addr::SC4_Free)(At<void*>(routes, aw::table::kBegin));
        alignas(4) char value[0x18] = {};
        using Clear = void(__thiscall*)(void*);
        reinterpret_cast<Clear>(addr::cSC4WatercraftManager_ClearRecords)(value + 8);
        At<void*>(value, 0x14) = f[9] + 4;
        void* key = f[8];
        alignas(4) char pair[0x1C] = {};
        using MakeFn = char*(__thiscall*)(void*, void* const*, const void*);
        char* const madePair = reinterpret_cast<MakeFn>(addr::cSC4WatercraftManager_MakeRoutePair)(pair, &key, value);
        water.Mix(madePair == pair ? 1u : 0u);
        for (std::size_t at = 0; at < 0x1C; at += 4)
            water.Mix(Canon(At<uint32_t>(pair, at)));
        reinterpret_cast<Clear>(addr::cSC4WatercraftManager_DestroyRoute)(pair + 4);
        if (void* const k = At<void*>(pair, 0))
            Virtual<uint32_t>(k, 0x08);
        At<void*>(value, 0x14) = nullptr;
        reinterpret_cast<Clear>(addr::cSC4WatercraftManager_DestroyRoute)(value);
    }

    // Shutdown (the tables cleared, the vector erased, the fake automaton
    // removed by slot 45), again (nothing), then the release; the generator
    // global put back.
    water.Mix(Virtual<uint32_t>(made, 0x10) & 0xFF);
    water.Mix(generator ? 1u : 0u);
    counts();
    water.Mix(Virtual<uint32_t>(made, 0x10) & 0xFF);
    for (const Fake& fake : fakes) {
        water.Mix(fake.state.refs);
        if (fake.state.unexpected)
            flags |= 0x200000;
    }
    generator = savedGenerator;
    water.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
}

void AwBehaviourDrive(cISC4City* city)
{
    std::string text = " E";
    Hash air, water, logged;
    uint32_t flags = 0;
    g_known.clear();
    // The first building occupant of the controller table by its hash (the
    // aircraft dispatch slots' source and map key).
    void* building = nullptr;
    uint32_t best = 0xFFFFFFFFu;
    if (char* const manager = Manager(city))
        ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
            void* const occupant = node->occupant;
            if (!occupant || Virtual<uint32_t>(occupant, kGetType) != 0x278128A0u)
                return;
            const uint32_t h = OccupantHash(occupant);
            if (h < best) {
                best = h;
                building = occupant;
            }
        });
    AwAirDrive(building, air, logged, flags);
    AwWaterDrive(city, water, logged, flags);
    g_known.clear();
    char part[64];
    std::snprintf(part, sizeof part, "%08X,%08X,%X", air.h, water.h, flags);
    text += part;
    g_aw4Text = text;
    log::Write("autotest: automata: aw4%s logged %08X", text.c_str(), logged.h);
}

// ====================================================================================
// Part aw5's drive (OPENSC4_TEST_AUTOMATA_AWMGR=5), see the top of the file.

std::string g_aw5Text;

// A fake boat: part aw4's fake automaton in a 0x400-byte block (the boat's
// fields up to +0x344: its legs +0x1E4 and +0x218, its path +0x2A4/+0x2A8 of
// 16-byte points, its timers, target and route; its state in
// g_fakeStates, as the legs cover +0x1F0), with slots 16 (a place and
// facing), 17 (its radius, ST0: FakeState::value32), 35 and 59 answering 0;
// its occupant answers slot 35 (0) too.
constexpr std::size_t kBoatSize = 0x400;

void** FakeBoatVtable()
{
    static void* table[0x1A0 / 4];
    if (!table[0]) {
        std::memcpy(table, FakeAutomatonVtable4(), sizeof table);
        table[0x40 / 4] = reinterpret_cast<void*>(&Fake2<0, 0x40, 3u>);
        table[0x44 / 4] = reinterpret_cast<void*>(&FakeDouble<0, 0x44>);
        table[0x8C / 4] = reinterpret_cast<void*>(&Fake0<0, 0x8C>);
        table[0xEC / 4] = reinterpret_cast<void*>(&Fake0<0, 0xEC>);
    }
    return table;
}

void** FakeBoatOccupantVtable()
{
    static void* table[0x90 / 4];
    if (!table[0]) {
        std::memcpy(table, FakeOccupantVtable(), sizeof table);
        table[0x8C / 4] = reinterpret_cast<void*>(&Fake0<4, 0x8C>);
    }
    return table;
}

struct FakeBoat {
    alignas(16) char block[kBoatSize] = {};
    FakeState state;
    char* Base() { return block; }
    void Init(uint32_t id, Hash& calls)
    {
        std::memset(block, 0, sizeof block);
        At<void**>(block, 0) = FakeBoatVtable();
        At<void**>(block, 4) = FakeBoatOccupantVtable();
        At<void**>(block, 0x20) = FakePart20Vtable();
        At<void**>(block, 0x18C) = FakePart18CVtable();
        state = FakeState{};
        state.calls = &calls;
        state.id = id;
        bool listed = false;
        for (auto& entry : g_fakeStates)
            if (entry.first == block)
                listed = true;
        if (!listed)
            g_fakeStates.push_back({block, &state});
        g_known.push_back({block, id});
        g_known.push_back({block + 4, id | 0x100});
    }
    // The storage the game's helpers gave its legs (+0x1E4, +0x218,
    // +0x24C: a byte vector at +4) and its route (+0x31C), freed.
    void Scrub()
    {
        using FreeFn = void(__cdecl*)(void*);
        for (const std::size_t at : {0x1E8u, 0x21Cu, 0x250u, 0x31Cu})
            if (void* const buffer = At<void*>(block, at)) {
                reinterpret_cast<FreeFn>(addr::SC4_Free)(buffer);
                At<void*>(block, at) = nullptr;
            }
    }
};

// A dock interface of the test's own (what 0x00568CE0 asks an occupant
// for): slot 9 its orientation, slot 3 its model (itself), slot 10 the
// model's box (two points).
struct FakeDock {
    void** vtable;
    uint32_t refs;
    uint32_t orientation;
    float box[6];
};
uint32_t __fastcall DockAddRef(FakeDock* d, void*) { return ++d->refs; }
uint32_t __fastcall DockRelease(FakeDock* d, void*) { return --d->refs; }
void* __fastcall DockModel(FakeDock* d, void*) { return d; }
uint32_t __fastcall DockOrientation(FakeDock* d, void*) { return d->orientation; }
uint32_t __fastcall DockBox(FakeDock* d, void*, float* low, float* high)
{
    std::memcpy(low, d->box, 12);
    std::memcpy(high, d->box + 3, 12);
    return 1;
}
void** FakeDockVtable()
{
    static void* table[16];
    if (!table[0]) {
        for (void*& slot : table)
            slot = reinterpret_cast<void*>(&HolderNone);
        table[1] = reinterpret_cast<void*>(&DockAddRef);
        table[2] = reinterpret_cast<void*>(&DockRelease);
        table[3] = reinterpret_cast<void*>(&DockModel);
        table[9] = reinterpret_cast<void*>(&DockOrientation);
        table[10] = reinterpret_cast<void*>(&DockBox);
    }
    return table;
}

// Slot 15 (CreateAutomaton) of the test's manager while its vtable is the
// test's copy: the arguments into the hash, the next fake boat of the pool
// (null when the pool is used up or `g_createRefuse`).
FakeBoat* g_createPool = nullptr;
uint32_t g_createCount = 0, g_createNext = 0;
bool g_createRefuse = false;
Hash* g_createHash = nullptr;

void* __fastcall FakeCreate(char*, void*, uint32_t kind, const float* point, uint32_t flags, const float* target,
                            uint32_t a5, uint32_t a6, uint32_t a7)
{
    Hash& h = *g_createHash;
    h.Mix(0xC5EA0000u);
    h.Mix(kind);
    for (int i = 0; i < 3; ++i)
        h.Mix(point ? Bits(point[i]) : 0xFFFFFFFFu);
    h.Mix(flags);
    for (int i = 0; i < 3; ++i)
        h.Mix(target ? Bits(target[i]) : 0xFFFFFFFFu);
    h.Mix(a5);
    h.Mix(a6);
    h.Mix(a7);
    if (g_createRefuse || g_createNext >= g_createCount)
        return nullptr;
    return g_createPool[g_createNext++].Base();
}

struct LegBytes {
    uint32_t words[13];
};
struct RouteIterator {
    char* node;
    char* table;
};
struct PathBytes {
    char* begin;
    char* end;
    char* capacity;
};

// A leg into the hash: its reference as the fake it is, its byte vector's
// size, its points and its last words.
void MixLeg(Hash& h, const char* leg)
{
    h.Mix(Canon(At<uint32_t>(const_cast<char*>(leg), 0)));
    h.Mix(static_cast<uint32_t>(At<char*>(const_cast<char*>(leg), 8) - At<char*>(const_cast<char*>(leg), 4)));
    h.MixBytes(leg + 0x10, 0x24);
}

// The records of a vector (begin, end) into the hash: the count and each
// record's automaton as the fake it is, its ferry route's +8 and its byte.
void MixRecords(Hash& h, char* vector)
{
    char* const begin = At<char*>(vector, 0);
    char* const end = At<char*>(vector, 4);
    h.Mix(static_cast<uint32_t>(end - begin) / 0x3C);
    for (char* r = begin; r < end; r += 0x3C) {
        h.Mix(Canon(At<uint32_t>(r, 0)));
        h.Mix(At<uint32_t>(r, 0xC));
        h.Mix(At<uint8_t>(r, 0x38));
    }
}

void AwWaterRouteDrive(cISC4City* city, void* building)
{
    namespace aw = automata_awmgr;
    Hash create, routes, frame, logged;
    uint32_t flags = 0;
    using Alloc = void*(__cdecl*)(uint32_t);
    using FreeFn = void(__cdecl*)(void*);
    const auto allocate = [](uint32_t size) { return reinterpret_cast<Alloc>(addr::SC4_stl_allocate)(size); };
    const auto release = [](void* p) { reinterpret_cast<FreeFn>(addr::SC4_Free)(p); };
    // The game's globals the part changes: the generator, the edge spawn's
    // side, timer and once bit, and the frame's two timers. Put back at the
    // end.
    constexpr uintptr_t kSaved[] = {0x00B22A78, 0x00B22ABC, 0x00B22AC0, 0x00B22AC4, 0x00B22AD0, 0x00B22AD4};
    uint32_t saved[6];
    for (int i = 0; i < 6; ++i)
        saved[i] = *reinterpret_cast<uint32_t*>(kSaved[i]);

    // The new watercraft manager of the test's own (used further down) is
    // made first, for its route table's bucket count.
    using Factory = void*(__cdecl*)();
    void* const serial = reinterpret_cast<Factory>(addr::cSC4WatercraftManager_Create)();
    char* const made = serial ? static_cast<char*>(serial) - aw::field::kSerializable : nullptr;
    uint32_t buckets = 0;
    if (made) {
        Virtual<uint32_t>(made, aw::slot::kAddRef);
        Virtual<uint32_t>(made, 0x0C); // Init
        buckets = static_cast<uint32_t>(At<char**>(made + aw::field::kWaterTable, aw::table::kEnd) -
                                        At<char**>(made + aw::field::kWaterTable, aw::table::kBegin));
    }
    // The boats at an address that is 0 modulo that count: routes A and B
    // are keyed by boats 7 and 6, and the table's order (a key's bucket is
    // its address modulo the count) is the frame's order. With the boats
    // where the DLL happened to be loaded, the routes' and frame's hashes
    // took one of two values run to run, on both sides.
    constexpr uint32_t kMostBuckets = 1031;
    alignas(16) static char arena[10 * sizeof(FakeBoat) + 16 * kMostBuckets];
    std::size_t offset = 0;
    if (buckets && buckets <= kMostBuckets)
        while ((reinterpret_cast<uintptr_t>(arena) + offset) % buckets)
            offset += 16;
    else
        flags |= 0x40;
    FakeBoat* const boats = reinterpret_cast<FakeBoat*>(arena + offset);
    for (uint32_t i = 0; i < 10; ++i)
        new (&boats[i]) FakeBoat;
    log::Write("autotest: automata: aw5 boats at %p (%u buckets, offset %X)", static_cast<void*>(boats), buckets,
               static_cast<uint32_t>(offset));
    static FakeBoat pool[10];
    for (uint32_t i = 0; i < 10; ++i)
        boats[i].Init(0x51 + i, routes);
    for (uint32_t i = 0; i < 10; ++i)
        pool[i].Init(0x61 + i, frame);
    char* b[10];
    for (int i = 0; i < 10; ++i)
        b[i] = boats[i].Base();
    static FakeDock docks[2];
    docks[0] = {FakeDockVtable(), 0, 0, {-8.0f, 0.0f, -24.0f, 8.0f, 6.0f, 24.0f}};
    docks[1] = {FakeDockVtable(), 0, 1, {-30.0f, 0.0f, -10.0f, 30.0f, 4.0f, 10.0f}};

    // The city's span and middle (16 m cells).
    const int32_t cellsX = Virtual<int32_t>(*reinterpret_cast<void**>(addr::SC4_City), 0x174);
    const int32_t cellsZ = Virtual<int32_t>(*reinterpret_cast<void**>(addr::SC4_City), 0x178);
    const float middle[3] = {static_cast<float>(cellsX) * 8.0f + 3.0f, 0.0f, static_cast<float>(cellsZ) * 8.0f + 5.0f};
    logged.Mix(static_cast<uint32_t>(cellsX));
    logged.Mix(static_cast<uint32_t>(cellsZ));

    // ---- The leaf helpers ----
    // The leg's constructor on a buffer of 0xAB: its 0x28 bytes and EAX.
    {
        alignas(4) char leg[0x34];
        std::memset(leg, 0xAB, sizeof leg);
        using Ctor = char*(__thiscall*)(void*);
        routes.Mix(reinterpret_cast<Ctor>(addr::cSC4Watercraft_ConstructLeg)(leg) == leg ? 1u : 0u);
        routes.MixBytes(leg, sizeof leg);
    }
    // The crossing rank: none, the kinds 0x4400 and 0x4402, the groups
    // 0x4414 and 0x4413, none of them. The path group test: 0x440F, 0x440A,
    // 0x4411 and none.
    {
        using Rank = uint32_t(__thiscall*)(void*, void*);
        using Test = uint32_t(__thiscall*)(void*);
        boats[0].state.kind[4] = 0x4400;
        boats[1].state.kind[4] = 0x4402;
        boats[2].state.groups[0] = 0x4414;
        boats[3].state.groups[0] = 0x4413;
        boats[4].state.groups[0] = 0x440F;
        boats[5].state.groups[0] = 0x440A;
        boats[6].state.groups[0] = 0x4411;
        frame.Mix(reinterpret_cast<Rank>(addr::cSC4WatercraftManager_CrossingRank)(nullptr, nullptr));
        for (int i = 0; i < 5; ++i)
            frame.Mix(reinterpret_cast<Rank>(addr::cSC4WatercraftManager_CrossingRank)(nullptr, b[i == 4 ? 7 : i]));
        for (int i = 4; i < 8; ++i)
            routes.Mix(reinterpret_cast<Test>(addr::cSC4Watercraft_IsPathBoat)(b[i]) & 0xFF);
    }
    // A path of the eight unit steps and one of two cells: the step
    // direction at each index and past the end; the cut at -1, past the end
    // and at 3 (its bit, the new end).
    {
        alignas(16) static char points[10][16];
        std::memset(points, 0, sizeof points);
        const int32_t steps[9][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {2, 0}};
        int32_t x = 5, z = 5;
        for (int i = 0; i < 10; ++i) {
            At<int32_t>(points[i], 0) = x;
            At<int32_t>(points[i], 4) = z;
            if (i < 9) {
                x += steps[i][0];
                z += steps[i][1];
            }
        }
        char* const boat = b[8];
        At<char*>(boat, 0x2A4) = points[0];
        At<char*>(boat, 0x2A8) = points[0] + sizeof points;
        At<char*>(boat, 0x2AC) = points[0] + sizeof points;
        using Step = uint32_t(__thiscall*)(void*, uint32_t);
        using Cut = uint32_t(__thiscall*)(void*, int32_t);
        for (uint32_t i = 0; i < 10; ++i)
            frame.Mix(reinterpret_cast<Step>(addr::cSC4Watercraft_StepDirection)(boat, i));
        frame.Mix(reinterpret_cast<Step>(addr::cSC4Watercraft_StepDirection)(boat, 0xFFFFFFFFu));
        for (const int32_t at : {-1, 10, 20, 3}) {
            frame.Mix(reinterpret_cast<Cut>(addr::cSC4Watercraft_CutPathAt)(boat, at) & 0xFF);
            frame.Mix(static_cast<uint32_t>(At<char*>(boat, 0x2A8) - points[0]));
        }
        frame.MixBytes(points, sizeof points);
        At<char*>(boat, 0x2A4) = nullptr;
        At<char*>(boat, 0x2A8) = nullptr;
        At<char*>(boat, 0x2AC) = nullptr;
    }
    // The route records: three of the test's own (boats 0, 1 and 2, ferry
    // route words 1, 0 and 1, bytes 1, 0, 1); their copy (0x00572360) and of
    // an empty vector; the fill of two (and of one at a null place: the next
    // place is not null); the erase of
    // the middle and of the last; the growing insert into an empty vector
    // (one), at the second place of a full one (one, then three) and at its
    // end (atEnd); each vector destroyed and freed after.
    {
        alignas(4) static char records[3][0x3C];
        std::memset(records, 0, sizeof records);
        for (int i = 0; i < 3; ++i) {
            At<void*>(records[i], 0) = b[i];
            At<uint32_t>(records[i], 0xC) = static_cast<uint32_t>(i != 1);
            At<uint8_t>(records[i], 0x38) = static_cast<uint8_t>(i != 1);
        }
        char* source[3] = {records[0], records[0] + sizeof records, records[0] + sizeof records};
        char* none[3] = {};
        using Copy = char*(__thiscall*)(void*, const void*);
        using Destroy = void(__cdecl*)(char*, char*, void*);
        using Fill = char*(__cdecl*)(char*, uint32_t, const char*, void*);
        using Erase = char*(__thiscall*)(void*, char*);
        using Insert = void(__thiscall*)(void*, char*, const char*, void*, uint32_t, uint32_t);
        uint32_t tag = 0;
        const auto destroy = [&](char** v) {
            reinterpret_cast<Destroy>(addr::cSC4WatercraftManager_DestroyRecords)(v[0], v[1], &tag);
            if (v[0])
                release(v[0]);
            v[0] = v[1] = v[2] = nullptr;
        };
        char* copy[3] = {};
        char* const answer = reinterpret_cast<Copy>(addr::cSC4WatercraftManager_CopyRecordVector)(copy, none);
        frame.Mix(answer == reinterpret_cast<char*>(copy) ? 1u : 0u);
        frame.Mix(copy[0] ? 1u : 0u);
        destroy(copy);
        reinterpret_cast<Copy>(addr::cSC4WatercraftManager_CopyRecordVector)(copy, source);
        MixRecords(frame, reinterpret_cast<char*>(copy));
        frame.Mix(static_cast<uint32_t>(copy[2] - copy[0]));
        char* const filled = static_cast<char*>(allocate(2 * 0x3C));
        char* const end = reinterpret_cast<Fill>(addr::cSC4WatercraftManager_FillRecords)(filled, 2, records[2], &tag);
        frame.Mix(static_cast<uint32_t>(end - filled));
        char* range[2] = {filled, end};
        MixRecords(frame, reinterpret_cast<char*>(range));
        reinterpret_cast<Destroy>(addr::cSC4WatercraftManager_DestroyRecords)(filled, end, &tag);
        release(filled);
        frame.Mix(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
            reinterpret_cast<Fill>(addr::cSC4WatercraftManager_FillRecords)(nullptr, 1, records[2], &tag))));
        frame.Mix(static_cast<uint32_t>(
            reinterpret_cast<Erase>(addr::cSC4WatercraftManager_EraseRecord)(copy, copy[0] + 0x3C) - copy[0]));
        MixRecords(frame, reinterpret_cast<char*>(copy));
        frame.Mix(static_cast<uint32_t>(
            reinterpret_cast<Erase>(addr::cSC4WatercraftManager_EraseRecord)(copy, copy[1] - 0x3C) - copy[0]));
        MixRecords(frame, reinterpret_cast<char*>(copy));
        destroy(copy);
        char* grown[3] = {};
        reinterpret_cast<Insert>(addr::cSC4WatercraftManager_InsertRecordOverflow)(grown, nullptr, records[0], &tag, 1,
                                                                                  1);
        MixRecords(frame, reinterpret_cast<char*>(grown));
        frame.Mix(static_cast<uint32_t>(grown[2] - grown[0]));
        reinterpret_cast<Copy>(addr::cSC4WatercraftManager_CopyRecordVector)(copy, source);
        reinterpret_cast<Insert>(addr::cSC4WatercraftManager_InsertRecordOverflow)(copy, copy[0] + 0x3C, records[1],
                                                                                  &tag, 1, 0);
        MixRecords(frame, reinterpret_cast<char*>(copy));
        frame.Mix(static_cast<uint32_t>(copy[2] - copy[0]));
        reinterpret_cast<Insert>(addr::cSC4WatercraftManager_InsertRecordOverflow)(grown, grown[0] + 0x3C, records[2],
                                                                                  &tag, 3, 1);
        MixRecords(frame, reinterpret_cast<char*>(grown));
        frame.Mix(static_cast<uint32_t>(grown[2] - grown[0]));
        destroy(copy);
        reinterpret_cast<Copy>(addr::cSC4WatercraftManager_CopyRecordVector)(copy, source);
        reinterpret_cast<Insert>(addr::cSC4WatercraftManager_InsertRecordOverflow)(copy, copy[0] + 0x3C, records[1],
                                                                                  &tag, 3, 0);
        MixRecords(frame, reinterpret_cast<char*>(copy));
        destroy(copy);
        destroy(grown);
        for (int i = 0; i < 3; ++i)
            frame.Mix(boats[i].state.refs);
    }
    // The cells of the city's four edges (sides 0 to 3, 5 and -1): their
    // count, the side as stored back and the cells' bytes.
    {
        using Edge = void(__thiscall*)(void*, int32_t*, void*);
        for (const int32_t side : {0, 1, 2, 3, 5, -1}) {
            char* cells[3] = {};
            int32_t s = side;
            reinterpret_cast<Edge>(addr::cSC4WatercraftManager_EdgeCells)(nullptr, &s, cells);
            frame.Mix(static_cast<uint32_t>(s));
            frame.Mix(static_cast<uint32_t>(cells[1] - cells[0]));
            if (cells[0]) {
                frame.MixBytes(cells[0], static_cast<std::size_t>(cells[1] - cells[0]));
                release(cells[0]);
            }
        }
    }

    // ---- The city's watercraft manager: queries only ----
    char* const water = Virtual<char*>(city, aw::kCitySlotWatercraft);
    void* lot = nullptr;
    {
        using LotAt = void*(__thiscall*)(void*, void*);
        using Body = uint32_t(__thiscall*)(void*, void*);
        create.Mix(reinterpret_cast<LotAt>(addr::cSC4WatercraftManager_LotAtOccupant)(nullptr, nullptr) ? 1u : 0u);
        create.Mix(reinterpret_cast<Body>(addr::cSC4WatercraftManager_LotWaterBody)(nullptr, nullptr) & 0xFFFF);
        if (building) {
            lot = reinterpret_cast<LotAt>(addr::cSC4WatercraftManager_LotAtOccupant)(nullptr, building);
            create.Mix(lot ? 1u : 0u);
            if (lot)
                create.Mix(reinterpret_cast<Body>(addr::cSC4WatercraftManager_LotWaterBody)(nullptr, lot) & 0xFFFF);
        } else {
            flags |= 1;
        }
    }
    // The dock search: none, a fake without the interface, the building,
    // and the fake with each of four orientations (the two boxes).
    {
        using Dock = uint32_t(__thiscall*)(void*, void*, float*, float*, uint32_t);
        auto dock = [&](void* occupant) {
            float point[3] = {1.0f, 2.0f, 3.0f}, direction[3] = {4.0f, 5.0f, 6.0f};
            create.Mix(reinterpret_cast<Dock>(addr::cSC4WatercraftManager_DockPoints)(nullptr, occupant, point,
                                                                                     direction, 0) & 0xFF);
            create.MixBytes(point, 12);
            create.MixBytes(direction, 12);
        };
        dock(nullptr);
        std::memcpy(boats[9].state.position, middle, 12);
        dock(b[9] + 4);
        if (building)
            dock(building);
        for (uint32_t o = 0; o < 4; ++o) {
            boats[9].state.dock = &docks[o & 1];
            docks[o & 1].orientation = o;
            dock(b[9] + 4);
        }
        boats[9].state.dock = nullptr;
        create.Mix(docks[0].refs);
        create.Mix(docks[1].refs);
    }
    // Open water (kind 2) at four cells, and a creation point at three
    // places (the middle, a corner, the building) for three groups, near
    // and not nearer than 200.
    if (water) {
        using Open = uint32_t(__thiscall*)(void*, int32_t, int32_t, uint32_t);
        const int32_t cells[4][2] = {{0, 0}, {cellsX / 2, cellsZ / 2}, {cellsX - 1, 3}, {-1, -1}};
        for (const auto& c : cells)
            create.Mix(reinterpret_cast<Open>(addr::cSC4WatercraftManager_IsOpenWater)(water, c[0], c[1], 2) & 0xFF);
        float places[3][3] = {{middle[0], 0.0f, middle[2]}, {20.0f, 0.0f, 20.0f}, {0.0f, 0.0f, 0.0f}};
        if (building)
            Virtual<void>(building, kGetPosition, static_cast<void*>(places[2]));
        for (const auto& p : places)
            for (const uint32_t group : {0x4418u, 0x4400u, 0x12345u})
                for (const float least : {0.0f, 200.0f}) {
                    float out[3] = {-1.0f, -1.0f, -1.0f}, direction[3] = {-2.0f, -2.0f, -2.0f};
                    create.Mix(Virtual<uint32_t>(water, 0x40, group, static_cast<const void*>(p), least, 900.0f,
                                                 static_cast<void*>(out), 0u, static_cast<void*>(direction)) &
                               0xFF);
                    create.MixBytes(out, 12);
                    create.MixBytes(direction, 12);
                }
    } else {
        flags |= 2;
    }

    // ---- A new watercraft manager of the test's own ----
    if (!made) {
        flags |= 4;
    } else {
        g_known.push_back({made, 0x70});
        using Seed = void(__thiscall*)(void*, uint32_t);
        reinterpret_cast<Seed>(addr::cRZRandomSuper_Seed)(made + aw::field::kRandom, 5150u);
        // Its vtable: a copy of the game's with slot 15 the test's creator.
        void** const original = At<void**>(made, 0);
        static void* table[68];
        std::memcpy(table, original, sizeof table);
        table[15] = reinterpret_cast<void*>(&FakeCreate);
        g_createPool = pool;
        g_createCount = 10;
        g_createNext = 0;
        g_createRefuse = false;
        g_createHash = &frame;

        using Resize = void(__thiscall*)(void*, uint32_t);
        using InsertFn = void*(__thiscall*)(void*, void*, const void*);
        // Routes and pairs. Route A: key boat 7, dock boat 9's occupant (the
        // dock interface), records of boats 0 (ferry route to the key, word 1,
        // byte 1) and 1 (to boat 6, word 0) and one without a boat. Route B:
        // key boat 6, dock boat 5's occupant (no interface), no records. The
        // first pair table: boat 4's occupant; the second: boat 9's occupant
        // of the type 0x6C36283C without a boat.
        alignas(4) static char records[3][0x3C];
        std::memset(records, 0, sizeof records);
        At<void*>(records[0], 0) = b[0];
        At<void*>(records[0], 4) = b[7];
        At<void*>(records[0], 8) = b[7];
        At<uint32_t>(records[0], 0xC) = 1;
        At<uint8_t>(records[0], 0x38) = 1;
        At<void*>(records[1], 0) = b[1];
        At<void*>(records[1], 8) = b[6];
        At<void*>(records[2], 8) = b[6];
        char* nodeA = nullptr;
        char* nodeB = nullptr;
        char* terminal = nullptr;
        {
            alignas(4) char pair[0x1C] = {};
            alignas(4) char result[12] = {};
            At<void*>(pair, 0) = b[7];
            At<char*>(pair, 0xC) = records[0];
            At<char*>(pair, 0x10) = records[0] + sizeof records;
            At<char*>(pair, 0x14) = records[0] + sizeof records;
            At<void*>(pair, 0x18) = b[9] + 4;
            reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRoute)(made + aw::field::kWaterTable, result,
                                                                                pair);
            nodeA = At<char*>(result, 0);
            alignas(4) char pairB[0x1C] = {};
            At<void*>(pairB, 0) = b[6];
            At<void*>(pairB, 0x18) = b[5] + 4;
            reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRoute)(made + aw::field::kWaterTable, result,
                                                                                pairB);
            nodeB = At<char*>(result, 0);
            void* p2[2] = {b[4] + 4, b[4]};
            char* const pairs2 = made + aw::field::kWaterTable2;
            reinterpret_cast<Resize>(addr::SC4Uint32HashSet_Resize)(pairs2,
                                                                    At<uint32_t>(pairs2, aw::table::kCount) + 1);
            reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertPair)(pairs2, result, p2);
            void* p3[2] = {b[9] + 4, nullptr};
            char* const pairs3 = made + aw::field::kWaterTable3;
            reinterpret_cast<Resize>(addr::SC4Uint32HashSet_Resize)(pairs3,
                                                                    At<uint32_t>(pairs3, aw::table::kCount) + 1);
            reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertPair)(pairs3, result, p3);
            terminal = At<char*>(result, 0);
        }
        if (!nodeA || !nodeB || !terminal)
            flags |= 8;
        boats[9].state.dock = &docks[0];
        docks[0].orientation = 2;
        boats[9].state.type = 0x6C36283C;
        std::memcpy(boats[5].state.position, middle, 12);
        for (int i = 0; i < 10; ++i)
            boats[i].state.value32 = 6.0 + i;
        At<void**>(made, 0) = table;

        // Creation on it: near boat 2 (in its list, radius 8) and outside
        // the city: refused both times (no boat is made by the game).
        Virtual<uint32_t>(made, 0x1C, static_cast<void*>(b[2]));
        std::memcpy(boats[2].state.position, middle, 12);
        {
            using Create = void*(__thiscall*)(void*, uint32_t, const float*, uint32_t, const void*, uint32_t, uint32_t,
                                              uint32_t);
            const uint32_t zeros[3] = {};
            const float farPoint[3] = {-500.0f, 0.0f, -700.0f};
            const float nearPoint[3] = {middle[0] + 4.0f, 0.0f, middle[2]};
            const auto refused = [&](const float* at) {
                return reinterpret_cast<Create>(addr::cSC4WatercraftManager_CreateAutomaton)(made, 0x4418, at, 0x10002,
                                                                                          zeros, 0, 0, 7);
            };
            create.Mix(refused(nearPoint) ? 1u : 0u);
            create.Mix(refused(farPoint) ? 1u : 0u);
            using Open = uint32_t(__thiscall*)(void*, int32_t, int32_t, uint32_t);
            const auto open = reinterpret_cast<Open>(addr::cSC4WatercraftManager_IsOpenWater);
            create.Mix(open(made, cellsX / 2, cellsZ / 2, 2) & 0xFF);
            // A creation point at the building's lot with a route keyed by
            // that lot (its dock the dock interface).
            if (lot) {
                alignas(4) char pair[0x1C] = {};
                alignas(4) char result[12] = {};
                At<void*>(pair, 0) = lot;
                At<void*>(pair, 0x18) = b[9] + 4;
                reinterpret_cast<InsertFn>(addr::cSC4WatercraftManager_InsertRoute)(made + aw::field::kWaterTable,
                                                                                    result, pair);
                float at[3] = {};
                Virtual<void>(building, kGetPosition, static_cast<void*>(at));
                float out[3] = {-1.0f, -1.0f, -1.0f}, direction[3] = {-2.0f, -2.0f, -2.0f};
                create.Mix(Virtual<uint32_t>(made, 0x40, 0x4418u, static_cast<const void*>(at), 0.0f, 900.0f,
                                             static_cast<void*>(out), 0u, static_cast<void*>(direction)) &
                           0xFF);
                create.MixBytes(out, 12);
                create.MixBytes(direction, 12);
                // The route erased again: its key's address (the heap's)
                // would set the table's order for the frame.
                using EraseFn = void(__thiscall*)(void*, void*);
                void* iterator[2] = {At<void*>(result, 0), made + aw::field::kWaterTable};
                if (iterator[0])
                    reinterpret_cast<EraseFn>(addr::cSC4WatercraftManager_EraseRoute)(made + aw::field::kWaterTable,
                                                                                    iterator);
                create.Mix(At<uint32_t>(made + aw::field::kWaterTable, aw::table::kCount));
            }
        }
        // The boat's path (empty, a target then none) and its legs (start 0
        // and 1).
        {
            using SetPath = void(__thiscall*)(void*, PathBytes, void*);
            reinterpret_cast<SetPath>(addr::cSC4Watercraft_SetPath)(b[3], PathBytes{}, b[5] + 4);
            routes.Mix(Canon(At<uint32_t>(b[3], 0x318)));
            routes.Mix(At<uint8_t>(b[3], 0x340));
            reinterpret_cast<SetPath>(addr::cSC4Watercraft_SetPath)(b[3], PathBytes{}, nullptr);
            routes.Mix(Canon(At<uint32_t>(b[3], 0x318)));
            using Ctor = char*(__thiscall*)(void*);
            using SetLegs = void(__thiscall*)(void*, LegBytes, LegBytes, uint32_t);
            for (const uint32_t start : {0u, 1u}) {
                LegBytes one{}, two{};
                reinterpret_cast<Ctor>(addr::cSC4Watercraft_ConstructLeg)(&one);
                reinterpret_cast<Ctor>(addr::cSC4Watercraft_ConstructLeg)(&two);
                one.words[4] = Bits(10.0f + static_cast<float>(start));
                two.words[6] = Bits(20.0f);
                reinterpret_cast<SetLegs>(addr::cSC4Watercraft_SetFerryRoute)(b[3], one, two, start);
                MixLeg(routes, b[3] + 0x1E4);
                MixLeg(routes, b[3] + 0x218);
                MixLeg(routes, b[3] + 0x24C);
                routes.Mix(At<uint8_t>(b[3], 0x2B0));
            }
        }
        // A record's two legs (0x0056FD60): with a boat and without; to a
        // route there (B) and not; walking an empty second path.
        {
            using FromDock = void(__thiscall*)(void*, RouteIterator, char*, char*, char*);
            using Ctor = char*(__thiscall*)(void*);
            const RouteIterator itA = {nodeA, made + aw::field::kWaterTable};
            auto legs = [&](char* record) {
                alignas(4) char one[0x34] = {}, two[0x34] = {};
                reinterpret_cast<Ctor>(addr::cSC4Watercraft_ConstructLeg)(one);
                reinterpret_cast<Ctor>(addr::cSC4Watercraft_ConstructLeg)(two);
                reinterpret_cast<FromDock>(addr::cSC4WatercraftManager_RouteFromDock)(made, itA, record, one, two);
                MixLeg(routes, one);
                MixLeg(routes, two);
                for (char* leg : {one, two}) {
                    if (void* const buffer = At<void*>(leg, 4))
                        release(buffer);
                    if (void* const object = At<void*>(leg, 0))
                        Virtual<uint32_t>(object, 0x08);
                }
            };
            if (nodeA) {
                alignas(4) char record[0x3C] = {};
                At<void*>(record, 4) = b[8];
                At<void*>(record, 8) = b[6];
                legs(record);
                At<void*>(record, 0) = b[3];
                At<void*>(record, 8) = b[8];
                legs(record);
                At<uint8_t>(record, 0x18) = 1;
                legs(record);
            }
        }
        // A new ferry for route A's records (0x00570DF0): the record of byte
        // 1 to nowhere, of byte 0 to route B, a refused creation.
        if (nodeA && nodeB) {
            using Next = uint32_t(__thiscall*)(void*, RouteIterator, RouteIterator, char*);
            const RouteIterator itA = {nodeA, made + aw::field::kWaterTable};
            const RouteIterator itB = {nodeB, made + aw::field::kWaterTable};
            const RouteIterator none = {nullptr, made + aw::field::kWaterTable};
            alignas(4) static char record[0x3C];
            std::memset(record, 0, sizeof record);
            At<void*>(record, 8) = b[6];
            At<uint32_t>(record, 0xC) = 1;
            At<uint8_t>(record, 0x38) = 1;
            routes.Mix(reinterpret_cast<Next>(addr::cSC4WatercraftManager_NextFerryRoute)(made, itA, none, record) &
                       0xFF);
            routes.Mix(Canon(At<uint32_t>(record, 0)));
            routes.Mix(At<uint8_t>(record, 0x38));
            routes.Mix(Canon(At<uint32_t>(nodeA, 8)));
            routes.Mix(reinterpret_cast<Next>(addr::cSC4WatercraftManager_NextFerryRoute)(made, itA, itB, record) &
                       0xFF);
            routes.Mix(Canon(At<uint32_t>(record, 0)));
            routes.Mix(Canon(At<uint32_t>(nodeB, 8)));
            g_createRefuse = true;
            routes.Mix(reinterpret_cast<Next>(addr::cSC4WatercraftManager_NextFerryRoute)(made, itB, itA, record) &
                       0xFF);
            g_createRefuse = false;
            if (void* const object = At<void*>(record, 0)) {
                At<void*>(record, 0) = nullptr;
                Virtual<uint32_t>(object, 0x08);
            }
        }
        // A boat for the terminal (0x005710A0) for the three kinds; a
        // refused creation; a dock without the interface.
        if (terminal) {
            using Plan = uint32_t(__thiscall*)(void*, RouteIterator, uint32_t);
            const RouteIterator it = {terminal, made + aw::field::kWaterTable3};
            for (const uint32_t kind : {0x4416u, 0x4411u, 0x4419u}) {
                routes.Mix(reinterpret_cast<Plan>(addr::cSC4WatercraftManager_PlanRoute)(made, it, kind) & 0xFF);
                routes.Mix(Canon(At<uint32_t>(terminal, 8)));
            }
            g_createRefuse = true;
            routes.Mix(reinterpret_cast<Plan>(addr::cSC4WatercraftManager_PlanRoute)(made, it, 0x4416u) & 0xFF);
            g_createRefuse = false;
            boats[9].state.dock = nullptr;
            routes.Mix(reinterpret_cast<Plan>(addr::cSC4WatercraftManager_PlanRoute)(made, it, 0x4416u) & 0xFF);
            boats[9].state.dock = &docks[0];
            // The terminal without a boat again for the frame.
            if (void* const object = At<void*>(terminal, 8)) {
                At<void*>(terminal, 8) = nullptr;
                Virtual<uint32_t>(object, 0x08);
            }
        }
        // The edge spawn: once (the timer started), then until a boat is
        // made or 40 calls (the draws from the seeded generator).
        {
            using Spawn = void(__thiscall*)(void*, float);
            *reinterpret_cast<uint32_t*>(0x00B22AC4) &= ~1u;
            *reinterpret_cast<int32_t*>(0x00B22ABC) = 0;
            const auto refs = [&] {
                return static_cast<uint32_t>(At<char*>(made, aw::field::kWaterRefs + 4) -
                                             At<char*>(made, aw::field::kWaterRefs));
            };
            uint32_t calls = 0;
            const uint32_t before = refs();
            while (calls < 40 && refs() == before) {
                reinterpret_cast<Spawn>(addr::cSC4WatercraftManager_SpawnAtEdge)(made, 16.0f);
                ++calls;
            }
            frame.Mix(calls);
            frame.Mix(refs());
            frame.Mix(*reinterpret_cast<uint32_t*>(0x00B22ABC));
            frame.Mix(*reinterpret_cast<uint32_t*>(0x00B22AC0));
            logged.Mix(calls);
        }
        // The frame update (slot 63): boats 0 and 2 in the list with paths
        // crossing (ranks 0 and 1), both timers due (the routes' boats let
        // go, route A's record without a boat given a ferry), the terminal
        // planned; then the city's ferry routes asked (+0xA88: route A's
        // records not in the city's list stopped and erased).
        {
            alignas(16) static char pathA[4][16], pathB[4][16];
            std::memset(pathA, 0, sizeof pathA);
            std::memset(pathB, 0, sizeof pathB);
            for (int i = 0; i < 4; ++i) {
                At<int32_t>(pathA[i], 0) = 10 + i;
                At<int32_t>(pathA[i], 4) = 10;
                At<int32_t>(pathB[i], 0) = 12;
                At<int32_t>(pathB[i], 4) = 8 + i;
            }
            Virtual<uint32_t>(made, 0x1C, static_cast<void*>(b[0]));
            At<char*>(b[0], 0x2A4) = pathA[0];
            At<char*>(b[0], 0x2A8) = pathA[0] + sizeof pathA;
            At<char*>(b[2], 0x2A4) = pathB[0];
            At<char*>(b[2], 0x2A8) = pathB[0] + sizeof pathB;
            boats[2].state.groups[0] = 0x4414;
            if (nodeB && !At<void*>(nodeB, 8)) {
                At<void*>(nodeB, 8) = b[8];
                Virtual<uint32_t>(b[8], 0x04);
            }
            *reinterpret_cast<float*>(0x00B22AD4) = 60000.0f;
            *reinterpret_cast<float*>(0x00B22AD0) = 35000.0f;
            At<uint8_t>(made, aw::field::kWaterByte) = 0;
            for (int pass = 0; pass < 2; ++pass) {
                if (pass == 1)
                    At<uint8_t>(made, aw::field::kWaterByte) = 1;
                // Its EAX is the common base's slot 63's, which no caller
                // reads (part aw3's test leaves it out too).
                Virtual<void>(made, 0xFC, 0x11u, Bits(16.0f));
                frame.Mix(At<uint8_t>(made, aw::field::kWaterByte));
                for (const std::size_t at : {aw::field::kWaterTable, aw::field::kWaterTable2, aw::field::kWaterTable3})
                    frame.Mix(At<uint32_t>(made + at, aw::table::kCount));
                if (nodeA) {
                    MixRecords(frame, nodeA + 0x10);
                    frame.Mix(Canon(At<uint32_t>(nodeA, 8)));
                    frame.Mix(Canon(At<uint32_t>(nodeA, 0xC)));
                }
                if (nodeB)
                    frame.Mix(Canon(At<uint32_t>(nodeB, 8)));
                if (terminal)
                    frame.Mix(Canon(At<uint32_t>(terminal, 8)));
                frame.Mix(static_cast<uint32_t>(At<char*>(b[0], 0x2A8) - pathA[0]));
                frame.Mix(static_cast<uint32_t>(At<char*>(b[2], 0x2A8) - pathB[0]));
                frame.Mix(RingCount(At<char*>(made, aw::field::kAutomata)));
            }
            for (char* boat : {b[0], b[2]}) {
                At<char*>(boat, 0x2A4) = nullptr;
                At<char*>(boat, 0x2A8) = nullptr;
            }
        }
        frame.Mix(g_createNext);

        // The game's vtable back, Shutdown (tables, vector and list), the
        // release.
        At<void**>(made, 0) = original;
        frame.Mix(Virtual<uint32_t>(made, 0x10) & 0xFF);
        frame.Mix(Virtual<uint32_t>(made, aw::slot::kRelease));
        boats[9].state.dock = nullptr;
    }
    g_createHash = nullptr;
    for (int i = 0; i < 6; ++i)
        *reinterpret_cast<uint32_t*>(kSaved[i]) = saved[i];
    for (int i = 0; i < 10; ++i) {
        FakeBoat& boat = boats[i];
        routes.Mix(boat.state.refs);
        if (boat.state.unexpected)
            flags |= 0x10;
        boat.Scrub();
    }
    for (FakeBoat& boat : pool) {
        frame.Mix(boat.state.refs);
        if (boat.state.unexpected)
            flags |= 0x20;
        boat.Scrub();
    }
    routes.Mix(docks[0].refs);
    routes.Mix(docks[1].refs);
    char part[64];
    std::snprintf(part, sizeof part, " W%08X,%08X,%08X,%X", create.h, routes.h, frame.h, flags);
    g_aw5Text = part;
    log::Write("autotest: automata: aw5%s logged %08X", part, logged.h);
}

void AwWaterDrive5(cISC4City* city)
{
    g_known.clear();
    // The first building occupant of the controller table by its hash (part
    // aw4's): its lot and position.
    void* building = nullptr;
    uint32_t best = 0xFFFFFFFFu;
    if (char* const manager = Manager(city))
        ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
            void* const occupant = node->occupant;
            if (!occupant || Virtual<uint32_t>(occupant, kGetType) != 0x278128A0u)
                return;
            const uint32_t h = OccupantHash(occupant);
            if (h < best) {
                best = h;
                building = occupant;
            }
        });
    AwWaterRouteDrive(city, building);
    g_known.clear();
}

// ====================================================================================
// Part c1's drive (OPENSC4_TEST_AUTOMATA_SITUATIONS=1), see the top of the file.

std::string g_situationText;

// An object of the test's own with a vtable of 64 slots, of which the
// manager calls AddRef (1), Release (2), slot 4 (one argument; the object
// map's stop) and, for a situation, slots 46, 48, 49 and 51.
struct FakeObject {
    void** vtable;
    uint32_t refs;
    uint32_t stops;
    uint32_t calls;
};

uint32_t __fastcall FakeAddRef(FakeObject* f, void*) { return ++f->refs; }
uint32_t __fastcall FakeRelease(FakeObject* f, void*) { return --f->refs; }
void __fastcall FakeStop(FakeObject* f, void*, uint32_t value) { f->stops += value; }
void __fastcall FakeAura46(FakeObject* f, void*, float* a, float* b)
{
    *a = 1.5f;
    *b = -2.25f;
    f->calls += 1;
}
void __fastcall FakeMoney48(FakeObject* f, void*, int64_t* money)
{
    *money = 123456789012LL;
    f->calls += 0x10;
}
void __fastcall FakeAura49(FakeObject* f, void*, float* a, float* b)
{
    *a = 3.0f;
    *b = 4.5f;
    f->calls += 0x100;
}
void __fastcall FakeMoney51(FakeObject* f, void*, int64_t* money)
{
    *money = -77;
    f->calls += 0x1000;
}

void** FakeObjectVtable()
{
    static void* table[64] = {};
    table[1] = reinterpret_cast<void*>(&FakeAddRef);
    table[2] = reinterpret_cast<void*>(&FakeRelease);
    table[4] = reinterpret_cast<void*>(&FakeStop);
    table[46] = reinterpret_cast<void*>(&FakeAura46);
    table[48] = reinterpret_cast<void*>(&FakeMoney48);
    table[49] = reinterpret_cast<void*>(&FakeAura49);
    table[51] = reinterpret_cast<void*>(&FakeMoney51);
    return table;
}

uint32_t FakeState(const FakeObject& f) { return f.refs | f.stops << 8 | f.calls << 16; }

// A node pushed at the head of its bucket in one of the manager's hash
// sets or maps (+4 the key, +8 the value for a map), the count up.
void PushNode(char* table, uint32_t key, void* value, uint32_t size)
{
    using Allocate = void*(__cdecl*)(uint32_t);
    char* const node = static_cast<char*>(reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(size));
    std::memset(node, 0, size);
    char** const begin = At<char**>(table, 4);
    const uint32_t buckets = static_cast<uint32_t>(At<char**>(table, 8) - begin);
    At<uint32_t>(node, 4) = key;
    if (size >= 0xC)
        At<void*>(node, 8) = value;
    At<char*>(node, 0) = begin[key % buckets];
    begin[key % buckets] = node;
    ++At<uint32_t>(table, 0x10);
}

// A node of 0x14 bytes at the end of the manager's list (+0xA04).
void PushListNode(char* manager, void* a, void* b, uint32_t id)
{
    namespace sf = automata_situations::field;
    using Allocate = void*(__cdecl*)(uint32_t);
    char* const node = static_cast<char*>(reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(0x14));
    char* const head = At<char*>(manager, sf::kList);
    At<void*>(node, 8) = a;
    At<void*>(node, 0xC) = b;
    At<uint32_t>(node, 0x10) = id;
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = At<char*>(head, 4);
    At<char*>(At<char*>(head, 4), 0) = node;
    At<char*>(head, 4) = node;
}

// The getters a manager answers the same on both sides (slots 7, 10 to 15
// and 27, 30 and 32), the pointers of 6, 8 and 9 as null or not.
void MixGetters(Hash& h, char* m)
{
    namespace sl = automata_situations::slot;
    h.Mix(Virtual<void*>(m, sl::kGetActiveSituation) ? 1u : 0u);
    h.Mix(Virtual<uint32_t>(m, sl::kGetCurrentStatus));
    h.Mix(Virtual<void*>(m, sl::kGetActiveAuto) ? 1u : 0u);
    h.Mix(Virtual<void*>(m, sl::kGetCurrentTarget) ? 1u : 0u);
    h.Mix(Virtual<uint32_t>(m, sl::kGetCurrentTargetIndex));
    h.Mix(Virtual<uint32_t>(m, sl::kGetNumTargets));
    h.Mix(Virtual<uint32_t>(m, sl::kGetNumTargetsRemaining));
    h.Mix(Bits(Virtual<float>(m, sl::kGetCurrentTimeLimitSeconds)));
    h.Mix(Bits(Virtual<float>(m, sl::kGetCurrentSituationTime)));
    h.Mix(Bits64(Virtual<double>(m, sl::kGetCurrentTimeRemainingSeconds)));
    h.Mix(Virtual<uint8_t>(m, sl::kGetMaxCsi));
    h.Mix(Virtual<uint8_t>(m, sl::kGetCsiVisible));
    h.Mix(Virtual<uint32_t>(m, sl::kCountdownIsInProgress));
}

// Slot 16's string, hashed, its buffer freed as the game's SC4String
// destructor would.
uint32_t TargetNameHash(char* m)
{
    struct {
        std::uintptr_t vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t extra;
    } name{};
    Virtual<void*>(m, automata_situations::slot::kGetCurrentTargetName, static_cast<void*>(&name));
    Hash h;
    h.Mix(name.vtable == addr::SC4String_Vtable ? 1u : 0u);
    h.Mix(NameHash(name.begin, name.end));
    h.Mix(name.extra);
    if (name.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(name.begin);
    return h.h;
}

// Slots 17 and 18: the AL answers and what they wrote.
void MixAuraMoney(Hash& h, char* m)
{
    namespace sl = automata_situations::slot;
    float a = -1.0f, b = -1.0f;
    int64_t money = -1;
    h.Mix(Virtual<uint8_t>(m, sl::kGetSituationAura, &a, &b));
    h.Mix(Bits(a));
    h.Mix(Bits(b));
    h.Mix(Virtual<uint8_t>(m, sl::kGetSituationMoney, &money));
    h.MixBytes(&money, sizeof money);
}

// The success map's (+0xA34) entries summed, with slot 5 on each key: its
// AX and whether its high half is the bucket vector's.
uint32_t SuccessHash(char* m, uint32_t& entries)
{
    namespace sf = automata_situations::field;
    char* const table = m + sf::kSuccesses;
    const uint32_t high = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(At<char*>(table, 4))) >> 16;
    uint32_t sum = 0;
    entries = 0;
    ForEachScriptNode(table, [&](char* node) {
        const uint32_t key = At<uint32_t>(node, 4);
        const uint32_t answer = Virtual<uint32_t>(m, automata_situations::slot::kGetMissionSuccessCount, key);
        Hash e;
        e.Mix(key);
        e.Mix(At<uint16_t>(node, 8));
        e.Mix(answer & 0xFFFF);
        e.Mix(answer >> 16 == high ? 1u : 0u);
        sum += e.h;
        ++entries;
    });
    Hash h;
    h.Mix(entries);
    h.Mix(sum);
    const uint32_t missing = Virtual<uint32_t>(m, automata_situations::slot::kGetMissionSuccessCount, 0x7A5A0000u);
    h.Mix(missing & 0xFFFF);
    h.Mix(missing >> 16 == high ? 1u : 0u);
    return h.h;
}

void SituationsDrive(cISC4City* city)
{
    namespace sf = automata_situations::field;
    namespace sl = automata_situations::slot;
    char* const m = Virtual<char*>(city, 0xC4); // GetCitySituationManager
    if (!m) {
        g_situationText = " C-";
        return;
    }
    Hash identity, queries, save, fresh;
    uint32_t flags = 0;
    // QueryInterface on the object and through the adjustors of its parts;
    // each AddRef given back through the answer's slot 2.
    const uint32_t iids[] = {automata_situations::kManagerIid, 1u, 0x452294AAu, 0xE98B2F57u, 0xE4FDA3D4u, 0x12345678u};
    for (const std::size_t at : {std::size_t{0}, sf::kVtable4, sf::kVtable8, sf::kSerializable, sf::kVtable10})
        for (const uint32_t iid : iids) {
            void* out = nullptr;
            const uint8_t answer = Virtual<uint8_t>(m + at, sl::kQueryInterface, iid, &out);
            identity.Mix(answer);
            identity.Mix(out ? static_cast<uint32_t>(static_cast<char*>(out) - m) : 0xFFFFFFFFu);
            if (answer && out)
                Virtual<uint32_t>(out, sl::kRelease);
        }
    const uint32_t up = Virtual<uint32_t>(m, sl::kAddRef);
    const uint32_t down = Virtual<uint32_t>(m, sl::kRelease);
    identity.Mix(up - down);
    identity.Mix(Virtual<uint32_t>(m + sf::kSerializable, 0x14)); // GetGZCLSID
    identity.Mix(At<uint8_t>(m, sf::kInitialized));
    // The queries.
    uint32_t successes = 0;
    queries.Mix(SuccessHash(m, successes));
    MixGetters(queries, m);
    queries.Mix(TargetNameHash(m));
    MixAuraMoney(queries, m);
    const uint32_t tracked = Virtual<uint32_t>(m, sl::kGetTrackedAutomataCount);
    // Write into a stream of the test's own.
    using Serial = uint32_t(__thiscall*)(void* part, void* stream);
    ByteOStream written;
    const uint32_t wrote = reinterpret_cast<Serial>(addr::cSC4CitySituationManager_Write)(
        m + sf::kSerializable, static_cast<cIGZOStream*>(&written));
    save.Mix(wrote);
    save.Mix(static_cast<uint32_t>(written.bytes.size()));
    save.MixBytes(written.bytes.data(), written.bytes.size());
    // A fresh manager from the factory, held by one reference.
    using Factory = char*(__cdecl*)();
    char* const made = reinterpret_cast<Factory>(addr::cSC4CitySituationManager_Create)();
    FakeObject fakes[6] = {};
    for (FakeObject& f : fakes)
        f.vtable = FakeObjectVtable();
    if (made) {
        flags |= 1;
        Virtual<uint32_t>(made, sl::kAddRef);
        void* out = nullptr;
        const uint8_t qi = Virtual<uint8_t>(made, sl::kQueryInterface, automata_situations::kManagerIid, &out);
        fresh.Mix(qi);
        if (qi && out)
            Virtual<uint32_t>(out, sl::kRelease);
        fresh.Mix(At<uint32_t>(made, sf::kStatus));
        fresh.Mix(At<uint8_t>(made, sf::kCsiVisible));
        // Read the city's bytes and write them again.
        {
            ByteIStream in(written.bytes);
            fresh.Mix(reinterpret_cast<Serial>(addr::cSC4CitySituationManager_Read)(made + sf::kSerializable,
                                                                                    static_cast<cIGZIStream*>(&in)));
            fresh.Mix(At<uint8_t>(made, sf::kRead));
            ByteOStream again;
            reinterpret_cast<Serial>(addr::cSC4CitySituationManager_Write)(made + sf::kSerializable,
                                                                          static_cast<cIGZOStream*>(&again));
            if (again.bytes == written.bytes)
                flags |= 2;
        }
        // Version 3 (refused), version 1 (no visibility) and a cut stream.
        {
            std::vector<uint8_t> v3 = {3, 0};
            ByteIStream in3(v3);
            fresh.Mix(reinterpret_cast<Serial>(addr::cSC4CitySituationManager_Read)(made + sf::kSerializable,
                                                                                    static_cast<cIGZIStream*>(&in3)) &
                      0xFF);
            std::vector<uint8_t> v1 = {1, 0, 2, 0, 0, 0, 0x01, 0, 0x5A, 0x7A, 7, 0, 0x02, 0, 0x5A, 0x7A, 9, 0};
            ByteIStream in1(v1);
            At<uint8_t>(made, sf::kCsiVisible) = 0x55;
            fresh.Mix(reinterpret_cast<Serial>(addr::cSC4CitySituationManager_Read)(made + sf::kSerializable,
                                                                                    static_cast<cIGZIStream*>(&in1)));
            fresh.Mix(At<uint8_t>(made, sf::kCsiVisible));
            uint32_t entries = 0;
            fresh.Mix(SuccessHash(made, entries));
            std::vector<uint8_t> cut = {2, 0, 3, 0, 0, 0, 0x01, 0, 0x5A, 0x7A, 7, 0, 0x02, 0};
            ByteIStream inCut(cut);
            fresh.Mix(reinterpret_cast<Serial>(addr::cSC4CitySituationManager_Read)(made + sf::kSerializable,
                                                                                    static_cast<cIGZIStream*>(&inCut)));
            fresh.Mix(SuccessHash(made, entries));
            At<uint8_t>(made, sf::kCsiVisible) = 1;
        }
        // The success map's operator[] and insert.
        {
            char* const table = made + sf::kSuccesses;
            using At16 = uint16_t*(__thiscall*)(void*, const uint32_t*);
            const auto at = reinterpret_cast<At16>(addr::cSC4CitySituationManager_SuccessAt);
            const uint32_t newKey = 0x7A5A0100u, oldKey = 0x7A5A0001u;
            uint16_t* const p = at(table, &newKey);
            fresh.Mix(p ? *p : 0xFFFFu);
            if (p)
                *p = 41;
            uint16_t* const q = at(table, &newKey);
            fresh.Mix(p == q ? 1u : 0u);
            fresh.Mix(q ? *q : 0xFFFFu);
            fresh.Mix(Virtual<uint32_t>(made, sl::kGetMissionSuccessCount, newKey) & 0xFFFF);
            struct {
                uint32_t key;
                uint16_t value;
                uint16_t pad;
            } pair{oldKey, 99, 0};
            struct {
                char* node;
                char* table;
                uint8_t inserted;
                uint8_t pad[3];
            } result{};
            using Insert = void*(__thiscall*)(void*, void*, const void*);
            const auto insert = reinterpret_cast<Insert>(addr::cSC4CitySituationManager_SuccessInsertUnique);
            fresh.Mix(insert(table, &result, &pair) == &result ? 1u : 0u);
            fresh.Mix(result.inserted | (result.table == table ? 2u : 0u) |
                      (result.node && At<uint32_t>(result.node, 4) == oldKey ? 4u : 0u));
            pair.key = 0x7A5A0200u;
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4Uint32HashSet_Resize)(
                table, At<uint32_t>(table, 0x10) + 1);
            insert(table, &result, &pair);
            fresh.Mix(result.inserted | (result.node && At<uint16_t>(result.node, 8) == 99 ? 2u : 0u));
            uint32_t entries = 0;
            fresh.Mix(SuccessHash(made, entries));
            fresh.Mix(entries);
        }
        // The getters over fields set by the test, the target a building.
        void* building = nullptr;
        if (char* const ctl = Manager(city)) {
            uint32_t best = 0;
            ForEach<ControllerNode>(Table(ctl, field::kControllers), [&](ControllerNode* node) {
                if (!node->occupant)
                    return;
                const uint32_t type = Virtual<uint32_t>(node->occupant, kGetType);
                if (type == kTypeAutomaton || type == kTypeAutomaton2 || type == kTypeProp)
                    return;
                const uint32_t h = OccupantHash(node->occupant);
                if (!building || h < best) {
                    building = node->occupant;
                    best = h;
                }
            });
        }
        At<uint32_t>(made, sf::kNumTargets) = 5;
        At<uint32_t>(made, sf::kTargetIndex) = 2;
        At<float>(made, sf::kTimeLimit) = 90.25f;
        At<float>(made, sf::kSituationTime) = 12.1f;
        At<float>(made, sf::kCountdown) = 3.0f;
        At<void*>(made, sf::kTarget) = building;
        MixGetters(fresh, made);
        fresh.Mix(TargetNameHash(made));
        At<void*>(made, sf::kTarget) = nullptr;
        fresh.Mix(TargetNameHash(made));
        At<float>(made, sf::kCountdown) = 0.0f;
        fresh.Mix(Virtual<uint32_t>(made, sl::kCountdownIsInProgress));
        At<uint32_t>(made, sf::kCountdown) = 0x7FC00000u; // a NaN
        fresh.Mix(Virtual<uint32_t>(made, sl::kCountdownIsInProgress));
        At<float>(made, sf::kCountdown) = 0.0f;
        At<uint32_t>(made, sf::kNumTargets) = 0;
        At<uint32_t>(made, sf::kTargetIndex) = 0;
        // Slots 17 and 18: no situation, then one of the test's own with the
        // statuses 2 and 4.
        MixAuraMoney(fresh, made);
        At<void*>(made, sf::kSituation) = &fakes[0];
        At<uint32_t>(made, sf::kStatus) = 2;
        MixAuraMoney(fresh, made);
        At<uint32_t>(made, sf::kStatus) = 4;
        MixAuraMoney(fresh, made);
        At<void*>(made, sf::kSituation) = nullptr;
        fresh.Mix(fakes[0].calls);
        // Slots 26 to 31 (the most CSIs is a global: put back after).
        const uint8_t most = Virtual<uint8_t>(made, sl::kGetMaxCsi);
        Virtual<void>(made, sl::kSetMaxCsi, 7u);
        fresh.Mix(Virtual<uint8_t>(made, sl::kGetMaxCsi));
        fresh.Mix(Virtual<uint8_t>(made, sl::kHandleTuningParametersChanged));
        fresh.Mix(Virtual<uint8_t>(made, sl::kGetMaxCsi));
        Virtual<void>(made, sl::kSetMaxCsi, static_cast<uint32_t>(most));
        fresh.Mix(Virtual<uint8_t>(made, sl::kGetMaxCsi) == most ? 1u : 0u);
        Virtual<void>(made, sl::kSetCsiVisible, 0x100u);
        fresh.Mix(Virtual<uint8_t>(made, sl::kGetCsiVisible));
        Virtual<void>(made, sl::kSetCsiVisible, 1u);
        fresh.Mix(Virtual<uint8_t>(made, sl::kGetCsiVisible));
        // The tracked automata (+0xA08), the object map (+0xAC4) and the list
        // (+0xA04) with objects of the test's own.
        using Life = void(__thiscall*)(void*);
        PushNode(made + sf::kTracked, reinterpret_cast<std::uintptr_t>(&fakes[1]), nullptr, 8);
        PushNode(made + sf::kTracked, reinterpret_cast<std::uintptr_t>(&fakes[2]), nullptr, 8);
        fakes[1].refs = fakes[2].refs = 1;
        fresh.Mix(Virtual<uint32_t>(made, sl::kGetTrackedAutomataCount));
        reinterpret_cast<Life>(addr::cSC4CitySituationManager_ReleaseTracked)(made);
        fresh.Mix(Virtual<uint32_t>(made, sl::kGetTrackedAutomataCount));
        PushNode(made + sf::kObjects, 3u, &fakes[3], 0xC);
        PushNode(made + sf::kObjects, 104u, &fakes[4], 0xC);
        fakes[3].refs = fakes[4].refs = 1;
        reinterpret_cast<Life>(addr::cSC4CitySituationManager_ReleaseObjects)(made);
        fresh.Mix(At<uint32_t>(made + sf::kObjects, 0x10));
        PushListNode(made, &fakes[5], &fakes[0], 0x1234u);
        PushListNode(made, nullptr, &fakes[5], 0u);
        fakes[5].refs = 2;
        fakes[0].refs = 1;
        reinterpret_cast<Life>(addr::cSC4CitySituationManager_ReleaseList)(made);
        char* const head = At<char*>(made, sf::kList);
        fresh.Mix(At<char*>(head, 0) == head && At<char*>(head, 4) == head ? 1u : 0u);
        PushListNode(made, &fakes[1], &fakes[2], 7u);
        fakes[1].refs = fakes[2].refs = 1;
        reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4CitySituationManager_ClearList)(made + sf::kList);
        fresh.Mix(At<char*>(head, 0) == head ? 1u : 0u);
        for (const FakeObject& f : fakes)
            fresh.Mix(FakeState(f));
        // ResetTutorials, when the preferences' done tutorials are all zero.
        bool reset = false;
        if (void* const app = Global<void*>(addr::SC4_Service43C94))
            if (char* const preferences = Virtual<char*>(app, 0x98)) {
                uint8_t zero[0x100] = {};
                if (std::memcmp(preferences + 0x1334, zero, sizeof zero) == 0) {
                    Virtual<void>(made, sl::kResetTutorials);
                    reset = true;
                }
            }
        if (reset) {
            flags |= 4;
            uint32_t entries = 0;
            fresh.Mix(SuccessHash(made, entries));
        }
        // The release: Shutdown (not initialized), the deleting destructor.
        fresh.Mix(Virtual<uint32_t>(made, sl::kRelease));
    }
    char text[96];
    std::snprintf(text, sizeof text, " C%08X,%08X,%08X,%08X,%X", identity.h, queries.h, save.h, fresh.h, flags);
    g_situationText = text;
    log::Write("autotest: automata: situations: identity %08X (references %u) queries %08X (successes %u, tracked %u, "
               "status %u) save %08X (%u bytes, answer %u) fresh %08X flags %X",
               identity.h, up, queries.h, successes, tracked, At<uint32_t>(m, sf::kStatus), save.h,
               static_cast<unsigned>(written.bytes.size()), wrote, fresh.h, flags);
}

// Part c2's drive (OPENSC4_TEST_AUTOMATA_SITUATIONS=2), see the top of the
// file. Only objects of the test's own are handed to the functions: as
// arguments, or in the fields of a fresh manager whose vtable is a copy with
// slots 24, 36 and 38 the test's (no situation is started and no message
// sent). The game's globals are read, never written.

std::string g_situation2Text;

namespace c2 {

Hash g_log;

template <typename R, typename... Args>
R This(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

struct Fake {
    void** vtable;
    uint32_t refs;
    uint32_t index;
    uint32_t flags;
    uint32_t kind;
    uint32_t answer;
    uint32_t value;
    void* other;
    void* other2;
    float pos[3];
    int32_t rect[4];
    uint32_t* groupsBegin;
    uint32_t* groupsEnd;
    uint32_t* groupsCap;
    uint32_t groups[4];
    double number;
};

constexpr int kFakes = 24;
Fake g_fakes[kFakes];
// Part c3's further fakes (its test only).
Fake g_more[kFakes];
char* g_fresh = nullptr;

// A pointer as the hashes see it: a fake's index + 1 (0x41 on for part c3's
// further ones), 0 for null, 0xFE for the fresh manager (or one of its
// parts), 0xFF for anything else.
uint32_t Id(const void* p)
{
    if (!p)
        return 0;
    for (int i = 0; i < kFakes; ++i)
        if (p == &g_fakes[i])
            return static_cast<uint32_t>(i + 1);
    for (int i = 0; i < kFakes; ++i)
        if (p == &g_more[i])
            return static_cast<uint32_t>(0x41 + i);
    if (g_fresh && p >= g_fresh && p < g_fresh + automata_situations::kObjectSize)
        return 0xFE;
    return 0xFF;
}

bool g_logEach = false; // part c3's trace, level 2

void Log(const Fake* f, uint32_t slot, uint32_t a = 0, uint32_t b = 0)
{
    if (g_logEach)
        log::Write("autotest: c3 call %02X %03X %08X %08X", f->index, slot, a, b);
    g_log.Mix(f->index << 16 | slot);
    g_log.Mix(a);
    g_log.Mix(b);
}

uint32_t __fastcall QI(Fake* f, void*, uint32_t iid, void** out)
{
    Log(f, 0x00, iid);
    if (iid != 0xA9B40F05u)
        return 0;
    *out = f;
    ++f->refs;
    return 1;
}
uint32_t __fastcall AddRef(Fake* f, void*) { return ++f->refs; }
uint32_t __fastcall Release(Fake* f, void*) { return --f->refs; }

// The binding, the arguments, a value and the results of the Lua callbacks.
void* __fastcall BindUp(Fake* f, void*, void* a1)
{
    Log(f, 0x20, Id(a1));
    return a1;
}
void* __fastcall BindManager(Fake* f, void*, void* up)
{
    Log(f, 0x48, Id(up));
    return f->other;
}
void* __fastcall ArgAt(Fake* f, void*, uint32_t i)
{
    Log(f, 0x14, i);
    return static_cast<Fake*>(f->other) + i;
}
void* __fastcall ArgAt6(Fake* f, void*, uint32_t i)
{
    Log(f, 0x18, i);
    return f->answer ? static_cast<Fake*>(f->other) + i : nullptr;
}
uint32_t __fastcall ValueType(Fake* f, void*)
{
    Log(f, 0x0C);
    return 0;
}
uint32_t __fastcall ValueRead(Fake* f, void*, uint32_t* p)
{
    Log(f, 0x64);
    *p = f->value;
    return f->answer;
}
uint32_t __fastcall ValuePush(Fake* f, void*, uint32_t v)
{
    Log(f, 0x68, v);
    return 0x12345600u;
}
uint32_t __fastcall ValueBool(Fake* f, void*, uint32_t* p)
{
    Log(f, 0x28);
    *reinterpret_cast<uint8_t*>(p) = static_cast<uint8_t>(f->value);
    return 1;
}
uint32_t __fastcall ResultPush(Fake* f, void*, uint32_t count, char* number)
{
    const uint16_t type = At<uint16_t>(number, 0xE);
    const uint32_t value = At<uint32_t>(number, 4);
    Log(f, 0x1C, count, type);
    g_log.Mix(type == 1 || type == 4 || type == 6 ? value : (value ? 1u : 0u));
    return 1;
}

// A situation (or advice): slots 1, 2, 3, 4, 7, 10, 15, 18, 30, 53, 54, 55.
void* __fastcall SitInner(Fake* f, void*)
{
    Log(f, 0x0C);
    return f->other;
}
uint32_t __fastcall SitKind(Fake* f, void*)
{
    Log(f, 0x10);
    return f->kind;
}
uint32_t __fastcall SitValue1C(Fake* f, void*)
{
    Log(f, 0x1C);
    return f->value;
}
uint32_t __fastcall SitValue28(Fake* f, void*)
{
    Log(f, 0x28);
    return f->value;
}
uint32_t __fastcall SitFlag(Fake* f, void*, uint32_t flag)
{
    Log(f, 0x3C, flag);
    return (f->flags & flag) ? 1u : 0u;
}
void* __fastcall SitGroups(Fake* f, void*)
{
    Log(f, 0x48);
    return &f->groupsBegin;
}
uint32_t __fastcall SitTable(Fake* f, void*)
{
    Log(f, 0x78);
    return f->value;
}
uint32_t __fastcall SitRelevant(Fake* f, void*, void* x)
{
    Log(f, 0xD4, Id(x));
    return f->answer & 1;
}
uint32_t __fastcall SitGroupRelevant(Fake* f, void*, uint32_t** v)
{
    Log(f, 0xD8, static_cast<uint32_t>(v[1] - v[0]), v[1] != v[0] ? *v[0] : 0u);
    return (f->answer >> 1) & 1;
}
void __fastcall SitStep(Fake* f, void*) { Log(f, 0xDC); }

// An automaton or occupant.
void* __fastcall AutoOther(Fake* f, void*)
{
    Log(f, 0x0C);
    return f->other;
}
void* __fastcall AutoSelf(Fake* f, void*)
{
    Log(f, 0x14);
    return f;
}
void __fastcall AutoPos1C(Fake* f, void*, float* out)
{
    Log(f, 0x1C);
    std::memcpy(out, f->pos, sizeof f->pos);
}
void __fastcall AutoPos20(Fake* f, void*, float* out)
{
    Log(f, 0x20);
    std::memcpy(out, f->pos, sizeof f->pos);
}
void __fastcall AutoRemove(Fake* f, void*, uint32_t v) { Log(f, 0x34, v); }
void __fastcall AutoGroup(Fake* f, void*, uint32_t g) { Log(f, 0x5C, g); }
uint32_t __fastcall AutoFlag(Fake* f, void*, uint32_t flag)
{
    Log(f, 0x7C, flag);
    return (f->flags & flag) ? 1u : 0u;
}
uint32_t __fastcall AutoKind(Fake* f, void*)
{
    Log(f, 0xEC);
    return f->kind;
}
void* __fastcall AutoHolder(Fake* f, void*)
{
    Log(f, 0x10C);
    return f->other2;
}
void __fastcall AutoTarget9C(Fake* f, void*, void* t) { Log(f, 0x9C, Id(t)); }
void __fastcall AutoTargetA0(Fake* f, void*, void* t) { Log(f, 0xA0, Id(t)); }

// A helper (+0xA9C, +0xAA0, +0xAF8).
void __fastcall HelpStop(Fake* f, void*, uint32_t v) { Log(f, 0x10, v); }
uint32_t __fastcall HelpOn(Fake* f, void*)
{
    Log(f, 0x14);
    return f->answer & 1;
}
uint32_t __fastcall HelpOff(Fake* f, void*)
{
    Log(f, 0x18);
    return (f->answer >> 1) & 1;
}
void* __fastcall HelpSource(Fake* f, void*)
{
    Log(f, 0x2C);
    return f->other;
}
void __fastcall HelpTransform(Fake* f, void*, char* t)
{
    Log(f, 0x3C, At<uint32_t>(t, 4), At<uint32_t>(t, 0x34));
    std::memcpy(t + 0x28, f->pos, sizeof f->pos);
}
void __fastcall HelpTouch(Fake* f, void*) { Log(f, 0x110); }

// A service of +0x9F8.
uint32_t __fastcall SvcHandles(Fake* f, void*, uint32_t g)
{
    Log(f, 0x78, g);
    return (g == f->value) ? 1u : 0u;
}
void __fastcall SvcAdd(Fake* f, void*, uint32_t g, char* set)
{
    Log(f, 0x80, g);
    if (!f->other)
        return;
    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(f->other);
    alignas(4) char result[0xC] = {};
    This<void>(addr::SC4Uint32HashSet_InsertUnique, set, static_cast<void*>(result), static_cast<void*>(&key));
}
void __fastcall SvcDone(Fake* f, void*, char* set) { Log(f, 0x84, At<uint32_t>(set, 0x10)); }
uint32_t __fastcall SvcPlace(Fake* f, void*, uint32_t g, float* from, uint32_t zero, uint32_t range, float* to,
                             uint32_t mask, uint32_t z2)
{
    Log(f, 0x40, g, range);
    g_log.Mix(mask ^ zero ^ z2);
    g_log.MixBytes(from, 12);
    if (f->answer & 1)
        std::memcpy(to, f->pos, sizeof f->pos);
    return f->answer & 1;
}
void* __fastcall SvcMake(Fake* f, void*, uint32_t g, float* to, uint32_t mask, void* vector, uint32_t a, uint32_t b,
                         uint32_t c)
{
    Log(f, 0x3C, g, mask);
    g_log.MixBytes(to, 12);
    g_log.Mix(a ^ b ^ c ^ (vector ? 1u : 0u));
    return f->other2;
}

// A script context.
uint32_t __fastcall LuaTop(Fake* f, void*)
{
    Log(f, 0x18);
    return 7;
}
void __fastcall LuaSetTop(Fake* f, void*, uint32_t t) { Log(f, 0x1C, t); }
void __fastcall LuaPushRef(Fake* f, void*, uint32_t r) { Log(f, 0xAC, r); }
uint32_t __fastcall LuaIsTable(Fake* f, void*, uint32_t i)
{
    Log(f, 0x11C, i);
    return f->answer & 1;
}
void __fastcall LuaPushNumber(Fake* f, void*, double d)
{
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    Log(f, 0x7C, static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32));
}
void __fastcall LuaCall(Fake* f, void*, uint32_t a, uint32_t b) { Log(f, 0xCC, a, b); }
uint32_t __fastcall LuaIsNumber(Fake* f, void*, uint32_t i)
{
    Log(f, 0x38, i);
    return (f->answer >> 1) & 1;
}
double __fastcall LuaToNumber(Fake* f, void*, uint32_t i)
{
    Log(f, 0x5C, i);
    return f->number;
}

// A lot manager (its lot in other) or, with kind 1, a lot (its rect).
void* __fastcall LotOf(Fake* f, void*, void* x)
{
    Log(f, 0x38, Id(x));
    if (!f->kind)
        return f->other;
    std::memcpy(x, f->rect, sizeof f->rect);
    return nullptr;
}

// The fresh manager's slots 24, 36 and 38.
uint32_t g_startAnswer = 0;
void __fastcall StubJoyride(char* m, void*, void* a, uint32_t flag, void* t)
{
    g_log.Mix(0x240000 | Id(m));
    g_log.Mix(Id(a));
    g_log.Mix(flag & 0xFF);
    g_log.Mix(Id(t));
}
uint32_t __fastcall StubStart(char* m, void*, void* advice, void* occupant, uint32_t flag)
{
    g_log.Mix(0x360000 | Id(m));
    g_log.Mix(Id(advice));
    g_log.Mix(Id(occupant));
    g_log.Mix(flag & 0xFF);
    return g_startAnswer;
}
void __fastcall StubEnd(char* m, void*) { g_log.Mix(0x380000 | Id(m)); }

using Slot = std::pair<std::size_t, void*>;

void** MakeVtable(void** table, std::initializer_list<Slot> slots)
{
    table[1] = reinterpret_cast<void*>(&AddRef);
    table[2] = reinterpret_cast<void*>(&Release);
    for (const Slot& s : slots)
        table[s.first / 4] = s.second;
    return table;
}

#define C2_SLOT(off, fn) Slot{off, reinterpret_cast<void*>(&fn)}

void** BindingVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x20, BindUp), C2_SLOT(0x48, BindManager)});
}
void** ArgumentsVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x14, ArgAt), C2_SLOT(0x18, ArgAt6)});
}
void** ValueVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x0C, ValueType), C2_SLOT(0x28, ValueBool), C2_SLOT(0x64, ValueRead),
                          C2_SLOT(0x68, ValuePush)});
}
void** ResultsVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x1C, ResultPush)});
}
void** SituationVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x0C, SitInner), C2_SLOT(0x10, SitKind), C2_SLOT(0x1C, SitValue1C),
                          C2_SLOT(0x28, SitValue28), C2_SLOT(0x3C, SitFlag), C2_SLOT(0x48, SitGroups),
                          C2_SLOT(0x78, SitTable), C2_SLOT(0xD4, SitRelevant), C2_SLOT(0xD8, SitGroupRelevant),
                          C2_SLOT(0xDC, SitStep)});
}
void** AutomatonVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x00, QI), C2_SLOT(0x0C, AutoOther), C2_SLOT(0x14, AutoSelf),
                          C2_SLOT(0x1C, AutoPos1C), C2_SLOT(0x20, AutoPos20), C2_SLOT(0x34, AutoRemove),
                          C2_SLOT(0x5C, AutoGroup), C2_SLOT(0x7C, AutoFlag), C2_SLOT(0x9C, AutoTarget9C),
                          C2_SLOT(0xA0, AutoTargetA0), C2_SLOT(0xEC, AutoKind), C2_SLOT(0x10C, AutoHolder)});
}
void** HelperVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x10, HelpStop), C2_SLOT(0x14, HelpOn), C2_SLOT(0x18, HelpOff),
                          C2_SLOT(0x2C, HelpSource), C2_SLOT(0x3C, HelpTransform), C2_SLOT(0x110, HelpTouch)});
}
void** ServiceVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x3C, SvcMake), C2_SLOT(0x40, SvcPlace), C2_SLOT(0x78, SvcHandles),
                          C2_SLOT(0x80, SvcAdd), C2_SLOT(0x84, SvcDone)});
}
void** ContextVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x18, LuaTop), C2_SLOT(0x1C, LuaSetTop), C2_SLOT(0x38, LuaIsNumber),
                          C2_SLOT(0x5C, LuaToNumber), C2_SLOT(0x7C, LuaPushNumber), C2_SLOT(0xAC, LuaPushRef),
                          C2_SLOT(0xCC, LuaCall), C2_SLOT(0x11C, LuaIsTable)});
}
void** LotVtable()
{
    static void* t[80] = {};
    return MakeVtable(t, {C2_SLOT(0x38, LotOf)});
}

#undef C2_SLOT

Fake* Make(int i, void** vtable)
{
    Fake& f = g_fakes[i];
    std::memset(&f, 0, sizeof f);
    f.vtable = vtable;
    f.refs = 100;
    f.index = static_cast<uint32_t>(i + 1);
    f.groupsBegin = f.groups;
    f.groupsEnd = f.groups;
    f.groupsCap = f.groups + 4;
    return &f;
}

void SetPos(Fake* f, float x, float y, float z)
{
    f->pos[0] = x;
    f->pos[1] = y;
    f->pos[2] = z;
}

// A node of 0x14 bytes at the end of the manager's list (+0xA04).
char* PushEntry(char* manager, void* entry, void* automaton, uint32_t id)
{
    namespace sf = automata_situations::field;
    using Allocate = void*(__cdecl*)(uint32_t);
    char* const node = static_cast<char*>(reinterpret_cast<Allocate>(addr::SC4_stl_allocate)(0x14));
    char* const head = At<char*>(manager, sf::kList);
    At<void*>(node, 8) = entry;
    At<void*>(node, 0xC) = automaton;
    At<uint32_t>(node, 0x10) = id;
    At<char*>(node, 0) = head;
    At<char*>(node, 4) = At<char*>(head, 4);
    At<char*>(At<char*>(head, 4), 0) = node;
    At<char*>(head, 4) = node;
    return node;
}

void ClearEntries(char* manager)
{
    This<void>(addr::cSC4CitySituationManager_ClearList, manager + automata_situations::field::kList);
}

uint32_t Bits32(float v)
{
    uint32_t b;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

uint32_t Bits64d(double v)
{
    uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    return static_cast<uint32_t>(b) ^ static_cast<uint32_t>(b >> 32) * 31u;
}

// The real advices of the advisor service, as slots 19 and 20 walk them: up
// to four GUIDs (the advice's slot 3's slot 10) and eight automata groups.
void RealAdvices(std::vector<uint32_t>& guids, std::vector<uint32_t>& groups)
{
    void* const service = Virtual<void*>(Global<void*>(addr::SC4_Service43D34), 0x1C, 0x1Du);
    if (!service)
        return;
    char** const tree = Virtual<char**>(service, 0x28);
    char* node = At<char*>(*tree, 8);
    while (node != *tree) {
        if (void* const advice = At<void*>(node, 0x10)) {
            void* face = nullptr;
            if (Virtual<uint8_t>(advice, 0x00, 0x6BEB1E8Au, &face) && face) {
                if (guids.size() < 4)
                    if (void* const base = Virtual<void*>(face, 0x0C))
                        guids.push_back(Virtual<uint32_t>(base, 0x28));
                uint32_t** const v = Virtual<uint32_t**>(face, 0x30);
                for (uint32_t* p = v[0]; p != v[1] && groups.size() < 8; ++p)
                    groups.push_back(*p);
            }
            if (face)
                Virtual<uint32_t>(face, 0x08);
        }
        This<void>(addr::SC4MapIterator_Next, static_cast<void*>(&node));
    }
}

} // namespace c2

void Situations2Drive(cISC4City* city)
{
    using namespace c2;
    namespace sf = automata_situations::field;
    namespace sl = automata_situations::slot;
    Hash lua, avail, starts, helpers;
    uint32_t flags = 0;
    using Factory = char*(__cdecl*)();
    char* const m = reinterpret_cast<Factory>(addr::cSC4CitySituationManager_Create)();
    if (!m) {
        g_situation2Text = " D-";
        return;
    }
    g_fresh = m;
    Virtual<uint32_t>(m, sl::kAddRef);
    // The constructor seeds the random (+0x28) with -1, from the clock: a
    // fixed seed for 0x00526380's picks.
    This<void*>(addr::SC4Random_ConstructSeeded, m + sf::kRandom, 0x7A5A1234u);
    // The fresh manager's vtable: a copy, slots 24, 36 and 38 the test's.
    static void* table[40];
    std::memcpy(table, reinterpret_cast<void*>(addr::cSC4CitySituationManager_Vtable), sizeof table);
    table[sl::kStartJoyride / 4] = reinterpret_cast<void*>(&StubJoyride);
    table[sl::kStartSituation / 4] = reinterpret_cast<void*>(&StubStart);
    table[sl::kEndSituation / 4] = reinterpret_cast<void*>(&StubEnd);
    void* const vtable = At<void*>(m, 0);
    At<void*>(m, 0) = table;

    // ---- The Lua callbacks.
    {
        using Callback = uint8_t(__cdecl*)(void*, void*, void*, void*);
        c2::Fake* const binding = Make(0, BindingVtable());
        c2::Fake* const arguments = Make(1, ArgumentsVtable());
        c2::Fake* const values = Make(2, ValueVtable());
        Make(3, ValueVtable());
        c2::Fake* const results = Make(4, ResultsVtable());
        arguments->other = values;
        arguments->answer = 1;
        binding->other = m;
        // A success count for get_success_count.
        const uint32_t key = 0x7A5A0001u;
        if (uint16_t* const count = This<uint16_t*>(addr::cSC4CitySituationManager_SuccessAt,
                                                    m + sf::kSuccesses, &key))
            *count = 5;
        void* const a1 = &g_fakes[5];
        const auto call = [&](std::uintptr_t address) {
            g_log = Hash{};
            const uint8_t answer = reinterpret_cast<Callback>(address)(a1, arguments, results, binding);
            lua.Mix(answer);
            lua.Mix(g_log.h);
        };
        values[0].value = key;
        values[0].answer = 1;
        call(addr::cSC4CitySituationManager_LuaGetSuccessCount);
        arguments->answer = 0; // argument 0 missing
        call(addr::cSC4CitySituationManager_LuaGetSuccessCount);
        arguments->answer = 1;
        values[0].value = 0; // an ID of 0
        call(addr::cSC4CitySituationManager_LuaGetSuccessCount);
        call(addr::cSC4CitySituationManager_LuaGetCurrentTarget);
        call(addr::cSC4CitySituationManager_LuaGetActiveAuto);
        values[0].value = 0;
        call(addr::cSC4CitySituationManager_LuaShowCsi);
        lua.Mix(At<uint8_t>(m, sf::kCsiVisible));
        values[0].value = 1;
        call(addr::cSC4CitySituationManager_LuaShowCsi);
        lua.Mix(At<uint8_t>(m, sf::kCsiVisible));
        At<uint32_t>(m, sf::kNumTargets) = 6;
        At<uint32_t>(m, sf::kTargetIndex) = 2;
        call(addr::cSC4CitySituationManager_LuaNumTargetsRemaining);
        call(addr::cSC4CitySituationManager_LuaGetTargetName);
        // activate_situation with an ID of 0 (no message).
        values[0].value = 0;
        values[1].value = 1;
        call(addr::cSC4CitySituationManager_LuaActivateSituation);
        // No manager: the defaults; activate_situation posts nothing.
        binding->other = nullptr;
        values[0].value = key;
        call(addr::cSC4CitySituationManager_LuaShowCsi);
        call(addr::cSC4CitySituationManager_LuaNumTargetsRemaining);
        call(addr::cSC4CitySituationManager_LuaGetTargetName);
        call(addr::cSC4CitySituationManager_LuaGetSuccessCount);
        call(addr::cSC4CitySituationManager_LuaActivateSituation);
        // The variables' accessor, both ways.
        using Accessor = uint32_t(__cdecl*)(uint32_t*, void*, uint32_t);
        const auto accessor = reinterpret_cast<Accessor>(addr::cSC4CitySituationManager_LuaVariable);
        g_log = Hash{};
        values[0].value = 0x1234;
        values[0].answer = 0x55AA01;
        lua.Mix(accessor(&At<uint32_t>(m, sf::kNumTargets), &values[0], 1));
        lua.Mix(At<uint32_t>(m, sf::kNumTargets));
        lua.Mix(accessor(&At<uint32_t>(m, sf::kTargetIndex), &values[0], 0));
        lua.Mix(g_log.h);
        At<uint32_t>(m, sf::kNumTargets) = 0;
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        At<uint8_t>(m, sf::kCsiVisible) = 1;
        for (int i = 0; i < 5; ++i)
            lua.Mix(g_fakes[i].refs);
    }

    // ---- IsSituationAvailable, for every group and GUID of the real advices.
    std::vector<uint32_t> guids, groups;
    RealAdvices(guids, groups);
    {
        avail.Mix(static_cast<uint32_t>(guids.size()));
        avail.Mix(static_cast<uint32_t>(groups.size()));
        std::vector<uint32_t> ids = groups;
        ids.insert(ids.end(), guids.begin(), guids.end());
        ids.push_back(0);
        ids.push_back(0x7A5A0001u);
        for (const uint32_t id : ids) {
            avail.Mix(id);
            avail.Mix(Virtual<uint8_t>(m, sl::kIsSituationAvailableById, id));
        }
        void* building = nullptr;
        void* second = nullptr;
        PickOccupants(Manager(city), building, second);
        avail.Mix(Virtual<uint8_t>(m, sl::kIsSituationAvailable, building));
        avail.Mix(Virtual<uint8_t>(m, sl::kIsSituationAvailable, second));
        avail.Mix(Virtual<uint8_t>(m, sl::kIsSituationAvailable, static_cast<void*>(nullptr)));
    }

    // ---- The starts (slots 21 to 23 with slot 36 the test's).
    c2::Fake* const advice = Make(6, SituationVtable());
    c2::Fake* const inner = Make(7, SituationVtable());
    c2::Fake* const candidate = Make(8, AutomatonVtable());
    c2::Fake* const reject = Make(9, AutomatonVtable());
    c2::Fake* const made = Make(10, AutomatonVtable());
    c2::Fake* const service = Make(11, ServiceVtable());
    c2::Fake* const service2 = Make(12, ServiceVtable());
    candidate->flags = 2;
    candidate->kind = 1;
    reject->flags = 2 | 1;
    made->flags = 2;
    inner->value = 0x7A5A0042u; // a GUID no advice has
    advice->other = inner;
    void* services[2] = {service, service2};
    At<void**>(m, sf::kServices) = services;
    At<void**>(m, sf::kServices + 4) = services + 2;
    At<void**>(m, sf::kServices + 8) = services + 2;
    {
        const auto run = [&](uint32_t answer, auto&& fn) {
            g_log = Hash{};
            g_startAnswer = answer;
            starts.Mix(fn());
            starts.Mix(g_log.h);
            if (EnvInt("OPENSC4_TEST_C2_TRACE", 0))
                log::Write("autotest: c2 start trace %08X random %08X", g_log.h,
                           BytesChecksum(m + sf::kRandom, sf::kCsiVisible - sf::kRandom));
        };
        using Guid = uint8_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
        const auto byGuid = reinterpret_cast<Guid>(addr::cSC4CitySituationManager_InitiateSituationByGuid);
        using Group = uint8_t(__thiscall*)(void*, uint32_t, uint32_t, uint32_t);
        const auto byGroup = reinterpret_cast<Group>(addr::cSC4CitySituationManager_InitiateSituationByAutomataGroup);
        using ForOccupant = uint8_t(__thiscall*)(void*, void*, void*);
        const auto forOccupant =
            reinterpret_cast<ForOccupant>(addr::cSC4CitySituationManager_InitiateSituationForOccupant);
        // Slot 21: an unknown GUID, then the real advices.
        run(1, [&] { return byGuid(m, 0x7A5A0042u, 0, 0); });
        service->value = 0x7A5A0007u; // the group of the third argument
        service->other = candidate;
        for (const uint32_t guid : guids) {
            run(1, [&] { return byGuid(m, guid, 0, 0x7A5A0007u); });
            run(0, [&] { return byGuid(m, guid, 0, 0x7A5A0007u); });
            run(1, [&] { return byGuid(m, guid, 1, 0x7A5A0007u); });
            service->other = reject;
            run(0, [&] { return byGuid(m, guid, 0, 0x7A5A0007u); });
            service->other = candidate;
            run(1, [&] { return byGuid(m, guid, 0, 0); });
        }
        // Slot 22: an advice given, then the list.
        run(1, [&] { return forOccupant(m, candidate, advice); });
        run(1, [&] { return forOccupant(m, static_cast<void*>(nullptr), advice); });
        advice->answer = 1;
        PushEntry(m, nullptr, nullptr, 0);
        PushEntry(m, inner, nullptr, 0);
        PushEntry(m, advice, nullptr, 0);
        inner->refs += 1;
        advice->refs += 1;
        run(1, [&] { return forOccupant(m, candidate, static_cast<void*>(nullptr)); });
        advice->answer = 0;
        run(0, [&] { return forOccupant(m, candidate, static_cast<void*>(nullptr)); });
        // Slot 23: a list entry for the group (slot 21 with its GUID), then
        // none with and without the joyride.
        advice->answer = 2;
        run(1, [&] { return byGroup(m, 0x7A5A0007u, 1, 1); });
        advice->answer = 0;
        run(1, [&] { return byGroup(m, 0x7A5A0007u, 0, 0); });
        run(1, [&] { return byGroup(m, 0x7A5A0007u, 0, 1); });
        service->other = reject;
        run(1, [&] { return byGroup(m, 0x7A5A0007u, 0, 0x101); });
        service->other = candidate;
        ClearEntries(m);
    }

    // ---- The helpers, directly.
    {
        const bool trace = EnvInt("OPENSC4_TEST_C2_TRACE", 0) != 0;
        uint32_t step = 0;
        const auto mixLog = [&](uint32_t v) {
            helpers.Mix(v);
            helpers.Mix(g_log.h);
            if (trace)
                log::Write("autotest: c2 trace %u: %08X %08X random %08X", step, v, g_log.h,
                           BytesChecksum(m + sf::kRandom, sf::kCsiVisible - sf::kRandom));
            ++step;
            g_log = Hash{};
        };
        g_log = Hash{};
        // 0x005245F0 over the flags and kinds.
        c2::Fake* const occ = Make(13, AutomatonVtable());
        const uint32_t flagSets[] = {0, 2, 3, 2 | 0x8000000u, 2 | 0x10000000u, 2, 2};
        const uint32_t kinds[] = {0, 0, 0, 0, 0, 0x34, 0x14};
        for (int i = 0; i < 7; ++i) {
            occ->flags = flagSets[i];
            occ->kind = kinds[i];
            mixLog(This<uint8_t>(addr::cSC4CitySituationManager_IsCandidate, m, static_cast<void*>(occ)));
        }
        mixLog(This<uint8_t>(addr::cSC4CitySituationManager_IsCandidate, m, static_cast<void*>(nullptr)));
        // 0x00525640 with the two services.
        {
            uint32_t ids[3] = {0x7A5A0007u, 0x7A5A0008u, 0x7A5A0009u};
            uint32_t* vec[3] = {ids, ids + 3, ids + 3};
            service2->value = 0x7A5A0009u;
            alignas(4) char set[0x14] = {};
            This<void>(addr::SC4Uint32HashSet_Construct, set, 100u);
            mixLog(This<uint8_t>(addr::cSC4CitySituationManager_GatherCandidates, m, static_cast<void*>(vec),
                                 static_cast<void*>(set)));
            helpers.Mix(At<uint32_t>(set, 0x10));
            This<void>(addr::SC4Uint32HashSet_Clear, set);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<void*>(set, 4));
        }
        // 0x005292F0 and 0x00528360: the services' places and makes.
        {
            uint32_t ids[2] = {0x7A5A0007u, 0x193Au};
            uint32_t* vec[3] = {ids, ids + 2, ids + 2};
            const auto create = [&] {
                return Id(This<void*>(addr::cSC4CitySituationManager_CreateCandidate, m, static_cast<void*>(vec)));
            };
            mixLog(create());
            service->answer = 1;
            service->other2 = made;
            SetPos(service, 10.0f, 2.0f, 30.0f);
            mixLog(create());
            service->answer = 0;
            service2->answer = 1;
            service2->other2 = nullptr;
            mixLog(create());
            service2->answer = 0;
            service->other2 = nullptr;
            // The class 0x193A through +0xAF8.
            c2::Fake* const holder = Make(14, HelperVtable());
            c2::Fake* const source = Make(15, AutomatonVtable());
            c2::Fake* const pick = Make(16, AutomatonVtable());
            holder->other = source;
            source->other = pick;
            At<void*>(m, sf::kRefAF8) = holder;
            mixLog(Id(This<void*>(addr::cSC4CitySituationManager_PickSite, m, 0x193Au)));
            source->other = nullptr;
            mixLog(Id(This<void*>(addr::cSC4CitySituationManager_PickSite, m, 0x193Au)));
            At<void*>(m, sf::kRefAF8) = nullptr;
            mixLog(This<void*>(addr::cSC4CitySituationManager_PickSite, m, 0u) ? 1u : 0u);
            mixLog(pick->refs);
        }
        // 0x00526380 over a list of the test's own.
        {
            using Allocate = void*(__cdecl*)(uint32_t);
            const auto alloc = reinterpret_cast<Allocate>(addr::SC4_stl_allocate);
            char* const head = static_cast<char*>(alloc(0xC));
            At<char*>(head, 0) = head;
            At<char*>(head, 4) = head;
            for (int i = 0; i < 3; ++i) {
                char* const node = static_cast<char*>(alloc(0xC));
                At<void*>(node, 8) = &g_fakes[17 + i];
                At<char*>(node, 0) = At<char*>(head, 0);
                At<char*>(head, 0) = node;
            }
            char* list = head;
            for (int i = 0; i < 4; ++i)
                mixLog(Id(This<void*>(addr::cSC4CitySituationManager_PickRandom, m, static_cast<void*>(&list))));
            for (char* node = At<char*>(head, 0); node != head;) {
                char* const next = At<char*>(node, 0);
                reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(node);
                node = next;
            }
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(head);
        }
        // 0x00524A50 and 0x00524A00.
        {
            c2::Fake* const a = Make(17, SituationVtable());
            c2::Fake* const b = Make(18, SituationVtable());
            c2::Fake* const pa = Make(19, SituationVtable());
            c2::Fake* const pb = Make(20, SituationVtable());
            a->other = pa;
            b->other = pb;
            pa->value = 3;
            pb->value = static_cast<uint32_t>(-2);
            void* x = a;
            void* y = b;
            void* none = nullptr;
            const auto cmp = [&](void** p, void** q) {
                return This<uint8_t>(addr::cSC4CitySituationManager_ComparePriority, static_cast<void*>(p),
                                     static_cast<void*>(q));
            };
            mixLog(cmp(&x, &y));
            mixLog(cmp(&y, &x));
            mixLog(cmp(&none, &y));
            mixLog(cmp(&none, &none));
            mixLog(cmp(&x, &none));
            void* holder[3] = {&g_fakes[0], &g_fakes[1], reinterpret_cast<void*>(7)};
            void** const got = This<void**>(addr::cSC4CitySituationManager_HolderInit, static_cast<void*>(holder),
                                            static_cast<void*>(a));
            mixLog((got == holder ? 1u : 0u) | Id(holder[0]) << 8 | Id(holder[1]) << 16 |
                   (holder[2] ? 1u : 0u) << 24);
            helpers.Mix(a->refs);
            Virtual<uint32_t>(a, 0x08);
        }
        // 0x005245A0 up a chain of groups.
        {
            alignas(4) char g1[0x70] = {}, g2[0x70] = {}, g3[0x70] = {};
            At<void*>(g1, 0xC) = g2;
            At<void*>(g2, 0xC) = g3;
            At<void*>(g3, 0x68) = &g_fakes[21];
            const auto up = [](char* g) {
                return Id(reinterpret_cast<void*(__fastcall*)(char*)>(
                    addr::cSC4CitySituationManager_InheritedTemplate)(g));
            };
            mixLog(up(g1));
            At<void*>(g3, 0x68) = nullptr;
            mixLog(up(g1));
        }
        // 0x00528ED0, no icon added: no automaton, no template, hidden
        // without an ID.
        {
            c2::Fake* const a = Make(13, AutomatonVtable());
            uint32_t id = 0;
            using Show = void(__thiscall*)(void*, void*, uint32_t*, uint32_t);
            const auto show = reinterpret_cast<Show>(addr::cSC4CitySituationManager_ShowAutomatonCsi);
            show(m, nullptr, &id, 1);
            mixLog(id);
            show(m, a, &id, 1);
            mixLog(id);
            alignas(4) char group[0x70] = {};
            a->other2 = group;
            show(m, a, &id, 1);
            mixLog(id);
            show(m, a, &id, 0);
            mixLog(id | a->refs << 8);
        }
        // Slot 25 and 0x005291C0 over list entries (the automaton tracked, so
        // no icon is added).
        {
            c2::Fake* const f = Make(13, AutomatonVtable());
            c2::Fake* const g = Make(14, AutomatonVtable());
            c2::Fake* const s1 = Make(17, SituationVtable());
            c2::Fake* const s2 = Make(18, SituationVtable());
            c2::Fake* const p1 = Make(19, SituationVtable());
            c2::Fake* const p2 = Make(20, SituationVtable());
            f->flags = 2;
            s1->other = p1;
            s2->other = p2;
            p1->value = 5;
            p2->value = 1;
            s1->answer = 1;
            using Treat = void(__thiscall*)(void*, void*, uint32_t);
            const auto treat = reinterpret_cast<Treat>(addr::cSC4CitySituationManager_TreatAutomatonAsGroup);
            treat(m, nullptr, 3);
            mixLog(0);
            f->flags = 0;
            treat(m, f, 0x7A5A0011u);
            mixLog(1);
            f->flags = 2;
            PushNode(m + sf::kTracked, reinterpret_cast<std::uintptr_t>(f), nullptr, 8);
            const uint8_t most = Virtual<uint8_t>(m, sl::kGetMaxCsi);
            helpers.Mix(most);
            if (most > 1)
                PushEntry(m, s2, g, 0x31);
            char* const free = PushEntry(m, s1, nullptr, 0);
            char* const lower = PushEntry(m, s2, f, 0x77);
            s1->refs += 2;
            s2->refs += 2;
            g->refs += 1;
            f->refs += 1;
            treat(m, f, 0x7A5A0012u);
            mixLog(Id(At<void*>(free, 0xC)) | Id(At<void*>(lower, 0xC)) << 8 | At<uint32_t>(free, 0x10) << 16 |
                   At<uint32_t>(lower, 0x10) << 24);
            treat(m, f, 0x7A5A0013u); // its entry is there now
            mixLog(2);
            ClearEntries(m);
            This<void>(addr::cSC4CitySituationManager_ReleaseTracked, m);
            helpers.Mix(f->refs);
            helpers.Mix(g->refs);
        }
        // 0x005256E0, 0x00526A20 and 0x00526EF0 over a situation of the
        // test's own.
        {
            c2::Fake* const sit = Make(17, SituationVtable());
            c2::Fake* const autom = Make(13, AutomatonVtable());
            c2::Fake* const target = Make(14, AutomatonVtable());
            c2::Fake* const driver = Make(15, HelperVtable());
            c2::Fake* const lots = Make(16, LotVtable());
            c2::Fake* const lot = Make(18, LotVtable());
            lot->kind = 1;
            c2::Fake* const ctx = Make(19, ContextVtable());
            SetPos(autom, 100.0f, 5.0f, 200.0f);
            SetPos(target, 180.5f, 9.0f, 260.25f);
            SetPos(driver, 90.0f, 4.0f, 210.0f);
            using Position = uint8_t(__thiscall*)(void*, float*);
            const auto position = reinterpret_cast<Position>(addr::cSC4CitySituationManager_GetDriverPosition);
            using Distance = double(__thiscall*)(void*, uint32_t);
            const auto distance = reinterpret_cast<Distance>(addr::cSC4CitySituationManager_TargetDistance);
            using Limit = float(__thiscall*)(void*);
            const auto limit = reinterpret_cast<Limit>(addr::cSC4CitySituationManager_ComputeTimeLimit);
            float out[3] = {-1.0f, -1.0f, -1.0f};
            const auto mixOut = [&](uint8_t r) {
                helpers.Mix(r);
                helpers.MixBytes(out, sizeof out);
                mixLog(0);
            };
            mixOut(position(m, out));
            At<void*>(m, sf::kSituation) = sit;
            mixOut(position(m, out));
            At<void*>(m, sf::kAuto) = autom;
            mixOut(position(m, out));
            sit->flags = 8;
            mixOut(position(m, out));
            At<void*>(m, sf::kRefA9C) = driver;
            for (uint32_t answer : {0u, 1u, 3u}) {
                driver->answer = answer;
                mixOut(position(m, out));
            }
            // The distances.
            const auto mixDistance = [&](uint32_t fromAutomaton) {
                helpers.Mix(Bits64d(distance(m, fromAutomaton)));
                mixLog(fromAutomaton);
            };
            At<void*>(m, sf::kAuto) = nullptr;
            mixDistance(1);
            At<void*>(m, sf::kAuto) = autom;
            driver->answer = 0;
            mixDistance(0); // no driver position
            sit->flags = 0;
            sit->kind = 2;
            mixDistance(1);
            mixDistance(0);
            sit->kind = 5;
            mixDistance(1);
            PushNode(m + sf::kSetAA8, 0x00140021u, nullptr, 8);
            mixDistance(1);
            sit->kind = 3;
            mixDistance(1);
            At<void*>(m, sf::kTarget) = target;
            mixDistance(1);
            sit->flags = 0x10;
            mixDistance(1); // no lot manager
            At<void*>(m, sf::kLotManager) = lots;
            mixDistance(1); // no lot
            lots->other = lot;
            const int32_t rects[][4] = {{4, 10, 8, 14}, {9, 14, 13, 18}, {10, 12, 12, 14}, {6, 13, 20, 20}, {2, 10, 5, 15}};
            for (const auto& r : rects) {
                std::memcpy(lot->rect, r, sizeof r);
                mixDistance(1);
            }
            alignas(4) char holder[0x10] = {};
            int32_t rect[4] = {6, 13, 6, 13};
            At<void*>(holder, 0xC) = rect;
            At<void*>(m, sf::kRefA58) = holder;
            mixDistance(1);
            At<void*>(holder, 0xC) = nullptr;
            mixDistance(1);
            At<void*>(m, sf::kRefA58) = nullptr;
            // The time limit.
            const auto mixLimit = [&] {
                helpers.Mix(Bits32(limit(m)));
                mixLog(0);
            };
            mixLimit(); // no table
            At<int32_t>(m, sf::kTableRef) = 41;
            mixLimit(); // no script context
            At<void*>(m, sf::kScriptContext) = ctx;
            sit->value = 9;
            sit->flags = 0x20;
            c2::Fake* const touch = Make(20, HelperVtable());
            At<void*>(m, sf::kRefAF8) = touch;
            mixLimit(); // not a table
            ctx->answer = 1;
            sit->flags = 0;
            mixLimit(); // no number
            ctx->answer = 3;
            ctx->number = 123.456789012345;
            mixLimit();
            alignas(4) char automGroup[0x70] = {};
            autom->other2 = automGroup;
            mixLimit(); // no inherited speed
            alignas(4) char speed[0x10] = {};
            At<float>(speed, 8) = 0.0375f;
            At<void*>(automGroup, 0x64) = speed;
            mixLimit();
            autom->other2 = nullptr;
            At<void*>(m, sf::kRefAF8) = nullptr;
            At<void*>(m, sf::kScriptContext) = nullptr;
            At<int32_t>(m, sf::kTableRef) = -2;
            At<void*>(m, sf::kTarget) = nullptr;
            At<void*>(m, sf::kLotManager) = nullptr;
            // Slot 39 with the situation's list entry, its CSI ID 0.
            c2::Fake* const stopper = Make(21, HelperVtable());
            At<void*>(m, sf::kRefAA0) = stopper;
            PushEntry(m, Make(22, SituationVtable()), nullptr, 0);
            char* const entry = PushEntry(m, sit, autom, 0);
            sit->refs += 1;
            autom->refs += 1;
            At<float>(m, sf::kTimeLimit) = 12.5f;
            Virtual<void>(m, sl::kStopSituation);
            mixLog(Id(At<void*>(m, sf::kRefA9C)) | Id(At<void*>(m, sf::kRefAA0)) << 8 |
                   Id(At<void*>(entry, 0xC)) << 16);
            helpers.Mix(At<uint32_t>(m, sf::kTimeLimit));
            helpers.Mix(driver->refs);
            helpers.Mix(stopper->refs);
            At<void*>(m, sf::kAuto) = nullptr;
            Virtual<void>(m, sl::kStopSituation); // no automaton
            mixLog(3);
            At<void*>(m, sf::kSituation) = nullptr;
            Virtual<void>(m, sl::kStopSituation); // no situation
            mixLog(4);
            ClearEntries(m);
            // Slot 37 without a situation, slot 35 off.
            Virtual<void>(m, sl::kBeginDrive);
            Virtual<void>(m, sl::kSetUDrive, 0u);
            mixLog(At<uint32_t>(m, sf::kStatus) | At<uint8_t>(m, sf::kJoyride) << 8);
            helpers.Mix(sit->refs);
            helpers.Mix(autom->refs);
        }
    }

    // ---- Put the fresh manager back and release it.
    At<void**>(m, sf::kServices) = nullptr;
    At<void**>(m, sf::kServices + 4) = nullptr;
    At<void**>(m, sf::kServices + 8) = nullptr;
    At<void*>(m, 0) = vtable;
    for (const std::size_t at : {sf::kSituation, sf::kAuto, sf::kTarget, sf::kRefA9C, sf::kRefAA0, sf::kRefAF8,
                                 sf::kScriptContext, sf::kLotManager, sf::kRefA58})
        if (At<void*>(m, at))
            flags |= 0x10;
    starts.Mix(Virtual<uint32_t>(m, sl::kRelease));
    g_fresh = nullptr;
    for (int i = 0; i < kFakes; ++i)
        helpers.Mix(g_fakes[i].refs);
    flags |= 1 | (guids.empty() ? 0u : 2u) | (groups.empty() ? 0u : 4u);
    char text[96];
    std::snprintf(text, sizeof text, " D%08X,%08X,%08X,%08X,%X", lua.h, avail.h, starts.h, helpers.h, flags);
    g_situation2Text = text;
    log::Write("autotest: automata: situations2: lua %08X avail %08X (guids %u, groups %u) starts %08X helpers %08X "
               "flags %X",
               lua.h, avail.h, static_cast<unsigned>(guids.size()), static_cast<unsigned>(groups.size()), starts.h,
               helpers.h, flags);
}

// Part c3's drive (OPENSC4_TEST_AUTOMATA_SITUATIONS=3), see the top of the
// file. As part c2's: a fresh manager whose vtable is a copy (slots 24, 35,
// 36 and 38 the test's), objects of the test's own as arguments, messages and
// fields; the game's globals read, never written. No message is posted, no
// popup opened, no camera effect made, no reward given.

std::string g_situation3Text;

namespace c3 {

// A message (cIGZMessage2Standard's getters).
struct Msg {
    void** vtable;
    uint32_t refs;
    uint32_t type;
    uint32_t has1;
    uint32_t data1;
    void* void1;
    uint32_t data2;
    void* void2;
    uint32_t data3;
    void* unknown;
};

void MsgLog(uint32_t slot) { c2::g_log.Mix(0x4D0000u | slot); }
uint32_t __fastcall MsgType(Msg* m, void*)
{
    MsgLog(0x10);
    return m->type;
}
uint32_t __fastcall MsgHas1(Msg* m, void*)
{
    MsgLog(0x20);
    return m->has1;
}
uint32_t __fastcall MsgData1(Msg* m, void*)
{
    MsgLog(0x24);
    return m->data1;
}
void* __fastcall MsgVoid1(Msg* m, void*)
{
    MsgLog(0x28);
    return m->void1;
}
uint32_t __fastcall MsgData2(Msg* m, void*)
{
    MsgLog(0x38);
    return m->data2;
}
void* __fastcall MsgVoid2(Msg* m, void*)
{
    MsgLog(0x3C);
    return m->void2;
}
uint32_t __fastcall MsgData3(Msg* m, void*)
{
    MsgLog(0x4C);
    return m->data3;
}
void* __fastcall MsgUnknown(Msg* m, void*)
{
    MsgLog(0x80);
    return m->unknown;
}

// A cIGZString for the cheat's text (slot 5, the characters).
struct Text {
    void** vtable;
    const char* chars;
};
const char* __fastcall TextChars(Text* t, void*)
{
    c2::g_log.Mix(0x54000014u);
    return t->chars;
}

// Advices and situations: QueryInterface for 0x6BEB1E8A (refused with the
// answer's bit 0x10), the radius (pos[0]) and drive time (pos[1]) floats,
// the groups at slot 12, slots 17 and 19 (answer bits 4 and 8), and the
// rewards' slots answering false.
uint32_t __fastcall QIAdvice(c2::Fake* f, void*, uint32_t iid, void** out)
{
    c2::Log(f, 0x00, iid);
    if (iid != 0x6BEB1E8Au || (f->answer & 0x10))
        return 0;
    *out = f;
    ++f->refs;
    return 1;
}
float __fastcall SitRadius(c2::Fake* f, void*)
{
    c2::Log(f, 0x24);
    return f->pos[0];
}
float __fastcall SitDriveTime(c2::Fake* f, void*)
{
    c2::Log(f, 0x2C);
    return f->pos[1];
}
void* __fastcall SitGroups30(c2::Fake* f, void*)
{
    c2::Log(f, 0x30);
    return &f->groupsBegin;
}
uint32_t __fastcall SitHasTargets(c2::Fake* f, void*)
{
    c2::Log(f, 0x44);
    return (f->answer >> 2) & 1;
}
uint32_t __fastcall SitChecks(c2::Fake* f, void*)
{
    c2::Log(f, 0x4C);
    return (f->answer >> 3) & 1;
}
uint32_t __fastcall SitMoneyB8(c2::Fake* f, void*, uint32_t*, uint32_t*)
{
    c2::Log(f, 0xB8);
    return 0;
}
uint32_t __fastcall SitNameBC(c2::Fake* f, void*, void*)
{
    c2::Log(f, 0xBC);
    return 0;
}
uint32_t __fastcall SitBudgetC0(c2::Fake* f, void*, uint32_t*)
{
    c2::Log(f, 0xC0);
    return 0;
}
uint32_t __fastcall SitMoneyC4(c2::Fake* f, void*, uint32_t*, uint32_t*)
{
    c2::Log(f, 0xC4);
    return 0;
}
uint32_t __fastcall SitNameC8(c2::Fake* f, void*, void*)
{
    c2::Log(f, 0xC8);
    return 0;
}
uint32_t __fastcall SitBudgetCC(c2::Fake* f, void*, uint32_t*)
{
    c2::Log(f, 0xCC);
    return 0;
}

// Automata and occupants: QueryInterface for 0xA9B40F05 (refused with bit
// 0x10), the properties (other), the flag set and the message slot 61.
uint32_t __fastcall QIOccupant(c2::Fake* f, void*, uint32_t iid, void** out)
{
    c2::Log(f, 0x00, iid);
    if (iid != 0xA9B40F05u || (f->answer & 0x10))
        return 0;
    *out = f;
    ++f->refs;
    return 1;
}
void* __fastcall AutoProps(c2::Fake* f, void*)
{
    c2::Log(f, 0x18);
    return f->other;
}
// Slot 53 for an automaton. While the tracked set holds several fakes its
// walk order follows their addresses (which move with the DLL's load
// address): those calls are summed, not hashed in order.
bool g_unordered = false;
uint32_t g_unorderedSum = 0, g_unorderedCount = 0;
uint32_t __fastcall SitRelevantAny(c2::Fake* f, void*, void* x)
{
    if (g_unordered) {
        g_unorderedSum += (f->index << 16 | c2::Id(x)) * 2654435761u;
        ++g_unorderedCount;
        return f->answer & 1;
    }
    return c2::SitRelevant(f, nullptr, x);
}
void __fastcall AutoSetFlag(c2::Fake* f, void*, uint32_t flag, uint32_t on) { c2::Log(f, 0x78, flag, on); }
void __fastcall AutoTell(c2::Fake* f, void*, uint32_t id) { c2::Log(f, 0xF4, id); }
uint32_t __fastcall PropHas(c2::Fake* f, void*, uint32_t id)
{
    c2::Log(f, 0x0C, id);
    return f->answer & 1;
}

// The services' slots 9, 10 and 21.
void __fastcall SvcJoyrideEnd(c2::Fake* f, void*) { c2::Log(f, 0x24); }
void __fastcall SvcCountdownEnd(c2::Fake* f, void*) { c2::Log(f, 0x28); }
void __fastcall SvcCheck(c2::Fake* f, void*, uint32_t group, uint32_t radius, void* subject, uint32_t flag)
{
    c2::Log(f, 0x54, group, radius);
    c2::g_log.Mix(c2::Id(subject) | flag << 8);
}

// A window: its child (other) for any ID with the answer's bit 1; a child's
// object (other2).
uint32_t __fastcall WinChild(c2::Fake* f, void*, uint32_t id, void** out)
{
    c2::Log(f, 0x1C4, id);
    if (!(f->answer & 1))
        return 0;
    *out = f->other;
    return 1;
}
void* __fastcall WinObject(c2::Fake* f, void*)
{
    c2::Log(f, 0x16C);
    return f->other2;
}

void __fastcall StubUDrive(char* m, void*, uint32_t on)
{
    c2::g_log.Mix(0x350000 | c2::Id(m));
    c2::g_log.Mix(on & 0xFF);
}

#define C3_SLOT(off, fn) c2::Slot{off, reinterpret_cast<void*>(&fn)}

void** MsgVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C3_SLOT(0x10, MsgType), C3_SLOT(0x20, MsgHas1), C3_SLOT(0x24, MsgData1),
                              C3_SLOT(0x28, MsgVoid1), C3_SLOT(0x38, MsgData2), C3_SLOT(0x3C, MsgVoid2),
                              C3_SLOT(0x4C, MsgData3), C3_SLOT(0x80, MsgUnknown)});
}
void** TextVtable()
{
    static void* t[8] = {};
    t[5] = reinterpret_cast<void*>(&TextChars);
    return t;
}
void** AdviceVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C3_SLOT(0x00, QIAdvice), C3_SLOT(0x0C, c2::SitInner), C3_SLOT(0x10, c2::SitKind),
            C3_SLOT(0x1C, c2::SitValue1C), C3_SLOT(0x24, SitRadius), C3_SLOT(0x28, c2::SitValue28),
            C3_SLOT(0x2C, SitDriveTime), C3_SLOT(0x30, SitGroups30), C3_SLOT(0x3C, c2::SitFlag),
            C3_SLOT(0x44, SitHasTargets), C3_SLOT(0x48, c2::SitGroups), C3_SLOT(0x4C, SitChecks),
            C3_SLOT(0x78, c2::SitTable), C3_SLOT(0xB8, SitMoneyB8), C3_SLOT(0xBC, SitNameBC),
            C3_SLOT(0xC0, SitBudgetC0), C3_SLOT(0xC4, SitMoneyC4), C3_SLOT(0xC8, SitNameC8),
            C3_SLOT(0xCC, SitBudgetCC), C3_SLOT(0xD4, SitRelevantAny)});
}
void** AutomatonVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C3_SLOT(0x00, QIOccupant), C3_SLOT(0x0C, c2::AutoOther), C3_SLOT(0x14, c2::AutoSelf),
            C3_SLOT(0x18, AutoProps), C3_SLOT(0x1C, c2::AutoPos1C), C3_SLOT(0x20, c2::AutoPos20),
            C3_SLOT(0x34, c2::AutoRemove), C3_SLOT(0x78, AutoSetFlag), C3_SLOT(0x7C, c2::AutoFlag),
            C3_SLOT(0x9C, c2::AutoTarget9C), C3_SLOT(0xA0, c2::AutoTargetA0), C3_SLOT(0xEC, c2::AutoKind),
            C3_SLOT(0xF4, AutoTell), C3_SLOT(0x10C, c2::AutoHolder)});
}
void** PropsVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C3_SLOT(0x0C, PropHas)});
}
void** ServiceVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C3_SLOT(0x24, SvcJoyrideEnd), C3_SLOT(0x28, SvcCountdownEnd),
                              C3_SLOT(0x3C, c2::SvcMake), C3_SLOT(0x40, c2::SvcPlace), C3_SLOT(0x54, SvcCheck),
                              C3_SLOT(0x78, c2::SvcHandles), C3_SLOT(0x80, c2::SvcAdd),
                              C3_SLOT(0x84, c2::SvcDone)});
}
void** WindowVtable()
{
    static void* t[120] = {};
    return c2::MakeVtable(t, {C3_SLOT(0x16C, WinObject), C3_SLOT(0x1C4, WinChild)});
}

#undef C3_SLOT

c2::Fake* MakeMore(int i, void** vtable)
{
    c2::Fake& f = c2::g_more[i];
    std::memset(&f, 0, sizeof f);
    f.vtable = vtable;
    f.refs = 100;
    f.index = static_cast<uint32_t>(0x41 + i);
    f.groupsBegin = f.groups;
    f.groupsEnd = f.groups;
    f.groupsCap = f.groups + 4;
    return &f;
}

void SetGroups(c2::Fake* f, std::initializer_list<uint32_t> ids)
{
    uint32_t n = 0;
    for (const uint32_t id : ids)
        f->groups[n++] = id;
    f->groupsEnd = f->groups + n;
}

Msg Message(uint32_t type)
{
    Msg m{};
    m.vtable = MsgVtable();
    m.refs = 1;
    m.type = type;
    return m;
}

} // namespace c3

void Situations3Drive(cISC4City*)
{
    using c2::This;
    using c3::Msg;
    namespace sf = automata_situations::field;
    namespace sl = automata_situations::slot;
    Hash msgs, frame, handlers, list;
    uint32_t flags = 0;
    using Factory = char*(__cdecl*)();
    char* const m = reinterpret_cast<Factory>(addr::cSC4CitySituationManager_Create)();
    if (!m) {
        g_situation3Text = " E-";
        return;
    }
    c2::g_fresh = m;
    Virtual<uint32_t>(m, sl::kAddRef);
    // The constructor seeds the random (+0x28) from the clock: a fixed seed.
    This<void*>(addr::SC4Random_ConstructSeeded, m + sf::kRandom, 0x7A5A1234u);
    static void* table[40];
    std::memcpy(table, reinterpret_cast<void*>(addr::cSC4CitySituationManager_Vtable), sizeof table);
    table[sl::kStartJoyride / 4] = reinterpret_cast<void*>(&c2::StubJoyride);
    table[sl::kSetUDrive / 4] = reinterpret_cast<void*>(&c3::StubUDrive);
    table[sl::kStartSituation / 4] = reinterpret_cast<void*>(&c2::StubStart);
    table[sl::kEndSituation / 4] = reinterpret_cast<void*>(&c2::StubEnd);
    void* const vtable = At<void*>(m, 0);
    At<void*>(m, 0) = table;

    const bool trace = EnvInt("OPENSC4_TEST_C3_TRACE", 0) != 0;
    uint32_t step = 0;
    const auto state = [&]() {
        Hash s;
        s.Mix(At<uint32_t>(m, sf::kStatus));
        s.Mix(c2::Id(At<void*>(m, sf::kSituation)) | c2::Id(At<void*>(m, sf::kAuto)) << 8 |
              c2::Id(At<void*>(m, sf::kRefA9C)) << 16 | c2::Id(At<void*>(m, sf::kRefAA0)) << 24);
        s.MixBytes(m + sf::kCheckTime, 0x14); // +0xA84 to +0xA94
        s.Mix(At<uint8_t>(m, sf::kJoyride) | At<uint8_t>(m, sf::kByteA99) << 8);
        return s.h;
    };
    const auto entries = [&]() {
        Hash s;
        char* const head = At<char*>(m, sf::kList);
        uint32_t n = 0;
        for (char* node = At<char*>(head, 0); node != head && n < 64; node = At<char*>(node, 0), ++n) {
            s.Mix(c2::Id(At<void*>(node, 8)) | c2::Id(At<void*>(node, 0xC)) << 8);
            s.Mix(At<uint32_t>(node, 0x10));
        }
        s.Mix(n);
        s.Mix(At<uint32_t>(m, sf::kTrackedCount));
        return s.h;
    };
    const auto mix = [&](Hash& h, uint32_t v) {
        h.Mix(v);
        h.Mix(c2::g_log.h);
        h.Mix(c3::g_unorderedSum);
        h.Mix(c3::g_unorderedCount);
        c3::g_unorderedSum = 0;
        c3::g_unorderedCount = 0;
        if (trace) {
            Hash refs;
            for (int i = 0; i < c2::kFakes; ++i) {
                refs.Mix(c2::g_fakes[i].refs);
                refs.Mix(c2::g_more[i].refs);
            }
            log::Write("autotest: c3 trace %u: %08X %08X refs %08X list %08X", step, v, c2::g_log.h, refs.h,
                       entries());
        }
        ++step;
        c2::g_log = Hash{};
    };
    c2::g_log = Hash{};

    c2::Fake* const ctx = c2::Make(0, c2::ContextVtable());
    c2::Fake* const svc1 = c2::Make(1, c3::ServiceVtable());
    c2::Fake* const svc2 = c2::Make(2, c3::ServiceVtable());
    c2::Fake* const sit = c2::Make(3, c3::AdviceVtable());
    c2::Fake* const sitInner = c2::Make(4, c3::AdviceVtable());
    c2::Fake* const autom = c2::Make(5, c3::AutomatonVtable());
    c2::Fake* const target = c2::Make(6, c3::AutomatonVtable());
    c2::Fake* const other = c2::Make(7, c3::AutomatonVtable());
    c2::Fake* const props = c2::Make(8, c3::PropsVtable());
    c2::Fake* const stopper = c2::Make(9, c2::HelperVtable());
    c2::Fake* const driver = c2::Make(10, c2::HelperVtable());
    c2::Fake* const touch = c2::Make(11, c2::HelperVtable());
    c2::Fake* const window = c2::Make(12, c3::WindowVtable());
    c2::Fake* const child = c2::Make(13, c3::WindowVtable());
    c2::Fake* advices[4];
    c2::Fake* inners[4];
    const uint32_t priorities[4] = {5, 2, 9, 2};
    for (int i = 0; i < 4; ++i) {
        advices[i] = c2::Make(14 + i, c3::AdviceVtable());
        inners[i] = c2::Make(18 + i, c3::AdviceVtable());
        advices[i]->other = inners[i];
        inners[i]->value = priorities[i];
    }
    c2::Fake* const cand1 = c2::Make(22, c3::AutomatonVtable());
    c2::Fake* const cand2 = c2::Make(23, c3::AutomatonVtable());
    sit->other = sitInner;
    sitInner->value = 0x7A5A0042u; // a GUID no advice has
    sit->kind = 4;
    c2::SetPos(sit, 8.0f, 2.5f, 0.0f);
    c3::SetGroups(sit, {0x7A5A0007u, 0x7A5A0008u});
    autom->flags = 0x8000000u; // SetStatus reads the situation's drive time
    svc1->value = 0x7A5A0007u;
    svc1->other = cand1;
    svc2->value = 0x7A5A0009u;
    svc2->other = cand2;
    cand1->flags = 2;
    cand2->flags = 2 | 1;
    void* services[2] = {svc1, svc2};
    At<void**>(m, sf::kServices) = services;
    At<void**>(m, sf::kServices + 4) = services + 2;
    At<void**>(m, sf::kServices + 8) = services + 2;

    // ---- DoMessage, every message it answers that posts nothing and opens
    // no popup.
    {
        using Do = uint8_t(__thiscall*)(void*, void*);
        const auto doMessage = reinterpret_cast<Do>(addr::cSC4CitySituationManager_DoMessage);
        const auto send = [&](Msg& x) {
            const uint8_t answer = doMessage(m + 4, &x);
            msgs.Mix(state());
            mix(msgs, answer);
        };
        Msg x = c3::Message(0x7A5A0000u); // unknown
        send(x);
        x = c3::Message(0x8A09FA67u);
        send(x);
        // The cheat: another one, then csitrigger with no ID or an ID of 0.
        x = c3::Message(0x230E27ACu);
        x.data1 = 1;
        send(x);
        c3::Text text{c3::TextVtable(), "csitrigger"};
        x.data1 = 0x8C4136DBu;
        x.void2 = &text;
        send(x);
        text.chars = "csitrigger 0";
        send(x);
        text.chars = "csitrigger zz";
        send(x);
        // The end: no situation, finished, running (slot 39).
        x = c3::Message(0x0BB324BDu);
        send(x);
        At<void*>(m, sf::kSituation) = sit;
        At<uint32_t>(m, sf::kStatus) = 1;
        send(x);
        At<uint32_t>(m, sf::kStatus) = 4;
        At<void*>(m, sf::kRefAA0) = stopper;
        ++stopper->refs;
        send(x);
        // The automata's events.
        At<void*>(m, sf::kAuto) = autom;
        At<void*>(m, sf::kTarget) = target;
        x = c3::Message(0x09D92FC3u);
        At<uint32_t>(m, sf::kStatus) = 3;
        x.void1 = autom;
        x.data2 = 2;
        send(x); // the drive aborted: the status 2
        x.void1 = other;
        send(x);
        At<uint32_t>(m, sf::kStatus) = 4;
        x.data2 = 9;
        send(x);
        x.void1 = autom;
        send(x);
        x.void1 = target;
        send(x);
        x.data2 = 11;
        x.void1 = other;
        send(x);
        x.data2 = 12;
        x.data3 = 0;
        send(x);
        x.data3 = 0x14;
        x.void1 = target;
        send(x); // the target's automaton destroyed: the status 2 (kind 4)
        x.data2 = 5;
        send(x);
        // The load, +0x9F1 not set.
        x = c3::Message(0x26D31EC1u);
        send(x);
        // An advice gone: none, another, the situation (slot 39), refused.
        At<uint32_t>(m, sf::kStatus) = 4;
        x = c3::Message(0x6BB350E4u);
        send(x);
        x.unknown = advices[0];
        send(x);
        x.unknown = sit;
        send(x);
        sit->answer = 0x10;
        send(x);
        sit->answer = 0;
        // The frame through the message.
        x = c3::Message(0x86AD10EEu);
        x.data3 = c2::Bits32(16.0f);
        send(x);
        // Occupants added and removed.
        other->other = props;
        x = c3::Message(0x99EF1142u);
        x.void1 = other;
        send(x); // tracked by 0x005291C0 (not a candidate)
        props->answer = 1;
        send(x); // marked 0x029244DB
        props->answer = 0;
        other->answer = 0x10;
        send(x); // refused
        other->answer = 0;
        x.void1 = nullptr;
        send(x);
        x = c3::Message(0x99EF1143u);
        x.void1 = other;
        send(x);
        // A start by GUID (slot 21; no advice has it, slot 36 the test's).
        x = c3::Message(0xAC1045EFu);
        x.data1 = 0x7A5A0042u;
        x.data2 = 1;
        send(x);
        // The driver: without and with the situation's flag 8, none.
        x = c3::Message(0xCC0D905Fu);
        x.unknown = driver;
        send(x);
        sit->flags = 8;
        send(x);
        x.unknown = nullptr;
        send(x);
        sit->flags = 0;
        msgs.Mix(driver->refs);
        msgs.Mix(stopper->refs);
    }

    // ---- The frame (0x0052E300), directly.
    {
        using Frame = uint8_t(__thiscall*)(void*, float);
        const auto frameAt = reinterpret_cast<Frame>(addr::cSC4CitySituationManager_Frame);
        const auto tick = [&](float dt) {
            const uint8_t answer = frameAt(m, dt);
            frame.Mix(state());
            mix(frame, answer);
        };
        At<void*>(m, sf::kSituation) = nullptr;
        At<void*>(m, sf::kAuto) = nullptr;
        At<void*>(m, sf::kTarget) = nullptr;
        if (void* const kept = At<void*>(m, sf::kRefA9C)) {
            At<void*>(m, sf::kRefA9C) = nullptr;
            Virtual<uint32_t>(kept, sl::kRelease);
        }
        At<uint32_t>(m, sf::kStatus) = 4;
        // The joyride's end, unless the U-Drive's camera would be asked.
        char* const udrive = Global<char*>(addr::SC4_UDriveMode);
        void* const camera = Global<void*>(addr::SC4_Service43D1C);
        if (udrive && (!camera || !At<void*>(udrive, 0x30))) {
            At<uint8_t>(m, sf::kJoyride) = 1;
            At<void*>(m, sf::kRefAA0) = stopper;
            ++stopper->refs;
            tick(16.0f);
            flags |= 2;
        }
        // The countdown, running and ending.
        At<float>(m, sf::kCountdown) = 100.0f;
        tick(16.0f);
        At<float>(m, sf::kCountdown) = 10.0f;
        At<void*>(m, sf::kRefAA0) = stopper;
        ++stopper->refs;
        tick(16.0f);
        // No situation: the driver kept, then dropped (slot 5 false).
        At<float>(m, sf::kCountdown) = 0.0f;
        At<void*>(m, sf::kRefA9C) = driver;
        ++driver->refs;
        driver->answer = 1;
        tick(16.0f);
        driver->answer = 0;
        tick(16.0f);
        // A situation: the drive's time running.
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = autom;
        At<float>(m, sf::kDriveTime) = 5.0f;
        tick(16.0f);
        // Its update: the warning, the target update, the check (the
        // automaton, the radius from the city's width).
        At<float>(m, sf::kDriveTime) = 0.0f;
        At<float>(m, sf::kTimeLimit) = 40.0f;
        At<float>(m, sf::kSituationTime) = 15.0f;
        At<float>(m, sf::kCheckTime) = 4990.0f;
        sit->answer = 8;
        tick(20.0f);
        tick(20.0f); // no warning again, the check's clock low
        // The target's occupant, a radius of 24; refused; slot 19 false.
        At<float>(m, sf::kCheckTime) = 5000.0f;
        sit->flags = 0x80;
        sit->pos[0] = 24.0f;
        At<void*>(m, sf::kTarget) = target;
        tick(20.0f);
        At<float>(m, sf::kCheckTime) = 5000.0f;
        target->answer = 0x10;
        tick(20.0f);
        target->answer = 0;
        At<float>(m, sf::kCheckTime) = 5000.0f;
        sit->answer = 0;
        tick(20.0f);
        sit->flags = 0;
        sit->pos[0] = 8.0f;
        // The status 3 with a time limit under 1 (0x0052C8F0 adds the time).
        At<uint32_t>(m, sf::kStatus) = 3;
        At<float>(m, sf::kTimeLimit) = 0.0f;
        At<uint8_t>(m, sf::kByteA99) = 0;
        tick(20.0f);
        At<uint32_t>(m, sf::kStatus) = 4;
        frame.Mix(stopper->refs);
        frame.Mix(driver->refs);
        frame.Mix(autom->refs);
        frame.Mix(target->refs);
    }

    // ---- The handlers, directly.
    {
        using H = void(__thiscall*)(void*, void*);
        const auto call = [&](std::uintptr_t address, void* x) {
            reinterpret_cast<H>(address)(m, x);
            handlers.Mix(state());
            mix(handlers, c2::Id(x));
        };
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = autom;
        At<void*>(m, sf::kTarget) = target;
        // 0x0052E670: the target reached (kinds 1, 3, 4), the automaton's
        // occupant, null, no situation.
        for (const uint32_t kind : {1u, 3u, 4u}) {
            sit->kind = kind;
            At<uint32_t>(m, sf::kStatus) = 3;
            call(addr::cSC4CitySituationManager_OccupantRemoved, target);
        }
        sit->kind = 4;
        At<uint32_t>(m, sf::kStatus) = 3;
        call(addr::cSC4CitySituationManager_OccupantRemoved, autom);
        call(addr::cSC4CitySituationManager_OccupantRemoved, nullptr);
        At<void*>(m, sf::kSituation) = nullptr;
        call(addr::cSC4CitySituationManager_OccupantRemoved, other);
        // 0x0052E710.
        call(addr::cSC4CitySituationManager_RecheckTarget, autom);
        At<void*>(m, sf::kSituation) = sit;
        for (c2::Fake* const a : {autom, target, other})
            call(addr::cSC4CitySituationManager_RecheckTarget, a);
        // 0x0052C020: the automaton, the target's (kinds 1, 4), another, a
        // tracked one, null without a situation.
        At<uint32_t>(m, sf::kStatus) = 3;
        call(addr::cSC4CitySituationManager_AutomatonDestroyed, autom);
        for (const uint32_t kind : {1u, 4u}) {
            sit->kind = kind;
            At<uint32_t>(m, sf::kStatus) = 3;
            call(addr::cSC4CitySituationManager_AutomatonDestroyed, target);
        }
        sit->kind = 4;
        call(addr::cSC4CitySituationManager_AutomatonDestroyed, other);
        ++other->refs;
        PushNode(m + sf::kTracked, reinterpret_cast<std::uintptr_t>(other), nullptr, 8);
        call(addr::cSC4CitySituationManager_AutomatonDestroyed, other);
        This<void>(addr::cSC4CitySituationManager_ReleaseTracked, m);
        At<void*>(m, sf::kSituation) = nullptr;
        call(addr::cSC4CitySituationManager_AutomatonDestroyed, nullptr);
        At<void*>(m, sf::kSituation) = sit;
        // 0x0052A7D0 with null; 0x00524DF0 with another advice and the
        // situation.
        call(addr::cSC4CitySituationManager_TrackOccupant, nullptr);
        call(addr::cSC4CitySituationManager_AdviceGone, advices[1]);
        call(addr::cSC4CitySituationManager_AdviceGone, sit);
        // 0x00524CC0 and 0x00524580 over the statuses.
        for (const uint32_t status : {0u, 1u, 2u, 3u, 4u, 5u}) {
            At<uint32_t>(m, sf::kStatus) = status;
            handlers.Mix(This<uint32_t>(addr::cSC4CitySituationManager_IsFinished, m));
            At<void*>(m, sf::kSituation) = status == 5 ? nullptr : sit;
            At<uint32_t>(m, sf::kStatus) = status == 5 ? 3 : status;
            handlers.Mix(This<uint32_t>(addr::cSC4CitySituationManager_AbortDrive, m));
            mix(handlers, state());
        }
        At<void*>(m, sf::kSituation) = sit;
        At<uint32_t>(m, sf::kStatus) = 4;
        // 0x00525130: no automaton, slot 19 false.
        using Check = void(__thiscall*)(void*, float);
        const auto check = reinterpret_cast<Check>(addr::cSC4CitySituationManager_CheckTargets);
        At<void*>(m, sf::kAuto) = nullptr;
        At<float>(m, sf::kCheckTime) = 6000.0f;
        check(m, 1.0f);
        mix(handlers, state());
        At<void*>(m, sf::kAuto) = autom;
        check(m, 1.0f);
        mix(handlers, state());
        // 0x00525100: the target, refused, none.
        handlers.Mix(c2::Id(This<void*>(addr::cSC4CitySituationManager_TargetOccupant, m)));
        target->answer = 0x10;
        handlers.Mix(c2::Id(This<void*>(addr::cSC4CitySituationManager_TargetOccupant, m)));
        target->answer = 0;
        At<void*>(m, sf::kTarget) = nullptr;
        handlers.Mix(c2::Id(This<void*>(addr::cSC4CitySituationManager_TargetOccupant, m)));
        mix(handlers, target->refs);
        // 0x0052C190 for none and another advice (the active one tells the
        // advisor service, not called).
        call(addr::cSC4CitySituationManager_Succeed, nullptr);
        call(addr::cSC4CitySituationManager_Succeed, advices[0]);
        // 0x00526720: the automaton's place, the target's, none (the script
        // in any case; the rewards' slots answer false).
        At<void*>(m, sf::kScriptContext) = ctx;
        At<uint32_t>(m, sf::kFailureRef) = 77;
        ctx->answer = 1;
        sit->value = 0x55;
        c2::SetPos(autom, 100.0f, 5.0f, 200.0f);
        c2::SetPos(target, 180.5f, 9.0f, 260.25f);
        call(addr::cSC4CitySituationManager_Fail, sit);
        At<void*>(m, sf::kAuto) = nullptr;
        At<void*>(m, sf::kTarget) = target;
        call(addr::cSC4CitySituationManager_Fail, sit);
        At<void*>(m, sf::kTarget) = nullptr;
        call(addr::cSC4CitySituationManager_Fail, sit);
        call(addr::cSC4CitySituationManager_Fail, advices[0]);
        call(addr::cSC4CitySituationManager_Fail, nullptr);
        // 0x00525430: -2, no context, not a table, with the flag 0x20.
        using Script = void(__thiscall*)(void*, uint32_t, uint32_t);
        const auto script = reinterpret_cast<Script>(addr::cSC4CitySituationManager_RunScript);
        script(m, 9, 0xFFFFFFFEu);
        mix(handlers, 0);
        At<void*>(m, sf::kScriptContext) = nullptr;
        script(m, 9, 41);
        mix(handlers, 1);
        At<void*>(m, sf::kScriptContext) = ctx;
        ctx->answer = 0;
        script(m, 9, 41);
        mix(handlers, 2);
        ctx->answer = 1;
        sit->flags = 0x20;
        At<void*>(m, sf::kRefAF8) = touch;
        script(m, 9, 41);
        mix(handlers, 3);
        sit->flags = 0;
        At<void*>(m, sf::kRefAF8) = nullptr;
        At<void*>(m, sf::kScriptContext) = nullptr;
        At<uint32_t>(m, sf::kFailureRef) = 0xFFFFFFFEu;
        // 0x0052BF00 without a situation (the advisor service is not
        // told): +0xA64 with the target's occupant, refused.
        At<void*>(m, sf::kSituation) = nullptr;
        At<uint8_t>(m, sf::kByteA64) = 1;
        At<void*>(m, sf::kTarget) = target;
        This<void>(addr::cSC4CitySituationManager_Dismiss, m);
        mix(handlers, state());
        target->answer = 0x10;
        This<void>(addr::cSC4CitySituationManager_Dismiss, m);
        mix(handlers, state());
        target->answer = 0;
        At<uint8_t>(m, sf::kByteA64) = 0;
        At<void*>(m, sf::kTarget) = nullptr;
        // 0x0052BFB0: a child with an object (the joyride, slot 24 the
        // test's), a child without one, none.
        window->other = child;
        child->other2 = cand1;
        window->answer = 1;
        call(addr::cSC4CitySituationManager_DismissForJoyride, window);
        child->other2 = nullptr;
        call(addr::cSC4CitySituationManager_DismissForJoyride, window);
        window->answer = 0;
        call(addr::cSC4CitySituationManager_DismissForJoyride, window);
        // 0x0052CAB0 for the messages it answers without closing the popup.
        using Proc = uint8_t(__thiscall*)(void*, void*, uint32_t*);
        const auto proc = reinterpret_cast<Proc>(addr::cSC4CitySituationManager_PopupProc);
        uint32_t words[][3] = {{0xA2BF8AD5u, 0, 0}, {0xA2BF8AD6u, 0, 0}, {0x55u, 0, 0}, {3, 0x12345678u, 1}};
        for (auto& w : words)
            mix(handlers, proc(m + sf::kVtable10, window, w));
        handlers.Mix(cand1->refs);
    }

    // ---- The list (+0xA04).
    {
        c2::g_logEach = EnvInt("OPENSC4_TEST_C3_TRACE", 0) >= 2;
        using Do = uint8_t(__thiscall*)(void*, void*);
        const auto doMessage = reinterpret_cast<Do>(addr::cSC4CitySituationManager_DoMessage);
        At<void*>(m, sf::kSituation) = nullptr;
        At<void*>(m, sf::kAuto) = nullptr;
        At<uint32_t>(m, sf::kStatus) = 4;
        c3::SetGroups(advices[0], {0x7A5A0007u});
        c3::SetGroups(advices[1], {0x7A5A0009u});
        c3::SetGroups(advices[3], {0x7A5A0007u});
        Msg add = c3::Message(0x8A09FA64u);
        Msg remove = c3::Message(0x8A09FA65u);
        const auto sendList = [&](Msg& x, void* advice) {
            x.unknown = advice;
            const uint8_t answer = doMessage(m + 4, &x);
            list.Mix(entries());
            mix(list, answer);
        };
        // Added in turn (a candidate for the first and the fourth), again.
        for (int i = 0; i < 4; ++i)
            sendList(add, advices[i]);
        sendList(add, advices[0]);
        sendList(add, nullptr);
        // The candidate tracked: untracked from its first entry, which looks
        // again (the tracked automaton, relevant, on a later entry of a lower
        // priority value: not taken).
        ++cand1->refs;
        PushNode(m + sf::kTracked, reinterpret_cast<std::uintptr_t>(cand1), nullptr, 8);
        advices[3]->answer = 1;
        This<void>(addr::cSC4CitySituationManager_UntrackAutomaton, m, static_cast<void*>(cand1));
        mix(list, entries());
        This<void>(addr::cSC4CitySituationManager_UntrackAutomaton, m, static_cast<void*>(cand2)); // not tracked
        mix(list, entries());
        This<void>(addr::cSC4CitySituationManager_ReleaseTracked, m); // one tracked key at a time
        // An entry of a higher priority at the end shows a tracked automaton:
        // taken over.
        c2::Fake* const late = c3::MakeMore(0, c3::AdviceVtable());
        c2::Fake* const lateInner = c3::MakeMore(1, c3::AdviceVtable());
        late->other = lateInner;
        lateInner->value = 1;
        ++late->refs;
        ++cand2->refs;
        c2::PushEntry(m, late, cand2, 0);
        ++cand2->refs;
        PushNode(m + sf::kTracked, reinterpret_cast<std::uintptr_t>(cand2), nullptr, 8);
        This<void>(addr::cSC4CitySituationManager_AddEntry, m, static_cast<void*>(advices[3]));
        mix(list, entries());
        // At the most CSIs: a new advice's candidate given up for a later
        // entry's CSI of a lower priority value.
        const uint32_t most = Virtual<uint8_t>(m, sl::kGetMaxCsi);
        list.Mix(most);
        c2::Fake* const cand3 = c3::MakeMore(2, c3::AutomatonVtable());
        cand3->flags = 2;
        svc1->other = cand3;
        if (most <= 16) {
            for (uint32_t i = At<uint32_t>(m, sf::kTrackedCount); i < most; ++i) {
                c2::Fake* const filler = c3::MakeMore(4 + static_cast<int>(i), c3::AutomatonVtable());
                ++filler->refs;
                PushNode(m + sf::kTracked, reinterpret_cast<std::uintptr_t>(filler), nullptr, 8);
            }
            c2::Fake* const fresh = c3::MakeMore(3, c3::AdviceVtable());
            fresh->other = inners[1];
            c3::SetGroups(fresh, {0x7A5A0007u});
            ++cand2->refs;
            char* const head = At<char*>(m, sf::kList);
            At<void*>(At<char*>(head, 4), 0xC) = cand2; // the late entry shows it again
            c3::g_unordered = true;
            sendList(add, fresh);
            sendList(add, advices[2]); // no candidate
            // Removed while at the most: no candidates handed out.
            sendList(remove, fresh);
            c3::g_unordered = false;
            flags |= 4;
        }
        // The tracked released; removals hand candidates to the entries
        // without one.
        This<void>(addr::cSC4CitySituationManager_ReleaseTracked, m);
        list.Mix(entries());
        sendList(remove, advices[0]);
        sendList(remove, advices[1]);
        sendList(remove, late);
        sendList(remove, nullptr);
        advices[3]->answer = 0;
        // 0x0052A1E0 directly.
        for (c2::Fake* const a : {advices[0], advices[1], advices[2]}) {
            void* found = nullptr;
            void** const out =
                This<void**>(addr::cSC4CitySituationManager_FindCandidate, m, static_cast<void*>(&found),
                             static_cast<void*>(a));
            list.Mix(out == &found ? 1u : 0u);
            mix(list, c2::Id(found));
            if (found)
                Virtual<uint32_t>(found, 0x08);
        }
        // A list of the test's own: made (0x00527300), filled at its end
        // (0x00526440), sorted (0x00527920); merged with another
        // (0x00525AF0); destroyed.
        const auto makeList = [&]() {
            char* own = nullptr;
            This<void*>(addr::cSC4CitySituationManager_ListConstruct, static_cast<void*>(&own));
            return own;
        };
        const auto insert = [&](char*& own, void* advice, uint32_t id) {
            void* value[3] = {advice, nullptr, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id))};
            char* made = nullptr;
            char** const out = This<char**>(addr::cSC4CitySituationManager_ListInsert, static_cast<void*>(&own),
                                            static_cast<void*>(&made), own, static_cast<void*>(value));
            list.Mix(out == &made ? 1u : 0u);
        };
        const auto order = [&](char* own) {
            Hash s;
            for (char* node = At<char*>(own, 0); node != own; node = At<char*>(node, 0)) {
                s.Mix(c2::Id(At<void*>(node, 8)));
                s.Mix(At<uint32_t>(node, 0x10));
            }
            return s.h;
        };
        const auto destroy = [&](char*& own) {
            This<void>(addr::cSC4CitySituationManager_ClearList, static_cast<void*>(&own));
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(own);
        };
        using Sort = void(__cdecl*)(char**, uint32_t);
        using Merge = void(__cdecl*)(char**, char**, uint32_t);
        const auto sort = reinterpret_cast<Sort>(addr::cSC4CitySituationManager_SortList);
        const auto merge = reinterpret_cast<Merge>(addr::cSC4CitySituationManager_MergeList);
        char* a = makeList();
        void* const items[] = {advices[2], advices[0], nullptr, advices[3], late, advices[1], nullptr, advices[0]};
        uint32_t id = 1;
        for (void* const item : items)
            insert(a, item, id++);
        mix(list, order(a));
        sort(&a, 0);
        mix(list, order(a));
        sort(&a, 0); // sorted already
        mix(list, order(a));
        char* b = makeList();
        sort(&b, 0); // empty
        insert(b, advices[1], 20);
        sort(&b, 0); // one
        insert(b, late, 21);
        insert(b, nullptr, 22);
        insert(b, advices[2], 23);
        sort(&b, 0);
        mix(list, order(b));
        merge(&a, &b, 0);
        mix(list, order(a) ^ order(b) << 1);
        destroy(a);
        destroy(b);
    }

    c2::g_logEach = false;
    // ---- Put the fresh manager back and release it.
    c2::ClearEntries(m);
    This<void>(addr::cSC4CitySituationManager_ReleaseTracked, m);
    At<void**>(m, sf::kServices) = nullptr;
    At<void**>(m, sf::kServices + 4) = nullptr;
    At<void**>(m, sf::kServices + 8) = nullptr;
    for (const std::size_t at : {sf::kSituation, sf::kAuto, sf::kTarget})
        At<void*>(m, at) = nullptr;
    At<void*>(m, 0) = vtable;
    for (const std::size_t at : {sf::kRefA9C, sf::kRefAA0, sf::kRefAF8, sf::kScriptContext, sf::kRefA28,
                                 sf::kRefA1C})
        if (At<void*>(m, at))
            flags |= 0x10;
    list.Mix(Virtual<uint32_t>(m, sl::kRelease));
    c2::g_fresh = nullptr;
    for (int i = 0; i < c2::kFakes; ++i) {
        handlers.Mix(c2::g_fakes[i].refs);
        list.Mix(c2::g_more[i].refs);
    }
    flags |= 1;
    char text[96];
    std::snprintf(text, sizeof text, " E%08X,%08X,%08X,%08X,%X", msgs.h, frame.h, handlers.h, list.h, flags);
    g_situation3Text = text;
    log::Write("autotest: automata: situations3: messages %08X frame %08X handlers %08X list %08X flags %X", msgs.h,
               frame.h, handlers.h, list.h, flags);
}

// Part c4's drive (OPENSC4_TEST_AUTOMATA_SITUATIONS=4), see the top of the
// file. As parts c2 and c3: a fresh manager whose vtable is a copy (slots
// 24, 35, 36 and 38 the test's), objects of the test's own as arguments and
// fields; the game's globals read, never written. No situation starts, no
// message is posted, no camera effect made.

std::string g_situation4Text;

namespace c4 {

// The situation's floats (slots 7, 10, 11, 20, 21 and 52) and the target
// IDs (slot 16), the cell goal (slot 44) and the kind of slot 36.
struct SitState {
    float near = 0.0f;
    float needed = 0.0f;
    float drive = 0.0f;
    float radius = 0.0f;
    float escort = 0.0f;
    float reach = 0.0f;
    uint32_t goal = 0;
    uint32_t kind36 = 0;
    uint32_t* targets[3] = {};
    uint32_t ids[4] = {};
};
SitState g_sit;

float __fastcall SitNear(c2::Fake* f, void*)
{
    c2::Log(f, 0x1C);
    return g_sit.near;
}
float __fastcall SitNeeded(c2::Fake* f, void*)
{
    c2::Log(f, 0x28);
    return g_sit.needed;
}
float __fastcall SitDrive(c2::Fake* f, void*)
{
    c2::Log(f, 0x2C);
    return g_sit.drive;
}
void* __fastcall SitIds(c2::Fake* f, void*)
{
    c2::Log(f, 0x34);
    return &static_cast<c2::Fake*>(f->other2)->groupsBegin;
}
void* __fastcall SitTargets(c2::Fake* f, void*)
{
    c2::Log(f, 0x40);
    return g_sit.targets;
}
float __fastcall SitRadius50(c2::Fake* f, void*)
{
    c2::Log(f, 0x50);
    return g_sit.radius;
}
float __fastcall SitEscort(c2::Fake* f, void*)
{
    c2::Log(f, 0x54);
    return g_sit.escort;
}
uint32_t __fastcall SitKind36(c2::Fake* f, void*)
{
    c2::Log(f, 0x90);
    return g_sit.kind36;
}
uint32_t __fastcall SitGoal(c2::Fake* f, void*)
{
    c2::Log(f, 0xB0);
    return g_sit.goal;
}
void __fastcall SitGoalOut(c2::Fake* f, void*, uint32_t* out)
{
    c2::Log(f, 0xB4);
    *out = 0x7A5A0000u | g_sit.goal;
}
float __fastcall SitReach(c2::Fake* f, void*)
{
    c2::Log(f, 0xD0);
    return g_sit.reach;
}

// Automata, occupants and targets: their direction (slot 15, other's pos),
// position set (slot 9), slot 22 (true for the ID in value), slot 56 (a
// controller block whose +0x28 is other2), slot 68.
void __fastcall AutoSetPos(c2::Fake* f, void*, const float* p)
{
    c2::Log(f, 0x24);
    c2::g_log.MixBytes(p, 12);
}
void __fastcall AutoDirection(c2::Fake* f, void*, float* out)
{
    c2::Log(f, 0x3C);
    std::memcpy(out, static_cast<c2::Fake*>(f->other)->pos, 12);
}
uint32_t __fastcall AutoHas(c2::Fake* f, void*, uint32_t id)
{
    c2::Log(f, 0x58, id);
    return id == f->value ? 1u : 0u;
}
char g_controllerBlock[0x30];
void* __fastcall AutoController(c2::Fake* f, void*)
{
    c2::Log(f, 0xE0);
    At<void*>(g_controllerBlock, 0x28) = f->other2;
    return g_controllerBlock;
}
void __fastcall AutoTouch(c2::Fake* f, void*) { c2::Log(f, 0x110); }
// The cell filter's object: slot 7 at a cell, the answer's low byte.
uint32_t __fastcall CellAt(c2::Fake* f, void*, uint32_t x, uint32_t z)
{
    c2::Log(f, 0x1C, x, z);
    return 0x12345600u | (f->answer & 0xFF);
}

// The filters' occupants: 0xA821EF94 (refused with the answer's bit 0x10)
// gives other, whose slot 9 is true for its value.
uint32_t __fastcall QIFilter(c2::Fake* f, void*, uint32_t iid, void** out)
{
    c2::Log(f, 0x00, iid);
    if (iid != 0xA821EF94u || (f->answer & 0x10))
        return 0;
    *out = f->other;
    ++static_cast<c2::Fake*>(f->other)->refs;
    return 1;
}
uint32_t __fastcall HolderMatch(c2::Fake* f, void*, uint32_t v)
{
    c2::Log(f, 0x24, v);
    return v == f->value ? 1u : 0u;
}
// A script's object: 0x26BB767E (refused with bit 0x10) gives itself.
uint32_t __fastcall QIObject(c2::Fake* f, void*, uint32_t iid, void** out)
{
    c2::Log(f, 0x00, iid);
    if (iid != 0x26BB767Eu || (f->answer & 0x10))
        return 0;
    *out = f;
    ++f->refs;
    return 1;
}

// The script context: as part c2's, with the strings, the tables, the
// references and the object of a value. Slot 74 (nil) answers bit after bit
// of g_nil, one per call.
uint32_t g_nil = 0, g_nilCalls = 0;
void __fastcall LuaPushString(c2::Fake* f, void*, const char* s)
{
    c2::Log(f, 0x84);
    c2::g_log.MixBytes(s, std::strlen(s));
}
void __fastcall LuaGetGlobal(c2::Fake* f, void*, const char* s)
{
    c2::Log(f, 0x94);
    c2::g_log.MixBytes(s, std::strlen(s));
}
void __fastcall LuaGetTable(c2::Fake* f, void*, uint32_t i) { c2::Log(f, 0x98, i); }
uint32_t __fastcall LuaIsTableReally(c2::Fake* f, void*, uint32_t i)
{
    c2::Log(f, 0x120, i);
    return (f->answer >> 2) & 1;
}
uint32_t __fastcall LuaIsNil(c2::Fake* f, void*, uint32_t i)
{
    c2::Log(f, 0x128, i);
    return (g_nil >> (g_nilCalls++ & 31)) & 1;
}
void __fastcall LuaPop(c2::Fake* f, void*, uint32_t n) { c2::Log(f, 0x2C, n); }
uint32_t __fastcall LuaRef(c2::Fake* f, void*, uint32_t i)
{
    c2::Log(f, 0xC8, i);
    return ++f->value;
}
void __fastcall LuaUnref(c2::Fake* f, void*, uint32_t r) { c2::Log(f, 0xF8, r); }
void __fastcall LuaNewTable(c2::Fake* f, void*) { c2::Log(f, 0xB0); }
void __fastcall LuaSetIndex(c2::Fake* f, void*, uint32_t i) { c2::Log(f, 0xB8, i); }
void* __fastcall LuaObject(c2::Fake* f, void*, void* service, uint32_t i)
{
    c2::Log(f, 0x130, service ? 1u : 0u, i);
    return f->other;
}

#define C4_SLOT(off, fn) c2::Slot{off, reinterpret_cast<void*>(&fn)}

void** SituationVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C4_SLOT(0x00, c3::QIAdvice), C4_SLOT(0x0C, c2::SitInner), C4_SLOT(0x10, c2::SitKind),
            C4_SLOT(0x1C, SitNear), C4_SLOT(0x28, SitNeeded), C4_SLOT(0x2C, SitDrive), C4_SLOT(0x34, SitIds),
            C4_SLOT(0x3C, c2::SitFlag), C4_SLOT(0x40, SitTargets), C4_SLOT(0x44, c3::SitHasTargets),
            C4_SLOT(0x48, c2::SitGroups), C4_SLOT(0x50, SitRadius50), C4_SLOT(0x54, SitEscort),
            C4_SLOT(0x78, c2::SitTable), C4_SLOT(0x90, SitKind36), C4_SLOT(0xB0, SitGoal),
            C4_SLOT(0xB4, SitGoalOut), C4_SLOT(0xD0, SitReach)});
}
void** AutomatonVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C4_SLOT(0x00, c3::QIOccupant), C4_SLOT(0x0C, c2::AutoOther), C4_SLOT(0x14, c2::AutoSelf),
            C4_SLOT(0x1C, c2::AutoPos1C), C4_SLOT(0x20, c2::AutoPos20), C4_SLOT(0x24, AutoSetPos),
            C4_SLOT(0x3C, AutoDirection), C4_SLOT(0x58, AutoHas), C4_SLOT(0x78, c3::AutoSetFlag),
            C4_SLOT(0x7C, c2::AutoFlag), C4_SLOT(0x9C, c2::AutoTarget9C), C4_SLOT(0xA0, c2::AutoTargetA0),
            C4_SLOT(0xE0, AutoController), C4_SLOT(0xEC, c2::AutoKind), C4_SLOT(0xF4, c3::AutoTell),
            C4_SLOT(0x110, AutoTouch)});
}
void** CellObjectVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C4_SLOT(0x1C, CellAt)});
}
void** FilterOccupantVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C4_SLOT(0x00, QIFilter), C4_SLOT(0x20, c2::AutoPos20), C4_SLOT(0x58, AutoHas)});
}
void** HolderVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C4_SLOT(0x24, HolderMatch)});
}
void** ObjectVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C4_SLOT(0x00, QIObject)});
}
void** ContextVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C4_SLOT(0x18, c2::LuaTop), C4_SLOT(0x1C, c2::LuaSetTop), C4_SLOT(0x2C, LuaPop),
            C4_SLOT(0x38, c2::LuaIsNumber), C4_SLOT(0x5C, c2::LuaToNumber), C4_SLOT(0x7C, c2::LuaPushNumber),
            C4_SLOT(0x84, LuaPushString), C4_SLOT(0x94, LuaGetGlobal), C4_SLOT(0x98, LuaGetTable),
            C4_SLOT(0xAC, c2::LuaPushRef), C4_SLOT(0xB0, LuaNewTable), C4_SLOT(0xB8, LuaSetIndex),
            C4_SLOT(0xC8, LuaRef), C4_SLOT(0xCC, c2::LuaCall), C4_SLOT(0xF8, LuaUnref),
            C4_SLOT(0x11C, c2::LuaIsTable), C4_SLOT(0x120, LuaIsTableReally), C4_SLOT(0x128, LuaIsNil),
            C4_SLOT(0x130, LuaObject)});
}

#undef C4_SLOT

// A hash set or map of the test's own (0x14 bytes, SC4Uint32HashSet's).
struct Table {
    alignas(4) char bytes[0x14] = {};
    char* p() { return bytes; }
};

void MakeTable(Table& t, uint32_t buckets)
{
    std::memset(t.bytes, 0, sizeof t.bytes);
    c2::This<void>(addr::SC4Uint32HashSet_Construct, t.p(), buckets);
}

void DestroyTable(Table& t)
{
    c2::This<void>(addr::SC4Uint32HashSet_Clear, t.p());
    if (void* const buckets = At<void*>(t.p(), 4))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(buckets);
    std::memset(t.bytes, 0, sizeof t.bytes);
}

uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>((At<char*>(table, 8) - At<char*>(table, 4)) >> 2);
}

uint32_t CellKey(uint32_t x, uint32_t z) { return (x & 0xFFFF) | (z & 0xFFFF) << 16; }

// The walk of a cell set by SC4HashTable::Begin and 0x00525AA0: keys in
// order, the count.
uint32_t Walk(char* table)
{
    Hash s;
    struct {
        char* node;
        char* table;
    } it{};
    c2::This<void*>(addr::SC4HashTable_Begin, table, static_cast<void*>(&it));
    uint32_t n = 0;
    while (it.node && n < 4096) {
        s.Mix(At<uint32_t>(it.node, 4));
        ++n;
        char* next = At<char*>(it.node, 0);
        if (!next)
            next = c2::This<char*>(addr::cSC4CitySituationManager_CellSetNextBucket, static_cast<void*>(&it));
        it.node = next;
    }
    s.Mix(n);
    s.Mix(At<uint32_t>(table, 0x10));
    s.Mix(BucketCount(table));
    return s.h;
}

} // namespace c4

void Situations4Drive(cISC4City*)
{
    using c2::This;
    namespace sf = automata_situations::field;
    namespace sl = automata_situations::slot;
    Hash targets, steps, sets, filters;
    uint32_t flags = 0;
    using Factory = char*(__cdecl*)();
    char* const m = reinterpret_cast<Factory>(addr::cSC4CitySituationManager_Create)();
    if (!m) {
        g_situation4Text = " F-";
        return;
    }
    c2::g_fresh = m;
    Virtual<uint32_t>(m, sl::kAddRef);
    // The constructor seeds the random (+0x28) from the clock: a fixed seed.
    This<void*>(addr::SC4Random_ConstructSeeded, m + sf::kRandom, 0x7A5A4444u);
    static void* table[40];
    std::memcpy(table, reinterpret_cast<void*>(addr::cSC4CitySituationManager_Vtable), sizeof table);
    table[sl::kStartJoyride / 4] = reinterpret_cast<void*>(&c2::StubJoyride);
    table[sl::kSetUDrive / 4] = reinterpret_cast<void*>(&c3::StubUDrive);
    table[sl::kStartSituation / 4] = reinterpret_cast<void*>(&c2::StubStart);
    table[sl::kEndSituation / 4] = reinterpret_cast<void*>(&c2::StubEnd);
    void* const vtable = At<void*>(m, 0);
    At<void*>(m, 0) = table;

    const bool trace = EnvInt("OPENSC4_TEST_C4_TRACE", 0) != 0;
    c2::g_logEach = EnvInt("OPENSC4_TEST_C4_TRACE", 0) >= 2;
    uint32_t step = 0;
    const auto state = [&]() {
        Hash s;
        s.Mix(At<uint32_t>(m, sf::kStatus));
        s.Mix(c2::Id(At<void*>(m, sf::kSituation)) | c2::Id(At<void*>(m, sf::kAuto)) << 8 |
              c2::Id(At<void*>(m, sf::kTarget)) << 16 | c2::Id(At<void*>(m, sf::kRefAF8)) << 24);
        s.MixBytes(m + sf::kTargetTime, 0x1C); // +0xA7C to +0xA94
        s.Mix(At<uint32_t>(m, sf::kTargetIndex));
        s.Mix(At<uint32_t>(m, sf::kNumTargets));
        s.Mix(At<uint32_t>(m, sf::kZeroABC));
        s.Mix(At<uint32_t>(m, sf::kSetAA8Count));
        s.MixBytes(m + sf::kWordsADC, 0x1C); // the seven script references
        return s.h;
    };
    const auto mix = [&](Hash& h, uint32_t v) {
        h.Mix(v);
        h.Mix(state());
        h.Mix(c2::g_log.h);
        if (trace) {
            Hash refs;
            for (int i = 0; i < c2::kFakes; ++i)
                refs.Mix(c2::g_fakes[i].refs);
            log::Write("autotest: c4 trace %u: %08X %08X refs %08X state %08X", step, v, c2::g_log.h, refs.h, state());
        }
        ++step;
        c2::g_log = Hash{};
    };
    const auto mixd = [&](Hash& h, double v) {
        uint64_t b;
        std::memcpy(&b, &v, sizeof b);
        h.Mix(static_cast<uint32_t>(b >> 32));
        mix(h, static_cast<uint32_t>(b));
    };
    c2::g_log = Hash{};
    c4::g_sit = c4::SitState{};
    c4::g_nil = 0;
    c4::g_nilCalls = 0;

    c2::Fake* const ctx = c2::Make(0, c4::ContextVtable());
    c2::Fake* const svc = c2::Make(1, c3::ServiceVtable());
    c2::Fake* const sit = c2::Make(2, c4::SituationVtable());
    c2::Fake* const sitInner = c2::Make(3, c4::SituationVtable());
    c2::Fake* const autom = c2::Make(4, c4::AutomatonVtable());
    c2::Fake* const target = c2::Make(5, c4::AutomatonVtable());
    c2::Fake* const escortee = c2::Make(6, c4::AutomatonVtable());
    c2::Fake* const other = c2::Make(7, c4::AutomatonVtable());
    c2::Fake* const dir = c2::Make(8, c4::AutomatonVtable());
    c2::Fake* const touch = c2::Make(9, c2::HelperVtable());
    c2::Fake* const object = c2::Make(10, c4::ObjectVtable());
    c2::Fake* const idsOf = c2::Make(11, c4::SituationVtable());
    c2::Fake* const cellObject = c2::Make(12, c4::CellObjectVtable());
    c2::Fake* const occ1 = c2::Make(13, c4::FilterOccupantVtable());
    c2::Fake* const occ2 = c2::Make(14, c4::FilterOccupantVtable());
    c2::Fake* const holder = c2::Make(15, c4::HolderVtable());
    c2::Fake* mapped[3];
    for (int i = 0; i < 3; ++i)
        mapped[i] = c2::Make(16 + i, c2::HelperVtable());
    sit->other = sitInner;
    sitInner->value = 0x7A5A0042u;
    sit->other2 = idsOf;
    sit->value = 0x7A5A0078u; // its table
    autom->other = dir;
    escortee->other2 = autom;
    escortee->kind = 0x34;
    void* services[1] = {svc};
    At<void**>(m, sf::kServices) = services;
    At<void**>(m, sf::kServices + 4) = services + 1;
    At<void**>(m, sf::kServices + 8) = services + 1;

    // The city's size in cells and its middle in the world's units, for the
    // functions that ask the city's grid.
    void* const city = Global<void*>(addr::SC4_City);
    const int32_t cw = city ? Virtual<int32_t>(city, 0x174) : 0;
    const int32_t ch = city ? Virtual<int32_t>(city, 0x178) : 0;
    const uint32_t mx = static_cast<uint32_t>(cw / 2), mz = static_cast<uint32_t>(ch / 2);
    const float wx = static_cast<float>(mx * 16 + 8), wz = static_cast<float>(mz * 16 + 8);

    using Status = uint32_t(__thiscall*)(void*);
    using StatusDt = uint32_t(__thiscall*)(void*, float);
    const auto setStatus = [&](uint32_t s) {
        This<void>(addr::cSC4CitySituationManager_SetStatus, m, s);
        mix(targets, s);
    };

    // ---- The status (0x00524C40) and the time up (0x00524CF0).
    {
        const auto timeUp = [&]() { mix(targets, reinterpret_cast<Status>(addr::cSC4CitySituationManager_TimeUp)(m)); };
        At<uint32_t>(m, sf::kStatus) = 4;
        setStatus(3);
        setStatus(3);
        timeUp(); // no situation
        At<void*>(m, sf::kSituation) = sit;
        setStatus(1); // no automaton: 1.0
        At<void*>(m, sf::kAuto) = autom;
        setStatus(2); // no flag: 1.0
        setStatus(4);
        autom->flags = 0x8000000u;
        c4::g_sit.drive = 0.5f;
        setStatus(1);
        c4::g_sit.drive = 7.25f;
        setStatus(2);
        c4::g_sit.drive = std::numeric_limits<float>::quiet_NaN();
        setStatus(1);
        c4::g_sit.drive = 1.0f;
        setStatus(2);
        setStatus(3);
        timeUp();
        timeUp();
        autom->flags = 0;
    }

    // ---- The overlay's intensity (0x00524660).
    for (const float d : {0.0f, 10.0f, 100.0f, 250.0f, 1000.0f, -50.0f, 0.1f, 1e30f, -0.0f,
                          std::numeric_limits<float>::quiet_NaN()})
        mixd(targets, This<double>(addr::cSC4CitySituationManager_TargetIntensity, m, d));

    // ---- The cell sets and map of the test's own.
    {
        c4::Table a, b, u, map;
        c4::MakeTable(a, 20);
        const uint32_t n = c4::BucketCount(a.p());
        sets.Mix(n);
        struct Result {
            char* node;
            char* table;
            uint8_t inserted;
            uint8_t pad[3];
        };
        const auto insert = [&](char* t, uint32_t x, uint32_t z) {
            const uint32_t key = c4::CellKey(x, z);
            Result r{};
            Result* const out = This<Result*>(addr::cSC4CitySituationManager_CellSetInsert, t, static_cast<void*>(&r),
                                              static_cast<const void*>(&key));
            sets.Mix(out == &r ? 1u : 0u);
            sets.Mix(r.inserted | (r.table == t ? 0x100u : 0u) | (r.node ? 0x200u : 0u));
            sets.Mix(r.node ? At<uint32_t>(r.node, 4) : 0u);
        };
        const auto count = [&](char* t, uint32_t x, uint32_t z) {
            const uint32_t key = c4::CellKey(x, z);
            sets.Mix(This<uint32_t>(addr::cSC4CitySituationManager_CellSetCount, t, static_cast<const void*>(&key)));
        };
        const auto erase = [&](char* t, uint32_t x, uint32_t z) {
            const uint32_t key = c4::CellKey(x, z);
            mix(sets, This<uint32_t>(addr::cSC4CitySituationManager_CellSetErase, t, static_cast<const void*>(&key)));
        };
        // Cells sharing buckets (x + n), the high halves set, a duplicate.
        insert(a.p(), 3, 4);
        insert(a.p(), 3 + n, 4);
        insert(a.p(), 3 + 2 * n, 4);
        insert(a.p(), 1, 0);
        insert(a.p(), 0xFFFF, 0xFFFF);
        insert(a.p(), 3, 4);
        insert(a.p(), 9, 2);
        mix(sets, c4::Walk(a.p()));
        for (const uint32_t x : {3u, 3 + n, 1u, 2u, 9u})
            count(a.p(), x, 4);
        mix(sets, 0);
        // Its unique insert directly, then the resize and the copy.
        {
            const uint32_t key = c4::CellKey(7, 7);
            Result r{};
            This<Result*>(addr::cSC4CitySituationManager_CellSetInsertUnique, a.p(), static_cast<void*>(&r),
                          static_cast<const void*>(&key));
            mix(sets, r.inserted);
            This<Result*>(addr::cSC4CitySituationManager_CellSetInsertUnique, a.p(), static_cast<void*>(&r),
                          static_cast<const void*>(&key));
            mix(sets, r.inserted);
        }
        This<void>(addr::cSC4CitySituationManager_CellSetResize, a.p(), 5u); // fewer: nothing
        mix(sets, c4::Walk(a.p()));
        This<void>(addr::cSC4CitySituationManager_CellSetResize, a.p(), 300u);
        mix(sets, c4::Walk(a.p()));
        c4::MakeTable(b, 7);
        This<void>(addr::cSC4CitySituationManager_CellSetAssign, b.p(), static_cast<void*>(a.p()));
        mix(sets, c4::Walk(b.p()));
        // The erases: a node behind another, the first, none, all of one.
        erase(a.p(), 3 + n, 4);
        erase(a.p(), 3, 4);
        erase(a.p(), 5, 5);
        erase(b.p(), 3 + 2 * n, 4);
        erase(b.p(), 3, 4);
        erase(b.p(), 3 + n, 4);
        mix(sets, c4::Walk(a.p()) ^ c4::Walk(b.p()) << 1);
        // An empty copy.
        DestroyTable(a);
        c4::MakeTable(a, 3);
        This<void>(addr::cSC4CitySituationManager_CellSetAssign, b.p(), static_cast<void*>(a.p()));
        mix(sets, c4::Walk(b.p()));
        // The referenced set's insert (its keys referenced), cleared by
        // SC4RefHashSet::Clear (which releases them).
        c4::MakeTable(u, 10);
        for (void* const key : {static_cast<void*>(mapped[0]), static_cast<void*>(mapped[1]),
                                static_cast<void*>(mapped[0])}) {
            Result r{};
            This<Result*>(addr::cSC4CitySituationManager_Uint32SetInsert, u.p(), static_cast<void*>(&r),
                          static_cast<const void*>(&key));
            sets.Mix(mapped[0]->refs);
            mix(sets, r.inserted | At<uint32_t>(u.p(), 0x10) << 8);
        }
        This<void>(addr::SC4RefHashSet_Clear, u.p());
        sets.Mix(mapped[0]->refs);
        sets.Mix(mapped[1]->refs);
        // The cell -> object map: objects referenced, a duplicate, null.
        c4::MakeTable(map, 10);
        const uint32_t mn = c4::BucketCount(map.p());
        struct CellObject {
            uint32_t cell;
            void* object;
        };
        const CellObject values[] = {{c4::CellKey(2, 3), mapped[0]},
                                     {c4::CellKey(2 + mn, 3), mapped[1]},
                                     {c4::CellKey(2, 3), mapped[2]},
                                     {c4::CellKey(8, 1), nullptr}};
        for (const CellObject& v : values) {
            Result r{};
            This<Result*>(addr::cSC4CitySituationManager_CellMapInsertUnique, map.p(), static_cast<void*>(&r),
                          static_cast<const void*>(&v));
            mix(sets, r.inserted | (r.node ? c2::Id(At<void*>(r.node, 8)) << 8 : 0xFF00u));
        }
        for (int i = 0; i < 3; ++i)
            sets.Mix(mapped[i]->refs);
        // The map's objects given back before its nodes are freed.
        for (uint32_t i = 0; i < mn; ++i)
            for (char* node = At<char**>(map.p(), 4)[i]; node; node = At<char*>(node, 0))
                if (void* const o = At<void*>(node, 8))
                    Virtual<uint32_t>(o, sl::kRelease);
        DestroyTable(map);
        DestroyTable(u);
        DestroyTable(b);
        DestroyTable(a);
    }

    // ---- The manager's cells (+0xAA8): the nearest cell (0x00526960) and
    // the target placed (0x00529560).
    char* const cells = m + sf::kSetAA8;
    const auto addCell = [&](uint32_t x, uint32_t z) {
        const uint32_t key = c4::CellKey(x, z);
        char result[0xC] = {};
        This<void*>(addr::cSC4CitySituationManager_CellSetInsert, cells, static_cast<void*>(result),
                    static_cast<const void*>(&key));
    };
    const auto nearest = [&](uint32_t x, uint32_t z) {
        const uint32_t key = c4::CellKey(x, z);
        uint32_t out = 0x7A5A7A5Au;
        const uint8_t found = This<uint8_t>(addr::cSC4CitySituationManager_NearestCell, m,
                                            static_cast<const void*>(&key), static_cast<void*>(&out));
        targets.Mix(out);
        mix(targets, found);
    };
    const auto place = [&]() {
        This<void>(addr::cSC4CitySituationManager_PlaceCellTarget, m);
        mix(targets, 0);
    };
    if (cw > 8 && ch > 8 && c4::BucketCount(cells) && c4::BucketCount(m + sf::kObjects)) {
        flags |= 2;
        nearest(1, 1); // none
        At<uint32_t>(m, sf::kCell) = c4::CellKey(mx, mz);
        place(); // no cells
        addCell(mx + 3, mz);
        addCell(mx - 2, mz + 2);
        addCell(mx + 1, mz - 1);
        nearest(mx, mz);
        nearest(mx + 1, mz - 1); // held
        nearest(mx - 1, mz + 2);
        nearest(mx + 0x8000, mz); // the 16-bit differences
        nearest(0, 0);
        At<void*>(m, sf::kTarget) = target;
        place();
        At<uint32_t>(m, sf::kCell) = c4::CellKey(1, ch - 2);
        place();
        At<void*>(m, sf::kTarget) = nullptr;
    }

    // ---- The next target (0x0052BBC0, never past the last: 0x0052AD00
    // would start one) and a target reached (0x0052C0E0).
    {
        const auto next = [&]() {
            mix(targets, This<uint8_t>(addr::cSC4CitySituationManager_NextTarget, m));
        };
        const auto reached = [&]() {
            mix(targets, reinterpret_cast<Status>(addr::cSC4CitySituationManager_TargetReached)(m));
        };
        c4::g_sit.ids[0] = 11;
        c4::g_sit.ids[1] = 12;
        c4::g_sit.targets[0] = c4::g_sit.ids;
        c4::g_sit.targets[1] = c4::g_sit.ids + 2;
        c4::g_sit.targets[2] = c4::g_sit.ids + 4;
        At<void*>(m, sf::kSituation) = nullptr;
        next();
        At<void*>(m, sf::kSituation) = sit;
        next(); // slot 17 false
        sit->answer = 4;
        At<uint32_t>(m, sf::kTargetIndex) = 1;
        next(); // the last
        At<uint32_t>(m, sf::kTargetIndex) = 7;
        next();
        sit->answer = 0;
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        // Reached: no script by kind, then a script with targets that are
        // neither 0x5005 nor 0x5006.
        At<uint32_t>(m, sf::kStatus) = 3;
        At<int32_t>(m, sf::kTargetRef) = -2;
        for (const uint32_t kind : {1u, 3u, 4u, 2u}) {
            sit->kind = kind;
            At<uint32_t>(m, sf::kStatus) = 3;
            reached();
        }
        At<int32_t>(m, sf::kTargetRef) = 0x55;
        At<uint32_t>(m, sf::kStatus) = 3;
        reached(); // no target
        At<void*>(m, sf::kTarget) = target;
        target->value = 0x5007;
        reached();
        At<void*>(m, sf::kTarget) = nullptr;
        target->value = 0;
        At<int32_t>(m, sf::kTargetRef) = -2;
    }

    // ---- The kinds' checks directly, then the update (0x0052C8F0).
    {
        const auto check = [&](std::uintptr_t at, float dt) {
            mix(targets, reinterpret_cast<StatusDt>(at)(m, dt));
        };
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = autom;
        At<void*>(m, sf::kTarget) = target;
        c2::SetPos(autom, wx, 10.0f, wz);
        c2::SetPos(target, wx + 40.0f, 3.0f, wz);
        // The kind 1: outside the slot 7 (the clock zeroed), inside it (the
        // clock runs, then is enough), the status 1 (no next target).
        sit->kind = 1;
        c4::g_sit.reach = 5.0f;
        c4::g_sit.near = 20.0f;
        c4::g_sit.needed = 2.0f;
        At<float>(m, sf::kTargetTime) = 700.0f;
        At<uint32_t>(m, sf::kStatus) = 3;
        check(addr::cSC4CitySituationManager_CheckDistanceTarget, 100.0f);
        c4::g_sit.near = 40.0f;
        check(addr::cSC4CitySituationManager_CheckDistanceTarget, 900.0f);
        check(addr::cSC4CitySituationManager_CheckDistanceTarget, 1100.0f);
        At<uint32_t>(m, sf::kStatus) = 1;
        At<float>(m, sf::kTargetTime) = 0.0f;
        check(addr::cSC4CitySituationManager_CheckDistanceTarget, 10.0f);
        c4::g_sit.near = std::numeric_limits<float>::quiet_NaN();
        At<uint32_t>(m, sf::kStatus) = 3;
        check(addr::cSC4CitySituationManager_CheckDistanceTarget, 10.0f);
        c4::g_sit.near = 40.0f;
        c4::g_sit.needed = std::numeric_limits<float>::quiet_NaN();
        check(addr::cSC4CitySituationManager_CheckDistanceTarget, 10.0f);
        // The escort (the situation's flag 2): the escortee of the group
        // within the radius, the clock, outside, of another automaton, not
        // of the type 0x34.
        c3::SetGroups(sit, {0x7A5A0031u});
        svc->value = 0x7A5A0031u;
        svc->other = escortee;
        c2::SetPos(escortee, wx + 3.0f, 99.0f, wz + 4.0f);
        c4::g_sit.radius = 6.0f;
        c4::g_sit.escort = 1.5f;
        At<uint32_t>(m, sf::kStatus) = 3;
        At<float>(m, sf::kEscortTime) = 0.0f;
        check(addr::cSC4CitySituationManager_CheckEscort, 1000.0f);
        check(addr::cSC4CitySituationManager_CheckEscort, 600.0f);
        At<uint32_t>(m, sf::kStatus) = 3;
        c4::g_sit.radius = 5.0f; // exactly at the edge
        check(addr::cSC4CitySituationManager_CheckEscort, 100.0f);
        c4::g_sit.radius = 4.0f;
        check(addr::cSC4CitySituationManager_CheckEscort, 100.0f);
        c4::g_sit.radius = 6.0f;
        escortee->other2 = other;
        check(addr::cSC4CitySituationManager_CheckEscort, 100.0f);
        escortee->other2 = autom;
        escortee->kind = 0x33;
        check(addr::cSC4CitySituationManager_CheckEscort, 100.0f);
        escortee->kind = 0x34;
        svc->other = nullptr;
        check(addr::cSC4CitySituationManager_CheckEscort, 100.0f); // an empty set
        At<void*>(m, sf::kAuto) = nullptr;
        check(addr::cSC4CitySituationManager_CheckEscort, 100.0f);
        At<void*>(m, sf::kAuto) = autom;
        filters.Mix(escortee->refs);
        // The kind 2: in the middle; at the edge heading out (with the flag
        // 0x2000, then the links), heading in.
        sit->kind = 2;
        At<uint32_t>(m, sf::kStatus) = 3;
        if (cw > 8 && ch > 8) {
            check(addr::cSC4CitySituationManager_CheckEdgeTarget, 16.0f);
            c2::SetPos(autom, 8.0f, 10.0f, wz);
            c2::SetPos(dir, -1.0f, 0.0f, 0.0f);
            autom->flags = 0x2000;
            check(addr::cSC4CitySituationManager_CheckEdgeTarget, 16.0f);
            autom->flags = 0;
            At<uint32_t>(m, sf::kStatus) = 3;
            check(addr::cSC4CitySituationManager_CheckEdgeTarget, 16.0f);
            At<uint32_t>(m, sf::kStatus) = 3;
            c2::SetPos(dir, 1.0f, 0.0f, 0.0f);
            check(addr::cSC4CitySituationManager_CheckEdgeTarget, 16.0f);
            c2::SetPos(autom, wx, 10.0f, static_cast<float>(ch * 16 - 8));
            c2::SetPos(dir, 0.0f, 0.0f, 1.0f);
            autom->flags = 0x2000;
            check(addr::cSC4CitySituationManager_CheckEdgeTarget, 16.0f);
            autom->flags = 0;
        }
        c2::SetPos(dir, 0.0f, 0.0f, 0.0f);
        // The kind 5: cells within the reach erased and counted, the next
        // cell placed below the goal, the status 1 at it; the slot 36 kind 6.
        sit->kind = 5;
        At<uint32_t>(m, sf::kStatus) = 3;
        if (flags & 2) {
            c2::SetPos(autom, wx, 10.0f, wz);
            addCell(mx, mz);
            addCell(mx + 1, mz);
            addCell(mx, mz + 6);
            c4::g_sit.reach = 20.0f;
            c4::g_sit.goal = 9;
            At<uint32_t>(m, sf::kZeroABC) = 0;
            At<uint32_t>(m, sf::kCell) = c4::CellKey(mx, mz + 6);
            At<void*>(m, sf::kTarget) = target; // placed through the target (0x00528580 not reached)
            check(addr::cSC4CitySituationManager_CheckCellTargets, 16.0f);
            addCell(mx, mz);
            check(addr::cSC4CitySituationManager_CheckCellTargets, 16.0f);
            c4::g_sit.goal = 0;
            At<uint32_t>(m, sf::kZeroAC0) = 3;
            c4::g_sit.kind36 = 6;
            addCell(mx + 1, mz + 1);
            check(addr::cSC4CitySituationManager_CheckCellTargets, 16.0f);
            c4::g_sit.kind36 = 0;
            At<uint32_t>(m, sf::kZeroAC0) = 0;
        }
        check(addr::cSC4CitySituationManager_CheckCellTargets, 16.0f); // the goal 0: no placing
        At<void*>(m, sf::kTarget) = nullptr;

        // The update.
        const auto update = [&](float dt) { check(addr::cSC4CitySituationManager_UpdateTarget, dt); };
        At<void*>(m, sf::kSituation) = nullptr;
        update(16.0f);
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = nullptr;
        update(16.0f);
        At<void*>(m, sf::kAuto) = autom;
        At<void*>(m, sf::kTarget) = target;
        c4::g_sit.reach = 5.0f;
        c4::g_sit.near = 40.0f;
        c4::g_sit.needed = 100.0f;
        At<float>(m, sf::kTimeLimit) = 0.0f;
        for (const uint32_t kind : {1u, 2u, 5u, 4u}) {
            sit->kind = kind;
            At<uint32_t>(m, sf::kStatus) = 3;
            update(16.0f);
        }
        sit->kind = 1;
        At<void*>(m, sf::kTarget) = nullptr;
        At<uint32_t>(m, sf::kStatus) = 3;
        update(16.0f); // the kind 1 without a target
        At<void*>(m, sf::kTarget) = target;
        At<uint32_t>(m, sf::kStatus) = 4;
        update(16.0f); // not at 3
        // The escort first (flag 2), at the status 3.
        sit->flags = 2;
        svc->other = escortee;
        At<uint32_t>(m, sf::kStatus) = 3;
        update(16.0f);
        sit->flags = 0;
        svc->other = nullptr;
        // The check_success script: no context, then numbers, with the flag
        // 0x20 and +0xAF8, refused.
        At<int32_t>(m, sf::kTargetRef) = 0x66;
        At<uint32_t>(m, sf::kStatus) = 3;
        update(16.0f);
        At<void*>(m, sf::kScriptContext) = ctx;
        ctx->answer = 3;
        for (const double number : {2.0, 3.7, -1.0, 5e9, 1e20, -0.5}) {
            ctx->number = number;
            At<uint32_t>(m, sf::kStatus) = 3;
            update(16.0f);
        }
        ctx->number = std::numeric_limits<double>::quiet_NaN();
        At<uint32_t>(m, sf::kStatus) = 3;
        update(16.0f);
        sit->flags = 0x20;
        At<void*>(m, sf::kRefAF8) = touch;
        ctx->number = 3.0;
        At<uint32_t>(m, sf::kStatus) = 3;
        update(16.0f);
        At<void*>(m, sf::kRefAF8) = nullptr;
        sit->flags = 0;
        ctx->answer = 1; // not a number
        update(16.0f);
        ctx->answer = 0; // not a function
        update(16.0f);
        At<int32_t>(m, sf::kTargetRef) = -2;
        // The time limit: under 1, reached, not reached.
        sit->kind = 4;
        At<float>(m, sf::kTimeLimit) = 0.5f;
        At<float>(m, sf::kSituationTime) = 3.0f;
        At<uint32_t>(m, sf::kStatus) = 3;
        update(100.0f);
        At<float>(m, sf::kTimeLimit) = 4.0f;
        At<float>(m, sf::kSituationTime) = 3.95f;
        update(100.0f);
        At<float>(m, sf::kSituationTime) = 3.0f;
        At<uint32_t>(m, sf::kStatus) = 3;
        update(100.0f);
        At<float>(m, sf::kTimeLimit) = 0.0f;
        At<void*>(m, sf::kTarget) = nullptr;
    }

    // ---- The scripts' references (0x00525800, 0x005254D0).
    {
        using Load = void(__thiscall*)(void*, void*);
        const auto load = reinterpret_cast<Load>(addr::cSC4CitySituationManager_LoadScripts);
        const auto refAt = [&](uint32_t answer, uint32_t nil) {
            ctx->answer = answer;
            c4::g_nil = nil;
            c4::g_nilCalls = 0;
            int32_t out = -7;
            This<void>(addr::cSC4CitySituationManager_GetScriptRef, m, "find_target", 0x7A5A0010u,
                       static_cast<void*>(&out));
            mix(steps, static_cast<uint32_t>(out));
        };
        refAt(1, 1); // nil
        refAt(0, 0); // not a function
        refAt(1, 0);
        At<void*>(m, sf::kScriptContext) = nullptr;
        load(m, sit); // no context
        mix(steps, 0);
        At<void*>(m, sf::kScriptContext) = ctx;
        ctx->answer = 1; // not a table
        load(m, sit);
        mix(steps, 0);
        // A table: every function found; on_failure and get_time_limit
        // missing (the base table's found); the base missing too.
        ctx->answer = 1 | 4;
        c4::g_nil = 0;
        c4::g_nilCalls = 0;
        load(m, sit);
        mix(steps, 0);
        load(m, nullptr);
        mix(steps, 0);
        c4::g_nil = 1u << 1 | 1u << 6;
        c4::g_nilCalls = 0;
        load(m, sit);
        mix(steps, c4::g_nilCalls);
        load(m, nullptr);
        mix(steps, 0);
        c4::g_nil = 1u << 6 | 1u << 7;
        c4::g_nilCalls = 0;
        load(m, sit);
        mix(steps, c4::g_nilCalls);
        c4::g_nil = 1u << 6 | 1u << 8;
        c4::g_nilCalls = 0;
        load(m, nullptr);
        load(m, sit); // the base's get_time_limit nil
        mix(steps, c4::g_nilCalls);
        ctx->answer = 4; // functions refused: nothing referenced
        c4::g_nil = 0;
        load(m, nullptr);
        load(m, sit);
        mix(steps, 0);
        load(m, nullptr);
        mix(steps, 0);
        At<void*>(m, sf::kScriptContext) = nullptr;
        load(m, nullptr); // no context: the references only set to -2
        mix(steps, 0);
        At<void*>(m, sf::kScriptContext) = ctx;
    }

    // ---- The active automaton (0x00525070).
    {
        using SetAuto = void(__thiscall*)(void*, void*);
        const auto setAuto = [&](void* a) {
            reinterpret_cast<SetAuto>(addr::cSC4CitySituationManager_SetActiveAutomaton)(m, a);
            steps.Mix(autom->refs);
            steps.Mix(other->refs);
            mix(steps, c2::Id(At<void*>(m, sf::kAuto)));
        };
        At<void*>(m, sf::kAuto) = nullptr;
        setAuto(nullptr);
        setAuto(autom);
        setAuto(autom);
        setAuto(other);
        setAuto(nullptr);
    }

    // ---- The target IDs (0x00529AF0) and the target script (0x00524F00).
    {
        using Ids = void(__thiscall*)(void*, void*);
        const auto ids = reinterpret_cast<Ids>(addr::cSC4CitySituationManager_GetTargetIds);
        uint32_t* vec[3] = {};
        const auto get = [&]() {
            ids(m, vec);
            Hash s;
            for (uint32_t* p = vec[0]; p && p != vec[1]; ++p)
                s.Mix(*p);
            s.Mix(static_cast<uint32_t>(vec[1] - vec[0]));
            mix(steps, s.h);
        };
        c3::SetGroups(idsOf, {21, 22, 23});
        At<void*>(m, sf::kSituation) = nullptr;
        get();
        At<void*>(m, sf::kSituation) = sit;
        get(); // slot 13's
        sit->answer = 4;
        At<uint32_t>(m, sf::kTargetIndex) = 1;
        get();
        At<uint32_t>(m, sf::kTargetIndex) = 2; // past the end: zeroed
        get();
        sit->answer = 0;
        c3::SetGroups(idsOf, {});
        get();
        if (vec[0])
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(vec[0]);

        using Call = void*(__thiscall*)(void*, uint32_t, const void*);
        const auto call = reinterpret_cast<Call>(addr::cSC4CitySituationManager_CallTargetScript);
        uint32_t list[4] = {3, 0x80000000u, 0xFFFFFFFFu, 0};
        uint32_t* idv[2] = {list, list + 3};
        const auto run = [&]() {
            void* const got = call(m, 0x7A5A0050u, idv);
            steps.Mix(object->refs);
            mix(steps, c2::Id(got));
        };
        At<void*>(m, sf::kSituation) = nullptr;
        run();
        At<void*>(m, sf::kSituation) = sit;
        ctx->answer = 0; // not a function
        run();
        ctx->answer = 1;
        ctx->other = nullptr; // no object
        run();
        ctx->other = object;
        run();
        object->answer = 0x10; // refused
        run();
        object->answer = 0;
        idv[1] = list; // no IDs
        run();
        ctx->other = nullptr;
    }

    // ---- The cell filter (0x00524E90, stdcall) and the end without a
    // situation (0x005294C0).
    {
        using Filter = uint8_t(__stdcall*)(const uint16_t*, int32_t, uint32_t, const uint32_t*, uint32_t, int32_t,
                                           int32_t, void*);
        const auto filter = reinterpret_cast<Filter>(addr::cSC4CitySituationManager_CellFilter);
        const auto test = [&](uint16_t x, uint16_t z, int32_t want, uint32_t count, uint32_t limit, int32_t w,
                              int32_t h) {
            const uint16_t cell[2] = {x, z};
            mix(filters, filter(cell, want, 0x7A5Au, &count, limit, w, h, cellObject));
        };
        cellObject->answer = 0xFF; // -1
        test(1, 1, -1, 5, 5, 10, 10); // the count at the limit
        test(10, 1, -1, 0, 5, 10, 10);
        test(1, 10, -1, 0, 5, 10, 10);
        test(1, 1, 0xFF, 0, 5, 10, 10); // not sign-extended to 0xFF
        if (cw > 8 && ch > 8) {
            test(static_cast<uint16_t>(mx), static_cast<uint16_t>(mz), -1, 0, 5, cw, ch);
            test(1, 1, -1, 0, 5, cw, ch);
            cellObject->answer = 1;
            test(static_cast<uint16_t>(mx), static_cast<uint16_t>(mz), 1, 4, 5, cw, ch);
        }
        At<void*>(m, sf::kSituation) = nullptr;
        This<void>(addr::cSC4CitySituationManager_EndSituation, m);
        mix(steps, 0);
    }

    // ---- The two occupant filters' slot 4 (0x00524AB0, 0x00526650).
    {
        alignas(4) char radius[0x20] = {};
        alignas(4) char group[0x28] = {};
        uint32_t groups[2] = {0x7A5A0061u, 0x7A5A0062u};
        At<uint32_t>(radius, 0xC) = 0x7A5A0071u;
        At<uint32_t*>(group, 0xC) = groups;
        At<uint32_t*>(group, 0x10) = groups + 2;
        occ1->other = holder;
        occ2->other = holder;
        holder->value = 0x7A5A0071u;
        c2::SetPos(occ1, 30.0f, 5.0f, 40.0f);
        c2::SetPos(occ2, 100.0f, 5.0f, 40.0f);
        occ1->value = 0x7A5A0062u;
        occ2->value = 0x7A5A0063u;
        using Includes = uint8_t(__thiscall*)(void*, void*);
        const auto byRadius = reinterpret_cast<Includes>(addr::cSC4CitySituationManager_RadiusFilterIncludes);
        const auto byGroup = reinterpret_cast<Includes>(addr::cSC4CitySituationManager_GroupRadiusFilterIncludes);
        const auto both = [&](void* occ) {
            filters.Mix(byRadius(radius, occ));
            mix(filters, byGroup(group, occ));
        };
        for (const float r2 : {0.0f, 400.0f, 2500.0f, 100.0f * 100.0f, -1.0f}) {
            At<float>(radius, 0x10) = r2;
            At<float>(group, 0x18) = r2;
            At<float>(radius, 0x14) = 10.0f;
            At<float>(radius, 0x18) = 77.0f; // the height compared at
            At<float>(radius, 0x1C) = 40.0f;
            At<float>(group, 0x1C) = 10.0f;
            At<float>(group, 0x20) = 77.0f;
            At<float>(group, 0x24) = 40.0f;
            both(occ1);
            both(occ2);
        }
        At<float>(radius, 0x10) = 0.0f;
        holder->value = 1; // slot 9 false
        filters.Mix(byRadius(radius, occ1));
        holder->value = 0x7A5A0071u;
        occ1->answer = 0x10; // refused
        filters.Mix(byRadius(radius, occ1));
        occ1->answer = 0;
        mix(filters, byRadius(radius, nullptr));
        filters.Mix(holder->refs);
    }

    c2::g_logEach = false;
    // ---- Put the fresh manager back and release it.
    {
        using SetAuto = void(__thiscall*)(void*, void*);
        reinterpret_cast<SetAuto>(addr::cSC4CitySituationManager_SetActiveAutomaton)(m, nullptr);
    }
    At<void**>(m, sf::kServices) = nullptr;
    At<void**>(m, sf::kServices + 4) = nullptr;
    At<void**>(m, sf::kServices + 8) = nullptr;
    for (const std::size_t at : {sf::kSituation, sf::kAuto, sf::kTarget, sf::kScriptContext})
        At<void*>(m, at) = nullptr;
    for (std::size_t at = sf::kWordsADC; at < sf::kWordsADC + 0x1C; at += 4)
        if (At<int32_t>(m, at) != -2)
            flags |= 0x10;
    At<void*>(m, 0) = vtable;
    for (const std::size_t at : {sf::kRefA9C, sf::kRefAA0, sf::kRefAF8, sf::kRefA28, sf::kRefA1C})
        if (At<void*>(m, at))
            flags |= 0x10;
    sets.Mix(At<uint32_t>(m, sf::kSetAA8Count));
    filters.Mix(Virtual<uint32_t>(m, sl::kRelease));
    c2::g_fresh = nullptr;
    for (int i = 0; i < c2::kFakes; ++i)
        steps.Mix(c2::g_fakes[i].refs);
    flags |= 1;
    char text[96];
    std::snprintf(text, sizeof text, " F%08X,%08X,%08X,%08X,%X", targets.h, steps.h, sets.h, filters.h, flags);
    g_situation4Text = text;
    log::Write("autotest: automata: situations4: targets %08X steps %08X sets %08X filters %08X flags %X", targets.h,
               steps.h, sets.h, filters.h, flags);
}

// Part c5's drive (OPENSC4_TEST_AUTOMATA_SITUATIONS=5), see the top of the
// file. As part c4's: a fresh manager whose vtable is a copy (slots 24, 35,
// 36 and 38 the test's), objects of the test's own as arguments and fields.
// The city's grids, networks and path finder are asked as the game asks
// them (read only). Where a function would post a message, open the U-Drive
// or make a marker effect or lot query, the global it uses (0x00B43CCC,
// 0x00B21D74, 0x00B43D1C, 0x00B43D08) is swapped for one of the test's own
// for that call only and put back. The target setter 0x00528580 is never
// reached for a new target: a found target is the one already set.

std::string g_situation5Text;

namespace c5 {

// The situation's further slots: the radii (8, 9), the flags (14), the zone
// type (37), the site class (38), the occupant type (39), the rectangle
// (40), the offsets' list (41) and the counts (42, 43).
struct SitState {
    float inner = 0.0f;
    float outer = 0.0f;
    uint32_t want = 0;
    uint32_t site = 0;
    uint32_t type = 0;
    uint32_t least = 0;
    uint32_t most = 0;
    uint32_t rect[2] = {};
    char* head = nullptr;
};
SitState g_s;

float __fastcall SitInner(c2::Fake* f, void*)
{
    c2::Log(f, 0x20);
    return g_s.inner;
}
float __fastcall SitOuter(c2::Fake* f, void*)
{
    c2::Log(f, 0x24);
    return g_s.outer;
}
uint32_t __fastcall SitFlags(c2::Fake* f, void*)
{
    c2::Log(f, 0x38);
    return f->flags;
}
uint32_t __fastcall SitWant(c2::Fake* f, void*)
{
    c2::Log(f, 0x94);
    return g_s.want;
}
uint32_t __fastcall SitSite(c2::Fake* f, void*)
{
    c2::Log(f, 0x98);
    return g_s.site;
}
uint32_t __fastcall SitType(c2::Fake* f, void*)
{
    c2::Log(f, 0x9C);
    return g_s.type;
}
const uint32_t* __fastcall SitRect(c2::Fake* f, void*)
{
    c2::Log(f, 0xA0);
    return g_s.rect;
}
char** __fastcall SitList(c2::Fake* f, void*)
{
    c2::Log(f, 0xA4);
    return &g_s.head;
}
uint32_t __fastcall SitLeast(c2::Fake* f, void*)
{
    c2::Log(f, 0xA8);
    return g_s.least;
}
uint32_t __fastcall SitMost(c2::Fake* f, void*)
{
    c2::Log(f, 0xAC);
    return g_s.most;
}

// The automaton's 0x8B99EE7C interface for 0x00554EA0: +0x40 x, +0x48 z.
struct Face {
    void** vtable;
    uint32_t refs;
    char pad[0x38];
    float x, y, z;
};
static_assert(offsetof(Face, x) == 0x40 && offsetof(Face, z) == 0x48, "the interface's position");
Face g_face;

// Automata: as part c4's, QueryInterface also gives the face for 0x8B99EE7C
// with the answer's bit 0x20 (bit 0x10 refuses all).
uint32_t __fastcall QIAutomaton(c2::Fake* f, void*, uint32_t iid, void** out)
{
    c2::Log(f, 0x00, iid);
    if (f->answer & 0x10)
        return 0;
    if (iid == 0xA9B40F05u) {
        *out = f;
        ++f->refs;
        return 1;
    }
    if (iid == 0x8B99EE7Cu && (f->answer & 0x20)) {
        *out = &g_face;
        ++g_face.refs;
        return 1;
    }
    return 0;
}

// The cell filter's object: slot 7, a pattern of two zone types (1 and 2).
uint32_t __fastcall CellPattern(c2::Fake* f, void*, uint32_t x, uint32_t z)
{
    c2::Log(f, 0x1C, x, z);
    return 0x12345600u | (((x ^ z) & 3) == 3 ? 2u : 1u);
}

// The effects service (slot 7: an effect by name, refused with the answer's
// bit 1) and its effect (slots 3, 4 and 12; 4 is the stop of 0x00527800).
uint32_t __fastcall FxCreate(c2::Fake* f, void*, const char* name, void** out)
{
    c2::Log(f, 0x1C);
    if (name)
        c2::g_log.MixBytes(name, std::strlen(name));
    if ((f->answer & 1) || !f->other)
        return 0;
    *out = f->other;
    ++static_cast<c2::Fake*>(f->other)->refs;
    return 1;
}
void __fastcall FxRect(c2::Fake* f, void*, uint32_t n, const float* r, uint32_t c)
{
    c2::Log(f, 0x30, n, c);
    c2::g_log.MixBytes(r, 16);
}
void __fastcall FxStart(c2::Fake* f, void*, uint32_t v) { c2::Log(f, 0x0C, v); }
void __fastcall FxStop(c2::Fake* f, void*, uint32_t v) { c2::Log(f, 0x10, v); }

// The message server: slots 3 (send) and 4 (post); the message's type, first
// data word, first void and unknown.
void LogMessage(void* msg)
{
    const uint32_t type = Virtual<uint32_t>(msg, 0x10);
    // The first data word holds a pointer for some messages (the automaton's
    // part): as the hashes see pointers.
    uint32_t data = Virtual<uint32_t>(msg, 0x24);
    if (data >= 0x10000u)
        data = 0x10000u | c2::Id(reinterpret_cast<void*>(static_cast<std::uintptr_t>(data)));
    const uint32_t first = c2::Id(Virtual<void*>(msg, 0x28));
    const uint32_t unknown = c2::Id(Virtual<void*>(msg, 0x80));
    if (c2::g_logEach)
        log::Write("autotest: c5 message %08X %08X %02X %02X", type, data, first, unknown);
    c2::g_log.Mix(type);
    c2::g_log.Mix(data);
    c2::g_log.Mix(first);
    c2::g_log.Mix(unknown);
}
uint32_t __fastcall ServerSend(c2::Fake* f, void*, void* msg)
{
    c2::Log(f, 0x0C);
    LogMessage(msg);
    ++f->value;
    return 1;
}
uint32_t __fastcall ServerPost(c2::Fake* f, void*, void* msg, uint32_t flag)
{
    c2::Log(f, 0x10, flag);
    LogMessage(msg);
    ++f->value;
    return 1;
}

// The city's slot 71 object for the flag 0x20 (its slot 12 gives other).
void* __fastcall PickOther(c2::Fake* f, void*, void* key)
{
    c2::Log(f, 0x30, c2::Id(key));
    return f->other;
}

#define C5_SLOT(off, fn) c2::Slot{off, reinterpret_cast<void*>(&fn)}

void** SituationVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C5_SLOT(0x00, c3::QIAdvice), C5_SLOT(0x0C, c2::SitInner), C5_SLOT(0x10, c2::SitKind),
            C5_SLOT(0x1C, c4::SitNear), C5_SLOT(0x20, SitInner), C5_SLOT(0x24, SitOuter),
            C5_SLOT(0x28, c4::SitNeeded), C5_SLOT(0x2C, c4::SitDrive), C5_SLOT(0x34, c4::SitIds),
            C5_SLOT(0x38, SitFlags), C5_SLOT(0x3C, c2::SitFlag), C5_SLOT(0x40, c4::SitTargets),
            C5_SLOT(0x44, c3::SitHasTargets), C5_SLOT(0x48, c2::SitGroups), C5_SLOT(0x50, c4::SitRadius50),
            C5_SLOT(0x54, c4::SitEscort), C5_SLOT(0x78, c2::SitTable), C5_SLOT(0x90, c4::SitKind36),
            C5_SLOT(0x94, SitWant), C5_SLOT(0x98, SitSite), C5_SLOT(0x9C, SitType), C5_SLOT(0xA0, SitRect),
            C5_SLOT(0xA4, SitList), C5_SLOT(0xA8, SitLeast), C5_SLOT(0xAC, SitMost), C5_SLOT(0xB0, c4::SitGoal),
            C5_SLOT(0xB4, c4::SitGoalOut), C5_SLOT(0xD0, c4::SitReach)});
}
void** AutomatonVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(
        t, {C5_SLOT(0x00, QIAutomaton), C5_SLOT(0x0C, c2::AutoOther), C5_SLOT(0x14, c2::AutoSelf),
            C5_SLOT(0x1C, c2::AutoPos1C), C5_SLOT(0x20, c2::AutoPos20), C5_SLOT(0x24, c4::AutoSetPos),
            C5_SLOT(0x3C, c4::AutoDirection), C5_SLOT(0x58, c4::AutoHas), C5_SLOT(0x78, c3::AutoSetFlag),
            C5_SLOT(0x7C, c2::AutoFlag), C5_SLOT(0x9C, c2::AutoTarget9C), C5_SLOT(0xA0, c2::AutoTargetA0),
            C5_SLOT(0xE0, c4::AutoController), C5_SLOT(0xEC, c2::AutoKind), C5_SLOT(0xF4, c3::AutoTell),
            C5_SLOT(0x110, c4::AutoTouch)});
}
void** CellObjectVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C5_SLOT(0x1C, CellPattern)});
}
void** EffectsVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C5_SLOT(0x1C, FxCreate)});
}
void** EffectVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C5_SLOT(0x0C, FxStart), C5_SLOT(0x10, FxStop), C5_SLOT(0x30, FxRect)});
}
void** ServerVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C5_SLOT(0x0C, ServerSend), C5_SLOT(0x10, ServerPost)});
}
void** PickVtable()
{
    static void* t[80] = {};
    return c2::MakeVtable(t, {C5_SLOT(0x30, PickOther)});
}
void** FaceVtable()
{
    static void* t[8] = {};
    return c2::MakeVtable(t, {});
}

#undef C5_SLOT

// A global of the game swapped for the test's own while one call runs.
struct Swap {
    void** at;
    void* old;
    Swap(std::uintptr_t address, void* value) : at(reinterpret_cast<void**>(address)), old(*at) { *at = value; }
    ~Swap() { *at = old; }
    Swap(const Swap&) = delete;
    Swap& operator=(const Swap&) = delete;
};

// The kind 7's offsets: a circular list (+0 next, +4 previous, +8 and +0xA
// the int16 offsets from the city's middle).
struct OffsetNode {
    OffsetNode* next;
    OffsetNode* prev;
    int16_t dx, dz;
};
OffsetNode g_offsets[5];

void MakeOffsets()
{
    static const int16_t kOffsets[4][2] = {{1, -1}, {0, 2}, {1, -1}, {-3, 0}};
    OffsetNode* const head = &g_offsets[0];
    head->dx = head->dz = 0;
    OffsetNode* last = head;
    for (int i = 0; i < 4; ++i) {
        OffsetNode* const n = &g_offsets[i + 1];
        n->dx = kOffsets[i][0];
        n->dz = kOffsets[i][1];
        n->prev = last;
        last->next = n;
        last = n;
    }
    last->next = head;
    head->prev = last;
    g_s.head = reinterpret_cast<char*>(head);
}

} // namespace c5

void Situations5Drive(cISC4City*)
{
    using c2::This;
    namespace sf = automata_situations::field;
    namespace sl = automata_situations::slot;
    Hash cells, fills, targets, starts;
    uint32_t flags = 0;
    using Factory = char*(__cdecl*)();
    char* const m = reinterpret_cast<Factory>(addr::cSC4CitySituationManager_Create)();
    if (!m) {
        g_situation5Text = " G-";
        return;
    }
    c2::g_fresh = m;
    Virtual<uint32_t>(m, sl::kAddRef);
    This<void*>(addr::SC4Random_ConstructSeeded, m + sf::kRandom, 0x7A5A5555u);
    static void* table[40];
    std::memcpy(table, reinterpret_cast<void*>(addr::cSC4CitySituationManager_Vtable), sizeof table);
    table[sl::kStartJoyride / 4] = reinterpret_cast<void*>(&c2::StubJoyride);
    table[sl::kSetUDrive / 4] = reinterpret_cast<void*>(&c3::StubUDrive);
    table[sl::kStartSituation / 4] = reinterpret_cast<void*>(&c2::StubStart);
    table[sl::kEndSituation / 4] = reinterpret_cast<void*>(&c2::StubEnd);
    void* const vtable = At<void*>(m, 0);
    At<void*>(m, 0) = table;

    const bool trace = EnvInt("OPENSC4_TEST_C5_TRACE", 0) != 0;
    c2::g_logEach = EnvInt("OPENSC4_TEST_C5_TRACE", 0) >= 2;
    uint32_t step = 0;
    const auto state = [&]() {
        Hash s;
        s.Mix(At<uint32_t>(m, sf::kStatus));
        s.Mix(c2::Id(At<void*>(m, sf::kSituation)) | c2::Id(At<void*>(m, sf::kAuto)) << 8 |
              c2::Id(At<void*>(m, sf::kTarget)) << 16 | c2::Id(At<void*>(m, sf::kRefAF8)) << 24);
        s.MixBytes(m + sf::kTargetTime, 0x1C); // +0xA7C to +0xA94
        s.Mix(At<uint32_t>(m, sf::kNumTargets));
        s.Mix(At<uint32_t>(m, sf::kTargetIndex));
        s.Mix(At<uint8_t>(m, sf::kByteA64) | At<uint8_t>(m, sf::kJoyride) << 8 | At<uint8_t>(m, sf::kByteA99) << 16);
        s.Mix(At<uint32_t>(m, sf::kZeroABC));
        s.Mix(At<uint32_t>(m, sf::kZeroAC0));
        s.Mix(At<uint32_t>(m, sf::kSetAA8Count));
        s.Mix(At<uint32_t>(m, sf::kCell));
        s.MixBytes(m + sf::kWordsADC, 0x1C);
        return s.h;
    };
    const auto mix = [&](Hash& h, uint32_t v) {
        h.Mix(v);
        h.Mix(state());
        h.Mix(c2::g_log.h);
        if (trace) {
            Hash refs;
            for (int i = 0; i < c2::kFakes; ++i)
                refs.Mix(c2::g_fakes[i].refs);
            log::Write("autotest: c5 trace %u: %08X %08X refs %08X state %08X", step, v, c2::g_log.h, refs.h, state());
        }
        ++step;
        c2::g_log = Hash{};
    };
    c2::g_log = Hash{};
    c4::g_sit = c4::SitState{};
    c5::g_s = c5::SitState{};
    c5::MakeOffsets();

    c2::Fake* const ctx = c2::Make(0, c4::ContextVtable());
    c2::Fake* const svc = c2::Make(1, c3::ServiceVtable());
    c2::Fake* const sit = c2::Make(2, c5::SituationVtable());
    c2::Fake* const sitB = c2::Make(3, c5::SituationVtable());
    c2::Fake* const autom = c2::Make(4, c5::AutomatonVtable());
    c2::Fake* const target = c2::Make(5, c5::AutomatonVtable());
    c2::Fake* const made = c2::Make(6, c5::AutomatonVtable());
    c2::Fake* const cand = c2::Make(7, c5::AutomatonVtable());
    c2::Fake* const idsOf = c2::Make(8, c5::SituationVtable());
    c2::Fake* const cellObject = c2::Make(9, c5::CellObjectVtable());
    c2::Fake* const effects = c2::Make(10, c5::EffectsVtable());
    c2::Fake* const effect = c2::Make(11, c5::EffectVtable());
    c2::Fake* const lots = c2::Make(12, c2::LotVtable());
    c2::Fake* const lot = c2::Make(13, c2::LotVtable());
    c2::Fake* const helper = c2::Make(14, c2::HelperVtable());
    c2::Fake* const siteHolder = c2::Make(15, c5::AutomatonVtable());
    c2::Fake* const site = c2::Make(16, c5::AutomatonVtable());
    c2::Fake* const server = c2::Make(17, c5::ServerVtable());
    c2::Fake* const picker = c2::Make(18, c5::PickVtable());
    c2::Fake* const object = c2::Make(19, c4::ObjectVtable());
    c2::Fake* const dir = c2::Make(20, c4::AutomatonVtable());
    std::memset(&c5::g_face, 0, sizeof c5::g_face);
    c5::g_face.vtable = c5::FaceVtable();
    c5::g_face.refs = 100;
    sit->other = idsOf;
    sit->other2 = idsOf;
    sitB->other = idsOf;
    sitB->other2 = idsOf;
    autom->other = dir;
    effects->other = effect;
    lots->other = lot;
    lot->kind = 1;
    helper->other = siteHolder;
    siteHolder->other = site;
    picker->other = site;
    cand->flags = 2;
    ctx->other = object;
    c3::SetGroups(idsOf, {0x7A5A0021u, 0x7A5A0022u});
    void* services[1] = {svc};
    At<void**>(m, sf::kServices) = services;
    At<void**>(m, sf::kServices + 4) = services + 1;
    At<void**>(m, sf::kServices + 8) = services + 1;

    void* const city = Global<void*>(addr::SC4_City);
    const int32_t cw = city ? Virtual<int32_t>(city, 0x174) : 0;
    const int32_t ch = city ? Virtual<int32_t>(city, 0x178) : 0;
    const bool big = cw > 16 && ch > 16;
    const uint32_t mx = static_cast<uint32_t>(cw / 2), mz = static_cast<uint32_t>(ch / 2);
    const float wx = static_cast<float>(mx * 16 + 8), wz = static_cast<float>(mz * 16 + 8);
    // The network cell nearest the middle (the network map's slot 13, as
    // 0x00527D30 asks it), for the floods and the links; else the middle.
    uint32_t nx = mx, nz = mz;
    if (void* const grid = Global<void*>(addr::SC4_Service43D54); grid && big) {
        bool found = false;
        for (int32_t r = 0; r < 48 && !found; ++r)
            for (int32_t dz = -r; dz <= r && !found; ++dz)
                for (int32_t dx = -r; dx <= r && !found; ++dx) {
                    if (dx != -r && dx != r && dz != -r && dz != r)
                        continue;
                    const int32_t x = static_cast<int32_t>(mx) + dx, z = static_cast<int32_t>(mz) + dz;
                    if (x < 0 || z < 0 || x >= cw || z >= ch)
                        continue;
                    if (Virtual<void*>(grid, 0x34, static_cast<uint32_t>(x), static_cast<uint32_t>(z))) {
                        nx = static_cast<uint32_t>(x);
                        nz = static_cast<uint32_t>(z);
                        found = true;
                        flags |= 8;
                    }
                }
    }
    const float nwx = static_cast<float>(nx * 16 + 8), nwz = static_cast<float>(nz * 16 + 8);
    char* const set = m + sf::kSetAA8;
    const auto clearCells = [&]() {
        This<void>(addr::SC4Uint32HashSet_Clear, set);
        This<void>(addr::cSC4CitySituationManager_ReleaseObjects, m);
    };
    const auto mixCells = [&](Hash& h) {
        h.Mix(c4::Walk(set));
        h.Mix(At<uint32_t>(m + sf::kObjects, 0x10));
        h.Mix(effect->refs);
    };

    // ---- The cell sets by kind (0x0052C4E0 and its 0x0052B450, 0x005299C0,
    // 0x00529680), with the effects service of the test's own.
    if (big) {
        flags |= 2;
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = autom;
        At<void*>(m, sf::kTarget) = target; // placed by 0x00529560 (0x00528580 not reached)
        c2::SetPos(autom, wx, 10.0f, wz);
        c5::Swap fx(addr::SC4_Service43D1C, effects);
        const auto fill = [&](uint32_t kind) {
            c4::g_sit.kind36 = kind;
            const uint32_t r = This<uint32_t>(addr::cSC4CitySituationManager_FillCells, m);
            mixCells(fills);
            mix(fills, r & 0xFF);
            clearCells();
        };
        At<void*>(m, sf::kSituation) = nullptr;
        fill(4); // no situation
        At<void*>(m, sf::kSituation) = sit;
        fill(0);
        // The kind 4: a rectangle, an empty one; refused effects; none.
        c5::g_s.rect[0] = c4::CellKey(mx, mz);
        c5::g_s.rect[1] = c4::CellKey(mx + 2, mz + 1);
        fill(4);
        c5::g_s.rect[0] = c4::CellKey(mx + 2, mz);
        c5::g_s.rect[1] = c4::CellKey(mx, mz);
        fill(4);
        c5::g_s.rect[0] = c4::CellKey(mx, mz);
        c5::g_s.rect[1] = c4::CellKey(mx + 1, mz + 1);
        effects->answer = 1;
        fill(4);
        effects->answer = 0;
        {
            c5::Swap none(addr::SC4_Service43D1C, nullptr);
            fill(4);
        }
        // The kind 7: offsets from the middle, one twice.
        fill(7);
        // The kinds 1, 5 and 6 over the city's zones around the automaton.
        c5::g_s.inner = 16.0f;
        c5::g_s.outer = 80.0f;
        c5::g_s.least = 2;
        c5::g_s.most = 12;
        for (const uint32_t want : {0u, 1u, 0xFFu}) {
            c5::g_s.want = want;
            fill(1);
        }
        c5::g_s.want = 0;
        c5::g_s.least = 20; // above the most: cut to it
        c5::g_s.most = 5;
        fill(1);
        c5::g_s.outer = 0.5f; // below 1: the city's size
        c5::g_s.least = 3;
        c5::g_s.most = 6;
        fill(1);
        c5::g_s.outer = 80.0f;
        c5::g_s.least = 2;
        c5::g_s.most = 8;
        fill(5);
        c5::g_s.most = 10;
        fill(6);
        c5::g_s.inner = 2000.0f; // the whole box inside the near one
        fill(1);
        c5::g_s.inner = 16.0f;
        // The kind 2: the +0xAF8 site's lot (the lot manager the test's own).
        {
            At<void*>(m, sf::kRefAF8) = helper;
            c5::g_s.site = 0x193A;
            c5::Swap lotSwap(addr::SC4_LotManager, lots);
            const int32_t wide[4] = {static_cast<int32_t>(mx), static_cast<int32_t>(mz), static_cast<int32_t>(mx + 2),
                                     static_cast<int32_t>(mz + 1)};
            std::memcpy(lot->rect, wide, sizeof wide);
            c5::g_s.least = 12; // the goal
            c5::g_s.most = 4;   // the column limit
            fill(2);
            c5::g_s.most = 0;
            fill(2);
            const int32_t tall[4] = {static_cast<int32_t>(mx), static_cast<int32_t>(mz), static_cast<int32_t>(mx),
                                     static_cast<int32_t>(mz + 3)};
            std::memcpy(lot->rect, tall, sizeof tall);
            c5::g_s.least = 9;
            fill(2);
            lots->other = nullptr; // no lot
            fill(2);
            lots->other = lot;
            At<void*>(m, sf::kRefAF8) = nullptr;
            fills.Mix(site->refs);
        }
        // The kind 3: a flood from a network occupant near the automaton.
        c5::g_s.inner = 0.0f;
        c5::g_s.outer = 8.0f; // the box: plus or minus 64
        c5::g_s.least = 8;
        c5::g_s.most = 20;
        c2::SetPos(autom, nwx, 10.0f, nwz);
        for (const uint32_t type : {1u, 0xFFFFFFFFu}) {
            c5::g_s.type = type;
            fill(3);
        }
        c2::SetPos(autom, -40000.0f, 10.0f, -40000.0f); // nothing there
        fill(3);
        c2::SetPos(autom, wx, 10.0f, wz);
        At<void*>(m, sf::kTarget) = nullptr;
    }

    // ---- 0x00527EA0 over a set and an object of the test's own.
    {
        c4::Table t;
        c4::MakeTable(t, 20);
        const auto region = [&](int32_t x, int32_t z, int32_t want, uint32_t limit, int32_t w, int32_t h) {
            uint32_t found = 0;
            This<void>(addr::cSC4CitySituationManager_RegionFill, m, x, z, want, t.p(), &found, limit, w, h,
                       static_cast<void*>(cellObject));
            cells.Mix(c4::Walk(t.p()));
            mix(cells, found);
            This<void>(addr::SC4Uint32HashSet_Clear, t.p());
        };
        region(5, 5, 1, 40, 10, 10);
        region(0, 0, 1, 8, 10, 10);
        region(3, 0, 2, 40, 10, 10); // its cell refused
        region(9, 9, 1, 200, 10, 10);
        region(5, 5, 1, 0, 10, 10);
        region(2, 7, 1, 30, 4, 8); // outside the width
        c4::DestroyTable(t);
    }

    // ---- 0x00527D30 and 0x00528140 over the manager's set, the city's
    // network grid read.
    if (big) {
        This<void>(addr::SC4Uint32HashSet_Clear, set);
        const auto flood = [&](uint32_t x, uint32_t z, uint32_t direction, uint32_t kind, int32_t left) {
            int32_t remaining = left;
            This<void>(addr::cSC4CitySituationManager_FloodCells, m, c4::CellKey(x, z), direction, kind, &remaining);
            cells.Mix(c4::Walk(set));
            mix(cells, static_cast<uint32_t>(remaining));
            This<void>(addr::SC4Uint32HashSet_Clear, set);
        };
        for (uint32_t d = 0; d < 5; ++d)
            flood(nx, nz, d, 1, 6);
        for (uint32_t d = 0; d < 4; ++d)
            flood(nx, nz, d, 0xFFFFFFFFu, 12);
        flood(nx, nz, 0x103, 0xFFFFFFFFu, 4); // the direction's low byte
        flood(mx, mz, 2, 0xFFFFFFFFu, 12);
        flood(0, 0, 0, 0xFFFFFFFFu, 5);
        flood(0, 0, 1, 0xFFFFFFFFu, 5);
        flood(static_cast<uint32_t>(cw), 3, 2, 0xFFFFFFFFu, 5);
        flood(3, static_cast<uint32_t>(ch), 3, 0xFFFFFFFFu, 5);
        flood(mx, mz, 1, 0xFFFFFFFFu, 0);
        const auto extend = [&](int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t count, uint32_t goal) {
            const int32_t rect[4] = {x0, z0, x1, z1};
            This<void>(addr::cSC4CitySituationManager_ExtendLotCells, m, static_cast<const void*>(rect), count, goal);
            cells.Mix(c4::Walk(set));
            mix(cells, goal);
            This<void>(addr::SC4Uint32HashSet_Clear, set);
        };
        const int32_t x = static_cast<int32_t>(mx), z = static_cast<int32_t>(mz);
        extend(x - 3, z, x + 3, z + 1, 0, 20);
        extend(x, z - 2, x, z + 2, 0, 12);
        extend(x, z - 2, x + 1, z + 2, 5, 12);
        extend(0, 0, 2, 0, 0, 9);
        extend(cw - 3, ch - 1, cw - 1, ch - 1, 0, 9); // the last row: before it only
        extend(0, 0, 0, 1, 0, 7);
        extend(x, z, x + 2, z, 3, 0);
        extend(x, z, x + 2, z, 9, 9);
    }

    // ---- 0x0052B200 by the script and the services.
    {
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = autom;
        c2::SetPos(autom, wx, 10.0f, wz);
        c5::g_s.inner = 10.0f;
        c5::g_s.outer = 40.0f;
        At<int32_t>(m, 0xAEC) = -2;
        const auto find = [&](uint32_t assign) {
            At<uint8_t>(m, sf::kByteA64) = 0;
            void* const got = This<void*>(addr::cSC4CitySituationManager_FindTarget, m, assign);
            targets.Mix(made->refs);
            targets.Mix(object->refs);
            mix(targets, c2::Id(got));
        };
        At<void*>(m, sf::kSituation) = nullptr;
        find(0);
        At<void*>(m, sf::kSituation) = sit;
        svc->answer = 0; // no place
        find(0);
        svc->answer = 1;
        c2::SetPos(svc, wx + 20.0f, 5.0f, wz - 20.0f);
        svc->other2 = nullptr; // none made
        find(0);
        svc->other2 = made;
        find(0);
        At<void*>(m, sf::kTarget) = made; // already the target
        find(1);
        At<void*>(m, sf::kTarget) = nullptr;
        // The find_target script.
        At<void*>(m, sf::kScriptContext) = ctx;
        ctx->answer = 1;
        At<int32_t>(m, 0xAEC) = 0x57;
        find(0);
        ctx->other = nullptr; // no object: the services
        find(0);
        ctx->other = object;
        At<int32_t>(m, 0xAEC) = -2;
        At<void*>(m, sf::kScriptContext) = nullptr;
        // No services.
        At<void**>(m, sf::kServices + 4) = services;
        find(0);
        At<void**>(m, sf::kServices + 4) = services + 1;
        svc->other2 = nullptr;
        svc->answer = 0;
    }

    // ---- 0x0052AD00 by kind, and 0x00554EA0.
    {
        At<int32_t>(m, 0xAE8) = -2;
        const auto next = [&](uint32_t assign) {
            void* const got = This<void*>(addr::cSC4CitySituationManager_StartTarget, m, assign);
            targets.Mix(cand->refs);
            targets.Mix(site->refs);
            targets.Mix(c5::g_face.refs);
            mix(targets, c2::Id(got));
        };
        At<void*>(m, sf::kAuto) = nullptr;
        next(0);
        At<void*>(m, sf::kAuto) = autom;
        for (const uint32_t kind : {0u, 4u, 5u}) {
            sit->kind = kind;
            next(0);
        }
        // The kind 4's create_target script.
        sit->kind = 4;
        At<void*>(m, sf::kScriptContext) = ctx;
        At<int32_t>(m, 0xAE8) = 0x58;
        next(0);
        At<int32_t>(m, 0xAE8) = -2;
        At<void*>(m, sf::kScriptContext) = nullptr;
        // The flag 0x20 with 0x193A among the IDs: the +0xAF8 site.
        sit->flags = 0x20;
        At<void*>(m, sf::kRefAF8) = helper;
        c3::SetGroups(idsOf, {0x7A5A0021u, 0x193Au});
        next(0);
        c3::SetGroups(idsOf, {0x7A5A0021u, 0x7A5A0022u}); // not among them: by the kind
        next(0);
        At<void*>(m, sf::kRefAF8) = nullptr;
        sit->flags = 0;
        // The kinds 1 and 3: a candidate of the services' groups by distance.
        svc->value = 0x7A5A0022u;
        svc->other = cand;
        for (const uint32_t kind : {1u, 3u}) {
            sit->kind = kind;
            for (const float dx : {30.0f, 50.0f, 5.0f, 40.0f}) {
                c2::SetPos(cand, wx + dx, 99.0f, wz);
                next(0);
            }
        }
        sit->kind = 1;
        c2::SetPos(cand, wx + 5.0f, 99.0f, wz);
        At<uint32_t>(m, sf::kTargetIndex) = 1; // no near radius
        next(0);
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        cand->flags = 3; // not a candidate
        next(0);
        cand->flags = 2;
        c2::SetPos(cand, wx + 30.0f, 99.0f, wz);
        At<void*>(m, sf::kTarget) = cand; // already the target
        next(1);
        At<void*>(m, sf::kTarget) = nullptr;
        // None near: the network service's occupants around the automaton.
        svc->other = nullptr;
        next(0);
        c2::SetPos(autom, nwx, 10.0f, nwz);
        next(0);
        c2::SetPos(autom, -40000.0f, 10.0f, -40000.0f);
        next(0);
        c2::SetPos(autom, wx, 10.0f, wz);
        // The kind 2: the nearest link of the automaton's interface.
        sit->kind = 2;
        next(0); // no interface
        autom->answer = 0x20;
        c5::g_face.x = nwx;
        c5::g_face.z = nwz;
        next(0);
        autom->answer = 0;
        // 0x00554EA0 directly.
        const auto link = [&](float x, float z) {
            c5::g_face.x = x;
            c5::g_face.z = z;
            void* const got = This<void*>(addr::cSC4CitySituationManager_NearestLinkOccupant, &c5::g_face);
            mix(targets, c2::Id(got));
        };
        link(nwx, nwz);
        link(wx, wz);
        link(8.0f, 8.0f);
        link(-500.0f, 99999.0f);
        sit->kind = 0;
    }

    // ---- Slot 36 (0x0052E750), the message server the test's own.
    {
        c5::Swap post(addr::SC4_MessageServer, server);
        c5::Swap fx(addr::SC4_Service43D1C, effects);
        void* const city71 = At<void*>(m, sf::kCity71);
        const auto drop = [&]() {
            if (void* const old = At<void*>(m, sf::kSituation)) {
                At<void*>(m, sf::kSituation) = nullptr;
                Virtual<uint32_t>(old, sl::kRelease);
            }
            clearCells();
        };
        const auto start = [&](void* situation, void* automaton, uint32_t notify) {
            const uint32_t r = This<uint32_t>(addr::cSC4CitySituationManager_StartSituation, m, situation, automaton, notify);
            starts.Mix(sit->refs);
            starts.Mix(autom->refs);
            starts.Mix(server->value);
            mixCells(starts);
            mix(starts, r & 0xFF);
        };
        At<void*>(m, sf::kSituation) = nullptr;
        At<void*>(m, sf::kAuto) = nullptr;
        At<void*>(m, sf::kTarget) = nullptr;
        start(nullptr, autom, 1); // the active one: nothing
        sit->kind = 2;
        start(sit, nullptr, 1); // no automaton
        drop();
        start(sit, autom, 1);
        drop();
        start(sit, autom, 0); // started, not posted: the status 5
        drop();
        // The kind 5: the cells (a rectangle), none.
        sit->kind = 5;
        c4::g_sit.kind36 = 4;
        c5::g_s.rect[0] = c4::CellKey(mx, mz);
        c5::g_s.rect[1] = c4::CellKey(mx + 1, mz + 2);
        At<void*>(m, sf::kTarget) = target;
        start(sit, autom, 1);
        drop();
        c4::g_sit.kind36 = 0;
        start(sit, autom, 1);
        drop();
        At<void*>(m, sf::kTarget) = nullptr;
        // The targets' kinds: the count from slot 17, the flags 0x100 and 1.
        sit->kind = 1;
        sit->answer = 4;
        c4::g_sit.ids[0] = 11;
        c4::g_sit.ids[1] = 12;
        c4::g_sit.ids[2] = 13;
        c4::g_sit.targets[0] = c4::g_sit.ids;
        c4::g_sit.targets[1] = c4::g_sit.ids + 3;
        svc->answer = 1;
        svc->other2 = made;
        sit->flags = 0x100;
        At<void*>(m, sf::kTarget) = made; // what 0x0052B200 finds
        start(sit, autom, 1);
        drop();
        sit->flags = 0;
        sit->answer = 0;
        svc->value = 0x7A5A0022u;
        svc->other = cand;
        c2::SetPos(cand, wx + 30.0f, 99.0f, wz);
        At<void*>(m, sf::kTarget) = cand; // what 0x0052AD00 finds
        start(sit, autom, 1);
        drop();
        svc->other = nullptr;
        c2::SetPos(autom, -40000.0f, 10.0f, -40000.0f);
        At<void*>(m, sf::kTarget) = nullptr;
        start(sit, autom, 1); // none: not started
        drop();
        sit->flags = 1; // then 0x0052B200
        At<void*>(m, sf::kTarget) = made;
        start(sit, autom, 1);
        drop();
        c2::SetPos(autom, wx, 10.0f, wz);
        At<void*>(m, sf::kTarget) = nullptr;
        sit->flags = 0;
        sit->kind = 4;
        start(sit, autom, 1);
        drop();
        // The flag 0x20: the city's slot 71 pick into +0xAF8; a second
        // situation over the first (slot 38, then the swap).
        sit->kind = 2;
        sit->flags = 0x20;
        At<void*>(m, sf::kCity71) = picker;
        start(sit, autom, 1);
        sitB->kind = 2;
        start(sitB, autom, 1);
        starts.Mix(sitB->refs);
        drop();
        At<void*>(m, sf::kCity71) = city71;
        This<void>(addr::SC4RefPtr_Assign, static_cast<void*>(&At<void*>(m, sf::kRefAF8)), static_cast<void*>(nullptr));
        starts.Mix(site->refs);
        sit->flags = 0;
        sit->kind = 0;
        svc->other2 = nullptr;
        svc->answer = 0;
    }

    // ---- StartJoyride (0x00524E20) and 0x00524D80, with the message server
    // the test's own and the U-Drive's view set (0x00506850 does nothing).
    {
        c5::Swap post(addr::SC4_MessageServer, server);
        alignas(8) static char view[0x100] = {};
        void* const shown = Global<void*>(addr::SC4_UDriveMode);
        c5::Swap udrive(addr::SC4_UDriveMode, shown ? shown : static_cast<void*>(view));
        using Joyride = void(__thiscall*)(void*, void*, uint32_t, void*);
        const auto joyride = reinterpret_cast<Joyride>(addr::cSC4CitySituationManager_StartJoyride);
        joyride(m, autom, 1, target);
        mix(starts, server->value);
        joyride(m, nullptr, 0, nullptr);
        mix(starts, server->value);
        At<uint8_t>(m, sf::kJoyride) = 0;
        using Send = void(__thiscall*)(void*, uint32_t, void*, uint32_t);
        const auto send = reinterpret_cast<Send>(addr::cSC4CitySituationManager_SendSituationMessage);
        send(m, 0xAC083D7Du, sit, 3);
        mix(starts, server->value);
        send(m, 0x7A5A0001u, nullptr, 0);
        mix(starts, server->value);
        flags |= 4;
    }

    // ---- The next target started (0x0052BBC0, 0x0052C0E0).
    {
        c5::Swap post(addr::SC4_MessageServer, server);
        using Status = uint32_t(__thiscall*)(void*);
        const auto nextTarget = [&]() {
            mix(targets, This<uint8_t>(addr::cSC4CitySituationManager_NextTarget, m));
        };
        const auto reached = [&]() {
            mix(targets, reinterpret_cast<Status>(addr::cSC4CitySituationManager_TargetReached)(m));
        };
        At<void*>(m, sf::kSituation) = sit;
        At<void*>(m, sf::kAuto) = autom;
        c2::SetPos(autom, wx, 10.0f, wz);
        sit->answer = 4;
        c4::g_sit.targets[0] = c4::g_sit.ids;
        c4::g_sit.targets[1] = c4::g_sit.ids + 2;
        // The kind 4 without a script: none.
        sit->kind = 4;
        At<int32_t>(m, 0xAE8) = -2;
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        nextTarget();
        // The kind 1: the candidate, already the target.
        sit->kind = 1;
        svc->value = 0x7A5A0022u;
        svc->other = cand;
        c2::SetPos(cand, wx + 30.0f, 99.0f, wz);
        At<void*>(m, sf::kTarget) = cand;
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        nextTarget();
        nextTarget(); // past the last
        // Reached: the kind 3 without a script, the next one.
        sit->kind = 3;
        At<int32_t>(m, sf::kTargetRef) = -2;
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        At<uint32_t>(m, sf::kStatus) = 3;
        reached();
        // A script and the target of 0x5005: the next one by 0x0052AD00.
        sit->kind = 1;
        cand->value = 0x5005;
        At<int32_t>(m, sf::kTargetRef) = 0x59;
        At<uint32_t>(m, sf::kStatus) = 3;
        reached();
        cand->value = 0;
        At<int32_t>(m, sf::kTargetRef) = -2;
        At<void*>(m, sf::kTarget) = nullptr;
        svc->other = nullptr;
        sit->answer = 0;
        sit->kind = 0;
        At<uint32_t>(m, sf::kTargetIndex) = 0;
        At<uint32_t>(m, sf::kStatus) = 0;
    }

    c2::g_logEach = false;
    // ---- Put the fresh manager back and release it.
    {
        using SetAuto = void(__thiscall*)(void*, void*);
        reinterpret_cast<SetAuto>(addr::cSC4CitySituationManager_SetActiveAutomaton)(m, nullptr);
    }
    clearCells();
    At<void**>(m, sf::kServices) = nullptr;
    At<void**>(m, sf::kServices + 4) = nullptr;
    At<void**>(m, sf::kServices + 8) = nullptr;
    for (const std::size_t at : {sf::kSituation, sf::kAuto, sf::kTarget, sf::kScriptContext})
        At<void*>(m, at) = nullptr;
    At<int32_t>(m, 0xAE8) = -2;
    At<int32_t>(m, 0xAEC) = -2;
    for (std::size_t at = sf::kWordsADC; at < sf::kWordsADC + 0x1C; at += 4)
        if (At<int32_t>(m, at) != -2)
            flags |= 0x10;
    At<void*>(m, 0) = vtable;
    for (const std::size_t at : {sf::kRefA9C, sf::kRefAA0, sf::kRefAF8, sf::kRefA28, sf::kRefA1C})
        if (At<void*>(m, at))
            flags |= 0x10;
    cells.Mix(At<uint32_t>(m, sf::kSetAA8Count));
    starts.Mix(Virtual<uint32_t>(m, sl::kRelease));
    c2::g_fresh = nullptr;
    for (int i = 0; i < c2::kFakes; ++i)
        starts.Mix(c2::g_fakes[i].refs);
    starts.Mix(c5::g_face.refs);
    flags |= 1;
    char text[96];
    std::snprintf(text, sizeof text, " G%08X,%08X,%08X,%08X,%X", cells.h, fills.h, targets.h, starts.h, flags);
    g_situation5Text = text;
    log::Write("autotest: automata: situations5: cells %08X fills %08X targets %08X starts %08X flags %X", cells.h,
               fills.h, targets.h, starts.h, flags);
}

void Drive(const Day& day)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 2 || !day.counted || g_written)
        return;
    if (day.day != day.first + EnvInt("OPENSC4_TEST_AUTOMATA_DAY", 3))
        return;
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 1 && day.city && game::IsBuild641() && g_awText.empty())
        AwManagerDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 2 && day.city && game::IsBuild641() && g_aw2Text.empty())
        AwLifeDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 3 && day.city && game::IsBuild641() && g_aw3Text.empty())
        AwAutoDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 4 && day.city && game::IsBuild641() && g_aw4Text.empty())
        AwBehaviourDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_AWMGR", 0) >= 5 && day.city && game::IsBuild641() && g_aw5Text.empty())
        AwWaterDrive5(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 1 && day.city && game::IsBuild641() && g_managerText.empty()) {
        ManagerDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 2)
            ManagerSaveDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_VEHMGR", 0) >= 3)
            ManagerFrameDrive(day.city);
    }
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 1 && day.city && game::IsBuild641() && g_scriptText.empty()) {
        ScriptsDrive(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 2)
            ScriptsDrive2(day.city);
        if (EnvInt("OPENSC4_TEST_AUTOMATA_SCRIPTS", 0) >= 3)
            ScriptsDrive3(day.city);
    }
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SITUATIONS", 0) >= 1 && day.city && game::IsBuild641() &&
        g_situationText.empty())
        SituationsDrive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SITUATIONS", 0) >= 2 && day.city && game::IsBuild641() &&
        g_situation2Text.empty())
        Situations2Drive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SITUATIONS", 0) >= 3 && day.city && game::IsBuild641() &&
        g_situation3Text.empty())
        Situations3Drive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SITUATIONS", 0) >= 4 && day.city && game::IsBuild641() &&
        g_situation4Text.empty())
        Situations4Drive(day.city);
    if (EnvInt("OPENSC4_TEST_AUTOMATA_SITUATIONS", 0) >= 5 && day.city && game::IsBuild641() &&
        g_situation5Text.empty())
        Situations5Drive(day.city);
    if (char* const manager = Manager(day.city)) {
        WriteDrive(manager);
        QueryDrive(manager);
        if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) >= 3)
            ControllerDrive(manager);
    }
}

void Fingerprint(cISC4City* city, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_AUTOMATA", 0) < 1)
        return;
    char* const manager = Manager(city);
    if (!manager) {
        line += " | auto none";
        return;
    }
    uint32_t occupants = 0, controllers = 0, sum = 0;
    ForEach<ControllerNode>(Table(manager, field::kControllers), [&](ControllerNode* node) {
        ++occupants;
        Hash entry;
        entry.Mix(OccupantHash(node->occupant));
        uint32_t count = 0, inner = 0;
        for (ListNode* e = node->head->next; e != node->head; e = e->next) {
            ++count;
            inner += ControllerHash(e->value);
        }
        controllers += count;
        entry.Mix(count);
        entry.Mix(inner);
        sum += entry.h;
    });
    const Grid* const grid = reinterpret_cast<const Grid*>(manager + field::kGrid);
    uint32_t filled = 0, entries = 0, gridSum = 0;
    const uint32_t cells = static_cast<uint32_t>(grid->end - grid->begin);
    for (uint32_t i = 0; i < cells; ++i) {
        ListNode* const head = grid->begin[i];
        if (!head)
            continue;
        uint32_t here = 0;
        for (ListNode* e = head->next; e != head; e = e->next) {
            ++here;
            Hash h;
            h.Mix(i);
            h.Mix(ControllerHash(e->value));
            gridSum += h.h;
        }
        entries += here;
        filled += here ? 1u : 0u;
    }
    Hash time;
    time.MixBytes(manager + field::kTimeA, 4);
    time.MixBytes(manager + field::kTimeB, 2);
    time.MixBytes(manager + field::kTimeC, 4);
    time.MixBytes(manager + field::kTimeD, 2);
    char text[192];
    std::snprintf(text, sizeof text, " | auto n%u/%u c%08X g%u,%u,%u,%08X", occupants, controllers, sum,
                  static_cast<unsigned>(grid->cellsX), filled, entries, gridSum);
    line += text;
    if (g_written)
        line += g_writeText + g_queryText + g_controllerText;
    line += g_managerText;
    line += g_scriptText;
    line += g_awText;
    line += g_aw2Text;
    line += g_aw3Text;
    line += g_aw4Text;
    line += g_aw5Text;
    line += g_situationText;
    line += g_situation2Text;
    line += g_situation3Text;
    line += g_situation4Text;
    line += g_situation5Text;
    // The automaton table: frame-driven, the log only.
    uint32_t parked = 0, automata = 0, parkedSum = 0;
    ForEach<AutomatonNode>(Table(manager, field::kAutomata), [&](AutomatonNode* node) {
        ++parked;
        Hash entry;
        entry.Mix(OccupantHash(node->occupant));
        uint32_t count = 0;
        for (ListNode* e = node->value.head->next; e != node->value.head; e = e->next)
            ++count;
        automata += count;
        entry.Mix(count);
        entry.Mix(node->value.count);
        entry.MixBytes(node->value.first, sizeof node->value.first);
        entry.MixBytes(node->value.second, sizeof node->value.second);
        entry.Mix(node->value.flag);
        parkedSum += entry.h;
    });
    static uint32_t lines = 0;
    if (lines++ < 64)
        log::Write("autotest: automata: %u automaton entries, %u automata (%08X), time %.9g %d %.9g %d (%08X)", parked,
                   automata, parkedSum, static_cast<double>(At<float>(manager, field::kTimeA)),
                   At<int16_t>(manager, field::kTimeB), static_cast<double>(At<float>(manager, field::kTimeC)),
                   At<int16_t>(manager, field::kTimeD), time.h);
}

const FingerprintPart kFingerprint{"automata", 241, &Fingerprint};
const DayHook kDrive{"automata.write", 241, &Drive};

} // namespace
} // namespace opensc4::autotest

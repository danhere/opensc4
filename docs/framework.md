# The game framework (GZCOM)

The framework is the layer under the game: the object that runs the program (start-up, the main loop, shutdown), the COM object that creates classes by ID, the directors that register those classes, the system services and the message server. This is roadmap item P9, "own GZCOM framework implementation". First a survey of the code in `SimCity 4.exe` 1.1.641, then what is ours so far: the message server.

Everything here was read from the disassembly. Names marked "ours" are not the game's.

## Survey

### Where the code is

The framework is not one block. The linker kept each library's object files together, and the framework's pieces sit among other library code:

| Range | What |
|---|---|
| 0x00472E10-0x00473A40 | The game's subclasses of the framework, the message server and two more, with header inlines (in the game's own code) |
| 0x0087931B-0x0087B0DB | The framework object (79 functions) |
| 0x0087B0DC-0x0087B212 | The app base class and the exe's root director |
| 0x0087B23D-0x0087C114 | The COM object and the library record (60 functions) |
| 0x008FF000-0x0090AD00 | An HTML parser (not framework) |
| 0x0090AD00-0x0090CC00 | Picture codecs' director and factories (not framework) |
| 0x0090CE94-0x0090D0A0 | The STL allocator (0x0090CF54 allocate, 0x0090CF63 free) |
| 0x0090D96B-0x0090DDD5 | Reference-count base, a list of interface pointers (vtable 0x00ACE650) |
| 0x0090DDF1-0x0090E242 | The director base class, cRZCOMDllDirector (18 functions) |
| 0x0090E1D1-0x0090FC30 | The string class (cRZString; vtable 0x00A80810, 29 slots) |
| 0x00910000-0x00913245 | Operating system helpers, dates, version |
| 0x00913292-0x009134FF | The standard message (cGZMessage; vtable 0x00ACEC00, 38 slots; constructor 0x009134D6) and the allocator 0x009133DA |
| 0x00913E16-0x00917ADB | Command line, the variant class (128 slots) |
| 0x009182FF-0x0091DB73 | Files and folders (cRZFile, 33 slots), streams |
| 0x0091DD53-0x0091DE4B | The system service base (constructor 0x0091DE0F) |
| 0x0091DE4B-0x00920BF0 | Ini files, the exception report and minidump writer |
| 0x00920BF0-0x00929000 | Clipboard, language and country tables, date and time formats |
| 0x00929000-0x0092B200 | The command server and command generator |
| 0x0092B25B-0x0092CF00 | Database record streams |
| 0x0092CF67-0x0092EBFD | The message queue, the two message servers (about 110 functions, most of them container code) |
| 0x0092EBDB-0x00930600 | A timer-like service (vtable 0x00AD1C60), shared small functions |
| 0x00930B02-0x00930BC3 | The critical section wrapper (constructor 0x00930B91, vtable 0x00AD1EF4) |
| 0x00932000-0x00936000 | A callback service, the debug log (groups, back ends, levels) |
| 0x00936000-0x00940000 | Resource manager set-up, the sound engine's tracks (not framework) |

About 34,556 functions make up the exe; the framework proper (framework object, COM object, director base, service base, message servers and queue, critical section, message) is about 300 of them.

### The framework object

Size 0x150, built in WinMain's helper 0x0044C170; `RZGetFramework` (0x008793EC, 909 call sites) returns the global 0x00B540AC.

- Vtable 0x00AC3090, 49 slots. Slots 0-39 are gzcom-dll's `cIGZFrameWork` in order. Slot 23 (0x008793F9) is GetCOMObject, slot 24 (0x00472E50) GetState. Slots 40-48 are not in the header: 40 the deleting destructor, 41-43 start the services in three bands of priority, 44-46 stop them, 47 is empty, 48 is the main loop.
- Second vtable at +4 (0x00AC3070): `cIGZFrameWorkW32` (instance handle +0x148, main window +0x14C).
- The COM object is a member, at +0x08.
- Members: +0x80 reference count; +0x84 the hooks (a list); +0x88 the services by priority (a tree); +0x94 the services by ID (a hash table); +0xA8 the idle list; +0xB0 the tick list, kept in descending tick priority; +0xAC and +0xB4 keep services that left a list while it was being called; +0xB8 tick on; +0xBC idle interval (4); +0xC0 frame counter; +0xC4 idle counter; +0xCC state; +0xD0-+0xD4 one "busy" byte per container (a change asked for while it is set is refused); +0xD8, +0xDC nesting depths; +0xE0 the object's one lock; +0xE8 the command line; +0x140, +0x144 quit flag and code.
- The app is a global (0x00B540B4), not a member; the exit code is 0x00B540A8.
- The game has a subclass (vtable 0x00A8C970, constructor 0x00472E10) whose OnTick (0x00473440) and OnIdle (0x004735F0) are the base's inside a try block; a caught exception becomes a message of type 0x49A780B2. The byte 0x00B21428 chooses between the subclass and the base, here and for the message server (0x004505B4). In our runs it is clear: the base classes run (measured for the message server, below). What sets it was not found.

Start-up (0x0087B0B9 = init 0x0087AF13, run 0x0087957A, shutdown 0x0087AB07):

| State | What happens |
|---|---|
| 0 | The COM object's RealInit (the root director gets InitializeCOM); the COM object added as a service; three app callbacks that are empty in SimCity 4 |
| 1 | The app's PreFrameWorkInit (cSC4App, 0x00452360): builds and adds every director of the exe (0x004493B0: all class registrations), creates the app's services (0x00450350: the message server among them), then scans the two Plugins folders for DLLs (0x00451260). Each DLL: load, `GZDllGetGZCOMDirector`, InitializeCOM, EnumClassObjects, OnStart. Then the hooks' PreFrameWorkInit |
| 3 | Services of priority above -2,000,000 started (slot 41); the hooks' PreAppInit |
| 4 | The app's own Init (slot 42); the tick switched on |
| 5 | The remaining services (slot 43) |
| 6 | The app's PostFrameWorkInit; the hooks' PostAppInit |
| 7 | The main loop (slot 48, 0x008795BD) |

The main loop: Windows messages until there are none, then one OnIdle (slot 16). OnIdle counts the frame, calls OnTick (slot 15) with the frame number, and on every fourth frame calls the idle list's services. OnTick copies the tick list under the lock and calls each service's OnTick outside it. The framework reads no clock and never sleeps; the argument of a service's OnTick is the frame number.

Shutdown: states 8-10, the hooks' PreAppShutdown, the app's PreFrameWorkShutdown, the services stopped in reverse bands (slots 44-46) with the hooks' PostAppShutdown between, the containers emptied, the COM object's RealShutdown (plugin DLLs freed), states 13-14.

### The COM object

A member of the framework (+0x08; constructor 0x0087B9A1; at most 0x78 bytes). Vtable 0x00AC3368, 14 slots, gzcom-dll's `cIGZCOM` in order (the two EnumerateLibraries overloads reversed): 3 GetClassObject 0x0087BE75, 4 FrameWork, 5 AddLibrary 0x0087C081, 6 FreeUnusedLibraries, 7-8 CreateGuid(s), 9 RealInit 0x0087BCDD, 10 RealShutdown, 11 SetServiceRunning, 12-13 EnumerateLibraries. It is a system service too (ID 0xA3CD8DB3).

- Members: +0x24 its lock; +0x34 the class map, a tree from class ID to {version, library}; +0x40 the libraries, a tree ordered by path; +0x50 the framework; +0x54 the root director; +0x58 a random generator and +0x70 a time, for GUIDs.
- **The class map holds the classes of loaded DLLs only.** GetClassObject looks there first; a hit goes to that library's director. A miss goes to the exe's root director (class 0x3E8 is the framework itself). So a DLL's class always wins over an exe class of the same ID, and among DLLs the higher version, then the one loaded first.
- AddLibrary: load, the director's InitializeCOM, EnumClassObjects into the class map, OnStart.
- `0x0090DDF1` (307 call sites; "GZCOM()" in our address files) is the root director's GZCOM getter; 0x0087B212 returns the root director (a static, 0x00B540B8).

### Directors

`cRZCOMDllDirector` (constructor 0x0090E18D, 0x4C bytes, 17 slots): +0x24 the COM object, +0x28 the framework, +0x2C child directors (a vector), +0x38 a hash table from class ID to {factory, type}. Type 1 factories return an object that is then asked for the interface; type 2 take the interface ID themselves.

- AddCls 0x0090E133 (460 call sites, all in director constructors; the first registration of an ID wins) and 0x0090E160 (type 2, 4 sites); AddDirector 0x0090E0D4 (it moves the child's own children up, so the list is flat).
- GetClassObject 0x0090DFAC: the children in order, the first that answers wins, then its own table.
- The exe has 26 director classes under the root (0x00AC3248, which registers nothing). The game's own (0x00A87E70, constructor 0x00460090) registers 190 classes; the sound director (0x00A93E18) 92 and adds five more directors when it starts. The list with IDs and counts is in the survey notes of 2026-10-11 (local, `analysis/`).
- Plugin DLLs carry their own copy of this class (gzcom-dll's), so the exe's copy serves the exe's directors only.

### System services

The base (constructor 0x0091DE0F: ID, priority) has 13 slots: after the three of cIGZUnknown, GetServiceID, SetServiceID, GetServicePriority, IsServiceRunning, SetServiceRunning, Init, Shutdown, OnTick, OnIdle, GetServiceTickPriority. 40 constructors call it across the exe. A service joins the tick or the idle list by asking the framework (AddToTick, AddToOnIdle); the framework adds none itself.

### What cannot be swapped during a run

core.dll is injected before the game's first instruction (phase Attach), so a detour installed there sees everything. From a director's first call on (phase DirectorInit, in state 1) these have already run or are on the stack and run to their end as compiled:

- the framework's and the COM object's constructors, the globals, the command line;
- RealInit, the root director, every class registration and AddDirector of the exe;
- the creation of the app's services, the message server among them;
- the start-up sequence itself (0x0087AF13: the order of states, hooks and service bands), cSC4App's PreFrameWorkInit, the DLL scan.

A replacement of the main loop (0x008795BD) installed after state 7 does nothing: the original is running and returns only at quit.

Called again and again for the whole run, so replaceable at any time on the game's own objects: GetClassObject and the directors' lookup, GetSystemService and the service, hook and tick list methods, OnTick and OnIdle, the message server.

Folded functions limit detours: small getters (the COM object's FrameWork at 0x009D41A1, AddRef thunks, seven of the director's 17 slots) are shared with unrelated classes and must be left or replaced in the vtable; container helpers are shared template code with up to 72 callers.

## The message server

`cGZMessageServer2` (class ID 0x252294E9, service ID 0x04FA845B; 0xB0 bytes; constructor 0x0092EB23; vtable 0x00AD1BF4, 14 slots = gzcom-dll's `cIGZMessageServer2`). The app creates it in state 1 (0x004505DA): by class ID through the COM object first, with `new` (0x00450607) if that fails; the global 0x00B43CCC points to it. This happens before the plugin DLLs are scanned, so the class ID always gives the exe's class: a plugin cannot take the server's place by registering the ID. Simulators and windows ask it for messages by type; anyone sends or posts one.

| Offset | Member |
|---|---|
| +0x00 | vtable; +0x04 the system service part; +0x08 a third part with the reference count |
| +0x20, +0x28, +0x30 | three locks: the table, the queue, the direct posts |
| +0x38 | the message queue, an object of its own (vtable 0x00AD1AD0, 25 slots) |
| +0x58 | messages taken from the queue so far (64 bits) |
| +0x60 | byte: empty the queue on every tick (1 from the constructor; the app sets it to 0 right after creating the server, 0x00450652) |
| +0x64 | busy: the table is being read or changed |
| +0x68 | pending changes: a list of {add or remove, target, type} |
| +0x6C | the table: a hash table from message type to a list of targets |
| +0x80 | direct posts left in the delivery pass under way |
| +0x84 | the direct posts: a deque of {target, message} |

How it works:

- **AddNotification, RemoveNotification** (slots 5, 6): while the server is busy the change joins the pending list; else it is made in the table (a target is in a type's list once; a type's list is dropped with its last target) and the pending list is replayed through the same two slots.
- **MessageSend** (slot 3): applies the pending list, then under the table's lock collects the targets of the message's type and after them those of type 0 (every message), and with the lock released calls each one's DoMessage in that order. A target removed during the delivery still gets the message being delivered.
- **MessagePost** (slot 4) puts the message at the queue's back, or its front with high priority. **OnTick** (slot 9, from the framework's tick through the service part) takes messages from the queue one at a time and sends each through slot 3; it goes on until the queue is empty (without the +0x60 byte: for as many as were queued when it began). Then it delivers the direct posts.
- **GeneralMessagePostToTarget** (slot 7) queues a message for one target; the delivery pass (0x0092DFA3) takes as many as there were when it began.
- **CreateMessage** (slot 13) makes a standard message for class 0x46DF45E7 and nothing else. It returns a bool in AL (gzcom-dll declares a pointer).

Findings:

- The game also has a subclass (vtable 0x00A8CAFC, constructor 0x00472EB0) whose MessageSend (0x004737E0) is the same with the delivery in a try block. The coverage meter shows the base's MessageSend running and the subclass's never: the server is the base class in our runs.
- **CancelGeneralMessagePostsToTarget (slot 8) has a fault.** It walks the deque and erases with an iterator it has already stepped forward. That holds only when the erased post lies in the deque's first half (the erase then moves the posts in front of it). In the second half the posts behind move down: the walk skips one, and when the erased post was the last it walks past the end. Ours does the same steps in the same order, so it has the same fault, unless `fixes.message_cancel` is on (below).
- Who calls it: no code reaches the function except through the two vtables (a scan of the whole code section for calls and jumps, and of the whole image for pointers, 2026-10-11; the same holds for every function of the server: the only direct calls come from inside the class and from the subclass's thunks). No call of slot 8 through the server's global 0x00B43CCC was found either; a caller that got the server from the framework would not show in that search. The game does post to single targets (slot 7; about 9 call sites).
- A second, older server (`cGZMessageQueueManager2`? class 0x452294DF, vtable 0x00AD1B80, 10 slots, 0x38 bytes) is registered and not ours.
- The service's Shutdown takes the server off the framework's tick. Called from inside a tick, the Init that follows is refused (the list is busy) and the game stops delivering queued messages: the test does not call them.

### What is ours

`src/core/framework_messageserver.cpp`, entry detours on the game's object, off by default:

| Replacement | Functions |
|---|---|
| `framework.messageserver.send` | MessageSend 0x0092EA65, the subclass's 0x004737E0, the pending changes applied 0x0092E8B5 |
| `framework.messageserver.notify` | AddNotification 0x0092E795, RemoveNotification 0x0092E560, the replay 0x0092DF35 |
| `framework.messageserver.queue` | MessagePost 0x0092D0BB, OnTick 0x0092E040, GetMessageQueueSize 0x0092D0F4, SetAlwaysClearQueueOnTick 0x0092D11F, CreateMessage 0x0092D1FE |
| `framework.messageserver.direct` | GeneralMessagePostToTarget 0x0092EA00, CancelGeneralMessagePostsToTarget 0x0092E642, the delivery pass 0x0092DFA3 |
| `framework.messageserver.interface` | QueryInterface 0x0092D091, GetRefCount 0x0092D0B3, Init 0x0092D05B, Shutdown 0x0092D076 |

`fixes.message_cancel` (off by default) is the cancel without the fault: after an erase the walk goes on from the position the erase returns. It works alone (it then replaces that one function) or with `framework.messageserver.direct`.

18 functions. The lists, the table's lookup and removal and the deque's simple cases are done in our code on the game's layouts with the game's allocator. Still the game's: the table's insert (it may grow the table: 0x0092FDD8), the deque's new block, block release and erase (0x0092E216, 0x00473290, 0x0092E162), the constructor, destructor and factory, and the three shared thunks for AddRef and Release.

Differences from the game that cannot be seen from outside: the list of targets a send collects grows without the game's extra AddRef and Release of each element on a move; the subclass's try block is not there (an exception from a target passes through, as in the base class).

Hooks go in at PostAppInit. The detours installed by then replace every later call; Init has already run by then, and ours is reached only if something calls it again.

### The message queue

The object at the server's +0x38 is a class of its own (vtable 0x00AD1AD0, 25 slots, 0x20 bytes, constructor 0x0092DD7E; its real name is not known, `cGZMessageQueue` in our address files). The older message server and one more class (0x00968094) have one too.

| Offset | Member |
|---|---|
| +0x00 | vtable |
| +0x04 | a list of messages (counted references) |
| +0x08 | its lock |
| +0x10 | an event: set when a message joins an empty queue, waited on by the two waiting pops |
| +0x18 | the reference count |

Slots (names ours): 3, 4 push at the back, at the front; 5-10 remove the first, the last or every message of a type or equal to a given message; 11 empty the queue; 12, 13 the type of the first, the last message; 14 is there a message of a type; 15, 16 the first, the last message without taking it; 17 find an equal message; 18, 19 take the first, the last; 20, 21 the same, waiting for a message when there is none; 22 the size (the nodes counted); 23 the deleting destructor; 24 take the first equal message. Two messages are equal by the queued one's own comparison (its slot 6). The message server uses four of them: 3 and 4 (MessagePost), 18 (OnTick) and 22.

`src/core/framework_messagequeue.cpp`, entry detours on the game's object, off by default: `framework.messagequeue.push` (2 functions), `.pop` (4), `.peek` (7), `.remove` (8), `.interface` (QueryInterface and Release): 23 functions. Still the game's: the constructor, the destructor, AddRef (seven bytes, shared with two other classes) and the event object.

### Testing

`OPENSC4_TEST_MESSAGES=<day>` (src/core/framework_autotest.cpp) drives the server through its interface on that day of a run, with three targets of its own: notifications added (one twice), a send to two targets and to a target of every message, changes made from inside a delivery (a removal, an addition, a nested send, a post), removals, a class the server does not make, the interfaces, posts of both priorities, direct posts (one posted from inside a delivery) and a cancel in the deque's first half. Every delivery (target, type, data) goes into a hash; the fingerprint line gets ` | msg <hash> n <deliveries> x <refusals> refs <the targets' reference counts>`.

`OPENSC4_TEST_MESSAGE_CANCEL=<day>` runs only with `fixes.message_cancel` on (in the original the case walks past the deque's end): five direct posts, the third and the last to the second target, whose posts are then cancelled. The three to the first target arrive, none to the second, and the targets' reference counts come back; checked with the fix alone and with all of the server ours.

`OPENSC4_TEST_MESSAGE_QUEUE=<day>` makes a queue of its own (the game's constructor on memory from the game's allocator, so the game's messages are not touched) and takes it through every method: pushes at both ends, every query, every removal with and without a match, the pops, the waiting pops on a queue that has a message, the emptying and the last Release, which deletes it. Every result goes into the fingerprint's `mq` part.

With the tests on, the coverage meter shows every function of the ten groups reached in the original except the subclass's MessageSend, Init and Shutdown (and the server's QueryInterface, which the meter cannot see: it is not in its list of functions).

### Parity

All five groups on against the original, 12 saves, 7 game days, two jobs, on 2026-10-11, twice, with up to nine copies of the game running on the machine (other threads' batches):

| Leg | First batch | Second batch |
|---|---|---|
| Plain | 10 same, 2 partly | 11 same, 1 partly |
| `OPENSC4_TEST_MESSAGES=2` | 8 same, 2 partly, 2 different | 11 same, 1 partly |

- The `msg` part of the fingerprint was the same in every run of both batches, original and ours, on all 12 saves.
- Every save but one came out the same in at least one batch of each leg. The one, Making Money d0365 in the plain leg, was partly both times: the original alone gave two and three outcomes there (the tick on which day 1 begins is 345 or 347, and the lots' checksum of day 2 takes one of two values with it); ours gave the same alternatives.
- The two "different" of the first batch (Making Money d0090 and d0365) were the same two-valued lots' checksum: rerun as a batch of their own, the original gave both values on each save and ours too.
- A control in the second batch, the original on both sides with the test on, gave 9 same and 3 partly (Big City d0090, Making Money d0090 and d0365): the original against itself did no better than ours against it.
- So no outcome was found that only ours gives, but under that load the verdict rests on outcome sets, not on a clean 12 of 12. A batch on a quiet machine should give that.

With the message queue's five groups on as well (ten groups against the original, same 12 saves, 7 days, 2026-10-11 late):

| Leg | Result |
|---|---|
| Plain | 11 same, 1 partly |
| `OPENSC4_TEST_MESSAGES=2` and `OPENSC4_TEST_MESSAGE_QUEUE=2` | 11 same, 1 partly |

- The `msg` and `mq` parts were the same in all 32 runs of the tests' leg, original and ours.
- The two partly (Making Money d1095 plain, Big City d0365 with the tests): the original gave two outcomes on each within the batch (on Making Money d1095 the lots' checksum of day 1 takes one of two values), ours two, one of them shared. The same kind of split as above; other threads' games were running.

## Services, tick and hooks

The framework's own lists, on the framework object (layout in the survey above).

How they work:

- **AddSystemService** (slot 3) puts a service into the tree (by priority; equal priorities in the order they came) and the table (by ID). It is refused while either is in use, for a null service and for an ID that is taken. **RemoveSystemService** (slot 4) takes it out of the idle list, the tick list, the tree and the table, and answers true whether it was known or not.
- **GetSystemService** (slot 5) looks the ID up in the table and asks that service for the interface. The table counts as in use while the service answers, so a service cannot add or remove services from inside its QueryInterface.
- **AddToTick** (slot 9) inserts before the first service of a lower tick priority, so the list runs from the highest down and equal priorities keep their order; a service whose ID is on the list already is not added again (the answer is still true). **AddToOnIdle** (slot 11) appends, and answers false for a service that is there.
- **RemoveFromTick, RemoveFromOnIdle** (slots 10, 12): during a call of the list the service is first put on a second list that keeps it referenced until the outermost call ends; the services of the pass under way are still called.
- **OnIdle** (slot 16) is what the main loop calls: it counts the frame, calls **OnTick** (slot 15) through the vtable with the frame's number, and when the frame count divides by the idle interval (4) calls the idle list's services with the idle count. Both copy their list under the lock and call outside it.
- **AddHook, RemoveHook** (slots 7, 8) keep a list; the seven functions that call the hooks first copy it (0x0087A178).

`src/core/framework_services.cpp`, entry detours on the game's object, off by default:

| Replacement | Functions |
|---|---|
| `framework.services.registry` | AddSystemService 0x0087AC3C, RemoveSystemService 0x0087A90E, GetSystemService 0x0087A0AC, the services listed by priority 0x00879ACE |
| `framework.services.tick` | AddToTick 0x00879BB9, RemoveFromTick 0x00879C6C, AddToOnIdle 0x00879D21, RemoveFromOnIdle 0x00879DB3, the idle interval 0x0087946C and 0x0087944F, OnTick 0x0087A74B, OnIdle 0x0087A812, and the game's subclass's OnTick 0x00473440 and OnIdle 0x004735F0 |
| `framework.services.hooks` | AddHook 0x0087A1F4, RemoveHook 0x008798AA, the hooks' copy 0x0087A178 |

17 functions. The lists, the table's lookup and removal and the tree's walk are done in our code on the game's layouts; the tree's insert and erase and the table's insert are still the game's helpers (0x00879E91, 0x00968D88, 0x0087A66F). The two functions that game code calls directly (0x00879ACE, 0x0087A178) are entered through thunks that keep ECX and EDX.

Still the game's in this part of the framework: the functions that start and stop the services in bands (slots 41-46) and the seven that call the hooks (they run at start-up, before our detours go in, and at shutdown), the tick switch and the small getters (shared code), and:

- **EnumSystemServices (slot 6) stalls the original** when it is called in a running city: it stops after the 32nd service and the game never comes back. A replacement could not be compared, so the function is left alone. Which service it stops at was not looked into.

Findings:

- Which frame a tick falls on is not the same from run to run: frames pass while the city loads, and their number depends on real time. The idle calls (every fourth frame) therefore land on different frames in two runs of the original. The simulation does not see this (the simulator runs its own slices); a test of the lists has to keep frame numbers out of what it compares.

### Testing

`OPENSC4_TEST_SERVICES=<day>` adds three services of its own (one twice), looks them up, lists the services by priority both ways through 0x00879ACE (how many, and where the three stand), puts the three on the tick list (one twice) and two on the idle list (one twice), removes what is not there, adds a hook twice and removes it twice, and reads and sets the idle interval. The services then record their first three ticks and two idle calls (who is called, in which order, with which number counted from the first; one takes another off the tick list from inside a tick) and take themselves out again. The fingerprint's `svc` part appears ten days after the drive, when all of that is over: run at least 13 days.

With the test on, the coverage meter shows all 17 functions reached in the original except the subclass's two.

### Parity

All thirteen framework groups on (services, message server, message queue) against the original, 12 saves, two jobs, on 2026-10-12, with a control (the original on both sides) run the same way right after; three to five other copies of the game were running:

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain, 7 days | 10 same, 2 partly | 11 same, 1 partly |
| `OPENSC4_TEST_SERVICES=2`, 14 days | 8 same, 4 partly | 10 same, 2 partly |

- The `svc` part was the same in all 50 runs of the test's leg, original and ours.
- Every difference is in parts that the original also gives two values for under load: the flammability walk's sums on the first day and the lots' checksum.
- **One save needed a closer look.** On Rush Hour d1095 the first day's flammability sum is 1303 in every run of the original (23 runs over five batches) and 1273 in about one run in six with our groups on (2 of 4 in the batch, 2 of 12 alone; none in 11 runs with one group at a time). It is a matter of timing and not of what our code does: with the same 17 functions hooked by stubs that only jump on to the original code, 1 run in 13 gave 1273 too. So something on the first day after a load races with the main thread, and a hook on these much-called functions (GetSystemService above all) is enough to tip it now and then. Ours come out a little worse than the control in both legs for that reason. Which two things race was not looked into; the load-time worker thread ([experiments/determinism.md](experiments/determinism.md)) is the first suspect.

## Directors: class registration and lookup

The COM object itself (GetClassObject 0x0087BE75, the libraries, the DLL scan) is ours since the plugin DLL work ([plugin-dlls.md](plugin-dlls.md), `resources.plugin_dlls.*`). What it falls back to for every class that no DLL registered is the exe's root director, and with it the director base class, which this part covers.

How the base class works (layout in the survey above):

- **AddCls** (0x0090E133; 0x0090E160 for factories that take the interface ID) puts {class ID, factory, type} into the director's table; a second registration of an ID is dropped. The exe calls them 464 times, all from director constructors while the app starts.
- **AddDirector** (0x0090E0D4) gives the child this director's COM object and path (InitializeCOM), adds the child's own children to this director first and then the child, so the root's list is flat. The children are not counted references.
- **GetClassObject** (slot 6) asks the children in order, the first that makes the object wins, then looks in its own table. A type 1 factory's object is asked for the interface; when it does not have it, it is given a reference and released, which ends it. A type 2 factory is handed the interface ID.
- **EnumClassObjects** (slot 5) calls a function for every class of the children and then of its own table, with version 0.
- **CanUnloadNow** (slot 7) says yes while the director is referenced, and without a reference only when every child says yes.

`src/core/framework_directors.cpp`, entry detours on the game's objects, off by default:

| Replacement | Functions | Goes in |
|---|---|---|
| `framework.directors.lookup` | GetClassObject 0x0090DFAC, EnumClassObjects 0x0090E02B, QueryInterface 0x0090DE02, RemoveRef 0x0090DE43, CanUnloadNow 0x0090DEA2, GetLibraryPath 0x0090DECD | PostAppInit |
| `framework.directors.registration` | AddCls 0x0090E133 and 0x0090E160, AddDirector 0x0090E0D4, InitializeCOM 0x0090DE57 | when core.dll is injected (Attach), so the exe's own registrations at start-up run through ours |

10 functions. The table's insert (0x0090E081, it may grow the table) is still the game's helper; so are the constructor and destructor, and the seven slots that are small functions shared with unrelated classes (AddRef, OnStart, OnUnload, RefCount, FrameWork, GZCOM, the heap size). Plugin DLLs carry their own copy of this class, so only the exe's directors (26 classes) run this code.

### Testing

`OPENSC4_TEST_CLASSES=<day>` makes two directors with the game's base constructor, registers three classes of the test's own on them (one twice, one with the second kind of factory), makes the second a child of the first and adds the first to the exe's root director, which takes the child as well. Then it looks the classes up through the COM object and through the directors (found, wrong interface, unknown class), enumerates them, asks for the director's interfaces, counts references down with RemoveRef, asks CanUnloadNow with and without a reference, and reads the library path. Everything goes into the fingerprint's `cls` part, with how many objects the factories made and how many ended (all of them). The two directors stay in the root's list for the rest of the run.

With the test on, the coverage meter shows all 10 functions reached in the original.

### Parity

All fifteen framework groups on (directors, services, message server, message queue) against the original, 12 saves, 7 days, two jobs, on 2026-10-12, on main after the change that holds the cycle clock's estimate in fixed-ticks runs ([experiments/determinism.md](experiments/determinism.md)); each leg with a control run the same way:

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 11 same, 1 partly (Big City d0000) | 11 same, 1 partly (Big City d0365) |
| `OPENSC4_TEST_CLASSES=2` | 12 same | 12 same |

The `cls` part was the same in all 26 runs of the test's leg. The game starts with every class registration of the exe going through ours.

**Rush Hour d1095 again.** With the cycle clock held, the first day's flammability sum on that save is two-valued in the original itself: 1273 in 11 of 14 runs and 1303 in 3. With hooks on the framework's service functions the odds turn round, whatever the hooks run: ours 12 of 20 runs at 1303, stubs that only jump on to the original code 6 of 12, the three hook-list functions alone 5 of 8. So the earlier reading holds (timing, not what our code does), and the value is no longer one that only the hooked runs show. What it is that races on the first day after a load is still not known; a batch that compares outcome sets is not disturbed by it.

## Starting and stopping the services, calling the hooks

Thirteen functions of the framework object that its start-up (0x0087AF13) and shutdown (0x0087AB07) call.

- **The services start in three bands of priority.** Slot 41 (0x00879F15) first adds the app itself as a system service, then walks the services from the highest priority down and starts those above -2,000,000; slot 42 (0x00879FBC) starts the band from -2,999,999 to -2,000,000, which is the app; slot 43 (0x0087A046) the rest. Starting is Init and, when Init says yes, SetServiceRunning(true). Only slot 42 can fail the start: when a service that answers the app's interface (0x66) fails its Init.
- **They stop the other way round**, lowest priority first: slot 44 (0x0087A4B6) the last band, slot 45 (0x0087A527) the app's, slot 46 (0x0087A59F) the first. Stopping is Shutdown and, on yes, SetServiceRunning(false); a function answers false when any Shutdown said no.
- **The hooks** are called by seven functions that differ in the method they call: PreFrameWorkInit 0x0087A286, PreAppInit 0x0087A2D6, PostAppInit 0x0087A326, PreAppShutdown 0x0087A376, PostAppShutdown 0x0087A3C6, PostSystemServiceShutdown 0x0087A416 and AbortiveQuit 0x0087A466. Each takes a copy of the hooks first and does nothing while the hooks' list is in use.

`src/core/framework_run.cpp`: `framework.run.services` (the six) and `framework.run.hooks` (the seven), off by default. Both go in when core.dll is injected (Attach), because the start functions and three of the hook callers run once, before any later phase. With them on, the game starts its services and calls its hooks (ours among them: core.dll's own phases hang on these calls) through our code. Still the game's: the start-up and shutdown sequences themselves and the main loop.

### Testing

Nothing can call these functions again in a running game (the services would be started twice), so the test watches them work. `diagnostics.trace.service_start` (on in both runs of a comparison) records every call of the system service base's SetServiceRunning (0x0091DD91, the slot of nearly every service): the service's ID and the flag go into a hash. The fingerprint gets ` | start <hash> <calls>`, and each stop is logged with the hash so far, since the stops come after the last fingerprint.

On Big City the original and ours give the same 34 starts (hash 6AED0313) and the same 35 stops in the same order.

### Parity

All seventeen framework groups on against the original, the trace on both sides, 12 saves, 7 days, two jobs, 2026-10-12: 12 of 12 the same on the first pair, and the control (the original on both sides) 12 of 12. The `start` part and the logged stops were the same in all 24 runs.


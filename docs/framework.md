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
| 0x00939040-0x0095FFFF | The UI layer: the property set and its values, their readers and writers, the UI script tree, the framework window and the stock widgets ([gzui.md](gzui.md); the property set, its readers and writers and the factory are ours) |

The UI layer, from the property set at 0x00939040 to the widgets past 0x0095FFFF, has a document of its own: [gzui.md](gzui.md) (survey, split into pieces u1 to u17, part u1, the property set and its values, `framework.propset.*`, and part u2, the property sets' readers and writers, the factory and the classes it makes, `framework.propio.*`, with part u2b, the Track objects' TKDT text form, and part u3, the XML writer, the UI script property handler, Create, the window's copy, the shuffle bag and the framework's own sound kinds' slots, and part u4, the UI file parser, its variable table and its director, `framework.xmlparse.*`).

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
- **CancelGeneralMessagePostsToTarget (slot 8) has a fault.** It walks the deque and erases with an iterator it has already stepped forward. That holds only when the erased post lies in the deque's first half (the erase then moves the posts in front of it). In the second half the posts behind move down: the walk skips one, and when the erased post was the last it walks past the end. Ours does the same steps in the same order, so it has the same fault, but `fixes.message_cancel` (below) is always on.
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

`fixes.message_cancel` (always on, a fix of the original's bug; README, "Original bugs fixed") is the cancel without the fault: after an erase the walk goes on from the position the erase returns. It works alone (it then replaces that one function) or with `framework.messageserver.direct`.

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

## The sequence and the main loop

What is left of the framework's own run: `GZFrameWork::Run` (0x0087B0B9, called once from WinMain's helper with the command line) and what it calls. Names ours.

- **Init** (0x0087AF13) is the start-up of the survey's table, state by state: the command line copied, the COM object's RealInit and its registration as a service, the app's three early callbacks, the app's PreFrameWorkInit, the hooks' PreFrameWorkInit, the first services, the hooks' PreAppInit, the app's own start, the tick switched on, the last services, the app's PostFrameWorkInit, the hooks' PostAppInit. It fails, with exit code -1, when the app's PreFrameWorkInit says no (the COM object is then shut down again) or when the app's band of services does not start.
- **The run phase** (0x0087957A) sets state 7 and asks the app's GZRun; SimCity 4's says no, and the framework's own main loop runs.
- **The main loop** (slot 48, 0x008795BD): with the tick off it waits for a Windows message; it hands every message there is to its window, and ends on WM_QUIT with that message's wParam as the exit code; with the tick on it calls OnIdle once per pass. A run started with "quit by flag" (SimCity 4 does not) also ends when the quit flag is set. It never sleeps.
- **Shutdown** (0x0087AB07) always follows: the hooks' PreAppShutdown, the app's PreFrameWorkShutdown, the services stopped band by band with the hooks' PostAppShutdown between, the tick off, the hooks' PostSystemServiceShutdown, the lists, table and tree emptied (0x0087AA81, 0x00879922), the COM object's RealShutdown (0x008793FD), the three global pointers released (0x008794CC).

`src/core/framework_run.cpp`: `framework.run.sequence` (Run, Init, the run phase, Shutdown and its four helpers: 8 functions) and `framework.run.loop` (the main loop), off by default, both put in when core.dll is injected. With the two on, the program runs from the framework's first step to its last through our code. The main loop calls the four Windows functions through the exe's import slots, so what stands in for them there (the interface scaling's wrappers) still does. Still the game's: the copy of the command line (0x0087AEE1) and 0x0091479D on it, the tree's erase.

With these, all of the framework object's own logic is ours except the constructor and destructor, AbortiveQuit and Quit, the streams and the small getters and setters.

### Testing

The same trace as above (`diagnostics.trace.service_start` on both sides): a run that starts, loads a city, plays and quits through our sequence and loop gives the same 34 starts and 35 stops as the original, and saves the city.

### Parity

All nineteen framework groups on against the original, the trace on both sides, 12 saves, 7 days, two jobs, 2026-10-12: 12 of 12 the same on the first pair, and the control (the original on both sides) 12 of 12. The `start` part and the logged stops were the same in all 24 runs.

## The string class

`cRZBaseString` (vtable 0x00A80810, 29 slots; 0x14 bytes) is what the game's `cIGZString` objects are: the paths, names and texts handed across interfaces. The object is a vtable, a string body at +4 (STLport's basic_string: first character, one past the last, end of the block) and a reference count at +0x10. Four more vtables (0x00A812F8, 0x00A85478, 0x00A86258, 0x00ADA4F0) are subclasses that share its code.

The class is a thin layer over the body, and that layer is what is ours:

- **Changing the text.** FromChar (a null pointer gives the empty string), Copy and operator= (the other string's Data and length, through this string's own slot 3), Resize (filled with zeros), Append, Insert, Replace (`length` characters at the position give way to the text) and Erase each pick one body operation and its arguments; the forms that take a string ask it for its text and length through its interface, so a string of any class works.
- **Comparing.** IsEqual is false at different lengths; with case it is strncmp, without it both sides are copied and lowered and the copies compared with memcmp (and their lengths once more: lowering text that is not ASCII can change it). CompareTo compares the common part the same two ways and, when that is equal, answers the difference of the lengths. Find and RFind are the body's searches, without case in lowered copies.
- **The interface.** QueryInterface answers 1 and 0x089B7DC8; Release deletes the string with its last reference.

`src/core/framework_string.cpp`, entry detours, off by default:

| Replacement | Functions |
|---|---|
| `framework.string.modify` | FromChar 0x0090F50F and 0x0090F546, operator= 0x0090E230, Copy 0x0090E20A, Resize 0x0090F042, Append 0x0090F06C and 0x0090F053, Insert 0x0090F0B0 and 0x0090F093, Replace 0x0090FC30 and 0x0090FC13, Erase 0x0090E4D8 |
| `framework.string.compare` | IsEqual 0x0090EC57 and 0x0090ED5D, CompareTo 0x0090EE61 and 0x0090EF5B, Find 0x0090F16C and 0x0090F0DB, RFind 0x0090F28C and 0x0090F1FB |
| `framework.string.interface` | QueryInterface 0x0090E1D1, Release 0x0090E1F3 |

22 functions. Game code calls many of them directly (Find 23 times, IsEqual 24, Append 14), and code built with whole-program optimisation may keep ECX and EDX across such a call, so every detour is entered through a thunk that keeps both.

Still the game's: the body's operations (STLport: assign 0x0044D3A0, resize 0x00442E90, append 0x004D8650, insert 0x0090E8B6, replace 0x005C2EB0, erase 0x004425C0, find 0x00442850, reverse find 0x005C2620), the lowering (0x0090EAC5: ASCII through the C runtime, anything else through the language's wide characters), the constructors and destructor, the C runtime's strlen, strncmp and memcmp (their exact results are CompareTo's answers), and the three getters (ToChar and Data are one function shared with other classes; Strlen is seven bytes with 99 direct callers).

### Testing

`OPENSC4_TEST_STRINGS=<day>` makes game strings with the game's constructor and calls the methods through their vtable slots: every pair of eight texts (empty, mixed case, accented letters, one longer than the lowering's own buffer) compared and searched with and without case, by string and by characters, from several positions; then one string changed step by step through every changing method, with a copy from a string of another class; then the interfaces and the count of one made on the heap, down to its end. Every result and every text go into the fingerprint's `str` part.

With the test on, the coverage meter shows all 22 functions reached in the original.

### Parity

All 22 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_STRINGS=2` | 12 same | 12 same |

All on the first pair. The `str` part was the same in all 24 runs of the test's leg.

## The standard message

`cGZMessage` (vtable 0x00ACEC00, 38 slots; 0x2C bytes, from the message pool) is the message object the message servers and the message queue pass around. It holds a reference count at +4, a byte of flags at +8, a type at +0xC, four data values from +0x10, and three counted references: a string at +0x20, an unknown at +0x24 and an extra object at +0x28. The flags say what the message has: bit 0 a type, bits 1 to 4 the data values, bit 5 the extra object, bit 6 the string, bit 7 the unknown. The message's subclasses share these functions through their own vtables.

- **The type and the data values.** SetType stores the type; a type of 0 clears the flag, so such a message has no type. A data value's setter stores the value and sets its flag whatever the value: once set, a value cannot be taken back.
- **The three objects.** A setter sets the flag by whether the object is null, then, when the object is not the one already held, adds a reference to the new one, stores it and releases the old one. GetString and GetExtra first ask the message itself whether it has the object (through its own slots, so a subclass can answer) and then ask the object for the interface.
- **The interface.** QueryInterface answers 1, 0xA52294B4 and 0x65297976. Release deletes the message with its last reference, through the deleting destructor's slot. Create makes a new standard message, asks it for the interface and gives it this message's type and nothing else of it; without the interface the new message is deleted again. The two comparisons (same type, lower type) read the other message's type through its slot.

`src/core/framework_message.cpp`, entry detours, off by default:

| Replacement | Functions |
|---|---|
| `framework.message.data` | SetType 0x009132DB, the data values' setters 0x00913322, 0x00913330, 0x0091333E, 0x0091334C and "has" answers 0x009132F3, 0x009132FE, 0x0091330A, 0x00913316 |
| `framework.message.objects` | SetString 0x00913452, SetIGZUnknown 0x0091346B, SetExtra 0x009134A1, their "has" answers 0x0091335A, 0x00913366, 0x0091336E, GetString 0x00913438, GetExtra 0x00913484 |
| `framework.message.interface` | QueryInterface 0x00913292, Release 0x009132C1, Create 0x009134FF, the comparisons 0x0091337A and 0x00913394, the deleting destructor 0x009134BA |

23 functions. Some have direct callers in game code, so every detour is entered through a thunk that keeps ECX and EDX.

Still the game's: the getters of the type, the data values and the unknown, which are small functions shared with unrelated classes.

### Testing

`OPENSC4_TEST_MESSAGE_OBJECT=<day>` makes messages with the game's constructor and calls the methods through their vtable slots: the type set, cleared and set again; each data value set, one of them to 0; a game string, and two counted test objects as the unknown and the extra object, set, set again, exchanged and cleared, with the objects' reference counts read after each step; GetString and GetExtra with an interface the object has and one it does not, and on a message without the object; the interfaces; Create, with and without an interface the new message has; the two comparisons; and the reference count down to the message's end. Every result goes into the fingerprint's `mobj` part.

With the test on, the coverage meter shows all 23 functions reached in the original.

### Parity

All 25 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_MESSAGE_OBJECT=2` | 12 same | 12 same |

All on the first pair. The `mobj` part was the same in all 48 runs of the test's leg and its control.

### The constructor, the destructor and the pool

A message's memory comes from a pool of its own (0x00B627E0), through two functions that 280 places in the game call directly before the constructor. The pool has nine block allocators, for blocks of 8, 16, 32 and so on up to 2,048 bytes. A request gets four more bytes in front for a header and goes to the first allocator whose block holds it; the header is the allocator's number in the top byte and the size with the header below it. What no allocator holds (more than 2,044 bytes) comes from the C runtime, with the top bit set in the header. The free reads the header's top byte to give the block back where it came from, and does nothing for a null pointer. Both hold the pool's lock (0x00B62870) while they work.

The constructor makes an empty message without a reference. The destructor sets the vtable back to the standard message's (a subclass's destructor ends in it) and releases the extra object, the unknown and the string, in that order.

`src/core/framework_messagepool.cpp`, entry detours, off by default:

| Replacement | Functions |
|---|---|
| `framework.message.lifetime` | the constructor 0x009134D6, the destructor 0x009133AB |
| `framework.message.pool` | allocate 0x009133DA, free 0x00913409 |

4 functions. The game's constructor returns the message in EAX, leaves ECX at 0 and keeps every other register, and its 282 callers may count on any of that, so ours is entered through a thunk of its own that leaves the registers the same way.

Still the game's: the block allocators (the pool's allocate 0x005B9FC0 and free 0x00505190 are a class other pools share, so their logic is written out in ours instead of hooked; the allocators' own slots give and take the blocks) and the C runtime's malloc and free.

`OPENSC4_TEST_MESSAGE_POOL=<day>` takes blocks of every size class from the pool, at each class's upper edge and one byte past it, and two sizes only the C runtime holds; reads their headers, writes and reads back their bytes and frees them, with a null pointer too. Then it builds a message with the constructor on memory filled with a pattern and reads every byte, and ends a message that has a foreign vtable and holds a string and two counted objects, reading the vtable and the reference counts after. The results go into the fingerprint's `mpool` part. All four functions run in every city without the test.

All 27 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_MESSAGE_POOL=2` | 12 same | 12 same |

All on the first pair. The `mpool` part was the same in all 48 runs of the test's leg and its control.

## The critical section wrapper

The lock object the framework, the message servers, the pools and many game classes hold (vtable 0x00AD1EF4, 8 slots; 67 places construct one) is 8 bytes: the vtable and a pointer to a block of 0x1C bytes from the C runtime, which is a Windows CRITICAL_SECTION followed by the wrapper's own count of locks held.

- **Lock** enters the section and then raises the count; **Unlock** lowers the count and then leaves. Both return the count. The count is only ever changed while the section is held.
- **TryLock** calls TryEnterCriticalSection through a pointer looked up when the program starts and returns 0 when that fails. When it succeeds, the game goes on to enter the section a second time before raising the count, so Windows counts two entries for one lock, and the matching Unlock does not give the section up: the thread keeps it for good. The test measures this in the original (2 entries after one TryLock on a free lock). Ours does the same. The coverage meter shows the game never calls TryLock in a city run, so nothing depends on it today.
- **The getters.** GetLockCount reads the count; GetHandle gives the CRITICAL_SECTION's address.
- **Beginning and end.** The constructor takes the block, clears it and initialises the section. The destructor deletes the section and frees the block. Release deletes the object outright (there is no reference count), through the deleting destructor's slot.
- **The lookup.** A static object's constructor (0x00930AC0, run once) loads Kernel32.dll and looks up TryEnterCriticalSection, on everything but Windows 95.

`src/core/framework_lock.cpp`, entry detours, off by default:

| Replacement | Functions |
|---|---|
| `framework.lock.use` | Lock 0x00930B35, Unlock 0x00930B6D, TryLock 0x00930B48, GetLockCount 0x00930B2E, GetHandle 0x00930B82 |
| `framework.lock.lifetime` | the constructor 0x00930B91, the destructor 0x00930B02, Release 0x00930B20, the deleting destructor 0x00930BC3, the lookup 0x00930AC0 |

10 functions. Locks are made from the program's first moments, and the lookup runs once among them, so the lifetime group goes in when core.dll is injected (phase Attach); the log shows the lookup running in our code. Windows is called through the exe's own import slots, as the game's code does.

Still the game's: the block's clearing helper (0x00930AA8, whose one caller is the constructor, so it no longer runs when ours is on), the lock scope guard (0x0048C9F0), the C runtime and Windows.

### Testing

`OPENSC4_TEST_LOCKS=<day>` makes a lock with the game's constructor and calls its slots: three locks and three unlocks, with the wrapper's count and Windows' own count of entries read after each; the handle; TryLock on a free lock and on a held one, then the unlocks (the entries TryLock leaves over are given back by hand); the destructor; one on the heap ended through Release; and the lookup run again. Every result goes into the fingerprint's `lock` part. TryLock's failing path needs a second thread and is not driven.

Without the test, a city run reaches the constructor, the destructor, Lock and Unlock; with it, the coverage meter shows all 10 functions reached in the original.

### Parity

All 29 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_LOCKS=2` | 12 same | 12 same |

All on the first pair. The `lock` part was the same in all 48 runs of the test's leg and its control.

## The string's formatter

`cRZBaseString::Sprintf` (0x0090F574, 1,649 bytes, 463 callers) is a printf of the game's own: it reads the format itself and leaves only the text of floating point numbers to the C runtime. A conversion is `%`, flags (`-`, `+`, space, `0`, `#`, in any order and number), a width (digits or `*`), a precision (`.` and digits or `*`), a size (`h`, `l`, `ll` or `I64`) and a letter.

- **Integers** (`d i u o x X`): a negative `d` or `i` is turned positive and remembered; the digits are those of the 16, 32 or 64 bit value.
- **Floating point** (`e f g E G`): a negative value is turned positive and remembered; the C runtime's `_snprintf` writes the rest, given only the `#` flag and the precision.
- **Strings** (`s`): the precision counts characters, not bytes, reading the text as UTF-8; the length of a character is taken from its first two bytes alone.
- **`c`** is one byte; **`p`** is eight hexadecimal digits; **`n`** stores the length the string had before the call, not what has been written so far.
- **Around the text**, in this order: the sign now if the `0` flag is set; padding to the width (zeros with the `0` flag, else spaces) unless left-aligned; the sign otherwise; the `#` prefix (`0x`, `0X`, `0`); zeros up to the precision (integers only); the text; padding when left-aligned. The widths count characters, as for strings.

It differs from C's printf in ways that ours keeps:

- The `#` prefix comes after zero padding (`%#08x` of 0x1f gives `00000x1f`) and is written for a value of 0 too.
- A point without a number (`%.f`) leaves the precision unset instead of 0.
- A negative width from `*` is no width, not left alignment.
- `C`, `S` and every other letter are not conversions: nothing is taken from the arguments, the width still pads an empty text, and the letter is then read as plain text.
- `%hs` and `%ls` are plain `%s`.

The text is built in two buffers of the game's own: a scratch buffer for one conversion (64 bytes on the stack) and the output (1,024 bytes on the stack); each moves to the game's heap and doubles when it fills. Ours grows them at the same moments with the same sizes, so the heap sees the same requests in the same order. One oddity kept: the first floating point conversion of a call always takes a 128-byte heap block, because the scratch buffer's used length is 0 when `_snprintf` is first asked.

`src/core/framework_stringformat.cpp`, an entry detour, off by default:

| Replacement | Functions |
|---|---|
| `framework.string.format` | Sprintf 0x0090F574 |

One function hooked. The buffer's helpers (grow 0x0090E25C, resize 0x0090E2C8, append 0x0090E2EB, 0x0090E312 and 0x0090E366), the digit test (0x0090E247) and the two UTF-8 helpers as the formatter uses them (0x009106A9, 0x009107D8) are written out inside ours and not hooked; the game's keep serving their other callers.

Still the game's: the C runtime's `_snprintf` (0x009F2108) for floating point text, the string body's assign and the heap.

### Testing

The game's own format strings (356 of the 463 call sites push one that can be read from the code) use 39 shapes of conversion, from `%d` (226 times), `%s` and `%08x` to `%3.2f`, `%I64d`, `%02.2d` and `%-18.18s`.

`OPENSC4_TEST_STRING_FORMAT=<day>` calls the formatter 130,423 times: each of those 39 shapes; then every combination of 14 sets of flags, 5 widths (with `*`), 6 precisions (with `.` alone and `.*`) and 5 sizes with each integer conversion, and the same without sizes for the floating point conversions, `s`, `c` and `p`, each over 8 values chosen for the signs, the limits of 16, 32 and 64 bits, UTF-8 texts (one cut short in the middle of a character) and star widths that are 0 or negative; letters that are not conversions; results that outgrow each buffer (to the byte around 1,024); `%n`, `%%`, and formats that end in the middle of a conversion. The length and the bytes of every result go into the fingerprint's `fmt` part. The test takes about 50 ms.

A city run reaches the formatter without the test.

### Parity

All 30 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_STRING_FORMAT=2` | 12 same | 12 same |

All on the first pair. The `fmt` part was the same in all 48 runs of the test's leg and its control.

## The string's helpers

Next to the interface's methods the string class has helpers that game code calls directly, in the same stretch of code (0x0090E242 to 0x0090FC12, 43 functions that were still the game's). Read one by one they are three kinds:

- **20 real helpers**, which are ours now (below).
- **2 too small to detour safely**, left: Strlen (0x0090E4B6, 7 bytes, 99 callers: a detour needs 5 of them and every caller would pay for it) and a 5-byte jump to the interface's slot 10 (0x0090E242).
- **21 that are not the class's own code**, left: 15 are STLport's string body generated for this use (searches 0x0090E3F6, 0x0090E4BD, 0x0090E4F1, 0x0090E529, 0x0090E60F, 0x0090E63C, 0x0090E657, 0x0090E7C5, insert 0x0090E8B6, and the wide string body's 0x0090E2A7, 0x0090E3BF, 0x0090E6BC, 0x0090E732, 0x0090E7E6, 0x0090E818), and 6 are the formatter's buffer and digit helpers, which our formatter does not call. The range has no constructors or destructor: the string's are inlines elsewhere in the game's code.

What the helpers do:

- **BeginsWith, EndsWith** compare the first or last `length` characters with a text, with case; false when the string is shorter. **AppendText** appends a C text; **AssignLimited** takes at most so many characters of one.
- **Mid, Left, Right** build a new string in memory the caller gives. Mid past the end gives an empty string; Left and Right asked for more than the string has give all of it.
- **LTrim, RTrim, Trim** remove spaces and tabs, nothing else.
- **CutThroughChar** erases the text through the first occurrence of a character; when the character is not there it erases everything and answers false.
- **SplitOff** builds the text before a separator (a text) and removes it and the separator from the string. Without the separator in the text the result is empty and the string keeps everything; a null or empty separator gives a copy.
- **SplitAtChar** moves the text before a character to another string. Without the character the other string takes everything and the answer is false.
- **SplitToken** gives the next token between runs of a character: leading separators are dropped, the separators after the token go with it, the last token takes the rest, and an empty string has none.
- **MakeLower, MakeUpper** change bytes below 0x7F in place through the C runtime's test and table. At the first byte from 0x7F up the whole text goes to wide characters (256 on the stack, a heap block for longer texts), through the C runtime's wide table, and back.
- Four small ones: a body takes a string's text (0x0090F31B), a string takes a range of characters through a temporary (0x0090FBE5), and the two conversions between a string and a wide string (to a string: wide characters above 0x7F become question marks; to wide: each byte is sign-extended).

`src/core/framework_stringhelpers.cpp`, entry detours, off by default:

| Replacement | Functions |
|---|---|
| `framework.string.helpers` | BeginsWith 0x0090E443, EndsWith 0x0090E478, AppendText 0x0090EA42, AssignLimited 0x0090F430, Mid 0x0090EA57, Left 0x0090F45F, Right 0x0090F4B3, the assigning helpers 0x0090F31B and 0x0090FBE5, the wide conversions 0x0090E901 and 0x0090E953 |
| `framework.string.split` | LTrim 0x0090E869, RTrim 0x0090E887, Trim 0x0090E8A6, CutThroughChar 0x0090E5DC, SplitOff 0x0090E98C, SplitAtChar 0x0090F335, SplitToken 0x0090F39D |
| `framework.string.case` | MakeLower 0x0090EAC5, MakeUpper 0x0090EB8E |

20 functions, 667 direct callers between them (SplitOff 158, MakeLower 154, AppendText 54, LTrim 50, Trim 47). The methods are entered through thunks that keep ECX and EDX. Temporaries are made and freed in the game's order, so the heap sees the same requests.

Still the game's: the string body's operations (find, erase, assign, append, swap, the constructors), the C runtime's character tests and case tables (isupper, tolower, islower, toupper, towlower, towupper), and the two conversions between the string's text and wide characters that the case changes use (0x009113EF, 0x00911358).

### Testing

`OPENSC4_TEST_STRING_HELPERS=<day>` calls every helper at its address on game strings, over 14 texts: empty, padded with spaces and tabs, only padding, with separators and runs of separators, mixed case with punctuation, accented letters in UTF-8 and in single bytes, the byte 0x7F, and one of about 1,200 bytes with accents (past the case changes' stack buffer). Mid, Left and Right run over positions and counts around both ends; the splits run repeatedly on the same string until nothing is left; SplitOff with 10 separators (null, empty, one and two characters, one that is not there). The length and bytes of every result go into the fingerprint's `shlp` part.

With the test on, the coverage meter shows all 20 functions reached in the original; a city run without it reaches 15.

### Parity

All 33 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_STRING_HELPERS=2` | 12 same | 12 same |

All on the first pair. The `shlp` part was the same in all 48 runs of the test's leg and its control.

## The system service base

`cRZBaseSystemService` is what the framework's services derive from (40 constructors call its own). Its part of an object is 0x1C bytes: the vtable (0x00ACF1CC, 13 slots), an unknown part at +4 with the reference count, the service's ID at +0xC, its priority at +0x10, its tick priority at +0x14 and a byte at +0x18 that says whether it is running.

- **The constructors.** One takes an ID and a priority and uses the priority as the tick priority too; the other (2 callers) takes all three. Both leave the service not running, with no reference.
- **The destructor** sets the class's vtables back, then the unknown base's: a subclass's destructor ends in it. The deleting destructor adds the free.
- **QueryInterface** answers the service's interface (0x287FB697) itself and hands anything else to the unknown part.
- **SetServiceRunning** stores the byte. Nearly every service uses the base's, which is why the start-order trace watches it.

`src/core/framework_servicebase.cpp`, entry detours, off by default, both put in when core.dll is injected (services are built and started while the app starts):

| Replacement | Functions |
|---|---|
| `framework.service.lifetime` | the constructors 0x0091DE0F and 0x0091DDCF, the destructor 0x0091DD53, the deleting destructor 0x0091DDB3 |
| `framework.service.interface` | QueryInterface 0x0091DD67, SetServiceRunning 0x0091DD91 |

6 functions. The destructor changes no register but ECX in the game, and most of its 42 callers are other destructors that jump to it, so ours is entered through a thunk that keeps EAX as well as ECX and EDX.

Left to the game:

- **Too small to detour safely:** Release (0x0091DD89) and the unknown part's three thunks (0x0091DD9B, 0x0091DDA3, 0x0091DDAB). Each is 8 bytes, a pointer adjustment and a jump, and ends where the next function begins.
- **Shared with unrelated classes:** the getters and setter of the ID, the priorities and the running byte (0x009CEDE1, 0x00894F6D, 0x0099BE66, 0x00932B83, 0x009D4714), AddRef (0x009297A0) and the empty Init, Shutdown, OnTick and OnIdle (0x00949ADE, 0x0091559A).
- The unknown base's own QueryInterface and Release.

### The start-order trace shares the detour

`diagnostics.trace.service_start` used to put its own detour on SetServiceRunning, and two detours cannot sit on one address. Now there is one detour (`framework_servicebase.h`) that either setting installs: each call is reported to the trace when it is on, and is then carried out by our translation when `framework.service.interface` is on, else by the game's function. What the trace records is unchanged: a run with the reworked trace gives the same `start` part, the same logged stops and the same final hash as a run from before, with the original and with ours.

### Testing

`OPENSC4_TEST_SERVICE_BASE=<day>` builds plain service base objects with both constructors on memory filled with a pattern and reads every field and both vtables; asks QueryInterface for the service's interface, the unknown's and one it does not have, through the main vtable and through the unknown part; sets the service running and not, with a value above 1 too; reads the getters; gives one reference back; and ends the objects with the destructor and the deleting destructor, with and without the free. The results go into the fingerprint's `sbase` part. The test's four calls of SetServiceRunning are seen by the trace like any others.

With the test on, the coverage meter shows five of the six reached in the original (three of them in a city run without the test); SetServiceRunning is not in the meter's function list, and the trace counts its calls (34 while a city starts).

### Parity

All 35 framework groups on against the original (the service start trace on both sides), 12 saves, 7 days, two jobs, 2026-10-12, each leg with a control (the original on both sides):

| Leg | Ours against the original | Original against itself |
|---|---|---|
| Plain | 12 same | 12 same |
| `OPENSC4_TEST_SERVICE_BASE=2`, first pass | 11 same, 1 partly | 12 same |
| `OPENSC4_TEST_SERVICE_BASE=2`, second pass | 12 same | |

The `sbase` part was the same in every run, and the `start` part in every run of every leg.

**The one run that differed is not explained.** In the first pass, on Rush Hour d0090, the first of four runs with ours had another value in one MySim's hashed fields (the `ms` part: `E8E91AB4` where every other run has `9A730D93`), from the first day on, which is before the test runs; the rest of that run's fingerprint was the same on all eight days. What is known:

- That value is in none of the earlier runs of this save that are still on disk: 363 of the original, 354 with our code.
- It did not come back: 16 single runs with all 35 groups, then 12 copies of the save run two at a time as in a batch, three legs with the service base groups (36 runs with ours, 36 of the original) and three legs without them (36 and 36), then the second pass above. Every run has the usual value.
- The saved city of that run does not show it: its MySim record differs from the original's in the same byte positions as a normal run's does (the positions that differ in every pair of runs). So the difference is in state the `ms` part reads from memory and the save does not keep, which on this class is the two stored stopwatches and the pacing fields.
- No MySim code calls the service base: none of its 132 call sites is in the MySim classes.

So it was seen once in 58 runs of this save with the service base groups on and never in about 830 others, it could not be reproduced, and there is no path from the new code to that field. It may be a rare timing difference at city load that these hooks make more likely (they are on the start-up path), or chance; one occurrence cannot tell. If it shows up again in a later batch, the first thing to do is log those MySim bytes field by field instead of as one hash.

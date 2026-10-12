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

18 functions. The lists, the table's lookup and removal and the deque's simple cases are done in our code on the game's layouts with the game's allocator. Still the game's: the message queue object (reached through its vtable), the table's insert (it may grow the table: 0x0092FDD8), the deque's new block, block release and erase (0x0092E216, 0x00473290, 0x0092E162), the constructor, destructor and factory, and the three shared thunks for AddRef and Release.

Differences from the game that cannot be seen from outside: the list of targets a send collects grows without the game's extra AddRef and Release of each element on a move; the subclass's try block is not there (an exception from a target passes through, as in the base class).

Hooks go in at PostAppInit. The detours installed by then replace every later call; Init has already run by then, and ours is reached only if something calls it again.

### Testing

`OPENSC4_TEST_MESSAGES=<day>` (src/core/framework_autotest.cpp) drives the server through its interface on that day of a run, with three targets of its own: notifications added (one twice), a send to two targets and to a target of every message, changes made from inside a delivery (a removal, an addition, a nested send, a post), removals, a class the server does not make, the interfaces, posts of both priorities, direct posts (one posted from inside a delivery) and a cancel in the deque's first half. Every delivery (target, type, data) goes into a hash; the fingerprint line gets ` | msg <hash> n <deliveries> x <refusals> refs <the targets' reference counts>`.

`OPENSC4_TEST_MESSAGE_CANCEL=<day>` runs only with `fixes.message_cancel` on (in the original the case walks past the deque's end): five direct posts, the third and the last to the second target, whose posts are then cancelled. The three to the first target arrive, none to the second, and the targets' reference counts come back; checked with the fix alone and with all of the server ours.

With the test on, the coverage meter shows every function of the five groups reached in the original except the subclass's MessageSend, Init and Shutdown.

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


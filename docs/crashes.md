# Crashes in the original game

The game's own crashes, what causes them, and what OpenSC4 does about each. Crashes still to look at are listed at the end.

## 0x0096DA20: reopening a closed record

**Seen:** about once in a few hundred parity runs, on A runs (no simulator code of ours) and B runs alike, nearly always while a city loads ([parity.md](parity.md)). Access violation at 0x0096DA20, `inc dword [eax+4]` with EAX = 0, on the main thread.

**Cause.** A packed-file segment (cGZDBSegmentPackedFile, 0x086D4A89) keeps its open records in a multimap from key to record at interface + 0xD4. Its open (0x009701FD, under the segment's lock from OpenRecord 0x009757A0) looks the key up first; if a record is already open and is in memory (type 2, +0x40), the new record shares the old one's buffer {data*, count} at +0x44: the constructor 0x0096D9D1 stores it and increments its count. The crash is that buffer being NULL. A record's buffer only becomes NULL in its Close (0x0096DB72, vtable 0x00AD82E8 + 0xF8), and Close is only called from the segment's CloseRecord and AbortRecord, which then release the map's reference and erase the node. So the node the open found should not exist: most likely its record was destroyed some other way (an extra Release; record counts are plain 16-bit increments with no lock) and the pool slot kept its type, NULL buffer and size. Seen in the one surviving report: an FSH texture (type 0x7AB50E44, group 0x891B0E1A, instance 0x15), size 0x14, the old record in the pool slot next to the new one.

**sc4fix** meant to guard the increment (its "puzzle-piece crash, part 1", 0x0096DA1D to 0x0096DA23) but installs that hook at the wrong address, so it never runs ([patch-census.md](patch-census.md), 3.2). Its guard would also have left the new record with a NULL buffer, which the record's reads dereference.

**Fix:** `resources.stale_record_fix = ours` ([src/core/stale_record_fix.cpp](../src/core/stale_record_fix.cpp), off by default). An entry detour on 0x009701FD: on a read-open, while the key's first map node points at a record that is type 2, has no buffer and is closed (flags word +0x0A zero), the node is erased (0x00927CF8) without touching the record, and the open then reads the record from the file as if it were not open. Each drop is logged (`stale_record_fix:` in opensc4.log). The parity harness turns it on for both runs (`scripts/run-parity.sh`, its base list), so the crash no longer costs a pair, and a drop shows in the run's log. Not fixed: the extra Release behind it, and a node whose freed slot has been reused by another record (nothing to tell it apart).

The sibling constructor 0x0096DA30 has the same unchecked increment at 0x0096DA95, but only when an 8-byte allocation fails.

## Puzzle piece over a transit-enabled lot

**Cause.** A solver in the network tool takes its next entry at 0x0065ED80: a pending (id, value) pair from its stack (+0x50/+0x54), or else the entry of its hash table (+0x28; buckets +0x2C to +0x30, node [0] next, [1] id, [2..8] the entry) with the smallest range hi - lo (node [3] - [2]); a negative range is its failure exit (failing id at +0x10, false). With the table empty, or every range NaN or infinite, it chooses no entry and reads the entry's flags through NULL at 0x0065EE40.

**sc4fix** patches the middle of the function (0x0065EE3E, 0x0065EE66) to jump to the failure exit then, with failing id 0; its second patch only makes that jump safe, and pops an x87 stack that is empty on that path.

**Fix:** `fixes.puzzle_piece = ours` ([src/core/puzzle_piece_fix.cpp](../src/core/puzzle_piece_fix.cpp), off by default). An entry detour checks the same condition before the original runs (no pending pair, and no node whose range is negative or below FLT_MAX, compared in double as the x87 does) and returns the failure itself: nothing changed, failing id 0, false. The one caller (0x00660B10) ignores the outputs on false. It coexists with sc4fix, whose patches sit later in the function.

## Crash on exit with plugin DLLs

**Cause.** At shutdown the game's GZCOM library list frees every plugin DLL (cGZCOMLibrary::Free, 0x0087B3B3, through a FreeLibrary wrapper called at 0x0087B3D1) while their objects or threads can still be reached, and the game then calls into unmapped code. sc4fix NOPs that call.

**Fix:** `fixes.pin_plugin_dlls = ours` ([src/core/pin_plugin_dlls.cpp](../src/core/pin_plugin_dlls.cpp), off by default). Once all plugins have loaded, every module that exports `GZDllGetGZCOMDirector` (but the exe and core.dll) is pinned with `GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN)`, so FreeLibrary leaves it mapped until the process ends. Nothing after the call depends on the unload: Free ignores its result and only clears the handle and its loaded flag. Like sc4fix's NOP, a library the game frees and loads again would keep its static state.

## Prop pox

Not fixed: sc4fix's "vaccine" does not change what the save does. It replaces cSC4COMSerializer::SaveClassObjects (0x005D3DE0, slot 7 of the vtable at 0x00AA268C) with a rewrite, but the original performs the same steps in the same order: per object the size and CRC placeholders, the object ID, Write, then seeks back to fill in the size and a CRC of up to 250,000 bytes (the game's CRC at 0x00932FCF is the same CRC-32: MSB first, polynomial 0x04C11DB7, initial 0xFFFFFFFF, no final xor), seeking back to the start on failure; the class map is copied with the objects AddRef'd and each object released as its node is erased, which the rewrite matches by using the game's own copy. So its effect on prop pox, if any, is not in the save logic.

The load side (LoadClassObjects, 0x005D38D0) is where objects are lost: a CRC mismatch skips the object and stops reading the rest of that class's record; a size that disagrees with what Read consumed is silently realigned; an exception in Read keeps the half-read object; an object ID seen twice makes Read run again on the existing object. Finding which objects write records that do not read back (the props' own Write and Read) is the next step.

## Still to look at

From the community: the multicore crash (players set one core) and running out of memory with large plugin folders (the 4GB patch). Other rare crashes the parity harness has hit: 0x00975CC8, 0x0080105D, 0x0075FEEE, 0x005A62A2 ([parity.md](parity.md)). Already fixed by the D3D11 driver: the crash above 2048 px and the start-up crash when no display mode matches ([renderer.md](renderer.md)).

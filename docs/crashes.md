# Crashes in the original game

The game's own crashes, what causes them, and what OpenSC4 does about each. Community crashes still to look at (puzzle pieces, prop pox, the exit crash, multicore, memory) are listed at the end.

## 0x0096DA20: reopening a closed record

**Seen:** about once in a few hundred parity runs, on A runs (no simulator code of ours) and B runs alike, nearly always while a city loads ([parity.md](parity.md)). Access violation at 0x0096DA20, `inc dword [eax+4]` with EAX = 0, on the main thread.

**Cause.** A packed-file segment (cGZDBSegmentPackedFile, 0x086D4A89) keeps its open records in a multimap from key to record at interface + 0xD4. Its open (0x009701FD, under the segment's lock from OpenRecord 0x009757A0) looks the key up first; if a record is already open and is in memory (type 2, +0x40), the new record shares the old one's buffer {data*, count} at +0x44: the constructor 0x0096D9D1 stores it and increments its count. The crash is that buffer being NULL. A record's buffer only becomes NULL in its Close (0x0096DB72, vtable 0x00AD82E8 + 0xF8), and Close is only called from the segment's CloseRecord and AbortRecord, which then release the map's reference and erase the node. So the node the open found should not exist: most likely its record was destroyed some other way (an extra Release; record counts are plain 16-bit increments with no lock) and the pool slot kept its type, NULL buffer and size. Seen in the one surviving report: an FSH texture (type 0x7AB50E44, group 0x891B0E1A, instance 0x15), size 0x14, the old record in the pool slot next to the new one.

**sc4fix** meant to guard the increment (its "puzzle-piece crash, part 1", 0x0096DA1D to 0x0096DA23) but installs that hook at the wrong address, so it never runs ([patch-census.md](patch-census.md), 3.2). Its guard would also have left the new record with a NULL buffer, which the record's reads dereference.

**Fix:** `resources.stale_record_fix = ours` ([src/core/stale_record_fix.cpp](../src/core/stale_record_fix.cpp), off by default). An entry detour on 0x009701FD: on a read-open, while the key's first map node points at a record that is type 2, has no buffer and is closed (flags word +0x0A zero), the node is erased (0x00927CF8) without touching the record, and the open then reads the record from the file as if it were not open. Each drop is logged (`stale_record_fix:` in opensc4.log). The parity harness turns it on for both runs (`scripts/run-parity.sh`, its base list), so the crash no longer costs a pair, and a drop shows in the run's log. Not fixed: the extra Release behind it, and a node whose freed slot has been reused by another record (nothing to tell it apart).

The sibling constructor 0x0096DA30 has the same unchecked increment at 0x0096DA95, but only when an 8-byte allocation fails.

## Still to look at

From the community and the ecosystem census ([patch-census.md](patch-census.md)): the puzzle-piece crash over transit-enabled lots (sc4fix parts 2 and 3, 0x0065EE3E), prop pox (sc4fix's serializer rewrite at 0x005D3DE0), the crash on exit with plugin DLLs (0x0087B3D1), the multicore crash (players set one core) and running out of memory with large plugin folders (the 4GB patch). Other rare crashes the parity harness has hit: 0x00975CC8, 0x0080105D, 0x0075FEEE, 0x005A62A2 ([parity.md](parity.md)). Already fixed by the D3D11 driver: the crash above 2048 px and the start-up crash when no display mode matches ([renderer.md](renderer.md)).

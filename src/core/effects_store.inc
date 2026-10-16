// The effects manager's store of compound descriptions (the 0x38-byte
// objects SC4EffectCompound::Read reads, effects_compound's notes at the
// reader), the part under the effects file reader's resize of the store
// (0x0058D4D0 -> 0x0058D070 -> 0x0058C8E0 -> 0x0058C270), written by hand
// from the game's code: the object's copy and the two lists inside it,
// their ends, a range copied and ended, the growth, the insert and the two
// resizes. Included by water_effects.cpp, which has the calls that the
// comparison notes (the St functions): each call the game makes to
// another function goes through one, a part the game has inlined is
// written out here.
//
// An object is 0x38 bytes: +0 and +4 plain values, +8 a list of 0x6C-byte
// records (each copied by 0x005888A0 and holding an allocation at +0),
// +0x14 a list of 0x18-byte records (a range copied by 0x005894C0, each
// holding an allocation at +4), +0x20 three plain values, +0x2C a string
// (copied by 0x00408420). Lists and the string are begin, end, end of
// storage.

// The list of 0x6C-byte records copied into fresh storage (0x0058A330):
// storage for exactly the source's count (none when empty), each record
// copy-constructed in order. Answers `to`.
char* CopyCompoundRecords(char* to, char* from)
{
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(At<char*>(from, 4) - At<char*>(from, 0)) / 0x6C);
    At<char*>(to, 0) = nullptr;
    At<char*>(to, 4) = nullptr;
    At<char*>(to, 8) = nullptr;
    char* const storage = count ? StAllocate(count * 0x6C) : nullptr;
    At<char*>(to, 0) = storage;
    At<char*>(to, 4) = storage;
    At<char*>(to, 8) = storage + count * 0x6C;
    char* place = storage;
    for (char* record = At<char*>(from, 0); record != At<char*>(from, 4); record += 0x6C, place += 0x6C) {
        if (place)
            StCopyRecord6C(place, record);
    }
    At<char*>(to, 4) = place;
    return to;
}

// The list of 0x18-byte records copied into fresh storage (0x0058A3B0):
// storage for exactly the source's count, the range copied by 0x005894C0.
// Answers `to`.
char* CopyCompoundRecordsB(char* to, char* from)
{
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(At<char*>(from, 4) - At<char*>(from, 0)) / 0x18);
    At<char*>(to, 0) = nullptr;
    At<char*>(to, 4) = nullptr;
    At<char*>(to, 8) = nullptr;
    char* const storage = count ? StAllocate(count * 0x18) : nullptr;
    At<char*>(to, 0) = storage;
    At<char*>(to, 4) = storage;
    At<char*>(to, 8) = storage + count * 0x18;
    At<char*>(to, 4) = StCopyRecords18(At<char*>(from, 0), At<char*>(from, 4), storage);
    return to;
}

// The list of 0x18-byte records ended (0x0058A420): each record's
// allocation at +4 freed, then the storage. The list is left as it was.
void DestroyCompoundRecordsB(char* list)
{
    for (char* record = At<char*>(list, 0); record != At<char*>(list, 4); record += 0x18) {
        if (char* const held = At<char*>(record, 4))
            StFree(held);
    }
    if (char* const storage = At<char*>(list, 0))
        StFree(storage);
}

// The list of 0x6C-byte records ended (0x0058A630): each record's
// allocation at +0 freed, then the storage.
void DestroyCompoundRecords(char* list)
{
    for (char* record = At<char*>(list, 0); record != At<char*>(list, 4); record += 0x6C) {
        if (char* const held = At<char*>(record, 0))
            StFree(held);
    }
    if (char* const storage = At<char*>(list, 0))
        StFree(storage);
}

// An object copy-constructed (0x0058A8C0): the two plain values, the two
// lists, the three values and the string. Answers `to`.
char* CopyCompound(char* to, char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
    At<uint32_t>(to, 4) = At<uint32_t>(from, 4);
    StCopyCompoundRecords(to + 8, from + 8);
    StCopyCompoundRecordsB(to + 0x14, from + 0x14);
    std::memcpy(to + 0x20, from + 0x20, 0xC);
    At<char*>(to, 0x2C) = nullptr;
    At<char*>(to, 0x30) = nullptr;
    At<char*>(to, 0x34) = nullptr;
    StCopyString(to + 0x2C, At<char*>(from, 0x2C), At<char*>(from, 0x30));
    return to;
}

// Objects copied into raw storage (0x0058B2D0): nothing made where the
// place is null, which still moves on. Answers the place after the last.
char* CopyCompoundRange(char* first, char* last, char* place)
{
    for (; first != last; first += 0x38, place += 0x38) {
        if (place)
            StCopyCompound(place, first);
    }
    return place;
}

// Objects ended (0x0058BE50), with the two lists' ends inlined: the
// string's storage, the 0x18-byte records' allocations and storage, then
// the 0x6C-byte records'.
void DestroyCompoundRange(char* first, char* last)
{
    for (; first != last; first += 0x38) {
        if (char* const text = At<char*>(first, 0x2C))
            StFree(text);
        DestroyCompoundRecordsB(first + 0x14);
        DestroyCompoundRecords(first + 8);
    }
}

// The store with no room left grown (0x0058C270): new storage for the size
// plus the larger of the size and `count`, the objects before `at` copied
// over, `count` copies of `object` after them, the objects from `at` on
// too unless `atEnd`, the old objects ended and their storage freed. Same
// shape as SC4EffectRecords::GrowInsert (effects_records.inc).
void GrowInsertCompounds(char* store, char* at, char* object, uint32_t count, bool atEnd)
{
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(store, 4) - At<char*>(store, 0)) / 0x38);
    const uint32_t room = size + (size < count ? count : size);
    char* const storage = room ? StAllocate(room * 0x38) : nullptr;
    char* place = storage;
    for (char* from = At<char*>(store, 0); from != at; from += 0x38, place += 0x38) {
        if (place)
            StCopyCompound(place, from);
    }
    for (uint32_t i = 0; i < count; ++i, place += 0x38) {
        if (place)
            StCopyCompound(place, object);
    }
    if (!atEnd)
        place = StCopyCompoundRange(at, At<char*>(store, 4), place);
    StDestroyCompoundRange(At<char*>(store, 0), At<char*>(store, 4));
    if (char* const old = At<char*>(store, 0))
        StFree(old);
    At<char*>(store, 4) = place;
    At<char*>(store, 8) = storage + room * 0x38;
    At<char*>(store, 0) = storage;
}

// `count` copies of `object` inserted at `at` (0x0058C8E0). Without room,
// the growth. With room the object is first copied (it may be one of the
// store's), then by the game's helpers for these objects: when more than
// `count` objects follow `at`, the last `count` are copied into the raw
// memory past the end (0x0058B2D0), the rest moved up by assignment from
// the back (0x0058BA40) and the copy assigned to the `count` places from
// `at` (0x0058B920); else the copies short of `count` are made past the
// end (0x0058B300), the objects from `at` copied after them and the copy
// assigned to the old places from `at` to the old end. The copy is ended
// (its string's storage, then its two lists).
void InsertCompounds(char* store, char* at, uint32_t count, char* object)
{
    if (!count)
        return;
    const uint32_t room =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(store, 8) - At<char*>(store, 4)) / 0x38);
    if (room < count) {
        StGrowInsertCompounds(store, at, object, count, false);
        return;
    }
    alignas(4) char copy[0x38];
    StCopyCompound(copy, object);
    char* const end = At<char*>(store, 4);
    const uint32_t after = static_cast<uint32_t>(static_cast<int32_t>(end - at) / 0x38);
    if (after > count) {
        char* const from = end - count * 0x38;
        StCopyCompoundRange(from, end, end);
        At<char*>(store, 4) += count * 0x38;
        StMoveCompoundsBack(at, from, end);
        StAssignCompounds(at, at + count * 0x38, copy);
    } else {
        StFillNewCompounds(end, count - after, copy);
        At<char*>(store, 4) += (count - after) * 0x38;
        StCopyCompoundRange(at, end, At<char*>(store, 4));
        At<char*>(store, 4) += after * 0x38;
        StAssignCompounds(at, end, copy);
    }
    if (char* const text = At<char*>(copy, 0x2C))
        StFree(text);
    StDestroyCompoundRecordsB(copy + 0x14);
    StDestroyCompoundRecords(copy + 8);
}

// The store resized to `count` with copies of `object` (0x0058D070): a
// shorter size cuts it (the erase's copy down, 0x0058B9A0, has nothing to
// move, then the tail is ended), a longer one inserts at the end.
void ResizeCompounds(char* store, uint32_t count, char* object)
{
    char* const begin = At<char*>(store, 0);
    char* const end = At<char*>(store, 4);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) / 0x38);
    if (count < size) {
        char* const cut = StCopyCompoundsDown(end, end, begin + count * 0x38);
        StDestroyCompoundRange(cut, At<char*>(store, 4));
        At<char*>(store, 4) = cut;
        return;
    }
    StInsertCompounds(store, end, count - size, object);
}

// The store resized to `count` with empty objects (0x0058D4D0): the object
// built on the stack, +0 cleared, +4 1, empty lists and an empty string
// (8 bytes of storage, its first byte the end), +0x20 cleared; the game
// leaves +0x24 and +0x28 as the stack held them (the reader fills both
// after the resize), so they are cleared here. Then the resize and the
// object's end.
void ResizeCompoundsEmpty(char* store, uint32_t count)
{
    alignas(4) char object[0x38] = {};
    At<uint32_t>(object, 4) = 1;
    char* const text = StAllocate(8);
    StClearText(text);
    At<char*>(object, 0x2C) = text;
    At<char*>(object, 0x30) = text;
    At<char*>(object, 0x34) = text + 8;
    StResizeCompounds(store, count, object);
    if (char* const storage = At<char*>(object, 0x2C))
        StFree(storage);
    StDestroyCompoundRecordsB(object + 0x14);
    StDestroyCompoundRecords(object + 8);
}

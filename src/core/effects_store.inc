// WORK IN PROGRESS, not included anywhere yet and not built: the effects
// manager's store of description records, the part under the description
// reader's insert (0x0058D4D0 -> 0x0058D070 -> 0x0058C8E0 -> 0x0058C270),
// written by hand from the game's code. 0x0058D4D0 is the vector's
// resize to n with a default record, 0x0058D070 resize(n, record),
// 0x0058C8E0 insert(at, count, record) and 0x0058C270 its growth.
// Translated so far (unbuilt, unchecked): the record's copy and destroy,
// the two vectors inside it, the growth, and the resize's growing path.
// Still to do: 0x0058C8E0's branches (its in-place paths call 0x0058B300,
// 0x0058B920, 0x0058BA40, none reached in Big City), the shrinking path
// of the resize (0x0058B9A0), 0x0058D4D0, the address entries, the comparison (to be included by water_effects.cpp like
// effects_records.inc, with the allocations noted through StAllocate and
// StFree) and the group's registration.
//
// A record is 0x38 bytes: +0 and +4 plain values, +8 a vector of 0x6C-byte
// elements (copied by 0x005888A0, each holds an allocation at +0), +0x14 a
// vector of 0x18-byte elements (a range copied by 0x005894C0, each holds an
// allocation at +4), +0x20 three plain values, +0x2C a byte vector (copied
// by 0x00408420). Vectors are begin, end, end of storage.

constexpr uint32_t kStCopyLarge = 0x005888A0;  // element 0x6C copy-construct, thiscall (to, from)
constexpr uint32_t kStCopySmallRange = 0x005894C0;  // cdecl (first, last, dest, tag), answers dest end
constexpr uint32_t kStCopyBytes = 0x00408420;  // thiscall (vector, first, last, tag), callee pops

char* StAllocate(uint32_t size);  // 0x0090CF54
void StFree(char* storage);  // 0x0090CF63

// The vector of 0x6C-byte elements copied into fresh storage (0x0058A330):
// storage for exactly the source's count (none when empty), each element
// copy-constructed in order.
char* CopyStoreLargeVector(char* to, char* from)
{
    char* const first = At<char*>(from, 0);
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(At<char*>(from, 4) - first) / 0x6C);
    At<char*>(to, 0) = nullptr;
    At<char*>(to, 4) = nullptr;
    At<char*>(to, 8) = nullptr;
    char* const storage = count ? StAllocate(count * 0x6C) : nullptr;
    At<char*>(to, 0) = storage;
    At<char*>(to, 4) = storage;
    At<char*>(to, 8) = storage + count * 0x6C;
    char* place = storage;
    for (char* element = At<char*>(from, 0); element != At<char*>(from, 4); element += 0x6C, place += 0x6C) {
        if (place)
            CallThis<void>(kStCopyLarge, place, element);
    }
    At<char*>(to, 4) = place;
    return to;
}

// The vector of 0x18-byte elements copied into fresh storage (0x0058A3B0):
// storage for exactly the source's count, the range copied by 0x005894C0.
char* CopyStoreSmallVector(char* to, char* from)
{
    char* const first = At<char*>(from, 0);
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(At<char*>(from, 4) - first) / 0x18);
    At<char*>(to, 0) = nullptr;
    At<char*>(to, 4) = nullptr;
    At<char*>(to, 8) = nullptr;
    char* const storage = count ? StAllocate(count * 0x18) : nullptr;
    At<char*>(to, 0) = storage;
    At<char*>(to, 4) = storage;
    At<char*>(to, 8) = storage + count * 0x18;
    // The fourth argument is the address of the caller's own argument slot
    // (an unused tag); any address will do.
    char tag = 0;
    At<char*>(to, 4) = CallCdecl<char*, char*, char*, char*, char*>(kStCopySmallRange, At<char*>(from, 0),
                                                                    At<char*>(from, 4), storage, &tag);
    return to;
}

// The vector of 0x18-byte elements ended (0x0058A420): each element's
// allocation at +4 freed, then the storage.
void DestroyStoreSmallVector(char* vector)
{
    for (char* element = At<char*>(vector, 0); element != At<char*>(vector, 4); element += 0x18) {
        if (char* const held = At<char*>(element, 4))
            StFree(held);
    }
    if (char* const storage = At<char*>(vector, 0))
        StFree(storage);
}

// The vector of 0x6C-byte elements ended (0x0058A630): each element's
// allocation at +0 freed, then the storage.
void DestroyStoreLargeVector(char* vector)
{
    for (char* element = At<char*>(vector, 0); element != At<char*>(vector, 4); element += 0x6C) {
        if (char* const held = At<char*>(element, 0))
            StFree(held);
    }
    if (char* const storage = At<char*>(vector, 0))
        StFree(storage);
}

// A record copy-constructed (0x0058A8C0, thiscall, ret 4). Answers `to`.
char* CopyStoreRecord(char* to, char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
    At<uint32_t>(to, 4) = At<uint32_t>(from, 4);
    CopyStoreLargeVector(to + 8, from + 8);
    CopyStoreSmallVector(to + 0x14, from + 0x14);
    std::memcpy(to + 0x20, from + 0x20, 0xC);
    At<char*>(to, 0x2C) = nullptr;
    At<char*>(to, 0x30) = nullptr;
    At<char*>(to, 0x34) = nullptr;
    // The game passes the address of its own argument slot as the unused tag.
    char tag = 0;
    CallThis<void>(kStCopyBytes, to + 0x2C, At<char*>(from, 0x2C), At<char*>(from, 0x30), &tag);
    return to;
}

// Records copied into raw storage (0x0058B2D0, cdecl (first, last, dest,
// tag); the callers pass a fourth, unused argument):
// a null destination is skipped but still advanced, as in the game.
// Answers the end of the copies.
char* CopyStoreRecordRange(char* first, char* last, char* place)
{
    for (; first != last; first += 0x38, place += 0x38) {
        if (place)
            CopyStoreRecord(place, first);
    }
    return place;
}

// Records ended (0x0058BE50, cdecl (first, last, tag); the tag is unused): the byte vector's
// storage, the small vector and the large vector, in that order.
void DestroyStoreRecordRange(char* first, char* last)
{
    for (; first != last; first += 0x38) {
        if (char* const bytes = At<char*>(first, 0x2C))
            StFree(bytes);
        DestroyStoreSmallVector(first + 0x14);
        DestroyStoreLargeVector(first + 8);
    }
}

// The vector with no room left grown (0x0058C270, thiscall (vector, at,
// record, tag, count, atEnd), ret 0x14): new storage for the size plus the
// larger of the size and `count`, the records before `at` copied over,
// `count` copies of `record` after them (one copy without a loop when
// `count` is 1), the records from `at` on too unless `atEnd`, the old
// records ended and their storage freed. Same shape as 0x005905C0
// (effects_records.inc).
void GrowInsertStoreRecords(char* vector, char* at, char* record, uint32_t count, bool atEnd)
{
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, 4) - At<char*>(vector, 0)) / 0x38);
    const uint32_t room = size + (size < count ? count : size);
    char* const storage = room ? StAllocate(room * 0x38) : nullptr;
    char* place = storage;
    for (char* from = At<char*>(vector, 0); from != at; from += 0x38, place += 0x38) {
        if (place)
            CopyStoreRecord(place, from);
    }
    for (uint32_t i = 0; i < count; ++i, place += 0x38) {
        if (place)
            CopyStoreRecord(place, record);
    }
    if (!atEnd)
        place = CopyStoreRecordRange(at, At<char*>(vector, 4), place);
    DestroyStoreRecordRange(At<char*>(vector, 0), At<char*>(vector, 4));
    if (char* const old = At<char*>(vector, 0))
        StFree(old);
    At<char*>(vector, 4) = place;
    At<char*>(vector, 8) = storage + room * 0x38;
    At<char*>(vector, 0) = storage;
}

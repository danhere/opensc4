// The effects manager's lists of records and texts: the vectors of decal
// descriptions, of 0x28- and 0x1C-byte records and of two kinds of text
// record that the effects file reader (0x005A4E70, 0x005A51E0) fills and
// resizes, their copies, fills, copies down, ends, growth, inserts, the
// decal descriptions' resize and the texts' erase, written by hand from the
// game's code. Included by water_effects.cpp, which has the calls the
// comparison notes (the Ls functions): each call the game makes to another
// function goes through one, a part the game has inlined is written out
// here.
//
// Five kinds of element, each list begin, end, end of storage:
// - a decal description, 0x64 bytes (SC4DecalDescription::CopyConstruct
//   0x00587890): +0 and +4 plain values, the bytes +8 and +9, +0xC, lists
//   of floats at +0x10, +0x1C, +0x28 and +0x40 (copied by 0x00464760), a
//   list of 12-byte items at +0x34 (0x0050BCB0), six plain values from
//   +0x4C;
// - a 0x28-byte record: +0 and +4, lists of floats at +8 and +0x14, +0x20
//   and +0x24;
// - a 0x1C-byte record: a list of 12-byte items at +0, a list of floats at
//   +0xC, +0x18;
// - a 0x14-byte text record: a string at +0 (copied by 0x00408420,
//   assigned by 0x00408A70), +0xC and +0x10;
// - a 0x10-byte text record: +0, a string at +4.
// A string is begin, end, end of storage, as a list.

uint32_t LsCount(char* first, char* last, uint32_t size)
{
    return static_cast<uint32_t>(static_cast<int32_t>(last - first) / static_cast<int32_t>(size));
}

// One element copy-constructed where the game inlines it (the decal
// description's copy is a call of its own, LsCopyDecal).
void CopyElement(Ls kind, char* to, char* from)
{
    switch (kind) {
    case Ls::Decals:
        LsCopyDecal(to, from);
        break;
    case Ls::Records28:
        At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
        At<uint32_t>(to, 4) = At<uint32_t>(from, 4);
        LsCopyFloats(to + 8, from + 8);
        LsCopyFloats(to + 0x14, from + 0x14);
        At<uint32_t>(to, 0x20) = At<uint32_t>(from, 0x20);
        At<uint32_t>(to, 0x24) = At<uint32_t>(from, 0x24);
        break;
    case Ls::Records1C:
        LsCopyFloat3s(to, from);
        LsCopyFloats(to + 0xC, from + 0xC);
        At<uint32_t>(to, 0x18) = At<uint32_t>(from, 0x18);
        break;
    case Ls::Texts14:
        At<char*>(to, 0) = nullptr;
        At<char*>(to, 4) = nullptr;
        At<char*>(to, 8) = nullptr;
        StCopyString(to, At<char*>(from, 0), At<char*>(from, 4));
        At<uint32_t>(to, 0xC) = At<uint32_t>(from, 0xC);
        At<uint32_t>(to, 0x10) = At<uint32_t>(from, 0x10);
        break;
    case Ls::Texts10:
        At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
        At<char*>(to, 4) = nullptr;
        At<char*>(to, 8) = nullptr;
        At<char*>(to, 0xC) = nullptr;
        StCopyString(to + 4, At<char*>(from, 4), At<char*>(from, 8));
        break;
    }
}

// A decal description copy-constructed (0x00587890). Answers `to`.
char* CopyDecalDescription(char* to, char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
    At<uint32_t>(to, 4) = At<uint32_t>(from, 4);
    At<uint8_t>(to, 8) = At<uint8_t>(from, 8);
    At<uint8_t>(to, 9) = At<uint8_t>(from, 9);
    At<uint32_t>(to, 0xC) = At<uint32_t>(from, 0xC);
    LsCopyFloats(to + 0x10, from + 0x10);
    LsCopyFloats(to + 0x1C, from + 0x1C);
    LsCopyFloats(to + 0x28, from + 0x28);
    LsCopyFloat3s(to + 0x34, from + 0x34);
    LsCopyFloats(to + 0x40, from + 0x40);
    std::memcpy(to + 0x4C, from + 0x4C, 0x18);
    return to;
}

// [first, last) copied into raw storage at `place` on (0x00588330,
// 0x005883C0, 0x00588480, 0x00588B10, 0x00588BC0): nothing made where the
// place is null, which still moves on. Answers the place after the last.
char* CopyElements(Ls kind, char* first, char* last, char* place)
{
    const uint32_t size = LsKindOf(kind).size;
    for (; first != last; first += size, place += size) {
        if (place)
            CopyElement(kind, place, first);
    }
    return place;
}

// `count` copies of the element at `value` made in raw storage at `place`
// on (0x00587960, 0x005879C0, 0x00588B70, 0x00588C10). Answers the place
// after the last.
char* FillElements(Ls kind, char* place, uint32_t count, char* value)
{
    const uint32_t size = LsKindOf(kind).size;
    for (; count; --count, place += size) {
        if (place)
            CopyElement(kind, place, value);
    }
    return place;
}

// [first, last) assigned to `to` on, the erase's copy down (0x00588560,
// 0x0058A4C0, 0x0058A540): the lists of floats by their assignment
// (0x00464430), a string by the string's (0x00408A70) unless it is its
// own source, the plain values copied. Answers the place after the last
// (`to` for an empty range).
char* CopyElementsDown(Ls kind, char* first, char* last, char* to)
{
    const uint32_t size = LsKindOf(kind).size;
    int32_t count = static_cast<int32_t>(LsCount(first, last, size));
    if (count <= 0)
        return to;
    for (; count; --count, first += size, to += size) {
        switch (kind) {
        case Ls::Records28:
            At<uint32_t>(to, 0) = At<uint32_t>(first, 0);
            At<uint32_t>(to, 4) = At<uint32_t>(first, 4);
            LsAssignFloats(to + 8, first + 8);
            LsAssignFloats(to + 0x14, first + 0x14);
            At<uint32_t>(to, 0x20) = At<uint32_t>(first, 0x20);
            At<uint32_t>(to, 0x24) = At<uint32_t>(first, 0x24);
            break;
        case Ls::Texts14:
            if (first != to)
                LsAssignText(to, At<char*>(first, 0), At<char*>(first, 4));
            At<uint32_t>(to, 0xC) = At<uint32_t>(first, 0xC);
            At<uint32_t>(to, 0x10) = At<uint32_t>(first, 0x10);
            break;
        case Ls::Texts10:
            At<uint32_t>(to, 0) = At<uint32_t>(first, 0);
            if (first != to)
                LsAssignText(to + 4, At<char*>(first, 4), At<char*>(first, 8));
            break;
        default:
            break;
        }
    }
    return to;
}

// [first, last) ended (0x00588760, 0x00587EE0, 0x005887D0): what each
// element's lists hold freed, last list first.
void DestroyElements(Ls kind, char* first, char* last)
{
    static constexpr uint32_t kDecalLists[] = {0x40, 0x34, 0x28, 0x1C, 0x10};
    static constexpr uint32_t kRecord28Lists[] = {0x14, 8};
    static constexpr uint32_t kRecord1CLists[] = {0xC, 0};
    const uint32_t* lists = kind == Ls::Decals ? kDecalLists : kind == Ls::Records28 ? kRecord28Lists : kRecord1CLists;
    const std::size_t count = kind == Ls::Decals ? 5 : 2;
    const uint32_t size = LsKindOf(kind).size;
    for (; first != last; first += size) {
        for (std::size_t i = 0; i < count; ++i) {
            if (char* const storage = At<char*>(first, lists[i]))
                StFree(storage);
        }
    }
}

// The 0x10-byte texts' list ended (0x0058A880): each string's storage
// freed, then the list's. The list is left as it was.
void DestroyTexts10(char* list)
{
    char* const end = At<char*>(list, 4);
    for (char* at = At<char*>(list, 0); at != end; at += 0x10) {
        if (char* const text = At<char*>(at, 4))
            StFree(text);
    }
    if (char* const storage = At<char*>(list, 0))
        StFree(storage);
}

// A list with no room left grown (0x00589260, 0x00588C60, 0x00589380,
// 0x0058A770, 0x0058AF00): new storage for the size plus the larger of the
// size and `count`, the elements before `at` copied over (the decal
// descriptions one by one, the others by the range copy), `count` copies
// of the one at `value` after them (one inlined, more by the fill; the
// decal descriptions one by one), the elements from `at` on too unless
// `atEnd`, then the old list ended: the decal descriptions and records by
// the range end and their storage freed, the 0x14-byte texts by 0x005DB590
// and the 0x10-byte ones by their end.
void GrowInsertElements(Ls kind, char* list, char* at, char* value, uint32_t count, bool atEnd)
{
    const uint32_t size = LsKindOf(kind).size;
    const uint32_t have = LsCount(At<char*>(list, 0), At<char*>(list, 4), size);
    const uint32_t room = have + (have < count ? count : have);
    char* const storage = room ? StAllocate(room * size) : nullptr;
    char* place = storage;
    if (kind == Ls::Decals) {
        for (char* from = At<char*>(list, 0); from != at; from += size, place += size) {
            if (place)
                LsCopyDecal(place, from);
        }
        for (uint32_t i = 0; i < count; ++i, place += size) {
            if (place)
                LsCopyDecal(place, value);
        }
    } else {
        place = LsCopyElements(kind, At<char*>(list, 0), at, storage);
        if (count == 1) {
            if (place)
                CopyElement(kind, place, value);
            place += size;
        } else {
            place = LsFillElements(kind, place, count, value);
        }
    }
    if (!atEnd)
        place = LsCopyElements(kind, at, At<char*>(list, 4), place);
    if (kind == Ls::Texts14) {
        LsDestroyTexts14(list);
    } else if (kind == Ls::Texts10) {
        LsDestroyTexts10(list);
    } else {
        LsDestroyElements(kind, At<char*>(list, 0), At<char*>(list, 4));
        if (char* const old = At<char*>(list, 0))
            StFree(old);
    }
    At<char*>(list, 0) = storage;
    At<char*>(list, 4) = place;
    At<char*>(list, 8) = storage + room * size;
}

// `count` copies of the element at `value` inserted at `at` (0x0058A020,
// 0x00589100, 0x0058A170). With room: a copy of the value made first (the
// decal description's by its copy, the 0x28-byte record's by 0x00587920,
// the 0x1C-byte record's inlined); when more than `count` elements follow
// `at`, the last `count` copied on past the end, the rest moved back
// (0x00588630, 0x00588680, 0x00588700) and `count` from `at` given the
// copy (0x00588360, 0x00588420, 0x005884D0); else the copies short of the
// following ones made at the end (0x00588390 for the decal descriptions,
// else the fill), the following ones copied on after them and given the
// copy. Then the copy ended (0x00583130, or its lists' storage freed).
// Without room the growth makes them.
void InsertElements(Ls kind, char* list, char* at, uint32_t count, char* value)
{
    if (!count)
        return;
    const LsKindInfo& k = LsKindOf(kind);
    const uint32_t size = k.size;
    if (LsCount(At<char*>(list, 4), At<char*>(list, 8), size) < count) {
        LsGrowInsertElements(kind, list, at, value, count, false);
        return;
    }
    alignas(4) char copy[0x64];
    if (kind == Ls::Decals) {
        LsCopyDecal(copy, value);
    } else if (kind == Ls::Records28) {
        LsCopyRecord28(copy, value);
    } else {
        LsCopyFloat3s(copy, value);
        LsCopyFloats(copy + 0xC, value + 0xC);
        At<uint32_t>(copy, 0x18) = At<uint32_t>(value, 0x18);
    }
    char* const end = At<char*>(list, 4);
    const uint32_t after = LsCount(at, end, size);
    if (after > count) {
        const uint32_t moved = count * size;
        LsCopyElements(kind, end - moved, end, end);
        At<char*>(list, 4) += moved;
        LsGame5(k.copyBack, at, end - moved, end);
        LsGame3(k.assignFill, at, at + moved, copy);
    } else {
        if (kind == Ls::Decals)
            LsGame4(k.fillNew, end, count - after, copy);
        else
            LsFillElements(kind, end, count - after, copy);
        At<char*>(list, 4) += (count - after) * size;
        LsCopyElements(kind, at, end, At<char*>(list, 4));
        At<char*>(list, 4) += after * size;
        LsGame3(k.assignFill, at, end, copy);
    }
    if (kind == Ls::Decals) {
        LsDestroyDecal(copy);
    } else {
        const uint32_t second = kind == Ls::Records28 ? 0x14 : 0xC, first = kind == Ls::Records28 ? 8 : 0;
        if (char* const storage = At<char*>(copy, second))
            StFree(storage);
        if (char* const storage = At<char*>(copy, first))
            StFree(storage);
    }
}

// The decal descriptions' list resized to `count` (0x0058ADC0): shorter,
// the elements from `count` on ended (the copy down 0x00588510 of the
// empty range past the end answering where they start); else the missing
// ones inserted at the end as copies of the one at `value`.
void ResizeDecalDescriptions(char* list, uint32_t count, char* value)
{
    char* const begin = At<char*>(list, 0);
    char* const end = At<char*>(list, 4);
    const uint32_t have = LsCount(begin, end, 0x64);
    if (count < have) {
        char* const kept = Ptr(LsGame5(addr::SC4DecalDescriptions_CopyDown, end, end, begin + count * 0x64));
        LsDestroyElements(Ls::Decals, kept, At<char*>(list, 4));
        At<char*>(list, 4) = kept;
        return;
    }
    LsInsertElements(Ls::Decals, list, end, count - have, value);
}

// [first, last) erased from a texts' list (0x0058AEA0, 0x0058AFF0): the
// texts after them copied down, the strings of the ones left past the new
// end freed, the end moved back. Answers `first`.
char* EraseTexts(Ls kind, char* list, char* first, char* last)
{
    const uint32_t size = LsKindOf(kind).size, text = kind == Ls::Texts14 ? 0 : 4;
    char* const kept = LsCopyElementsDown(kind, last, At<char*>(list, 4), first);
    char* const end = At<char*>(list, 4);
    for (char* at = kept; at != end; at += size) {
        if (char* const storage = At<char*>(at, text))
            StFree(storage);
    }
    At<char*>(list, 4) = kept;
    return first;
}

// The manager's two texts' lists emptied (0x0058BEF0, the end of
// 0x0058F690): the 0x10-byte texts at +0xFC, then the 0x14-byte ones at
// +0xF0; the byte +0xD0 cleared.
void ClearManagerTexts(char* manager)
{
    LsEraseTexts(Ls::Texts10, manager + 0xFC, At<char*>(manager, 0xFC), At<char*>(manager, 0x100));
    LsEraseTexts(Ls::Texts14, manager + 0xF0, At<char*>(manager, 0xF0), At<char*>(manager, 0xF4));
    At<uint8_t>(manager, 0xD0) = 0;
}

// 0x20-byte name tables ended (0x0058B330): each one's list of objects at
// +0x14 (their counted base at +4) let go (slot 0x14), the list's storage
// freed, the hash table at +0 cleared (0x0041A920) and its buckets'
// storage (+4) freed.
void DestroyNameTables(char* first, char* last)
{
    for (; first != last; first += 0x20) {
        char* const end = At<char*>(first, 0x18);
        for (char* at = At<char*>(first, 0x14); at != end; at += 4) {
            if (char* const object = At<char*>(at, 0))
                PtLetGo(Held::CountedAt4, object);
        }
        if (char* const storage = At<char*>(first, 0x14))
            StFree(storage);
        LsClearTable(first);
        if (char* const buckets = At<char*>(first, 4))
            StFree(buckets);
    }
}

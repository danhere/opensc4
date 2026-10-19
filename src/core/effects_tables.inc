// The effects tables: the object the effects file reader fills (the
// reader's +4, built at 0x005A4CD0) and the manager holds, their
// constructor, end and take; the parts they are built of (the list of name
// tables and its fill, a name hash's copy and insert, the zoom distances);
// the copies of a compound description's two kinds of record and its
// copy down; and three of the manager's own small functions (AddRef,
// QueryInterface and the setting of its two render states), written by
// hand from the game's code. Included by water_effects.cpp, which has the
// calls the comparison notes (the Tb functions): each call the game makes
// to another function goes through one, a part the game has inlined is
// written out here.
//
// The tables, 0x118 bytes: the vtable 0x00A9F040 at +0; six hash tables
// (the SC4Uint32HashSet layout: +4 buckets begin, end, end of storage,
// +0x10 count) at +4, +0x24, +0x44, +0x64, +0x84 and +0xA4, each followed
// by a list: compound descriptions (0x38 bytes) at +0x18, held
// descriptions at +0x38, counted objects at +0x58, decal descriptions at
// +0x78, 0x28-byte records at +0x98, 0x1C-byte records at +0xB8; the name
// tables at +0xC4; the zoom distances at +0xD4 (a list of floats, then
// +0xC to +0x18), read when the byte +0xD0 is set; the 0x14- and 0x10-byte
// texts' lists at +0xF0 and +0xFC; a byte at +0x108; a string at +0x10C.
//
// A name table, 0x20 bytes: a hash of names at +0 (a node is next, a
// string at +4, a value at +0x10) and a list of objects held by their
// counted base at +4 at +0x14.
//
// A compound description's record, 0x6C bytes: a string at +0, +0xC,
// +0x10, the bytes +0x14 and +0x15, plain values from +0x18 to +0x4B,
// +0x4C to +0x51 and +0x54 to +0x6B. Its second kind of record, 0x18
// bytes: +0, a string at +4, +0x10 and +0x14.

constexpr uint32_t kTablesVtable = 0x00A9F040;
constexpr uint32_t kTablesHashes[] = {4, 0x24, 0x44, 0x64, 0x84, 0xA4};

uint32_t TbCount4(char* first, char* last)
{
    return static_cast<uint32_t>(static_cast<int32_t>(last - first) >> 2);
}

// A compound description's 0x6C-byte record copy-constructed (0x005888A0;
// the game first sets the floats from +0x18 to +0x47 to the value at
// 0x00B22C58, then copies over all of them). Answers `to`.
char* CopyCompoundRecord(char* to, char* from)
{
    At<char*>(to, 0) = nullptr;
    At<char*>(to, 4) = nullptr;
    At<char*>(to, 8) = nullptr;
    StCopyString(to, At<char*>(from, 0), At<char*>(from, 4));
    At<uint32_t>(to, 0xC) = At<uint32_t>(from, 0xC);
    At<uint32_t>(to, 0x10) = At<uint32_t>(from, 0x10);
    At<uint8_t>(to, 0x14) = 0;
    std::memcpy(to + 0x18, from + 0x18, 0x34);
    At<uint8_t>(to, 0x15) = At<uint8_t>(from, 0x15);
    std::memcpy(to + 0x4C, from + 0x4C, 6);
    std::memcpy(to + 0x54, from + 0x54, 0x18);
    return to;
}

// A 0x18-byte record made at `place` as a copy of the one at `value`.
void MakeCompoundRecordB(char* place, char* value)
{
    At<uint32_t>(place, 0) = At<uint32_t>(value, 0);
    At<char*>(place, 4) = nullptr;
    At<char*>(place, 8) = nullptr;
    At<char*>(place, 0xC) = nullptr;
    StCopyString(place + 4, At<char*>(value, 4), At<char*>(value, 8));
    At<uint32_t>(place, 0x10) = At<uint32_t>(value, 0x10);
    At<uint32_t>(place, 0x14) = At<uint32_t>(value, 0x14);
}

// One 0x18-byte record made in raw storage (0x00588D90): nothing where
// the place is null. Answers the record's +0x10 (zero for a null place),
// what the game leaves in EAX.
uint32_t ConstructCompoundRecordB(char* place, char* value)
{
    if (!place)
        return 0;
    MakeCompoundRecordB(place, value);
    return At<uint32_t>(value, 0x10);
}

// [first, last) of 0x6C-byte records copied into raw storage at `place`
// on (0x00589490), each by its copy; nothing made where the place is null,
// which still moves on. Answers the place after the last.
char* CopyCompoundRecords(char* first, char* last, char* place)
{
    for (; first != last; first += 0x6C, place += 0x6C) {
        if (place)
            TbCopyCompoundRecord(place, first);
    }
    return place;
}

// The same of 0x18-byte records (0x005894C0), each made inline.
char* CopyCompoundRecordsB(char* first, char* last, char* place)
{
    for (; first != last; first += 0x18, place += 0x18) {
        if (place)
            MakeCompoundRecordB(place, first);
    }
    return place;
}

// A name hash made a copy of another (0x00589BD0): its buckets emptied,
// reserved (0x0045D4F0) and filled with nulls (0x004FBA20) to the other's
// count; each of the other's chains copied in order, the first node's
// string by the string copy, the others' inlined (storage of the length
// plus one, the bytes and an end byte; the game's length check before it
// cannot fail); the count copied. The nodes the table held are not freed.
void CopyNameHash(char* table, char* from)
{
    char* const buckets = table + 4;
    At<char*>(buckets, 4) = At<char*>(buckets, 0);
    const uint32_t count = TbCount4(At<char*>(from, 4), At<char*>(from, 8));
    TbReserve(buckets, count);
    const uint32_t null = 0;
    TbFillInsert(buckets, At<char*>(buckets, 4), count, &null);
    for (uint32_t i = 0; i < TbCount4(At<char*>(from, 4), At<char*>(from, 8)); ++i) {
        char* node = At<char**>(from, 4)[i];
        if (!node)
            continue;
        char* const first = StAllocate(0x14);
        At<char*>(first, 0) = nullptr;
        At<char*>(first, 4) = nullptr;
        At<char*>(first, 8) = nullptr;
        At<char*>(first, 0xC) = nullptr;
        StCopyString(first + 4, At<char*>(node, 4), At<char*>(node, 8));
        At<uint32_t>(first, 0x10) = At<uint32_t>(node, 0x10);
        At<char**>(table, 4)[i] = first;
        char* last = first;
        for (node = At<char*>(node, 0); node; node = At<char*>(node, 0)) {
            char* const next = StAllocate(0x14);
            At<char*>(next, 0) = nullptr;
            const int32_t length = static_cast<int32_t>(At<char*>(node, 8) - At<char*>(node, 4));
            char* const text = StAllocate(static_cast<uint32_t>(length) + 1);
            At<char*>(next, 4) = text;
            At<char*>(next, 8) = text;
            At<char*>(next, 0xC) = text + length + 1;
            if (length > 0)
                std::memcpy(text, At<char*>(node, 4), static_cast<std::size_t>(length));
            At<char*>(next, 8) = text + (length > 0 ? length : 0);
            *At<char*>(next, 8) = 0;
            At<uint32_t>(next, 0x10) = At<uint32_t>(node, 0x10);
            At<char*>(last, 0) = next;
            last = next;
        }
    }
    At<uint32_t>(table, 0x10) = At<uint32_t>(from, 0x10);
}

// A name inserted into a name hash, after the first node of an equal name
// in its bucket (the bucket by 0x0051C9B0, which only reads) or else at
// the bucket's head (0x00589E80). `key` is a string and a value at +0xC.
// The node and the table go to `out`, which is answered.
char* InsertNameEqual(char* table, char* out, char* key)
{
    const uint32_t bucket = CallThis<uint32_t>(addr::SC4EffectNameHash_Bucket, table, key);
    char* const head = At<char**>(table, 4)[bucket];
    const std::size_t length = static_cast<std::size_t>(At<char*>(key, 4) - At<char*>(key, 0));
    char* equal = nullptr;
    for (char* at = head; at && !equal; at = At<char*>(at, 0)) {
        if (static_cast<std::size_t>(At<char*>(at, 8) - At<char*>(at, 4)) == length &&
            std::memcmp(At<char*>(at, 4), At<char*>(key, 0), length) == 0)
            equal = at;
    }
    char* const node = StAllocate(0x14);
    At<char*>(node, 0) = nullptr;
    At<char*>(node, 4) = nullptr;
    At<char*>(node, 8) = nullptr;
    At<char*>(node, 0xC) = nullptr;
    StCopyString(node + 4, At<char*>(key, 0), At<char*>(key, 4));
    At<uint32_t>(node, 0x10) = At<uint32_t>(key, 0xC);
    if (equal) {
        At<char*>(node, 0) = At<char*>(equal, 0);
        At<char*>(equal, 0) = node;
    } else {
        At<char*>(node, 0) = head;
        At<char**>(table, 4)[bucket] = node;
    }
    ++At<uint32_t>(table, 0x10);
    At<char*>(out, 0) = node;
    At<char*>(out, 4) = table;
    return out;
}

// [first, last) of compound descriptions assigned to `to` on, the erase's
// copy down (0x0058B9A0): the two lists of records by their assignments
// (0x0058B050, 0x0058B180), the string by the string's (0x00408A70)
// unless it is its own, the plain values copied. Answers the place after
// the last (`to` for an empty range).
char* CopyCompoundsDown(char* first, char* last, char* to)
{
    int32_t count = static_cast<int32_t>(last - first) / 0x38;
    if (count <= 0)
        return to;
    for (; count; --count, first += 0x38, to += 0x38) {
        At<uint32_t>(to, 0) = At<uint32_t>(first, 0);
        At<uint32_t>(to, 4) = At<uint32_t>(first, 4);
        TbAssignCompoundRecords(to + 8, first + 8);
        TbAssignCompoundRecordsB(to + 0x14, first + 0x14);
        At<uint32_t>(to, 0x20) = At<uint32_t>(first, 0x20);
        At<uint32_t>(to, 0x24) = At<uint32_t>(first, 0x24);
        At<uint32_t>(to, 0x28) = At<uint32_t>(first, 0x28);
        if (first + 0x2C != to + 0x2C)
            LsAssignText(to + 0x2C, At<char*>(first, 0x2C), At<char*>(first, 0x30));
    }
    return to;
}

// `count` copies of the name table at `value` made in raw storage at
// `place` on (0x0058C0F0): the hash by its copy, the list of objects into
// storage of its own, each object held (slot 0x10); nothing made where the
// place is null. Answers the place after the last.
char* FillNameTables(char* place, uint32_t count, char* value)
{
    for (; count; --count, place += 0x20) {
        if (!place)
            continue;
        At<uint32_t>(place, 4) = 0;
        At<uint32_t>(place, 8) = 0;
        At<uint32_t>(place, 0xC) = 0;
        At<uint32_t>(place, 0x10) = 0;
        TbCopyNameHash(place, value);
        At<char*>(place, 0x14) = nullptr;
        At<char*>(place, 0x18) = nullptr;
        At<char*>(place, 0x1C) = nullptr;
        const uint32_t objects = TbCount4(At<char*>(value, 0x14), At<char*>(value, 0x18));
        char* const storage = objects ? StAllocate(objects * 4) : nullptr;
        At<char*>(place, 0x14) = storage;
        At<char*>(place, 0x18) = storage;
        At<char*>(place, 0x1C) = storage + objects * 4;
        char* to = storage;
        char* const end = At<char*>(value, 0x18);
        for (char* at = At<char*>(value, 0x14); at != end; at += 4, to += 4) {
            if (!to)
                continue;
            char* const object = At<char*>(at, 0);
            At<char*>(to, 0) = object;
            if (object)
                PtHold(Held::CountedAt4, object);
        }
        At<char*>(place, 0x18) = to;
    }
    return place;
}

// The list of name tables made with `count` empty ones (0x0058C1C0):
// storage for them, an empty table made on the stack (its hash by
// 0x00829BC0 with 0x64 buckets) and copied into each by the fill, then
// ended (its list by 0x00587810, its hash cleared and its buckets freed).
// Answers the list.
char* ConstructNameTables(char* list, uint32_t count)
{
    At<char*>(list, 0) = nullptr;
    At<char*>(list, 4) = nullptr;
    At<char*>(list, 8) = nullptr;
    char* const storage = count ? StAllocate(count * 0x20) : nullptr;
    At<char*>(list, 8) = storage + count * 0x20;
    At<char*>(list, 0) = storage;
    At<char*>(list, 4) = storage;
    alignas(4) char table[0x20];
    At<uint32_t>(table, 4) = 0;
    At<uint32_t>(table, 8) = 0;
    At<uint32_t>(table, 0xC) = 0;
    At<uint32_t>(table, 0x10) = 0;
    TbConstructHash(table, 0x64);
    At<char*>(table, 0x14) = nullptr;
    At<char*>(table, 0x18) = nullptr;
    At<char*>(table, 0x1C) = nullptr;
    At<char*>(list, 4) = TbFillNameTables(At<char*>(list, 0), count, table);
    TbCall0(addr::SC4CountedAt4Ptrs_Destroy, table + 0x14);
    LsClearTable(table);
    if (char* const buckets = At<char*>(table, 4))
        StFree(buckets);
    return list;
}

// The zoom distances made (0x00587F30): an empty list of floats, +0xC and
// +0x10 zero, 4 and 7 at +0x14 and +0x18, then 2500, 1250, 625, 312 and
// 156 appended (in place when there is room, else by 0x00676CC0). Answers
// the object.
char* ConstructZoomDistances(char* object)
{
    At<char*>(object, 4) = nullptr;
    At<char*>(object, 0) = nullptr;
    At<char*>(object, 8) = nullptr;
    At<uint32_t>(object, 0xC) = 0;
    At<uint32_t>(object, 0x10) = 0;
    At<float>(object, 0x14) = 4.0f;
    At<float>(object, 0x18) = 7.0f;
    for (const float distance : {2500.0f, 1250.0f, 625.0f, 312.0f, 156.0f}) {
        char* const end = At<char*>(object, 4);
        if (end != At<char*>(object, 8)) {
            if (end)
                At<float>(end, 0) = distance;
            At<char*>(object, 4) += 4;
        } else {
            TbAppendFloat(object, end, &distance);
        }
    }
    return object;
}

// The tables made (0x0058C580): each hash emptied and made with 0x64
// buckets (0x00829BC0) and the list after it emptied, the 13 name tables,
// the zoom distances, the texts' lists and the byte +0x108 cleared, the
// string made empty in storage of 8 bytes. Answers the tables.
char* ConstructTables(char* tables)
{
    At<uint32_t>(tables, 0) = kTablesVtable;
    for (const uint32_t hash : kTablesHashes) {
        char* const table = tables + hash;
        At<uint32_t>(table, 4) = 0;
        At<uint32_t>(table, 8) = 0;
        At<uint32_t>(table, 0xC) = 0;
        At<uint32_t>(table, 0x10) = 0;
        TbConstructHash(table, 0x64);
        At<uint32_t>(table, 0x14) = 0;
        At<uint32_t>(table, 0x18) = 0;
        At<uint32_t>(table, 0x1C) = 0;
    }
    TbConstructNameTables(tables + 0xC4, 0xD);
    TbConstructZoomDistances(tables + 0xD4);
    for (uint32_t at = 0xF0; at < 0x108; at += 4)
        At<uint32_t>(tables, at) = 0;
    At<uint8_t>(tables, 0x108) = 0;
    At<char*>(tables, 0x10C) = nullptr;
    At<char*>(tables, 0x110) = nullptr;
    At<char*>(tables, 0x114) = nullptr;
    char* const text = StAllocate(8);
    At<char*>(tables, 0x10C) = text;
    At<char*>(tables, 0x110) = text;
    At<char*>(tables, 0x114) = text + 8;
    *text = 0;
    return tables;
}

// A hash of the tables ended: cleared (0x0041A920) and its buckets freed.
void EndTablesHash(char* table)
{
    LsClearTable(table);
    if (char* const buckets = At<char*>(table, 4))
        StFree(buckets);
}

// A list of the tables ended by the game's range end, its storage freed.
void EndTablesList(uint32_t destroy, char* list)
{
    TbEndRange(destroy, At<char*>(list, 0), At<char*>(list, 4));
    if (char* const storage = At<char*>(list, 0))
        StFree(storage);
}

// The tables ended (0x0058C6E0), last part first: the string's storage
// freed, the 0x10- and 0x14-byte texts' lists ended (0x0058A880,
// 0x005DB590), the zoom distances' storage freed, the name tables and the
// lists of records and decal descriptions ended with their hashes, the
// counted objects' list ended (0x005877D0), each held description let go
// (0x005B1C80) and their storage freed, the compound descriptions ended,
// the first hash.
void DestroyTables(char* tables)
{
    At<uint32_t>(tables, 0) = kTablesVtable;
    if (char* const text = At<char*>(tables, 0x10C))
        StFree(text);
    TbCall0(addr::SC4EffectTexts10_Destroy, tables + 0xFC);
    LsDestroyTexts14(tables + 0xF0);
    if (char* const distances = At<char*>(tables, 0xD4))
        StFree(distances);
    EndTablesList(addr::SC4EffectNameTables_DestroyRange, tables + 0xC4);
    EndTablesList(addr::SC4EffectRecords1C_DestroyRange, tables + 0xB8);
    EndTablesHash(tables + 0xA4);
    EndTablesList(addr::SC4EffectRecords28_DestroyRange, tables + 0x98);
    EndTablesHash(tables + 0x84);
    EndTablesList(addr::SC4DecalDescriptions_DestroyRange, tables + 0x78);
    EndTablesHash(tables + 0x64);
    TbCall0(addr::SC4CountedPtrs_Destroy, tables + 0x58);
    EndTablesHash(tables + 0x44);
    char* const end = At<char*>(tables, 0x3C);
    for (char* at = At<char*>(tables, 0x38); at != end; at += 4) {
        if (char* const description = At<char*>(at, 0))
            TbCall0(addr::SC4EffectDescription_Release, description);
    }
    if (char* const storage = At<char*>(tables, 0x38))
        StFree(storage);
    EndTablesHash(tables + 0x24);
    EndTablesList(addr::SC4EffectCompounds_DestroyRange, tables + 0x18);
    EndTablesHash(tables + 4);
}

// The dwords two tables swap when the other's compound descriptions are
// empty: each hash but its first dword and the list after it, the name
// tables, the 0x10- and the 0x14-byte texts.
constexpr uint32_t kTablesSwapped[] = {
    0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x48,
    0x4C, 0x50, 0x54, 0x58, 0x5C, 0x60, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x88, 0x8C,
    0x90, 0x94, 0x98, 0x9C, 0xA0, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC,
    0xFC, 0x100, 0x104, 0xF0, 0xF4, 0xF8,
};

// The tables take what another holds (0x0058D0E0): when the other's
// compound descriptions are empty the two swap their parts, and when the
// zoom distances were read (+0xD0) the other gets a copy of them (the list
// by its assignment 0x00464430); else the game's merge, by the byte +0x108
// 0x00587230 or 0x0058CA50, which the game jumps to.
uint32_t TakeTables(char* tables, char* other)
{
    if (At<char*>(other, 0x18) != At<char*>(other, 0x1C))
        return TbCall1(At<uint8_t>(tables, 0x108) ? addr::SC4EffectTables_MergeKept : addr::SC4EffectTables_Merge,
                       tables, other);
    for (const uint32_t at : kTablesSwapped)
        std::swap(At<uint32_t>(tables, at), At<uint32_t>(other, at));
    if (At<uint8_t>(tables, 0xD0)) {
        LsAssignFloats(other + 0xD4, tables + 0xD4);
        std::memcpy(other + 0xE0, tables + 0xE0, 0x10);
    }
    return 0;
}

// The manager's AddRef (0x0058D840): its count at +0x1B0 up by one.
// Answers the new count.
uint32_t AddRefManager(char* manager)
{
    return ++At<uint32_t>(manager, 0x1B0);
}

// The manager's two render states set (0x0058D870) by 0x007D2B50, which
// writes only the two dwords it is given.
void SetManagerRenderStates(char* manager)
{
    constexpr uint32_t kMinusOne = 0xFFFFFFFF, kMinusOneFloat = 0xBF800000;
    using Init = void(__thiscall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const auto init = reinterpret_cast<Init>(addr::SC4RenderState_Init);
    init(manager + 0xD5C, 1, 0, 0, 0, 1, 0, 0, kMinusOne, 4, 1, kMinusOne, kMinusOneFloat, kMinusOne, kMinusOne);
    init(manager + 0xD64, 1, 0, 0, 0, 1, 0, 0, kMinusOne, 8, 5, kMinusOne, kMinusOneFloat, kMinusOne, kMinusOne);
}

// The manager's QueryInterface (0x0058D9E0): for its own IDs (1 and
// 0xC97CF5CD itself, 0xC97CF5CE its interface at +0x94, 0x452294AA the one
// at +8) the interface given and the manager held (slot 4); 0x0A5EAA1A
// asked of the object at +0xD98 (its slot 0) when there is one. Answers
// whether an interface was given.
uint32_t QueryManager(char* manager, uint32_t id, char** out)
{
    switch (id) {
    case 1:
    case 0xC97CF5CD:
        *out = manager;
        break;
    case 0xC97CF5CE:
        *out = manager ? manager + 0x94 : nullptr;
        break;
    case 0x452294AA:
        *out = manager ? manager + 8 : nullptr;
        break;
    case 0x0A5EAA1A:
        if (char* const other = At<char*>(manager, 0xD98))
            return TbQueryOther(other, id, out);
        return 0;
    default:
        return 0;
    }
    TbAddRef(manager);
    return 1;
}

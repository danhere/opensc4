// The effects manager's hash tables and small objects: a dword hash's look-up
// and end, a hash of key pairs' insert and growth, a hash of names' growth,
// the clear of a hash of lists and of a list of events, the 0x40-byte
// counted object's constructor and two destructors, and the character
// transform, written by hand from the game's code. (The manager's lists of
// held effects, the fourth kind of effects_pointers.inc, are of the same
// group.) Included by water_effects.cpp, which has the calls the comparison
// notes (the Hs functions): each call the game makes to another function
// goes through one, a part the game has inlined is written out here.
//
// A hash table (the SC4Uint32HashSet layout): +4 buckets begin, end, end
// of storage, +0x10 count; a node's next at +0. A dword hash's node has
// its key at +4; a pair hash's (0x10 bytes) two keys at +4 and +8 and a
// value at +0xC; a name hash's a string at +4 (begin, end); a hash of
// lists' (0x14 bytes) a list at +8. Its bucket counts are the primes at
// 0x00A829B0 (28 of them), past the last 0xFFFFFFFB.

constexpr uint32_t kHashPrimes = 0x00A829B0, kHashPrimesEnd = 0x00A82A20;

uint32_t HsBuckets(char* table)
{
    return static_cast<uint32_t>(static_cast<int32_t>(At<char*>(table, 8) - At<char*>(table, 4)) >> 2);
}

// The first prime not below `count` (the lower bound 0x0079FA90, inlined
// by the growths' callee), or 0xFFFFFFFB past the last.
uint32_t HsPrimeAtLeast(uint32_t count)
{
    const uint32_t* first = reinterpret_cast<const uint32_t*>(kHashPrimes);
    int32_t left = static_cast<int32_t>((kHashPrimesEnd - kHashPrimes) >> 2);
    while (left > 0) {
        const int32_t half = left >> 1;
        if (first[half] < count) {
            first += half + 1;
            left -= half + 1;
        } else {
            left = half;
        }
    }
    return Word(first) == kHashPrimesEnd ? 0xFFFFFFFBu : *first;
}

// The node of a dword key, or null (0x0058EFE0; the key by pointer).
char* FindUint32(char* table, const uint32_t* key)
{
    const uint32_t value = *key;
    for (char* node = At<char**>(table, 4)[value % HsBuckets(table)]; node; node = At<char*>(node, 0)) {
        if (At<uint32_t>(node, 4) == value)
            return node;
    }
    return nullptr;
}

// A dword hash's nodes freed and its buckets emptied, the count cleared
// (SC4Uint32HashSet::Clear 0x008EBA60, inlined here), then its bucket
// storage freed (0x0058F3A0). The buckets' list itself is left.
void DestroyUint32Hash(char* table)
{
    for (uint32_t i = 0; i < HsBuckets(table); ++i) {
        for (char* node = At<char**>(table, 4)[i]; node;) {
            char* const next = At<char*>(node, 0);
            StFree(node);
            node = next;
        }
        At<char**>(table, 4)[i] = nullptr;
    }
    At<uint32_t>(table, 0x10) = 0;
    if (char* const storage = At<char*>(table, 4))
        StFree(storage);
}

// A hash of lists emptied (0x00584660): each node's list storage and the
// node freed, the buckets emptied, the count cleared.
void ClearListHash(char* table)
{
    for (uint32_t i = 0; i < HsBuckets(table); ++i) {
        for (char* node = At<char**>(table, 4)[i]; node;) {
            char* const next = At<char*>(node, 0);
            if (char* const list = At<char*>(node, 8))
                StFree(list);
            StFree(node);
            node = next;
        }
        At<char**>(table, 4)[i] = nullptr;
    }
    At<uint32_t>(table, 0x10) = 0;
}

// The buckets of a hash grown to the first prime not below `count` when
// that is more than it has (0x0058F3C0 for a pair hash, 0x00587010 for a
// name hash): new storage of nulls (the vector constructor 0x0051C950,
// inlined), each old chain's nodes moved in order to the head of their new
// bucket, the new list put in and the old storage freed.
template <typename Hash>
void GrowHash(char* table, uint32_t count, Hash&& hash)
{
    const uint32_t old = HsBuckets(table);
    if (count <= old)
        return;
    const uint32_t buckets = HsPrimeAtLeast(count);
    if (buckets <= old)
        return;
    char** const storage = reinterpret_cast<char**>(StAllocate(buckets * 4));
    for (uint32_t i = 0; i < buckets; ++i)
        storage[i] = nullptr;
    for (uint32_t i = 0; i < old; ++i) {
        for (char* node; (node = At<char**>(table, 4)[i]) != nullptr;) {
            const uint32_t bucket = hash(node) % buckets;
            At<char**>(table, 4)[i] = At<char*>(node, 0);
            At<char*>(node, 0) = storage[bucket];
            storage[bucket] = node;
        }
    }
    char* const before = At<char*>(table, 4);
    At<char**>(table, 4) = storage;
    At<char**>(table, 8) = storage + buckets;
    At<char**>(table, 0xC) = storage + buckets;
    if (before)
        StFree(before);
}

uint32_t HashPair(char* node)
{
    return At<uint32_t>(node, 4) ^ At<uint32_t>(node, 8);
}

// A name's hash: h * 5 plus each character, sign-extended.
uint32_t HashName(char* node)
{
    uint32_t hash = 0;
    const char* const first = At<char*>(node, 4);
    const uint32_t length = static_cast<uint32_t>(At<char*>(node, 8) - first);
    for (uint32_t i = 0; i < length; ++i)
        hash = hash * 5 + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(first[i])));
    return hash;
}

void GrowPairHash(char* table, uint32_t count)
{
    GrowHash(table, count, HashPair);
}

void GrowNameHash(char* table, uint32_t count)
{
    GrowHash(table, count, HashName);
}

// A key pair and its value (12 bytes at `value`) put into a pair hash
// unless the pair is there (0x0058F230): a new node at the head of its
// bucket, the count raised. `out` gets the node, the table and whether it
// was put in (a byte); it is answered.
char* InsertPairUnique(char* table, char* out, const uint32_t* value)
{
    const uint32_t bucket = (value[0] ^ value[1]) % HsBuckets(table);
    char* const first = At<char**>(table, 4)[bucket];
    for (char* node = first; node; node = At<char*>(node, 0)) {
        if (At<uint32_t>(node, 4) == value[0] && At<uint32_t>(node, 8) == value[1]) {
            At<char*>(out, 0) = node;
            At<char*>(out, 4) = table;
            At<uint8_t>(out, 8) = 0;
            return out;
        }
    }
    char* const node = StAllocate(0x10);
    At<char*>(node, 0) = nullptr;
    std::memcpy(node + 4, value, 0xC);
    At<char*>(node, 0) = first;
    At<char**>(table, 4)[bucket] = node;
    ++At<uint32_t>(table, 0x10);
    At<char*>(out, 0) = node;
    At<char*>(out, 4) = table;
    At<uint8_t>(out, 8) = 1;
    return out;
}

// A list of events emptied (0x00584210): from the sentinel (`*list`) on,
// each node's object at +0xC let go (slot 8) and the node freed; the
// sentinel linked to itself both ways.
void ClearEvents(char* list)
{
    for (char* node = At<char*>(At<char*>(list, 0), 0); node != At<char*>(list, 0);) {
        char* const next = At<char*>(node, 0);
        if (char* const object = At<char*>(node, 0xC))
            HsLetGoEvent(object);
        StFree(node);
        node = next;
    }
    char* const sentinel = At<char*>(list, 0);
    At<char*>(sentinel, 0) = sentinel;
    At<char*>(sentinel, 4) = sentinel;
}

// The 0x40-byte counted object (0x005836A0): its counted base made (the
// count at +4 cleared, the base's table 0x00ACE624, 0x0090D957 inlined),
// its table 0x00A9EEA4; +8 cleared, an empty string at +0xC (storage of 8
// bytes, its first byte cleared), +0x18 one and the rest cleared. Answers
// the object.
char* ConstructCounted40(char* object)
{
    At<uint32_t>(object, 4) = 0;
    At<uint32_t>(object, 0) = 0x00ACE624;
    At<uint32_t>(object, 0) = 0x00A9EEA4;
    At<uint32_t>(object, 8) = 0;
    At<char*>(object, 0xC) = nullptr;
    At<char*>(object, 0x10) = nullptr;
    At<char*>(object, 0x14) = nullptr;
    char* const text = StAllocate(8);
    At<char*>(object, 0xC) = text;
    At<char*>(object, 0x10) = text;
    At<char*>(object, 0x14) = text + 8;
    *text = 0;
    for (uint32_t at = 0x1C; at <= 0x30; at += 4)
        At<uint32_t>(object, at) = 0;
    At<float>(object, 0x18) = 1.0f;
    At<uint32_t>(object, 0x34) = 0;
    At<uint32_t>(object, 0x38) = 0;
    At<uint32_t>(object, 0x3C) = 0;
    return object;
}

// Its destructor (0x00583720): the storage at +0x34 and +0xC freed, the
// counted base's table put back (0x0090D964, inlined).
void DestroyCounted40(char* object)
{
    if (char* const storage = At<char*>(object, 0x34))
        StFree(storage);
    if (char* const storage = At<char*>(object, 0xC))
        StFree(storage);
    At<uint32_t>(object, 0) = 0x00ACE624;
}

// Its deleting destructor (0x00583700, slot 0): destroyed, then deleted
// (operator delete 0x005E5620) when the flag's low bit is set. Answers the
// object.
char* DeleteCounted40(char* object, uint32_t flags)
{
    HsDestroyCounted40(object);
    if (flags & 1)
        HsDelete(object);
    return object;
}

// Each character of [first, last) passed (sign-extended) through `fn` and
// the result stored at `out` on (0x00586AC0). Answers the end of what it
// wrote.
char* TransformChars(const char* first, const char* last, char* out, uint32_t fn)
{
    for (; first != last; ++first, ++out)
        *out = static_cast<char>(reinterpret_cast<int(__cdecl*)(int)>(fn)(static_cast<signed char>(*first)));
    return out;
}

// The look-up of a compound description by name and the compound
// effect's taking of its description, written by hand from the game's
// code: SC4EffectsManager::FindDescriptionByName (0x0058F5A0), the name
// map's EqualRange (0x00586EF0) and SC4CompoundEffect::SetDescription
// (0x005BFF80). Included by water_effects.cpp. Offsets are from the manager
// object, 4 below the interface the 3D render holds.
//
// The name map (+0x98) is a hash multimap of strings to indices into the
// compound descriptions (+0xAC, 0x38 bytes each): buckets as a vector of
// node pointers at +4, a node a next pointer (+0), the key's text (+4 to
// +8) and the index (+0x10). A string is (begin, end, ...).

bool SameText(char* string, char* other)
{
    const std::size_t size = static_cast<std::size_t>(At<char*>(string, 4) - At<char*>(string, 0));
    return static_cast<std::size_t>(At<char*>(other, 4) - At<char*>(other, 0)) == size &&
           std::memcmp(At<char*>(string, 0), At<char*>(other, 0), size) == 0;
}

// The head of the first bucket after `bucket` with a node, or none.
char* NextBucket(char* map, uint32_t bucket)
{
    char** const buckets = At<char**>(map, 4);
    const uint32_t count = static_cast<uint32_t>((At<char*>(map, 8) - reinterpret_cast<char*>(buckets)) >> 2);
    for (uint32_t i = bucket + 1; i < count; ++i) {
        if (buckets[i])
            return buckets[i];
    }
    return nullptr;
}

// The nodes of a key (0x00586EF0): `out` gets the first node with that
// text and the node after the last, each with the map (none for none);
// the bucket is the game's string hash's (0x0051C9B0). Answers `out`.
char* NameEqualRange(char* map, char* out, char* key)
{
    const uint32_t bucket = CallThis<uint32_t>(0x0051C9B0, map, key);
    char* first = At<char**>(map, 4)[bucket];
    while (first && !SameText(first + 4, key))
        first = At<char*>(first, 0);
    char* last = nullptr;
    if (first) {
        last = At<char*>(first, 0);
        while (last && SameText(last + 4, key))
            last = At<char*>(last, 0);
        if (!last)
            last = NextBucket(map, bucket);
    }
    At<char*>(out, 0) = first;
    At<char*>(out, 4) = map;
    At<char*>(out, 8) = last;
    At<char*>(out, 0xC) = map;
    return out;
}

// The look-up (0x0058F5A0): of the descriptions of that name, the index of
// the one whose word +4 is the largest not above the manager's word at
// +0xB90 (and above 0); -1 for none. The nodes are gone through as the
// map's iterator does: the next node, else the head of the next bucket
// with one (the bucket found by hashing the node's text again).
int32_t FindDescriptionByName(char* self, char* name)
{
    char range[0x10];
    NameEqualRange(self + 0x98, range, name);
    int32_t found = -1;
    char* node = At<char*>(range, 0);
    if (node == At<char*>(range, 8))
        return found;
    char* const map = At<char*>(range, 4);
    uint32_t best = 0;
    do {
        const int32_t index = At<int32_t>(node, 0x10);
        const uint32_t count = At<uint32_t>(At<char*>(self, 0xAC) + index * 0x38, 4);
        if (count <= At<uint32_t>(self, 0xB90) && static_cast<int32_t>(count) > static_cast<int32_t>(best)) {
            found = index;
            best = count;
        }
        if (At<char*>(node, 0)) {
            node = At<char*>(node, 0);
        } else {
            uint32_t hash = 0;
            for (const char* at = At<char*>(node, 4); at != At<char*>(node, 8); ++at)
                hash = hash * 5 + static_cast<uint32_t>(static_cast<int32_t>(*at));
            node = NextBucket(map, hash % static_cast<uint32_t>((At<char*>(map, 8) - At<char*>(map, 4)) >> 2));
        }
    } while (node != At<char*>(range, 8));
    return found;
}

// A block of a compound effect made the identity's again when it has
// changed: the count of its changes (+0), the flag that it changed (+1),
// a matrix, a place and a scale (+4).
void ResetCompoundBlock(char* block)
{
    if (!At<uint8_t>(block, 1))
        return;
    for (uint32_t i = 0; i < 13; ++i)
        At<float>(block, 4 + i * 4) = (i % 4 == 0 && i < 9) || i == 12 ? 1.0f : 0.0f;
    ++At<uint8_t>(block, 0);
    At<uint8_t>(block, 1) = 0;
}

// The compound effect's taking of its description (0x005BFF80): the
// description's first word kept at +0x28, +0x2C and +0x144 cleared, its
// four blocks (+0x34, +0x6C, +0xA4, +0xDC) reset, the description's three
// words at +0x20 kept at +0x120, and its vector of values at +0x12C
// emptied.
void SetCompoundDescription(char* compound, char* description)
{
    At<uint32_t>(compound, 0x28) = At<uint32_t>(description, 0);
    At<uint32_t>(compound, 0x2C) = 0;
    At<uint32_t>(compound, 0x144) = 0;
    for (const uint32_t block : {0x34u, 0x6Cu, 0xA4u, 0xDCu})
        ResetCompoundBlock(compound + block);
    At<uint32_t>(compound, 0x120) = At<uint32_t>(description, 0x20);
    At<uint32_t>(compound, 0x124) = At<uint32_t>(description, 0x24);
    At<uint32_t>(compound, 0x128) = At<uint32_t>(description, 0x28);
    At<char*>(compound, 0x130) = At<char*>(compound, 0x12C);
}

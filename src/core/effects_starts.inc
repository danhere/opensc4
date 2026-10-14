// The look-up of a compound description by its IDs and two of the
// manager's ways of starting compound effects, written by hand from the
// game's code: the ID map's EqualRange (0x0058EE80),
// SC4EffectsManager::FindDescriptionById (0x0058F2E0), ::StartById
// (0x00593AE0) and ::RestartInUse (0x00593B90). Included by
// water_effects.cpp, which has the calls that the starts' comparison notes
// (the Sb functions). Offsets are from the manager object, 4 below the
// interface the 3D render holds.
//
// The ID map (+0xD00) is a hash multimap of a pair of IDs to indices into
// the compound descriptions: buckets as a vector of node pointers at +4, a
// node the next node (+0), the two IDs (+4, +8) and the index (+0xC); a
// pair's bucket is the two IDs exclusive-ored, modulo the bucket count.

uint32_t IdBucketCount(char* map)
{
    return static_cast<uint32_t>((At<char*>(map, 8) - At<char*>(map, 4)) >> 2);
}

// The head of the first bucket after `bucket` with a node, or none.
char* NextIdBucket(char* map, uint32_t bucket)
{
    for (uint32_t i = bucket + 1; i < IdBucketCount(map); ++i) {
        if (At<char**>(map, 4)[i])
            return At<char**>(map, 4)[i];
    }
    return nullptr;
}

// The nodes of a pair of IDs (0x0058EE80): `out` gets the first node with
// them and the node after the last, each with the map. Answers `out`.
char* IdEqualRange(char* map, char* out, const uint32_t* key)
{
    const uint32_t bucket = (key[0] ^ key[1]) % IdBucketCount(map);
    const auto same = [&](char* node) { return At<uint32_t>(node, 4) == key[0] && At<uint32_t>(node, 8) == key[1]; };
    char* first = At<char**>(map, 4)[bucket];
    while (first && !same(first))
        first = At<char*>(first, 0);
    char* last = nullptr;
    if (first) {
        last = At<char*>(first, 0);
        while (last && same(last))
            last = At<char*>(last, 0);
        if (!last)
            last = NextIdBucket(map, bucket);
    }
    At<char*>(out, 0) = first;
    At<char*>(out, 4) = map;
    At<char*>(out, 8) = last;
    At<char*>(out, 0xC) = map;
    return out;
}

// The look-up (0x0058F2E0): of the descriptions of that pair of IDs, the
// index of the one whose word +4 is the largest above 0 and not above the
// manager's word at +0xB90; -1 for none. The nodes are gone through as the
// map's iterator does.
int32_t FindDescriptionById(char* self, uint32_t group, uint32_t instance)
{
    const uint32_t key[2] = {group, instance};
    char range[0x10];
    IdEqualRange(self + 0xD00, range, key);
    int32_t found = -1;
    char* node = At<char*>(range, 0);
    if (node == At<char*>(range, 8))
        return found;
    char* const map = At<char*>(range, 4);
    uint32_t best = 0;
    do {
        const int32_t index = At<int32_t>(node, 0xC);
        const uint32_t count = At<uint32_t>(At<char*>(self, 0xAC) + index * 0x38, 4);
        if (count <= At<uint32_t>(self, 0xB90) && static_cast<int32_t>(count) > static_cast<int32_t>(best)) {
            found = index;
            best = count;
        }
        if (At<char*>(node, 0))
            node = At<char*>(node, 0);
        else
            node = NextIdBucket(map, (At<uint32_t>(node, 8) ^ At<uint32_t>(node, 4)) % IdBucketCount(map));
    } while (node != At<char*>(range, 8));
    return found;
}

// A compound effect started by its IDs (0x00593AE0, a slot of the
// manager's interface): false without a place for it; else the
// description of those IDs, and when there is none and the ID map has no
// node of them either (0x0058EF90), false. A compound effect is made
// (NewCompound) with the IDs at +0x18 and +0x1C and the index at +0x20;
// one with a description is given it and the waiting ones started. The
// caller gets it, held once more (its slot 4).
bool StartCompoundById(char* self, uint32_t group, uint32_t instance, char** out)
{
    if (!out)
        return false;
    const int32_t index = SbFindById(self, group, instance);
    if (index < 0) {
        const uint32_t key[2] = {group, instance};
        if (!SbFindNode(self + 0xD00, key))
            return false;
    }
    char* const compound = SbNewCompound(self);
    At<uint32_t>(compound, 0x18) = group;
    At<uint32_t>(compound, 0x1C) = instance;
    At<int32_t>(compound, 0x20) = index;
    if (index >= 0) {
        SbCompoundDescription(compound, At<char*>(self, 0xAC) + index * 0x38);
        SbStartQueued(self);
    }
    *out = compound;
    SbHold(compound);
    return true;
}

// Every compound effect in use started again (0x00593B90, with `first`
// clear), when the byte +0x1AD is set; then the waiting ones started.
void RestartCompoundsInUse(char* self)
{
    if (!At<uint8_t>(self, 0x1AD))
        return;
    for (uint32_t* at = At<uint32_t*>(self, 0xBAC); at != At<uint32_t*>(self, 0xBB0); ++at)
        SbStartCompound(self, At<char**>(self, 0xB94)[*at], 0);
    SbStartQueued(self);
}

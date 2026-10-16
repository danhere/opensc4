// The look-up of a compound description by its IDs and two of the
// manager's ways of starting compound effects, written by hand from the
// game's code: the ID map's EqualRange (0x0058EE80),
// SC4EffectsManager::FindDescriptionById (0x0058F2E0), ::StartById
// (0x00593AE0), ::RestartInUse (0x00593B90), ::StartByName (0x005939B0)
// and ::OnZoomChanged (0x00593BE0). Included by
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

// A compound effect started by its name (0x005939B0, slot 0x1C of the
// manager's interface): false without a name, with an empty one or without
// a place for it. The name is copied into the manager's string at +0x1A0
// and lowered there, then looked up; with no description, a string made
// from it is looked for in the manager's set of names (+0x98), and with
// none there either the answer is false. A compound effect is made with the
// lowered name at +0xC and the index at +0x20; one with a description is
// given it and the waiting ones started. The caller gets it, held once
// more. The string helpers are shared, so they stay the game's.
bool StartCompoundByName(char* self, const char* name, char** out)
{
    if (!name || !*name || !out)
        return false;
    char* const lowered = self + 0x1A0;
    CallThis<char*>(addr::SC4StringBody_AssignChars, lowered, name, name + std::strlen(name));
    reinterpret_cast<char*(__cdecl*)(char*, char*, char*, uint32_t)>(addr::SC4_TransformChars)(
        At<char*>(lowered, 0), At<char*>(lowered, 4), At<char*>(lowered, 0), addr::CRT_tolower);
    const int32_t index = SbFindByName(self, lowered);
    if (index < 0) {
        // The game passes the place of its `out` argument as the allocator,
        // which the constructor does not read.
        char known[0xC];
        CallThis<char*>(addr::cSC4Advice_F408000, known, At<char*>(lowered, 0), &out);
        const bool found = CallThis<uint32_t>(addr::SC4StringHashSet_Find, self + 0x98, known) != 0;
        if (At<char*>(known, 0))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<char*>(known, 0));
        if (!found)
            return false;
    }
    char* const compound = SbNewCompound(self);
    SbAssignName(compound, lowered);
    At<int32_t>(compound, 0x20) = index;
    if (index >= 0) {
        SbCompoundDescription(compound, At<char*>(self, 0xAC) + index * 0x38);
        SbStartQueued(self);
    }
    *out = compound;
    SbHold(compound);
    return true;
}

// The change of zoom (0x00593BE0, from the manager's handler of the view's
// change): +0xC80 the zoom, +0xC88 the old +0xC84, +0xC84 the zoom less
// +0xC8C plus 1, at least 1. +0xCD0 is the table of floats at +0xCB4 read
// at the zoom times 0.25 times the table's last index, in a straight line
// between the two entries around it; the game takes the whole part by
// rounding x - 0.5 with 1.5 times 2 to the 23rd (0x00A9F0AC) and moving up
// one when x is that plus one. +0xCD4 is a float of the object at
// [0x00B43CC4]+0x24 (+0x1D4) times the camera view's +0x150. Then every
// compound effect in use is started again.
void ZoomChanged(char* self, int32_t zoom)
{
    const uint32_t previous = At<uint32_t>(self, 0xC84);
    At<int32_t>(self, 0xC80) = zoom;
    const int32_t steps = zoom - At<int32_t>(self, 0xC8C) + 1;
    At<uint32_t>(self, 0xC88) = previous;
    At<int32_t>(self, 0xC84) = steps > 1 ? steps : 1;
    const float* const table = At<float*>(self, 0xCB4);
    const int32_t last = static_cast<int32_t>((At<char*>(self, 0xCB8) - At<char*>(self, 0xCB4)) >> 2) - 1;
    const auto unsignedValue = [](int32_t value) {
        return value < 0 ? static_cast<double>(value) + 4294967296.0 : static_cast<double>(value);
    };
    float value;
    if (last == 0) {
        value = table[0];
    } else {
        const double x = static_cast<double>(zoom) * static_cast<double>(0.25f) * unsignedValue(last);
        const float rounded = static_cast<float>(x - 0.5 + 12582912.0);
        uint32_t bits;
        std::memcpy(&bits, &rounded, sizeof bits);
        int32_t whole = static_cast<int32_t>(bits + 0xB4C00000u);
        const int32_t next = static_cast<int32_t>(static_cast<uint32_t>(whole) + 1);
        if (static_cast<double>(next) == x)
            whole = next;
        const double fraction = x - unsignedValue(whole);
        if (fraction > 0.0)
            value = static_cast<float>(
                (static_cast<double>(table[whole + 1]) - static_cast<double>(table[whole])) * fraction +
                static_cast<double>(table[whole]));
        else
            value = table[whole];
    }
    At<float>(self, 0xCD0) = value;
    At<float>(self, 0xCD4) = static_cast<float>(
        static_cast<double>(At<float>(At<char*>(Global<char*>(addr::SC4_App43CC4), 0x24), 0x1D4)) *
        static_cast<double>(At<float>(Global<char*>(addr::SC4_CameraView), 0x150)));
    SbRestart(self);
}

// The makers of what a record holds, written by hand from the game's code:
// SC4EffectsManager::NewEffect (0x00590BC0), ::NewEffectB (0x00590D40),
// ::NewDecal (0x00591160) and ::NewCompound (0x00590A60), and the decal's
// reset (SC4EffectDecal::Reset, 0x00581F80) one of them calls. Included by
// water_effects.cpp, which has the calls that the comparison notes (the Mx
// functions). Offsets are from the manager object, 4 below the interface
// the 3D render holds.
//
// Each kind the manager makes has a list of three vectors: a table of
// pointers to every one it has made (it holds each), the indices in use
// and the free ones. A compound effect's maker puts the index in the
// vector of those waiting to start (+0xBA0) instead of those in use.

// The decal's reset (0x00581F80): the byte +8 cleared, and when the byte
// +0x75 is set, the thirteen floats from +0x78 (a matrix, a place, a scale)
// made the identity's, +0x75 cleared and the count at +0x74 one more; the
// word at +0x70 set to -1.
void ResetDecal(char* decal)
{
    At<uint8_t>(decal, 8) = 0;
    if (At<uint8_t>(decal, 0x75)) {
        for (uint32_t i = 0; i < 13; ++i)
            At<float>(decal, 0x78 + i * 4) = (i % 4 == 0 && i < 9) || i == 12 ? 1.0f : 0.0f;
        At<uint8_t>(decal, 0x75) = 0;
        ++At<uint8_t>(decal, 0x74);
    }
    At<int32_t>(decal, 0x70) = -1;
}

// One more index at the end of a vector of words, through the game's
// insert when there is no room (0x0051CA60).
void MxPushIndex(char* vector, uint32_t index)
{
    uint32_t* const end = At<uint32_t*>(vector, 4);
    if (end != At<uint32_t*>(vector, 8)) {
        if (end)
            *end = index;
        At<uint32_t*>(vector, 4) = end + 1;
    } else {
        MxGrowIndex(vector, end, index);
    }
}

// A maker (`spec` says which). With a free one, the last free index is
// taken, put among those in use (or waiting) and the one at that index
// answered. Else a new one: allocated (0x005E55E0) and constructed; an
// effect has its sub-object's slot 0x10 called and is given the manager's
// block at +0xCA0 (slot 0x34), and one of the first class, when the
// options' byte +0x33C is set and the manager has an object at +0xDEC, is
// given it (0x005B26B0); a decal is reset; a compound effect holds the
// manager at +0x148 (the manager's slot 4; the one it held let go, slot
// 8). Its index, the table's size, goes among those in use (or waiting),
// and it goes into the table held (slot 0x1C, a compound effect's slot 4),
// through the game's insert when the table has no room (0x0058FF70, a
// compound effect's 0x00430A90); the hold taken around that is let go
// after (slot 0x20, slot 8). Only the decal's and the compound effect's
// makers test for a failed allocation.
char* MakeFromPool(char* self, const MxSpec& spec)
{
    const bool compound = spec.maker == addr::SC4EffectsManager_NewCompound;
    const bool tested = compound || spec.maker == addr::SC4EffectsManager_NewDecal;
    char* const freed = self + spec.freed;
    if (At<uint32_t*>(freed, 0) != At<uint32_t*>(freed, 4)) {
        if (compound) {
            MxPushIndex(self + spec.used, At<uint32_t*>(freed, 4)[-1]);
            At<uint32_t*>(freed, 4) -= 1;
            return At<char**>(self, spec.table)[At<uint32_t*>(self + spec.used, 4)[-1]];
        }
        const uint32_t index = At<uint32_t*>(freed, 4)[-1];
        At<uint32_t*>(freed, 4) -= 1;
        MxPushIndex(self + spec.used, index);
        return At<char**>(self, spec.table)[index];
    }
    char* object = MxNew(spec.size);
    if (object)
        object = MxConstruct(spec.constructor, object);
    if (spec.maker == addr::SC4EffectsManager_NewDecal) {
        MxResetDecal(object);
    } else if (compound) {
        char* const old = At<char*>(object, 0x148);
        if (self != old) {
            if (self)
                MxCall(self, 0x4);
            At<char*>(object, 0x148) = self;
            if (old)
                MxCall(old, 0x8);
        }
    } else {
        MxCall(object + 4, 0x10);
        MxCall(object, 0x34, self + 0xCA0);
        if (spec.maker == addr::SC4EffectsManager_NewEffect &&
            At<uint8_t>(At<char*>(M<char*>(0x00B43CC4), 0xC), 0x33C) && At<uint32_t>(self, 0xDEC))
            MxSetObject(object, At<uint32_t>(self, 0xDEC));
    }
    char* const table = self + spec.table;
    MxPushIndex(self + spec.used,
                static_cast<uint32_t>(static_cast<int32_t>(At<char*>(table, 4) - At<char*>(table, 0)) >> 2));
    const uint32_t hold = compound ? 0x4 : 0x1C, end = compound ? 0x8 : 0x20;
    if (!tested || object)
        MxCall(object, hold);
    char** const place = At<char**>(table, 4);
    if (place != At<char**>(table, 8)) {
        if (place) {
            *place = object;
            if (!tested || object)
                MxCall(object, hold);
        }
        At<char**>(table, 4) = place + 1;
    } else {
        MxGrowTable(compound, table, place, object);
    }
    if (!tested || object)
        MxCall(object, end);
    return object;
}

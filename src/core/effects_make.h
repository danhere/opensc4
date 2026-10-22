// The making of a record's effect, written by hand from the game's code:
// SC4EffectsManager::MakeRecordEffect (0x00591730) and the small function
// it gives the record what it made with, SC4EffectHolder::Assign
// (0x0058D8D0). Included by water_effects.cpp, which has the calls that the
// comparison notes (the Mk functions). Offsets are from the manager object,
// 4 below the interface the 3D render holds.
//
// A part of a compound effect's description (0x6C bytes) says what it is
// by the word at +0xC, and which one by the index at +0x68. The record
// (0x44 bytes) holds what is made at +0x38 and has flags at +0x40.

// What a record holds changed for another (0x0058D8D0; `holder` is the
// place of the pointer): the new one held (its slot 0x1C), the old one let
// go (its slot 0x20); nothing for the same one. Answers the place.
char* AssignHeld(char* holder, char* object)
{
    char* const old = At<char*>(holder, 0);
    if (object != old) {
        if (object)
            CallThis<void>(At<uint32_t>(At<char*>(object, 0), 0x1C), object);
        At<char*>(holder, 0) = object;
        if (old)
            CallThis<void>(At<uint32_t>(At<char*>(old, 0), 0x20), old);
    }
    return holder;
}

// The making (0x00591730; `made` is a word the start keeps from one part
// to the next).
//
// Kind 0, an effect of particles, as many as the part's count (+0x4E; the
// record ends up holding the last). Its description is the manager's
// (the table of pointers at +0xCC). The effect is of the first class
// (made by 0x00590BC0) for a description without flag 1 (the byte +8); with
// it, of the second class (0x00590D40) when the description's float at
// +0x1D8 is 0, else of a third (0x00590EA0). The effect takes the
// description (slot 0x28); each after the first is told one value of
// 0x101, the part's word at +0x50 times its place in the count (slot
// 0x14). An effect of the second class of a part with flag 2 (+0x10) is
// remembered in `made`, after the one remembered before is given it
// (0x005AC1C0) and it is marked (0x005B2230); any other clears `made`. The
// record holds the effect (slot 0x1C) and lets go of what it held (slot
// 0x20).
//
// Kind 0x10: an object made by 0x00591000, given the word at the index of
// the manager's table at +0xEC (slot 0x3C), and held the same way.
//
// Kind 1, a decal: made by 0x00591160 and given its description
// (0x00583480; 0x64 bytes each in the table at +0x10C).
//
// Kind 2, another compound effect: made by 0x00590A60, given the part's
// name (the string at the part's start, to +0xC), the description of that
// name (0x0058F5A0; its index kept at +0x20, and the description given by
// 0x005BFF80 when there is one); the record holds its part at +4.
//
// Kinds 3 to 0xF: the manager has a list for each (0x20 bytes each from
// +0x158's, the vector at +0x14); the entry at the index is asked for an
// object (its slot 4), which the record holds, with flag 2 set. An index
// outside the list leaves the record as it is, its flags too.
//
// Last the record's flag 4 is set or cleared as the part's flag 1 is.
void MakeRecordEffect(char* self, char* part, char* record, uint32_t* made)
{
    const int32_t kind = At<int32_t>(part, 0xC);
    if (kind == 0) {
        char* const description = At<char**>(self, 0xCC)[At<int32_t>(part, 0x68)];
        for (int32_t i = 0; i < At<uint16_t>(part, 0x4E); ++i) {
            char* second = nullptr;
            char* effect = nullptr;
            if (At<uint8_t>(description, 8) & 1) {
                if (At<float>(description, 0x1D8) == 0.0f)
                    effect = second = MkMaker(addr::SC4EffectsManager_NewEffectB, self);
                else
                    effect = MkMaker(addr::SC4EffectsManager_NewEffectC, self);
            } else {
                effect = MkMaker(addr::SC4EffectsManager_NewEffect, self);
            }
            MkSetDescription(effect, description);
            if (i > 0) {
                float value = static_cast<float>(static_cast<int32_t>(At<uint16_t>(part, 0x50) * i));
                MkValues(effect, 0x101, &value, 1);
            }
            if (second && (At<uint32_t>(part, 0x10) >> 1 & 1)) {
                if (*made) {
                    MkLink(reinterpret_cast<char*>(*made), second);
                    MkMark(second);
                }
                *made = reinterpret_cast<uint32_t>(second);
            } else {
                *made = 0;
            }
            char* const held = At<char*>(record, 0x38);
            if (effect != held) {
                MkHold(effect);
                At<char*>(record, 0x38) = effect;
                if (held)
                    MkEnd(held);
            }
        }
    } else if (kind == 0x10) {
        const uint32_t value = At<uint32_t*>(self, 0xEC)[At<int32_t>(part, 0x68)];
        char* const object = MkMaker(addr::SC4EffectsManager_NewEffect10, self);
        MkGive(object, value);
        char* const held = At<char*>(record, 0x38);
        if (object != held) {
            MkHold(object);
            At<char*>(record, 0x38) = object;
            if (held)
                MkEnd(held);
        }
    } else if (kind == 1) {
        char* const description = At<char*>(self, 0x10C) + At<int32_t>(part, 0x68) * 0x64;
        char* const decal = MkMaker(addr::SC4EffectsManager_NewDecal, self);
        MkDecalDescription(decal, description);
        MkAssign(record + 0x38, decal);
    } else if (kind == 2) {
        char* const compound = MkMaker(addr::SC4EffectsManager_NewCompound, self);
        MkString(compound + 0xC, part);
        const int32_t index = MkFind(self, part);
        At<int32_t>(compound, 0x20) = index;
        if (index >= 0)
            MkCompoundDescription(compound, At<char*>(self, 0xAC) + index * 0x38);
        MkAssign(record + 0x38, compound + 4);
    } else if (kind >= 3 && kind <= 0xF) {
        const int32_t which = At<int32_t>(part, 0x68);
        if (which < 0)
            return;
        char* const list = At<char*>(self, 0x158) + (kind - 3) * 0x20 + 0x14;
        char** const begin = At<char**>(list, 0);
        if (static_cast<uint32_t>(which) >= static_cast<uint32_t>((At<char*>(list, 4) - reinterpret_cast<char*>(begin)) >> 2))
            return;
        char* const source = begin[which];
        MkAssign(record + 0x38, MkAsk(source));
        At<uint32_t>(record, 0x40) |= 2;
    }
    if (At<uint8_t>(part, 0x10) & 1)
        At<uint32_t>(record, 0x40) |= 4;
    else
        At<uint32_t>(record, 0x40) &= ~4u;
}

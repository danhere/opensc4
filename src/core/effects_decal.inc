// A decal's small functions and two of the manager's, written by hand from
// the game's code: the decal's (vtable 0x00A9EE78, 0x124 bytes) parameter
// setter (0x00582F00, slot 5) and Release (0x005833A0, slot 8), its
// description's (0x64 bytes, at the decal's +0xC) end (0x00583130) and
// assignment (0x005833F0); the manager's fill of held pointers
// (0x0058DF60) and the AddRef thunks of its interfaces at +4 and +8
// (0x0058FD60, 0x0058FD80). The decal's update (slot 3) is generated
// (effects_decal_update.inc). Included by water_effects.cpp, which has
// what ours calls (the Dc functions, noted in a replay).

// Slot 5 (0x00582F00): a parameter by its id. 3: the word at value to
// +0x108. 0x102: the word at value + 4 to +0x10C. 0xC: a rectangle (x0,
// z0, x1, z1 as floats) gives the middle times 8 at +0xE4 and +0xE8 (the
// z sum kept as a float first), the width times 16 at +0xEC, the depth
// over the width at +0x104, with the description's flag 8 the width at
// +0x100; +0xF0 cleared and the byte +0x110 set. Answers AL, false for any
// other id.
uint32_t SetDecalParameter(char* decal, uint32_t id, const char* value)
{
    if (id == 3) {
        At<uint32_t>(decal, 0x108) = *reinterpret_cast<const uint32_t*>(value);
        return 1;
    }
    if (id == 0x102) {
        At<uint32_t>(decal, 0x10C) = *reinterpret_cast<const uint32_t*>(value + 4);
        return 1;
    }
    if (id != 0xC)
        return 0;
    const float* const rect = reinterpret_cast<const float*>(value);
    const double eight = M<float>(0x00A8E178);
    const float sumZ = static_cast<float>(static_cast<double>(rect[1]) + rect[3]);
    At<float>(decal, 0xE4) = static_cast<float>((static_cast<double>(rect[0]) + rect[2]) * eight);
    At<float>(decal, 0xE8) = static_cast<float>(static_cast<double>(sumZ) * eight);
    const double width = static_cast<double>(rect[2]) - rect[0];
    const double depth = static_cast<double>(rect[3]) - rect[1];
    At<uint32_t>(decal, 0xF0) = 0;
    At<float>(decal, 0xEC) = static_cast<float>(width * M<float>(0x00A8D45C));
    At<float>(decal, 0x104) = static_cast<float>(depth / width);
    if (At<uint32_t>(decal, 0xC) >> 3 & 1)
        At<float>(decal, 0x100) = static_cast<float>(width);
    At<uint8_t>(decal, 0x110) = 1;
    return 1;
}

// The description's end (0x00583130): its five lists' storage freed
// (+0x40, +0x34, +0x28, +0x1C, +0x10); the pointers are left.
void DestroyDecalDescription(char* description)
{
    for (const uint32_t at : {0x40u, 0x34u, 0x28u, 0x1Cu, 0x10u}) {
        if (char* const storage = At<char*>(description, at))
            StFree(storage);
    }
}

// The description's assignment (0x005833F0): the words +0, +4, +0xC and
// +0x4C to +0x60 and the bytes +8 and +9 copied; the lists of floats at
// +0x10, +0x1C, +0x28 and +0x40 assigned (0x00464430), the list of three
// floats at +0x34 (0x0050C540). Answers this.
char* AssignDecalDescription(char* to, char* from)
{
    At<uint32_t>(to, 0) = At<uint32_t>(from, 0);
    At<uint32_t>(to, 4) = At<uint32_t>(from, 4);
    At<uint8_t>(to, 8) = At<uint8_t>(from, 8);
    At<uint8_t>(to, 9) = At<uint8_t>(from, 9);
    At<uint32_t>(to, 0xC) = At<uint32_t>(from, 0xC);
    LsAssignFloats(to + 0x10, from + 0x10);
    LsAssignFloats(to + 0x1C, from + 0x1C);
    LsAssignFloats(to + 0x28, from + 0x28);
    DcAssignFloat3s(to + 0x34, from + 0x34);
    LsAssignFloats(to + 0x40, from + 0x40);
    for (uint32_t at = 0x4C; at <= 0x60; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(from, at);
    return to;
}

// Slot 8 (0x005833A0): the count at +4 down by one, answered; at zero the
// table put back, the object at +0x120 let go (its slot 8), the texture at
// +0x11C let go (0x008090E0), the description ended (0x00583130) and the
// decal deleted (0x005E5620), answering 0.
uint32_t ReleaseDecal(char* decal)
{
    const uint32_t count = At<uint32_t>(decal, 4) - 1;
    At<uint32_t>(decal, 4) = count;
    if (count)
        return count;
    At<uint32_t>(decal, 0) = 0x00A9EE78;
    if (char* const held = At<char*>(decal, 0x120))
        DcLetGo(held);
    if (char* const texture = At<char*>(decal, 0x11C))
        DcReleaseTexture(texture);
    LsDestroyDecal(decal + 0xC);
    DcDelete(decal);
    return 0;
}

// The manager's fill of held pointers (0x0058DF60, cdecl): count copies of
// the pointer at value from place on, each held (its slot 1); nothing
// written where the place is null. Answers the place after the last.
char* FillHeldPointers(char* place, uint32_t count, char* const* value)
{
    for (; count; --count, place += 4) {
        if (!place)
            continue;
        char* const object = *value;
        At<char*>(place, 0) = object;
        if (object)
            CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 4), object);
    }
    return place;
}

// The AddRef thunks of the manager's interfaces at +4 and +8 (0x0058FD60,
// 0x0058FD80): the manager's AddRef (0x0058D840) on this less 4 or 8.
uint32_t AddRefManagerFrom(char* self, uint32_t offset)
{
    return CallThis<uint32_t>(addr::SC4EffectsManager_AddRef, self - offset);
}

// The constructors of a decal and a compound effect, written by hand from
// the game's code: SC4EffectDecal::Construct (0x00583190), the decal
// description's (SC4DecalDescription::Construct, 0x00582FE0) and
// SC4CompoundEffect::Construct (0x005C0150). Included by water_effects.cpp.
// The game's code stores some fields twice (a global's value, then the
// final one); only the final values are written here.

char* StlAllocate(std::size_t size)
{
    return reinterpret_cast<char*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(size);
}

// A block: the count of its changes and the flag that it changed cleared,
// then a matrix, a place and a scale made the identity's.
void ClearBlock(char* block)
{
    At<uint8_t>(block, 0) = 0;
    At<uint8_t>(block, 1) = 0;
    for (uint32_t i = 0; i < 13; ++i)
        At<float>(block, 4 + i * 4) = (i % 4 == 0 && i < 9) || i == 12 ? 1.0f : 0.0f;
}

// A vector of floats (begin, end, room) holding one value, as the game's
// fill (0x00581F20) makes it from a whole number.
void OneFloat(char* vector, int32_t value)
{
    char* const storage = StlAllocate(4);
    At<char*>(vector, 0) = storage;
    At<char*>(vector, 8) = storage + 4;
    At<char*>(vector, 4) = reinterpret_cast<char*(__cdecl*)(char*, uint32_t, const int32_t*)>(0x00581F20)(storage, 1u,
                                                                                                         &value);
}

// A decal's description (0x00582FE0, 0x64 bytes): its flags and +4 cleared,
// the byte +8 cleared and +9 set, a speed of 5 (+0xC), four vectors of one
// float (+0x10 holding 0, +0x1C, +0x28 and +0x40 holding 1), one of one
// three-float value (+0x34, the three floats at 0x00B0A214), +0x4C to
// +0x54 cleared, +0x58 one, and +0x5C and +0x60 the word at 0x00B22C08.
char* ConstructDecalDescription(char* self)
{
    At<uint32_t>(self, 0) = 0;
    At<uint32_t>(self, 4) = 0;
    At<uint8_t>(self, 8) = 0;
    At<uint8_t>(self, 9) = 1;
    At<float>(self, 0xC) = 5.0f;
    OneFloat(self + 0x10, 0);
    OneFloat(self + 0x1C, 1);
    OneFloat(self + 0x28, 1);
    char* const triple = StlAllocate(0xC);
    At<char*>(self, 0x34) = triple;
    At<char*>(self, 0x3C) = triple + 0xC;
    if (triple)
        std::memcpy(triple, reinterpret_cast<const void*>(0x00B0A214), 0xC);
    At<char*>(self, 0x38) = triple + 0xC;
    OneFloat(self + 0x40, 1);
    At<uint32_t>(self, 0x4C) = 0;
    At<uint32_t>(self, 0x50) = 0;
    At<uint32_t>(self, 0x54) = 0;
    At<float>(self, 0x58) = 1.0f;
    At<uint32_t>(self, 0x5C) = M<uint32_t>(0x00B22C08);
    At<uint32_t>(self, 0x60) = M<uint32_t>(0x00B22C08);
    return self;
}

// A decal (0x00583190, 0x124 bytes): its table of functions (0x00A9EE78),
// no holders (+4), the byte +8 cleared, its description at +0xC
// constructed, +0x70 -1, its two blocks (+0x74, +0xAC) cleared, +0xE4,
// +0xE8, +0xF0 and the byte +0x110 cleared, +0xEC and +0xF4 to +0x10C one,
// +0x118 one, no texture (+0x11C) and +0x120 cleared.
char* ConstructDecal(char* self)
{
    At<uint32_t>(self, 0) = 0x00A9EE78;
    At<uint32_t>(self, 4) = 0;
    At<uint8_t>(self, 8) = 0;
    CallThis<char*>(addr::SC4DecalDescription_Construct, self + 0xC);
    At<int32_t>(self, 0x70) = -1;
    ClearBlock(self + 0x74);
    ClearBlock(self + 0xAC);
    At<uint32_t>(self, 0xE4) = 0;
    At<uint32_t>(self, 0xE8) = 0;
    At<uint32_t>(self, 0xF0) = 0;
    At<uint8_t>(self, 0x110) = 0;
    At<float>(self, 0xEC) = 1.0f;
    for (uint32_t at = 0xF4; at <= 0x10C; at += 4)
        At<float>(self, at) = 1.0f;
    At<float>(self, 0x118) = 1.0f;
    At<uint32_t>(self, 0x11C) = 0;
    At<uint32_t>(self, 0x120) = 0;
    return self;
}

// A compound effect (0x005C0150, 0x14C bytes): its two tables of functions
// (0x00AA1170, and 0x00AA1148 at +4), no holders (+8), an empty name
// (+0xC: eight bytes of room, the first 0), no IDs (+0x18, +0x1C), no
// description (+0x20, -1), the bytes +0x24 and +0x25 cleared, +0x28 to
// +0x30 cleared, its four blocks (+0x34, +0x6C, +0xA4, +0xDC) cleared, no
// records (+0x114), and +0x12C to +0x148 cleared.
char* ConstructCompoundEffect(char* self)
{
    At<uint32_t>(self, 0) = 0x00AA1170;
    At<uint32_t>(self, 4) = 0x00AA1148;
    At<uint32_t>(self, 8) = 0;
    char* const text = StlAllocate(8);
    At<char*>(self, 0xC) = text;
    At<char*>(self, 0x10) = text;
    At<char*>(self, 0x14) = text + 8;
    text[0] = 0;
    At<uint32_t>(self, 0x18) = 0;
    At<uint32_t>(self, 0x1C) = 0;
    At<int32_t>(self, 0x20) = -1;
    At<uint8_t>(self, 0x24) = 0;
    At<uint8_t>(self, 0x25) = 0;
    At<uint32_t>(self, 0x28) = 0;
    At<uint32_t>(self, 0x2C) = 0;
    At<uint32_t>(self, 0x30) = 0;
    for (const uint32_t block : {0x34u, 0x6Cu, 0xA4u, 0xDCu})
        ClearBlock(self + block);
    At<uint32_t>(self, 0x114) = 0;
    At<uint32_t>(self, 0x118) = 0;
    At<uint32_t>(self, 0x11C) = 0;
    for (uint32_t at = 0x12C; at <= 0x148; at += 4)
        At<uint32_t>(self, at) = 0;
    return self;
}

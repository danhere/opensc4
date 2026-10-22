// A decal's taking of its description, written by hand from the game's
// code: SC4EffectDecal::SetDescription (0x00583480). Included by
// water_effects.cpp, which has the two calls that the comparison notes
// (the Ds functions).
//
// A decal (0x124 bytes) keeps a copy of its description (0x64 bytes) at
// +0xC; the description's first word holds its flags and its +0xC a speed.

// The decal's description (0x00583480): copied in (0x005833F0); the
// reciprocal of the speed (of 0.0001 when the speed is not above that)
// kept at +0x118; the texture it held (+0x11C) let go (0x008090E0). Three
// scales drawn from the generator at 0x00B22C10 (0x00913761): +0xF4
// within the description's +0x58 of 1, +0xF8 within its +0x5C of 1, and
// +0xFC within its +0x60 of 0 when that is above 0, else 1. +0x108,
// +0x10C, +0x100 and +0x104 are 1, +0xE4, +0xE8, +0xF0 and the byte +0x110
// cleared, +0xEC the scale at +0xF8. With the description's flag 0x10 the
// byte +0x110 is set and the city's (0x00B43CEC) two sizes (slots 0x168
// and 0x164) give half of each at +0xE8 and +0xE4, and the second whole
// at +0xEC.
void SetDecalDescription(char* decal, char* description)
{
    DsCopy(decal + 0xC, description);
    const float floor = M<float>(0x00A99E1C);
    const float speed = At<float>(description, 0xC);
    At<float>(decal, 0x118) =
        static_cast<float>(static_cast<double>(M<float>(0x00A81228)) / (floor < speed ? speed : 0.0001f));
    if (char* const texture = At<char*>(decal, 0x11C)) {
        At<char*>(decal, 0x11C) = nullptr;
        DsRelease(texture);
    }
    const double one = M<double>(0x00A80AB0);
    At<float>(decal, 0x108) = 1.0f;
    At<float>(decal, 0x10C) = 1.0f;
    At<float>(decal, 0xF4) =
        static_cast<float>(DsUniform(one - At<float>(decal, 0x58), one + At<float>(decal, 0x58)));
    At<float>(decal, 0xF8) =
        static_cast<float>(DsUniform(one - At<float>(decal, 0x5C), one + At<float>(decal, 0x5C)));
    const float spread = At<float>(decal, 0x60);
    At<float>(decal, 0xFC) = spread > M<float>(0x00A81054) ? static_cast<float>(DsUniform(-spread, spread)) : 1.0f;
    At<float>(decal, 0x100) = 1.0f;
    At<float>(decal, 0x104) = 1.0f;
    At<uint32_t>(decal, 0xE4) = 0;
    At<uint32_t>(decal, 0xE8) = 0;
    At<uint32_t>(decal, 0xEC) = At<uint32_t>(decal, 0xF8);
    At<uint32_t>(decal, 0xF0) = 0;
    At<uint8_t>(decal, 0x110) = 0;
    if (At<uint32_t>(decal, 0xC) >> 4 & 1) {
        At<uint8_t>(decal, 0x110) = 1;
        char* const city = M<char*>(0x00B43CEC);
        const double half = M<double>(0x00A92D28);
        const float across = static_cast<float>(DsCityFloat(city, 0x168) * half);
        At<float>(decal, 0xE4) = static_cast<float>(DsCityFloat(city, 0x164) * half);
        At<float>(decal, 0xE8) = across;
        At<float>(decal, 0xEC) = static_cast<float>(DsCityFloat(city, 0x164));
    }
}

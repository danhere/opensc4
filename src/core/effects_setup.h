// An effect's set-up, written by hand from the game's code: the base
// effect class's constructor (0x005B2B50), its transform (0x005B3430), the
// axes its particles are drawn along (0x005B3B90), its start (0x005B3000),
// a new particle's start (0x005B7580) and a new particle put on the ground
// (0x005B71E0); an effect given its description (0x005B7C90); a
// description's inner constructor (0x005B0FE0) and its read from the
// effects file (0x005B1DC0). Included by water_effects.cpp.
//
// The x87 runs at 53 bits: intermediates are doubles, rounded to float
// where the game stores one, in the game's order. The game reads some
// constants from its data (0x00A81054, 0x00AA0D00, 0x00B0A664, the zero at
// 0x00B22D70); they are read from there too.

double SuUniform(double low, double high)
{
    return reinterpret_cast<double(__thiscall*)(void*, double, double)>(addr::SC4Random_Uniform)(
        reinterpret_cast<void*>(0x00B22DF8), low, high);
}

// A call through an object's table (slot, in bytes).
template <typename R, typename... Args>
R SuSlot(char* object, uint32_t slot, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(At<uint32_t>(At<char*>(object, 0), slot))(object, args...);
}

// The terrain's kind of the cell under (x, z), as a new particle and an
// effect's start ask it of the object at 0x00B43CEC: the cell (slot
// 0x17C), then its kind (slot 0x40 of what slot 0x130 answers).
uint8_t SuCellKind(uint32_t x, uint32_t z)
{
    char* const terrain = M<char*>(0x00B43CEC);
    uint32_t a = 0, b = 0;
    SuSlot<uint32_t>(terrain, 0x17C, x, z, &a, &b);
    char* const cells = reinterpret_cast<char*>(SuSlot<uint32_t>(terrain, 0x130));
    return SuSlot<uint8_t>(cells, 0x40, a, b);
}

// 0x005B2B50: the base class's constructor. The sub-object at +4
// (SC4EffectBase::Construct) and the batch at +0x28 (0x005EC690) are
// constructed, the class's two tables of functions set, and the fields
// given their first values: the two blocks of the transform cleared to the
// identity's (+0x8C, +0xC4), the clocks' rate (+0x120, +0x130 from the
// doubles at 0x00A80990 and 0x00A80AD0), scales of one, the box cleared and
// the limits of where particles start (+0x200) one and zero. Only the final
// value of a field the game writes twice is written; +0x88, +0x108 to
// +0x11F, +0x128, +0x138, +0x14C, +0x15C, +0x1E0, +0x1E4 and +0x21A on are
// left as they were, as the game leaves them.
uint32_t ConstructEffect(char* self)
{
    CallThis<uint32_t>(addr::SC4EffectBase_Construct, self + 4);
    At<uint32_t>(self, 0) = 0x00AA0EA0;
    At<uint32_t>(self, 4) = 0x00AA0E58;
    At<uint32_t>(self, 0x24) = 0;
    CallThis<uint32_t>(0x005EC690, self + 0x28);
    const uint32_t zero = M<uint32_t>(0x00B22D70);
    At<uint32_t>(self, 0x68) = 0;
    At<uint32_t>(self, 0x6C) = 0;
    for (uint32_t at = 0x70; at <= 0x84; at += 4)
        At<uint32_t>(self, at) = zero;
    ClearBlock(self + 0x8C);
    ClearBlock(self + 0xC4);
    At<uint32_t>(self, 0xFC) = 0;
    At<uint32_t>(self, 0x100) = 0;
    At<uint32_t>(self, 0x104) = 0;
    At<double>(self, 0x120) = M<double>(0x00A80990);
    At<double>(self, 0x130) = M<double>(0x00A80AD0);
    for (const uint32_t at : {0x13Cu, 0x140u, 0x144u, 0x150u, 0x154u, 0x158u, 0x160u, 0x164u, 0x168u, 0x16Cu,
                              0x170u, 0x174u, 0x178u, 0x17Cu, 0x180u, 0x194u, 0x198u, 0x19Cu, 0x1B8u, 0x1BCu,
                              0x1C0u})
        At<uint32_t>(self, at) = zero;
    At<uint32_t>(self, 0x148) = 0;
    At<uint32_t>(self, 0x184) = 0;
    At<uint32_t>(self, 0x188) = 1;
    At<uint32_t>(self, 0x18C) = 1;
    At<uint32_t>(self, 0x190) = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        At<float>(self, 0x1A0 + i * 4) = 1.0f;
        At<uint32_t>(self, 0x1AC + i * 4) = 0;
        At<float>(self, 0x200 + i * 4) = 1.0f;
        At<uint32_t>(self, 0x20C + i * 4) = 0;
        At<uint32_t>(self, 0x1D4 + i * 4) = M<uint32_t>(0x00B0A668 + i * 4);
    }
    for (uint32_t at = 0x1C4; at <= 0x1D0; at += 4)
        At<float>(self, at) = 1.0f;
    for (uint32_t at = 0x1E8; at <= 0x1FC; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint8_t>(self, 0x218) = 0;
    At<uint8_t>(self, 0x219) = 0;
    return reinterpret_cast<uint32_t>(self);
}

// 0x005B3430 (ret 8), slot 0xC of both classes: the two blocks (two bytes
// and thirteen floats each) kept at +0x8C and +0xC4, each one's count of
// changes raised and its second byte copied. An effect without +0x21A whose
// description has flag 0x1000 takes no place from the first: both blocks'
// places are cleared (flag 1 set, the counts raised again) and the box is
// made the limits of where particles start (+0x200 to +0x214), its centre
// the midpoint. Then the centre (+0x194) is the first block's place taken
// through the second block (its matrix by rows, its scale and place), and
// the box moves by as much as the centre did. The answer is not used.
void SetEffectTransform(char* self, char* first, char* second)
{
    ++At<uint8_t>(self, 0x8C);
    std::memcpy(self + 0x90, first + 4, 0x34);
    At<uint8_t>(self, 0x8D) = At<uint8_t>(first, 1);
    ++At<uint8_t>(self, 0xC4);
    std::memcpy(self + 0xC8, second + 4, 0x34);
    At<uint8_t>(self, 0xC5) = At<uint8_t>(second, 1);
    if (!At<uint8_t>(self, 0x21A) && (At<uint32_t>(At<char*>(self, 0x24), 0) >> 12 & 1)) {
        const uint32_t zero = M<uint32_t>(0x00B22D70);
        At<uint32_t>(self, 0xB4) = zero;
        At<uint32_t>(self, 0xB8) = zero;
        At<uint32_t>(self, 0xBC) = zero;
        At<uint8_t>(self, 0x8D) |= 1;
        ++At<uint8_t>(self, 0x8C);
        At<uint32_t>(self, 0xEC) = zero;
        At<uint32_t>(self, 0xF0) = zero;
        At<uint32_t>(self, 0xF4) = zero;
        At<uint8_t>(self, 0xC5) |= 1;
        ++At<uint8_t>(self, 0xC4);
        std::memcpy(self + 0x1AC, self + 0x20C, 12);
        std::memcpy(self + 0x1A0, self + 0x200, 12);
        const double half = M<float>(0x00A84D2C);
        const double x = static_cast<double>(At<float>(self, 0x1AC)) + At<float>(self, 0x1A0);
        const float y = static_cast<float>(static_cast<double>(At<float>(self, 0x1B0)) + At<float>(self, 0x1A4));
        const float z = static_cast<float>(static_cast<double>(At<float>(self, 0x1B4)) + At<float>(self, 0x1A8));
        At<float>(self, 0x19C) = static_cast<float>(z * half);
        At<float>(self, 0x194) = static_cast<float>(x * half);
        At<float>(self, 0x198) = static_cast<float>(y * half);
    }
    const double oldX = At<float>(self, 0x194), oldY = At<float>(self, 0x198);
    const float oldZ = At<float>(self, 0x19C);
    std::memcpy(self + 0x194, self + 0xB4, 12);
    const double px = At<float>(self, 0x194), py = At<float>(self, 0x198), pz = At<float>(self, 0x19C);
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const double x = (f(0xD0) * pz + f(0xCC) * py) + f(0xC8) * px;
    const double y = (f(0xDC) * pz + f(0xD8) * py) + px * f(0xD4);
    const double z = (f(0xE8) * pz + f(0xE4) * py) + px * f(0xE0);
    const float zf = static_cast<float>(z);
    At<float>(self, 0x194) = static_cast<float>(x);
    At<float>(self, 0x198) = static_cast<float>(y);
    At<float>(self, 0x19C) = zf;
    const double scale = f(0xF8);
    const float xs = static_cast<float>(x * scale);
    const double ys = y * scale;
    At<float>(self, 0x194) = xs;
    At<float>(self, 0x198) = static_cast<float>(ys);
    const double zs = static_cast<double>(zf) * scale;
    At<float>(self, 0x19C) = static_cast<float>(zs);
    At<float>(self, 0x194) = static_cast<float>(static_cast<double>(xs) + f(0xEC));
    At<float>(self, 0x198) = static_cast<float>(ys + f(0xF0));
    At<float>(self, 0x19C) = static_cast<float>(zs + f(0xF4));
    const float dx = static_cast<float>(f(0x194) - oldX);
    const double dy = f(0x198) - oldY;
    const double dz = f(0x19C) - oldZ;
    At<float>(self, 0x1A0) = static_cast<float>(static_cast<double>(dx) + f(0x1A0));
    At<float>(self, 0x1A4) = static_cast<float>(dy + f(0x1A4));
    At<float>(self, 0x1A8) = static_cast<float>(dz + f(0x1A8));
    At<float>(self, 0x1AC) = static_cast<float>(static_cast<double>(dx) + f(0x1AC));
    At<float>(self, 0x1B0) = static_cast<float>(dy + f(0x1B0));
    At<float>(self, 0x1B4) = static_cast<float>(dz + f(0x1B4));
}

// 0x005B3B90 (ret 4), slot 0x38 of both classes, with the render context:
// when the view's rotation (the context's +0x18) is not the one kept at
// +0x88, it is kept and the two axes the particles are drawn along (+0x70
// and +0x7C) are taken again: the camera view's (the context's +0x24, its
// +0x60 and +0x6C), or, for a description whose byte +0xD5 is 1, two
// worked out from its three floats at +0x30 (fixed axes when the first's
// length is below the limit at 0x00AA0D00).
void SetEffectAxes(char* self, char* context)
{
    const uint32_t rotation = At<uint32_t>(context, 0x18);
    if (rotation == At<uint32_t>(self, 0x88))
        return;
    At<uint32_t>(self, 0x88) = rotation;
    char* const description = At<char*>(self, 0x24);
    if (At<uint8_t>(description, 0xD5) != 1) {
        char* const view = At<char*>(context, 0x24);
        std::memcpy(self + 0x70, view + 0x60, 12);
        std::memcpy(self + 0x7C, view + 0x6C, 12);
        return;
    }
    const double k = M<float>(0x00A81054);
    const double a = At<float>(description, 0x30), b = At<float>(description, 0x34), c = At<float>(description, 0x38);
    const double bk = b * k;
    const float t4 = static_cast<float>(bk - c);
    const float t8 = static_cast<float>(c * k - a * k);
    const double u = a - bk;
    const double length = std::sqrt((u * u + static_cast<double>(t8) * t8) + static_cast<double>(t4) * t4);
    if (std::fabs(length) < M<float>(0x00AA0D00)) {
        At<uint32_t>(self, 0x70) = 0;
        At<uint32_t>(self, 0x74) = 0;
        At<float>(self, 0x78) = 1.0f;
        At<float>(self, 0x7C) = 1.0f;
        At<uint32_t>(self, 0x80) = 0;
        At<uint32_t>(self, 0x84) = 0;
        return;
    }
    const double r = M<float>(0x00B0A664) / length;
    At<float>(self, 0x78) = static_cast<float>(u * r);
    At<float>(self, 0x70) = static_cast<float>(t4 * r);
    At<float>(self, 0x74) = static_cast<float>(t8 * r);
    const double x = At<float>(self, 0x70), y = At<float>(self, 0x74), z = At<float>(self, 0x78);
    const double yk = y * k;
    At<float>(self, 0x84) = static_cast<float>(yk - x);
    At<float>(self, 0x7C) = static_cast<float>(z - yk);
    At<float>(self, 0x80) = static_cast<float>(x * k - z * k);
}

// 0x005B3000 (ret 4), slot 0 of both classes, with one word: nothing when
// the word is set and the description has a count at +0x18 without flag 8,
// or when the sub-object cannot be set going again (0x005B9CF0). Else the
// clocks (+0x110, +0x120) and +0x128, +0x138 set back and the pull (+0x13C)
// the description's (+0xE0). By the description's flags: 0x800000 a random
// drift (+0x150, its middle cleared) and, with 0x1000000, a random speed
// along it and a random +0x148, else +0x148 cleared and the wander run
// once (0x005B2730); without it the drift cleared. 0x2000000: the side of
// the cell under the effect (the terrain's kind) as a direction (+0x160,
// +0x15C whether there is one), taken through the second block and over
// its scale. +0x190 a random delay by the description's +0x20 or +0x28;
// +0x188 the word, +0x18C cleared, +0x118 whether the word was set.
void BeginEffect(char* self, uint32_t again)
{
    char* description = At<char*>(self, 0x24);
    if (again && At<uint32_t>(description, 0x18) && !(At<uint32_t>(description, 0) >> 3 & 1))
        return;
    if (!(CallThis<uint32_t>(addr::SC4EffectBase_Restart, self + 4) & 0xFF))
        return;
    At<double>(self, 0x120) = M<double>(0x00A80990);
    At<uint32_t>(self, 0x128) = 0;
    At<uint32_t>(self, 0x138) = 0;
    At<double>(self, 0x110) = M<double>(0x00A80990);
    std::memcpy(self + 0x13C, description + 0xE0, 12);
    description = At<char*>(self, 0x24);
    if (At<uint32_t>(description, 0) >> 23 & 1) {
        const float x = static_cast<float>(SuUniform(At<float>(description, 0x30), At<float>(description, 0x3C)));
        const float y = static_cast<float>(SuUniform(At<float>(description, 0x34), At<float>(description, 0x40)));
        At<float>(self, 0x158) =
            static_cast<float>(SuUniform(At<float>(description, 0x38), At<float>(description, 0x44)));
        At<float>(self, 0x150) = x;
        At<float>(self, 0x154) = y;
        At<uint32_t>(self, 0x154) = 0;
        description = At<char*>(self, 0x24);
        if (At<uint8_t>(description, 3) & 1) {
            const double speed = SuUniform(At<float>(description, 0x17C), At<float>(description, 0x180));
            const double a = At<float>(self, 0x150), b = At<float>(self, 0x154), c = At<float>(self, 0x158);
            const double squared = (a * a + b * b) + c * c;
            const double k = M<float>(0x00A81054);
            if (squared > k) {
                const double q = speed / std::sqrt(squared);
                At<float>(self, 0x150) = static_cast<float>(q * At<float>(self, 0x150));
                At<float>(self, 0x154) = static_cast<float>(q * At<float>(self, 0x154));
                At<float>(self, 0x158) = static_cast<float>(q * At<float>(self, 0x158));
            }
            description = At<char*>(self, 0x24);
            At<float>(self, 0x148) =
                static_cast<float>(SuUniform(At<float>(description, 0x174), At<float>(description, 0x178)));
        } else {
            At<uint32_t>(self, 0x148) = 0;
            CallThis<uint32_t>(0x005B2730, self, 0u);
        }
    } else {
        At<uint32_t>(self, 0x150) = 0;
        At<uint32_t>(self, 0x154) = 0;
        At<uint32_t>(self, 0x158) = 0;
    }
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 25 & 1) {
        At<uint8_t>(self, 0x15C) = 1;
        const uint8_t kind = SuCellKind(At<uint32_t>(self, 0x194), At<uint32_t>(self, 0x19C));
        float direction[3] = {0.0f, 0.0f, 0.0f};
        if (kind & 1)
            direction[2] = -1.0f;
        else if (kind & 2)
            direction[0] = -1.0f;
        else if (kind & 8)
            direction[2] = 1.0f;
        else if (kind & 4)
            direction[0] = 1.0f;
        else
            At<uint8_t>(self, 0x15C) = 0;
        if (At<uint8_t>(self, 0x15C)) {
            std::memcpy(self + 0x160, direction, 12);
            const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
            const double x = (f(0xD4) * f(0x164) + f(0xE0) * f(0x168)) + f(0x160) * f(0xC8);
            const double y = (f(0xD8) * f(0x164) + f(0xCC) * f(0x160)) + f(0xE4) * f(0x168);
            const double z = (f(0xDC) * f(0x164) + f(0xD0) * f(0x160)) + f(0xE8) * f(0x168);
            const float zf = static_cast<float>(z);
            At<float>(self, 0x160) = static_cast<float>(x);
            At<float>(self, 0x164) = static_cast<float>(y);
            At<float>(self, 0x168) = zf;
            const float inverse = static_cast<float>(static_cast<double>(M<float>(0x00B0A664)) / f(0xF8));
            At<float>(self, 0x160) = static_cast<float>(x * inverse);
            At<float>(self, 0x164) = static_cast<float>(y * inverse);
            At<float>(self, 0x168) = static_cast<float>(static_cast<double>(zf) * inverse);
        }
    }
    description = At<char*>(self, 0x24);
    const double none = M<double>(0x00A80990);
    if (At<float>(description, 0x20) >= none)
        At<float>(self, 0x190) =
            static_cast<float>(SuUniform(At<float>(description, 0x20), At<float>(description, 0x24)));
    else if (At<float>(description, 0x28) >= none)
        At<float>(self, 0x190) =
            static_cast<float>(SuUniform(At<float>(description, 0x28), At<float>(description, 0x2C)));
    else
        At<uint32_t>(self, 0x190) = 0;
    At<uint32_t>(self, 0x188) = again;
    At<uint32_t>(self, 0x18C) = 0;
    At<uint8_t>(self, 0x118) = again != 0;
}

// 0x005B71E0 (ret 4): a new particle put on the ground. Its position
// (+0xC) is taken to the terrain's grid through the second block (by
// columns, its scale and place); the particle is ended (its first byte
// cleared, the answer false) when that is below zero or past the map
// (+0x1F4, +0x1F8), when the height the terrain answers there (the object
// at +0x1F0, slot 0x34) less the effect's own (+0x1FC) is outside the
// description's +0x158 and +0x15C (flag 0x10000), or when the cell is of
// the wrong kind (flags of the mask at 0x00B22E1C; with flag 0x20000 any
// of its low four bits, else not 0x80). Else, for flag 0x4000 the
// particle's height is that height (with flag 0x8000 not below the
// effect's own), for 0x8000 alone the effect's own, plus a random amount
// between +0x204 and +0x210, taken back through the block; the answer is
// true.
uint32_t PlaceParticleOnGround(char* self, char* particle)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const double a = At<float>(particle, 0xC), b = At<float>(particle, 0x10), c = At<float>(particle, 0x14);
    const float x0 = static_cast<float>((c * f(0xD0) + b * f(0xCC)) + a * f(0xC8));
    const double z0 = (c * f(0xE8) + b * f(0xE4)) + a * f(0xE0);
    const double scale = f(0xF8);
    const float xs = static_cast<float>(static_cast<double>(x0) * scale);
    const float gx = static_cast<float>(static_cast<double>(xs) + f(0xEC));
    const float gz = static_cast<float>(z0 * scale + f(0xF4));
    const double low = M<float>(0x00A81054);
    const auto end = [&] {
        At<uint8_t>(particle, 0) = 0;
        return 0u;
    };
    if (!(gx >= low) || !(gz >= low) || !(gx < At<float>(self, 0x1F4)) || !(gz < At<float>(self, 0x1F8)))
        return end();
    uint32_t gxBits = 0, gzBits = 0;
    std::memcpy(&gxBits, &gx, 4);
    std::memcpy(&gzBits, &gz, 4);
    const float height = static_cast<float>(SuSlot<double>(At<char*>(self, 0x1F0), 0x34, gxBits, gzBits));
    char* const description = At<char*>(self, 0x24);
    const uint32_t flags = At<uint32_t>(description, 0);
    if (flags >> 16 & 1) {
        const double above = static_cast<double>(height) - f(0x1FC);
        if (above < At<float>(description, 0x158) || above > At<float>(description, 0x15C))
            return end();
    }
    if (flags & M<uint32_t>(0x00B22E1C)) {
        const uint8_t kind = SuCellKind(gxBits, gzBits);
        const bool wrong = (At<uint32_t>(At<char*>(self, 0x24), 0) >> 17 & 1) ? (kind & 0xF) == 0 : kind != 0x80;
        if (wrong)
            return end();
    }
    const uint32_t now = At<uint32_t>(At<char*>(self, 0x24), 0);
    float base = 0.0f;
    if (now >> 14 & 1) {
        base = height;
        if ((now >> 15 & 1) && height < At<float>(self, 0x1FC))
            base = At<float>(self, 0x1FC);
    } else if (now >> 15 & 1) {
        base = At<float>(self, 0x1FC);
    } else {
        return 1;
    }
    const double y = SuUniform(At<float>(self, 0x204), At<float>(self, 0x210)) + base;
    const double lx = static_cast<double>(gx) - f(0xEC);
    const float ly0 = static_cast<float>(y - f(0xF0));
    const float lz0 = static_cast<float>(static_cast<double>(gz) - f(0xF4));
    const double inverse = static_cast<double>(M<float>(0x00B0A664)) / f(0xF8);
    const float inverseF = static_cast<float>(inverse);
    const double x = lx * inverse;
    const float ly = static_cast<float>(static_cast<double>(ly0) * inverseF);
    const double lz = static_cast<double>(lz0) * inverseF;
    const float lzF = static_cast<float>(lz);
    const double nx = (lz * f(0xE0) + ly * f(0xD4)) + x * f(0xC8);
    const double ny = (lzF * f(0xE4) + ly * f(0xD8)) + x * f(0xCC);
    const double nz = (lzF * f(0xE8) + ly * f(0xDC)) + x * f(0xD0);
    At<float>(particle, 0xC) = static_cast<float>(nx);
    At<float>(particle, 0x14) = static_cast<float>(nz);
    At<float>(particle, 0x10) = static_cast<float>(ny);
    return 1;
}

// The angle a particle of a description whose byte +0xD5 is 1 is turned
// by: atan2 of the first block's -(+0xA8) and +0x90 (FPATAN gives a 64-bit
// result whatever the precision control), times the float at 0x00B0A660,
// added to the particle's +0x30; the x87's own instructions in the game's
// order.
void SuTurnParticle(char* self, char* particle)
{
    float* const turn = reinterpret_cast<float*>(particle + 0x30);
    const float* const scale = reinterpret_cast<const float*>(0x00B0A660);
    __asm {
        mov eax, self
        fld dword ptr [eax + 0xA8]
        fchs
        fld dword ptr [eax + 0x90]
        fpatan
        mov ecx, scale
        fmul dword ptr [ecx]
        mov edx, turn
        fadd dword ptr [edx]
        fstp dword ptr [edx]
    }
}

// 0x005B7580 (ret 4): a particle just taken from the pool given its first
// values. Marked as running, age 0 and a random life (the description's
// +0xC to +0x10); its position random within the limits of where
// particles start (+0x200 to +0x214); its velocity random within the
// description's +0x30 to +0x44 and made as long as a random speed (+0x48,
// +0x4C) times +0x1CC; for a description with a pull (+0xF0) the velocity
// gets that much towards the pull's centre (+0xF4); the position and the
// velocity taken through the first block (its matrix by rows, the
// position also its scale and place), the effect's drift (+0xFC) added.
// For flags of the mask at 0x00B22E18 the particle is put on the ground
// (0x005B71E0), and if that ended it nothing more is done. For flag 0x20
// the position moves by half the velocity times a list's first float
// (+0xAC), the velocity's length across the axes (+0x70, +0x7C) over the
// description's +0xDC when that is not zero, over the speed. Then its
// size, spin and the rest (+0x28 to +0x40) are random around one by the
// description's spreads, the size by +0x1E4 and +0x1C8 (by the effect's
// +0x1E8 list's second float without flag 1 of +8; by the block's scale
// for flag 0x400000), +0x30 random by +0x70 or one, turned for byte +0xD5.
void StartParticle(char* self, char* particle)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const auto p = [&](uint32_t at) -> float& { return At<float>(particle, at); };
    At<uint8_t>(particle, 0) = 1;
    char* description = At<char*>(self, 0x24);
    p(8) = static_cast<float>(SuUniform(At<float>(description, 0xC), At<float>(description, 0x10)));
    At<uint32_t>(particle, 4) = 0;
    const float x = static_cast<float>(SuUniform(f(0x200), f(0x20C)));
    const float y = static_cast<float>(SuUniform(f(0x204), f(0x210)));
    p(0x14) = static_cast<float>(SuUniform(f(0x208), f(0x214)));
    p(0x10) = y;
    p(0xC) = x;
    description = At<char*>(self, 0x24);
    const float vx = static_cast<float>(SuUniform(At<float>(description, 0x30), At<float>(description, 0x3C)));
    const float vy = static_cast<float>(SuUniform(At<float>(description, 0x34), At<float>(description, 0x40)));
    p(0x24) = static_cast<float>(SuUniform(At<float>(description, 0x38), At<float>(description, 0x44)));
    p(0x20) = vy;
    p(0x1C) = vx;
    description = At<char*>(self, 0x24);
    const float speed =
        static_cast<float>(SuUniform(At<float>(description, 0x48), At<float>(description, 0x4C)) * f(0x1CC));
    const double k = M<float>(0x00A81054);
    {
        const double a = p(0x1C), b = p(0x20), c = p(0x24);
        const double squared = (a * a + b * b) + c * c;
        if (squared > k) {
            const double q = speed / std::sqrt(squared);
            p(0x1C) = static_cast<float>(q * p(0x1C));
            p(0x20) = static_cast<float>(q * p(0x20));
            p(0x24) = static_cast<float>(q * p(0x24));
        }
    }
    description = At<char*>(self, 0x24);
    if (At<float>(description, 0xF0) > k) {
        const float dx = static_cast<float>(static_cast<double>(p(0xC)) - At<float>(description, 0xF4));
        const float dy = static_cast<float>(static_cast<double>(p(0x10)) - At<float>(description, 0xF8));
        const float dz = static_cast<float>(static_cast<double>(p(0x14)) - At<float>(description, 0xFC));
        // The sum is checked against the zero as it is, against the limit as a float.
        const double sum = (static_cast<double>(dx) * dx + static_cast<double>(dz) * dz) + static_cast<double>(dy) * dy;
        const float squared = static_cast<float>(sum);
        if (!(sum < k) && !(std::fabs(static_cast<double>(squared)) < M<float>(0x00AA0D00))) {
            const double r = At<float>(description, 0xF0) / std::sqrt(static_cast<double>(squared));
            const double ax = dx * r;
            const float ay = static_cast<float>(dy * r);
            const float az = static_cast<float>(dz * r);
            p(0x1C) = static_cast<float>(ax + p(0x1C));
            p(0x20) = static_cast<float>(static_cast<double>(ay) + p(0x20));
            p(0x24) = static_cast<float>(static_cast<double>(az) + p(0x24));
        }
    }
    {
        const double px = p(0xC), py = p(0x10), pz = p(0x14);
        const double nx = (f(0x98) * pz + f(0x94) * py) + px * f(0x90);
        const double ny = (f(0xA4) * pz + f(0xA0) * py) + f(0x9C) * px;
        const double nz = (f(0xB0) * pz + f(0xAC) * py) + f(0xA8) * px;
        const float nzF = static_cast<float>(nz);
        p(0xC) = static_cast<float>(nx);
        p(0x10) = static_cast<float>(ny);
        p(0x14) = nzF;
        const double scale = f(0xC0);
        const float xs = static_cast<float>(nx * scale);
        const double ys = ny * scale;
        p(0xC) = xs;
        p(0x10) = static_cast<float>(ys);
        const double zs = static_cast<double>(nzF) * scale;
        p(0x14) = static_cast<float>(zs);
        p(0xC) = static_cast<float>(static_cast<double>(xs) + f(0xB4));
        p(0x10) = static_cast<float>(ys + f(0xB8));
        p(0x14) = static_cast<float>(zs + f(0xBC));
    }
    {
        const double vx0 = p(0x1C), vy0 = p(0x20), vz0 = p(0x24);
        const double nx = (f(0x98) * vz0 + f(0x94) * vy0) + f(0x90) * vx0;
        const double ny = (f(0xA4) * vz0 + f(0xA0) * vy0) + vx0 * f(0x9C);
        const double nz = (f(0xB0) * vz0 + f(0xAC) * vy0) + vx0 * f(0xA8);
        p(0x24) = static_cast<float>(nz);
        p(0x1C) = static_cast<float>(nx);
        p(0x20) = static_cast<float>(ny);
        p(0x1C) = static_cast<float>(f(0xFC) + p(0x1C));
        p(0x20) = static_cast<float>(f(0x100) + p(0x20));
        p(0x24) = static_cast<float>(f(0x104) + p(0x24));
    }
    if ((At<uint32_t>(At<char*>(self, 0x24), 0) & M<uint32_t>(0x00B22E18)) &&
        !(CallThis<uint32_t>(addr::SC4Effect_PlaceOnGround, self, particle) & 0xFF))
        return;
    description = At<char*>(self, 0x24);
    const double one = M<float>(0x00A81228);
    if (At<uint32_t>(description, 0) >> 5 & 1) {
        double m = one;
        if (!(At<float>(description, 0xDC) == k)) {
            const double vx0 = p(0x1C), vy0 = p(0x20), vz0 = p(0x24);
            const double a = (vz0 * f(0x84) + vy0 * f(0x80)) + f(0x7C) * vx0;
            const double b = (vz0 * f(0x78) + vy0 * f(0x74)) + vx0 * f(0x70);
            m = std::sqrt(b * b + a * a) / At<float>(description, 0xDC);
        }
        const double half = M<float>(0x00A84D2C);
        const float* const list = At<float*>(description, 0xAC);
        const double hx = p(0x1C) * half, hy = p(0x20) * half;
        const float hz = static_cast<float>(p(0x24) * half);
        const float mx = static_cast<float>(hx * m);
        const float my = static_cast<float>(hy * m);
        const double mz = m * hz;
        const double w = list[0];
        const float wx = static_cast<float>(mx * w);
        const float wy = static_cast<float>(my * w);
        const double wz = mz * w;
        const double inverse = static_cast<double>(M<float>(0x00B0A664)) / speed;
        const float ix = static_cast<float>(wx * inverse);
        const float iy = static_cast<float>(wy * inverse);
        const double iz = wz * inverse;
        p(0xC) = static_cast<float>(static_cast<double>(ix) + p(0xC));
        p(0x10) = static_cast<float>(static_cast<double>(iy) + p(0x10));
        p(0x14) = static_cast<float>(iz + p(0x14));
    }
    At<uint8_t>(particle, 0x18) = 0;
    const auto around = [&](uint32_t at) {
        const double spread = At<float>(At<char*>(self, 0x24), at);
        return SuUniform(one - spread, one + spread);
    };
    p(0x28) = static_cast<float>(around(0x68) * f(0x1E4) * f(0x1C8));
    p(0x2C) = static_cast<float>(around(0x6C));
    p(0x34) = static_cast<float>(around(0x78));
    p(0x38) = static_cast<float>(around(0x7C));
    p(0x3C) = static_cast<float>(around(0x80));
    p(0x40) = static_cast<float>(around(0x84));
    description = At<char*>(self, 0x24);
    if (At<float>(description, 0x70) > k) {
        const double spin = At<float>(description, 0x70);
        p(0x30) = static_cast<float>(SuUniform(-spin, spin));
    } else {
        p(0x30) = 1.0f;
    }
    if (!(At<uint8_t>(At<char*>(self, 0x24), 8) & 1))
        p(0x28) = static_cast<float>(static_cast<double>(At<float>(At<char*>(self, 0x1E8), 4)) * p(0x28));
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 22 & 1)
        p(0x28) = static_cast<float>(f(0xC0) * p(0x28));
    if (At<uint8_t>(At<char*>(self, 0x24), 0xD5) == 1)
        SuTurnParticle(self, particle);
}

// One value put at the end of a vector (begin, end, room) of 4-byte or
// 12-byte items, as the description's constructor does it: in place with
// room, else through the game's growing insert (0x00676CC0 for 4 bytes,
// 0x0050C3B0 for 12; where, the value, an unused tag, one, at the end).
void SuPushBack(char* vector, const void* value, uint32_t size)
{
    char* const end = At<char*>(vector, 4);
    if (end != At<char*>(vector, 8)) {
        if (end)
            std::memcpy(end, value, size);
        At<char*>(vector, 4) = end + size;
        return;
    }
    uint8_t tag = 0;
    CallThis<uint32_t>(size == 4 ? 0x00676CC0u : 0x0050C3B0u, vector, end, value, &tag, 1u, 1u);
}

// 0x005B0FE0: a description's fields given their first values (its whole
// constructor, 0x005B1660, calls it first). Flags and counts cleared, the
// times and rates the game's (2, 1, -1 and so on), the floats at +0x50 to
// +0x64 the value at 0x00B0A5D0 (the first three negated), those that are
// the word at 0x00B22D54, the lists empty, an empty string (eight bytes of
// the game's memory, 0x0090CF54) at +0x160, and one value put in each of
// seven lists: 30 at +0x88, the three floats at 0x00B0A5D4 at +0x94, one
// at +0xA0, +0xAC and +0xB8, zero at +0xC4 and 400 at +0x1F4. +0x1C8 and
// the count of holders (+0x204) are left as they were.
uint32_t ConstructDescription(char* self)
{
    const auto set = [&](uint32_t at, uint32_t value) { At<uint32_t>(self, at) = value; };
    constexpr uint32_t kOne = 0x3F800000, kTwo = 0x40000000, kMinusOne = 0xBF800000;
    for (uint32_t at = 0; at < 0x50; at += 4)
        set(at, 0);
    set(0xC, kTwo);
    set(0x10, kTwo);
    set(0x14, kOne);
    for (uint32_t at = 0x20; at <= 0x2C; at += 4)
        set(at, kMinusOne);
    set(0x34, kOne);
    set(0x40, kOne);
    const float limit = M<float>(0x00B0A5D0);
    const uint32_t word = M<uint32_t>(0x00B0A5D0);
    At<float>(self, 0x50) = -limit;
    At<float>(self, 0x54) = -limit;
    At<float>(self, 0x58) = -limit;
    set(0x5C, word);
    set(0x60, word);
    set(0x64, word);
    const uint32_t fill = M<uint32_t>(0x00B22D54);
    for (uint32_t at = 0x68; at <= 0x78; at += 4)
        set(at, 0);
    for (const uint32_t at : {0x7Cu, 0x80u, 0x84u, 0xE0u, 0xE4u, 0xE8u, 0xF4u, 0xF8u, 0xFCu, 0x18Cu, 0x190u, 0x194u})
        set(at, fill);
    for (uint32_t at = 0x88; at <= 0xD0; at += 4)
        set(at, 0);
    At<uint8_t>(self, 0xD4) = 0;
    At<uint8_t>(self, 0xD5) = 0;
    set(0xD8, 0);
    set(0xDC, 0);
    set(0xEC, 0);
    set(0xF0, 0);
    for (uint32_t at = 0x100; at <= 0x148; at += 4)
        set(at, 0);
    set(0x118, kOne);
    set(0x124, kOne);
    set(0x14C, 0xCE6E6B28);
    set(0x150, 0);
    set(0x154, kOne);
    set(0x158, 0xC61C4000);
    set(0x15C, 0x461C4000);
    char* const text = StlAllocate(8);
    At<char*>(self, 0x160) = text;
    At<char*>(self, 0x164) = text;
    At<char*>(self, 0x168) = text + 8;
    text[0] = 0;
    At<uint16_t>(self, 0x16C) = 0;
    At<uint16_t>(self, 0x16E) = 1;
    set(0x170, kOne);
    set(0x174, 0x40A00000);
    set(0x178, 0x40A00000);
    set(0x17C, 0x42480000);
    set(0x180, 0x42480000);
    set(0x184, 0x3DCCCCCD);
    set(0x188, 0x3E4CCCCD);
    for (uint32_t at = 0x198; at <= 0x1C4; at += 4)
        set(at, 0);
    set(0x1B0, kOne);
    for (uint32_t at = 0x1CC; at <= 0x1FC; at += 4)
        set(at, 0);
    set(0x1F0, 0x40800000);
    set(0x200, 0x40800000);
    const uint32_t thirty = 0x41F00000, one = kOne, zero = 0, fourHundred = 0x43C80000;
    SuPushBack(self + 0x88, &thirty, 4);
    SuPushBack(self + 0x94, reinterpret_cast<const void*>(0x00B0A5D4), 12);
    SuPushBack(self + 0xA0, &one, 4);
    SuPushBack(self + 0xAC, &one, 4);
    SuPushBack(self + 0xB8, &one, 4);
    SuPushBack(self + 0xC4, &zero, 4);
    SuPushBack(self + 0x1F4, &fourHundred, 4);
    return reinterpret_cast<uint32_t>(self);
}

// 0x005B1DC0 (cdecl, (stream, description)): a description read from the
// effects file, its numbers one after another through the stream's slots
// (0x14 a byte, 0x1C 16 bits, 0x24 32 bits, 0x30 a float) and its parts
// through readers of the game's own, each (stream, place): 0x005DA4F0,
// 0x005DA550 and 0x005DA5B0 a few floats in place, 0x005DB4B0 a list of
// floats, 0x005A4AD0 one of points, 0x005DB350 one of IDs, 0x005811F0 the
// string, and the description's three lists (ReadListA, ReadListB,
// ReadListC). The first three words go through a word of the stack; the
// second keeps its low byte, the third its low eleven bits. The answer is
// the last read's.
uint32_t ReadDescription(uint32_t stream, char* self)
{
    const auto read = [&](uint32_t slot, uint32_t at) {
        return StreamRead(stream, slot, reinterpret_cast<uint32_t>(self + at));
    };
    const auto part = [&](uint32_t reader, uint32_t at) {
        StreamHelper(reader, stream, reinterpret_cast<uint32_t>(self + at));
    };
    constexpr uint32_t kTwo = 0x005DA4F0, kThree = 0x005DA550, kSix = 0x005DA5B0, kFloats = 0x005DB4B0,
                       kPoints = 0x005A4AD0, kIds = 0x005DB350, kString = 0x005811F0;
    uint32_t word = 0;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&word));
    At<uint32_t>(self, 0) = word;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&word));
    At<uint32_t>(self, 4) = word & 0xFF;
    StreamRead(stream, 0x24, reinterpret_cast<uint32_t>(&word));
    At<uint32_t>(self, 8) = word & 0x7FF;
    part(kTwo, 0xC);
    read(0x30, 0x14);
    read(0x24, 0x18);
    read(0x30, 0x1C);
    part(kTwo, 0x20);
    part(kTwo, 0x28);
    part(kSix, 0x30);
    part(kTwo, 0x48);
    part(kSix, 0x50);
    for (uint32_t at = 0x68; at <= 0x78; at += 4)
        read(0x30, at);
    part(kThree, 0x7C);
    part(kFloats, 0x88);
    part(kPoints, 0x94);
    for (uint32_t at = 0xA0; at <= 0xC4; at += 0xC)
        part(kFloats, at);
    read(0x24, 0xD0);
    read(0x14, 0xD4);
    read(0x14, 0xD5);
    read(0x30, 0xD8);
    read(0x30, 0xDC);
    part(kThree, 0xE0);
    read(0x30, 0xEC);
    read(0x30, 0xF0);
    part(kThree, 0xF4);
    read(0x30, 0x100);
    read(0x30, 0x104);
    part(addr::SC4EffectDescription_ReadListA, 0x108);
    read(0x30, 0x114);
    part(kTwo, 0x118);
    part(kThree, 0x120);
    part(kFloats, 0x12C);
    for (uint32_t at = 0x138; at <= 0x154; at += 4)
        read(0x30, at);
    part(kTwo, 0x158);
    part(kString, 0x160);
    read(0x1C, 0x16C);
    read(0x1C, 0x16E);
    read(0x30, 0x170);
    part(kTwo, 0x174);
    part(kTwo, 0x17C);
    read(0x30, 0x184);
    read(0x30, 0x188);
    part(kThree, 0x18C);
    for (uint32_t at = 0x198; at <= 0x1A0; at += 4)
        read(0x30, at);
    part(kFloats, 0x1A4);
    read(0x30, 0x1B0);
    read(0x24, 0x1B4);
    part(addr::SC4EffectDescription_ReadListB, 0x1B8);
    read(0x30, 0x1C4);
    part(addr::SC4EffectDescription_ReadListC, 0x1CC);
    read(0x30, 0x1D8);
    read(0x30, 0x1DC);
    part(kIds, 0x1E0);
    read(0x30, 0x1EC);
    read(0x30, 0x1F0);
    part(kFloats, 0x1F4);
    return read(0x30, 0x200);
}

// Whether the effects manager (0x00B43D1C, slot 0x14) has a description
// named as this one's (+0x160) with a suffix ("%s_terrain" at 0x00AA0F10,
// "%s_water" at 0x00AA0F04): the name made in a string of the game's own
// on the stack (its table 0x00A80810, eight bytes of the game's memory,
// the formatter at 0x00A8087C), its text (slot 0x14) asked about, the
// string's memory freed.
uint8_t SuHasNamed(char* manager, char* description, uint32_t format)
{
    uint32_t text[5] = {0x00A80810, 0, 0, 0, 0};
    char* const storage = StlAllocate(8);
    text[1] = reinterpret_cast<uint32_t>(storage);
    text[2] = reinterpret_cast<uint32_t>(storage);
    text[3] = reinterpret_cast<uint32_t>(storage + 8);
    storage[0] = 0;
    char* const made = reinterpret_cast<char*(__cdecl*)(void*, uint32_t, uint32_t)>(M<uint32_t>(0x00A8087C))(
        text, format, At<uint32_t>(description, 0x160));
    const uint32_t name = SuSlot<uint32_t>(made, 0x14);
    const uint8_t found = SuSlot<uint8_t>(manager, 0x14, name);
    if (text[1])
        reinterpret_cast<void(__cdecl*)(uint32_t)>(addr::SC4_Free)(text[1]);
    return found;
}

// 0x005B7C90 (ret 4), slot 0x28 of the base class: an effect given its
// description. A new one is held (0x005AF940) and the old one let go
// (0x005B1C80); then, new or not, +0x21A cleared, the render state set by
// the description's kind (+0xD4, 0x005B2260), its texture loaded (+0xD0,
// 0x005B74C0), the batch at +0x28 set up (slot 0x40), and the values that
// come from the description: the clock's rate (+0x130, one over the
// description's +0x14 or a small floor), four scales of one, the box
// cleared, the limits of where particles start (+0x200 to +0x214, for
// flag 0x1000 with the terrain's height added (its slot 0x74) unless flag
// 0x4000 or 0x8000, then by 0x005B7030 for flag 0x2000 or the map's size),
// whether the description has a pull (+0x15C: any of its +0x18C to +0x194
// away from the zero, kept at +0x160), and for flag 0x100 whether the
// manager has effects named with "_terrain" and "_water" (+0x218, +0x219).
void SetEffectDescription(char* self, char* description)
{
    char* const old = At<char*>(self, 0x24);
    if (description != old) {
        if (description)
            VrNotedInto(StMake(addr::SC4Effect_Hold, description), {},
                        [&] { return CallThis<uint32_t>(addr::SC4Effect_Hold, description); });
        At<char*>(self, 0x24) = description;
        if (old)
            VrNotedInto(StMake(addr::SC4EffectDescription_Release, old), {},
                        [&] { return CallThis<uint32_t>(addr::SC4EffectDescription_Release, old); });
    }
    char* d = At<char*>(self, 0x24);
    At<uint8_t>(self, 0x21A) = 0;
    CallThis<uint32_t>(addr::SC4Effect_SetKindState, self, static_cast<uint32_t>(At<uint8_t>(d, 0xD4)));
    const uint32_t texture = At<uint32_t>(At<char*>(self, 0x24), 0xD0);
    VrNotedInto(StMake(addr::SC4Effect_LoadTexture, self, Ptr(texture)), {{self + 0x68, 8}},
                [&] { return CallThis<uint32_t>(addr::SC4Effect_LoadTexture, self, texture); });
    VrNotedInto(StMake(addr::SC4Effect_SortBatch, self, self + 0x28), {{self + 0x28, 0x40}},
                [&] { return SuSlot<uint32_t>(self, 0x40, self + 0x28); });
    d = At<char*>(self, 0x24);
    float rate = 0.0f;
    const uint32_t floor = 0x358637BD;
    std::memcpy(&rate, &floor, 4);
    if (M<float>(0x00A86510) < At<float>(d, 0x14))
        rate = At<float>(d, 0x14);
    At<uint32_t>(self, 0x88) = 0xFFFFFFFF;
    At<float>(self, 0x1CC) = 1.0f;
    At<float>(self, 0x1D0) = 1.0f;
    At<float>(self, 0x1E0) = 1.0f;
    At<float>(self, 0x1E4) = 1.0f;
    At<double>(self, 0x130) = M<double>(0x00A80AB0) / rate;
    for (uint32_t i = 0; i < 3; ++i)
        At<uint32_t>(self, 0x1D4 + i * 4) = M<uint32_t>(0x00B0A668 + i * 4);
    for (uint32_t at = 0x194; at <= 0x1B4; at += 4)
        At<uint32_t>(self, at) = 0;
    std::memcpy(self + 0x20C, d + 0x5C, 12);
    std::memcpy(self + 0x200, d + 0x50, 12);
    At<uint32_t>(self, 0xFC) = 0;
    At<uint32_t>(self, 0x100) = 0;
    At<uint32_t>(self, 0x104) = 0;
    const uint32_t flags = At<uint32_t>(At<char*>(self, 0x24), 0);
    if (flags >> 12 & 1) {
        if (!(flags >> 14 & 1) && !(flags >> 15 & 1)) {
            const double height = SuSlot<double>(At<char*>(self, 0x1F0), 0x74);
            At<float>(self, 0x204) = static_cast<float>(height + At<float>(self, 0x204));
            At<float>(self, 0x210) = static_cast<float>(height + At<float>(self, 0x210));
        }
        if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 13 & 1) {
            CallThis<uint32_t>(0x005B7030, self, 0u);
        } else {
            At<uint32_t>(self, 0x200) = 0;
            At<uint32_t>(self, 0x20C) = At<uint32_t>(self, 0x1F4);
            At<uint32_t>(self, 0x208) = 0;
            At<uint32_t>(self, 0x214) = At<uint32_t>(self, 0x1F8);
        }
    }
    d = At<char*>(self, 0x24);
    const double zero = M<float>(0x00B22D70);
    const double limit = M<float>(0x00AA0D00);
    const bool pull = !(std::fabs(At<float>(d, 0x18C) - zero) < limit) ||
                      !(std::fabs(At<float>(d, 0x190) - zero) < limit) ||
                      !(std::fabs(At<float>(d, 0x194) - zero) < limit);
    At<uint8_t>(self, 0x15C) = pull;
    if (pull)
        std::memcpy(self + 0x160, d + 0x18C, 12);
    char* const manager = M<char*>(0x00B43D1C);
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 8 & 1) {
        At<uint8_t>(self, 0x218) = SuHasNamed(manager, At<char*>(self, 0x24), 0x00AA0F10);
        At<uint8_t>(self, 0x219) = SuHasNamed(manager, At<char*>(self, 0x24), 0x00AA0F04);
    }
}

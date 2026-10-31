// The second effect class (vtable 0x00AA0C58), written by hand from the
// game's code: its take of a particle (0x005AEF20), its give back
// (0x005AF390) and its step when it is not drawn (0x005AE6E0). Included by
// water_effects.cpp after effects_emit.h, whose conventions it keeps: the
// x87 runs at 53 bits, intermediates are doubles, rounded to float where
// the game stores one.
//
// A particle of this class is 0x54 bytes, from a pool of its own
// (0x00B0A5C0, free list 0x00B0A5CC), and may carry an effect of its own
// (+0x44) that the effects manager (0x00B43D1C, slot 0x1C) made for it.
// What these functions ask of that effect, of the manager and of the
// object at the effect's +0x278 goes through BOurs (so that the
// comparison's replay answers it from the game's notes); the game's
// helpers they call (the start of a particle, the string's join and free,
// the block) are called as they are.

constexpr std::uintptr_t kParticleFreeB = 0x00B0A5CC, kParticlePoolB = 0x00B0A5C0, kEffectsManager = 0x00B43D1C;
constexpr std::size_t kParticleBytesB = 0x54;

// What the notes call the calls: the manager's make, the held object's slot
// 4 and a slot of a particle's own effect (0xE000 and the slot); releases
// are 0x8, as BRelease notes them.
constexpr uint32_t kSdMake = 0xE11C, kSdHeld = 0xE204, kSdOwn = 0xE000;

// A slot of a particle's own effect with one word.
inline uint32_t SdOwnCall(char* object, uint32_t slot, uint32_t value)
{
    return BOurs(kSdOwn + slot, Word(object), Word(&value), 4,
                 [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), slot), object, value); });
}

// Its slot 0x14, which only asks (the answer in the low byte).
inline bool SdOwnAsks(char* object)
{
    return BOurs(kSdOwn + 0x14, Word(object), 0, 0,
                 [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 0x14), object); }) &
           0xFF;
}

// Its slot 0x20 or 0x1C, with a block of two bytes and thirteen floats.
inline void SdOwnBlock(char* object, uint32_t slot, char* block)
{
    BOurs(kSdOwn + slot, Word(object), Word(block), kBBlock,
          [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), slot), object, block); });
}

// Its slot 0x34 (0xD, the effect's +0x270, 1) or 0x30 (3, a value, 1): a
// kind, a pointer to a word and a flag. The notes keep the word; for slot
// 0x34 the pointer too (the effect's own field; slot 0x30's is the
// caller's stack).
inline void SdOwnValue(char* object, uint32_t slot, uint32_t kind, uint32_t* value, uint32_t flag)
{
    const uint32_t noted[4] = {kind, *value, flag, slot == 0x34 ? Word(value) : 0u};
    BOurs(kSdOwn + slot, Word(object), Word(noted), sizeof noted, [&] {
        return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), slot), object, kind, value, flag);
    });
}

// Slot 8: let go of.
inline void SdRelease(char* object)
{
    BOurs(0x8, Word(object), 0, 0, [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 0x8), object); });
}

// The effect named by the description's name (+0x160, the game's string)
// joined with `suffix` (0x0041E270, the joined string freed after), made by
// the effects manager's slot 0x1C and left at `out`; true when it was.
inline bool SdMakeNamed(char* description, const char* suffix, uint32_t out)
{
    uint32_t joined[3] = {};
    reinterpret_cast<uint32_t*(__cdecl*)(uint32_t*, char*, const char*)>(addr::SC4String_ConcatBodyText)(
        joined, description + 0x160, suffix);
    const char* const name = Ptr(joined[0]);
    char* const manager = M<char*>(kEffectsManager);
    const bool made = BOurs(kSdMake, 0, Word(name), kBString,
                            [&] {
                                return CallThis<uint32_t>(At<uint32_t>(At<char*>(manager, 0), 0x1C), manager, name,
                                                          out);
                            },
                            out) &
                      0xFF;
    if (joined[0])
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(Ptr(joined[0]));
    return made;
}

// The angle a new particle of a description not of kinds 2 to 4 is turned
// by: atan2 of the first block's -(+0xA8) and +0x90 (FPATAN, 64 bits
// whatever the precision control) times the float at 0x00B0A5A4 (1/2pi),
// stored as a float; the x87's own instructions in the game's order.
inline float SdParticleTurn(char* self)
{
    float turn = 0.0f;
    float* const out = &turn;
    const float* const scale = reinterpret_cast<const float*>(0x00B0A5A4);
    __asm {
        mov eax, self
        fld dword ptr [eax + 0xA8]
        fchs
        fld dword ptr [eax + 0x90]
        fpatan
        mov ecx, scale
        fmul dword ptr [ecx]
        mov edx, out
        fstp dword ptr [edx]
    }
    return turn;
}

// 0x005AEF20 (no arguments, plain ret), slot 0x3C of the class's
// sub-object (the effect's +4): the first particle of the class's free
// list, after the pool has grown (0x005A61B0) when the list is empty,
// cleared (0x005ADCB0: the particle's own clear 0x005B9DF0, six floats
// from 0x00B22D4C and no effect of its own) and started (0x005B7580).
// One that is running after that gets +0x48 and +0x4C zeroed and a turn at
// +0x50: -999 for kinds 2 to 4 of the description's +0xD5, else
// SdParticleTurn. When the description names an effect (+0x160 not empty),
// an effect the particle held is let go (+0x44 cleared first) and the
// manager makes the one named with "_slave" (0x00A9EFA0) into +0x44; when
// it did, that effect's slot 0x34 is called with 0xD, the effect's +0x270
// and 1 for an effect whose byte +0x26F is set, and its slot 0xC with the
// effect's +0x188. The answer is the particle.
inline uint32_t TakeParticleB(char* base)
{
    char* const self = base - 4;
    if (!Global<uint32_t>(kParticleFreeB))
        PlGrowFor(kParticlePoolB);
    const uint32_t particle = Global<uint32_t>(kParticleFreeB);
    Global<uint32_t>(kParticleFreeB) = M<uint32_t>(particle);
    char* const p = Ptr(particle);
    PlClearFor(p);
    for (const uint32_t offset : {0x1Cu, 0x20u, 0x24u, 0x38u, 0x3Cu, 0x40u})
        At<uint32_t>(p, offset) = M<uint32_t>(0x00B22D4C);
    At<uint32_t>(p, 0x44) = 0;
    CallThis<void>(addr::SC4Effect_StartParticle, self, particle);
    if (!At<uint8_t>(p, 0))
        return particle;
    At<uint32_t>(p, 0x48) = 0;
    At<uint32_t>(p, 0x4C) = 0;
    const uint8_t kind = At<uint8_t>(At<char*>(self, 0x24), 0xD5);
    if (kind >= 2 && kind <= 4)
        At<uint32_t>(p, 0x50) = 0xC479C000u;
    else
        At<float>(p, 0x50) = SdParticleTurn(self);
    char* const description = At<char*>(self, 0x24);
    if (At<uint32_t>(description, 0x160) == At<uint32_t>(description, 0x164))
        return particle;
    if (char* const old = At<char*>(p, 0x44)) {
        At<uint32_t>(p, 0x44) = 0;
        SdRelease(old);
    }
    if (!SdMakeNamed(description, reinterpret_cast<const char*>(0x00A9EFA0), particle + 0x44))
        return particle;
    if (At<uint8_t>(self, 0x26F))
        SdOwnValue(At<char*>(p, 0x44), 0x34, 0xD, &At<uint32_t>(self, 0x270), 1);
    SdOwnCall(At<char*>(p, 0x44), 0xC, At<uint32_t>(self, 0x188));
    return particle;
}

// The block the "_death" effect is placed with (two bytes and thirteen
// floats). For an effect whose second block has its second byte (+0xC5)
// set: the particle's position (+0xC) through the second block (its matrix
// by rows at +0xC8, its place +0xEC and scale +0xF8), and the matrix
// multiplied out against one with the float at 0x00A81054 (zero) off the
// diagonal and one on it, as the game's code does it (its sums and
// roundings kept, so a NaN or a signed zero comes out as the game's);
// the second byte that byte with bit 0 set. Else the position as it is
// with a matrix of one on the diagonal (its first row the floats at
// 0x00A81228 and 0x00A81054 and a zero) and the second byte 1. The first
// byte is zero, the scale is the effect's (one in the second case).
inline void SdDeathBlock(char* self, char* p, char* block)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const auto put = [&](uint32_t at, float value) { At<float>(block, at) = value; };
    At<uint8_t>(block, 0) = 0;
    const uint8_t second = At<uint8_t>(self, 0xC5);
    const double px = At<float>(p, 0xC), py = At<float>(p, 0x10), pz = At<float>(p, 0x14);
    if (!second) {
        At<uint8_t>(block, 1) = 1;
        put(0x4, M<float>(0x00A81228));
        put(0x8, M<float>(0x00A81054));
        for (const uint32_t at : {0xCu, 0x10u, 0x18u, 0x1Cu, 0x20u})
            At<uint32_t>(block, at) = 0;
        At<uint32_t>(block, 0x14) = 0x3F800000u;
        At<uint32_t>(block, 0x24) = 0x3F800000u;
        std::memcpy(block + 0x28, p + 0xC, 12);
        At<uint32_t>(block, 0x34) = 0x3F800000u;
        return;
    }
    At<uint8_t>(block, 1) = second | 1;
    const double scale = f(0xF8);
    const double t0 = (pz * f(0xD0) + py * f(0xCC)) + px * f(0xC8);
    const double t1 = (pz * f(0xDC) + py * f(0xD8)) + px * f(0xD4);
    const float t2 = static_cast<float>((pz * f(0xE8) + py * f(0xE4)) + px * f(0xE0));
    const float sx = static_cast<float>(scale * t0);
    const float sy = static_cast<float>(t1 * scale);
    put(0x28, static_cast<float>(static_cast<double>(sx) + f(0xEC)));
    put(0x2C, static_cast<float>(static_cast<double>(sy) + f(0xF0)));
    put(0x30, static_cast<float>(t2 * scale + f(0xF4)));
    const double k = M<float>(0x00A81054);
    // Row 2, then row 1, each from the products of k with the two others.
    put(0x1C, static_cast<float>((k * f(0xE4) + k * f(0xE8)) + f(0xE0)));
    put(0x20, static_cast<float>((k * f(0xE0) + k * f(0xE8)) + f(0xE4)));
    put(0x24, static_cast<float>((k * f(0xE0) + k * f(0xE4)) + f(0xE8)));
    put(0x10, static_cast<float>((k * f(0xD8) + k * f(0xDC)) + f(0xD4)));
    put(0x14, static_cast<float>((k * f(0xD4) + k * f(0xDC)) + f(0xD8)));
    put(0x18, static_cast<float>((k * f(0xD4) + k * f(0xD8)) + f(0xDC)));
    // Row 0: k times +0xD0 and times +0xCC each stored as a float once.
    const float k2 = static_cast<float>(k * f(0xD0));
    const double k1 = k * f(0xCC);
    const float k1f = static_cast<float>(k1);
    const double k0 = k * f(0xC8);
    put(0x4, static_cast<float>((k1 + k2) + f(0xC8)));
    put(0x8, static_cast<float>((static_cast<double>(k2) + k0) + f(0xCC)));
    put(0xC, static_cast<float>((k0 + k1f) + f(0xD0)));
    put(0x34, static_cast<float>(scale));
}

// 0x005AF390 (ret 4), slot 0x40 of the class's sub-object, with the
// particle. For an effect with an object at +0x278 and a particle whose age
// (+4) is not below its life (+8), the particle is started again
// (0x005AF060) instead. Otherwise the particle's own effect (+0x44) is
// stopped (its slot 0x10 with the effect's +0x18C) and, read again, let go
// with +0x44 cleared first. An effect whose byte +0x26E is set and whose
// +0x18C is zero has the manager make the one named with "_death"
// (0x00AA0CDC): when it did, it is given SdDeathBlock (slot 0x20), its slot
// 0x34 is called with 0xD, +0x270 and 1 for an effect whose byte +0x26F is
// set, and its slot 0xC with 0. The particle's own effect, read again, is
// let go when there (without clearing it), the particle goes to the head of
// the class's free list and the made effect, when there, is let go. Last,
// either way, an effect with the object at +0x278, no particle left (+0x18)
// and its byte +0x10 set tells that object (its slot 4 with 0).
inline void GiveBackParticleB(char* base, uint32_t particle)
{
    char* const self = base - 4;
    char* const p = Ptr(particle);
    if (At<uint32_t>(self, 0x278) && At<float>(p, 4) >= At<float>(p, 8)) {
        BOurs(addr::SC4EffectB_RestartParticle, particle, 0, 0, [&] {
            CallThis<void>(addr::SC4EffectB_RestartParticle, self, particle);
            return 0u;
        });
    } else {
        if (char* const own = At<char*>(p, 0x44)) {
            SdOwnCall(own, 0x10, At<uint32_t>(self, 0x18C));
            if (char* const again = At<char*>(p, 0x44)) {
                At<uint32_t>(p, 0x44) = 0;
                SdRelease(again);
            }
        }
        uint32_t made = 0;
        if (At<uint8_t>(self, 0x26E) && At<uint32_t>(self, 0x18C) == 0 &&
            SdMakeNamed(At<char*>(self, 0x24), reinterpret_cast<const char*>(0x00AA0CDC), Word(&made))) {
            char block[0x38] = {};
            SdDeathBlock(self, p, block);
            SdOwnBlock(Ptr(made), 0x20, block);
            if (At<uint8_t>(self, 0x26F))
                SdOwnValue(Ptr(made), 0x34, 0xD, &At<uint32_t>(self, 0x270), 1);
            SdOwnCall(Ptr(made), 0xC, 0);
        }
        if (char* const own = At<char*>(p, 0x44))
            SdRelease(own);
        M<uint32_t>(particle) = Global<uint32_t>(kParticleFreeB);
        Global<uint32_t>(kParticleFreeB) = particle;
        if (made)
            SdRelease(Ptr(made));
    }
    char* const object = At<char*>(self, 0x278);
    if (object && At<int32_t>(self, 0x18) == 0 && At<uint8_t>(self, 0x10)) {
        const uint32_t zero = 0;
        BOurs(kSdHeld, Word(object), Word(&zero), 4,
              [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 0x4), object, zero); });
    }
}

// --- The check whether a particle is in view and the restart of one --------------------------
//
// 0x005AD890 and 0x005AF060 (part E15): what the second class's draw and
// its give back still called by address. What the notes call the calls on
// the object at the effect's +0x278 (0xE500 and the slot), on its
// sub-object (0xE600 and the slot) and the holds of the model instance and
// of the model (0x1004, as BAddRef notes them).
constexpr uint32_t kSdObject = 0xE500, kSdSub = 0xE600, kSdHold = 0x1004;

// The blocks a particle's own effect filled (its slots 0x38 and 0x3C) in
// the game's run, in order (the replay's answers).
struct SdFilled {
    char bytes[0x38];
};
std::vector<SdFilled> g_sdFilled;
std::size_t g_sdFilledAt = 0;

// Its slot 0x38 or 0x3C, which fills a block.
inline void SdOwnFill(char* object, uint32_t slot, char* block)
{
    BOurs(kSdOwn + slot, Word(object), Word(block), kBBlock,
          [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), slot), object, block); });
    if (g_bReplay && g_sdFilledAt < g_sdFilled.size())
        std::memcpy(block, g_sdFilled[g_sdFilledAt++].bytes, sizeof(SdFilled));
}

// A block of one on the diagonal at `place` (zero without one), its scale
// one and both bytes `flag`.
inline void SdPlainBlock(char* block, uint8_t flag, const float* place)
{
    At<uint8_t>(block, 0) = flag;
    At<uint8_t>(block, 1) = flag;
    for (uint32_t i = 0; i < 9; ++i)
        At<uint32_t>(block, 4 + i * 4) = i % 4 ? 0u : 0x3F800000u;
    for (uint32_t i = 0; i < 3; ++i)
        At<float>(block, 0x28 + i * 4) = place ? place[i] : 0.0f;
    At<uint32_t>(block, 0x34) = 0x3F800000u;
}

// An object held through the counting part of a model (its virtual base:
// the offset at its +4's +4, from its +4).
inline char* SdCounted(char* model)
{
    return model + 4 + At<int32_t>(At<char*>(model, 4), 4);
}

inline void SdHold(char* object)
{
    BOurs(kSdHold, Word(object), 0, 0, [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 4), object); });
}

// 0x005AD890 (ret 0x14), with the depth of the camera's plane, the camera
// (+0x3C to +0x44 its place, +0x54 to +0x5C its direction, +0x60 to +0x68
// and +0x6C to +0x74 its two other axes), the particle, its place and its
// scale. With the floats at the effect's +0x1E8 (+0x20 to +0x30, v below)
// and D the place along the direction plus the depth (kept unrounded):
// the scale becomes v+0x30 over D times the scale (s, unrounded, stored as
// a float); a and b are the place less the camera's along its two other
// axes (floats). r = (s - v+0x20) / v+0x24 (stored as a float, compared
// unrounded): above zero, r at most one, the place moves toward the
// camera's axes by s less one, then by the stored scale less one, each
// times r and a or b, the sums kept as the game keeps them. A particle
// whose D is not below the scale's size is in view. Otherwise it bounces
// off the camera when D is above zero, its velocity (+0x1C) going toward
// the camera (d, along the direction, below zero) and a and b within
// v+0x28: the velocity loses twice d along the direction and gains v+0x2C
// times a and b along the two axes, the manager makes "camera_collision"
// (0x00AA0CB0) which, when made, is placed (slot 0x20 with a block of one
// at the place, both bytes 1) and started (slot 0xC with +0x188), and what
// it left is let go; in view. Otherwise the particle's own effect (+0x44),
// when it answers its slot 0x14, is stopped (slot 0x10 with +0x18C) and,
// read again, let go with +0x44 cleared first; not in view.
inline bool ParticleInViewB(char* self, float depth, char* camera, char* p, float* place, float* scale)
{
    const auto c = [&](uint32_t at) { return static_cast<double>(At<float>(camera, at)); };
    const auto v = [&](uint32_t at) { return static_cast<double>(At<float>(At<char*>(self, 0x1E8), at)); };
    const double d0 = ((c(0x58) * place[1] + c(0x5C) * place[2]) + c(0x54) * place[0]) + depth;
    const double s = v(0x30) / d0 * *scale;
    *scale = static_cast<float>(s);
    const double x = place[0] - c(0x3C), y = place[1] - c(0x40), z = place[2] - c(0x44);
    const float a = static_cast<float>((z * c(0x68) + y * c(0x64)) + x * c(0x60));
    const float b = static_cast<float>((z * c(0x74) + y * c(0x70)) + x * c(0x6C));
    const double toward = (s - v(0x20)) / v(0x24);
    if (toward > M<float>(0x00A81054)) {
        float r = static_cast<float>(toward);
        if (r > M<float>(0x00A81228))
            r = 1.0f;
        const double q = s - M<float>(0x00A81228);
        const float q1 = static_cast<float>(q * c(0x64));
        const float q2 = static_cast<float>(static_cast<float>(q * c(0x68)) * static_cast<double>(r));
        const float px = static_cast<float>(static_cast<double>(static_cast<float>(((q * c(0x60)) * r) * a)) + place[0]);
        const float py = static_cast<float>((static_cast<double>(q1) * r) * a + place[1]);
        const double pz = static_cast<double>(q2) * a + place[2];
        place[0] = px;
        place[1] = py;
        place[2] = static_cast<float>(pz);
        const double t = static_cast<double>(*scale) - M<float>(0x00A81228);
        const float t1 = static_cast<float>(t * c(0x70));
        const float t2 = static_cast<float>(static_cast<float>(t * c(0x74)) * static_cast<double>(r));
        place[0] = static_cast<float>(static_cast<double>(px) + static_cast<float>(((t * c(0x6C)) * r) * b));
        place[1] = static_cast<float>(static_cast<double>(py) + (static_cast<double>(t1) * r) * b);
        place[2] = static_cast<float>(pz + static_cast<float>(static_cast<double>(t2) * b));
    }
    if (!(d0 < std::fabs(static_cast<double>(*scale))))
        return true;
    const float d = static_cast<float>((c(0x5C) * At<float>(p, 0x24) + c(0x58) * At<float>(p, 0x20)) +
                                       c(0x54) * At<float>(p, 0x1C));
    const double reach = At<float>(At<char*>(self, 0x1E8), 0x28);
    if (d0 > M<float>(0x00A81054) && d < M<float>(0x00A81054) && std::fabs(a) < reach && std::fabs(b) < reach) {
        const double twice = static_cast<double>(d) + d;
        const float n1 = static_cast<float>(twice * c(0x58)), n2 = static_cast<float>(twice * c(0x5C));
        At<float>(p, 0x1C) = static_cast<float>(At<float>(p, 0x1C) - twice * c(0x54));
        At<float>(p, 0x20) = static_cast<float>(static_cast<double>(At<float>(p, 0x20)) - n1);
        At<float>(p, 0x24) = static_cast<float>(static_cast<double>(At<float>(p, 0x24)) - n2);
        for (const auto& [axis, along] : {std::pair<uint32_t, float>{0x60, a}, {0x6C, b}}) {
            const double k = v(0x2C);
            const float k1 = static_cast<float>(k * c(axis + 4)), k2 = static_cast<float>(k * c(axis + 8));
            At<float>(p, 0x1C) = static_cast<float>((k * c(axis)) * along + At<float>(p, 0x1C));
            At<float>(p, 0x20) = static_cast<float>(static_cast<double>(k1) * along + At<float>(p, 0x20));
            At<float>(p, 0x24) =
                static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(k2) * along)) +
                                   At<float>(p, 0x24));
        }
        // What the make leaves is emptied first (SC4RefPtr::Reset, 0x004F20A0),
        // which for the empty one does nothing.
        uint32_t made = 0;
        const char* const name = reinterpret_cast<const char*>(0x00AA0CB0);
        char* const manager = M<char*>(kEffectsManager);
        const bool answered =
            BOurs(kSdMake, 0, Word(name), kBString,
                  [&] {
                      return CallThis<uint32_t>(At<uint32_t>(At<char*>(manager, 0), 0x1C), manager, name, &made);
                  },
                  Word(&made)) &
            0xFF;
        if (answered) {
            char block[0x38];
            SdPlainBlock(block, 1, place);
            SdOwnBlock(Ptr(made), 0x20, block);
            SdOwnCall(Ptr(made), 0xC, At<uint32_t>(self, 0x188));
        }
        if (made)
            SdRelease(Ptr(made));
        return true;
    }
    if (At<char*>(p, 0x44) && SdOwnAsks(At<char*>(p, 0x44))) {
        SdOwnCall(At<char*>(p, 0x44), 0x10, At<uint32_t>(self, 0x18C));
        if (char* const own = At<char*>(p, 0x44)) {
            At<uint32_t>(p, 0x44) = 0;
            SdRelease(own);
        }
    }
    return false;
}

// 0x005AF060 (ret 4), with the particle, for an effect with an object at
// +0x278 (another effect of the class; its description, read once, d
// below). The particle's age is zeroed, its life drawn from d's +0xC to
// +0x10 (SC4Random::Uniform) and it runs again. When d names an effect
// (+0x160) other than the effect's description's, the particle's own
// effect (+0x44, read again for each call) fills two blocks of one on the
// diagonal (slots 0x38 and 0x3C), is stopped (slot 0x10 with +0x18C) and
// let go with +0x44 cleared first; the manager makes the one d names with
// "_slave" into +0x44 and, when it did, it is given the first block
// (slots 0x1C and 0x20), slot 0x34 with 0xD, +0x270 and 1 for an effect
// whose byte +0x26F is set, and slot 0xC with +0x188. An effect with a
// model instance (+0x220) gives the object one when it has none: the
// instance held and kept there (SC4RefPtr::AssignFrom, 0x0054DB10), then
// the model (+0x224) as 0x00420220 does it, the new one held and the old
// let go through their counting parts. The object is started (slot 0 with
// 0) unless it answers its slot 8 (whether it runs); for flag 0x200 of
// d's third word it starts the particle (SC4Effect::StartParticle), for
// flag 0x800000 of its first it takes the particle's velocity as its own
// (+0x150 to +0x158, +0x154 then zeroed), its timer +0x148 zeroed, and
// wanders (SC4Effect::StepFlag800000 with 0); last its sub-object (+4)
// takes the particle (slot 0x2C).
inline void RestartParticleB(char* self, char* p)
{
    char* const object = At<char*>(self, 0x278);
    char* const described = At<char*>(object, 0x24);
    At<uint32_t>(p, 4) = 0;
    At<float>(p, 8) = static_cast<float>(SuUniform(At<float>(described, 0xC), At<float>(described, 0x10)));
    At<uint8_t>(p, 0) = 1;
    const char* const name = At<const char*>(described, 0x160);
    const uint32_t length = At<uint32_t>(described, 0x164) - At<uint32_t>(described, 0x160);
    char* const mine = At<char*>(self, 0x24);
    const bool same = At<uint32_t>(mine, 0x164) - At<uint32_t>(mine, 0x160) == length &&
                      std::memcmp(name, At<const char*>(mine, 0x160), length) == 0;
    if (length && !same) {
        char block[0x38], other[0x38];
        SdPlainBlock(block, 0, nullptr);
        SdPlainBlock(other, 0, nullptr);
        if (At<char*>(p, 0x44)) {
            SdOwnFill(At<char*>(p, 0x44), 0x38, block);
            SdOwnFill(At<char*>(p, 0x44), 0x3C, other);
            SdOwnCall(At<char*>(p, 0x44), 0x10, At<uint32_t>(self, 0x18C));
            if (char* const own = At<char*>(p, 0x44)) {
                At<uint32_t>(p, 0x44) = 0;
                SdRelease(own);
            }
        }
        if (char* const own = At<char*>(p, 0x44)) {
            At<uint32_t>(p, 0x44) = 0;
            SdRelease(own);
        }
        if (SdMakeNamed(described, reinterpret_cast<const char*>(0x00A9EFA0), Word(p + 0x44))) {
            SdOwnBlock(At<char*>(p, 0x44), 0x1C, block);
            SdOwnBlock(At<char*>(p, 0x44), 0x20, block);
            if (At<uint8_t>(self, 0x26F))
                SdOwnValue(At<char*>(p, 0x44), 0x34, 0xD, &At<uint32_t>(self, 0x270), 1);
            SdOwnCall(At<char*>(p, 0x44), 0xC, At<uint32_t>(self, 0x188));
        }
    }
    if (char* const instance = At<char*>(self, 0x220); instance && !At<uint32_t>(object, 0x220)) {
        SdHold(instance);
        At<char*>(object, 0x220) = instance;
        char* const model = At<char*>(self, 0x224);
        char* const old = At<char*>(object, 0x224);
        if (model != old) {
            if (model)
                SdHold(SdCounted(model));
            At<char*>(object, 0x224) = model;
            if (old)
                SdRelease(SdCounted(old));
        }
    }
    const bool runs = BOurs(kSdObject + 8, Word(object), 0, 0, [&] {
                          return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 8), object);
                      }) &
                      0xFF;
    if (!runs) {
        const uint32_t zero = 0;
        BOurs(kSdObject, Word(object), Word(&zero), 4,
              [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(object, 0), 0), object, zero); });
    }
    if (At<uint32_t>(described, 8) >> 9 & 1)
        CallThis<void>(addr::SC4Effect_StartParticle, object, Word(p));
    if (At<uint32_t>(described, 0) >> 23 & 1) {
        At<uint32_t>(object, 0x150) = At<uint32_t>(p, 0x1C);
        At<uint32_t>(object, 0x154) = At<uint32_t>(p, 0x20);
        At<uint32_t>(object, 0x158) = At<uint32_t>(p, 0x24);
        At<uint32_t>(object, 0x154) = 0;
        At<uint32_t>(object, 0x148) = 0;
        CallThis<void>(addr::SC4Effect_StepFlag800000, object, 0u);
    }
    char* const sub = object + 4;
    const uint32_t particle = Word(p);
    BOurs(kSdSub + 0x2C, Word(sub), Word(&particle), 4,
          [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(sub, 0), 0x2C), sub, particle); });
}

// The whole number nearest to `value` less the float at 0x00A84D2C, by the
// float at 0x00AA0BFC (1.5 times 2^23) added and taken off the float's
// bits, one up when the value is that one up (LfRoundDown's, the game's
// other copy of the constant).
inline uint32_t SdRoundDown(double value)
{
    uint32_t whole =
        LfBits(static_cast<float>((value - M<float>(0x00A84D2C)) + M<float>(0x00AA0BFC))) + 0xB4C00000u;
    const uint32_t up = whole + 1;
    if (value == static_cast<double>(static_cast<int32_t>(up)))
        whole = up;
    return whole;
}

// A list of floats of the description (begin at `at`, end at `at` + 4)
// read at `t` (0 to 1) of its length: its one value, or between the two
// neighbours of t times the number of values less one. (The part's step
// reads its list at an unrounded `t`; the other callers' are floats.)
inline double SdCurve(char* description, uint32_t at, double t)
{
    const float* const values = At<const float*>(description, at);
    const int32_t last = (static_cast<int32_t>(At<uint32_t>(description, at + 4) - At<uint32_t>(description, at)) >> 2) - 1;
    if (last == 0)
        return values[0];
    const double x = LfUnsignedF(static_cast<uint32_t>(last)) * t;
    const uint32_t i = SdRoundDown(x);
    const double part = x - LfUnsignedF(i);
    if (part > M<float>(0x00A81054))
        return (static_cast<double>(values[i + 1]) - values[i]) * part + values[i];
    return values[i];
}

// An x87 value as the FPU keeps it, its ten bytes: FSIN, FCOS and FPATAN
// leave 64 bits of mantissa whatever the precision control, and the block
// multiplies some of them as they are (each product then rounded to the 53
// bits the precision control keeps).
struct SdX87 {
    unsigned char bytes[10];
};

// The sine and the cosine of `angle` (FSIN, FCOS).
inline void SdSinCos(double angle, SdX87* sine, SdX87* cosine)
{
    __asm {
        mov eax, sine
        mov edx, cosine
        fld qword ptr [angle]
        fld st(0)
        fcos
        fstp tbyte ptr [edx]
        fsin
        fstp tbyte ptr [eax]
    }
}

// The angle of (x, y) (FPATAN of y over x).
inline SdX87 SdAtan2(double y, double x)
{
    SdX87 angle;
    SdX87* const out = &angle;
    __asm {
        mov eax, out
        fld qword ptr [y]
        fld qword ptr [x]
        fpatan
        fstp tbyte ptr [eax]
    }
    return angle;
}

// `value` times `factor`.
inline double SdTimes(const SdX87& value, double factor)
{
    double product = 0.0;
    double* const out = &product;
    const SdX87* const in = &value;
    __asm {
        mov eax, in
        mov edx, out
        fld tbyte ptr [eax]
        fmul qword ptr [factor]
        fstp qword ptr [edx]
    }
    return product;
}

// `from` less `value`.
inline double SdLess(double from, const SdX87& value)
{
    double difference = 0.0;
    double* const out = &difference;
    const SdX87* const in = &value;
    __asm {
        mov eax, in
        mov edx, out
        fld tbyte ptr [eax]
        fsubr qword ptr [from]
        fstp qword ptr [edx]
    }
    return difference;
}

// `value` rounded to a float at once.
inline float SdFloat(const SdX87& value)
{
    float rounded = 0.0f;
    float* const out = &rounded;
    const SdX87* const in = &value;
    __asm {
        mov eax, in
        mov edx, out
        fld tbyte ptr [eax]
        fstp dword ptr [edx]
    }
    return rounded;
}

// 0x005AC070 (cdecl), with a time, a description's list at +0x108 (a
// vector of 0x1C-byte records) and a place (three floats): each record
// moves the place by the sine (FSIN) of the place as it was on entry
// along the record's second to fourth floats plus the time times its
// first, times its last three floats. The sum is ((z d2 + x d0) + t r) +
// y d1; the sine times the first value is added to x as it is, the other
// two products are rounded to floats first. Writes only the place.
inline void MovePlace(float t, char* list, float* place)
{
    const double x = place[0], y = place[1], z = place[2];
    for (const float* e = At<const float*>(list, 0); e != At<const float*>(list, 4); e += 7) {
        SdX87 sine, cosine;
        SdSinCos(((z * e[3] + x * e[1]) + static_cast<double>(t) * e[0]) + y * e[2], &sine, &cosine);
        const float b = static_cast<float>(SdTimes(sine, e[5]));
        const float c = static_cast<float>(SdTimes(sine, e[6]));
        place[0] = static_cast<float>(SdTimes(sine, e[4]) + place[0]);
        place[1] = static_cast<float>(static_cast<double>(b) + place[1]);
        place[2] = static_cast<float>(static_cast<double>(c) + place[2]);
    }
}

// The move as the ported callers make it: ours when
// renderer.effects.move_place is, else the game's by its address (so
// through its detour in compare mode).
inline void SdMovePlace(float t, char* list, float* place)
{
    if (g_movePlaceOurs)
        MovePlace(t, list, place);
    else
        reinterpret_cast<void(__cdecl*)(float, char*, float*)>(addr::SC4Effect_MovePlace)(t, list, place);
}

// 0x005ACA30 (ret 0x14), with the particle, a point of its life (0 to 1,
// a float), a scale, its place (three floats, which 0x005AC070 may move)
// and the block to fill: two bytes and thirteen floats, the particle's
// turn and size as a matrix by rows, its place and its scale. The x87 runs
// at 53 bits; k is the float at 0x00A81054 (zero), which the game's code
// multiplies through where an identity matrix was folded in.
//
// The turn Q: for kinds 2 to 4 of the description's +0xD5 the particle's
// velocity (+0x1C), made one long when its length is above k, is made a
// matrix by 0x0080B910, 0x0080B530 or 0x0080B830, each of whose entries
// becomes (k times the two others of its row) plus itself. For a
// description whose +0x198 is above k that matrix is then turned about its
// second axis: the particle's angle (+0x50) goes towards the matrix's own
// (FPATAN of -m20 over m00, times the float at 0x00B0A5A4) by the part
// over +0x198 times the float at 0x00AA0BF4, a full turn added or taken
// off first when they are more than half a turn apart, or is set to it
// when it was -999 (0x00AA0BF8); the matrix turns by the difference times
// 2pi (0x00AA0C08). And for one whose +0x19C is not k it is tilted about
// its third axis by the particle's +0x4C, which first loses +0x1A0 times
// the frame's step (+0x108) of itself and gains the step times either the
// effect's +0x13C to +0x144 against the velocity (descriptions without
// flag 0x80 of +8) or the description's +0xEC times the effect's +0x1B8
// to +0x1C0 against it (with), times +0x19C. For other kinds Q is the turn
// by the particle's angle (+0x50) times 2pi about the second axis.
//
// Then Q spins about the second axis by the description's list at +0xC4
// read at the point of the life, times the particle's +0x30 and 2pi; the
// place is moved by 0x005AC070 (with the effect's double at +0x110 as a
// float and the description's list at +0x108) when that list is not empty
// and the description's +8 has no bit 8. For an effect whose second block
// has its second byte (+0xC5) set the block is that second block times the
// spun matrix, the place taken through the second block and the scale
// times the effect's (+0xF8); else the spun matrix, the place and the
// scale as they are. The block's first byte goes up by one, its second is
// 5, with 2 for a scale that is not one and the second block's byte. The
// effect's box (+0x1A0) is made or widened by the block's place.
inline void ParticleBlockB(char* self, char* p, float t, float scale, float* place, char* block)
{
    const double k = M<float>(0x00A81054);
    const double twoPi = M<float>(0x00AA0C08);
    const auto f = [](double value) { return static_cast<float>(value); };
    uint8_t marks = scale == M<float>(0x00A81228) ? 5 : 7;
    char* const description = At<char*>(self, 0x24);
    // The turn: q00 stays on the x87 as a double, the rest are floats.
    double q00 = 0.0;
    float q01 = 0.0f, q02 = 0.0f, q10 = 0.0f, q11 = 1.0f, q12 = 0.0f, q20 = 0.0f, q21 = 0.0f, q22 = 0.0f;
    const uint8_t kind = At<uint8_t>(description, 0xD5);
    if (kind >= 2 && kind <= 4) {
        float direction[3] = {At<float>(p, 0x1C), At<float>(p, 0x20), At<float>(p, 0x24)};
        const double length = std::sqrt((static_cast<double>(direction[2]) * direction[2] +
                                         static_cast<double>(direction[1]) * direction[1]) +
                                        static_cast<double>(direction[0]) * direction[0]);
        if (length > k) {
            const double inverse = M<float>(0x00B0A5A8) / length;
            for (float& d : direction)
                d = f(d * inverse);
        }
        float m[16] = {};
        for (int i = 0; i < 9; ++i)
            std::memcpy(&m[i], reinterpret_cast<const void*>(0x00B22D4C), 4);
        const uintptr_t maker = kind == 2 ? 0x0080B910u : kind == 3 ? 0x0080B530u : addr::SC4Matrix_FromDirection;
        reinterpret_cast<void(__cdecl*)(float*, float*)>(maker)(direction, m);
        // Each entry: k times the row's two others, plus itself.
        float r[9];
        for (int row = 0; row < 3; ++row) {
            const double a = m[row * 3], b = m[row * 3 + 1], c = m[row * 3 + 2];
            r[row * 3] = f((k * b + k * c) + a);
            r[row * 3 + 1] = f((k * a + k * c) + b);
            r[row * 3 + 2] = f((k * a + k * b) + c);
        }
        q00 = r[0];
        q01 = r[1], q02 = r[2], q10 = r[3], q11 = r[4], q12 = r[5], q20 = r[6], q21 = r[7], q22 = r[8];
        const double spread = At<float>(description, 0x198);
        if (spread > k) {
            const double toward = SdTimes(SdAtan2(-static_cast<double>(m[6]), m[0]), M<float>(0x00B0A5A4));
            const double previous = At<float>(p, 0x50);
            float angle;
            if (At<float>(p, 0x50) == M<float>(0x00AA0BF8)) {
                angle = f(toward);
            } else {
                double from = previous;
                if (toward < previous - M<double>(0x00A92D28))
                    from = previous - M<float>(0x00A81228);
                else if (toward > previous + M<double>(0x00A92D28))
                    from = previous + M<float>(0x00A81228);
                angle = f((toward - from) / spread * M<float>(0x00AA0BF4) + from);
            }
            SdX87 sine, cosine;
            SdSinCos((angle - toward) * twoPi, &sine, &cosine);
            const double c = SdFloat(cosine), s = SdFloat(sine);
            q00 = c * r[0] - SdTimes(sine, r[2]);
            q02 = f(s * r[0] + c * r[2]);
            q10 = f(static_cast<double>(f(c * r[3])) - f(s * r[5]));
            q12 = f(static_cast<double>(f(s * r[3])) + f(c * r[5]));
            q20 = f(static_cast<double>(f(c * r[6])) - f(s * r[8]));
            q22 = f(static_cast<double>(f(s * r[6])) + f(c * r[8]));
            At<float>(p, 0x50) = angle;
        }
        const double lean = At<float>(description, 0x19C);
        if (!(lean == k)) {
            const double dx = direction[0], dz = -static_cast<double>(direction[2]);
            double pull;
            if (At<uint32_t>(description, 8) >> 7 & 1) {
                const double d = At<float>(description, 0xEC);
                const float a = f(d * At<float>(self, 0x1B8)), b = f(d * At<float>(self, 0x1BC));
                pull = (((d * At<float>(self, 0x1C0)) * dx + a * dz) + b * k) * lean;
            } else {
                pull = ((dx * At<float>(self, 0x144) + dz * At<float>(self, 0x13C)) + At<float>(self, 0x140) * k) * lean;
            }
            const double step = At<float>(self, 0x108), old = At<float>(p, 0x4C);
            const double z = (old - (At<float>(description, 0x1A0) * step) * old) + pull * step;
            At<float>(p, 0x4C) = f(z);
            SdX87 sine, cosine;
            SdSinCos(z, &sine, &cosine);
            const double c = SdFloat(cosine), s = SdFloat(sine);
            const double first = f(q00);
            const float n10 = f(static_cast<double>(f(c * q10)) + f(s * q11));
            const float n20 = f(static_cast<double>(f(c * q20)) + f(s * q21));
            const float n01 = f(c * q01 + f(first * -s));
            const float n11 = f(static_cast<double>(f(q10 * -s)) + f(c * q11));
            const float n21 = f(static_cast<double>(f(-s * q20)) + f(c * q21));
            q00 = q00 * c + f(s * q01);
            q10 = n10, q20 = n20, q01 = n01, q11 = n11, q21 = n21;
        }
    } else {
        SdX87 sine, cosine;
        SdSinCos(static_cast<double>(At<float>(p, 0x50)) * twoPi, &sine, &cosine);
        const float c = SdFloat(cosine), s = SdFloat(sine);
        const float ks = f(SdTimes(sine, k)), kc = f(k * c);
        q00 = static_cast<double>(c) - ks;
        q02 = f(static_cast<double>(kc) + s);
        q10 = f(static_cast<double>(kc) - ks);
        q12 = f(static_cast<double>(ks) + kc);
        q20 = f(SdLess(kc, sine));
        q22 = f(static_cast<double>(ks) + c);
    }
    // The spin about the second axis.
    const double spin = SdCurve(description, 0xC4, t) * At<float>(p, 0x30) * twoPi;
    SdX87 sine, cosine;
    SdSinCos(spin, &sine, &cosine);
    const double s = SdFloat(sine);
    const float s00 = f(SdTimes(cosine, q00) - SdTimes(sine, q02));
    const float s10 = f(static_cast<double>(f(SdTimes(cosine, q10))) - s * q12);
    const float s20 = f(static_cast<double>(f(SdTimes(cosine, q20))) - f(s * q22));
    const float s02 = f(q00 * s + f(SdTimes(cosine, q02)));
    const float s12 = f(s * q10 + f(SdTimes(cosine, q12)));
    const float s22 = f(static_cast<double>(f(s * q20)) + f(SdTimes(cosine, q22)));
    const float s01 = q01, s11 = q11, s21 = q21;
    if (At<uint32_t>(description, 0x108) != At<uint32_t>(description, 0x10C) &&
        !(At<uint32_t>(description, 8) >> 8 & 1))
        SdMovePlace(f(At<double>(self, 0x110)), description + 0x108, place);
    float out[13];
    const uint8_t second = At<uint8_t>(self, 0xC5);
    if (second) {
        const auto e = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
        const double e00 = e(0xC8), e01 = e(0xCC), e02 = e(0xD0), e10 = e(0xD4), e11 = e(0xD8), e12 = e(0xDC),
                     e20 = e(0xE0), e21 = e(0xE4), e22 = e(0xE8), size = e(0xF8);
        const double v0 = place[0], v1 = place[1], v2 = place[2];
        const double p0 = (v2 * e02 + e01 * v1) + e00 * v0;
        const double p1 = (v2 * e12 + e11 * v1) + e10 * v0;
        const float p2 = f((e22 * v2 + e21 * v1) + e20 * v0);
        out[9] = f(static_cast<double>(f(p0 * size)) + e(0xEC));
        out[10] = f(e(0xF0) + f(p1 * size));
        out[11] = f(e(0xF4) + p2 * size);
        out[0] = f((s20 * e02 + s10 * e01) + s00 * e00);
        out[1] = f((s01 * e00 + e02 * s21) + e01 * s11);
        out[2] = f((s22 * e02 + s12 * e01) + s02 * e00);
        out[3] = f((s20 * e12 + s10 * e11) + s00 * e10);
        out[4] = f((s01 * e10 + e12 * s21) + e11 * s11);
        out[5] = f((s22 * e12 + s12 * e11) + s02 * e10);
        out[6] = f((s00 * e20 + e22 * s20) + e21 * s10);
        out[7] = f((e22 * s21 + e21 * s11) + e20 * s01);
        out[8] = f((s02 * e20 + e22 * s22) + e21 * s12);
        out[12] = f(size * scale);
        marks |= second;
    } else {
        const float spun[9] = {s00, s01, s02, s10, s11, s12, s20, s21, s22};
        std::memcpy(out, spun, sizeof spun);
        std::memcpy(out + 9, place, 12);
        out[12] = scale;
    }
    ++At<uint8_t>(block, 0);
    std::memcpy(block + 4, out, sizeof out);
    At<uint8_t>(block, 1) = marks;
    const float x = out[9], y = out[10], z = out[11];
    if (At<float>(self, 0x1A0) > At<float>(self, 0x1AC)) {
        std::memcpy(self + 0x1AC, block + 0x28, 12);
        std::memcpy(self + 0x1A0, block + 0x28, 12);
        return;
    }
    if (!(At<float>(self, 0x1AC) > x))
        At<float>(self, 0x1AC) = x;
    if (!(At<float>(self, 0x1A0) < x))
        At<float>(self, 0x1A0) = x;
    if (!(At<float>(self, 0x1B0) > y))
        At<float>(self, 0x1B0) = y;
    if (!(At<float>(self, 0x1A4) < y))
        At<float>(self, 0x1A4) = y;
    if (!(At<float>(self, 0x1B4) > z))
        At<float>(self, 0x1B4) = z;
    if (!(At<float>(self, 0x1A8) < z))
        At<float>(self, 0x1A8) = z;
}

// 0x005AE6E0 (no arguments, plain ret), slot 0x4C of the class: the step
// of an effect that is not drawn. The box's low corner (+0x1A0) is set to
// one and its high corner (+0x1AC) to zero, so it starts empty. For each
// particle of the list (+0x14, read again for every node):
// - one whose own effect (+0x44) is there and answers its slot 0x14 gets a
//   block from 0x005ACA30 (with the particle, its age over its life as a
//   float, a scale of the description's list at +0xAC read at that point
//   times the particle's +0x28 times the double at 0x00AA02D8, a copy of
//   its position and a block of one on the diagonal, the place zero and
//   the scale one), which turns the particle and widens the box; for
//   descriptions with flag 0x10 of their third word (+8) the own effect's
//   slot 0x30 gets 3, the effect's +0x1D0 times the particle's +0x34 times
//   the list at +0xA0 read at the same point, and 1; then, for flag 8, its
//   slot 0x1C a block of one on the diagonal at the first block's place
//   (both bytes 1, the scale one), else its slot 0x20 the first block;
// - any other has its position taken through the second block (its matrix
//   by rows, scaled, its place; the x kept as the x87's double) and widens
//   the box as KeepEffectBox does (the first point makes it).
// When the box is not empty at the end it is grown on every side by the
// first float of the list at +0xAC times the effect's +0x274 times the
// double at 0x00AA02D8.
inline void StepNotDrawnB(char* self)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    for (uint32_t i = 0; i < 3; ++i) {
        At<uint32_t>(self, 0x1A0 + i * 4) = 0x3F800000u;
        At<uint32_t>(self, 0x1AC + i * 4) = 0;
    }
    for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14); node = At<char*>(node, 0)) {
        char* const p = At<char*>(node, 8);
        float position[3] = {At<float>(p, 0xC), At<float>(p, 0x10), At<float>(p, 0x14)};
        char* const own = At<char*>(p, 0x44);
        if (own && SdOwnAsks(own)) {
            const float t = static_cast<float>(static_cast<double>(At<float>(p, 4)) / At<float>(p, 8));
            const float scale = static_cast<float>(SdCurve(At<char*>(self, 0x24), 0xAC, t) * At<float>(p, 0x28) *
                                                   M<double>(0x00AA02D8));
            char block[0x38] = {};
            for (const uint32_t at : {0x4u, 0x14u, 0x24u, 0x34u})
                At<uint32_t>(block, at) = 0x3F800000u;
            CallThis<void>(addr::SC4EffectB_ParticleBlock, self, p, LfBits(t), LfBits(scale), position, block);
            char* const description = At<char*>(self, 0x24);
            char* const mine = At<char*>(p, 0x44);
            if (At<uint32_t>(description, 8) >> 4 & 1) {
                const double curve = SdCurve(description, 0xA0, t);
                uint32_t value = LfBits(static_cast<float>(f(0x1D0) * At<float>(p, 0x34) * curve));
                SdOwnValue(mine, 0x30, 3, &value, 1);
            }
            if (At<uint32_t>(At<char*>(self, 0x24), 8) >> 3 & 1) {
                char placed[0x38] = {};
                At<uint8_t>(placed, 0) = 1;
                At<uint8_t>(placed, 1) = 1;
                for (const uint32_t at : {0x4u, 0x14u, 0x24u, 0x34u})
                    At<uint32_t>(placed, at) = 0x3F800000u;
                std::memcpy(placed + 0x28, block + 0x28, 12);
                SdOwnBlock(mine, 0x1C, placed);
            } else {
                SdOwnBlock(mine, 0x20, block);
            }
            continue;
        }
        const double px = position[0], py = position[1], pz = position[2];
        const double scale = f(0xF8);
        const double x0 = (pz * f(0xD0) + py * f(0xCC)) + px * f(0xC8);
        const double y0 = (pz * f(0xDC) + py * f(0xD8)) + px * f(0xD4);
        const float z0 = static_cast<float>((pz * f(0xE8) + py * f(0xE4)) + px * f(0xE0));
        const float xs = static_cast<float>(x0 * scale);
        const float ys = static_cast<float>(y0 * scale);
        const float zs = static_cast<float>(static_cast<double>(z0) * scale);
        // x stays on the x87 as a double; y and z go through a float.
        const double x = static_cast<double>(xs) + f(0xEC);
        const float y = static_cast<float>(static_cast<double>(ys) + f(0xF0));
        const float z = static_cast<float>(static_cast<double>(zs) + f(0xF4));
        const float xf = static_cast<float>(x);
        if (At<float>(self, 0x1A0) > At<float>(self, 0x1AC)) {
            At<float>(self, 0x1AC) = xf;
            At<float>(self, 0x1B0) = y;
            At<float>(self, 0x1B4) = z;
            At<float>(self, 0x1A0) = xf;
            At<float>(self, 0x1A4) = y;
            At<float>(self, 0x1A8) = z;
            continue;
        }
        if (!(At<float>(self, 0x1AC) > x))
            At<float>(self, 0x1AC) = xf;
        if (!(At<float>(self, 0x1A0) < x))
            At<float>(self, 0x1A0) = xf;
        if (!(At<float>(self, 0x1B0) > y))
            At<float>(self, 0x1B0) = y;
        if (!(At<float>(self, 0x1A4) < y))
            At<float>(self, 0x1A4) = y;
        if (!(At<float>(self, 0x1B4) > z))
            At<float>(self, 0x1B4) = z;
        if (!(At<float>(self, 0x1A8) < z))
            At<float>(self, 0x1A8) = z;
    }
    if (At<float>(self, 0x1A0) > At<float>(self, 0x1AC))
        return;
    const double grow = static_cast<double>(*At<float*>(At<char*>(self, 0x24), 0xAC)) * At<float>(self, 0x274) *
                        M<double>(0x00AA02D8);
    At<float>(self, 0x1A0) = static_cast<float>(f(0x1A0) - grow);
    At<float>(self, 0x1A4) = static_cast<float>(f(0x1A4) - grow);
    At<float>(self, 0x1A8) = static_cast<float>(f(0x1A8) - grow);
    At<float>(self, 0x1AC) = static_cast<float>(grow + f(0x1AC));
    At<float>(self, 0x1B0) = static_cast<float>(grow + f(0x1B0));
    At<float>(self, 0x1B4) = static_cast<float>(grow + f(0x1B4));
}

// --- The part's step --------------------------------------------------------------------------
//
// 0x005AC200 (part E16): what the second class's draw still called by
// address for an effect with +0x26C set and flag 0x100 of its
// description's third word. The part (the effect's +0x228) keeps the
// model instance it was given (+4), the model's meshes (+0xC to +0x10,
// each with its vertex format at +0, its count of vertices as a word at +6
// and its vertices at +8, 0x14 bytes each: a place and two texture
// coordinates), for each mesh a list of three floats a vertex (+0x24, a
// vector of vectors of 0xC bytes; made by 0x00803370 when empty) and its
// description (+0x40). What the notes call the instance's slot 0x10, which
// answers where it keeps its copy of a mesh (0xE700 and the slot).
constexpr uint32_t kSdPartInstance = 0xE700;

// A texture coordinate moved on by `by` less whole turns of 8: the whole
// number nearest to an eighth of it less a half, one up when that is
// exact, times 8 (as the game's integer) taken off.
inline float SdTurnsOff(double by)
{
    return static_cast<float>(
        by - static_cast<double>(static_cast<int32_t>(SdRoundDown(by * M<float>(0x00A867A8)) << 3)));
}

// 0x005AC200 (ret 4), with the step (the effect's +0x110 as a float).
// When the part's lists of three floats are empty, 0x00803370 makes them.
// With d its description and A the step times d+0x114 (unrounded), the
// texture's moves are u = A times d+0x118 (rounded) and v = A times
// d+0x11C (unrounded), each less whole turns of 8 (SdTurnsOff). For each
// mesh (the count read again each time) whose format is the expanded
// legacy format 2 (GDVertexFormat::Expand): the instance's copy of it
// (its slot 0x10 with the mesh's index answers where the copy is kept;
// the copy's vertices at its +8, 0x18 bytes each: a place, a colour and
// two texture coordinates) and, while it has vertices left (the count read
// again after each), each vertex of the copy gets the mesh's place (moved
// by 0x005AC070 with the step and d's list at +0x108 when that is not
// empty), its texture coordinates plus u and v and, when d's list at
// +0x12C is not empty, the colour's last byte: that list read (SdCurve) at
// ((the vertex's three floats along d+0x120 to d+0x128, z first) plus one)
// times a half, unrounded, plus 49152 rounded to a float, the float's bits
// less 0x47400000 and the byte below taken off the low byte.
inline void StepPartB(char* part, float step)
{
    if (At<uint32_t>(part, 0x24) == At<uint32_t>(part, 0x28))
        CallThis<void>(addr::SC4EffectPartB_MakeNormals, part);
    char* description = At<char*>(part, 0x40);
    const auto d = [&](uint32_t at) { return static_cast<double>(At<float>(description, at)); };
    const double along = static_cast<double>(step) * d(0x114);
    const float u = SdTurnsOff(static_cast<float>(along * d(0x118)));
    const float v = SdTurnsOff(along * d(0x11C));
    const auto meshes = [&] {
        return static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(part, 0x10) - At<uint32_t>(part, 0xC)) >> 2);
    };
    const auto mesh = [&](uint32_t i) { return At<char**>(part, 0xC)[i]; };
    for (uint32_t i = 0; i < meshes(); ++i) {
        if (At<uint32_t>(mesh(i), 0) != reinterpret_cast<uint32_t(__cdecl*)(uint32_t)>(addr::GDVertexFormat_Expand)(2))
            continue;
        char* from = At<char*>(mesh(i), 8);
        char* const instance = At<char*>(part, 4);
        const uint32_t kept = BOurs(kSdPartInstance + 0x10, Word(instance), Word(&i), 4, [&] {
            return CallThis<uint32_t>(At<uint32_t>(At<char*>(instance, 0), 0x10), instance, i);
        });
        char* to = At<char*>(M<char*>(kept), 8);
        for (uint32_t j = 0; j < At<uint16_t>(mesh(i), 6); ++j, from += 0x14, to += 0x18) {
            std::memcpy(to, from, 12);
            description = At<char*>(part, 0x40);
            if (At<uint32_t>(description, 0x108) != At<uint32_t>(description, 0x10C))
                SdMovePlace(step, description + 0x108, reinterpret_cast<float*>(to));
            At<float>(to, 0x10) = static_cast<float>(static_cast<double>(u) + At<float>(from, 0xC));
            At<float>(to, 0x14) = static_cast<float>(static_cast<double>(v) + At<float>(from, 0x10));
            description = At<char*>(part, 0x40);
            if (At<uint32_t>(description, 0x12C) == At<uint32_t>(description, 0x130))
                continue;
            const float* const n = At<const float*>(At<char*>(part, 0x24), i * 0xC) + j * 3;
            const double dot = (n[2] * d(0x128) + n[1] * d(0x124)) + n[0] * d(0x120);
            const double at = (dot + M<float>(0x00A81228)) * M<double>(0x00A92D28);
            const uint32_t bits =
                LfBits(static_cast<float>(SdCurve(description, 0x12C, at) + M<float>(0x00AA0C00))) + 0xB8C00000u;
            At<uint8_t>(to, 0xF) = static_cast<uint8_t>(static_cast<uint8_t>(bits) - static_cast<uint8_t>(bits >> 8));
        }
    }
}

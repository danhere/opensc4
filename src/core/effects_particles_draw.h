// The effects' particle draws, written by hand from the game's code: the
// second class's draw (0x005ADD50), the draw into the driver's dynamic
// vertex buffer (0x005B4B90) and the base class's draw without it
// (0x005B3DA0). Included by water_effects.cpp after effects_second.h,
// whose conventions it keeps: the x87 runs at 53 bits,
// intermediates are doubles, rounded to float where the game stores one,
// in the game's order; FSIN, FCOS and FPATAN results are kept as the x87
// leaves them (SdX87) where the game multiplies them as they are.
//
// The second class's draw makes the calls that cannot run twice (the
// model instance's and the model's, the particle's own effect's, the
// part's, the check whether a particle is in view and the context's)
// through BOurs, so that its comparison's replay answers them from the
// game's notes.

// --- The second class's draw -----------------------------------------------------------------

// What the notes call the calls on the model instance (+0x220) and the
// model (+0x224): 0xE300 and 0xE400 and the slot.
constexpr uint32_t kDbInstance = 0xE300, kDbModel = 0xE400;

// The colour block the instance answers (its slot 0x3C): two bytes, a word
// and four floats. The comparison's game run keeps a copy of the one it
// was given, which the replay answers ours with.
constexpr std::size_t kDbColour = 0x14;
char g_dbColour[kDbColour] = {};

// What the check whether a particle is in view left in the place and the
// scale it was given, by the game's run, in order (the replay's answers).
struct DbLeft {
    uint32_t place[3], scale;
};
std::vector<DbLeft> g_dbInView;
std::size_t g_dbInViewAt = 0;

inline char* DbInstanceColour(char* instance, char* scratch)
{
    const uint32_t answer = BOurs(kDbInstance + 0x3C, Word(instance), 0, 0, [&] {
        return CallThis<uint32_t>(At<uint32_t>(At<char*>(instance, 0), 0x3C), instance, scratch);
    });
    return g_bReplay ? g_dbColour : Ptr(answer);
}

inline void DbInstanceCall(char* instance, uint32_t slot, const void* data, std::size_t size,
                           const std::function<uint32_t()>& real)
{
    BOurs(kDbInstance + slot, Word(instance), Word(data), size, real);
}

// 0x005AD890 (ret 0x14), with the depth of the camera's plane, the camera,
// the particle, its place and its scale (both of which it may change): the
// answer in the low byte. The notes keep the depth, the camera, the place
// and the scale it was given.
inline bool DbInView(char* self, float depth, char* camera, char* p, float* place, float* scale)
{
    const uint32_t noted[6] = {LfBits(depth), Word(camera), LfBits(place[0]), LfBits(place[1]),
                               LfBits(place[2]), LfBits(*scale)};
    const uint32_t answer = BOurs(addr::SC4EffectB_ParticleInView, Word(p), Word(noted), sizeof noted, [&] {
        return CallThis<uint32_t>(addr::SC4EffectB_ParticleInView, self, LfBits(depth), camera, p, place, scale);
    });
    if (g_bReplay && g_dbInViewAt < g_dbInView.size()) {
        const DbLeft& left = g_dbInView[g_dbInViewAt++];
        std::memcpy(place, left.place, 12);
        std::memcpy(scale, &left.scale, 4);
    }
    return answer & 0xFF;
}

// 0x005ADD50 (slot 0x44 of the second class, ret 4), with the render
// context. The box (+0x1A0) starts empty (its low corner one, its high
// corner zero). The colour starts as the floats at 0x00B0A5AC to 0x00B0A5B4
// and one with both bytes clear, or is what the model instance (+0x220)
// answers (its slot 0x3C), with bit 2 of its first byte set for
// descriptions with flag 1. The depth of the camera's plane (the context's
// +0x24, its +0x3C to +0x44 the place, +0x54 to +0x5C the direction) is
// +0x40 over +0x58, less the place along the direction (summed z, y, x),
// plus the vector at +0x1E8's +0x30. For each particle of the list (+0x14,
// read again for every node):
// - its place, for descriptions whose +0x104 is not the double at
//   0x00A80990, turned about the y axis by its height above the effect's
//   +0xB8 times +0x104 (its x and z less +0xB4 and +0xBC, the turned
//   offsets added to its own x and z);
// - `t`, its age over its life, as a float, and a scale of the list at
//   +0xAC read there times its +0x28 times the double at 0x00AA02D8;
// - for descriptions with flag 2 of their third word, 0x005AD890 asked
//   whether it is in view; one that is not is left;
// - its block from 0x005ACA30 (a block of one on the diagonal, the place
//   zero and the scale one given), which turns it and widens the box;
// - its colour: the list of three floats at +0x94 read at `t` (the part
//   rounded to a float; the next one's first value times the part kept
//   unrounded, the rest rounded before they are added), times its +0x38 to
//   +0x40; its opacity: +0x1D0 times its +0x34 times the list at +0xA0 read
//   at `t`;
// - with an instance: the block given (slot 0x28); bit 1 of the colour's
//   first byte set unless the opacity is one and the list at +0xA0 has one
//   value or none; for a model (+0x224) of more than one frame (its slot
//   0x30) the particle's clock (+0x48) moved on by the step (+0x108, times
//   the description's +0x1DC when that is above zero) times its speed
//   (flag 0x40) or over the frames of its slot 0x44's word at +2 times the
//   float at 0x00A8FAF0, and the frame set (slot 0x28): the last for
//   flag 0x20 once the clock reached the frames, else the clock, less a
//   whole number of loops when it reached them, rounded down; for other
//   models an effect with +0x26C set has its part (+0x228) started
//   (0x00803E10 with the pointer at 0x00B43DDC and 0, flag 4) and stepped
//   (0x005AC200 with the clock +0x110 as a float, flag 0x100); then the
//   colour given (slot 0x40) and the instance drawn (slot 0x30 with the
//   context and 0);
// - for a particle whose own effect (+0x44) answers its slot 0x14 (read
//   again after): slot 0x30 with 3, the opacity and 1 for flag 0x10; slot
//   0x1C with a block of one on the diagonal at the first block's place
//   (both bytes 1) for flag 8, else slot 0x20 with the block.
// Last the context's colour is reset (0x007D3290), and the box, when not
// empty, grown on every side by the first float of the list at +0xAC times
// +0x274 times the double at 0x00AA02D8.
inline void DrawParticlesB(char* self, char* context)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    for (uint32_t i = 0; i < 3; ++i) {
        At<uint32_t>(self, 0x1A0 + i * 4) = 0x3F800000u;
        At<uint32_t>(self, 0x1AC + i * 4) = 0;
    }
    char colour[kDbColour] = {};
    At<uint32_t>(colour, 4) = M<uint32_t>(0x00B0A5AC);
    At<uint32_t>(colour, 8) = M<uint32_t>(0x00B0A5B0);
    At<uint32_t>(colour, 0xC) = M<uint32_t>(0x00B0A5B4);
    At<uint32_t>(colour, 0x10) = 0x3F800000u;
    if (char* const instance = At<char*>(self, 0x220)) {
        char scratch[kDbColour];
        std::memcpy(colour, DbInstanceColour(instance, scratch), kDbColour);
        if (At<uint8_t>(At<char*>(self, 0x24), 0) & 1)
            At<uint8_t>(colour, 0) |= 4;
    }
    char* description = At<char*>(self, 0x24);
    const int32_t lastColour = static_cast<int32_t>(At<uint32_t>(description, 0x98) - At<uint32_t>(description, 0x94)) / 12 - 1;
    char* const camera = At<char*>(context, 0x24);
    const auto c = [&](uint32_t at) { return static_cast<double>(At<float>(camera, at)); };
    const float depth = static_cast<float>(
        (c(0x40) / c(0x58) - ((c(0x44) * c(0x5C) + c(0x40) * c(0x58)) + c(0x3C) * c(0x54))) +
        At<float>(At<char*>(self, 0x1E8), 0x30));
    for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14); node = At<char*>(node, 0)) {
        char* const p = At<char*>(node, 8);
        float place[3] = {At<float>(p, 0xC), At<float>(p, 0x10), At<float>(p, 0x14)};
        description = At<char*>(self, 0x24);
        const double spin = At<float>(description, 0x104);
        if (!(spin == M<double>(0x00A80990))) {
            const float dx = static_cast<float>(static_cast<double>(place[0]) - f(0xB4));
            const double dy = static_cast<double>(place[1]) - f(0xB8);
            const float dz = static_cast<float>(static_cast<double>(place[2]) - f(0xBC));
            SdX87 sine, cosine;
            SdSinCos(dy * spin, &sine, &cosine);
            place[0] = static_cast<float>((SdTimes(cosine, dx) - SdTimes(sine, dz)) + place[0]);
            place[2] = static_cast<float>((SdTimes(cosine, dz) + SdTimes(sine, dx)) + place[2]);
        }
        const float t = static_cast<float>(static_cast<double>(At<float>(p, 4)) / At<float>(p, 8));
        float scale = static_cast<float>(SdCurve(description, 0xAC, t) * At<float>(p, 0x28) * M<double>(0x00AA02D8));
        if ((At<uint32_t>(description, 8) >> 1 & 1) && !DbInView(self, depth, camera, p, place, &scale))
            continue;
        char block[0x38] = {};
        for (const uint32_t at : {0x4u, 0x14u, 0x24u, 0x34u})
            At<uint32_t>(block, at) = 0x3F800000u;
        CallThis<void>(addr::SC4EffectB_ParticleBlock, self, p, LfBits(t), LfBits(scale), place, block);
        description = At<char*>(self, 0x24);
        const float* const colours = At<const float*>(description, 0x94);
        double red, green;
        float blue;
        if (lastColour == 0) {
            red = colours[0];
            green = colours[1];
            blue = colours[2];
        } else {
            const double x = LfUnsignedF(static_cast<uint32_t>(lastColour)) * t;
            const uint32_t i = SdRoundDown(x);
            const float part = static_cast<float>(x - LfUnsignedF(i));
            const float* const e = colours + i * 3;
            if (part > M<float>(0x00A81054)) {
                const double nextRed = static_cast<double>(part) * e[3];
                const float nextGreen = static_cast<float>(static_cast<double>(part) * e[4]);
                const float nextBlue = static_cast<float>(static_cast<double>(part) * e[5]);
                const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
                const float restRed = static_cast<float>(rest * e[0]);
                const float restGreen = static_cast<float>(rest * e[1]);
                const float restBlue = static_cast<float>(rest * e[2]);
                red = nextRed + restRed;
                green = static_cast<double>(restGreen) + nextGreen;
                blue = static_cast<float>(static_cast<double>(restBlue) + nextBlue);
            } else {
                red = e[0];
                green = e[1];
                blue = e[2];
            }
        }
        const float r = static_cast<float>(red * At<float>(p, 0x38));
        const float g = static_cast<float>(green * At<float>(p, 0x3C));
        const float b = static_cast<float>(static_cast<double>(blue) * At<float>(p, 0x40));
        float opacity = static_cast<float>(f(0x1D0) * At<float>(p, 0x34) * SdCurve(description, 0xA0, t));
        if (char* const instance = At<char*>(self, 0x220)) {
            DbInstanceCall(instance, 0x28, block, kBBlock,
                           [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(instance, 0), 0x28), instance, block); });
            char* const d = At<char*>(self, 0x24);
            if (opacity == M<float>(0x00A81228) &&
                static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(d, 0xA4) - At<uint32_t>(d, 0xA0)) >> 2) <= 1)
                At<uint8_t>(colour, 0) &= 0xFD;
            else
                At<uint8_t>(colour, 0) |= 2;
            At<float>(colour, 0x10) = opacity;
            char* const model = At<char*>(self, 0x224);
            if (model && SuSlot<int32_t>(model, 0x30) > 1) {
                float step = At<float>(self, 0x108);
                char* const d2 = At<char*>(self, 0x24);
                if (At<float>(d2, 0x1DC) > M<float>(0x00A81054))
                    step = static_cast<float>(static_cast<double>(step) * At<float>(d2, 0x1DC));
                double moved;
                if (At<uint32_t>(d2, 8) >> 6 & 1) {
                    const double x = At<float>(p, 0x1C), y = At<float>(p, 0x20), z = At<float>(p, 0x24);
                    moved = std::sqrt((x * x + y * y) + z * z) * step;
                } else {
                    const uint16_t frames = At<uint16_t>(SuSlot<char*>(model, 0x44), 2);
                    moved = static_cast<double>(step) / frames * M<float>(0x00A8FAF0);
                }
                At<float>(p, 0x48) = static_cast<float>(moved + At<float>(p, 0x48));
                const int32_t count = SuSlot<int32_t>(model, 0x30);
                uint32_t frame;
                if ((At<uint32_t>(At<char*>(self, 0x24), 8) >> 5 & 1) &&
                    static_cast<double>(count) <= At<float>(p, 0x48)) {
                    frame = static_cast<uint32_t>(count - 1);
                } else {
                    if (static_cast<double>(count) <= At<float>(p, 0x48)) {
                        const uint32_t loops = SdRoundDown(At<float>(p, 0x48) / static_cast<double>(count));
                        At<float>(p, 0x48) = static_cast<float>(
                            At<float>(p, 0x48) - static_cast<double>(static_cast<int32_t>(loops * count)));
                    }
                    frame = SdRoundDown(At<float>(p, 0x48));
                }
                BOurs(kDbModel + 0x28, Word(model), Word(&frame), 4,
                      [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(model, 0), 0x28), model, frame); });
            } else if (At<uint8_t>(self, 0x26C)) {
                char* const part = self + 0x228;
                if (At<uint32_t>(At<char*>(self, 0x24), 8) >> 2 & 1) {
                    const uint32_t args[2] = {M<uint32_t>(0x00B43DDC), 0};
                    BOurs(addr::SC4EffectPartB_Start, Word(part), Word(args), sizeof args,
                          [&] { return CallThis<uint32_t>(addr::SC4EffectPartB_Start, part, args[0], args[1]); });
                }
                if (At<uint32_t>(At<char*>(self, 0x24), 8) >> 8 & 1) {
                    const uint32_t clock = LfBits(static_cast<float>(At<double>(self, 0x110)));
                    BOurs(addr::SC4EffectPartB_Step, Word(part), Word(&clock), 4,
                          [&] { return CallThis<uint32_t>(addr::SC4EffectPartB_Step, part, clock); });
                }
            }
            At<float>(colour, 4) = r;
            At<float>(colour, 8) = g;
            At<float>(colour, 0xC) = b;
            char* const shown = At<char*>(self, 0x220);
            DbInstanceCall(shown, 0x40, colour, kDbColour,
                           [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(shown, 0), 0x40), shown, colour); });
            const uint32_t drawn[2] = {Word(context), 0};
            DbInstanceCall(shown, 0x30, drawn, sizeof drawn, [&] {
                return CallThis<uint32_t>(At<uint32_t>(At<char*>(shown, 0), 0x30), shown, context, 0u);
            });
        }
        char* const own = At<char*>(p, 0x44);
        if (!own || !SdOwnAsks(own))
            continue;
        char* const mine = At<char*>(p, 0x44);
        if (At<uint32_t>(At<char*>(self, 0x24), 8) >> 4 & 1) {
            uint32_t value = LfBits(opacity);
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
    }
    BOurs(addr::SC4RenderContext_ResetColour, Word(context), 0, 0,
          [&] { return CallThis<uint32_t>(addr::SC4RenderContext_ResetColour, context); });
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

// --- The base class's two particle draws -----------------------------------------------------

// What the notes call the calls on the dynamic vertex buffer (+0x1EC):
// 0xE500 and the slot.
constexpr uint32_t kBfBuffer = 0xE500;

// The vertices handed over (the game's run: from the locked memory at the
// unlock, or the batch's at its draw; the replay: from the memory ours was
// lent, or the batch's), each run one after another; and the lent memory.
std::vector<char> g_bfGameVertices, g_bfOurVertices, g_bfLent;

// The buffer's slot 0x18 (0 and so many vertices): the memory to write
// them to. The replay lends ours memory of its own.
inline char* BfLock(char* buffer, uint32_t vertices)
{
    const uint32_t noted[2] = {0, vertices};
    const uint32_t answer = BOurs(kBfBuffer + 0x18, Word(buffer), Word(noted), sizeof noted, [&] {
        return CallThis<uint32_t>(At<uint32_t>(At<char*>(buffer, 0), 0x18), buffer, 0u, vertices);
    });
    if (!g_bReplay)
        return Ptr(answer);
    g_bfLent.assign(static_cast<std::size_t>(vertices) * 0x20, 0);
    return answer ? g_bfLent.data() : nullptr;
}

// What the draw writes of so many 0x20-byte vertices (the first 0x18 bytes
// of each), kept for the comparison's notes.
inline void BfKeep(const char* vertices, std::size_t count, std::vector<char>& into)
{
    for (std::size_t i = 0; i < count; ++i)
        into.insert(into.end(), vertices + i * 0x20, vertices + i * 0x20 + 0x18);
}

// Slot 0x20 (0): the vertices handed over.
inline void BfUnlock(char* buffer)
{
    const uint32_t zero = 0;
    BOurs(kBfBuffer + 0x20, Word(buffer), Word(&zero), 4,
          [&] { return CallThis<uint32_t>(At<uint32_t>(At<char*>(buffer, 0), 0x20), buffer, 0u); });
    if (g_bReplay)
        BfKeep(g_bfLent.data(), g_bfLent.size() / 0x20, g_bfOurVertices);
}

// The colour of a vertex from a part of one (0 to 1): the float at
// 0x00AA0CFC added, the bits less those of that float, then less themselves
// over 256, the low byte kept (one becomes 255).
inline uint8_t BfColourByte(float value)
{
    const uint32_t bits = LfBits(static_cast<float>(static_cast<double>(value) + M<float>(0x00AA0CFC))) + 0xB8C00000u;
    return static_cast<uint8_t>(bits - (bits >> 8));
}

// A list of floats of the description (begin at `at`) read at `t` as the
// buffered draw reads it: its one value, or the place (the last index
// times `t`) kept as a float for the test of the rounding down and the
// part; the part is tested unrounded and multiplied rounded to a float.
// The answer is a float.
inline float BfCurve(char* description, uint32_t at, float t)
{
    const float* const values = At<const float*>(description, at);
    const int32_t last =
        (static_cast<int32_t>(At<uint32_t>(description, at + 4) - At<uint32_t>(description, at)) >> 2) - 1;
    if (last == 0)
        return values[0];
    const double x = LfUnsignedF(static_cast<uint32_t>(last)) * t;
    const float place = static_cast<float>(x);
    uint32_t i = LfBits(static_cast<float>((x - M<float>(0x00A84D2C)) + M<float>(0x00AA0CF8))) + 0xB4C00000u;
    if (static_cast<double>(place) == static_cast<double>(static_cast<int32_t>(i + 1)))
        i += 1;
    const double part = static_cast<double>(place) - LfUnsignedF(i);
    if (part > M<float>(0x00A81054))
        return static_cast<float>((static_cast<double>(values[i + 1]) - values[i]) * static_cast<float>(part) +
                                  values[i]);
    return values[i];
}

// The same list read as the other draw reads it: `t` and the place
// unrounded (SdCurve with the float at 0x00AA0CF8 for the rounding down).
inline double PlCurve(char* description, uint32_t at, double t)
{
    const float* const values = At<const float*>(description, at);
    const int32_t last =
        (static_cast<int32_t>(At<uint32_t>(description, at + 4) - At<uint32_t>(description, at)) >> 2) - 1;
    if (last == 0)
        return values[0];
    const double x = LfUnsignedF(static_cast<uint32_t>(last)) * t;
    const uint32_t i = LfRoundDown(x);
    const double part = x - LfUnsignedF(i);
    if (part > M<float>(0x00A81054))
        return (static_cast<double>(values[i + 1]) - values[i]) * part + values[i];
    return values[i];
}

// A box (+0x1A0 to +0x1B4) widened by a place: made of it when empty (its
// low x above its high x).
inline void BfWiden(char* self, const float* place)
{
    if (At<float>(self, 0x1A0) > At<float>(self, 0x1AC)) {
        std::memcpy(self + 0x1AC, place, 12);
        std::memcpy(self + 0x1A0, place, 12);
        return;
    }
    for (uint32_t i = 0; i < 3; ++i) {
        if (!(At<float>(self, 0x1AC + i * 4) > place[i]))
            At<float>(self, 0x1AC + i * 4) = place[i];
        if (!(At<float>(self, 0x1A0 + i * 4) < place[i]))
            At<float>(self, 0x1A0 + i * 4) = place[i];
    }
}

// The box, when not empty, grown on every side by the first float of the
// description's list at +0xAC.
inline void BfGrow(char* self)
{
    if (At<float>(self, 0x1A0) > At<float>(self, 0x1AC))
        return;
    const double grow = *At<float*>(At<char*>(self, 0x24), 0xAC);
    for (uint32_t i = 0; i < 3; ++i) {
        At<float>(self, 0x1A0 + i * 4) = static_cast<float>(static_cast<double>(At<float>(self, 0x1A0 + i * 4)) - grow);
        At<float>(self, 0x1AC + i * 4) = static_cast<float>(grow + At<float>(self, 0x1AC + i * 4));
    }
}

// The sine and the cosine of `angle`, each rounded to a float.
inline void BfSinCos(double angle, float* sine, float* cosine)
{
    SdX87 s, c;
    SdSinCos(angle, &s, &c);
    *sine = SdFloat(s);
    *cosine = SdFloat(c);
}

// The colour of a particle from the description's list of three floats at
// +0x94 read at `t` (as the second class's draw reads it; the place
// unrounded, the part rounded to a float): the next value's first part
// unrounded, the rest's parts rounded before they are added. The red and
// green are left unrounded.
inline void PlColour(char* description, int32_t last, double t, double* red, double* green, float* blue)
{
    const float* const colours = At<const float*>(description, 0x94);
    if (last == 0) {
        *red = colours[0];
        *green = colours[1];
        *blue = colours[2];
        return;
    }
    const double x = LfUnsignedF(static_cast<uint32_t>(last)) * t;
    const uint32_t i = LfRoundDown(x);
    const float part = static_cast<float>(x - LfUnsignedF(i));
    const float* const e = colours + i * 3;
    if (!(part > M<float>(0x00A81054))) {
        *red = e[0];
        *green = e[1];
        *blue = e[2];
        return;
    }
    const double nextRed = static_cast<double>(part) * e[3];
    const float nextGreen = static_cast<float>(static_cast<double>(part) * e[4]);
    const float nextBlue = static_cast<float>(static_cast<double>(part) * e[5]);
    const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
    const float restRed = static_cast<float>(rest * e[0]);
    const float restGreen = static_cast<float>(rest * e[1]);
    const float restBlue = static_cast<float>(rest * e[2]);
    *red = nextRed + restRed;
    *green = static_cast<double>(restGreen) + nextGreen;
    *blue = static_cast<float>(static_cast<double>(restBlue) + nextBlue);
}

// 0x005AC070 for a description with a list at +0x108: the place moved,
// with the description's +0x14 times the effect's double at +0x120.
inline void PlMovePlace(char* self, float* place)
{
    char* const description = At<char*>(self, 0x24);
    if (At<uint32_t>(description, 0x108) == At<uint32_t>(description, 0x10C))
        return;
    char* const d = At<char*>(self, 0x24);
    SdMovePlace(static_cast<float>(static_cast<double>(At<float>(d, 0x14)) * At<double>(self, 0x120)), d + 0x108,
                place);
}

// 0x005B4B90 (slot 0x48 of both classes, ret 4), with the render context:
// the particles drawn into the dynamic vertex buffer (+0x1EC). Nothing
// without a texture (+0x68). The texture is bound (0x007D4070 with 0) and
// the box (+0x1A0) started empty. The corners' texture places are (0, 0),
// (0, 1), (1, 1) and (1, 0), the second value of each turned over for flag
// 0x40000000 of the description's first word and the first value of the
// first and third for flag 0x20000000. The count (+0x18) is drawn in runs
// of at most the buffer's vertices (its slot 0x14 with 0) over four: the
// batch (+0x28) emptied (0x005EC4A0) and given the run's quads
// (0x005EC850), the buffer locked for +0x30 times the run's vertices (slot
// 0x18 with 0; none, and the draw ends there), a quad of four 0x20-byte
// vertices (place, colour, texture place; the last eight bytes left as
// they were) written for each particle of the list, which is followed for
// the count without looking for its end, the buffer unlocked (slot 0x20
// with 0) and drawn (slot 0x28 with two zeros, the batch's index count
// +0x38 and its indices +0x2C). Last the box, when not empty, is grown by
// the first float of the list at +0xAC. The game guards the runs with an
// exception handler that unlocks and resets the buffer after a fault; ours
// has none.
//
// A particle's quad: with `t` its age over its life as a float, the
// description's lists read at `t` (BfCurve) give its colour (the list of
// three floats at +0x94, the part rounded to a float, the sum of each
// value's two parts rounded; the rest less the part rounded to a float for
// the red and green, not for the blue) times its +0x38 to +0x40, its
// opacity (its +0x34 times +0x1D0 times the list at +0xA0), a spread (the
// list at +0xB8 times its +0x2C), a size (the list at +0xAC times its
// +0x28, at most the vector at +0x1E8's +0x34) and a turn (the list at
// +0xC4 times its +0x30 plus the description's +0x74). Its place (+0xC) is
// turned about the y axis around the effect's +0xB4 when the description's
// +0x104 is not zero (by the height above +0xB8 times +0x104; the sine and
// cosine rounded to floats), moved by 0x005AC070 when the list at +0x108
// is not empty. The axes are the effect's (+0x7C, +0x70): when the
// description's +0xDC is not zero they are turned to face along the
// particle's velocity (+0x1C) (its length worked out from the second
// axis's part unrounded times rounded, the first's part over it
// unrounded, the second's over it rounded) and the first one stretched by
// that length over +0xDC when that is above it. The first is scaled by the
// size, the second by the size times the spread (unrounded for its x);
// both are turned by the turn times the float at 0x00AA0D04 (2 pi); for an
// effect whose second block has its second byte set (+0xC5) the place is
// taken through that block (its rows at +0xC8, scaled by +0xF8, plus its
// place +0xEC) and the axes scaled by +0xF8. The quad's corner is the
// place less half the two axes' sum (the float at 0x00A84D2C): its
// vertices the corner plus the first axis, the corner, the corner plus the
// second, and that plus the first. The box is widened by the place.
inline void DrawParticlesBuffered(char* self, char* context)
{
    const auto f = [](double value) { return static_cast<float>(value); };
    if (!At<uint32_t>(self, 0x68))
        return;
    BOurs(addr::SC4RenderContext_BindTexture, Word(context), Word(self + 0x68), 4, [&] {
        return CallThis<uint32_t>(addr::SC4RenderContext_BindTexture, context, At<uint32_t>(self, 0x68), 0u);
    });
    for (uint32_t i = 0; i < 3; ++i) {
        At<uint32_t>(self, 0x1A0 + i * 4) = 0x3F800000u;
        At<uint32_t>(self, 0x1AC + i * 4) = 0;
    }
    float uv[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 30 & 1) {
        uv[0][1] = 1.0f;
        uv[1][1] = 0.0f;
        uv[2][1] = 0.0f;
        uv[3][1] = 1.0f;
    }
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 29 & 1) {
        uv[0][0] = 1.0f;
        uv[1][0] = 0.0f;
        uv[2][0] = 0.0f;
        uv[3][0] = 1.0f;
    }
    int32_t left = At<int32_t>(self, 0x18);
    const int32_t room = static_cast<int32_t>(SuSlot<uint32_t>(At<char*>(self, 0x1EC), 0x14, 0u) >> 2);
    char* description = At<char*>(self, 0x24);
    const int32_t lastColour =
        static_cast<int32_t>(At<uint32_t>(description, 0x98) - At<uint32_t>(description, 0x94)) / 12 - 1;
    char* node = At<char*>(At<char*>(self, 0x14), 0);
    while (left > 0) {
        const uint32_t run = static_cast<uint32_t>(room < left ? room : left);
        left -= static_cast<int32_t>(run);
        char* const batch = self + 0x28;
        IbClearFor(batch);
        IbAddQuadsFor(batch, run);
        char* out = BfLock(At<char*>(self, 0x1EC), At<uint32_t>(self, 0x30) * run);
        if (!out)
            return;
        for (uint32_t n = 0; n < run; ++n) {
            char* const p = At<char*>(node, 8);
            const float t = f(static_cast<double>(At<float>(p, 4)) / At<float>(p, 8));
            description = At<char*>(self, 0x24);
            const float* const colours = At<const float*>(description, 0x94);
            float red, green, blue;
            if (lastColour == 0) {
                red = colours[0];
                green = colours[1];
                blue = colours[2];
            } else {
                const double x = LfUnsignedF(static_cast<uint32_t>(lastColour)) * t;
                const float place = f(x);
                uint32_t i = LfBits(f((x - M<float>(0x00A84D2C)) + M<float>(0x00AA0CF8))) + 0xB4C00000u;
                if (static_cast<double>(place) == static_cast<double>(static_cast<int32_t>(i + 1)))
                    i += 1;
                const double between = static_cast<double>(place) - LfUnsignedF(i);
                const float part = f(between);
                const float* const e = colours + i * 3;
                if (between > M<float>(0x00A81054)) {
                    const float nextBlue = f(static_cast<double>(part) * e[5]);
                    const float nextGreen = f(static_cast<double>(part) * e[4]);
                    const float nextRed = f(static_cast<double>(part) * e[3]);
                    const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
                    const float restRounded = f(rest);
                    const float restBlue = f(rest * e[2]);
                    const float restGreen = f(static_cast<double>(restRounded) * e[1]);
                    const float restRed = f(static_cast<double>(restRounded) * e[0]);
                    blue = f(static_cast<double>(restBlue) + nextBlue);
                    green = f(static_cast<double>(restGreen) + nextGreen);
                    red = f(static_cast<double>(restRed) + nextRed);
                } else {
                    red = e[0];
                    green = e[1];
                    blue = e[2];
                }
            }
            const float b = f(static_cast<double>(blue) * At<float>(p, 0x40));
            const float g = f(static_cast<double>(green) * At<float>(p, 0x3C));
            const float r = f(static_cast<double>(red) * At<float>(p, 0x38));
            const float opacity =
                f(static_cast<double>(At<float>(p, 0x34)) * At<float>(self, 0x1D0) * BfCurve(description, 0xA0, t));
            const float spread = f(static_cast<double>(BfCurve(description, 0xB8, t)) * At<float>(p, 0x2C));
            float size = f(static_cast<double>(BfCurve(description, 0xAC, t)) * At<float>(p, 0x28));
            const float turn = f(static_cast<double>(BfCurve(description, 0xC4, t)) * At<float>(p, 0x30) +
                                 At<float>(description, 0x74));
            float place[3] = {At<float>(p, 0xC), At<float>(p, 0x10), At<float>(p, 0x14)};
            if (!(static_cast<double>(At<float>(description, 0x104)) == M<double>(0x00A80990))) {
                const float pivot[3] = {At<float>(self, 0xB4), At<float>(self, 0xB8), At<float>(self, 0xBC)};
                const float dz = f(static_cast<double>(place[2]) - pivot[2]);
                const float dy = f(static_cast<double>(place[1]) - pivot[1]);
                const float dx = f(static_cast<double>(place[0]) - pivot[0]);
                float sine, cosine;
                BfSinCos(static_cast<double>(dy) * At<float>(description, 0x104), &sine, &cosine);
                place[0] = f((static_cast<double>(dx) * cosine - static_cast<double>(dz) * sine) + pivot[0]);
                place[2] = f((static_cast<double>(dx) * sine + static_cast<double>(dz) * cosine) + pivot[2]);
            }
            PlMovePlace(self, place);
            float a[3] = {At<float>(self, 0x7C), At<float>(self, 0x80), At<float>(self, 0x84)};
            float s[3] = {At<float>(self, 0x70), At<float>(self, 0x74), At<float>(self, 0x78)};
            if (!(static_cast<double>(At<float>(description, 0xDC)) == M<double>(0x00A80990))) {
                const double vx = At<float>(p, 0x1C), vy = At<float>(p, 0x20), vz = At<float>(p, 0x24);
                const float along = f((vz * a[2] + vy * a[1]) + vx * a[0]);
                const double across = (vz * s[2] + vy * s[1]) + static_cast<double>(s[0]) * vx;
                const float acrossRounded = f(across);
                const double length = std::sqrt(across * acrossRounded + static_cast<double>(along) * along);
                const float lengthRounded = f(length);
                const double inverse = M<double>(0x00A80AB0) / length;
                const double k1 = f(inverse * along);
                const double k2 = f(static_cast<double>(f(inverse)) * acrossRounded);
                float na[3], ns[3];
                for (int i = 0; i < 3; ++i) {
                    na[i] = f(static_cast<double>(f(k1 * a[i])) + f(k2 * s[i]));
                    ns[i] = f(static_cast<double>(f(k1 * s[i])) - f(k2 * a[i]));
                }
                std::memcpy(a, na, sizeof a);
                std::memcpy(s, ns, sizeof s);
                if (lengthRounded > At<float>(description, 0xDC)) {
                    const double stretch = f(static_cast<double>(lengthRounded) / At<float>(description, 0xDC));
                    for (float& value : a)
                        value = f(value * stretch);
                }
            }
            if (size > At<float>(At<char*>(self, 0x1E8), 0x34))
                size = At<float>(At<char*>(self, 0x1E8), 0x34);
            for (float& value : a)
                value = f(static_cast<double>(size) * value);
            const double wide = static_cast<double>(size) * spread;
            s[0] = f(wide * s[0]);
            s[1] = f(static_cast<double>(s[1]) * f(wide));
            s[2] = f(static_cast<double>(s[2]) * f(wide));
            if (!(static_cast<double>(turn) == M<double>(0x00A80990))) {
                float sine, cosine;
                BfSinCos(static_cast<double>(turn) * M<float>(0x00AA0D04), &sine, &cosine);
                float na[3], ns[3];
                for (int i = 0; i < 3; ++i) {
                    na[i] = f(static_cast<double>(f(static_cast<double>(a[i]) * cosine)) -
                              f(static_cast<double>(s[i]) * sine));
                    ns[i] = f(static_cast<double>(f(static_cast<double>(s[i]) * cosine)) +
                              f(static_cast<double>(a[i]) * sine));
                }
                std::memcpy(a, na, sizeof a);
                std::memcpy(s, ns, sizeof s);
            }
            if (At<uint8_t>(self, 0xC5)) {
                const auto row = [&](uint32_t at) {
                    return f((static_cast<double>(place[1]) * At<float>(self, at + 4) +
                              static_cast<double>(place[2]) * At<float>(self, at + 8)) +
                             static_cast<double>(place[0]) * At<float>(self, at));
                };
                const float moved[3] = {row(0xC8), row(0xD4), row(0xE0)};
                const double scale = At<float>(self, 0xF8);
                for (int i = 0; i < 3; ++i)
                    place[i] = f(static_cast<double>(f(moved[i] * scale)) + At<float>(self, 0xEC + i * 4));
                for (int i = 0; i < 3; ++i) {
                    a[i] = f(scale * a[i]);
                    s[i] = f(scale * s[i]);
                }
            }
            float corner[3], second[3], first[3], last[3];
            for (int i = 0; i < 3; ++i)
                corner[i] = f(static_cast<double>(place[i]) -
                              f(static_cast<double>(f(static_cast<double>(s[i]) + a[i])) * M<float>(0x00A84D2C)));
            const uint32_t colour = BfColourByte(b) | BfColourByte(g) << 8 | BfColourByte(r) << 16 |
                                    static_cast<uint32_t>(BfColourByte(opacity)) << 24;
            BfWiden(self, place);
            for (int i = 0; i < 3; ++i) {
                first[i] = f(static_cast<double>(corner[i]) + a[i]);
                second[i] = f(static_cast<double>(corner[i]) + s[i]);
                last[i] = f(static_cast<double>(second[i]) + a[i]);
            }
            const float* const places[4] = {first, corner, second, last};
            for (int v = 0; v < 4; ++v) {
                std::memcpy(out, places[v], 12);
                At<uint32_t>(out, 0xC) = colour;
                std::memcpy(out + 0x10, uv[v], 8);
                out += 0x20;
            }
            node = At<char*>(node, 0);
        }
        BfUnlock(At<char*>(self, 0x1EC));
        char* const drawn = At<char*>(self, 0x1EC);
        const uint32_t indices = At<uint32_t>(batch, 0x2C), count = At<uint32_t>(batch, 0x38);
        const uint32_t noted[4] = {0, 0, count, indices};
        BOurs(kBfBuffer + 0x28, Word(drawn), Word(noted), sizeof noted, [&] {
            return CallThis<uint32_t>(At<uint32_t>(At<char*>(drawn, 0), 0x28), drawn, 0u, 0u, count, indices);
        });
    }
    BfGrow(self);
}

// The batch's vertices as they are (its count +0x24 times its size +0x20,
// from +0x14), for the comparison's notes.
inline void PlKeepVertices(char* batch, std::vector<char>& into)
{
    const char* const begin = At<char*>(batch, 0x14);
    into.insert(into.end(), begin, begin + static_cast<std::size_t>(At<uint32_t>(batch, 0x24)) * At<uint32_t>(batch, 0x20));
}

// 0x005B3DA0 (slot 0x44 of the base class, ret 4), with the render
// context: the particles drawn as quads of the batch (+0x28). Nothing
// without a texture (+0x68). The box (+0x1A0) is started empty, the batch
// emptied (0x005EC4A0) and given the vertices (0x005EC8C0) and the
// indices (0x005EC850) of a quad for each of the count (+0x18). Each
// particle of the list (+0x14, read again for every node) is worked out as
// in the buffered draw, but for the arithmetic, which keeps more of it on
// the x87: `t` and the place where the lists are read unrounded (PlCurve;
// the colour as the second class's draw reads it, PlColour); the place
// turned with the cosine rounded and the sine not; the first axis's x
// kept unrounded through the velocity, the size, the spread, the turn and
// the second block, and the second axis's x through the velocity and the
// turn; the spread's product and the second block's rows (but the z) and
// halves (but the x) unrounded. The box is widened by the place. A quad
// whose opacity as a byte is below the double at 0x00A92688 is not
// written; the others are written one after another as four 0x18-byte
// vertices (place and colour; the texture places left as they were). Last,
// with vertices in the batch (+0x24), the texture is bound (0x007D4070
// with 0) and the batch drawn (0x005EC5D0 with the context), and the box
// grown as the buffered draw's is.
inline void DrawParticlesPlain(char* self, char* context)
{
    const auto f = [](double value) { return static_cast<float>(value); };
    if (!At<uint32_t>(self, 0x68))
        return;
    for (uint32_t i = 0; i < 3; ++i) {
        At<uint32_t>(self, 0x1A0 + i * 4) = 0x3F800000u;
        At<uint32_t>(self, 0x1AC + i * 4) = 0;
    }
    char* const batch = self + 0x28;
    IbClearFor(batch);
    char* out = Ptr(IbAddVerticesFor(batch, At<uint32_t>(self, 0x18)));
    IbAddQuadsFor(batch, At<uint32_t>(self, 0x18));
    char* description = At<char*>(self, 0x24);
    const int32_t lastColour =
        static_cast<int32_t>(At<uint32_t>(description, 0x98) - At<uint32_t>(description, 0x94)) / 12 - 1;
    for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14); node = At<char*>(node, 0)) {
        char* const p = At<char*>(node, 8);
        const double t = static_cast<double>(At<float>(p, 4)) / At<float>(p, 8);
        description = At<char*>(self, 0x24);
        double red, green;
        float blue;
        PlColour(description, lastColour, t, &red, &green, &blue);
        const float r = f(red * At<float>(p, 0x38));
        const float g = f(green * At<float>(p, 0x3C));
        const float b = f(static_cast<double>(blue) * At<float>(p, 0x40));
        const float opacity =
            f(static_cast<double>(At<float>(p, 0x34)) * At<float>(self, 0x1D0) * PlCurve(description, 0xA0, t));
        const double spread = f(PlCurve(description, 0xB8, t) * At<float>(p, 0x2C));
        float size = f(PlCurve(description, 0xAC, t) * At<float>(p, 0x28));
        const float turn = f(PlCurve(description, 0xC4, t) * At<float>(p, 0x30) + At<float>(description, 0x74));
        float place[3] = {At<float>(p, 0xC), At<float>(p, 0x10), At<float>(p, 0x14)};
        if (!(static_cast<double>(At<float>(description, 0x104)) == M<double>(0x00A80990))) {
            const double px = At<float>(self, 0xB4), py = At<float>(self, 0xB8), pz = At<float>(self, 0xBC);
            const float dx = f(place[0] - px);
            const double dy = place[1] - py;
            const float dz = f(place[2] - pz);
            SdX87 sine, cosine;
            SdSinCos(dy * At<float>(description, 0x104), &sine, &cosine);
            const double c = SdFloat(cosine);
            place[0] = f((dx * c - SdTimes(sine, dz)) + px);
            place[2] = f((SdTimes(sine, dx) + dz * c) + pz);
        }
        PlMovePlace(self, place);
        double ax = At<float>(self, 0x7C);
        float ay = At<float>(self, 0x80), az = At<float>(self, 0x84);
        float sx = At<float>(self, 0x70), sy = At<float>(self, 0x74), sz = At<float>(self, 0x78);
        if (!(static_cast<double>(At<float>(description, 0xDC)) == M<double>(0x00A80990))) {
            const double vx = At<float>(p, 0x1C), vy = At<float>(p, 0x20), vz = At<float>(p, 0x24);
            const double along = (vz * At<float>(self, 0x84) + vy * At<float>(self, 0x80)) + vx * At<float>(self, 0x7C);
            const double across =
                (vz * At<float>(self, 0x78) + vy * At<float>(self, 0x74)) + vx * At<float>(self, 0x70);
            const double length = std::sqrt(across * across + along * along);
            float lengthRounded = f(length);
            const double inverse = M<double>(0x00A80AB0) / length;
            const double k1 = f(inverse * along), k2 = f(inverse * across);
            const float nax = f(k1 * ax + k2 * sx);
            const float nay = f(static_cast<double>(f(ay * k1)) + sy * k2);
            const float naz = f(static_cast<double>(f(az * k1)) + f(sz * k2));
            const float nsx = f(k1 * sx - ax * k2);
            const float nsy = f(static_cast<double>(f(sy * k1)) - f(ay * k2));
            const float nsz = f(static_cast<double>(f(sz * k1)) - f(az * k2));
            sx = nsx, sy = nsy, sz = nsz;
            ax = nax, ay = nay, az = naz;
            if (lengthRounded > At<float>(description, 0xDC)) {
                lengthRounded = f(static_cast<double>(lengthRounded) / At<float>(description, 0xDC));
                ax = nax * static_cast<double>(lengthRounded);
                ay = f(static_cast<double>(nay) * lengthRounded);
                az = f(static_cast<double>(naz) * lengthRounded);
            }
        }
        if (size > At<float>(At<char*>(self, 0x1E8), 0x34))
            size = At<float>(At<char*>(self, 0x1E8), 0x34);
        ax *= size;
        ay = f(static_cast<double>(ay) * size);
        az = f(static_cast<double>(az) * size);
        const double wide = size * spread;
        sx = f(sx * wide);
        sy = f(sy * wide);
        sz = f(sz * wide);
        if (!(static_cast<double>(turn) == M<double>(0x00A80990))) {
            float sinf, cosf;
            BfSinCos(static_cast<double>(turn) * M<float>(0x00AA0D04), &sinf, &cosf);
            const double sn = sinf, cs = cosf;
            const float nax = f(cs * ax - sn * sx);
            const float nay = f(static_cast<double>(f(ay * cs)) - sy * sn);
            const float naz = f(static_cast<double>(f(az * cs)) - f(sz * sn));
            const float nsx = f(cs * sx + ax * sn);
            const float nsy = f(static_cast<double>(f(sy * cs)) + f(ay * sn));
            const float nsz = f(static_cast<double>(f(sz * cs)) + f(az * sn));
            sx = nsx, sy = nsy, sz = nsz;
            ax = nax, ay = nay, az = naz;
        }
        if (At<uint8_t>(self, 0xC5)) {
            const double x = place[0], y = place[1], z = place[2];
            const auto e = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
            const double mx = (z * e(0xD0) + y * e(0xCC)) + x * e(0xC8);
            const double my = (z * e(0xDC) + y * e(0xD8)) + x * e(0xD4);
            const float mz = f((z * e(0xE8) + y * e(0xE4)) + x * e(0xE0));
            const double scale = e(0xF8);
            place[0] = f(static_cast<double>(f(mx * scale)) + e(0xEC));
            place[1] = f(my * scale + e(0xF0));
            place[2] = f(mz * scale + e(0xF4));
            ax *= scale;
            ay = f(ay * scale);
            az = f(az * scale);
            sx = f(sx * scale);
            sy = f(sy * scale);
            sz = f(sz * scale);
        }
        const double half = M<float>(0x00A84D2C);
        const float hx = f((sx + ax) * half);
        const double hy = (static_cast<double>(sy) + ay) * half;
        const double hz = static_cast<double>(f(static_cast<double>(sz) + az)) * half;
        const float corner[3] = {f(place[0] - static_cast<double>(hx)), f(place[1] - hy), f(place[2] - hz)};
        const uint8_t shown = BfColourByte(opacity);
        const uint32_t colour = BfColourByte(b) | BfColourByte(g) << 8 | BfColourByte(r) << 16 |
                                static_cast<uint32_t>(shown) << 24;
        BfWiden(self, place);
        if (static_cast<double>(shown) < M<double>(0x00A92688))
            continue;
        const double secondX = corner[0] + static_cast<double>(sx);
        const float second[3] = {f(secondX), f(static_cast<double>(corner[1]) + sy),
                                 f(static_cast<double>(corner[2]) + sz)};
        const float places[4][3] = {
            {f(corner[0] + ax), f(static_cast<double>(corner[1]) + ay), f(static_cast<double>(corner[2]) + az)},
            {corner[0], corner[1], corner[2]},
            {second[0], second[1], second[2]},
            {f(secondX + ax), f(static_cast<double>(second[1]) + ay), f(static_cast<double>(second[2]) + az)}};
        for (int v = 0; v < 4; ++v) {
            std::memcpy(out + v * 0x18, places[v], 12);
            At<uint32_t>(out + v * 0x18, 0xC) = colour;
        }
        out += 0x60;
    }
    if (At<uint32_t>(batch, 0x24)) {
        BOurs(addr::SC4RenderContext_BindTexture, Word(context), Word(self + 0x68), 4, [&] {
            return CallThis<uint32_t>(addr::SC4RenderContext_BindTexture, context, At<uint32_t>(self, 0x68), 0u);
        });
        if (g_bReplay)
            PlKeepVertices(batch, g_bfOurVertices);
        BOurs(addr::SC4IndexBatch_Draw, Word(batch), Word(&context), 4,
              [&] { return IbDrawFor(batch, context); });
    }
    BfGrow(self);
}

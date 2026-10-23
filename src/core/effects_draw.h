// The effects' draws without particles, written by hand from the game's
// code: the effects manager's draw (0x00593120), an effect's draw
// (0x005B8540), a decal's step (0x00582920) and a decal's update
// (0x00582600). Included by water_effects.cpp after effects_emit.h, whose
// conventions it keeps: the x87 runs at 53 bits, intermediates are
// doubles, rounded to float where the game stores one, in the game's order.
//
// The effect's draw makes its calls through CallThisTraced and
// CallPlainTraced, so that the comparison's replay answers them from the
// game's record. The manager's draw makes the calls that draw or step
// something through DwManagerCall (defined before this file is included),
// which its comparison only notes in ours' run.

// The kinds of call DwManagerCall makes, as its comparison notes them.
enum DwKind : uint32_t {
    kDwBeforeDraw = 1,
    kDwEffect = 2,
    kDwBox = 3,
    kDwDecal = 4,
    kDwFlash = 5,
    kDwShake = 6,
    kDwAfter = 7,
};

uint32_t DwManagerCall(uint32_t kind, uint32_t target, char* self, uint32_t a = 0, uint32_t b = 0);

// A float field as the x87 loads it.
inline double DwF(char* object, std::size_t at)
{
    return At<float>(object, at);
}

// --- An effect's draw ------------------------------------------------------------------------

// 0x005B8540 (slot 0x24 of both classes, ret 8), with the step and a
// render context (null for an effect out of view); the answer is AL.
// Nothing but false for an effect that does not run (+9). The step (plus,
// once, the description's +0x1C when +0x118 is set) is kept at +0x108 as
// a float and added unrounded to the clock (+0x110, a double). The object
// at +0x1F0 answers the value at +0x1FC (its slot 0x74). Descriptions with
// flag 0x2000 get 0x005B7030 with 0. A description with entries from
// +0x1A4 to +0x1A8 and its +0x1B0 above the zero at 0x00A81054 has its pull
// (0x005B6C40) worked out; others clear +0x16C to +0x174. The frame's
// velocity (+0x13C) is, per axis, the description's (+0xE0) plus the
// effect's own (+0x150) plus the pull (+0x16C), rounded to float, plus the
// vector at +0x1E8's +8 times the description's +0xEC; the third axis
// rounds the first sum to float before adding the pull and keeps the last
// product unrounded. Flag 0x80000000 gets 0x005B8410. Each particle of the
// list at +0x14 that runs (its first byte) is stepped (slot 0x50, with the
// bits of +0x108); one that does not or that the step ends is unlinked,
// its node freed, the count (+0x18) lowered and the particle given back
// (the sub-object's slot 0x40). Unless +0x21A is set, new ones are emitted
// (slot 0x3C). With a context and a count above zero: slot 0x38, the
// context's colour from 0x00B0A668 for descriptions with flag 1 (0x007D31B0,
// undone by 0x007D3290 after the drawing), its view matrix (0x007D2D20),
// the render state at +0x1C (0x007D4530), then slot 0x48 when +0x1EC is
// set, else slot 0x44. Without, slot 0x4C. Last, unless the sub-object
// says the effect is over (0x005B9D40), descriptions with flag 0x800000
// get 0x005B2730 with +0x108.
uint32_t DrawEffect(char* self, float step, char* context)
{
    if (!At<uint8_t>(self, 9))
        return 0;
    double time = step;
    if (At<uint8_t>(self, 0x118)) {
        time += DwF(At<char*>(self, 0x24), 0x1C);
        At<uint8_t>(self, 0x118) = 0;
    }
    At<float>(self, 0x108) = static_cast<float>(time);
    At<double>(self, 0x110) = time + At<double>(self, 0x110);
    At<float>(self, 0x1FC) = static_cast<float>(SuSlot<double>(At<char*>(self, 0x1F0), 0x74));
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 13 & 1)
        CallThisTraced(addr::SC4Effect_StepFlag2000, self, 0u);
    char* d = At<char*>(self, 0x24);
    if (At<uint32_t>(d, 0x1A4) != At<uint32_t>(d, 0x1A8) && DwF(d, 0x1B0) > M<float>(0x00A81054)) {
        CallThisTraced(addr::SC4Effect_PathVelocity, self);
    } else {
        At<uint32_t>(self, 0x16C) = 0;
        At<uint32_t>(self, 0x170) = 0;
        At<uint32_t>(self, 0x174) = 0;
    }
    d = At<char*>(self, 0x24);
    const double factor = DwF(d, 0xEC);
    char* const pushed = At<char*>(self, 0x1E8) + 8;
    const float px = static_cast<float>(factor * DwF(pushed, 0));
    const float py = static_cast<float>(factor * DwF(pushed, 4));
    const double pz = factor * DwF(pushed, 8);
    const float ownZ = static_cast<float>(DwF(d, 0xE8) + DwF(self, 0x158));
    const float vx = static_cast<float>((DwF(d, 0xE0) + DwF(self, 0x150)) + DwF(self, 0x16C));
    const float vy = static_cast<float>((DwF(d, 0xE4) + DwF(self, 0x154)) + DwF(self, 0x170));
    const float vz = static_cast<float>(static_cast<double>(ownZ) + DwF(self, 0x174));
    At<float>(self, 0x144) = static_cast<float>(vz + pz);
    At<float>(self, 0x13C) = static_cast<float>(static_cast<double>(vx) + px);
    At<float>(self, 0x140) = static_cast<float>(static_cast<double>(vy) + py);
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 31)
        CallThisTraced(addr::SC4Effect_StepFlag80000000, self);
    char* node = At<char*>(At<char*>(self, 0x14), 0);
    while (node != At<char*>(self, 0x14)) {
        char* const particle = At<char*>(node, 8);
        if (At<uint8_t>(particle, 0) &&
            (CallThisTraced(At<uint32_t>(At<char*>(self, 0), 0x50), self, Word(particle), At<uint32_t>(self, 0x108)) &
             0xFF)) {
            node = At<char*>(node, 0);
            continue;
        }
        char* const next = At<char*>(node, 0);
        char* const previous = At<char*>(node, 4);
        At<char*>(previous, 0) = next;
        At<char*>(next, 4) = previous;
        CallPlainTraced(addr::SC4_Free, Word(node));
        --At<int32_t>(self, 0x18);
        char* const sub = self + 4;
        CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x40), sub, Word(particle));
        node = next;
    }
    if (!At<uint8_t>(self, 0x21A))
        CallThisTraced(At<uint32_t>(At<char*>(self, 0), 0x3C), self);
    if (context && At<int32_t>(self, 0x18) > 0) {
        CallThisTraced(At<uint32_t>(At<char*>(self, 0), 0x38), self, Word(context));
        if (At<uint8_t>(At<char*>(self, 0x24), 0) & 1)
            CallThisTraced(addr::SC4RenderContext_SetColourAlt, context, 0x00B0A668u);
        CallThisTraced(addr::SC4RenderContext_UpdateViewMatrix, context);
        CallThisTraced(addr::SC4RenderContext_SetRenderState, context, Word(self + 0x1C));
        CallThisTraced(At<uint32_t>(At<char*>(self, 0), At<uint32_t>(self, 0x1EC) ? 0x48 : 0x44), self, Word(context));
        if (At<uint8_t>(At<char*>(self, 0x24), 0) & 1)
            CallThisTraced(addr::SC4RenderContext_ResetColour, context);
    } else {
        CallThisTraced(At<uint32_t>(At<char*>(self, 0), 0x4C), self);
    }
    if (!(CallThisTraced(addr::SC4EffectBase_Finish, self + 4) & 0xFF) &&
        (At<uint32_t>(At<char*>(self, 0x24), 0) >> 23 & 1))
        CallThisTraced(addr::SC4Effect_StepFlag800000, self, At<uint32_t>(self, 0x108));
    return 1;
}

// --- A decal's step and update ---------------------------------------------------------------

// A decal's curve (the vector of floats at `at`) read at `t`: one value
// alone is the value. Else the place is the last index (as the x87 loads a
// word, made unsigned with the float at 0x00A80AA8) times `t`; it is
// rounded down by the float at 0x00A9EE6C (1.5 times 2^23) added and
// taken off the float's bits, less the float at 0x00A84D2C and one up when
// the place is that one up; past the zero at 0x00A81054 the value is
// interpolated towards the next one.
double DwCurve(char* self, std::size_t at, float t)
{
    float* const data = At<float*>(self, at);
    const uint32_t last = static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(self, at + 4) - Word(data)) >> 2) - 1;
    if (last == 0)
        return data[0];
    const double place = LfUnsignedF(last) * t;
    uint32_t index =
        LfBits(static_cast<float>((place - M<float>(0x00A84D2C)) + M<float>(0x00A9EE6C))) + 0xB4C00000u;
    if (place == static_cast<double>(static_cast<int32_t>(index + 1)))
        index += 1;
    const double part = place - LfUnsignedF(index);
    if (part > M<float>(0x00A81054))
        return (static_cast<double>(data[index + 1]) - data[index]) * part + data[index];
    return data[index];
}

// 0x00582920 (slot 0x24 of the decal's table 0x00A9EE78, ret 4), with the
// step. Nothing for a decal without a handle (+0x70 is -1). Its time
// (+0x114) grows by the step; the time, unrounded, times +0x118 is where it
// is in its life, `t`. A looping decal (+0x15 is 1) keeps what is past the
// whole number (rounded down as the curves are, less the float at
// 0x00A84D2C); any other past the double at 0x00A80AB0 ends itself when of
// kind 2 (its slot 4 with 0, and nothing more) or stays at 1. Then the
// owner (+0x120) is given, for the handle: with flag 0x40 of +0xC only the
// opacity (slot 0x44: +0x108 times +0xF4 times the curve at +0x34). Else a
// block of nine values (slot 0x34): the place (+0xE4, +0xE8), +0x10C times
// +0xEC times the curve at +0x28, the curve at +0x1C times +0xFC plus
// +0xF0, the curve at +0x4C times +0x104 (and with flag 8, +0x100 times
// +0x64 and that times the last, else two ones), +0x68 and +0x6C; then the
// opacity, and with more than one position at +0x40 (12 bytes each) the
// one the time has reached (slot 0x48), interpolated between two.
void StepDecal(char* self, float step)
{
    const uint32_t handle = At<uint32_t>(self, 0x70);
    if (handle == 0xFFFFFFFFu)
        return;
    const double time = static_cast<double>(step) + DwF(self, 0x114);
    At<float>(self, 0x114) = static_cast<float>(time);
    float t = static_cast<float>(time * DwF(self, 0x118));
    const uint8_t kind = At<uint8_t>(self, 0x15);
    if (kind == 1) {
        int32_t whole = static_cast<int32_t>(
            LfBits(static_cast<float>((static_cast<double>(t) - M<float>(0x00A84D2C)) + M<float>(0x00A9EE6C))) +
            0xB4C00000u);
        if (t == static_cast<double>(whole + 1))
            whole += 1;
        t = static_cast<float>(t - static_cast<double>(whole));
    } else if (t > M<double>(0x00A80AB0)) {
        if (kind == 2) {
            SuSlot<uint32_t>(self, 4, 0u);
            return;
        }
        t = 1.0f;
    }
    const uint32_t flags = At<uint32_t>(self, 0xC);
    char* owner = At<char*>(self, 0x120);
    if (flags >> 6 & 1) {
        const double v = DwCurve(self, 0x34, t);
        SuSlot<uint32_t>(owner, 0x44, handle, LfBits(static_cast<float>(DwF(self, 0x108) * DwF(self, 0xF4) * v)));
        return;
    }
    uint32_t block[9];
    block[0] = At<uint32_t>(self, 0xE4);
    block[1] = At<uint32_t>(self, 0xE8);
    const double size = DwCurve(self, 0x28, t);
    block[2] = LfBits(static_cast<float>(DwF(self, 0x10C) * DwF(self, 0xEC) * size));
    const double turn = DwCurve(self, 0x1C, t);
    block[3] = LfBits(static_cast<float>(turn * DwF(self, 0xFC) + DwF(self, 0xF0)));
    const float ratio = static_cast<float>(DwCurve(self, 0x4C, t) * DwF(self, 0x104));
    block[4] = LfBits(ratio);
    if (flags >> 3 & 1) {
        const double width = DwF(self, 0x100) * DwF(self, 0x64);
        block[5] = LfBits(static_cast<float>(width));
        block[6] = LfBits(static_cast<float>(width * ratio));
    } else {
        block[5] = 0x3F800000u;
        block[6] = 0x3F800000u;
    }
    block[7] = At<uint32_t>(self, 0x68);
    block[8] = At<uint32_t>(self, 0x6C);
    SuSlot<uint32_t>(owner, 0x34, handle, block);
    owner = At<char*>(self, 0x120);
    const double opacity = DwCurve(self, 0x34, t);
    SuSlot<uint32_t>(owner, 0x44, At<uint32_t>(self, 0x70),
                     LfBits(static_cast<float>(DwF(self, 0x108) * DwF(self, 0xF4) * opacity)));
    const int32_t count = static_cast<int32_t>(At<uint32_t>(self, 0x44) - At<uint32_t>(self, 0x40)) / 12;
    if (static_cast<uint32_t>(count) <= 1)
        return;
    float* const points = At<float*>(self, 0x40);
    owner = At<char*>(self, 0x120);
    const uint32_t last = static_cast<uint32_t>(count - 1);
    const double place = LfUnsignedF(last) * t;
    uint32_t index =
        LfBits(static_cast<float>((place - M<float>(0x00A84D2C)) + M<float>(0x00A9EE6C))) + 0xB4C00000u;
    if (place == static_cast<double>(static_cast<int32_t>(index + 1)))
        index += 1;
    const float part = static_cast<float>(place - LfUnsignedF(index));
    float* const p = points + index * 3;
    float at[3];
    if (part > M<float>(0x00A81054)) {
        const double nx = static_cast<double>(part) * p[3];
        const double ny = static_cast<double>(part) * p[4];
        const float nz = static_cast<float>(static_cast<double>(part) * p[5]);
        const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
        at[0] = static_cast<float>(rest * p[0] + nx);
        const float ry = static_cast<float>(rest * p[1]);
        const float rz = static_cast<float>(rest * p[2]);
        at[1] = static_cast<float>(static_cast<double>(ry) + ny);
        at[2] = static_cast<float>(static_cast<double>(rz) + nz);
    } else {
        std::memcpy(at, p, sizeof at);
    }
    SuSlot<uint32_t>(owner, 0x48, At<uint32_t>(self, 0x70), at);
}

// The turn: the x87's own arctangent of `y` over `x` (64-bit), times the
// float at 0x00B0A210, stored as a float.
void DwTurn(double x, float y, float* out)
{
    const float* const scale = &M<float>(0x00B0A210);
    __asm {
        fld dword ptr [y]
        fchs
        fld qword ptr [x]
        fpatan
        mov eax, scale
        fmul dword ptr [eax]
        mov edx, out
        fstp dword ptr [edx]
    }
}

// 0x00582600 (slot 3 of the decal's table, ret 8), with two transforms
// (each with a byte at +1 saying it holds one; floats from +4, a turn in
// the rows at +4 and +0x1C, the place at +0x28 and +0x30, a scale at
// +0x34). Nothing when +0x110 is set. With the second alone (or neither)
// its own values are taken; with the first alone, the first's; with both,
// the second's composed with the first's rows. The place goes to +0xE4 and
// +0xE8, the scale times +0xF8 to +0xEC and the turn (DwTurn of the second
// row's first value, negated, over the first's) to +0xF0. With flag 0x40
// of +0xC and a handle (+0x70), the owner (+0x120) is asked its slot 0x38
// with the handle and a block (the second and third words zero; the game
// leaves the first, and what follows the third, as its stack held them,
// ours zero), then given the
// block (slot 0x34): the place, +0x10C times +0xEC times the curve at
// +0x28 and the curve at +0x1C times +0xFC plus +0xF0, both read at the
// zero at 0x00A81054.
void UpdateDecal(char* self, char* first, char* second)
{
    if (At<uint8_t>(self, 0x110))
        return;
    double placeX, turnX;
    uint32_t placeZ;
    float turnY, scale;
    if (!At<uint8_t>(first, 1) || !At<uint8_t>(second, 1)) {
        char* const from = At<uint8_t>(first, 1) ? first : second;
        placeX = DwF(from, 0x28);
        turnX = DwF(from, 4);
        placeZ = At<uint32_t>(from, 0x30);
        turnY = At<float>(from, 0x1C);
        scale = At<float>(from, 0x34);
    } else {
        char* const a = first;
        char* const b = second;
        const double x = (DwF(b, 8) * DwF(a, 0x2C) + DwF(b, 4) * DwF(a, 0x28)) + DwF(b, 0xC) * DwF(a, 0x30);
        const double z = (DwF(b, 0x20) * DwF(a, 0x2C) + DwF(b, 0x1C) * DwF(a, 0x28)) + DwF(b, 0x24) * DwF(a, 0x30);
        const float sx = static_cast<float>(DwF(b, 0x34) * x);
        const float sz = static_cast<float>(z * DwF(b, 0x34));
        placeX = DwF(b, 0x28) + sx;
        placeZ = LfBits(static_cast<float>(DwF(b, 0x30) + sz));
        const double y = (DwF(b, 0x1C) * DwF(a, 4) + DwF(b, 0x24) * DwF(a, 0x1C)) + DwF(b, 0x20) * DwF(a, 0x10);
        turnX = (DwF(b, 4) * DwF(a, 4) + DwF(b, 0xC) * DwF(a, 0x1C)) + DwF(b, 8) * DwF(a, 0x10);
        turnY = static_cast<float>(y);
        scale = static_cast<float>(DwF(b, 0x34) * DwF(a, 0x34));
    }
    At<float>(self, 0xE4) = static_cast<float>(placeX);
    At<uint32_t>(self, 0xE8) = placeZ;
    At<float>(self, 0xEC) = static_cast<float>(static_cast<double>(scale) * DwF(self, 0xF8));
    DwTurn(turnX, turnY, &At<float>(self, 0xF0));
    if (!(At<uint32_t>(self, 0xC) >> 6 & 1) || At<int32_t>(self, 0x70) == -1)
        return;
    char* const owner = At<char*>(self, 0x120);
    uint32_t block[9] = {};
    SuSlot<uint32_t>(owner, 0x38, At<uint32_t>(self, 0x70), block);
    block[0] = At<uint32_t>(self, 0xE4);
    block[1] = At<uint32_t>(self, 0xE8);
    const float zero = M<float>(0x00A81054);
    const double size = DwCurve(self, 0x28, zero);
    block[2] = LfBits(static_cast<float>(DwF(self, 0x10C) * DwF(self, 0xEC) * size));
    const double turn = DwCurve(self, 0x1C, zero);
    block[3] = LfBits(static_cast<float>(turn * DwF(self, 0xFC) + DwF(self, 0xF0)));
    SuSlot<uint32_t>(owner, 0x34, At<uint32_t>(self, 0x70), block);
}

// --- The manager's draw ----------------------------------------------------------------------

// An effect out of view pushed on the vector at +0xDAC (through
// 0x0051CA60 when it is full: the place, the value, a tag and one, at the
// end).
void DwPushHidden(char* self, uint32_t effect)
{
    uint32_t* const end = At<uint32_t*>(self, 0xDB0);
    if (end != At<uint32_t*>(self, 0xDB4)) {
        if (end)
            *end = effect;
        At<uint32_t*>(self, 0xDB0) = end + 1;
        return;
    }
    uint32_t tag = 0;
    CallThis<uint32_t>(addr::SC4PtrVector_InsertN, self + 0xDAC, end, &effect, &tag, 1u, 1u);
}

// An effect in view pushed on the vector of {depth, effect} at +0xDA0:
// through 0x007C61A0 when it is full for the first two lists; the third's
// growth is the game's own inline copy of it (twice the size, at least
// one, from 0x0090CF54, the old storage freed).
void DwPushShown(char* self, DepthEntry entry, bool inlineGrowth)
{
    DepthEntry* const end = At<DepthEntry*>(self, 0xDA4);
    if (end != At<DepthEntry*>(self, 0xDA8)) {
        if (end)
            *end = entry;
        At<DepthEntry*>(self, 0xDA4) = end + 1;
        return;
    }
    if (!inlineGrowth) {
        uint32_t tag = 0;
        CallThis<uint32_t>(addr::SC4PointVector_InsertN, self + 0xDA0, end, &entry, &tag, 1u, 1u);
        return;
    }
    DepthEntry* const begin = At<DepthEntry*>(self, 0xDA0);
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(Word(end) - Word(begin)) >> 3);
    const uint32_t grown = size + (size < 1 ? 1 : size);
    DepthEntry* const fresh =
        grown ? reinterpret_cast<DepthEntry*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(grown * 8) : nullptr;
    DepthEntry* to = fresh;
    for (DepthEntry* from = begin; from != end; ++from, ++to) {
        if (to)
            *to = *from;
    }
    if (to)
        *to = entry;
    if (begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    At<DepthEntry*>(self, 0xDA0) = fresh;
    At<DepthEntry*>(self, 0xDA4) = to + 1;
    At<DepthEntry*>(self, 0xDA8) = fresh + grown;
}

// One of the three lists of effects (an index vector into a table): each
// running one (its slot 8) has its box (0x005B26F0); unless its
// description has flag 0x10, the camera view's frustum (slot 0x20 of the
// view at +0xDDC) is asked whether the box is in sight (0x007F6770). One
// out of sight goes to the effects out of view; the others to the effects
// in view, with the depth: the position (+0x194) along `axis` times `sign`
// plus the description's +0xD8.
void DwCollect(char* self, std::size_t indices, std::size_t table, uint32_t axis, float sign, bool inlineGrowth)
{
    for (uint32_t* it = At<uint32_t*>(self, indices); it != At<uint32_t*>(self, indices + 4); ++it) {
        if (!(SuSlot<uint32_t>(At<char**>(self, table)[*it], 8) & 0xFF))
            continue;
        char** const slot = At<char**>(self, table) + *it;
        float box[6];
        CallThis<uint32_t>(addr::SC4Effect_GetBox, *slot, box);
        if (!(At<uint32_t>(At<char*>(*slot, 0x24), 0) >> 4 & 1)) {
            char* const frustum = SuSlot<char*>(At<char*>(self, 0xDDC), 0x20);
            if (!(CallThis<uint32_t>(addr::SC4CameraView_BoxInView, frustum, box, box + 3) & 0xFF)) {
                DwPushHidden(self, Word(*slot));
                continue;
            }
        }
        char* const effect = *slot;
        float place[3];
        std::memcpy(place, effect + 0x194, sizeof place);
        const float depth =
            static_cast<float>(static_cast<double>(sign) * place[axis] + DwF(At<char*>(effect, 0x24), 0xD8));
        DwPushShown(self, DepthEntry{LfBits(depth), Word(effect)}, inlineGrowth);
    }
}

// 0x00593120 (slot 3 of the table 0x00A9F250, ret 4), `self` the
// interface the 3D render holds (the manager's +4), with the render
// context; the answer is true. The render (the context's +0x38) has its
// properties at +0xC. Nothing more with the property at +0x39C set. Else:
// the object at +0xD94 gets 0x00586720 with the context when +0xD98 is
// set, the two vectors (+0xDA0, +0xDAC) are emptied, and the three lists
// (indices at +0xBF0, +0xC38 and +0xC5C into the tables at +0xBE4, +0xC2C
// and +0xC50) collected (DwCollect), the axis by the view's rotation (the
// context's +0x18: 0 the third with 1, 1 the first with -1, 2 the third
// with -1, 3 the first with 1; another leaves the game's stack as it was,
// ours the first with 1). The budget (+0xC9C) is steered by the count of
// the last frame (+0xC8C, made a float) against the most (the word at the
// render's +0x18's +0xE4) and the target (its +0x24's +0x94) with the
// blend at its +0x24's +0xBC; the count goes back to zero. The effects in
// view are sorted by depth (0x005906C0 with twice the depth's log, then
// 0x0058F980 on the first 16 and 0x0058E170 on the rest, or 0x0058F980 on
// all; the comparer a word whose low byte is clear). With the property at
// +0x1BC each is drawn (slot 0x24 with the step at +0x1B8 and the
// context) and its particles (+0x18) counted, and each out of view gets
// the same call without a context, last to first. The property at +0x35C
// draws their boxes (0x007D5030, in green at 0.3). Every running entry of
// the fourth list (indices at +0xBCC into the table at +0xBC0) is stepped
// (slot 0x24 with the step) and counted (+0xC98); the number of effects in
// view goes to +0xC94. Then 0x0058E870 (the context, the step) when +0xD44
// is above zero and the property at +0x3FC set, 0x0058EC40 (the step) when
// +0xD88 is above zero, and the clean-up (0x005922A0), each on the
// manager; the step and +0x1B4 are cleared.
uint32_t DrawEffects(char* self, char* context)
{
    if (At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x39C))
        return 1;
    if (At<uint8_t>(self, 0xD98))
        DwManagerCall(kDwBeforeDraw, addr::SC4EffectsManager_BeforeDraw, At<char*>(self, 0xD94), Word(context));
    At<uint32_t>(self, 0xDA4) = At<uint32_t>(self, 0xDA0);
    At<uint32_t>(self, 0xDB0) = At<uint32_t>(self, 0xDAC);
    uint32_t axis = 0;
    float sign = 1.0f;
    switch (At<uint32_t>(context, 0x18)) {
    case 0:
        axis = 2;
        break;
    case 1:
        sign = -1.0f;
        break;
    case 2:
        axis = 2;
        sign = -1.0f;
        break;
    default:
        break;
    }
    DwCollect(self, 0xBF0, 0xBE4, axis, sign, false);
    DwCollect(self, 0xC38, 0xC2C, axis, sign, false);
    DwCollect(self, 0xC5C, 0xC50, axis, sign, true);
    char* const render = At<char*>(context, 0x38);
    const double most = At<int32_t>(At<char*>(render, 0x18), 0xE4);
    const float target = At<float>(At<char*>(render, 0x24), 0x94);
    const double blend = DwF(At<char*>(At<char*>(context, 0x38), 0x24), 0xBC);
    const double counted = LfUnsignedF(At<uint32_t>(self, 0xC8C));
    const float count = static_cast<float>(counted);
    const double one = M<float>(0x00A81228);
    if (counted > most) {
        At<float>(self, 0xC9C) =
            static_cast<float>(((one - blend) * most / count + blend) * DwF(self, 0xC9C));
    } else if (At<float>(self, 0xC9C) < target) {
        const double scaled = (most * (one - blend) + count * blend) * DwF(self, 0xC9C);
        At<float>(self, 0xC9C) = scaled < static_cast<double>(count) * target
                                     ? static_cast<float>(scaled / count)
                                     : target;
    }
    DepthEntry* const first = At<DepthEntry*>(self, 0xDA0);
    DepthEntry* const last = At<DepthEntry*>(self, 0xDA4);
    At<uint32_t>(self, 0xC8C) = 0;
    if (first != last) {
        const int32_t n = static_cast<int32_t>(Word(last) - Word(first)) >> 3;
        uint32_t log = 0;
        for (int32_t m = n; m != 1; m >>= 1)
            ++log;
        const uint32_t comparer = LfBits(count) & 0xFFFFFF00u;
        using Sort5 = uint32_t(__cdecl*)(DepthEntry*, DepthEntry*, uint32_t, uint32_t, uint32_t);
        using Sort3 = uint32_t(__cdecl*)(DepthEntry*, DepthEntry*, uint32_t);
        using Sort4 = uint32_t(__cdecl*)(DepthEntry*, DepthEntry*, uint32_t, uint32_t);
        reinterpret_cast<Sort5>(addr::SC4EffectDepths_SortLoop)(first, last, 0, log * 2, comparer);
        if (n > 16) {
            reinterpret_cast<Sort3>(addr::SC4EffectDepths_InsertionSort)(first, first + 16, comparer);
            reinterpret_cast<Sort4>(addr::SC4EffectDepths_InsertionSortAfter)(first + 16, last, 0, comparer);
        } else {
            reinterpret_cast<Sort3>(addr::SC4EffectDepths_InsertionSort)(first, last, comparer);
        }
    }
    if (At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x1BC)) {
        for (DepthEntry* e = At<DepthEntry*>(self, 0xDA0); e != At<DepthEntry*>(self, 0xDA4); ++e) {
            char* const effect = Ptr(e->effect);
            DwManagerCall(kDwEffect, At<uint32_t>(At<char*>(effect, 0), 0x24), effect, At<uint32_t>(self, 0x1B8),
                          Word(context));
            At<uint32_t>(self, 0xC8C) += At<uint32_t>(Ptr(e->effect), 0x18);
        }
        for (uint32_t* it = At<uint32_t*>(self, 0xDB0); it != At<uint32_t*>(self, 0xDAC); --it) {
            char* const effect = Ptr(it[-1]);
            DwManagerCall(kDwEffect, At<uint32_t>(At<char*>(effect, 0), 0x24), effect, At<uint32_t>(self, 0x1B8), 0);
        }
    }
    if (At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x35C)) {
        for (DepthEntry* e = At<DepthEntry*>(self, 0xDA0); e != At<DepthEntry*>(self, 0xDA4); ++e) {
            const uint32_t colour[4] = {0, 0x3F800000u, 0, 0x3E99999Au};
            float box[6];
            const uint32_t at = CallThis<uint32_t>(addr::SC4Effect_GetBox, Ptr(e->effect), box);
            DwManagerCall(kDwBox, addr::SC4RenderContext_DrawBox, context, at, Word(colour));
        }
    }
    At<uint32_t>(self, 0xC98) = 0;
    for (uint32_t* it = At<uint32_t*>(self, 0xBCC); it != At<uint32_t*>(self, 0xBD0); ++it) {
        if (!(SuSlot<uint32_t>(At<char**>(self, 0xBC0)[*it], 8) & 0xFF))
            continue;
        char* const decal = At<char**>(self, 0xBC0)[*it];
        DwManagerCall(kDwDecal, At<uint32_t>(At<char*>(decal, 0), 0x24), decal, At<uint32_t>(self, 0x1B8));
        ++At<uint32_t>(self, 0xC98);
    }
    At<int32_t>(self, 0xC94) =
        static_cast<int32_t>(At<uint32_t>(self, 0xDA4) - At<uint32_t>(self, 0xDA0)) >> 3;
    if (DwF(self, 0xD44) > M<float>(0x00A81054) && At<uint8_t>(At<char*>(At<char*>(context, 0x38), 0xC), 0x3FC))
        DwManagerCall(kDwFlash, addr::SC4EffectsManager_DrawFlash, self - 4, Word(context), At<uint32_t>(self, 0x1B8));
    if (DwF(self, 0xD88) > M<float>(0x00A81054))
        DwManagerCall(kDwShake, addr::SC4EffectsManager_StepShake, self - 4, At<uint32_t>(self, 0x1B8));
    DwManagerCall(kDwAfter, addr::SC4EffectsManager_AfterDraw, self - 4);
    At<uint32_t>(self, 0x1B8) = 0;
    At<uint32_t>(self, 0x1B4) = 0;
    return 1;
}

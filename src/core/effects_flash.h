// The effects manager's flash and camera shake after the draw, written by
// hand from the game's code: the flash (0x0058E870) and the camera shake
// (0x0058EC40). Included by water_effects.cpp after effects_draw.h, whose
// conventions it keeps: the x87 runs at 53 bits, intermediates are doubles,
// rounded to float where the game stores one, in the game's order. DwF
// reads a float field as a double.

// The whole number nearest to `value` less the float at 0x00A84D2C, by the
// float at 0x00A9F0AC (1.5 times 2^23) added and taken off the float's
// bits, taken one up when the value is that one up: the flash's rounding
// down (as 0x00A9F0AC is the flash's 12582912).
inline uint32_t FsRoundDown(double value)
{
    uint32_t whole =
        LfBits(static_cast<float>((value - M<float>(0x00A84D2C)) + M<float>(0x00A9F0AC))) + 0xB4C00000u;
    if (value == static_cast<double>(static_cast<int32_t>(whole + 1)))
        whole += 1;
    return whole;
}

// A point from a list of three-float entries `count` long at `t` (the
// flash's value), in the flash's way: one entry alone answers it. Else the
// place is the last index (made unsigned as the x87 loads it) times `t`,
// rounded down. Past the zero at 0x00A81054 the part goes towards the next
// point: each axis the next point's part plus, as floats, the rest (the
// float at 0x00A81228 less the part) of this one, the first axis unrounded.
inline void FsPointAt(const float* first, uint32_t count, float t, float* at)
{
    const int32_t last = static_cast<int32_t>(count) - 1;
    if (last == 0) {
        std::memcpy(at, first, 12);
        return;
    }
    const double place = LfUnsignedF(static_cast<uint32_t>(last)) * static_cast<double>(t);
    const uint32_t index = FsRoundDown(place);
    const double part = static_cast<float>(place - LfUnsignedF(index));
    if (part <= M<float>(0x00A81054)) {
        std::memcpy(at, first + index * 3, 12);
        return;
    }
    const float* const p = first + index * 3;
    const float nextZ = static_cast<float>(part * static_cast<double>(p[5]));
    const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
    const float ry = static_cast<float>(rest * static_cast<double>(p[1]));
    const float rz = static_cast<float>(rest * static_cast<double>(p[2]));
    at[0] = static_cast<float>(rest * static_cast<double>(p[0]) + part * static_cast<double>(p[3]));
    at[1] = static_cast<float>(static_cast<double>(ry) + part * static_cast<double>(p[4]));
    at[2] = static_cast<float>(static_cast<double>(rz) + static_cast<double>(nextZ));
}

// A value from a curve of floats `count` long at `t`, in the flash's way:
// one value answers it. Else the place as for the point list; past zero the
// value is interpolated towards the next one.
inline double FsCurveAt(const float* first, uint32_t count, float t)
{
    const int32_t last = static_cast<int32_t>(count) - 1;
    if (last == 0)
        return static_cast<double>(first[0]);
    const double place = LfUnsignedF(static_cast<uint32_t>(last)) * static_cast<double>(t);
    const uint32_t index = FsRoundDown(place);
    const double part = place - LfUnsignedF(index);
    if (part <= M<float>(0x00A81054))
        return static_cast<double>(first[index]);
    return (static_cast<double>(first[index + 1]) - static_cast<double>(first[index])) * part +
           static_cast<double>(first[index]);
}

// 0x0058E870: the effects manager's flash, with the render context and the
// step (the manager's +0x1B8). `self` is the manager's object (its
// interface less four; the manager's draw calls it so). The value at
// +0xD44 decays by +0xD48 each step; it is stored back, and above 1 the
// flash is over: +0xD48 is cleared and nothing more is done. Else a colour
// from the 3-float list at +0xD28 and an intensity from the curve at +0xD34,
// read at the value, are taken; with the byte at +0xD4C set the intensity
// is multiplied by 1 less the squared distance of the flash's place
// (+0xD50, +0xD54) from the render properties' (+0x24's +0x3DC, +0x3E4)
// over the square of the properties' +0x150 times the flash's +0xD58, and
// by the float at 0x00A9F0A8 once for each of the 4 less the zoom (+0xC80)
// steps when that is above zero; a factor below zero ends it. The context's
// +0x34 object (slot 0x2C, two words out) is asked for a pair; with the
// intensity below zero the sprite is the colour less (0x00B0A270,
// 0x00B0A274, 0x00B0A278) times the intensity and the intensity negated
// as its scale, and the render state +0xD64; else the colour, with the
// intensity as its scale when below one, and the render state +0xD5C. The
// render state is set (0x007D4530) and the object drawn (slot 0xC4 with
// the pair, zero and one).
void DrawFlash(char* self, char* context, float step)
{
    double flashed = static_cast<double>(step) * DwF(self, 0xD48) + DwF(self, 0xD44);
    At<float>(self, 0xD44) = static_cast<float>(flashed);
    if (static_cast<float>(flashed) > M<float>(0x00A81228)) {
        At<uint32_t>(self, 0xD48) = 0;
        return;
    }
    const float t = static_cast<float>(flashed);
    float colour[3];
    FsPointAt(At<float*>(self, 0xD28),
              static_cast<uint32_t>(
                  static_cast<int32_t>(Word(At<float*>(self, 0xD2C)) - Word(At<float*>(self, 0xD28))) / 12),
              t, colour);
    const double curve = FsCurveAt(
        At<float*>(self, 0xD34),
        static_cast<uint32_t>(
            static_cast<int32_t>(Word(At<float*>(self, 0xD38)) - Word(At<float*>(self, 0xD34))) >> 2),
        t);
    float intensity = static_cast<float>(curve);
    if (At<uint8_t>(self, 0xD4C)) {
        char* const props = At<char*>(context, 0x24);
        const double dx = static_cast<float>(DwF(self, 0xD50) - DwF(props, 0x3DC));
        const double dz = DwF(self, 0xD54) - DwF(props, 0x3E4);
        const double scale = DwF(props, 0x150) * DwF(self, 0xD58);
        double factor = static_cast<double>(M<float>(0x00A81228)) - (dz * dz + dx * dx) / (scale * scale);
        if (factor < M<float>(0x00A81054))
            return;
        if (At<int32_t>(self, 0xC80) < 4) {
            int32_t n = 4 - At<int32_t>(self, 0xC80);
            while (--n >= 0)
                factor *= M<float>(0x00A9F0A8);
        }
        intensity = static_cast<float>(curve * factor);
    }
    char* const owner = At<char*>(context, 0x34);
    uint32_t pair[2] = {};
    SuSlot<uint32_t>(owner, 0x2C, reinterpret_cast<uint32_t>(&pair[0]), reinterpret_cast<uint32_t>(&pair[1]));
    uint32_t data[4] = {0, 0, pair[0], pair[1]};
    float block[4];
    char* state;
    if (intensity < M<float>(0x00A81054)) {
        block[0] = static_cast<float>((static_cast<double>(colour[0]) - M<float>(0x00B0A270)) * intensity);
        block[1] = static_cast<float>(
            (static_cast<double>(static_cast<float>(static_cast<double>(colour[1]) - M<float>(0x00B0A274))) *
             static_cast<double>(intensity)));
        block[2] = static_cast<float>(
            (static_cast<double>(static_cast<float>(static_cast<double>(colour[2]) - M<float>(0x00B0A278))) *
             static_cast<double>(intensity)));
        block[3] = -intensity;
        state = self + 0xD64;
    } else {
        block[0] = colour[0];
        block[1] = colour[1];
        block[2] = colour[2];
        block[3] = intensity < M<float>(0x00A81228) ? intensity : M<float>(0x00A81228);
        state = self + 0xD5C;
    }
    CallThis<uint32_t>(addr::SC4RenderContext_SetRenderState, context, Word(state));
    SuSlot<uint32_t>(owner, 0xC4, data, block, 0u, 1u);
}

// 0x0058EC40: the effects manager's camera shake, with the step (the
// manager's +0x1B8). `self` is the manager's object (its interface less
// four; the manager's draw calls it so). The value at +0xD88 decays by
// +0xD8C each step; it is stored back, and above 1 the shake is over:
// +0xD8C is cleared and it is stopped (slot 0x20 of 0x00B43DDC). Else a
// direction is read from the 3-float point
// list at +0xD6C and a strength from the curve at +0xD78, both at the
// value, and the object at 0x00B43DDC is shaken (slot 0x1C with the
// direction and the strength).
void StepShake(char* self, float step)
{
    double shaken = static_cast<double>(step) * DwF(self, 0xD8C) + DwF(self, 0xD88);
    At<float>(self, 0xD88) = static_cast<float>(shaken);
    char* const camera = M<char*>(0x00B43DDC);
    if (static_cast<float>(shaken) > M<float>(0x00A81228)) {
        At<uint32_t>(self, 0xD8C) = 0;
        SuSlot<uint32_t>(camera, 0x20);
        return;
    }
    const float t = static_cast<float>(shaken);
    float direction[3];
    FsPointAt(At<float*>(self, 0xD6C),
              static_cast<uint32_t>(
                  static_cast<int32_t>(Word(At<float*>(self, 0xD70)) - Word(At<float*>(self, 0xD6C))) / 12),
              t, direction);
    const float strength = static_cast<float>(FsCurveAt(
        At<float*>(self, 0xD78),
        static_cast<uint32_t>(
            static_cast<int32_t>(Word(At<float*>(self, 0xD7C)) - Word(At<float*>(self, 0xD78))) >> 2),
        t));
    SuSlot<uint32_t>(camera, 0x1C, direction, LfBits(strength));
}
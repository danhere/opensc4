// An effect's emitters, written by hand from the game's code: the count
// emitter (0x005B3CF0), the stream emitter (0x005B87B0) and the burst
// emitter (0x005B8B50). Included by water_effects.cpp after effects_life.h,
// whose conventions it keeps: the x87 runs at 53 bits, intermediates are
// doubles, rounded to float where the game stores one.
//
// The stream and the burst also step each particle they take (slot 0x50
// of the effect), hand a running one to 0x005B84C0 for descriptions with
// flag 0x80000000, stop the effect (slot 4 with 0) and draw from the
// generator at 0x00B22DF8 (LfEmitterUniform, which the comparison's
// replay answers from the record).
//
// The pool's three functions are called through the table of the
// sub-object at the effect's +4 (CallThisTraced, so that the comparison's
// replay answers them from the game's record): slot 0x3C takes a particle
// (started, or with its first byte clear when the start ended it), slot
// 0x2C adds one to the effect's list and slot 0x40 gives one back.

// The word at +0x128 as the x87 loads it (signed) and then makes unsigned
// (the double at 0x00A80AB8, 2^32, added when it is negative).
inline double LfUnsigned(uint32_t value)
{
    double d = static_cast<int32_t>(value);
    if (static_cast<int32_t>(value) < 0)
        d += M<double>(0x00A80AB8);
    return d;
}

// One particle taken from the pool: added to the list while it runs, given
// back when its start ended it at once.
inline void TakeOneParticle(char* self)
{
    char* const sub = self + 4;
    const uint32_t particle = CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x3C), sub);
    if (!particle)
        return;
    if (At<uint8_t>(Ptr(particle), 0))
        CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x2C), sub, particle);
    else
        CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x40), sub, particle);
}

// 0x005B3CF0 (ret 4), with the time to emit for: the time times the rate
// (+0x130, a double) is added to the double at +0x120. While that stays at
// or below the count emitted so far (+0x128, unsigned) nothing is done.
// Else the description's +0x88 points at the number wanted, which is
// rounded up to a whole number: with the float at 0x00A84D2C added and the
// float at 0x00AA0CF8 (1.5 times 2^23) the float's bits less 0x4B400000
// are the nearest whole number, taken back by one when the number was
// that less one. As many particles as that is above the effect's count
// (+0x18) are taken, the difference counted down as the game's unsigned
// loop does.
inline void EmitToCount(char* self, float time)
{
    const double total = static_cast<double>(time) * At<double>(self, 0x130) + At<double>(self, 0x120);
    At<double>(self, 0x120) = total;
    const double count = LfUnsigned(At<uint32_t>(self, 0x128));
    if (total < count || total == count)
        return;
    const double wanted = *At<float*>(At<char*>(self, 0x24), 0x88);
    const float rounded = static_cast<float>((static_cast<double>(M<float>(0x00A84D2C)) + wanted) +
                                             M<float>(0x00AA0CF8));
    uint32_t whole = 0;
    std::memcpy(&whole, &rounded, 4);
    whole += 0xB4C00000u;
    const uint32_t less = whole - 1;
    if (wanted == static_cast<double>(static_cast<int32_t>(less)))
        whole = less;
    uint32_t left = whole - At<uint32_t>(self, 0x18);
    if (!left)
        return;
    do
        TakeOneParticle(self);
    while (--left);
}

// A float's bits as a whole number.
inline uint32_t LfBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The whole number nearest to `value` less the float at 0x00A84D2C, by the
// float at 0x00AA0CF8 (1.5 times 2^23) added and taken off the float's
// bits, taken one up when the value is that one up: the stream's rounding
// down.
inline uint32_t LfRoundDown(double value)
{
    uint32_t whole =
        LfBits(static_cast<float>((value - M<float>(0x00A84D2C)) + M<float>(0x00AA0CF8))) + 0xB4C00000u;
    const uint32_t up = whole + 1;
    if (value == static_cast<double>(static_cast<int32_t>(up)))
        whole = up;
    return whole;
}

// A signed word as the x87 loads it and makes unsigned with the float at
// 0x00A80AA8 (2^32).
inline double LfUnsignedF(uint32_t value)
{
    double d = static_cast<int32_t>(value);
    if (static_cast<int32_t>(value) < 0)
        d += M<float>(0x00A80AA8);
    return d;
}

// One particle taken and stepped by `time` (its bits): a running one that
// the step keeps goes to 0x005B84C0 for descriptions with flag 0x80000000
// and is added to the list; one the start or the step ended is given back.
inline void TakeAndStep(char* self, uint32_t time)
{
    char* const sub = self + 4;
    const uint32_t particle = CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x3C), sub);
    if (!particle)
        return;
    if (At<uint8_t>(Ptr(particle), 0) &&
        (CallThisTraced(At<uint32_t>(At<char*>(self, 0), 0x50), self, particle, time) & 0xFF)) {
        if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 31 & 1)
            CallThisTraced(addr::SC4Effect_StartAtBirth, self, particle);
        CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x2C), sub, particle);
    } else {
        CallThisTraced(At<uint32_t>(At<char*>(sub, 0), 0x40), sub, particle);
    }
}

// 0x005B87B0 (ret 4), with the time to emit for, in slices of at most the
// float at 0x00AA0CF4. The scale is the float at +0x1E8's target times
// +0x1E0 times +0x1C4, times +0xC0 once, twice or three times for flags
// 0x80000, 0x100000 or 0x200000 of the description. Each slice adds the
// slice times the rate (+0x130) to the double at +0x120; when that passes
// the count (+0x128) plus one, the count goes up by one, and:
// - when it has reached the description's most (+0x18, when not zero), an
//   effect without flag 8 is stopped (slot 4 with 0) and nothing more is
//   done; one with it keeps the count one below the most and +0x120 the
//   most less the double at 0x00A9F190;
// - else, when the description's +0x28 is not below the float at
//   0x00A81054, the slice is cut short by the part past the count times
//   the description's +0x14, +0x120 is set to the count, and from then on
//   each slice ends with a pause drawn between +0x28 and +0x2C.
// The number of particles comes from the description's list at +0x88 (one
// value, or the values read at the part past the count across the list,
// between two neighbours), times the slice and the scale, added to the
// float at +0x138, which keeps what is below a whole one. The particles
// taken are stepped by the time left, less the slice over their number
// for each one after. A pause that takes the time below zero leaves the
// countdown (+0x190) at what it went below by.
inline void EmitStream(char* self, float time)
{
    char* const description = At<char*>(self, 0x24);
    float scale = static_cast<float>(static_cast<double>(*At<float*>(self, 0x1E8)) * At<float>(self, 0x1E0) *
                                     At<float>(self, 0x1C4));
    const uint32_t flags = At<uint32_t>(description, 0);
    const double power = At<float>(self, 0xC0);
    if (flags >> 19 & 1)
        scale = static_cast<float>(static_cast<double>(scale) * power);
    else if (flags >> 20 & 1)
        scale = static_cast<float>(power * power * scale);
    else if (flags >> 21 & 1)
        scale = static_cast<float>(power * power * power * scale);
    bool pauses = false;
    do {
        float slice = M<float>(0x00AA0CF4) < time ? M<float>(0x00AA0CF4) : time;
        const uint32_t next = At<uint32_t>(self, 0x128) + 1;
        const double total = static_cast<double>(slice) * At<double>(self, 0x130) + At<double>(self, 0x120);
        At<double>(self, 0x120) = total;
        if (total > LfUnsigned(next)) {
            char* const d = At<char*>(self, 0x24);
            At<uint32_t>(self, 0x128) = next;
            const uint32_t most = At<uint32_t>(d, 0x18);
            if (most != 0 && next == most) {
                if (!(At<uint32_t>(d, 0) >> 3 & 1)) {
                    CallThisTraced(At<uint32_t>(At<char*>(self, 0), 4), self, 0u);
                    return;
                }
                At<uint32_t>(self, 0x128) = most - 1;
                At<double>(self, 0x120) = LfUnsigned(At<uint32_t>(d, 0x18)) - M<double>(0x00A9F190);
            } else if (At<float>(d, 0x28) >= M<float>(0x00A81054)) {
                const double n = LfUnsigned(next);
                pauses = true;
                slice = static_cast<float>(static_cast<double>(slice) - (total - n) * At<float>(d, 0x14));
                At<double>(self, 0x120) = n;
            }
        }
        char* const d = At<char*>(self, 0x24);
        char* const values = At<char*>(d, 0x88);
        const int32_t last = (static_cast<int32_t>(At<uint32_t>(d, 0x8C) - At<uint32_t>(d, 0x88)) >> 2) - 1;
        double value;
        if (last == 0) {
            value = At<float>(values, 0);
        } else {
            const double x = (At<double>(self, 0x120) - LfUnsigned(At<uint32_t>(self, 0x128))) *
                             LfUnsignedF(static_cast<uint32_t>(last));
            const uint32_t i = LfRoundDown(x);
            const double part = x - LfUnsignedF(i);
            if (part > M<float>(0x00A81054)) {
                const double low = At<float>(values, i * 4);
                value = (static_cast<double>(At<float>(values, i * 4 + 4)) - low) * part + low;
            } else {
                value = At<float>(values, i * 4);
            }
        }
        const double owed = value * slice * scale + At<float>(self, 0x138);
        At<float>(self, 0x138) = static_cast<float>(owed);
        const uint32_t count = LfRoundDown(owed);
        const float whole = static_cast<float>(static_cast<int32_t>(count));
        At<float>(self, 0x138) = static_cast<float>(owed - whole);
        if (static_cast<int32_t>(count) > 0) {
            const float apart = static_cast<float>(static_cast<double>(slice) / whole);
            float at = time;
            for (uint32_t left = count; left; --left) {
                TakeAndStep(self, LfBits(at));
                at = static_cast<float>(static_cast<double>(at) - apart);
            }
        }
        time = static_cast<float>(static_cast<double>(time) - slice);
        if (pauses) {
            char* const pd = At<char*>(self, 0x24);
            const double left = time - LfEmitterUniform(At<float>(pd, 0x28), At<float>(pd, 0x2C));
            time = static_cast<float>(left);
            if (left < M<float>(0x00A81054)) {
                At<float>(self, 0x190) = -time;
                return;
            }
        }
    } while (time > M<double>(0x00A80990));
}

// 0x005B8B50 (ret 4), with the time to emit for: the time times the rate
// (+0x130) is added to the double at +0x120, and while that is above the
// count (+0x128) bursts are made. A burst's size is the description's
// first value at +0x88, times the float at +0x1E8's target, +0x1E0 and
// +0x1C4 when it is above the float at 0x00AA0F1C and the description's
// +8 has no bit 0, rounded up to a whole number. Each round: when the part
// past the count times the description's +0x14 is below its +0x10, that
// many particles are taken and stepped by it; the count goes up by one,
// and at the description's most (+0x18, when not zero) the effect is
// stopped (slot 4 with 0). With the description's +0x28 not below the
// float at 0x00A81054, a pause drawn between +0x28 and +0x2C times the
// rate is taken off +0x120; when that goes below the count, the countdown
// (+0x190) is the difference times +0x14 and +0x120 the count.
inline void EmitBurst(char* self, float time)
{
    const double total = static_cast<double>(time) * At<double>(self, 0x130) + At<double>(self, 0x120);
    At<double>(self, 0x120) = total;
    const double count = LfUnsigned(At<uint32_t>(self, 0x128));
    if (total < count || total == count)
        return;
    char* const description = At<char*>(self, 0x24);
    double wanted = *At<float*>(description, 0x88);
    if (wanted > M<float>(0x00AA0F1C) && !(At<uint8_t>(description, 8) & 1))
        wanted = wanted * (static_cast<double>(*At<float*>(self, 0x1E8)) * At<float>(self, 0x1E0) *
                           At<float>(self, 0x1C4));
    uint32_t size = LfBits(static_cast<float>((static_cast<double>(M<float>(0x00A84D2C)) + wanted) +
                                              M<float>(0x00AA0CF8))) +
                    0xB4C00000u;
    const uint32_t less = size - 1;
    if (wanted == static_cast<double>(static_cast<int32_t>(less)))
        size = less;
    if (!(total > count))
        return;
    do {
        char* d = At<char*>(self, 0x24);
        const double over = (At<double>(self, 0x120) - LfUnsigned(At<uint32_t>(self, 0x128))) * At<float>(d, 0x14);
        const float at = static_cast<float>(over);
        if (over < At<float>(d, 0x10) && size != 0) {
            for (uint32_t left = size; left; --left)
                TakeAndStep(self, LfBits(at));
        }
        const uint32_t next = At<uint32_t>(self, 0x128) + 1;
        At<uint32_t>(self, 0x128) = next;
        d = At<char*>(self, 0x24);
        const uint32_t most = At<uint32_t>(d, 0x18);
        if (most != 0 && next >= most) {
            CallThisTraced(At<uint32_t>(At<char*>(self, 0), 4), self, 0u);
            return;
        }
        if (At<float>(d, 0x28) >= M<float>(0x00A81054)) {
            const double acc = At<double>(self, 0x120) -
                               LfEmitterUniform(At<float>(d, 0x28), At<float>(d, 0x2C)) * At<double>(self, 0x130);
            At<double>(self, 0x120) = acc;
            const double n = LfUnsigned(At<uint32_t>(self, 0x128));
            if (acc < n) {
                At<float>(self, 0x190) = static_cast<float>((n - acc) * At<float>(At<char*>(self, 0x24), 0x14));
                At<double>(self, 0x120) = n;
                return;
            }
        }
    } while (LfUnsigned(At<uint32_t>(self, 0x128)) < At<double>(self, 0x120));
}
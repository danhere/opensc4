// A particle's life, written by hand from the game's code: a particle's
// step (0x005B9BA0) and its other step (0x005B9790), a particle meeting
// the water or the ground (0x005B8DC0), an effect's emit (0x005B96E0), the
// box of an effect not drawn (0x005B6390), an effect's wandering
// (0x005B2730) and the pull on its particles (0x005B6C40). Included by
// water_effects.cpp after effects_setup.h, whose helpers it uses.
//
// The x87 runs at 53 bits: intermediates are doubles, rounded to float
// where the game stores one, in the game's order; a value the game keeps
// on the x87 stack after storing it stays a double here too.

// 0x005B9BA0 (ret 8), slot 0x50 of both classes, with the particle and
// the step: descriptions with a flag of the mask at 0x00B22E14 go to the
// other step (0x005B9790) with the same two. Else the step is added to the
// age (+4), and a particle whose age has reached its life (+8) is ended
// (its first byte cleared, the answer false). The velocity (+0x1C) gets
// the step times the effect's velocity of the frame (+0x13C) and is
// multiplied by the constant at 0x00A81228 less the step times the
// description's +0x100; the position (+0xC) moves by the step times the
// double at 0x00A92D28 times the sum of the old velocity and the new. The
// game keeps some of the sums on the x87 stack and stores others first:
// the first component's new velocity is used as stored, the other two as
// worked out, and the first component's sum is stored before it is scaled.
uint32_t StepParticle(char* self, char* particle, float step)
{
    if (At<uint32_t>(At<char*>(self, 0x24), 0) & M<uint32_t>(0x00B22E14))
        return CallThis<uint32_t>(addr::SC4Effect_StepParticleFull, self, particle, step);
    const double s = step;
    const double age = s + At<float>(particle, 4);
    At<float>(particle, 4) = static_cast<float>(age);
    if (age >= At<float>(particle, 8)) {
        At<uint8_t>(particle, 0) = 0;
        return 0;
    }
    const double vx = At<float>(particle, 0x1C), vy = At<float>(particle, 0x20);
    const float oldVz = At<float>(particle, 0x24);
    const float dz = static_cast<float>(s * At<float>(self, 0x144));
    const double a = s * At<float>(self, 0x13C) + vx;
    const float af = static_cast<float>(a);
    At<float>(particle, 0x1C) = af;
    const double b = s * At<float>(self, 0x140) + vy;
    At<float>(particle, 0x20) = static_cast<float>(b);
    const double c = static_cast<double>(dz) + oldVz;
    At<float>(particle, 0x24) = static_cast<float>(c);
    const double keep = M<float>(0x00A81228) - s * At<float>(At<char*>(self, 0x24), 0x100);
    const float nx = static_cast<float>(af * keep);
    At<float>(particle, 0x1C) = nx;
    const float ny = static_cast<float>(b * keep);
    At<float>(particle, 0x20) = ny;
    const float nz = static_cast<float>(c * keep);
    At<float>(particle, 0x24) = nz;
    const float sx = static_cast<float>(static_cast<double>(nx) + vx);
    const double sy = static_cast<double>(ny) + vy;
    const double sz = static_cast<double>(nz) + oldVz;
    const double h = s * M<double>(0x00A92D28);
    const float mx = static_cast<float>(sx * h);
    const float my = static_cast<float>(sy * h);
    const double mz = h * sz;
    At<float>(particle, 0xC) = static_cast<float>(static_cast<double>(mx) + At<float>(particle, 0xC));
    At<float>(particle, 0x10) = static_cast<float>(static_cast<double>(my) + At<float>(particle, 0x10));
    At<float>(particle, 0x14) = static_cast<float>(mz + At<float>(particle, 0x14));
    return 1;
}

// Which emitter an emit calls, and with what time.
using LfEmitterFn = void (*)(char* self, uintptr_t emitter, float time);

// 0x005B96E0, slot 0x3C of both classes: nothing when +0x10 is set. The
// countdown at +0x190, while above zero (the double at 0x00A80990), is
// lowered by the frame's step (+0x108); while it stays above zero nothing
// is emitted, and once at or below it the time to emit for is what it
// went below by. When the description's +0x1C is above the float at
// 0x00A81054, the time is held to it less the double at 0x00A92688. The
// time goes to an emitter by the description's flags: 0x005B3CF0 for flag
// 4, 0x005B8B50 for flag 2, else 0x005B87B0 (through `emit`).
void EmitParticles(char* self, LfEmitterFn emit)
{
    if (At<uint8_t>(self, 0x10))
        return;
    const double zero = M<double>(0x00A80990);
    float time = At<float>(self, 0x108);
    if (At<float>(self, 0x190) > zero) {
        const double left = static_cast<double>(At<float>(self, 0x190)) - time;
        At<float>(self, 0x190) = static_cast<float>(left);
        if (left > zero)
            return;
        time = static_cast<float>(-left);
    }
    char* const description = At<char*>(self, 0x24);
    if (At<float>(description, 0x1C) > M<float>(0x00A81054)) {
        const double limit = static_cast<double>(At<float>(description, 0x1C)) - M<double>(0x00A92688);
        if (time > limit)
            time = static_cast<float>(limit);
    }
    const uint32_t flags = At<uint32_t>(description, 0);
    if (flags >> 2 & 1)
        emit(self, addr::SC4Effect_EmitToCount, time);
    else if (flags >> 1 & 1)
        emit(self, addr::SC4Effect_EmitBurst, time);
    else
        emit(self, addr::SC4Effect_EmitStream, time);
}
// 0x005B6390 (no arguments, plain ret), slot 0x4C of the base class: the
// box around an effect's particles taken again. The box's low corner
// (+0x1A0 to +0x1A8) is set to one and its high corner (+0x1AC to +0x1B4)
// to zero, so it starts empty (low above high). Each particle of the list
// at +0x14 (a ring with a head node: a node's next at +0, its particle at
// +8) has its position (+0xC) taken through the second block (its matrix
// by rows, its scale and place, the place's x kept as the x87's double);
// while the box is empty (low x above high x) the point makes it, else
// the point widens it: a high corner
// is kept only while it is above the point, a low one only while below
// (else the point's value, so NaN takes the point). When the box is not
// empty at the end it is grown on every side by the first float of the
// description's list at +0xAC. The answer is not used.
void KeepEffectBox(char* self)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    for (uint32_t i = 0; i < 3; ++i) {
        At<float>(self, 0x1A0 + i * 4) = 1.0f;
        At<uint32_t>(self, 0x1AC + i * 4) = 0;
    }
    char* const head = At<char*>(self, 0x14);
    for (char* node = At<char*>(head, 0); node != At<char*>(self, 0x14); node = At<char*>(node, 0)) {
        char* const particle = At<char*>(node, 8);
        const double px = At<float>(particle, 0xC), py = At<float>(particle, 0x10), pz = At<float>(particle, 0x14);
        const float x0 = static_cast<float>((pz * f(0xD0) + py * f(0xCC)) + px * f(0xC8));
        const float y0 = static_cast<float>((px * f(0xD4) + pz * f(0xDC)) + py * f(0xD8));
        const double z0 = (px * f(0xE0) + pz * f(0xE8)) + py * f(0xE4);
        const double scale = f(0xF8);
        const float xs = static_cast<float>(static_cast<double>(x0) * scale);
        const float ys = static_cast<float>(static_cast<double>(y0) * scale);
        const float zs = static_cast<float>(z0 * scale);
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
    const double grow = *At<float*>(At<char*>(self, 0x24), 0xAC);
    At<float>(self, 0x1A0) = static_cast<float>(f(0x1A0) - grow);
    At<float>(self, 0x1A4) = static_cast<float>(f(0x1A4) - grow);
    At<float>(self, 0x1A8) = static_cast<float>(f(0x1A8) - grow);
    At<float>(self, 0x1AC) = static_cast<float>(grow + f(0x1AC));
    At<float>(self, 0x1B0) = static_cast<float>(grow + f(0x1B0));
    At<float>(self, 0x1B4) = static_cast<float>(grow + f(0x1B4));
}

// The heading of the velocity (+0x158 over +0x150, the x87 arctangent),
// rounded from its 64-bit result straight to a float as the game stores it.
float SuWanderHeading(char* self)
{
    float heading = 0.0f;
    float* const out = &heading;
    __asm {
        mov eax, self
        fld dword ptr [eax + 0x158]
        fld dword ptr [eax + 0x150]
        fpatan
        mov edx, out
        fstp dword ptr [edx]
    }
    return heading;
}

// A heading made a velocity again: the cosine and the sine of the angle
// (64-bit results), each times the speed (a float) and stored as floats.
// The game takes one cosine and sine of a double still on its stack, the
// others of a float it reloads; loading either into the x87 is exact, so
// one helper serves both.
void SuWanderPolar(double angle, const float* speed, float* x, float* z)
{
    __asm {
        mov eax, speed
        mov ecx, x
        mov edx, z
        fld qword ptr [angle]
        fld st(0)
        fcos
        fmul dword ptr [eax]
        fstp dword ptr [ecx]
        fsin
        fmul dword ptr [eax]
        fstp dword ptr [edx]
    }
}

// 0x005B2730 (ret 4), an effect's wandering, with the step. The timer at
// +0x148 is lowered by the step (the check against the zero at 0x00A81054
// on the unrounded difference) and, each time it is at or below the zero,
// it gets a random time more (the description's +0x174, +0x178) and the
// velocity (+0x150 to +0x158) is turned. When the velocity's length in the
// plane (squared) is below the double at 0x00A9F190 it is first a random
// direction (0x0080B680, the generator at 0x00B22DF8). Its heading is
// taken (FPATAN) and a random speed drawn (+0x17C, +0x180). With a
// direction (+0x15C, the vector at +0x160), the heading is turned both ways
// by (a random amount up to +0x184 from the double at 0x00A80990, plus
// +0x188) times the float at 0x00AA0D04; of the two velocities so made the
// one further along the direction is taken if it is further along it than
// the old velocity is (its share along the direction over its length,
// times the speed); else the velocity is left. Without one, the heading is
// turned by a random amount within +0x184 plus the double at 0x00A95620
// (times 0x00AA0D04), then on by +0x188 times 0x00AA0D04 the same way, and
// the velocity is the speed along it. A new velocity has +0x154 cleared.
void WanderEffect(char* self, float step)
{
    const double k = M<float>(0x00A81054);
    const double c4 = M<float>(0x00AA0D04);
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const double left = f(0x148) - static_cast<double>(step);
    At<float>(self, 0x148) = static_cast<float>(left);
    if (!(left <= k))
        return;
    do {
        char* description = At<char*>(self, 0x24);
        const double more = SuUniform(At<float>(description, 0x174), At<float>(description, 0x178));
        At<float>(self, 0x148) = static_cast<float>(more + f(0x148));
        {
            const double x = f(0x150), z = f(0x158);
            if (z * z + x * x < M<double>(0x00A9F190)) {
                float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                reinterpret_cast<void*(__cdecl*)(float*, void*)>(0x0080B680)(out, reinterpret_cast<void*>(0x00B22DF8));
                At<float>(self, 0x150) = out[0];
                At<float>(self, 0x158) = out[1];
            }
        }
        const float heading = SuWanderHeading(self);
        description = At<char*>(self, 0x24);
        const float speed =
            static_cast<float>(SuUniform(At<float>(description, 0x17C), At<float>(description, 0x180)));
        description = At<char*>(self, 0x24);
        if (At<uint8_t>(self, 0x15C)) {
            const double r = SuUniform(M<double>(0x00A80990), At<float>(description, 0x184));
            const double turn = (r + At<float>(At<char*>(self, 0x24), 0x188)) * c4;
            const double plus = static_cast<double>(heading) + turn;
            const float minus = static_cast<float>(static_cast<double>(heading) - turn);
            float x1 = 0.0f, z1 = 0.0f, x2 = 0.0f, z2 = 0.0f;
            SuWanderPolar(plus, &speed, &x1, &z1);
            SuWanderPolar(minus, &speed, &x2, &z2);
            const double x = f(0x150), y = f(0x154), z = f(0x158);
            const double along = (z * f(0x168) + y * f(0x164)) + x * f(0x160);
            const double length = std::sqrt((x * x + y * y) + z * z);
            const double now = along / length * speed;
            const double d1 = (static_cast<double>(z1) * f(0x168) + static_cast<double>(x1) * f(0x160)) + f(0x164) * k;
            const float d2 = static_cast<float>((static_cast<double>(z2) * f(0x168) + f(0x164) * k) +
                                                static_cast<double>(x2) * f(0x160));
            if (d1 > static_cast<double>(d2)) {
                if (!(d1 > now))
                    continue;
                At<float>(self, 0x150) = x1;
                At<float>(self, 0x158) = z1;
            } else {
                if (!(static_cast<double>(d2) > now))
                    continue;
                At<float>(self, 0x150) = x2;
                At<float>(self, 0x158) = z2;
            }
        } else {
            const double spread = static_cast<double>(At<float>(description, 0x184)) + M<double>(0x00A95620);
            const double r = SuUniform(-spread, spread);
            const float turned = static_cast<float>(c4 * r + heading);
            const double by = static_cast<double>(At<float>(At<char*>(self, 0x24), 0x188)) * c4;
            const double angle = r < k ? static_cast<double>(turned) - by : by + turned;
            SuWanderPolar(angle, &speed, reinterpret_cast<float*>(self + 0x150), reinterpret_cast<float*>(self + 0x158));
        }
        At<uint32_t>(self, 0x154) = 0;
    } while (f(0x148) <= k);
}

// 0x005B6C40 (no arguments, plain RET; the answer is not used): the pull
// on an effect's particles from outside, as the effect's draw asks it. For
// a description with flag 0x08000000 the effect's place (+0xB4) is only kept
// at +0x178. Else a direction is found: with a kind (+0x1B4), the service
// at 0x00B43CEC (slot 0xBC, then its slot 0x58) is asked about the middle of
// the effect's box in a query of the game's own on the stack (0x005B2A40,
// freed by 0x005E8C40), the vector it answers made one long and turned back
// (the length, negated, the strength); without one, the way from the box's
// middle to +0x178 across the ground, made one long when it has a length
// (the length itself then the measure, +0x184 the strength). A strength of
// zero clears the velocity (+0x16C). Else the measure over the description's
// +0x1B0, held to zero to one, picks a value from the description's list at
// +0x1A4 (between two neighbours), times the strength; for flag 0x04000000
// that is kept at +0x1D0 and the velocity cleared, else the velocity is the
// direction taken through the second block (by columns, over its scale)
// times it. The game's stack frame (0x6C bytes) is kept as one buffer, the
// query at +0x28 of it (0x44 bytes), so what the service writes lands as in
// the game.
void PullEffect(char* self)
{
    char* description = At<char*>(self, 0x24);
    if (At<uint32_t>(description, 0) >> 27 & 1) {
        std::memcpy(self + 0x178, self + 0xB4, 12);
        return;
    }
    alignas(4) char frame[0x6C] = {};
    const auto v = [&](uint32_t at) -> float& { return At<float>(frame, at); };
    char* const query = frame + 0x28;
    const uint32_t zero = M<uint32_t>(0x00B22D70);
    At<uint32_t>(frame, 0x10) = zero;
    At<uint32_t>(frame, 0x14) = zero;
    At<uint32_t>(frame, 0x18) = zero;
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const double k = M<float>(0x00A81054);
    const double half = M<float>(0x00A84D2C);
    const double unit = M<float>(0x00B0A664);
    if (At<uint32_t>(description, 0x1B4)) {
        CallThis<uint32_t>(0x005B2A40, query);
        const double sx = f(0x1AC) + f(0x1A0);
        description = At<char*>(self, 0x24);
        const float sy = static_cast<float>(f(0x1B0) + f(0x1A4));
        const float sz = static_cast<float>(f(0x1B4) + f(0x1A8));
        At<float>(query, 0x3C) = static_cast<float>(sz * half);
        At<float>(query, 0x34) = static_cast<float>(sx * half);
        At<float>(query, 0x38) = static_cast<float>(sy * half);
        At<uint32_t>(query, 0x40) = At<uint32_t>(description, 0x1B4);
        char* const service = SuSlot<char*>(M<char*>(0x00B43CEC), 0xBC);
        SuSlot<uint32_t>(service, 0x58, query, frame + 0x10, frame + 0xC, frame + 8);
        const double x = v(0x10), y = v(0x14), z = v(0x18);
        At<uint32_t>(frame, 0) = 0;
        const double length = -std::sqrt((z * z + y * y) + x * x);
        const double r = unit / length;
        v(0x10) = static_cast<float>(static_cast<double>(v(0x10)) * r);
        v(0x14) = static_cast<float>(static_cast<double>(v(0x14)) * r);
        v(0x18) = static_cast<float>(static_cast<double>(v(0x18)) * r);
        v(4) = static_cast<float>(length);
        CallThis<uint32_t>(0x005E8C40, query);
    } else {
        At<uint32_t>(frame, 0x14) = 0;
        const float cx = static_cast<float>((f(0x1AC) + f(0x1A0)) * half);
        const double cz = (f(0x1B4) + f(0x1A8)) * half;
        const float dx = static_cast<float>(f(0x178) - cx);
        v(0x10) = dx;
        const double dz = f(0x180) - cz;
        v(0x18) = static_cast<float>(dz);
        const double length = std::sqrt(dz * dz + static_cast<double>(dx) * dx);
        v(0) = static_cast<float>(length);
        if (length > k) {
            const double r = unit / v(0);
            v(0x10) = static_cast<float>(dx * r);
            v(0x14) = static_cast<float>(k * r);
            v(0x18) = static_cast<float>(dz * r);
        }
        At<uint32_t>(frame, 4) = At<uint32_t>(self, 0x184);
    }
    const auto still = [&] {
        At<uint32_t>(self, 0x16C) = 0;
        At<uint32_t>(self, 0x170) = 0;
        At<uint32_t>(self, 0x174) = 0;
    };
    const double strength = v(4);
    if (strength == k) {
        still();
        return;
    }
    description = At<char*>(self, 0x24);
    double q = static_cast<double>(v(0)) / At<float>(description, 0x1B0);
    const double one = M<float>(0x00A81228);
    if (q < k)
        q = k;
    else if (q > one)
        q = one;
    const uint32_t begin = At<uint32_t>(description, 0x1A4);
    const auto item = [&](uint32_t i) { return static_cast<double>(*reinterpret_cast<float*>(begin + i * 4)); };
    const int32_t last = (static_cast<int32_t>(At<uint32_t>(description, 0x1A8) - begin) >> 2) - 1;
    const auto unsignedOf = [&](int32_t n) {
        double d = n;
        if (n < 0)
            d += M<float>(0x00A80AA8);
        return d;
    };
    double value = 0.0;
    if (last == 0) {
        value = item(0);
    } else {
        const double p = q * unsignedOf(last);
        const float rounded = static_cast<float>((p - half) + M<float>(0x00AA0CF8));
        uint32_t i = 0;
        std::memcpy(&i, &rounded, 4);
        i += 0xB4C00000u;
        const uint32_t next = i + 1;
        if (p == static_cast<double>(static_cast<int32_t>(next)))
            i = next;
        const double frac = p - unsignedOf(static_cast<int32_t>(i));
        if (frac > k)
            value = frac * (item(i + 1) - item(i)) + item(i);
        else
            value = item(i);
    }
    const double w = value * strength;
    if (At<uint32_t>(description, 0) >> 26 & 1) {
        At<float>(self, 0x1D0) = static_cast<float>(w);
        still();
        return;
    }
    const double x = v(0x10), y = v(0x14), z = v(0x18);
    const double nx = (z * f(0xE0) + y * f(0xD4)) + x * f(0xC8);
    const double ny = (z * f(0xE4) + y * f(0xD8)) + x * f(0xCC);
    const float nz = static_cast<float>((z * f(0xE8) + y * f(0xDC)) + x * f(0xD0));
    const double inverse = static_cast<float>(unit / f(0xF8));
    v(0x10) = static_cast<float>(nx * inverse);
    v(0x14) = static_cast<float>(ny * inverse);
    v(0x18) = static_cast<float>(static_cast<double>(nz) * inverse);
    At<float>(self, 0x174) = static_cast<float>(static_cast<double>(v(0x18)) * w);
    At<float>(self, 0x16C) = static_cast<float>(static_cast<double>(v(0x10)) * w);
    At<float>(self, 0x170) = static_cast<float>(static_cast<double>(v(0x14)) * w);
}

// 0x005B7030 (ret 4), with a word (0 from both callers, the effect's draw
// for flag 0x2000 and its taking of a description for flags 0x1000 and
// 0x2000): the limits of where particles start (+0x200 to +0x214) put on
// one edge of the map (its size at +0x1F4 and +0x1F8, as the set-up has
// it). For a description with flag 0x2000 the word grows by what the
// vector at +0x1E8's +8 says, its first float against its third and the
// one that is greater (the third when they are equal) against the zero at
// 0x00A81054: the first above the third and above zero 3, at most zero 2;
// else the third above zero 0, at most zero 1. Then by the word
// (unsigned): 0 the first axis from zero to +0x1F4 and the third at zero;
// 1 the first at +0x1F4 and the third from zero to +0x1F8; 2 the first
// from zero to +0x1F4 and the third at +0x1F8; 3 the first at zero and the
// third from zero to +0x1F8; anything else leaves them. The second axis
// is not touched.
void SetStartEdge(char* self, uint32_t which)
{
    if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 13 & 1) {
        char* const pushed = At<char*>(self, 0x1E8) + 8;
        const float a = At<float>(pushed, 0);
        const float b = At<float>(pushed, 8);
        const float zero = M<float>(0x00A81054);
        if (a > b)
            which += a > zero ? 3 : 2;
        else
            which += b > zero ? 0 : 1;
    }
    switch (which) {
    case 0:
        At<uint32_t>(self, 0x200) = 0;
        At<uint32_t>(self, 0x20C) = At<uint32_t>(self, 0x1F4);
        At<uint32_t>(self, 0x208) = 0;
        At<uint32_t>(self, 0x214) = 0;
        break;
    case 1:
        At<uint32_t>(self, 0x200) = At<uint32_t>(self, 0x1F4);
        At<uint32_t>(self, 0x20C) = At<uint32_t>(self, 0x1F4);
        At<uint32_t>(self, 0x208) = 0;
        At<uint32_t>(self, 0x214) = At<uint32_t>(self, 0x1F8);
        break;
    case 2:
        At<uint32_t>(self, 0x200) = 0;
        At<uint32_t>(self, 0x20C) = At<uint32_t>(self, 0x1F4);
        At<uint32_t>(self, 0x208) = At<uint32_t>(self, 0x1F8);
        At<uint32_t>(self, 0x214) = At<uint32_t>(self, 0x1F8);
        break;
    case 3:
        At<uint32_t>(self, 0x200) = 0;
        At<uint32_t>(self, 0x20C) = 0;
        At<uint32_t>(self, 0x208) = 0;
        At<uint32_t>(self, 0x214) = At<uint32_t>(self, 0x1F8);
        break;
    default:
        break;
    }
}

// 0x005B8410 (no arguments, plain RET), for descriptions with flag
// 0x80000000: effects started where particles are when they reach set
// ages. The description's table at +0x1CC holds entries of 16 bytes, a
// suffix (a string's address) first and an age at +0xC. For each particle
// of the list at +0x14 that runs (its first byte), with the table's count
// worked out anew, each entry whose age the particle's (+4) has not passed
// and will reach within the step: its age less the entry's at most the
// zero at 0x00A81054, and that plus the step (+0x108) above it (doubles,
// so a NaN starts nothing). `start` gets the effect, the suffix and the
// particle's place (+0xC), as 0x005B8290 does in the game. The table's
// first entry and the particle's age are read again for each entry.
template <typename Start>
void StartAtAges(char* self, Start&& start)
{
    char* const table = At<char*>(self, 0x24) + 0x1CC;
    const double zero = M<float>(0x00A81054);
    for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14);
         node = At<char*>(node, 0)) {
        const int32_t count = static_cast<int32_t>(At<uint32_t>(table, 4) - At<uint32_t>(table, 0)) >> 4;
        char* const particle = At<char*>(node, 8);
        if (!At<uint8_t>(particle, 0) || count <= 0)
            continue;
        for (int32_t i = 0; i < count; ++i) {
            char* const entry = At<char*>(table, 0) + i * 16;
            const double left = static_cast<double>(At<float>(particle, 4)) - At<float>(entry, 0xC);
            if (!(left <= zero))
                continue;
            if (left + At<float>(self, 0x108) > zero)
                start(self, At<uint32_t>(entry, 0), particle + 0xC);
        }
    }
}

// 0x005B84C0 (ret 4), with a particle the emitters just took and stepped,
// for descriptions with flag 0x80000000: effects started where it is born.
// For a running particle (its first byte), the same table as above (its
// count worked out once) is walked from its first entry while the entry's
// age is the zero at 0x00A81054 (floats, so -0 starts too and a NaN ends
// the walk); `start` gets the effect, the suffix and the particle's place
// (+0xC). The first entry with another age ends the walk. The table's
// first entry is read again for each entry.
template <typename Start>
void StartAtBirth(char* self, char* particle, Start&& start)
{
    char* const table = At<char*>(self, 0x24) + 0x1CC;
    if (!At<uint8_t>(particle, 0))
        return;
    const int32_t count = static_cast<int32_t>(At<uint32_t>(table, 4) - At<uint32_t>(table, 0)) >> 4;
    for (int32_t i = 0; i < count; ++i) {
        char* const entry = At<char*>(table, 0) + i * 16;
        if (!(At<float>(entry, 0xC) == M<float>(0x00A81054)))
            return;
        start(self, At<uint32_t>(entry, 0), particle + 0xC);
    }
}

// 0x005B8290 (ret 8), with a suffix (a string's address) and a place
// (three floats): another effect started there, named by the
// description's name (+0x160, the game's string, from its first character
// to its end) and the suffix. The game joins the two in a string of its
// own (at 0x00B22D5C, made once and kept, read nowhere else). The effects
// manager (0x00B43D1C) makes the effect of that name (slot 0x1C, the name
// and where to put it; the answer in AL). A made effect gets a block of
// two bytes (both 1; the game leaves the two after them as they were on
// its stack) and thirteen floats: the identity matrix by rows, the place
// and the scale 1 (slot 0x20); it is started (slot 0xC with 0). Whatever
// was put where the manager was asked to put it is let go (slot 8).
// `ops` makes, gives the block, starts and lets go (LfNamedOps for the
// game's objects).
template <typename Ops>
void StartNamed(char* self, const char* suffix, const float* place, Ops&& ops)
{
    char* const description = At<char*>(self, 0x24);
    std::string name(At<const char*>(description, 0x160), At<const char*>(description, 0x164));
    name += suffix;
    char* effect = nullptr;
    if (ops.Make(name.c_str(), &effect)) {
        char block[0x38] = {};
        block[0] = 1;
        block[1] = 1;
        for (const uint32_t at : {4u, 0x14u, 0x24u, 0x34u})
            At<uint32_t>(block, at) = 0x3F800000u;
        std::memcpy(block + 0x28, place, 12);
        ops.Block(effect, block);
        ops.Start(effect);
    }
    if (effect)
        ops.Release(effect);
}

struct LfNamedOps {
    bool Make(const char* name, char** out)
    {
        char* const manager = M<char*>(0x00B43D1C);
        return SuSlot<uint32_t>(manager, 0x1C, name, out) & 0xFF;
    }
    void Block(char* effect, char* block) { SuSlot<uint32_t>(effect, 0x20, block); }
    void Start(char* effect) { SuSlot<uint32_t>(effect, 0xC, 0u); }
    void Release(char* effect) { SuSlot<uint32_t>(effect, 8); }
};

// The game's list allocator at 0x00B207E4, got from the framework
// (0x3AE4BEA3, 0x3AE4BEAB, the holder released after) when there is none.
inline char* LfNodeAllocator()
{
    if (!M<char*>(addr::SC4List_NodeAllocator2)) {
        char* allocator = nullptr;
        if (char* const framework = reinterpret_cast<char*(__cdecl*)()>(addr::RZGetFramework)())
            SuSlot<uint32_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, &allocator);
        M<char*>(addr::SC4List_NodeAllocator2) = allocator;
        CallThis<uint32_t>(addr::SC4_ReleaseRef, &allocator);
    }
    return M<char*>(addr::SC4List_NodeAllocator2);
}

// 0x005B8020 (ret 4; the effect in ECX is not read), with a particle, for
// descriptions with bit 0 of their +4 at the end of the full step: a
// particle that meets a building or a prop ends. The box of one (the float
// at 0x00A81228) each way around its place (+0xC, as it is, not through
// the effect's blocks) is asked of the occupant manager (0x00B43D0C, slot
// 0x90 with the list, the two corners, 0 and -1) into a list whose head
// (0xC bytes, itself both ways) the allocator gives (slot 0xC). An
// occupant filter (0xC bytes: vtable 0x00A93628, cRZBaseUnknown's
// construct on its +4, then vtables 0x00AA0E28 and 0x00AA0E40) is made
// after the ask, kept in the list's second word and held (slot 4), and
// never used. The first occupant of the list, in order, of type 0x278128A0
// or 0xA823821E (slot 0x1C) gives its cells (slot 0x2C, four words); a
// cell region of them filled with ones is made (0x004422A0) and its rows
// freed at once; the particle's first byte is cleared and the walk ends.
// Last the list goes back to the manager (slot 0xC0), the filter is let
// go (slot 8), the list is cleared (0x00413E60) and its head freed (the
// allocator's slot 0x10).
inline void StepFlagB1(char* particle)
{
    const double one = M<float>(0x00A81228);
    float box[6];
    for (uint32_t i = 0; i < 3; ++i) {
        box[i] = static_cast<float>(static_cast<double>(At<float>(particle, 0xC + 4 * i)) - one);
        box[3 + i] = static_cast<float>(static_cast<double>(At<float>(particle, 0xC + 4 * i)) + one);
    }
    struct {
        char* head;
        char* filter;
    } list = {nullptr, nullptr};
    char* const head = SuSlot<char*>(LfNodeAllocator(), 0xC, 0xCu);
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    list.head = head;
    SuSlot<uint32_t>(M<char*>(addr::SC4_NetworkService), 0x90, &list, box, box + 3, 0u, 0xFFFFFFFFu);
    char* const end = list.head;
    char* node = At<char*>(end, 0);
    char* const filter = reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(0xC);
    if (filter) {
        At<uint32_t>(filter, 0) = 0x00A93628u;
        CallThis<uint32_t>(addr::cRZBaseUnknown_Construct, filter + 4);
        At<uint32_t>(filter, 0) = 0x00AA0E28u;
        At<uint32_t>(filter, 4) = 0x00AA0E40u;
        list.filter = filter;
        SuSlot<uint32_t>(filter, 4);
    }
    for (; node != end; node = At<char*>(node, 0)) {
        char* const occupant = At<char*>(node, 8);
        const uint32_t type = SuSlot<uint32_t>(occupant, 0x1C);
        if (type != 0x278128A0u && type != 0xA823821Eu)
            continue;
        int32_t rect[4] = {};
        SuSlot<uint32_t>(occupant, 0x2C, rect);
        struct {
            int32_t x0, z0, x1, z1;
            char cells[0x14];
        } region;
        region.x0 = rect[0];
        region.z0 = rect[1];
        region.x1 = rect[2];
        region.z1 = rect[3];
        const uint8_t fill = 1;
        CallThis<uint32_t>(addr::SC4CellRegion_ConstructCells, region.cells,
                           static_cast<uint32_t>(rect[2]) - static_cast<uint32_t>(rect[0]) + 1u,
                           static_cast<uint32_t>(rect[3]) - static_cast<uint32_t>(rect[1]) + 1u, &fill);
        At<uint8_t>(particle, 0) = 0;
        char** const rows = At<char**>(region.cells, 0x10);
        if (rows) {
            const uint32_t count = At<uint32_t>(region.cells, 4);
            for (uint32_t i = 0; i < count; ++i)
                reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(rows[i]);
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(rows);
        }
        break;
    }
    SuSlot<uint32_t>(M<char*>(addr::SC4_NetworkService), 0xC0, &list);
    if (list.filter)
        SuSlot<uint32_t>(list.filter, 8);
    CallThis<uint32_t>(addr::SC4List_Clear2, &list);
    char* const freed = list.head;
    SuSlot<uint32_t>(LfNodeAllocator(), 0x10, freed);
}

// The calls the ported code makes of the two: ours when their switches
// (renderer.effects.step_b1, renderer.effects.start_named) are ours, else
// the game's by its address (so through its detour in compare mode).
inline void LfStepB1(char* self, char* particle)
{
    if (g_stepB1Ours)
        StepFlagB1(particle);
    else
        CallThis<uint32_t>(addr::SC4Effect_StepFlagB1, self, particle);
}

inline void LfStartNamed(char* self, uint32_t suffix, void* place)
{
    if (g_startNamedOurs)
        StartNamed(self, reinterpret_cast<const char*>(static_cast<std::uintptr_t>(suffix)),
                   static_cast<const float*>(place), LfNamedOps{});
    else
        CallThis<uint32_t>(addr::SC4Effect_StartNamed, self, suffix, place);
}

// 0x005B6600 (ret 8): a particle's move along the description's path, for
// flag 0x10000000; the effect, the particle and the part of the step. The
// path's keys (0x20 bytes: a point, a direction, a time and an end) end at
// the description's +0x1BC; the key used is the last whose time (+0x18) is
// not above the particle's age, found walking back from the end. Past a
// key's end (+0x1C, none when zero) the particle moves as without a path.
// Else the time left (to the end, or to the particle's life) plus the part
// is the span; the key's point and direction are taken through the
// effect's transform (+0x90, the place +0xB4) and scale (+0xC0), the
// point's height the effect's +0x1FC less its +0xF0 for flag 0x8000. With
// a reach at the description's +0x1C4, a particle farther from the point
// than the span times the reach has its age set back by the time since the
// key and the span grown by as much. The part over the span is the curve's
// parameter: the four weights of the cubic matrix at 0x00B22D78 and their
// slopes take the position, the key's point, the velocity times the span
// and the direction times the span to the new position, and (the slopes
// over the span) to the new velocity.
inline void MoveAlongPath(char* self, char* particle, float part)
{
    const auto e = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const auto p = [&](uint32_t at) -> float& { return At<float>(particle, at); };
    const auto m = [](uint32_t i) { return static_cast<double>(M<float>(0x00B22D78 + 4 * i)); };
    char* const description = At<char*>(self, 0x24);
    const double age = p(4);
    char* key = At<char*>(description, 0x1BC);
    do
        key -= 0x20;
    while (age < At<float>(key, 0x18));
    const auto k = [&](uint32_t at) { return static_cast<double>(At<float>(key, at)); };
    const double pd = part;
    double span;
    if (At<float>(key, 0x1C) == 0.0f) {
        span = p(8) - age + pd;
    } else if (!(age > At<float>(key, 0x1C))) {
        span = k(0x1C) - age + pd;
    } else {
        const double mx = pd * p(0x1C), my = pd * p(0x20);
        const float mz = static_cast<float>(pd * p(0x24));
        p(0xC) = static_cast<float>(mx + p(0xC));
        p(0x10) = static_cast<float>(my + p(0x10));
        p(0x14) = static_cast<float>(static_cast<double>(mz) + p(0x14));
        return;
    }
    // The key's point and direction through the transform; the game keeps
    // some of the products on the x87 stack and stores others.
    const double scale = e(0xC0);
    const double kx = k(0), ky = k(4), kz = k(8);
    const float rx = static_cast<float>(kz * e(0x98) + ky * e(0x94) + kx * e(0x90));
    const float ry = static_cast<float>(kz * e(0xA4) + ky * e(0xA0) + kx * e(0x9C));
    const double rz = kz * e(0xB0) + ky * e(0xAC) + kx * e(0xA8);
    const float px = static_cast<float>(static_cast<float>(rx * scale) + e(0xB4));
    float py = static_cast<float>(static_cast<float>(ry * scale) + e(0xB8));
    const float pz = static_cast<float>(rz * scale + e(0xBC));
    const double dx = k(0xC), dy = k(0x10), dz = k(0x14);
    const double sx = dz * e(0x98) + dy * e(0x94) + dx * e(0x90);
    const double sy = dz * e(0xA4) + dy * e(0xA0) + dx * e(0x9C);
    const float sz = static_cast<float>(dz * e(0xB0) + dy * e(0xAC) + dx * e(0xA8));
    const float tx = static_cast<float>(sx * scale), ty = static_cast<float>(sy * scale),
                tz = static_cast<float>(sz * scale);
    if (At<uint32_t>(description, 0) >> 15 & 1)
        py = static_cast<float>(e(0x1FC) - e(0xF0));
    const float reach = At<float>(description, 0x1C4);
    if (!(reach == 0.0f)) {
        const double ox = p(0xC) - static_cast<double>(px), oy = p(0x10) - static_cast<double>(py),
                     oz = p(0x14) - static_cast<double>(pz);
        const double away = std::sqrt(ox * ox + oz * oz + oy * oy);
        if (span * reach < away) {
            const double back = age - k(0x18);
            span += back;
            p(4) = static_cast<float>(p(4) - back);
        }
    }
    const double inv = static_cast<double>(M<float>(0x00A81228)) / span;
    const double u = pd * inv, u2 = u * u, u3 = u2 * u;
    const float twoU = static_cast<float>(u + u), threeU2 = static_cast<float>(u2 * M<float>(0x00A85074));
    const float w0 = static_cast<float>(u * m(2) + u2 * m(1) + u3 * m(0) + m(3));
    const float w1 = static_cast<float>(u2 * m(5) + u * m(6) + u3 * m(4) + m(7));
    const float w2 = static_cast<float>(u2 * m(9) + u * m(10) + u3 * m(8) + m(11));
    const float w3 = static_cast<float>(m(14) * u + m(13) * u2 + m(12) * u3 + m(15));
    const double zero = M<float>(0x00A81054);
    const double s0 = twoU * m(1) + threeU2 * m(0) + m(3) * zero + m(2);
    const float s1 = static_cast<float>(twoU * m(5) + threeU2 * m(4) + m(7) * zero + m(6));
    const float s2 = static_cast<float>(twoU * m(9) + threeU2 * m(8) + m(11) * zero + m(10));
    const float s3 = static_cast<float>(m(13) * twoU + m(12) * threeU2 + m(15) * zero + m(14));
    const float w2s = static_cast<float>(w2 * span), w3s = static_cast<float>(w3 * span);
    const float s0i = static_cast<float>(s0 * inv), s1i = static_cast<float>(s1 * inv);
    // The new position.
    const double w3x = static_cast<double>(w3s) * tx;
    const float w3y = static_cast<float>(static_cast<double>(w3s) * ty),
                w3z = static_cast<float>(static_cast<double>(w3s) * tz);
    const double w2x = static_cast<double>(w2s) * p(0x1C), w2y = static_cast<double>(w2s) * p(0x20);
    const float w2z = static_cast<float>(static_cast<double>(w2s) * p(0x24));
    const double w1x = static_cast<double>(w1) * px, w1y = static_cast<double>(w1) * py;
    const float w1z = static_cast<float>(static_cast<double>(w1) * pz);
    const double w0x = static_cast<double>(w0) * p(0xC);
    const float w0y = static_cast<float>(static_cast<double>(w0) * p(0x10)),
                w0z = static_cast<float>(static_cast<double>(w0) * p(0x14));
    const float x1 = static_cast<float>(w0x + w1x);
    const double y1 = w0y + w1y;
    const float z1 = static_cast<float>(static_cast<double>(w0z) + w1z);
    const float x2 = static_cast<float>(x1 + w2x), y2 = static_cast<float>(y1 + w2y),
                z2 = static_cast<float>(static_cast<double>(z1) + w2z);
    const double x = w3x + x2, y = static_cast<double>(y2) + w3y;
    const float z = static_cast<float>(static_cast<double>(z2) + w3z);
    // The new velocity.
    const double s3x = static_cast<double>(s3) * tx, s3y = static_cast<double>(s3) * ty;
    const float s3z = static_cast<float>(static_cast<double>(s3) * tz);
    const double s2x = static_cast<double>(s2) * p(0x1C), s2y = static_cast<double>(s2) * p(0x20);
    const float s2z = static_cast<float>(static_cast<double>(s2) * p(0x24));
    const double s1x = static_cast<double>(s1i) * px;
    const float s1y = static_cast<float>(static_cast<double>(s1i) * py),
                s1z = static_cast<float>(static_cast<double>(s1i) * pz);
    const float s0x = static_cast<float>(static_cast<double>(s0i) * p(0xC)),
                s0y = static_cast<float>(static_cast<double>(s0i) * p(0x10)),
                s0z = static_cast<float>(static_cast<double>(s0i) * p(0x14));
    const float vx1 = static_cast<float>(s0x + s1x);
    const double vy1 = static_cast<double>(s0y) + s1y;
    const float vz1 = static_cast<float>(static_cast<double>(s0z) + s1z);
    const float vx2 = static_cast<float>(vx1 + s2x), vy2 = static_cast<float>(vy1 + s2y),
                vz2 = static_cast<float>(static_cast<double>(vz1) + s2z);
    p(0xC) = static_cast<float>(x);
    p(0x10) = static_cast<float>(y);
    p(0x14) = z;
    p(0x1C) = static_cast<float>(vx2 + s3x);
    p(0x20) = static_cast<float>(vy2 + s3y);
    p(0x24) = static_cast<float>(static_cast<double>(vz2) + s3z);
}

// 0x005B9790 (ret 8): a particle's full step, which the plain step
// (0x005B9BA0) hands over to for descriptions with a flag of the mask at
// 0x00B22E14; the effect, the particle and the step. The particle's age
// (+4) grows by the step and the particle is ended (its first byte cleared,
// the answer false) once the sum is not below its life (+8). Else the step
// is done in parts of at most the float at 0x00AA0CF4. Each part moves the
// position (+0xC) by the velocity (+0x1C) times the part (for flag
// 0x10000000 the move along a path, 0x005B6600, moves it instead) and adds
// the effect's velocity change (+0x13C) times the part to the velocity. For
// flag 0x8000000 the velocity is drawn towards the effect's +0x178: the
// way there made one long and times a value read from the description's
// list of floats (+0x1A4 to +0x1A8) at the distance over the description's
// +0x1B0 (at most one). The velocity is then damped by one less the step
// still to do times the description's +0x100. The position is taken to
// the terrain's grid through the second block (by columns, its scale and
// place); off the map (not above zero or not below +0x1F4, +0x1F8) the
// particle is ended for flag 0x800, on it for flags 0x100 or 0x400 when
// 0x005B8DC0 answers false. After the last part, bit 0 of the
// description's +4 sends the particle to 0x005B8020. The answer is the
// particle's first byte.
uint32_t StepParticleFull(char* self, char* particle, float step)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const auto p = [&](uint32_t at) -> float& { return At<float>(particle, at); };
    const auto end = [&] {
        At<uint8_t>(particle, 0) = 0;
        return 0u;
    };
    const double age = static_cast<double>(step) + p(4);
    p(4) = static_cast<float>(age);
    if (age >= p(8))
        return end();
    float left = step;
    for (;;) {
        const float most = M<float>(0x00AA0CF4);
        const float part = most < left ? most : left;
        const double pd = part;
        if (At<uint32_t>(At<char*>(self, 0x24), 0) >> 28 & 1) {
            CallThis<uint32_t>(addr::SC4Effect_MoveAlongPath, self, particle, part);
        } else {
            const double mx = pd * p(0x1C), my = pd * p(0x20);
            const float mz = static_cast<float>(pd * p(0x24));
            p(0xC) = static_cast<float>(mx + p(0xC));
            p(0x10) = static_cast<float>(my + p(0x10));
            p(0x14) = static_cast<float>(static_cast<double>(mz) + p(0x14));
        }
        {
            const double ax = pd * f(0x13C), ay = pd * f(0x140);
            const float az = static_cast<float>(pd * f(0x144));
            p(0x1C) = static_cast<float>(ax + p(0x1C));
            p(0x20) = static_cast<float>(ay + p(0x20));
            p(0x24) = static_cast<float>(static_cast<double>(az) + p(0x24));
        }
        char* description = At<char*>(self, 0x24);
        if (At<uint32_t>(description, 0) >> 27 & 1) {
            float dx = static_cast<float>(f(0x178) - p(0xC));
            float dy = static_cast<float>(f(0x17C) - p(0x10));
            const double dzd = f(0x180) - p(0x14);
            float dz = static_cast<float>(dzd);
            // The z square is the unrounded difference times the stored float.
            const double sum =
                (dzd * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
            if (sum > M<float>(0x00A81054)) {
                const double length = std::sqrt(sum);
                const double inverse = M<float>(0x00B0A664) / length;
                dx = static_cast<float>(dx * inverse);
                dy = static_cast<float>(dy * inverse);
                dz = static_cast<float>(dz * inverse);
                const float ratio = static_cast<float>(length / At<float>(description, 0x1B0));
                const float at = M<float>(0x00A81228) < ratio ? 1.0f : ratio;
                const float* const list = At<float*>(description, 0x1A4);
                const int32_t n =
                    static_cast<int32_t>(reinterpret_cast<char*>(At<float*>(description, 0x1A8)) -
                                         reinterpret_cast<const char*>(list)) >> 2;
                const int32_t last = n - 1;
                double value;
                if (last == 0) {
                    value = list[0];
                } else {
                    double nd = static_cast<double>(last);
                    if (last < 0)
                        nd = nd + M<float>(0x00A80AA8);
                    const double pos = nd * at;
                    const float rounded =
                        static_cast<float>((pos - M<float>(0x00A84D2C)) + M<float>(0x00AA0CF8));
                    uint32_t bits = 0;
                    std::memcpy(&bits, &rounded, 4);
                    const int32_t below = static_cast<int32_t>(bits + 0xB4C00000u);
                    const int32_t high = static_cast<int32_t>(static_cast<uint32_t>(below) + 1u);
                    const int32_t i = pos == static_cast<double>(high) ? below : high;
                    double id = static_cast<double>(i);
                    if (i < 0)
                        id = id + M<float>(0x00A80AA8);
                    const double fraction = pos - id;
                    if (fraction > M<float>(0x00A81054))
                        value = (static_cast<double>(list[i + 1]) - list[i]) * fraction + list[i];
                    else
                        value = list[i];
                }
                const double px = dx * value;
                const float py = static_cast<float>(dy * value);
                const float pz = static_cast<float>(dz * value);
                p(0x1C) = static_cast<float>(static_cast<double>(p(0x1C)) + px);
                p(0x20) = static_cast<float>(static_cast<double>(p(0x20)) + py);
                p(0x24) = static_cast<float>(static_cast<double>(p(0x24)) + pz);
            }
        }
        {
            // The damping is by the step still to do, not the part.
            const double damp =
                M<float>(0x00A81228) - static_cast<double>(left) * At<float>(At<char*>(self, 0x24), 0x100);
            p(0x1C) = static_cast<float>(damp * p(0x1C));
            p(0x20) = static_cast<float>(damp * p(0x20));
            p(0x24) = static_cast<float>(damp * p(0x24));
        }
        float grid[3];
        {
            const double x = p(0xC), y = p(0x10), z = p(0x14);
            const float gx0 = static_cast<float>((z * f(0xD0) + y * f(0xCC)) + x * f(0xC8));
            const float gy0 = static_cast<float>((x * f(0xD4) + z * f(0xDC)) + y * f(0xD8));
            const double gz0 = (z * f(0xE8) + y * f(0xE4)) + x * f(0xE0);
            const double scale = f(0xF8);
            const float gxs = static_cast<float>(gx0 * scale);
            const float gys = static_cast<float>(gy0 * scale);
            grid[0] = static_cast<float>(static_cast<double>(gxs) + f(0xEC));
            grid[1] = static_cast<float>(static_cast<double>(gys) + f(0xF0));
            grid[2] = static_cast<float>(gz0 * scale + f(0xF4));
        }
        const double low = M<float>(0x00A81054);
        const bool on = grid[0] > low && grid[2] > low && grid[0] < At<float>(self, 0x1F4) &&
                        grid[2] < At<float>(self, 0x1F8);
        const uint32_t flags = At<uint32_t>(At<char*>(self, 0x24), 0);
        if (on) {
            if (((flags >> 8 & 1) || (flags >> 10 & 1)) &&
                !(CallThis<uint32_t>(0x005B8DC0, self, particle, grid, part) & 0xFF))
                return end();
        } else if (flags >> 11 & 1) {
            return end();
        }
        const double rest = static_cast<double>(left) - part;
        left = static_cast<float>(rest);
        if (!(rest > M<double>(0x00A80990)))
            break;
    }
    if (At<uint8_t>(At<char*>(self, 0x24), 4) & 1)
        LfStepB1(self, particle);
    return At<uint8_t>(particle, 0);
}

// 0x005B8DC0 (ret 0xC): a particle meeting the water or the ground, as the
// full step (0x005B9790) asks it for descriptions with flag 0x100 or 0x400;
// the effect, the particle, its place on the terrain's grid (three floats)
// and the part of the step. The answer false ends the particle (the caller
// clears its first byte).
//
// A particle below the water's height (the grid's second float under the
// effect's +0x1FC) ends unless the description's +0x154 is zero: always
// for one, else when +0x154 is not below a random number (0x0091372E on
// the generator at 0x00B22DF8); an effect with +0x219 then starts another
// effect there (0x005B8290, the string at 0x00AA0F2C, the grid's x, the
// water's height and the grid's z). Otherwise the service at 0x00B43D1C is
// asked for the interface 0x0A5EAA1A (slot 0); without one the answer is
// true, with one it is held (slot 4) and let go (slot 8) at once, and let go
// again before each answer below.
//
// For flag 0x400 a point ahead of the particle (its position plus its
// velocity times the description's +0x144 over the velocity's length, or
// times the float at 0x00A8C950 when the length is not above the float at
// 0x00A94D50) is taken to the grid through the second block; off the map
// the answer is true. Its height over the ground (the interface's slot
// 0x18) below the description's +0x14C ends the particle; at or above
// +0x13C nothing happens; between, the ground's normal (slot 0x20) times
// the square of how far below +0x13C it is (its second part times +0x148)
// times the part and +0x140 is a push: for flag 0x800000 added to the
// effect's drift (+0x150), which is then scaled back to its old length
// (unless the new one is not above zero), else added to the particle's
// velocity. The answer is true.
//
// Else, when the description's +0x138 is zero: the terrain's height (the
// object at +0x1F0, slot 0x34) above the particle (or, for flag 0x200,
// any height) puts the particle on the ground (its position the grid
// point at that height, taken back through the second block) and the part
// of its velocity into the ground (the normal of slot 0x6C) is taken away:
// it slides. When +0x138 is not zero, a particle not below the height of
// slot 0x4C is left; else, with +0x218, an effect is started at the ground
// (0x005B8290, the string at 0x00AA0F20); the particle ends as by the water
// but with the description's +0x150; if it lives it is put on the ground
// and its velocity bounces: less the normal (through the second block's
// matrix) times its share along the velocity times one plus +0x138.
uint32_t ParticleMeetsGround(char* self, char* particle, float* grid, float part)
{
    const auto f = [&](uint32_t at) { return static_cast<double>(At<float>(self, at)); };
    const auto p = [&](uint32_t at) -> float& { return At<float>(particle, at); };
    const double k = M<float>(0x00A81054);
    const auto random = [] {
        return reinterpret_cast<double(__thiscall*)(void*)>(0x0091372E)(reinterpret_cast<void*>(0x00B22DF8));
    };
    // Whether a chance (+0x154 or +0x150) ends the particle: never at zero,
    // always at one, else when it is not below a random number.
    const auto ends = [&](char* description, uint32_t at) {
        const double chance = At<float>(description, at);
        if (chance == k)
            return false;
        if (At<uint32_t>(description, at) == 0x3F800000)
            return true;
        const double r = random();
        return chance >= r;
    };
    char* description = At<char*>(self, 0x24);
    if (grid[1] < At<float>(self, 0x1FC) && ends(description, 0x154)) {
        if (At<uint8_t>(self, 0x219)) {
            float at[3] = {grid[0], At<float>(self, 0x1FC), grid[2]};
            LfStartNamed(self, 0x00AA0F2Cu, at);
        }
        return 0;
    }
    char* const service = M<char*>(0x00B43D1C);
    char* ground = nullptr;
    SuSlot<uint32_t>(service, 0, 0x0A5EAA1Au, &ground);
    if (!ground)
        return 1;
    SuSlot<uint32_t>(ground, 4);
    SuSlot<uint32_t>(ground, 8);
    const auto done = [&](uint32_t answer) {
        SuSlot<uint32_t>(ground, 8);
        return answer;
    };
    description = At<char*>(self, 0x24);
    if (At<uint32_t>(description, 0) >> 10 & 1) {
        const double r = At<float>(description, 0x144);
        const float ax = static_cast<float>(r * p(0x1C));
        const float ay = static_cast<float>(r * p(0x20));
        const float az = static_cast<float>(r * p(0x24));
        const double vx = p(0x1C), vy = p(0x20), vz = p(0x24);
        const double length = std::sqrt((vz * vz + vy * vy) + vx * vx);
        float inverse;
        if (length > M<float>(0x00A94D50))
            inverse = static_cast<float>(M<float>(0x00B0A664) / length);
        else
            inverse = static_cast<float>(static_cast<double>(M<float>(0x00B0A664)) * M<float>(0x00A8C950));
        // x is stored as a float, y and z stay on the x87 as doubles.
        const float X = static_cast<float>(static_cast<double>(ax) * inverse + p(0xC));
        const double Y = static_cast<double>(ay) * inverse + p(0x10);
        const float azi = static_cast<float>(static_cast<double>(az) * inverse);
        const double Z = static_cast<double>(azi) + p(0x14);
        const float gx0 = static_cast<float>((Z * f(0xD0) + Y * f(0xCC)) + X * f(0xC8));
        const float gy0 = static_cast<float>((Z * f(0xDC) + Y * f(0xD8)) + X * f(0xD4));
        const double gz0 = (Z * f(0xE8) + Y * f(0xE4)) + X * f(0xE0);
        const double scale = f(0xF8);
        const float gxs = static_cast<float>(gx0 * scale);
        const float gys = static_cast<float>(gy0 * scale);
        const float hx = static_cast<float>(static_cast<double>(gxs) + f(0xEC));
        const float hy = static_cast<float>(static_cast<double>(gys) + f(0xF0));
        const float hz = static_cast<float>(gz0 * scale + f(0xF4));
        if (!(hx > k) || !(hz > k) || !(hx < At<float>(self, 0x1F4)) || !(hz < At<float>(self, 0x1F8)))
            return done(1);
        uint32_t hxBits = 0, hzBits = 0;
        std::memcpy(&hxBits, &hx, 4);
        std::memcpy(&hzBits, &hz, 4);
        const float above =
            static_cast<float>(static_cast<double>(hy) - SuSlot<double>(ground, 0x18, 0u, hxBits, hzBits));
        description = At<char*>(self, 0x24);
        if (above < At<float>(description, 0x14C))
            return done(0);
        if (!(above < At<float>(description, 0x13C)))
            return done(1);
        // The game's buffer holds the unscaled grid x and y from above.
        float out[3] = {gx0, gy0, 0.0f};
        const float* const normal = SuSlot<float*>(ground, 0x20, out, 0u, hxBits, hzBits);
        const double nx = normal[0], ny = normal[1];
        const float nz = normal[2];
        description = At<char*>(self, 0x24);
        const double t = static_cast<double>(At<float>(description, 0x13C)) - above;
        const float w = static_cast<float>(t * t);
        const float fx = static_cast<float>(w * nx);
        const float fy = static_cast<float>(ny * w * At<float>(description, 0x148));
        const float fz = static_cast<float>(static_cast<double>(nz) * w);
        const double pm = static_cast<double>(part) * At<float>(description, 0x140);
        if (At<uint32_t>(description, 0) >> 23 & 1) {
            const double x0 = f(0x150), y0 = f(0x154), z0 = f(0x158);
            const double before = (x0 * x0 + y0 * y0) + z0 * z0;
            At<float>(self, 0x150) = static_cast<float>(fx * pm + f(0x150));
            const float my = static_cast<float>(fy * pm);
            const float mz = static_cast<float>(fz * pm);
            At<float>(self, 0x154) = static_cast<float>(static_cast<double>(my) + f(0x154));
            At<float>(self, 0x158) = static_cast<float>(static_cast<double>(mz) + f(0x158));
            const double x1 = f(0x150), y1 = f(0x154), z1 = f(0x158);
            const double after = (x1 * x1 + y1 * y1) + z1 * z1;
            const float afterF = static_cast<float>(after);
            if (!(after > k))
                return done(1);
            const double ratio = std::sqrt(before) / std::sqrt(static_cast<double>(afterF));
            At<float>(self, 0x150) = static_cast<float>(ratio * f(0x150));
            At<float>(self, 0x154) = static_cast<float>(ratio * f(0x154));
            At<float>(self, 0x158) = static_cast<float>(ratio * f(0x158));
            return done(1);
        }
        const float my = static_cast<float>(fy * pm);
        const float mz = static_cast<float>(fz * pm);
        p(0x1C) = static_cast<float>(fx * pm + p(0x1C));
        p(0x20) = static_cast<float>(static_cast<double>(my) + p(0x20));
        p(0x24) = static_cast<float>(static_cast<double>(mz) + p(0x24));
        return done(1);
    }
    // The particle put on the grid point (its height given) and taken back
    // to the effect's space through the second block (by rows, its place
    // and scale undone).
    const auto putBack = [&](uint32_t height) {
        std::memcpy(particle + 0xC, grid, 12);
        At<uint32_t>(particle, 0x10) = height;
        p(0xC) = static_cast<float>(static_cast<double>(p(0xC)) - f(0xEC));
        p(0x10) = static_cast<float>(static_cast<double>(p(0x10)) - f(0xF0));
        p(0x14) = static_cast<float>(static_cast<double>(p(0x14)) - f(0xF4));
        const double inverse = static_cast<double>(M<float>(0x00B0A664)) / f(0xF8);
        p(0xC) = static_cast<float>(inverse * p(0xC));
        p(0x10) = static_cast<float>(inverse * p(0x10));
        p(0x14) = static_cast<float>(inverse * p(0x14));
        const double x = p(0xC), y = p(0x10), z = p(0x14);
        const double a = (f(0xE0) * z + f(0xD4) * y) + f(0xC8) * x;
        const double b = (f(0xE4) * z + f(0xD8) * y) + f(0xCC) * x;
        const float c = static_cast<float>((f(0xE8) * z + f(0xDC) * y) + f(0xD0) * x);
        p(0xC) = static_cast<float>(a);
        p(0x14) = c;
        p(0x10) = static_cast<float>(b);
    };
    uint32_t gxBits = 0, gzBits = 0;
    std::memcpy(&gxBits, &grid[0], 4);
    std::memcpy(&gzBits, &grid[2], 4);
    char* const terrain = At<char*>(self, 0x1F0);
    const uint32_t zero = M<uint32_t>(0x00B22D70);
    if (At<float>(description, 0x138) == k) {
        const double height = SuSlot<double>(terrain, 0x34, gxBits, gzBits);
        if (!(At<uint32_t>(At<char*>(self, 0x24), 0) >> 9 & 1) && !(height > grid[1]))
            return done(1);
        const float heightF = static_cast<float>(height);
        uint32_t heightBits = 0;
        std::memcpy(&heightBits, &heightF, 4);
        putBack(heightBits);
        uint32_t n[3] = {zero, zero, zero};
        SuSlot<uint32_t>(At<char*>(self, 0x1F0), 0x6C, gxBits, gzBits, n);
        float normal[3];
        std::memcpy(normal, n, 12);
        const double into =
            -((static_cast<double>(normal[2]) * p(0x24) + static_cast<double>(normal[1]) * p(0x20)) +
              static_cast<double>(normal[0]) * p(0x1C));
        if (!(into > k))
            return done(1);
        const double sx = normal[0] * into, sy = normal[1] * into;
        const float sz = static_cast<float>(normal[2] * into);
        p(0x1C) = static_cast<float>(sx + p(0x1C));
        p(0x20) = static_cast<float>(sy + p(0x20));
        p(0x24) = static_cast<float>(static_cast<double>(sz) + p(0x24));
        return done(1);
    }
    if (!(SuSlot<double>(terrain, 0x4C, gxBits, gzBits) > grid[1]))
        return done(1);
    const float height = static_cast<float>(SuSlot<double>(At<char*>(self, 0x1F0), 0x34, gxBits, gzBits));
    if (At<uint8_t>(self, 0x218)) {
        float at[3] = {grid[0], height, grid[2]};
        LfStartNamed(self, 0x00AA0F20u, at);
    }
    description = At<char*>(self, 0x24);
    if (ends(description, 0x150))
        return done(0);
    uint32_t heightBits = 0;
    std::memcpy(&heightBits, &height, 4);
    putBack(heightBits);
    uint32_t n[3] = {zero, zero, zero};
    SuSlot<uint32_t>(At<char*>(self, 0x1F0), 0x6C, gxBits, gzBits, n);
    float normal[3];
    std::memcpy(normal, n, 12);
    description = At<char*>(self, 0x24);
    const double nx = normal[0], ny = normal[1], nz = normal[2];
    // The normal through the second block's matrix: x and y stay doubles,
    // z goes through a float.
    const double a = (nz * f(0xE0) + ny * f(0xD4)) + nx * f(0xC8);
    const double b = (nz * f(0xE4) + ny * f(0xD8)) + nx * f(0xCC);
    const float c = static_cast<float>((nz * f(0xE8) + ny * f(0xDC)) + nx * f(0xD0));
    const double along = (a * p(0x1C) + static_cast<double>(c) * p(0x24)) + b * p(0x20);
    const float m = static_cast<float>(along * (static_cast<double>(At<float>(description, 0x138)) + M<float>(0x00A81228)));
    const float am = static_cast<float>(a * m);
    const double bm = b * m;
    const double cm = static_cast<double>(c) * m;
    p(0x1C) = static_cast<float>(static_cast<double>(p(0x1C)) - am);
    p(0x20) = static_cast<float>(static_cast<double>(p(0x20)) - bm);
    p(0x24) = static_cast<float>(static_cast<double>(p(0x24)) - cm);
    return done(1);
}

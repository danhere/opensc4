// The effects manager's time step (0x0058E580) and an effect's start
// actions (0x005908A0), written by hand from the game's code. Both act
// through other objects' tables (the proxied calls of the tail's
// comparison) and a few direct helpers (the Tm calls, noted in a replay).
// Included by water_effects.cpp.
//
// The x87 runs at 53 bits: intermediates are doubles, rounded to float
// where the game stores one, in the game's order.

// 0x0058E580 (ret 4): the step (ms, times the speed of the service at
// 0x00B43D5C, slot 0x80, truncated as _ftol2) added to the time at +0x1B8,
// that in seconds at +0x1BC (times 0.000001); the cursor's screen place
// (the framework's object 0xA417445E/0x5A4, its slot 0x84) as a ray into
// the world (the city view's 0x007F9080, its point and direction first
// the word at 0x00B22C5C); then each compound effect in use that is
// running (slot 0x14) gets the point and the direction (slot 0x2C with 9,
// the point and 2 vectors, with flag 0x20 at +0x28) and its part at +4 the
// step in seconds (slot 0x10); the cursor's holder let go (0x004177F0).
void StepEffectsTime(char* manager, uint32_t ms)
{
    uint32_t step = ms;
    if (char* const speed = M<char*>(0x00B43D5C))
        step = static_cast<uint32_t>(static_cast<int64_t>(TmSlotDouble(speed, 0x80) * HmUnsigned(ms)));
    At<uint32_t>(manager, 0x1B8) += step;
    At<float>(manager, 0x1BC) =
        static_cast<float>(HmUnsigned(At<uint32_t>(manager, 0x1B8)) * M<double>(0x00A9F190));
    char* holder = nullptr;
    if (char* const framework = reinterpret_cast<char*(__cdecl*)()>(addr::RZGetFramework)())
        VrCallSlot<uint32_t>(framework, 0x14, 0xA417445Eu, 0x5A4u, &holder);
    const uint32_t fill = M<uint32_t>(0x00B22C5C);
    uint32_t x = 0, z = 0;
    // The point and the direction side by side, as on the game's stack: the
    // effects' slot 0x2C is given two vectors (six floats) from the point.
    uint32_t ray[6] = {fill, fill, fill, fill, fill, fill};
    uint32_t* const point = ray;
    VrCallSlot<uint32_t>(holder, 0x84, &x, &z);
    TmScreenRay(M<char*>(0x00B43DD8), x, z, point, ray + 3);
    for (uint32_t i = 0; i < static_cast<uint32_t>(At<uint32_t*>(manager, 0xBB0) - At<uint32_t*>(manager, 0xBAC));
         ++i) {
        const uint32_t index = At<uint32_t*>(manager, 0xBAC)[i];
        if (!VrCallSlot<uint8_t>(At<char**>(manager, 0xB94)[index], 0x14))
            continue;
        char* const effect = At<char**>(manager, 0xB94)[index];
        if (At<uint32_t>(effect, 0x28) >> 5 & 1)
            VrCallSlot<uint32_t>(effect, 0x2C, 9u, point, 2u);
        const float seconds = static_cast<float>(HmUnsigned(step) * M<double>(0x00A9F190));
        uint32_t bits = 0;
        std::memcpy(&bits, &seconds, 4);
        VrCallSlot<uint32_t>(At<char**>(manager, 0xB94)[index] + 4, 0x10, bits);
    }
    TmReleaseHolder(&holder);
}

// 0x005908A0 (ret 4): an effect's start actions, those of its record in
// the store (0x38 bytes from +0xAC, by the effect's +0x20; the actions
// 0x18 bytes each from the record's +0x14). The effect's place on the
// ground: its point (+0x94) by the matrix at +0x38 (x from +0x38, +0x3C,
// +0x40; z from +0x50, +0x54, +0x58), times its scale (+0x68), plus its
// offset (+0x5C, +0x64). +0x144 cleared, then by each action's flags: 1 a
// sound (the renderer at 0x00B43DD0, slot 0xA8, with the sound at +0x12C
// and, with flag 2, the place), its handle kept at +0x144; else 4 a
// shake (at +0x14C, 0x1C bytes each): with flag 2 at the place
// (0x00590540), else the manager's own (+0xD28, +0xD34 assigned, +0xD40
// its length, +0xD48 one over it, +0xD44 and +0xD4C cleared); else 8 a
// flash (0x0058FE70).
void StartEffectActions(char* manager, char* effect)
{
    char* const record = At<char*>(manager, 0xAC) + At<uint32_t>(effect, 0x20) * 0x38;
    const double a0 = At<float>(effect, 0x94), a1 = At<float>(effect, 0x98), a2 = At<float>(effect, 0x9C);
    const double x = (a2 * At<float>(effect, 0x40) + a1 * At<float>(effect, 0x3C)) + a0 * At<float>(effect, 0x38);
    const double z = (a2 * At<float>(effect, 0x58) + a1 * At<float>(effect, 0x54)) + a0 * At<float>(effect, 0x50);
    const double scale = At<float>(effect, 0x68);
    const float scaled = static_cast<float>(static_cast<double>(static_cast<float>(x)) * scale);
    const float px = static_cast<float>(static_cast<double>(scaled) + At<float>(effect, 0x5C));
    const float pz = static_cast<float>(z * scale + At<float>(effect, 0x64));
    At<uint32_t>(effect, 0x144) = 0;
    for (char* action = At<char*>(record, 0x14); action != At<char*>(record, 0x18); action += 0x18) {
        const uint32_t flags = At<uint32_t>(action, 0);
        float place[2] = {px, pz};
        if (flags & 1) {
            char* const renderer = M<char*>(0x00B43DD0);
            char* const sound = At<char*>(manager, 0x12C) + At<uint32_t>(action, 0x14) * 0x28;
            At<uint32_t>(effect, 0x144) =
                VrCallSlot<uint32_t>(renderer, 0xA8, sound, (flags >> 1 & 1) ? place : nullptr);
        } else if (flags >> 2 & 1) {
            char* const shake = At<char*>(manager, 0x14C) + At<uint32_t>(action, 0x14) * 0x1C;
            if (flags >> 1 & 1) {
                TmShakeAt(manager, shake, place, At<uint32_t>(action, 0x10));
            } else {
                DcAssignFloat3s(manager + 0xD28, shake);
                LsAssignFloats(manager + 0xD34, shake + 0xC);
                At<uint32_t>(manager, 0xD40) = At<uint32_t>(shake, 0x18);
                At<float>(manager, 0xD48) =
                    static_cast<float>(static_cast<double>(M<float>(0x00A81228)) / At<float>(manager, 0xD40));
                At<uint32_t>(manager, 0xD44) = 0;
                At<uint8_t>(manager, 0xD4C) = 0;
            }
        } else if (flags >> 3 & 1) {
            TmFlash(manager, At<char*>(manager, 0x14C) + At<uint32_t>(action, 0x14) * 0x1C);
        }
    }
}

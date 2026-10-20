// Nine of the effects manager's small functions that act through other
// objects' tables, written by hand from the game's code: the script
// registry's register (0x0058DE80) and unregister (0x0058DF10), an
// effect's follow-up start (0x0058E340), the place given to the compound
// effects (0x0058E4A0), the Release thunks of the manager's interfaces at
// +4 and +8 (0x0058FD70, 0x0058FD90), and the lift of the particles' floor
// under an occupant (0x0058E260, 0x0058E720, 0x0058F140). Included by
// water_effects.cpp, which has the Tl calls (noted in a replay).

template <typename R, typename... Args>
R VrCallSlot(char* object, uint32_t slot, Args... args)
{
    return CallThis<R>(At<uint32_t>(At<char*>(object, 0), slot), object, args...);
}

// 0x0058DE80 (cdecl, seven words): with a script (the seventh) its slot
// 0x54 asked first; the fourth, when given, asked for its unknown (slot 0,
// id 1) and that handed to the script's slot 0x24, whose answer (or slot
// 0x54's when there is no fourth) must be set; then the script's slot 0xC
// with the first three words, that answer, the fifth with bit 0 set and the
// sixth; the unknown let go (slot 8). Answers slot 0xC's, or 0.
uint32_t RegisterScriptFunction(uint32_t a, uint32_t b, uint32_t c, char* object, uint32_t flags, uint32_t f,
                                char* script)
{
    if (!script)
        return 0;
    uint32_t answer = VrCallSlot<uint32_t>(script, 0x54);
    char* unknown = nullptr;
    if (object) {
        VrCallSlot<uint8_t>(object, 0, 1u, &unknown);
        answer = VrCallSlot<uint32_t>(script, 0x24, unknown);
    }
    if (!answer) {
        if (unknown)
            VrCallSlot<uint32_t>(unknown, 8);
        return 0;
    }
    const uint32_t result = VrCallSlot<uint32_t>(script, 0xC, a, b, c, answer, flags | 1, f);
    if (unknown)
        VrCallSlot<uint32_t>(unknown, 8);
    return result;
}

// 0x0058DF10 (cdecl): the object's unknown (slot 0, id 1) handed to the
// script's slot 0x1C, then let go (slot 8). Answers slot 0x1C's AL; false
// without either.
uint32_t UnregisterScriptTable(char* object, char* script)
{
    if (!script || !object)
        return 0;
    char* unknown = nullptr;
    VrCallSlot<uint8_t>(object, 0, 1u, &unknown);
    const uint8_t result = VrCallSlot<uint8_t>(script, 0x1C, unknown);
    if (unknown)
        VrCallSlot<uint32_t>(unknown, 8);
    return result;
}

// 0x0058E340 (ret 4): the effect's +0x144 given to the renderer at
// 0x00B43DD0 (its slot 0xB0) and cleared; then, when the effect's record in
// the store (0x38 bytes each from +0xAC, by the effect's +0x20) has a
// follow-up name (+0x2C, not empty), the manager's slot 0x1C starts it by
// that name and the new effect gets the effect's +0x6C (slot 0x20), +0x34
// (slot 0x1C) and slot 0xC with 0, and is let go (slot 8).
void StartFollowUp(char* manager, char* effect)
{
    char* const record = At<char*>(manager, 0xAC) + At<uint32_t>(effect, 0x20) * 0x38;
    if (const uint32_t held = At<uint32_t>(effect, 0x144)) {
        VrCallSlot<uint32_t>(M<char*>(0x00B43DD0), 0xB0, held);
        At<uint32_t>(effect, 0x144) = 0;
    }
    char* const name = At<char*>(record, 0x2C);
    if (name == At<char*>(record, 0x30))
        return;
    char* started = nullptr;
    if (VrCallSlot<uint8_t>(manager, 0x1C, name, &started)) {
        VrCallSlot<uint32_t>(started, 0x20, effect + 0x6C);
        VrCallSlot<uint32_t>(started, 0x1C, effect + 0x34);
        VrCallSlot<uint32_t>(started, 0xC, 0u);
    }
    if (started)
        VrCallSlot<uint32_t>(started, 8);
}

// 0x0058E4A0: each compound effect in use (the indices at +0xBAC into the
// table at +0xB94) with flag 1 at +0x28 given its +0x6C (slot 0x20).
void PlaceCompounds(char* manager)
{
    for (uint32_t* at = At<uint32_t*>(manager, 0xBAC); at != At<uint32_t*>(manager, 0xBB0); ++at) {
        char* const compound = At<char**>(manager, 0xB94)[*at];
        if (At<uint8_t>(compound, 0x28) & 1)
            VrCallSlot<uint32_t>(compound, 0x20, compound + 0x6C);
    }
}

// 0x0058FD70 and 0x0058FD90: the manager's Release for its interfaces at
// +4 and +8 (this less 4 or 8) is the shared stub 0x008CD450, which
// answers 1.
uint32_t ReleaseManagerFrom(char* self, uint32_t offset)
{
    return CallThis<uint32_t>(addr::SC4_ReturnOne, self - offset);
}

// 0x0058E260 (the occupant in EDI, an answer in AL): whether an occupant
// lifts the particles' floor. Its type (slot 0x18) must not have
// 0x029244DB and must have one of 0x27812820 to 0x27812824 (slot 0xC);
// then a building (the occupant's interface 0x87DDA3A9, 0x0048FF50, let go
// again) does, and anything else when its type's property 0x8A5E5DB8 is
// true (0x005FD390).
uint32_t OccupantLifts(char* occupant)
{
    char* const type = VrCallSlot<char*>(occupant, 0x18);
    if (!type || VrCallSlot<uint8_t>(type, 0xC, 0x029244DBu))
        return 0;
    bool kind = false;
    for (const uint32_t id : {0x27812821u, 0x27812820u, 0x27812822u, 0x27812823u, 0x27812824u}) {
        if (VrCallSlot<uint8_t>(type, 0xC, id)) {
            kind = true;
            break;
        }
    }
    if (!kind)
        return 0;
    char* held = nullptr;
    TlQueryHeld(&held, occupant, 0x87DDA3A9u, 0u);
    const bool building = held != nullptr;
    if (held)
        VrCallSlot<uint32_t>(held, 8);
    if (building)
        return 1;
    uint8_t flag = 0;
    if (!TlGetPropertyBool(type, 0x8A5E5DB8u, &flag))
        return 0;
    return flag ? 1 : 0;
}

// 0x0058E720 (ret 4): with a height map (+0xD98) and +0x1AE clear, an
// occupant that lifts (0x0058E260) raises the map's first layer to its
// height (its box's high y less its low y, slot 0x28) over its cell
// rectangle (slot 0x2C) one wider to the high side, two rings (the map's
// slot 0x28); +0xDA0 keeps the highest top seen.
void LiftUnderOccupant(char* manager, char* occupant)
{
    char* const map = At<char*>(manager, 0xD98);
    if (!map || At<uint8_t>(manager, 0x1AE) || !TlOccupantLifts(occupant))
        return;
    float low[3] = {1.0f, 1.0f, 1.0f}, high[3] = {0.0f, 0.0f, 0.0f};
    VrCallSlot<uint32_t>(occupant, 0x28, low, high);
    const float height = static_cast<float>(static_cast<double>(high[1]) - low[1]);
    int32_t rect[4] = {};
    VrCallSlot<uint32_t>(occupant, 0x2C, rect);
    ++rect[2];
    ++rect[3];
    uint32_t bits = 0;
    std::memcpy(&bits, &height, 4);
    VrCallSlot<uint32_t>(map, 0x28, 1u, rect, bits, 2u);
    if (high[1] > At<float>(manager, 0xDA0))
        At<float>(manager, 0xDA0) = high[1];
}

// 0x0058F140 (cdecl, a message handler: the occupant, the manager):
// 0x0058E720, answering 1.
uint32_t LiftUnderOccupantMessage(char* occupant, char* manager)
{
    TlLiftUnderOccupant(manager, occupant);
    return 1;
}

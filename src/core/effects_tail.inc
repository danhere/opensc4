// Nine of the effects manager's small functions that act through other
// objects' tables, written by hand from the game's code: the script
// registry's register (0x0058DE80) and unregister (0x0058DF10), an
// effect's follow-up start (0x0058E340), the place given to the compound
// effects (0x0058E4A0), the Release thunks of the manager's interfaces at
// +4 and +8 (0x0058FD70, 0x0058FD90), and the lift of the particles' floor
// under an occupant (0x0058E260, 0x0058E720, 0x0058F140). Included by
// water_effects.cpp, which has the Tl calls (noted in a replay).

template <typename T>
uint32_t VrWord(T value)
{
    if constexpr (std::is_pointer_v<T>)
        return Word(value);
    else
        return static_cast<uint32_t>(value);
}

// A call through an object's table. In ours' replay one on an object not
// holding its proxy's copy is never made: it is noted and answered as the
// game's was there (a proxied one's thunk does that itself).
template <typename R, typename... Args>
R VrCallSlot(char* object, uint32_t slot, Args... args)
{
    if (g_stReplay && g_stDepth == 0 && GetCurrentThreadId() == g_stThread) {
        VrProxy* const proxy = VrFind(object);
        if (!proxy || At<uint32_t>(object, 0) != proxy->clone) {
            const uint32_t words[] = {VrWord(args)..., 0u, 0u, 0u};
            uint32_t rest = sizeof...(Args) > 2 ? StArg(Ptr(words[2])) : 0;
            for (std::size_t i = 3; i < sizeof...(Args); ++i)
                rest = (rest ^ StArg(Ptr(words[i]))) * 16777619u;
            const uint32_t function = At<uint32_t>(Ptr(VrOriginalOf(At<uint32_t>(object, 0))), slot);
            const uint32_t result =
                StReplayed(StMake(function, object, sizeof...(Args) > 0 ? Ptr(words[0]) : nullptr,
                                  sizeof...(Args) > 1 ? Ptr(words[1]) : nullptr, rest),
                           nullptr);
            if constexpr (std::is_pointer_v<R>)
                return reinterpret_cast<R>(result);
            else
                return static_cast<R>(result);
        }
    }
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

// Slot 5 of the manager's table (0x005916C0, ret 4): whether a name is one
// of its compound descriptions': none for no name or an empty one; else
// the name copied into the manager's string at +0x1A0 (0x0044D3A0), lowered
// in place (0x00586AC0 with CRT::tolower), and looked up in the hash of
// names at +0x98 (0x0051C9F0). Answers AL.
uint32_t HasDescriptionNamed(char* manager, const char* name)
{
    if (!name || !*name)
        return 0;
    char* const text = manager + 0x1A0;
    TlAssignChars(text, name, name + std::strlen(name));
    TlLower(At<char*>(text, 0), At<char*>(text, 4));
    return TlFindName(manager + 0x98, text) != 0;
}

// Slot 3 of the tables' interface (0x0058F690; this is the manager + 0x94):
// with the message server at 0x00B43CCC, each message the manager listens
// for (16 bytes each from +0xFC) taken off for its target (the manager +
// 8, slot 0x18); the two dword hashes at +0xC6C and +0xC80 cleared
// (0x008EBA60); then the texts' lists cleared (jumps to 0x0058BEF0).
uint32_t StopListening(char* tables)
{
    if (char* const server = M<char*>(0x00B43CCC)) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(At<char*>(tables, 0x100) - At<char*>(tables, 0xFC)) >> 4; ++i)
            VrCallSlot<uint32_t>(server, 0x18, tables - 0x94 ? tables - 0x8C : nullptr,
                                 At<uint32_t>(At<char*>(tables, 0xFC) + i * 0x10, 0));
    }
    TlClearHash(tables + 0xC6C);
    TlClearHash(tables + 0xC80);
    return TlClearTexts(tables);
}

// Slot 4 of the tables' interface (0x005900A0; this is the manager +
// 0x94): the game first calls an empty function (0x00407990), left out
// here. The list of floats at +0xD4 assigned to +0xC20 (0x00464430) and the
// four words after it copied; the list read at the city view's zoom (its
// +0xC, times 0.25, times the list's last index, floored by the 1.5 x
// 2^23 rounding and the rest a fraction between two values) into +0xC3C.
// Then for each 20-byte entry from +0xF0, the name map at +4's equal
// range for it (0x00586EF0) walked: each node's value with the entry's
// +0xC and +0x10 put into the pair hash at +0xC6C (grown first to one more
// than its count, 0x0058F3C0; inserted, 0x0058F230), the walk going to the
// next bucket's first node at the end of a chain (a name's hash h * 5 plus
// each character). The messages (16 bytes each from +0xFC) numbered in the
// map at +0xC80 (0x004F8D30) and, with the message server at 0x00B43CCC,
// each listened for (slot 0x14) for the manager + 8.
uint32_t StartListening(char* tables)
{
    LsAssignFloats(tables + 0xC20, tables + 0xD4);
    for (uint32_t at = 0xC; at <= 0x18; at += 4)
        At<uint32_t>(tables, 0xC20 + at) = At<uint32_t>(tables, 0xD4 + at);
    float* const curve = At<float*>(tables, 0xC20);
    const uint32_t last = static_cast<uint32_t>((At<char*>(tables, 0xC24) - reinterpret_cast<char*>(curve)) >> 2) - 1;
    if (last == 0) {
        At<float>(tables, 0xC3C) = curve[0];
    } else {
        const double place = static_cast<double>(At<int32_t>(M<char*>(0x00B43DD8), 0xC)) * M<float>(0x00A8821C) *
                             HmUnsigned(last);
        const uint32_t i = HmFloor(place);
        const double fraction = place - HmUnsigned(i);
        At<float>(tables, 0xC3C) =
            fraction > 0.0
                ? static_cast<float>((static_cast<double>(curve[i + 1]) - curve[i]) * fraction + curve[i])
                : curve[i];
    }
    // The pair hash's count (+0xC7C), kept here: one more for each pair the
    // insert says it put in (in a replay the insert's note has only that).
    uint32_t pairs = At<uint32_t>(tables, 0xC7C);
    for (uint32_t e = 0; e < static_cast<uint32_t>((At<char*>(tables, 0xF4) - At<char*>(tables, 0xF0)) / 20); ++e) {
        char* const entry = At<char*>(tables, 0xF0) + e * 20;
        char range[0x10] = {};
        TlEqualRange(tables + 4, range, entry);
        char* node = At<char*>(range, 0);
        char* const map = At<char*>(range, 4);
        while (node != At<char*>(range, 8)) {
            uint32_t pair[3] = {At<uint32_t>(entry, 0xC), At<uint32_t>(entry, 0x10), At<uint32_t>(node, 0x10)};
            TlGrowPairs(tables + 0xC6C, pairs + 1);
            char out[0xC] = {};
            TlInsertPair(tables + 0xC6C, out, pair);
            if (At<uint8_t>(out, 8))
                ++pairs;
            if (At<char*>(node, 0)) {
                node = At<char*>(node, 0);
                continue;
            }
            uint32_t hash = 0;
            const char* const name = At<char*>(node, 4);
            const uint32_t length = static_cast<uint32_t>(At<char*>(node, 8) - name);
            for (uint32_t c = 0; c < length; ++c)
                hash = hash * 5 + static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(name[c])));
            char** const buckets = At<char**>(map, 4);
            const uint32_t count = static_cast<uint32_t>(At<char*>(map, 8) - reinterpret_cast<char*>(buckets)) >> 2;
            uint32_t bucket = hash % count;
            node = nullptr;
            while (++bucket < count) {
                if ((node = buckets[bucket]))
                    break;
            }
        }
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>((At<char*>(tables, 0x100) - At<char*>(tables, 0xFC)) >> 4); ++i) {
        if (uint32_t* const number = TlLookup(tables + 0xC80, At<char*>(tables, 0xFC) + i * 0x10))
            *number = i;
    }
    if (char* const server = M<char*>(0x00B43CCC)) {
        for (uint32_t i = 0; i < static_cast<uint32_t>((At<char*>(tables, 0x100) - At<char*>(tables, 0xFC)) >> 4);
             ++i)
            VrCallSlot<uint32_t>(server, 0x14, tables - 0x94 ? tables - 0x8C : nullptr,
                                 At<uint32_t>(At<char*>(tables, 0xFC) + i * 0x10, 0));
    }
    return 0;
}

// 0x00590380 (ret 4): an occupant going away. With a height map and +0x1AE
// clear, an occupant that lifts (0x0058E260) has the map's first layer
// cleared over its cell rectangle two cells wider on every side and three
// to the high side (the map's slot 0x2C); then every occupant of type
// 0x278128A0 in that rectangle two cells wider again (the occupant manager
// at 0x00B43D0C, slot 0xA4, into a list made by the node allocator at
// 0x00B207E4, slot 0xC) lifts it again (0x0058E720) and is let go (slot 8);
// the list ended (0x00413FE0). The allocator is made first when there is
// none (the framework's 0x3AE4BEA3/0x3AE4BEAB object, its holder let go,
// 0x00411870).
void RelieveUnderOccupant(char* manager, char* occupant)
{
    char* const map = At<char*>(manager, 0xD98);
    if (!map || At<uint8_t>(manager, 0x1AE) || !TlOccupantLifts(occupant))
        return;
    float low[3] = {1.0f, 1.0f, 1.0f}, high[3] = {0.0f, 0.0f, 0.0f};
    VrCallSlot<uint32_t>(occupant, 0x28, low, high);
    int32_t rect[4] = {};
    VrCallSlot<uint32_t>(occupant, 0x2C, rect);
    rect[0] -= 2;
    rect[1] -= 2;
    rect[2] += 3;
    rect[3] += 3;
    VrCallSlot<uint32_t>(map, 0x2C, 1u, rect);
    int32_t from[2] = {rect[0] - 2, rect[1] - 2}, to[2] = {rect[2] + 2, rect[3] + 2};
    if (!M<char*>(0x00B207E4)) {
        char* made = nullptr;
        if (char* const framework = reinterpret_cast<char*(__cdecl*)()>(addr::RZGetFramework)())
            VrCallSlot<uint32_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, &made);
        M<char*>(0x00B207E4) = made;
        TlReleaseRef(&made);
    }
    char* holder = VrCallSlot<char*>(M<char*>(0x00B207E4), 0xC, 0xCu);
    At<char*>(holder, 0) = holder;
    At<char*>(holder, 4) = holder;
    VrCallSlot<uint32_t>(M<char*>(0x00B43D0C), 0xA4, &holder, from, to, 0x278128A0u, 0xFFFFFFFFu);
    for (char* node = At<char*>(holder, 0); node != holder; node = At<char*>(node, 0)) {
        TlLiftUnderOccupant(manager, At<char*>(node, 8));
        VrCallSlot<uint32_t>(At<char*>(node, 8), 8);
    }
    TlDestroyList(&holder);
}

// Our city situation manager, part c3 (build 641): DoMessage and its
// handlers, the CSI list of the situation advices, and the message popup's
// window procedure with its answers. The class map is in
// src/core/automata_situations.h and the survey in docs/automata.md, "Part
// c". Calls between functions of the part, and to the other parts, go to the
// game's addresses, so each detour stands alone and OPENSC4_AUTOMATA_SKIP
// (function addresses, hex, eight digits) bisects them.
//
// Groups, off by default:
//   simulators.automata.situations.messages (15): kMessages below.
//   simulators.automata.situations.list (8): kList.
//   simulators.automata.situations.popup (5): kPopup.
//
// What it does.
//   - DoMessage (the +4 part's slot 3): the cheat csitrigger (0x230E27AC,
//     posts 0xAC1045EF with its hex argument), the end of a situation
//     (0x0BB324BD, slot 39 unless it succeeded or failed), the automata's
//     events (0x09D92FC3: kinds 2, 9, 11 and 12), the load (0x26D31EC1,
//     every advice of the advisor service put on the list when +0x9F1 is
//     set), the popup's reopening (0x2C16BDE7), an advice gone (0x6BB350E4),
//     the frame (0x86AD10EE), an advice added and removed (0x8A09FA64 and
//     0x8A09FA65), an occupant added and removed (0x99EF1142, 0x99EF1143),
//     a start (0xAC1045EF, slot 21), the driver (0xCC0D905F) and the popup
//     (0xEBB67EF7). Unknown messages answer false, the rest true.
//   - The frame (0x0052E300): the joyride's end (+0xA98: the services' slot
//     9, the countdown 4000, the U-Drive's camera), the countdown (the
//     services' slot 10 at its end), the drive's time (+0xA88; at its end
//     0x0052C190 or 0x00526720 by the status and the message 0xEBB67EF7);
//     the driver dropped when its slot 5 is false; while a situation runs the
//     warning 0x6C32871F to the automaton 30 seconds before the time limit,
//     the target update (0x0052C8F0, part c4) and the target check
//     (0x00525130, every 5000 ms the services' slot 21 for the situation's
//     groups).
//   - The success (0x0052C190) and the failure (0x00526720): the success
//     count, the tutorial noted in the preferences, the camera's effect at
//     the automaton, the money and the budget's reward or loss, the script
//     (+0xADC or +0xAE0), the advice told and its list entry removed.
//   - The list (+0xA04): an advice's entry added (0x0052A380, sorted by
//     priority by 0x00527920 and 0x00525AF0, STLport's list sort and merge)
//     with a candidate automaton's CSI (0x0052A1E0) or a tracked one's taken
//     over, removed (0x0052A650, its CSI given to another entry), an
//     automaton untracked (0x0052A830).
//   - The popup: 0x0052E910 loads the window (0x96A006B0 and the status's
//     instance) and fills it (0x0052CC50, the game's), its link from the
//     advice's properties; the window procedure (the +0x10 part's slot)
//     answers the buttons 1 (continue or dismiss), 6 (the drive), 7 (the
//     joyride) and the links, and closes it (0x00525330).
//
// Oddities kept: DoMessage's 0xAC1045EF hands slot 21 its bool in the low
// byte of the message pointer; 0x0052E910 and DoMessage's QueryInterface
// out pointers live in their argument slots (ours are locals); 0x0052A380
// passes the advice as the sort's comparator (unused); the merge takes an
// entry without an advice before one with; 0x0052A1E0's set is released by
// 0x00B43D44's slot 33, not the manager's services; the window procedure
// answers the link's bool inverted; 0x0052A380 at the most CSIs gives up
// the candidate it found and takes over a lower entry's CSI instead.
//
// Floats: the countdown, the drive's time and the target check's clock are
// compared unrounded (the x87's register, 53-bit precision control) and
// stored rounded, as the game.
//
// Still the game's, at their addresses: 0x0052CC50 (an SC4UIScale site),
// the targets (0x00524C40, 0x0052C0E0, 0x0052C8F0, 0x0052BBC0; part c4), the
// starts and the stop (part c2's), the list's clear and the hash map and set
// helpers, the window and resource managers, the message server.
#include "core/automata_situations.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::automata_situations {
namespace {

// ====================================================================================
// Helpers

template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

template <typename T>
inline T& At(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

template <typename T>
inline T Global(std::uintptr_t address)
{
    return *reinterpret_cast<const T*>(address);
}

// A virtual call through the object's first vtable, `byteOffset` in bytes.
template <typename R, typename... Args>
inline R Virtual(void* object, std::size_t byteOffset, Args... args)
{
    void** const vtable = *reinterpret_cast<void***>(object);
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(vtable[byteOffset / 4])(object, args...);
}

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("automata situations: first %s", name);
}

void* Allocate(std::uint32_t size) { return CallCdecl<void*>(addr::SC4_stl_allocate, size); }
void Free(void* pointer) { CallCdecl<void>(addr::SC4_Free, pointer); }

void AddRef(void* object) { Virtual<std::uint32_t>(object, slot::kAddRef); }
void Release(void* object) { Virtual<std::uint32_t>(object, slot::kRelease); }

// A referenced field: zeroed, then the old object released.
void Drop(void* base, std::size_t offset)
{
    if (void* const object = At<void*>(base, offset)) {
        At<void*>(base, offset) = nullptr;
        Release(object);
    }
}

// (end - begin) >> 2, an arithmetic shift as the asm's SAR, compared
// unsigned.
std::uint32_t Count(const void* begin, const void* end)
{
    const std::int32_t bytes =
        static_cast<std::int32_t>(static_cast<const char*>(end) - static_cast<const char*>(begin));
    return static_cast<std::uint32_t>(bytes >> 2);
}

void** Services(char* self) { return At<void**>(self, field::kServices); }
std::uint32_t ServiceCount(char* self) { return Count(At<void*>(self, field::kServices), At<void*>(self, field::kServices + 4)); }

// A hash map iterator, as SC4HashTable::Begin (0x004B0390) writes it.
struct Iterator {
    char* node;
    char* table;
};

Iterator Begin(char* table)
{
    Iterator it{};
    CallThis<void*>(addr::SC4HashTable_Begin, table, static_cast<void*>(&it));
    return it;
}

// How many nodes of the tracked automata's bucket chain hold the key (the
// lookup inlined in 0x0052C020 and 0x0052A1E0: the key modulo the bucket
// count, unsigned).
std::uint32_t TrackedHits(char* self, std::uintptr_t key)
{
    char** const buckets = At<char**>(self, field::kTrackedBuckets);
    const std::uint32_t count = Count(buckets, At<void*>(self, field::kTrackedBuckets + 4));
    std::uint32_t hits = 0;
    for (char* node = buckets[static_cast<std::uint32_t>(key) % count]; node; node = At<char*>(node, 0))
        if (At<std::uintptr_t>(node, 4) == key)
            ++hits;
    return hits;
}

// The game's SC4String on the stack (vtable 0x00A80810, the STL-style
// begin, end and capacity, a fifth word).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    std::uint32_t extra;
};

// The string's destruction, inlined where the functions here end: the vtable
// put back, the buffer freed.
void DestroyString(GameString& s)
{
    s.vtable = addr::SC4String_Vtable;
    if (s.begin)
        Free(s.begin);
}

// A transform of 0x38 bytes as the camera's effect and the U-Drive's take
// it: two flag bytes, a 3x3 rotation (identity), a translation and a scale
// (1).
struct Transform {
    std::uint8_t a;
    std::uint8_t b;
    std::uint8_t pad[2];
    float rotation[9];
    float translation[3];
    float scale;
};

Transform IdentityTransform(std::uint8_t a, std::uint8_t b, const std::uint32_t translation[3])
{
    Transform t{};
    t.a = a;
    t.b = b;
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::memcpy(t.rotation, identity, sizeof identity);
    std::memcpy(t.translation, translation, sizeof t.translation);
    t.scale = 1.0f;
    return t;
}

std::uint32_t ZeroBits() { return Global<std::uint32_t>(addr::cSC4CitySituationManager_ZeroFloat); }

constexpr std::uint32_t kAdviceIid = 0x6BEB1E8Au;
constexpr std::uint32_t kOccupantIid = 0xA9B40F05u;

// The messages DoMessage answers.
namespace msg {
inline constexpr std::uint32_t kCheat = 0x230E27ACu, kEnd = 0x0BB324BDu, kAutomaton = 0x09D92FC3u;
inline constexpr std::uint32_t kLoaded = 0x26D31EC1u, kReopen = 0x2C16BDE7u, kAdviceGone = 0x6BB350E4u;
inline constexpr std::uint32_t kFrame = 0x86AD10EEu, kAdviceAdded = 0x8A09FA64u, kAdviceRemoved = 0x8A09FA65u;
inline constexpr std::uint32_t kIgnored = 0x8A09FA67u, kOccupantAdded = 0x99EF1142u, kOccupantRemoved = 0x99EF1143u;
inline constexpr std::uint32_t kStart = 0xAC1045EFu, kDriver = 0xCC0D905Fu, kPopup = 0xEBB67EF7u;
inline constexpr std::uint32_t kCsiTrigger = 0x8C4136DBu;
} // namespace msg

// cIGZMessage2Standard's slots.
namespace m2 {
inline constexpr std::size_t kGetType = 0x10, kSetType = 0x14, kHasData1 = 0x20, kData1 = 0x24, kVoid1 = 0x28;
inline constexpr std::size_t kSetData1 = 0x2C, kSetVoid1 = 0x30, kData2 = 0x38, kVoid2 = 0x3C, kSetData2 = 0x40;
inline constexpr std::size_t kData3 = 0x4C, kSetData3 = 0x54, kUnknown = 0x80, kSetUnknown = 0x84;
} // namespace m2

void* MessageServer() { return Global<void*>(addr::SC4_MessageServer); }

// A cGZMessage of 0x2C bytes (RZ::operator_new, the constructor), or null.
void* NewMessage()
{
    void* const block = CallCdecl<void*>(addr::RZ_operator_new, static_cast<std::uint32_t>(0x2C));
    if (!block)
        return nullptr;
    return CallThis<void*>(addr::cGZMessage_cGZMessage, block);
}

// SC4RefPtr::Reset (0x004F20A0) on an out pointer: the old object released,
// the slot zeroed, the slot answered.
void** Reset(void** slot) { return CallThis<void**>(addr::SC4RefPtr_Reset, static_cast<void*>(slot)); }

void SetStatus(char* self, std::uint32_t status)
{
    CallThis<void>(addr::cSC4CitySituationManager_SetStatus, self, status);
}

void ClosePopupAt(char* self, void* window)
{
    CallThis<void>(addr::cSC4CitySituationManager_ClosePopup, self, window);
}

void PostAt(char* self, std::uint32_t type, void* object, void* data)
{
    CallThis<void>(addr::cSC4CitySituationManager_PostSituationMessage, self, type, object, data);
}

void RemoveEntryAt(char* self, void* advice)
{
    CallThis<void>(addr::cSC4CitySituationManager_RemoveEntry, self, advice);
}

void RunScriptAt(char* self, std::uint32_t table, std::uint32_t ref)
{
    CallThis<void>(addr::cSC4CitySituationManager_RunScript, self, table, ref);
}

void* AdviceService() { return Virtual<void*>(Global<void*>(addr::SC4_Service43D34), 0x1C, 0x1Du); }

// ====================================================================================
// DoMessage

// 0x0052EB80, DoMessage (the +4 part's slot 3, ret 4): see the header. The
// handlers get the manager (this - 4).
std::uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged;
    FirstCall("DoMessage", logged);
    char* const self = part - field::kVtable4;
    const std::uint32_t type = Virtual<std::uint32_t>(message, m2::kGetType);
    switch (type) {
    case msg::kCheat: {
        if (Virtual<std::uint32_t>(message, m2::kData1) != msg::kCsiTrigger)
            return 1;
        void* const text = Virtual<void*>(message, m2::kVoid2);
        const char* const chars = Virtual<const char*>(text, 0x14);
        alignas(4) char tokenizer[0x3C];
        alignas(4) char unused[0x14];
        CallThis<void*>(addr::SC4Tokenizer_SC4Tokenizer, tokenizer, chars);
        CallThis<void*>(addr::SC4String_SC4String, unused);
        if (CallThis<std::int32_t>(addr::SC4Tokenizer_Count, tokenizer) >= 2) {
            void* const word = Virtual<void*>(tokenizer, 0x40, 1u);
            const char* const digits = Virtual<const char*>(word, 0x14);
            const std::uint32_t id = CallCdecl<std::uint32_t>(addr::SC4_strtoul, digits, static_cast<void*>(nullptr), 16);
            if (id) {
                void* const start = NewMessage();
                AddRef(start);
                Virtual<void>(start, m2::kSetType, msg::kStart);
                Virtual<void>(start, m2::kSetData1, id);
                Virtual<void>(start, m2::kSetData2, 1u);
                Virtual<void>(start, m2::kSetData3, 0u);
                Virtual<std::uint8_t>(MessageServer(), 0x10, start, 0u);
                Release(start);
            }
        }
        CallThis<void>(addr::SC4String_Destroy, unused);
        CallThis<void>(addr::SC4Tokenizer_Destroy, tokenizer);
        return 1;
    }
    case msg::kEnd:
        if (!At<void*>(self, field::kSituation))
            return 1;
        if (CallThis<std::uint32_t>(addr::cSC4CitySituationManager_IsFinished, self) & 0xFF)
            return 1;
        Virtual<void>(self, slot::kStopSituation);
        return 1;
    case msg::kAutomaton: {
        void* const automaton = Virtual<void*>(message, m2::kVoid1);
        switch (Virtual<std::uint32_t>(message, m2::kData2)) {
        case 2:
            if (automaton == At<void*>(self, field::kAuto))
                CallThis<std::uint32_t>(addr::cSC4CitySituationManager_AbortDrive, self);
            break;
        case 9:
            CallThis<void>(addr::cSC4CitySituationManager_RecheckTarget, self, automaton);
            break;
        case 12:
            if (Virtual<std::uint32_t>(message, m2::kData3) != 0x14)
                break;
            [[fallthrough]];
        case 11:
            CallThis<void>(addr::cSC4CitySituationManager_AutomatonDestroyed, self, automaton);
            break;
        default:
            break;
        }
        return 1;
    }
    case msg::kLoaded: {
        if (!At<std::uint8_t>(self, field::kRead))
            return 1;
        if (!Global<void*>(addr::SC4_Service43D34))
            return 1;
        void* const service = AdviceService();
        if (!service)
            return 1;
        char* const header = *Virtual<char**>(service, 0x28);
        char* node = At<char*>(header, 8);
        while (node != header) {
            if (void* const advice = At<void*>(node, 0x10)) {
                void* face = nullptr;
                if (Virtual<std::uint8_t>(advice, slot::kQueryInterface, kAdviceIid, static_cast<void*>(Reset(&face))))
                    CallThis<void>(addr::cSC4CitySituationManager_AddEntry, self, face);
                if (face)
                    Release(face);
            }
            CallThis<void>(addr::SC4MapIterator_Next, static_cast<void*>(&node));
        }
        return 1;
    }
    case msg::kReopen:
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0xE4);
        if (void* const window = At<void*>(self, field::kRefA28))
            ClosePopupAt(self, window);
        CallThis<void>(addr::cSC4CitySituationManager_ShowPopup, self, static_cast<void*>(nullptr),
                       static_cast<void*>(nullptr));
        return 1;
    case msg::kAdviceGone:
    case msg::kAdviceRemoved: {
        void* const unknown = Virtual<void*>(message, m2::kUnknown);
        if (!unknown)
            return 1;
        void* face = nullptr;
        if (Virtual<std::uint8_t>(unknown, slot::kQueryInterface, kAdviceIid, static_cast<void*>(Reset(&face))))
            CallThis<void>(type == msg::kAdviceGone ? addr::cSC4CitySituationManager_AdviceGone
                                                    : addr::cSC4CitySituationManager_RemoveEntry,
                           self, face);
        if (face)
            Release(face);
        return 1;
    }
    case msg::kFrame:
        CallThis<std::uint8_t>(addr::cSC4CitySituationManager_Frame, self, Virtual<std::uint32_t>(message, m2::kData3));
        return 1;
    case msg::kAdviceAdded: {
        void* const unknown = Virtual<void*>(message, m2::kUnknown);
        if (!unknown)
            return 1;
        void* face = nullptr;
        if (Virtual<std::uint8_t>(unknown, slot::kQueryInterface, kAdviceIid, static_cast<void*>(&face)))
            CallThis<void>(addr::cSC4CitySituationManager_AddEntry, self, face);
        if (face)
            Release(face);
        return 1;
    }
    case msg::kIgnored:
        return 1;
    case msg::kOccupantAdded:
        CallThis<void>(addr::cSC4CitySituationManager_TrackOccupant, self, Virtual<void*>(message, m2::kVoid1));
        return 1;
    case msg::kOccupantRemoved:
        CallThis<void>(addr::cSC4CitySituationManager_OccupantRemoved, self, Virtual<void*>(message, m2::kVoid1));
        return 1;
    case msg::kStart: {
        const std::uint32_t id = Virtual<std::uint32_t>(message, m2::kData1);
        const std::uint8_t forOccupant = Virtual<std::uint32_t>(message, m2::kData2) != 0 ? 1 : 0;
        const std::uint32_t group = Virtual<std::uint32_t>(message, m2::kData3);
        // The bool goes in the low byte of the argument slot, which holds the
        // message.
        const std::uint32_t flag =
            (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(message)) & 0xFFFFFF00u) | forOccupant;
        Virtual<std::uint8_t>(self, slot::kInitiateSituationByGuid, id, flag, group);
        return 1;
    }
    case msg::kDriver: {
        void* const situation = At<void*>(self, field::kSituation);
        if (!situation || !Virtual<std::uint8_t>(situation, 0x3C, 8u))
            return 1;
        if (void* const driver = Virtual<void*>(message, m2::kUnknown))
            CallThis<void*>(addr::SC4RefPtr_Assign, static_cast<void*>(&At<void*>(self, field::kRefA9C)), driver);
        return 1;
    }
    case msg::kPopup: {
        void* const unknown = Virtual<void*>(message, m2::kUnknown);
        void* face = nullptr;
        if (unknown)
            Virtual<std::uint8_t>(unknown, slot::kQueryInterface, kAdviceIid, static_cast<void*>(Reset(&face)));
        void* data = nullptr;
        if (Virtual<std::uint8_t>(message, m2::kHasData1))
            data = Virtual<void*>(message, m2::kVoid1);
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0xE4);
        if (void* const window = At<void*>(self, field::kRefA28))
            ClosePopupAt(self, window);
        CallThis<void>(addr::cSC4CitySituationManager_ShowPopup, self, face, data);
        if (face)
            Release(face);
        return 1;
    }
    default:
        return 0;
    }
}

// 0x0052E300 (ret 4): the frame, dt in ms (the message's data 3). AL 1.
std::uint8_t __fastcall Frame(char* self, void*, float dt)
{
    static bool logged;
    FirstCall("Frame", logged);
    const float zero = Global<float>(addr::SC4_FloatZero);
    bool running = false; // BL: true skips the situation's update below
    if (At<std::uint8_t>(self, field::kJoyride)) {
        for (std::uint32_t i = 0; i < ServiceCount(self); ++i)
            Virtual<void>(Services(self)[i], 0x24);
        void* const stopper = At<void*>(self, field::kRefAA0);
        At<std::uint8_t>(self, field::kJoyride) = 0;
        At<std::uint32_t>(self, field::kCountdown) = 0x457A0000u; // 4000.0f
        if (stopper) {
            Virtual<void>(stopper, 0x10, 0u);
            Drop(self, field::kRefAA0);
        }
        void* const camera = Global<void*>(addr::SC4_Service43D1C);
        void* const udrive = At<void*>(Global<char*>(addr::SC4_UDriveMode), 0x30);
        if (camera && udrive) {
            Drop(self, field::kRefAA0);
            if (Virtual<std::uint8_t>(camera, 0x1C, Global<std::uint32_t>(addr::cSC4CitySituationManager_UDriveCameraName),
                                      static_cast<void*>(&At<void*>(self, field::kRefAA0)))) {
                const std::uint32_t zeros[3] = {0, 0, 0};
                Transform t = IdentityTransform(0, 0, zeros);
                std::uint32_t position[3] = {ZeroBits(), ZeroBits(), ZeroBits()};
                Virtual<void>(udrive, 0x1C, static_cast<void*>(position));
                std::memcpy(t.translation, position, sizeof position);
                t.b |= 1;
                t.a += 1;
                Virtual<void>(At<void*>(self, field::kRefAA0), 0x1C, static_cast<void*>(&t));
                Virtual<void>(At<void*>(self, field::kRefAA0), 0x0C, 0u);
            }
        }
        running = true;
    } else if (At<float>(self, field::kCountdown) > zero) {
        running = true;
        const double left = static_cast<double>(At<float>(self, field::kCountdown)) - static_cast<double>(dt);
        At<float>(self, field::kCountdown) = static_cast<float>(left);
        if (left <= static_cast<double>(zero)) {
            Drop(self, field::kRefAA0);
            running = false;
            for (std::uint32_t i = 0; i < ServiceCount(self); ++i)
                Virtual<void>(Services(self)[i], 0x28);
        }
    } else if (void* const situation = At<void*>(self, field::kSituation)) {
        if (At<float>(self, field::kDriveTime) > zero) {
            const double step =
                static_cast<double>(dt) * static_cast<double>(Global<float>(addr::FloraTuning_HeightEpsilon));
            const double left = static_cast<double>(At<float>(self, field::kDriveTime)) - step;
            At<float>(self, field::kDriveTime) = static_cast<float>(left);
            if (left <= static_cast<double>(zero)) {
                const std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
                if (status == 1)
                    CallThis<void>(addr::cSC4CitySituationManager_Succeed, self, situation);
                else if (status == 2)
                    CallThis<void>(addr::cSC4CitySituationManager_Fail, self, situation);
                PostAt(self, msg::kPopup, At<void*>(self, field::kSituation), nullptr);
            }
            running = true;
        }
    }
    if (void* const driver = At<void*>(self, field::kRefA9C))
        if (!Virtual<std::uint8_t>(driver, 0x14))
            Drop(self, field::kRefA9C);
    if (!At<void*>(self, field::kSituation) || running)
        return 1;
    const float limit = Virtual<float>(self, slot::kGetCurrentTimeLimitSeconds);
    if (!At<std::uint8_t>(self, field::kByteA99)) {
        const float warning = Global<float>(addr::cSC4CitySituationManager_WarningSeconds);
        if (limit >= warning &&
            static_cast<double>(limit) - static_cast<double>(warning) <=
                static_cast<double>(At<float>(self, field::kSituationTime))) {
            Virtual<void>(At<void*>(self, field::kAuto), 0xF4, 0x6C32871Fu);
            At<std::uint8_t>(self, field::kByteA99) = 1;
        }
    }
    const std::uint32_t status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_UpdateTarget, self, dt);
    SetStatus(self, status);
    CallThis<void>(addr::cSC4CitySituationManager_CheckTargets, self, dt);
    return 1;
}

// 0x0052E670 (ret 4): an occupant removed. With a situation: its target
// reached (0x0052C0E0) while driving, the drive aborted (0x00524CC0) when
// it is the automaton's occupant, the status set; then the occupant's
// automaton untracked (0x0052A830).
void __fastcall OccupantRemoved(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("OccupantRemoved", logged);
    if (!occupant)
        return;
    if (At<void*>(self, field::kSituation)) {
        std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
        if (occupant == At<void*>(self, field::kTarget) && status == 3)
            status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_TargetReached, self);
        if (void* const automaton = At<void*>(self, field::kAuto))
            if (Virtual<void*>(automaton, 0x14) == occupant && status == 3)
                status = CallThis<std::uint32_t>(addr::cSC4CitySituationManager_AbortDrive, self);
        SetStatus(self, status);
    }
    void* face = nullptr;
    if (Virtual<std::uint8_t>(occupant, slot::kQueryInterface, kOccupantIid, static_cast<void*>(&face)))
        CallThis<void>(addr::cSC4CitySituationManager_UntrackAutomaton, self, face);
    if (face)
        Release(face);
}

// 0x0052E710 (ret 4): the automata event 9. With a situation, when the
// automaton is the active one or its occupant (slot 5) the target, the
// target updated (0x0052C8F0 with 0) and the status set.
void __fastcall RecheckTarget(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("RecheckTarget", logged);
    if (!At<void*>(self, field::kSituation))
        return;
    if (At<void*>(self, field::kAuto) != automaton) {
        void* const target = At<void*>(self, field::kTarget);
        if (target != Virtual<void*>(automaton, 0x14))
            return;
    }
    SetStatus(self, CallThis<std::uint32_t>(addr::cSC4CitySituationManager_UpdateTarget, self, 0u));
}

// 0x0052C020 (ret 4): the automata events 11 and 12 (an automaton
// destroyed). Its flag 0x10000000 set (slot 30 with 1); with a situation the
// status 2 for the active automaton, for the target's the next target
// (0x0052BBC0) or 1 when the kind is 1 or 3, else 2; untracked (0x0052A830)
// when tracked.
void __fastcall AutomatonDestroyed(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("AutomatonDestroyed", logged);
    if (automaton)
        Virtual<void>(automaton, 0x78, 0x10000000u, 1u);
    if (At<void*>(self, field::kSituation)) {
        std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
        if (automaton == At<void*>(self, field::kAuto)) {
            status = 2;
        } else {
            void* const target = At<void*>(self, field::kTarget);
            if (Virtual<void*>(automaton, 0x14) == target) {
                const std::uint32_t kind = Virtual<std::uint32_t>(At<void*>(self, field::kSituation), 0x10);
                if (kind == 1 || kind == 3) {
                    if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_NextTarget, self))
                        status = 1;
                } else {
                    status = 2;
                }
            }
        }
        SetStatus(self, status);
    }
    if (TrackedHits(self, reinterpret_cast<std::uintptr_t>(automaton)))
        CallThis<void>(addr::cSC4CitySituationManager_UntrackAutomaton, self, automaton);
}

// 0x0052A7D0 (ret 4): an occupant added. Unless its slot 6's slot 3 answers
// true for 0x029244DB, its automaton interface tracked (0x005291C0).
void __fastcall TrackOccupant(char* self, void*, void* occupant)
{
    static bool logged;
    FirstCall("TrackOccupant", logged);
    if (!occupant)
        return;
    void* const properties = Virtual<void*>(occupant, 0x18);
    if (Virtual<std::uint8_t>(properties, 0x0C, 0x029244DBu))
        return;
    void* face = nullptr;
    if (Virtual<std::uint8_t>(occupant, slot::kQueryInterface, kOccupantIid, static_cast<void*>(&face)))
        CallThis<void>(addr::cSC4CitySituationManager_TrackAutomaton, self, face);
    if (face)
        Release(face);
}

// 0x00524DF0 (ret 4): an advice gone. Slot 35 with 0 (the U-Drive off), and
// slot 39 when it is the active situation.
void __fastcall AdviceGone(char* self, void*, void* advice)
{
    static bool logged;
    FirstCall("AdviceGone", logged);
    Virtual<void>(self, slot::kSetUDrive, 0u);
    if (advice == At<void*>(self, field::kSituation))
        Virtual<void>(self, slot::kStopSituation);
}

// 0x00524CC0: the status, 2 instead of 3 while a situation runs, set and
// answered.
std::uint32_t __fastcall AbortDrive(char* self, void*)
{
    static bool logged;
    FirstCall("AbortDrive", logged);
    std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    if (At<void*>(self, field::kSituation) && status == 3)
        status = 2;
    SetStatus(self, status);
    return status;
}

// 0x00524580: whether the status is 1 or 2 (succeeded or failed).
std::uint32_t __fastcall IsFinished(char* self, void*)
{
    static bool logged;
    FirstCall("IsFinished", logged);
    const std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
    return (status == 1 || status == 2) ? 1u : 0u;
}

// 0x00525130 (ret 4): the target check. While a situation with an automaton
// runs and its slot 19 is true, every 5000 ms (+0xA84) the services that
// handle one of the situation's groups (slot 18) get slot 21 (group, radius,
// the target's occupant with the flag 0x80, else the automaton, 1); the
// radius is the situation's slot 9, or half the city's width under 16.
void __fastcall CheckTargets(char* self, void*, float dt)
{
    static bool logged;
    FirstCall("CheckTargets", logged);
    void* const situation = At<void*>(self, field::kSituation);
    if (!situation || !At<void*>(self, field::kAuto))
        return;
    if (!Virtual<std::uint8_t>(situation, 0x4C))
        return;
    const double clock = static_cast<double>(dt) + static_cast<double>(At<float>(self, field::kCheckTime));
    At<float>(self, field::kCheckTime) = static_cast<float>(clock);
    if (!(clock >= static_cast<double>(Global<float>(addr::cSC4CitySituationManager_CheckInterval))))
        return;
    At<std::uint32_t>(self, field::kCheckTime) = 0;
    void* subject = nullptr;
    if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0x3C, 0x80u))
        CallThis<void*>(addr::SC4RefPtr_Assign, static_cast<void*>(&subject),
                        CallThis<void*>(addr::cSC4CitySituationManager_TargetOccupant, self));
    else
        CallThis<void*>(addr::SC4RefPtr_AssignFrom, static_cast<void*>(&subject),
                        static_cast<void*>(&At<void*>(self, field::kAuto)));
    if (!subject)
        return;
    std::uint32_t** const groups = Virtual<std::uint32_t**>(At<void*>(self, field::kSituation), 0x48);
    // Stored rounded, compared as the x87 register holds it.
    const double answered = Virtual<double>(At<void*>(self, field::kSituation), 0x24);
    float radius = static_cast<float>(answered);
    if (answered < static_cast<double>(Global<float>(addr::SC4_FloatSixteen))) {
        const std::int32_t w = Virtual<std::int32_t>(Global<void*>(addr::SC4_City), 0x174);
        const std::int32_t cells = static_cast<std::int32_t>(static_cast<std::uint32_t>(w) << 4);
        radius = static_cast<float>(static_cast<double>(cells) *
                                    static_cast<double>(Global<float>(addr::cSC4CitySituationManager_Half)));
    }
    std::uint32_t radiusBits;
    std::memcpy(&radiusBits, &radius, sizeof radiusBits);
    for (void** it = Services(self); it != At<void**>(self, field::kServices + 4); ++it) {
        void* const service = *it;
        if (service)
            AddRef(service);
        for (std::uint32_t* g = groups[0]; g != groups[1]; ++g)
            if (Virtual<std::uint8_t>(service, 0x78, *g))
                Virtual<void>(service, 0x54, *g, radiusBits, subject, 1u);
        if (service)
            Release(service);
    }
    Release(subject);
}

// 0x00525100: the target's occupant interface (0xA9B40F05), released before
// it is answered; null without a target.
void* __fastcall TargetOccupant(char* self, void*)
{
    static bool logged;
    FirstCall("TargetOccupant", logged);
    void* face = nullptr;
    void* const target = At<void*>(self, field::kTarget);
    if (!target)
        return nullptr;
    if (Virtual<std::uint8_t>(target, slot::kQueryInterface, kOccupantIid, static_cast<void*>(&face)))
        Release(face);
    return face;
}

// 0x00524D10 (ret 0xC, this unused): a message of the type, with the object
// (SetIGZUnknown) and the data (SetVoid1) when set, posted.
void __fastcall PostSituationMessage(void*, void*, std::uint32_t type, void* object, void* data)
{
    static bool logged;
    FirstCall("PostSituationMessage", logged);
    void* const message = NewMessage();
    AddRef(message);
    Virtual<void>(message, m2::kSetType, type);
    if (object)
        Virtual<void>(message, m2::kSetUnknown, object);
    if (data)
        Virtual<void>(message, m2::kSetVoid1, data);
    Virtual<std::uint8_t>(MessageServer(), 0x10, message, 0u);
    Release(message);
}

// 0x0052C190 (ret 4): the situation succeeded. Its GUID's success count up
// (at most 0xFFFF; a new one 1); with the flag 0x40 the GUID noted among the
// preferences' 64 done tutorials (+0x1334) and the preferences saved; the
// camera's effect (0x00B43D1C's slot 7 with the situation's slot 47 name)
// at the automaton; the money (slot 46, 0x00B43D60's slot 7) and the
// budget's reward (slot 48, 0x00B43D64's slot 7); the script (+0xADC); the
// advice service's slot 20 with the advice; the list entry removed.
void __fastcall Succeed(char* self, void*, void* situation)
{
    static bool logged;
    FirstCall("Succeed", logged);
    if (!situation || situation != At<void*>(self, field::kSituation))
        return;
    const std::uint32_t guid = Virtual<std::uint32_t>(Virtual<void*>(situation, 0x0C), 0x28);
    char* const successes = self + field::kSuccesses;
    Iterator found{};
    CallThis<void*>(addr::SC4Uint32HashTable_Find, successes, static_cast<void*>(&found), &guid);
    if (!found.node) {
        CallThis<void>(addr::SC4Uint32HashSet_Resize, successes, At<std::uint32_t>(successes, map::kCount) + 1);
        struct {
            std::uint32_t key;
            std::uint16_t count;
            std::uint16_t pad;
        } pair = {guid, 1, 0};
        alignas(4) char result[0x10] = {};
        CallThis<void*>(addr::cSC4CitySituationManager_SuccessInsertUnique, successes, static_cast<void*>(result),
                        static_cast<void*>(&pair));
    } else {
        std::uint16_t& count = At<std::uint16_t>(found.node, 8);
        if (count < 0xFFFF)
            ++count;
    }
    if (Virtual<std::uint8_t>(situation, 0x3C, 0x40u))
        if (void* const app = Global<void*>(addr::SC4_Service43C94))
            if (char* const prefs = Virtual<char*>(app, 0x98))
                for (std::uint32_t i = 0; i < 0x40; ++i) {
                    std::uint32_t& done = At<std::uint32_t>(prefs, 0x1334 + 4 * i);
                    if (!done) {
                        done = guid;
                        Virtual<void>(app, 0x44);
                        break;
                    }
                    if (done == guid)
                        break;
                }
    std::uint32_t position[3] = {ZeroBits(), ZeroBits(), ZeroBits()};
    if (void* const automaton = At<void*>(self, field::kAuto))
        Virtual<void>(automaton, 0x1C, static_cast<void*>(position));
    GameString name{};
    name.begin = static_cast<char*>(Allocate(8));
    name.end = name.begin;
    name.capacity = name.begin + 8;
    name.begin[0] = 0;
    name.vtable = addr::SC4String_Vtable;
    name.extra = 0;
    if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0xBC, static_cast<void*>(&name)))
        if (void* const camera = Global<void*>(addr::SC4_Service43D1C)) {
            void* effect = nullptr;
            const char* const text = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&name));
            if (Virtual<std::uint8_t>(camera, 0x1C, text, static_cast<void*>(&effect))) {
                Transform t = IdentityTransform(1, 1, position);
                Virtual<void>(effect, 0x20, static_cast<void*>(&t));
                Virtual<void>(effect, 0x0C, 0u);
            }
            if (effect)
                Release(effect);
        }
    std::uint32_t money[2] = {};
    if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0xB8, static_cast<void*>(&money[1]),
                              static_cast<void*>(&money[0])))
        if (void* const service = Global<void*>(addr::SC4_Service43D60))
            Virtual<void>(service, 0x1C, position[0], position[2], money[1], money[0]);
    std::uint32_t reward[2] = {};
    if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0xC0, static_cast<void*>(reward)))
        if (void* const budget = Global<void*>(addr::SC4_BudgetSimulator))
            Virtual<void>(budget, 0x1C, reward[0], reward[1]);
    const std::uint32_t ref = At<std::uint32_t>(self, field::kSuccessRef);
    RunScriptAt(self, Virtual<std::uint32_t>(At<void*>(self, field::kSituation), 0x78), ref);
    if (Global<void*>(addr::SC4_Service43D34))
        if (void* const service = AdviceService())
            Virtual<void>(service, 0x50, Virtual<void*>(situation, 0x0C));
    RemoveEntryAt(self, situation);
    DestroyString(name);
}

// 0x00526720 (ret 4): the situation failed. With an automaton (its
// position) or a target (its position, slot 8): the camera's effect (slot 50
// name), the money (slot 49) and the budget's loss (slot 51); then the
// script (+0xAE0) in any case.
void __fastcall Fail(char* self, void*, void* situation)
{
    static bool logged;
    FirstCall("Fail", logged);
    if (!situation || situation != At<void*>(self, field::kSituation))
        return;
    std::uint32_t position[3] = {ZeroBits(), ZeroBits(), ZeroBits()};
    bool placed = true;
    if (void* const automaton = At<void*>(self, field::kAuto))
        Virtual<void>(automaton, 0x1C, static_cast<void*>(position));
    else if (void* const target = At<void*>(self, field::kTarget))
        Virtual<void>(target, 0x20, static_cast<void*>(position));
    else
        placed = false;
    if (placed) {
        GameString name{};
        CallThis<void*>(addr::SC4String_SC4String, static_cast<void*>(&name));
        if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0xC8, static_cast<void*>(&name)))
            if (void* const camera = Global<void*>(addr::SC4_Service43D1C)) {
                void* effect = nullptr;
                void** const out = Reset(&effect);
                const char* const text = CallThis<const char*>(addr::cRZBaseString_Data, static_cast<void*>(&name));
                if (Virtual<std::uint8_t>(camera, 0x1C, text, static_cast<void*>(out))) {
                    Transform t = IdentityTransform(1, 1, position);
                    Virtual<void>(effect, 0x20, static_cast<void*>(&t));
                    Virtual<void>(effect, 0x0C, 0u);
                }
                if (effect)
                    Release(effect);
            }
        std::uint32_t money[2] = {};
        if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0xC4, static_cast<void*>(&money[1]),
                                  static_cast<void*>(&money[0])))
            if (void* const service = Global<void*>(addr::SC4_Service43D60))
                Virtual<void>(service, 0x1C, position[0], position[2], money[1], money[0]);
        std::uint32_t loss[2] = {};
        if (Virtual<std::uint8_t>(At<void*>(self, field::kSituation), 0xCC, static_cast<void*>(loss)))
            if (void* const budget = Global<void*>(addr::SC4_BudgetSimulator))
                Virtual<void>(budget, 0x1C, loss[0], loss[1]);
        DestroyString(name);
    }
    const std::uint32_t ref = At<std::uint32_t>(self, field::kFailureRef);
    RunScriptAt(self, Virtual<std::uint32_t>(At<void*>(self, field::kSituation), 0x78), ref);
}

// 0x00525430 (ret 8): the situation's script. Unless the reference is -2 or
// there is no script context: with the situation's flag 0x20 +0xAF8's slot
// 68; the reference pushed (slot 43) and, when it is a table (slot 71), the
// table pushed and the function called (slot 51 with 1, 0); the stack put
// back.
void __fastcall RunScript(char* self, void*, std::uint32_t table, std::uint32_t ref)
{
    static bool logged;
    FirstCall("RunScript", logged);
    if (ref == 0xFFFFFFFEu || !At<void*>(self, field::kScriptContext))
        return;
    if (void* const situation = At<void*>(self, field::kSituation))
        if (Virtual<std::uint8_t>(situation, 0x3C, 0x20u))
            if (void* const holder = At<void*>(self, field::kRefAF8))
                Virtual<void>(holder, 0x110);
    const std::uint32_t top = Virtual<std::uint32_t>(At<void*>(self, field::kScriptContext), 0x18);
    Virtual<void>(At<void*>(self, field::kScriptContext), 0xAC, ref);
    if (Virtual<std::uint8_t>(At<void*>(self, field::kScriptContext), 0x11C, 0xFFFFFFFFu)) {
        Virtual<void>(At<void*>(self, field::kScriptContext), 0xAC, table);
        Virtual<void>(At<void*>(self, field::kScriptContext), 0xCC, 1u, 0u);
    }
    Virtual<void>(At<void*>(self, field::kScriptContext), 0x1C, top);
}

// ====================================================================================
// The CSI list (+0xA04; nodes +0 next, +4 previous, +8 the advice, +0xC the
// automaton shown, +0x10 the overlay ID)

using Node = char;

Node* Head(char* self) { return At<Node*>(self, field::kList); }

// 0x00527300 (this a list): its head node (0x14 bytes) made, pointing at
// itself.
void* __fastcall ListConstruct(void** list, void*)
{
    static bool logged;
    FirstCall("ListConstruct", logged);
    *list = nullptr;
    Node* const head = static_cast<Node*>(Allocate(0x14));
    At<Node*>(head, 0) = head;
    At<Node*>(head, 4) = head;
    *list = head;
    return head;
}

// 0x00526440 (ret 0xC, this unused): a node of the value (two references and
// a word) before the position, answered through out.
Node** __fastcall ListInsert(void*, void*, Node** out, Node* position, void** value)
{
    static bool logged;
    FirstCall("ListInsert", logged);
    Node* const node = static_cast<Node*>(Allocate(0x14));
    if (void** const slot = reinterpret_cast<void**>(node + 8)) {
        slot[0] = value[0];
        if (slot[0])
            AddRef(slot[0]);
        slot[1] = value[1];
        if (slot[1])
            AddRef(slot[1]);
        slot[2] = value[2];
    }
    At<Node*>(node, 0) = position;
    At<Node*>(node, 4) = At<Node*>(position, 4);
    At<Node*>(At<Node*>(position, 4), 0) = node;
    At<Node*>(position, 4) = node;
    *out = node;
    return out;
}

// SGI's transfer: [first, last) moved before position.
void Transfer(Node* position, Node* first, Node* last)
{
    At<Node*>(At<Node*>(last, 4), 0) = position;
    At<Node*>(At<Node*>(first, 4), 0) = last;
    At<Node*>(At<Node*>(position, 4), 0) = first;
    Node* const tmp = At<Node*>(position, 4);
    At<Node*>(position, 4) = At<Node*>(last, 4);
    At<Node*>(last, 4) = At<Node*>(first, 4);
    At<Node*>(first, 4) = tmp;
}

// 0x00525AF0 (cdecl, the comparator unused): STLport's list merge of other
// into list, by the advices' priority (their slot 3's slot 7, the lower
// first; an entry without an advice goes before one with).
void __cdecl MergeList(Node** list, Node** other, std::uint32_t)
{
    static bool logged;
    FirstCall("MergeList", logged);
    Node* const last2 = *other;
    Node* const last1 = *list;
    Node* first1 = At<Node*>(last1, 0);
    Node* first2 = At<Node*>(last2, 0);
    while (first1 != last1) {
        if (first2 == last2)
            return;
        void* const a = At<void*>(first2, 8);
        void* const b = At<void*>(first1, 8);
        bool less;
        if (!a) {
            less = b != nullptr;
        } else if (!b) {
            less = false;
        } else {
            void* const inner2 = Virtual<void*>(a, 0x0C);
            void* const inner1 = Virtual<void*>(b, 0x0C);
            const std::int32_t p1 = Virtual<std::int32_t>(inner1, 0x1C);
            const std::int32_t p2 = Virtual<std::int32_t>(inner2, 0x1C);
            less = p2 < p1;
        }
        if (less) {
            Node* const next = At<Node*>(first2, 0);
            if (first1 != next)
                Transfer(first1, first2, next);
            first2 = next;
        } else {
            first1 = At<Node*>(first1, 0);
        }
    }
    if (first2 != last2 && last1 != last2)
        Transfer(last1, first2, last2);
}

// A list's destruction, as 0x00527920 inlines it: the nodes cleared
// (0x00525EC0) and the head freed.
void DestroyList(Node** list)
{
    CallThis<void>(addr::cSC4CitySituationManager_ClearList, static_cast<void*>(list));
    if (*list)
        Free(*list);
}

// 0x00527920 (cdecl): STLport's list sort (a carry and 64 counters made by
// 0x00527300 through the vector constructor 0x0041D490, merged by
// 0x00525AF0), for lists of two or more.
void __cdecl SortList(Node** list, std::uint32_t comparator)
{
    static bool logged;
    FirstCall("SortList", logged);
    Node* const head = *list;
    if (At<Node*>(head, 0) == head || At<Node*>(At<Node*>(head, 0), 0) == head)
        return;
    Node* carry = static_cast<Node*>(Allocate(0x14));
    At<Node*>(carry, 0) = carry;
    At<Node*>(carry, 4) = carry;
    Node* counter[64];
    reinterpret_cast<void(__stdcall*)(void*, std::uint32_t, std::uint32_t, std::uintptr_t)>(addr::SC4UI_F41D490)(
        counter, 4u, 64u, addr::cSC4CitySituationManager_ListConstruct);
    int fill = 0;
    while (At<Node*>(*list, 0) != *list) {
        Node* const first = At<Node*>(*list, 0);
        Node* const position = At<Node*>(carry, 0);
        Node* const last = At<Node*>(first, 0);
        if (position != first && position != last)
            Transfer(position, first, last);
        int i = 0;
        while (i < fill && At<Node*>(counter[i], 0) != counter[i]) {
            CallCdecl<void>(addr::cSC4CitySituationManager_MergeList, &counter[i], &carry, comparator);
            Node* const swap = counter[i];
            counter[i] = carry;
            carry = swap;
            ++i;
        }
        Node* const swap = counter[i];
        counter[i] = carry;
        carry = swap;
        if (i == fill)
            ++fill;
    }
    for (int i = 1; i < fill; ++i)
        CallCdecl<void>(addr::cSC4CitySituationManager_MergeList, &counter[i], &counter[i - 1], comparator);
    Node* const swap = *list;
    *list = counter[fill - 1];
    counter[fill - 1] = swap;
    for (int i = 63; i >= 0; --i)
        DestroyList(&counter[i]);
    DestroyList(&carry);
}

// 0x0052A1E0 (ret 8): the first candidate of the advice's automata groups
// (slot 12, gathered by 0x00525640) with the flag 2, none of 1, 0x8000000
// and 0x10000000, the type neither 0x34 nor 0x14, and not tracked, into out
// (referenced); the set released by 0x00B43D44's slot 33.
void** __fastcall FindCandidate(char* self, void*, void** out, void* advice)
{
    static bool logged;
    FirstCall("FindCandidate", logged);
    void* found = nullptr;
    alignas(4) char set[0x14] = {};
    CallThis<void>(addr::SC4Uint32HashSet_Construct, set, static_cast<std::uint32_t>(100));
    void* const groups = Virtual<void*>(advice, 0x30);
    CallThis<std::uint8_t>(addr::cSC4CitySituationManager_GatherCandidates, self, groups, static_cast<void*>(set));
    const Iterator it = Begin(set);
    for (char* node = it.node; node;) {
        void* const candidate = At<void*>(node, 4);
        if (candidate && Virtual<std::uint8_t>(candidate, 0x7C, 2u) && !Virtual<std::uint8_t>(candidate, 0x7C, 1u) &&
            !Virtual<std::uint8_t>(candidate, 0x7C, 0x8000000u) && !Virtual<std::uint8_t>(candidate, 0x7C, 0x10000000u)) {
            const std::uint32_t kind = Virtual<std::uint32_t>(candidate, 0xEC);
            if (kind != 0x34 && kind != 0x14 && !TrackedHits(self, reinterpret_cast<std::uintptr_t>(candidate))) {
                AddRef(candidate);
                found = candidate;
                break;
            }
        }
        if (char* const next = At<char*>(node, 0)) {
            node = next;
            continue;
        }
        char** const buckets = At<char**>(it.table, map::kBegin);
        std::uint32_t bucket = At<std::uint32_t>(node, 4) % Count(buckets, At<void*>(it.table, map::kEnd));
        node = nullptr;
        for (++bucket; bucket < Count(At<void*>(it.table, map::kBegin), At<void*>(it.table, map::kEnd)); ++bucket)
            if (char* const first = At<char**>(it.table, map::kBegin)[bucket]) {
                node = first;
                break;
            }
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43D44), 0x84, static_cast<void*>(set));
    *out = found;
    if (found)
        AddRef(found);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
    if (void* const buckets = At<void*>(set, map::kBegin))
        Free(buckets);
    if (found)
        Release(found);
    return out;
}

// The list's node holding the advice, or the head (the find inlined in
// 0x0052A380 and 0x0052A650, through a holder of 0x00524A00).
Node* FindEntry(char* self, void* advice)
{
    Node* const head = Head(self);
    Node* node = At<Node*>(head, 0);
    void* holder[3] = {};
    void** const held = CallThis<void**>(addr::cSC4CitySituationManager_HolderInit, static_cast<void*>(holder), advice);
    void* const key = held[0];
    while (node != head && At<void*>(node, 8) != key)
        node = At<Node*>(node, 0);
    if (holder[1])
        Release(holder[1]);
    if (holder[0])
        Release(holder[0]);
    return node;
}

// 0x0052A380 (ret 4): the advice's list entry, added in priority order when
// missing; when it shows no automaton, a candidate's CSI (0x0052A1E0, under
// the most CSIs; else the CSI of a later entry of lower priority that has
// one is taken over), or the CSI of a later entry showing a tracked
// automaton the advice answers true for (slot 53) and of lower priority.
void __fastcall AddEntry(char* self, void*, void* advice)
{
    static bool logged;
    FirstCall("AddEntry", logged);
    if (!advice)
        return;
    Node** const list = &At<Node*>(self, field::kList);
    Node* node = FindEntry(self, advice);
    if (node == *list) {
        AddRef(advice);
        void* value[3] = {advice, nullptr, nullptr};
        Node* made = nullptr;
        CallThis<Node**>(addr::cSC4CitySituationManager_ListInsert, static_cast<void*>(list), &made,
                         At<Node*>(*list, 0), static_cast<void*>(value));
        CallCdecl<void>(addr::cSC4CitySituationManager_SortList, list,
                        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(advice)));
        Node* const head = *list;
        node = At<Node*>(head, 0);
        while (node != head && At<void*>(node, 8) != advice)
            node = At<Node*>(node, 0);
        Release(advice);
    }
    if (node == *list)
        return;
    void** const entry = reinterpret_cast<void**>(node + 8);
    if (At<void*>(node, 0xC))
        return;
    void* candidate = nullptr;
    CallThis<void**>(addr::cSC4CitySituationManager_FindCandidate, self, &candidate, advice);
    if (!candidate) {
        const Iterator it = Begin(self + field::kTracked);
        bool done = false;
        for (char* t = it.node; t && !done;) {
            void* const automaton = At<void*>(t, 4);
            if (Virtual<std::uint8_t>(advice, 0xD4, automaton)) {
                Node* const head = *list;
                for (Node* q = At<Node*>(node, 0); q != head; q = At<Node*>(q, 0)) {
                    if (At<void*>(q, 0xC) != automaton)
                        continue;
                    if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_ComparePriority,
                                                static_cast<void*>(q + 8), static_cast<void*>(entry)))
                        continue;
                    CallThis<void*>(addr::SC4RefPtr_Assign, static_cast<void*>(node + 0xC), automaton);
                    At<std::uint32_t>(node, 0x10) = At<std::uint32_t>(q, 0x10);
                    Drop(q, 0xC);
                    At<std::uint32_t>(q, 0x10) = 0;
                    done = true;
                    break;
                }
            }
            char* next = At<char*>(t, 0);
            if (!next) {
                Iterator step = {t, it.table};
                next = CallThis<char*>(addr::SC4HashTable_NextBucket, static_cast<void*>(&step));
            }
            t = next;
        }
        return;
    }
    const std::uint32_t tracked = At<std::uint32_t>(self, field::kTrackedCount);
    const std::uint32_t most = Virtual<std::uint8_t>(self, slot::kGetMaxCsi);
    if (tracked < most) {
        CallThis<void>(addr::cSC4CitySituationManager_ShowAutomatonCsi, self, candidate,
                       static_cast<void*>(node + 0x10), 1u);
        CallThis<void*>(addr::SC4RefPtr_AssignFrom, static_cast<void*>(node + 0xC), static_cast<void*>(&candidate));
        Release(candidate);
        return;
    }
    Node* const head = *list;
    for (Node* q = At<Node*>(node, 0); q != head; q = At<Node*>(q, 0)) {
        if (!At<void*>(q, 0xC))
            continue;
        if (!CallThis<std::uint8_t>(addr::cSC4CitySituationManager_ComparePriority, static_cast<void*>(q + 8),
                                    static_cast<void*>(entry)))
            continue;
        CallThis<void*>(addr::SC4RefPtr_AssignFrom, static_cast<void*>(node + 0xC), static_cast<void*>(q + 0xC));
        At<std::uint32_t>(node, 0x10) = At<std::uint32_t>(q, 0x10);
        Drop(q, 0xC);
        At<std::uint32_t>(q, 0x10) = 0;
        break;
    }
    Release(candidate);
}

// 0x0052A650 (ret 4): the advice's list entry removed (its CSI taken away,
// 0x00528ED0 with 0), then, under the most CSIs, every other entry without
// an automaton given a candidate's (0x0052A1E0, 0x00528ED0 with 1).
void __fastcall RemoveEntry(char* self, void*, void* advice)
{
    static bool logged;
    FirstCall("RemoveEntry", logged);
    if (!advice)
        return;
    Node* const node = FindEntry(self, advice);
    if (node != Head(self)) {
        if (void* const automaton = At<void*>(node, 0xC)) {
            CallThis<void>(addr::cSC4CitySituationManager_ShowAutomatonCsi, self, automaton,
                           static_cast<void*>(node + 0x10), 0u);
            Drop(node, 0xC);
        }
        Drop(node, 8);
        At<Node*>(At<Node*>(node, 4), 0) = At<Node*>(node, 0);
        At<Node*>(At<Node*>(node, 0), 4) = At<Node*>(node, 4);
        if (void* const automaton = At<void*>(node, 0xC))
            Release(automaton);
        if (void* const entry = At<void*>(node, 8))
            Release(entry);
        Free(node);
    }
    std::uint32_t count = At<std::uint32_t>(self, field::kTrackedCount);
    const std::uint32_t most = Virtual<std::uint8_t>(self, slot::kGetMaxCsi);
    Node* const head = Head(self);
    for (Node* n = At<Node*>(head, 0); n != head && count < most; n = At<Node*>(n, 0)) {
        void* const entry = At<void*>(n, 8);
        if (entry == advice || At<void*>(n, 0xC))
            continue;
        void* candidate = nullptr;
        CallThis<void**>(addr::cSC4CitySituationManager_FindCandidate, self, &candidate, entry);
        if (!candidate)
            continue;
        CallThis<void>(addr::cSC4CitySituationManager_ShowAutomatonCsi, self, candidate, static_cast<void*>(n + 0x10),
                       1u);
        CallThis<void*>(addr::SC4RefPtr_AssignFrom, static_cast<void*>(n + 0xC), static_cast<void*>(&candidate));
        ++count;
        Release(candidate);
    }
}

// 0x0052A830 (ret 4): a tracked automaton's CSI taken away from the first
// list entry showing it (0x00528ED0 with 0), and that entry's advice added
// again (0x0052A380) for another automaton.
void __fastcall UntrackAutomaton(char* self, void*, void* automaton)
{
    static bool logged;
    FirstCall("UntrackAutomaton", logged);
    if (!automaton)
        return;
    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(automaton);
    if (!CallThis<std::uint32_t>(addr::SC4Uint32HashSet_Count, self + field::kTracked, static_cast<void*>(&key)))
        return;
    Node* const head = Head(self);
    Node* node = At<Node*>(head, 0);
    while (node != head && At<void*>(node, 0xC) != automaton)
        node = At<Node*>(node, 0);
    if (node == head)
        return;
    CallThis<void>(addr::cSC4CitySituationManager_ShowAutomatonCsi, self, automaton, static_cast<void*>(node + 0x10),
                   0u);
    Drop(node, 0xC);
    if (void* const entry = At<void*>(node, 8))
        CallThis<void>(addr::cSC4CitySituationManager_AddEntry, self, entry);
}

// ====================================================================================
// The popup

// 0x0052E910 (ret 8): the popup for the advice (null: the status 5) with
// the data. The overlay manager's layer 4 off; the window 0x96A006B0 (the
// instance by the status: 0x0BBC06B6 for 1 and 5, 0x4BBC080F for 2, else
// 0x2BB16D50) loaded; under a modal guard its procedure set to the +0x10
// part, filled by 0x0052CC50 and kept (+0xA28); the data's link (property
// 0x2BE8FBF3, else by its type) opened from the resource 0x6534284A,
// 0x690F693F (property 0xEC1B2361, 0x00B43DC4's slot 56); the window shown
// and made modal.
void __fastcall ShowPopup(char* self, void*, void* advice, void* data)
{
    static bool logged;
    FirstCall("ShowPopup", logged);
    if (void* const overlay = At<void*>(self, field::kOverlay))
        CallThis<void>(addr::SC4OverlayManager_F46D3A0, overlay, 4u, 0u);
    if (!advice)
        At<std::uint32_t>(self, field::kStatus) = 5;
    void* window = nullptr;
    std::uint32_t key[3] = {0, 0x96A006B0u, 0};
    std::uint32_t layout;
    switch (At<std::uint32_t>(self, field::kStatus)) {
    case 1:
    case 5:
        key[2] = 0x0BBC06B6u;
        layout = 0xEBBC081Eu;
        break;
    case 2:
        key[2] = 0x4BBC080Fu;
        layout = 0xEBBC081Eu;
        break;
    default:
        key[2] = 0x2BB16D50u;
        layout = 0xEBB16D71u;
        break;
    }
    if (CallCdecl<std::uint8_t>(addr::SC4UI_LoadWindow, static_cast<void*>(key), 0u, layout, 0x5386D516u,
                                static_cast<void*>(&window))) {
        Virtual<void>(CallCdecl<void*>(addr::SC4UDrive_GetService602640), 0x18, 0u);
        alignas(4) char guard[0x20] = {};
        CallThis<void*>(addr::SC4ModalGuard_Construct, guard, 0u, 1u, 1u, 1u);
        Virtual<void>(window, 0x1C, static_cast<void*>(self + field::kVtable10));
        CallThis<void>(addr::cSC4CitySituationManager_FillPopup, self, Virtual<void*>(window, 0x0C), advice, data);
        void* const shown = Virtual<void*>(window, 0x0C);
        void* const old = At<void*>(self, field::kRefA28);
        if (shown != old) {
            if (shown)
                AddRef(shown);
            At<void*>(self, field::kRefA28) = shown;
            if (old)
                Release(old);
        }
        if (data) {
            std::uint32_t link = 0;
            void* const properties = Virtual<void*>(Virtual<void*>(data, 0x14), 0x18);
            if (!CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint32, properties, 0x2BE8FBF3u, static_cast<void*>(&link))) {
                const std::uint32_t kind = Virtual<std::uint32_t>(Virtual<void*>(data, 0x14), 0x1C);
                link = kind == 0x480E6151u ? 0x6BE8846Cu : 0x0BE8824Fu;
            }
            std::uint32_t resource[3] = {0x6534284Au, 0x690F693Fu, link};
            void* holder = nullptr;
            if (Virtual<std::uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0x0C, static_cast<void*>(resource),
                                      0xA52160F5u, static_cast<void*>(&holder), 0u, 0u)) {
                std::uint32_t target = 0;
                if (CallCdecl<std::uint8_t>(addr::SC4_GetPropertyUint32, Virtual<void*>(holder, 0x1C), 0xEC1B2361u,
                                            static_cast<void*>(&target)))
                    Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0xE0, target);
            }
            if (holder)
                Release(holder);
        }
        void* const ui = Global<void*>(addr::SC4_Service43CE0);
        Virtual<void>(ui, 0x28, Virtual<void*>(window, 0x0C), 1u);
        void* const manager = CallCdecl<void*>(addr::SC4_GetWinManager);
        Virtual<void>(manager, 0xA4, Virtual<void*>(window, 0x0C));
        Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, Virtual<void*>(window, 0x0C));
        Virtual<void>(CallCdecl<void*>(addr::SC4_GetWinManager), 0x94, Global<void*>(addr::SC4_Service43DC8));
        CallThis<void>(addr::SC4ModalGuard_Destruct, guard);
    }
    if (window)
        Release(window);
}

// 0x00525330 (ret 4): the popup closed. +0xA1C stopped (slot 6 with 0) and
// dropped; the window's children 0xEA9F13C0 and 0x8BB696C1 (slot 113) have
// their slot 91's object released and are taken off (slot 114 with 0);
// +0xA28 dropped when it is the window; the window manager's slot 9 (0xF,
// the window, 0) and slot 23.
void __fastcall ClosePopup(char* self, void*, void* window)
{
    static bool logged;
    FirstCall("ClosePopup", logged);
    if (void* const stopper = At<void*>(self, field::kRefA1C)) {
        Virtual<void>(stopper, 0x18, 0u);
        Drop(self, field::kRefA1C);
    }
    for (const std::uint32_t id : {0xEA9F13C0u, 0x8BB696C1u}) {
        void* child = nullptr;
        if (Virtual<std::uint8_t>(window, 0x1C4, id, static_cast<void*>(&child))) {
            if (void* const object = Virtual<void*>(child, 0x16C))
                Release(object);
            Virtual<void>(window, 0x1C8, id, 0u);
        }
    }
    if (window == At<void*>(self, field::kRefA28))
        Drop(self, field::kRefA28);
    Virtual<void>(CallCdecl<void*>(addr::SC4_GetWinManager), 0x24, 0xFu, window, 0u);
    Virtual<void>(CallCdecl<void*>(addr::SC4_GetWinManager), 0x5C, window);
}

// 0x0052BF00: the popup dismissed. With +0xA64 the target's occupant gets
// slot 13 with 0; with a situation the advice service's slot 20 with the
// advice and its list entry removed; slot 39.
void __fastcall Dismiss(char* self, void*)
{
    static bool logged;
    FirstCall("Dismiss", logged);
    if (At<std::uint8_t>(self, field::kByteA64)) {
        void* occupant = nullptr;
        if (void* const target = At<void*>(self, field::kTarget))
            if (Virtual<std::uint8_t>(target, slot::kQueryInterface, kOccupantIid, static_cast<void*>(&occupant)))
                Release(occupant);
        if (occupant) {
            AddRef(occupant);
            Virtual<void>(occupant, 0x34, 0u);
            Release(occupant);
        }
    }
    if (void* const situation = At<void*>(self, field::kSituation)) {
        if (Global<void*>(addr::SC4_Service43D34))
            if (void* const service = AdviceService())
                Virtual<void>(service, 0x50, Virtual<void*>(situation, 0x0C));
        RemoveEntryAt(self, situation);
    }
    Virtual<void>(self, slot::kStopSituation);
}

// 0x0052BFB0 (ret 4): the joyride button. The window's child 0x8BB696C1's
// slot 91 object kept; dismissed (0x0052BF00); a joyride (slot 24 with the
// object, 1, 0) for it.
void __fastcall DismissForJoyride(char* self, void*, void* window)
{
    static bool logged;
    FirstCall("DismissForJoyride", logged);
    void* child = nullptr;
    void* automaton = nullptr;
    if (Virtual<std::uint8_t>(window, 0x1C4, 0x8BB696C1u, static_cast<void*>(&child)))
        if (void* const object = Virtual<void*>(child, 0x16C)) {
            AddRef(object);
            automaton = object;
        }
    CallThis<void>(addr::cSC4CitySituationManager_Dismiss, self);
    if (automaton) {
        Virtual<void>(self, slot::kStartJoyride, automaton, 1u, static_cast<void*>(nullptr));
        Release(automaton);
    }
}

// 0x0052CAB0 (the +0x10 part's window procedure, ret 8): the popup's
// messages. 0xA2BF8AD5 and 0xA2BF8AD6 true; a button (3, 0x287259F6): 1
// continues (0x00B43DC4's slot 57; when it succeeded or failed the popup
// closed and 0x6BB350E4 posted with the situation, else dismissed and
// closed), 6 the drive (slot 37), 7 the joyride (0x0052BFB0), each closed
// (0x00525330), true; a link (0xE4A85D91, 0x0078D140) as button 1, answering
// whether the link failed. Else false.
std::uint8_t __fastcall PopupProc(char* part, void*, void* window, std::uint32_t* message)
{
    static bool logged;
    FirstCall("PopupProc", logged);
    char* const self = part - field::kVtable10;
    const std::uint32_t type = message[0];
    const auto proceed = [&](std::uint8_t answer) {
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0xE4);
        void* const situation = At<void*>(self, field::kSituation);
        if (situation && (CallThis<std::uint32_t>(addr::cSC4CitySituationManager_IsFinished, self) & 0xFF)) {
            ClosePopupAt(self, window);
            PostAt(self, msg::kAdviceGone, At<void*>(self, field::kSituation), nullptr);
            return answer;
        }
        CallThis<void>(addr::cSC4CitySituationManager_Dismiss, self);
        ClosePopupAt(self, window);
        return answer;
    };
    if (type == 0xE4A85D91u) {
        std::uint32_t follower =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(message)) & 0xFFFFFF00u;
        const std::uint8_t followed =
            CallThis<std::uint8_t>(addr::SC4UI_FollowLink, static_cast<void*>(&follower), message[2], 0u);
        const std::uint8_t answer = followed ? 0 : 1;
        Virtual<void>(Global<void*>(addr::SC4_Service43DC4), 0xE4);
        void* const situation = At<void*>(self, field::kSituation);
        const std::uint32_t status = At<std::uint32_t>(self, field::kStatus);
        if (situation && (status == 1 || status == 2)) {
            ClosePopupAt(self, window);
            PostAt(self, msg::kAdviceGone, At<void*>(self, field::kSituation), nullptr);
            return answer;
        }
        CallThis<void>(addr::cSC4CitySituationManager_Dismiss, self);
        ClosePopupAt(self, window);
        return answer;
    }
    if (type == 0xA2BF8AD6u || type == 0xA2BF8AD5u)
        return 1;
    if (type != 3 || message[1] != 0x287259F6u)
        return 0;
    switch (message[2]) {
    case 1:
        return proceed(1);
    case 6:
        Virtual<void>(self, slot::kBeginDrive);
        break;
    case 7:
        CallThis<void>(addr::cSC4CitySituationManager_DismissForJoyride, self, window);
        break;
    default:
        break;
    }
    ClosePopupAt(self, window);
    return 1;
}

// ====================================================================================
// Installation

bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_AUTOMATA_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

bool InstallList(const Detour* detours, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i)
        if (!addr_check::Find(detours[i].address)) {
            log::Write("automata situations %s: no address entry for 0x%08X", what,
                       static_cast<unsigned>(detours[i].address));
            return false;
        }
    unsigned installed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (Skipped(detours[i].address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detours[i].address), detours[i].ours, &original))
            return false;
        ++installed;
    }
    log::Write("automata situations %s: %u detours", what, installed);
    return true;
}

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kMessages[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_DoMessage, DoMessage),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Frame, Frame),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_OccupantRemoved, OccupantRemoved),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_RecheckTarget, RecheckTarget),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_AutomatonDestroyed, AutomatonDestroyed),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TrackOccupant, TrackOccupant),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_AdviceGone, AdviceGone),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_AbortDrive, AbortDrive),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_IsFinished, IsFinished),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_CheckTargets, CheckTargets),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_TargetOccupant, TargetOccupant),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_PostSituationMessage, PostSituationMessage),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Succeed, Succeed),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Fail, Fail),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_RunScript, RunScript),
};

const Detour kList[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_AddEntry, AddEntry),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_RemoveEntry, RemoveEntry),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_UntrackAutomaton, UntrackAutomaton),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_FindCandidate, FindCandidate),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_SortList, SortList),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_MergeList, MergeList),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ListConstruct, ListConstruct),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ListInsert, ListInsert),
};

const Detour kPopup[] = {
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ShowPopup, ShowPopup),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_ClosePopup, ClosePopup),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_Dismiss, Dismiss),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_DismissForJoyride, DismissForJoyride),
    OPENSC4_DETOUR(addr::cSC4CitySituationManager_PopupProc, PopupProc),
};

#undef OPENSC4_DETOUR

bool InstallMessages(registry::Mode) { return InstallList(kMessages, sizeof kMessages / sizeof kMessages[0], "messages"); }
bool InstallListGroup(registry::Mode) { return InstallList(kList, sizeof kList / sizeof kList[0], "list"); }
bool InstallPopup(registry::Mode) { return InstallList(kPopup, sizeof kPopup / sizeof kPopup[0], "popup"); }

const bool g_registered[] = {
    registry::Register({"simulators.automata.situations.messages", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_DoMessage,
                        "Our city situation manager's DoMessage and its handlers: the frame (the countdown, the "
                        "drive's time, the warning, the target check), the success and the failure, the automata's "
                        "and occupants' events, the script, the message post (15 detours; part c3, "
                        "src/core/automata_situations_3.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.automata.situations.list", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_AddEntry,
                        "Our city situation manager's CSI list: the advices' entries added and removed, the "
                        "candidates' CSIs, the untracking, the list's sort, merge, construction and insert (8 "
                        "detours; part c3, src/core/automata_situations_3.cpp)",
                        false, InstallListGroup}),
    registry::Register({"simulators.automata.situations.popup", "simulators/automata", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4CitySituationManager_PopupProc,
                        "Our city situation manager's message popup: its opening, closing, the window procedure "
                        "and the dismiss and joyride answers (5 detours; part c3, "
                        "src/core/automata_situations_3.cpp)",
                        false, InstallPopup}),
};

} // namespace
} // namespace opensc4::automata_situations

// Our cSC4MySimAgentSimulator (build 641; docs/mysim.md). The simulator
// keeps the city's MySims in a vector of six (+0x14; the five MySim slots
// and, last, the one whose slot index is 30) and hands them the game's
// messages; the MySims themselves (cSC4MySim) do the work. Every function is
// a replacement on the original object (vtable slots, detours), and
// simulators.mysim.object registers our own object under the class ID.
#include "core/address_check.h"
#include "core/director.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cIGZUnknown.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::mysim_sim {
namespace {

// Fields (offsets from the object's start; 0x3C bytes).
namespace field {
constexpr std::size_t kInitialized = 0x0C; // uint8
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kSims = 0x14;        // SC4RefVector<cISC4MySim*>: begin, end, capacity
constexpr std::size_t kDisasterKey = 0x20; // from message 0xCA1B1E64 (its slot 24)
constexpr std::size_t kKeys = 0x24;        // three uint32 from 0x00B20E10
constexpr std::size_t kCity = 0x30;
constexpr std::size_t kMessages = 0x34;    // the message server
constexpr std::size_t kPanelSlot = 0x38;   // int32, -1 for none
} // namespace field

constexpr uint32_t kIidMySim = 0x0A1DA42F;
constexpr uint32_t kIidOccupant = 0x87DDA3A9;
constexpr uint32_t kCheatMessage = 0x230E27AC;
constexpr uint32_t kCheatCode = 0xEB6F3C97;
constexpr uint32_t kOccupantKind = 0x278128A0;

constexpr uint32_t kMessageTypes[] = {0x99EF1142, 0x99EF1145, 0x99EF1143, 0xA6B79602, 0xA6B79612, 0x44778230,
                                      0x8A09FA6C, 0xCA1B1E64, 0xEA1F0DCD, 0x0A36F1E9, 0xEA0D163E};

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("mysim: first %s", name);
}

void** Sims(void* self) { return At<void**>(self, field::kSims); }

uint32_t SimCount(void* self)
{
    return static_cast<uint32_t>(At<void**>(self, field::kSims + 4) - At<void**>(self, field::kSims));
}

// The script service (0xEBE0E860, interface 0xE898ED03) into `holder`.
void GetScriptService(void** holder)
{
    using GetFrameworkFn = void*(__cdecl*)();
    *holder = nullptr;
    if (void* const framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
        Virtual<uint8_t>(framework, 0x14, 0xEBE0E860u, 0xE898ED03u, holder);
}

void ReleaseHolder(void** holder)
{
    reinterpret_cast<void(__thiscall*)(void**)>(addr::SC4_ReleaseRefHolder)(holder);
}

// `*slot` = `value`, referenced, the old one released (the game's inline
// smart-pointer assignment).
void Assign(void** slot, void* value)
{
    void* const old = *slot;
    if (value == old)
        return;
    if (value)
        Virtual<uint32_t>(value, 0x4);
    *slot = value;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// --- The interface --------------------------------------------------------

// Slot 5 (0x0042F4A0): the active MySims (their slot 20) of the first five
// into `out` (emptied first), referenced; true when there is one.
uint8_t __fastcall GetActiveMySims(void* self, void*, void** out)
{
    static bool logged = false;
    FirstCall("GetActiveMySims", logged);
    using EraseFn = void**(__thiscall*)(void* vector, void** first, void** last);
    using CopyFn = void**(__cdecl*)(void** first, void** last, void** dest);
    using AllocFn = void*(__cdecl*)(std::size_t size);
    using DestroyFn = void(__thiscall*)(void* vector);
    reinterpret_cast<EraseFn>(addr::SC4RefVector_Erase)(out, static_cast<void**>(out[0]), static_cast<void**>(out[1]));
    bool none = true;
    for (int i = 0; i < 5; ++i) {
        void* const sim = Sims(self)[i];
        if (!sim || !Virtual<uint8_t>(sim, 0x50))
            continue;
        Virtual<uint32_t>(sim, 0x4);
        auto*& begin = reinterpret_cast<void**&>(out[0]);
        auto*& end = reinterpret_cast<void**&>(out[1]);
        auto*& capacity = reinterpret_cast<void**&>(out[2]);
        if (end != capacity) {
            if (end) {
                *end = sim;
                Virtual<uint32_t>(sim, 0x4);
            }
            ++end;
        } else {
            const uint32_t size = static_cast<uint32_t>(end - begin);
            const uint32_t grown = size + (size < 1 ? 1 : size);
            void** const storage = grown ? static_cast<void**>(reinterpret_cast<AllocFn>(addr::SC4_stl_allocate)(grown * 4))
                                         : nullptr;
            void** last = reinterpret_cast<CopyFn>(addr::SC4RefVector_CopyReferenced)(begin, end, storage);
            if (last) {
                *last = sim;
                Virtual<uint32_t>(sim, 0x4);
            }
            reinterpret_cast<DestroyFn>(addr::SC4RefVector_Destroy)(out);
            begin = storage;
            end = last + 1;
            capacity = storage + grown;
        }
        Virtual<uint32_t>(sim, 0x8);
        none = false;
    }
    return none ? 0 : 1;
}

// Slot 6 (0x0042E990): the first of the five MySims that is not active
// moves in (its slot 4, the slot number first).
void __fastcall MoveIn(void* self, void*, void* name, uint32_t male, uint32_t sign, void* bio, uint32_t bitmap,
                       void* path, uint32_t slot)
{
    static bool logged = false;
    FirstCall("MoveIn", logged);
    for (int i = 0; i < 5; ++i) {
        void* const sim = Sims(self)[i];
        if (sim && !Virtual<uint8_t>(sim, 0x50)) {
            Virtual<void>(sim, 0x10, slot, name, male, sign, bio, bitmap, path);
            return;
        }
    }
}

// Slot 7 (0x0042E9F0): `target` (every MySim when null) shown (slots 32,
// 34, 36 with 1), the others hidden (33, 35, 36 with 0).
void __fastcall TrackLocation(void* self, void*, void* target)
{
    static bool logged = false;
    FirstCall("TrackLocation", logged);
    for (int i = 0; i < 6; ++i) {
        void* const sim = Sims(self)[i];
        if (!sim)
            continue;
        if (target && sim != target) {
            Virtual<void>(sim, 0x84);
            Virtual<void>(sim, 0x8C);
            Virtual<void>(sim, 0x90, 0u);
        } else {
            Virtual<void>(sim, 0x80);
            Virtual<void>(sim, 0x88);
            Virtual<void>(sim, 0x90, 1u);
        }
    }
}

// Slot 8 (0x0042EA70): every MySim hidden.
void __fastcall HideLocation(void* self, void*)
{
    static bool logged = false;
    FirstCall("HideLocation", logged);
    for (int i = 0; i < 6; ++i) {
        if (void* const sim = Sims(self)[i]) {
            Virtual<void>(sim, 0x84);
            Virtual<void>(sim, 0x8C);
            Virtual<void>(sim, 0x90, 0u);
        }
    }
}

// Slot 9 (0x0042EB60): the one of the five MySims whose home (slot 11) is
// `building`.
void* __fastcall MySimLivesHere(void* self, void*, void* building)
{
    static bool logged = false;
    FirstCall("MySimLivesHere", logged);
    for (int i = 0; i < 5; ++i) {
        void* const sim = Sims(self)[i];
        if (sim && Virtual<void*>(sim, 0x2C) == building)
            return Sims(self)[i];
    }
    return nullptr;
}

// Slot 11 (0x0042E910): the index of the one of the five MySims whose slot
// number (its slot 10) is `slot`, or -1.
void __fastcall SetActiveUIPanelSim(void* self, void*, int32_t slot)
{
    static bool logged = false;
    FirstCall("SetActiveUIPanelSim", logged);
    for (int32_t i = 0; i < 5; ++i) {
        void* const sim = Sims(self)[i];
        if (sim && static_cast<int32_t>(Virtual<uint8_t>(sim, 0x28)) == slot) {
            At<int32_t>(self, field::kPanelSlot) = i;
            return;
        }
    }
    At<int32_t>(self, field::kPanelSlot) = -1;
}

// Slot 12 (0x0042E960): the panel's MySim and its index, or null.
void* __fastcall GetActiveUIPanelSim(void* self, void*, int32_t* slot)
{
    static bool logged = false;
    FirstCall("GetActiveUIPanelSim", logged);
    const int32_t index = At<int32_t>(self, field::kPanelSlot);
    if (index < 0 || static_cast<uint32_t>(index) >= SimCount(self))
        return nullptr;
    *slot = index;
    return Sims(self)[At<int32_t>(self, field::kPanelSlot)];
}

// --- Messages -------------------------------------------------------------

using StringFn = void(__thiscall*)(void* string);
using TextFn = void*(__thiscall*)(void* string, const char* text);

// The cheat (0x230E27AC with code 0xEB6F3C97): "print" posts 0x0B6F3E27;
// "newjob <n>", "newhouse <n>" and "dispatch <n>" post 0xAB6F3E2D,
// 0x4B6F3EBF and 0x0BB04126 with the number.
void Cheat(void* message)
{
    using TokenizerFn = void*(__thiscall*)(void* tokenizer, const char* text);
    using OptionFn = uint8_t(__thiscall*)(void* tokenizer, void* name, void* value, uint32_t flag);
    using HasFn = uint8_t(__thiscall*)(void* tokenizer, void* name);
    using HolderFn = void**(__thiscall*)(void** holder, void* object);
    using NewFn = void*(__cdecl*)(std::size_t size);
    using CtorFn = void*(__thiscall*)(void* message);
    using DataFn = const char*(__thiscall*)(void* string);
    using NumberFn = int32_t(__cdecl*)(const char* text);
    alignas(4) char tokenizer[0x30];
    alignas(4) char value[0x14];
    alignas(4) char name[0x14];
    void* holder = nullptr;
    void* const text = Virtual<void*>(message, 0x3C);
    reinterpret_cast<TokenizerFn>(addr::SC4Tokenizer_SC4Tokenizer)(tokenizer, Virtual<const char*>(text, 0x14));
    reinterpret_cast<StringFn>(addr::SC4String_SC4String)(value);
    const auto Option = [&](const char* word) {
        void* const n = reinterpret_cast<TextFn>(addr::SC4String_FromText)(name, word);
        const uint8_t found = reinterpret_cast<OptionFn>(addr::SC4Tokenizer_GetOption)(tokenizer, n, value, 1u);
        reinterpret_cast<StringFn>(addr::SC4String_Destroy)(name);
        return found != 0;
    };
    const auto NewMessage = [&]() {
        void* m = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        if (m)
            m = reinterpret_cast<CtorFn>(addr::cGZMessage_cGZMessage)(m);
        reinterpret_cast<HolderFn>(addr::SC4RefHolder_SC4RefHolder)(&holder, m);
        return holder;
    };
    const auto Post = [&](void* m) {
        Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x10, m, 0u);
        Virtual<uint32_t>(m, 0x8);
    };
    const auto PostNumber = [&](uint32_t type) {
        void* const m = NewMessage();
        Virtual<void>(m, 0x14, type);
        const char* const digits = reinterpret_cast<DataFn>(addr::cRZBaseString_Data)(value);
        Virtual<void>(m, 0x2C, reinterpret_cast<NumberFn>(addr::SC4_TextToInt)(digits));
        Post(m);
    };
    if (Option("print")) {
        void* const m = NewMessage();
        Virtual<void>(m, 0x14, 0x0B6F3E27u);
        Post(m);
    } else if (Option("newjob")) {
        PostNumber(0xAB6F3E2Du);
    } else if (Option("newhouse")) {
        PostNumber(0x4B6F3EBFu);
    } else {
        void* const n = reinterpret_cast<TextFn>(addr::SC4String_FromText)(name, "dispatch");
        const uint8_t found = reinterpret_cast<HasFn>(addr::SC4Tokenizer_HasOption)(tokenizer, n);
        reinterpret_cast<StringFn>(addr::SC4String_Destroy)(name);
        if (found) {
            void* const m = NewMessage();
            Virtual<void>(m, 0x14, 0x0BB04126u);
            const char* const digits = reinterpret_cast<DataFn>(addr::cRZBaseString_Data)(value);
            Virtual<void>(m, 0x2C, reinterpret_cast<NumberFn>(addr::SC4_TextToInt)(digits));
            Post(m);
        }
    }
    reinterpret_cast<StringFn>(addr::SC4String_Destroy)(value);
    reinterpret_cast<StringFn>(addr::SC4Tokenizer_Destroy)(tokenizer);
}

// The five MySims each called at `slot` with `args` (the handlers
// 0x0042E640 to 0x0042EAE0 are all this loop).
template <typename... Args>
void EachOfFive(void* self, std::size_t slot, Args... args)
{
    for (int i = 0; i < 5; ++i)
        if (void* const sim = Sims(self)[i])
            Virtual<void>(sim, slot, args...);
}

// An occupant (interface 0x87DDA3A9) of `object` to the five MySims at
// `slot`, with `extra` when given (0x0042E690, 0x0042E6F0, 0x0042E880).
void OccupantToSims(void* self, void* object, std::size_t slot, bool withExtra, void* extra)
{
    void* occupant = nullptr;
    if (Virtual<uint8_t>(object, 0x0, kIidOccupant, &occupant)) {
        for (int i = 0; i < 5; ++i) {
            if (void* const sim = Sims(self)[i]) {
                if (withExtra)
                    Virtual<void>(sim, slot, occupant, extra);
                else
                    Virtual<void>(sim, slot, occupant);
            }
        }
    }
    if (occupant)
        Virtual<uint32_t>(occupant, 0x8);
}

// Cell (a, b) into a position (the city's slot 97, in place), to the five
// MySims at `slot` (0x0042E7A0, 0x0042E7F0).
void CellToSims(void* self, uint32_t a, uint32_t b, std::size_t slot)
{
    uint32_t x = a, z = b;
    Virtual<void>(At<void*>(self, field::kCity), 0x184, a, b, &x, &z);
    EachOfFive(self, slot, x, z);
}

// cIGZMessageTarget2 slot 3 (0x0042EFA0), on the part at +4.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(part) - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x44778230: { // 0x0042EAE0: by code, slots 38, 39, 54
        const uint32_t code = Virtual<uint32_t>(message, 0x38);
        if (code == 8)
            EachOfFive(self, 0x98);
        else if (code == 0x11)
            EachOfFive(self, 0x9C);
        else if (code == 0x16)
            EachOfFive(self, 0xD8);
        return 1;
    }
    case kCheatMessage:
        if (Virtual<uint32_t>(message, 0x24) == kCheatCode)
            Cheat(message);
        return 1;
    case 0x0A36F1E9: {
        const uint32_t b = Virtual<uint32_t>(message, 0x4C);
        CellToSims(self, Virtual<uint32_t>(message, 0x38), b, 0xBC);
        return 1;
    }
    case 0x8A09FA6C: {
        void* sim = nullptr;
        void* const object = Virtual<void*>(message, 0x80);
        if (Virtual<uint8_t>(object, 0x0, kIidMySim, &sim))
            Virtual<void>(sim, 0xA4, Virtual<uint32_t>(message, 0x24));
        if (sim)
            Virtual<uint32_t>(sim, 0x8);
        return 1;
    }
    case 0x99EF1142: {
        const uint32_t kind = Virtual<uint32_t>(message, 0x4C);
        if (kind == kOccupantKind)
            OccupantToSims(self, Virtual<void*>(message, 0x28), 0xA8, false, nullptr);
        else if (kind == 0x69D25C5F) { // 0x0042E750: a position, its x and z
            uint32_t position[3] = {};
            Virtual<void>(Virtual<void*>(message, 0x28), 0x20, position);
            EachOfFive(self, 0xB4, position[0], position[2]);
        }
        [[fallthrough]];
    }
    case 0x99EF1145:
        if (Virtual<uint32_t>(message, 0x4C) == kOccupantKind) {
            void* const object = Virtual<void*>(message, 0x28);
            Virtual<uint32_t>(message, 0x60);
            OccupantToSims(self, object, 0xAC, false, nullptr);
        }
        return 1;
    case 0x99EF1143:
        if (Virtual<uint32_t>(message, 0x4C) == kOccupantKind) {
            void* const object = Virtual<void*>(message, 0x28);
            const uint32_t kind = Virtual<uint32_t>(message, 0x60);
            void* extra = nullptr; // 0x0042E880: the disaster key for kind 4 during a disaster
            if (kind == 4 && Virtual<uint8_t>(Virtual<void*>(At<void*>(self, field::kCity), 0x120), 0x28))
                extra = reinterpret_cast<void*>(static_cast<uintptr_t>(At<uint32_t>(self, field::kDisasterKey)));
            OccupantToSims(self, object, 0xB0, true, extra);
        }
        return 1;
    case 0xA6B79602:
    case 0xA6B79612:
        EachOfFive(self, 0x94);
        return 1;
    case 0xCA1B1E64: { // 0x0042E640
        const uint32_t a = Virtual<uint32_t>(message, 0x38);
        const uint32_t b = Virtual<uint32_t>(message, 0x4C);
        At<uint32_t>(self, field::kDisasterKey) = Virtual<uint32_t>(message, 0x60);
        for (int i = 0; i < 5; ++i)
            if (void* const sim = Sims(self)[i])
                Virtual<void>(sim, 0xC0, At<uint32_t>(self, field::kDisasterKey), a, b);
        return 1;
    }
    case 0xEA0D163E: {
        const uint32_t b = Virtual<uint32_t>(message, 0x4C);
        EachOfFive(self, 0xC4, Virtual<uint32_t>(message, 0x38), b);
        return 1;
    }
    case 0xEA1F0DCD: {
        const uint32_t b = Virtual<uint32_t>(message, 0x38);
        CellToSims(self, Virtual<uint32_t>(message, 0x24), b, 0xB8);
        return 1;
    }
    default:
        return 1;
    }
}

// --- Init and Shutdown ----------------------------------------------------

// The successor of a node of the game's map (left +8, parent +4, right
// +0xC), as its inline iterator increment.
char* Next(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (At<char*>(right, 0x8))
            right = At<char*>(right, 0x8);
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    return At<char*>(node, 0xC) != parent ? parent : node;
}

// Slot 3 (0x0042EC30): the city; the MySims from the city's slot 81 (each
// map value's MySim interface, the one at slot index 30 last, the others in
// order); the messages; the script commands get_sim and get_count.
uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    if (!At<void*>(self, field::kCity))
        Assign(&At<void*>(self, field::kCity), Global<void*>(addr::SC4_City));
    void* const holder = Virtual<void*>(At<void*>(self, field::kCity), 0x144);
    if (holder) {
        Virtual<uint32_t>(holder, 0x4);
        char** const map = Virtual<char**>(holder, 0x20);
        std::size_t next = 0;
        for (char* node = At<char*>(*map, 0x8); node != *map; node = Next(node)) {
            void* const object = At<void*>(node, 0x14);
            void* sim = nullptr;
            if (Virtual<uint8_t>(object, 0x0, kIidMySim, &sim)) {
                void** slot;
                if (Virtual<uint32_t>(object, 0x14) == 0x1E) {
                    slot = Sims(self) + 5;
                } else {
                    slot = Sims(self) + next;
                    ++next;
                }
                Assign(slot, sim);
            }
            if (sim)
                Virtual<uint32_t>(sim, 0x8);
        }
    }
    if (!At<void*>(self, field::kMessages)) {
        Assign(&At<void*>(self, field::kMessages), Global<void*>(addr::SC4_MessageServer));
        void* const target = static_cast<char*>(self) + 0x4;
        for (const uint32_t type : kMessageTypes)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x14, target, type);
    }
    using RegisterFunctionFn = void*(__cdecl*)(std::uintptr_t, const char*, std::uintptr_t, void*, uint32_t, void*, void*);
    void* script;
    GetScriptService(&script);
    reinterpret_cast<RegisterFunctionFn>(addr::SC4Script_RegisterFunction)(
        addr::cSC4MySimAgentSimulator_ScriptGetSim, reinterpret_cast<const char*>(addr::cSC4MySimAgentSimulator_GetSimName), addr::cSC4MySimAgentSimulator_ScriptGetSim, self, 0,
        self, script);
    ReleaseHolder(&script);
    GetScriptService(&script);
    reinterpret_cast<RegisterFunctionFn>(addr::SC4Script_RegisterFunction)(
        addr::cSC4MySimAgentSimulator_ScriptGetCount, reinterpret_cast<const char*>(addr::cSC4MySimAgentSimulator_GetCountName), addr::cSC4MySimAgentSimulator_ScriptGetCount, self,
        0, self, script);
    ReleaseHolder(&script);
    if (holder)
        Virtual<uint32_t>(holder, 0x8);
    return 1;
}

// Slot 4 (0x0042E500): the script commands and messages off, the city and
// every MySim released.
uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    using UnregisterFn = uint8_t(__cdecl*)(void* self, void* service);
    void* script;
    GetScriptService(&script);
    reinterpret_cast<UnregisterFn>(addr::SC4Script_UnregisterTable)(self, script);
    ReleaseHolder(&script);
    At<uint8_t>(self, field::kInitialized) = 0;
    if (At<void*>(self, field::kMessages)) {
        void* const target = static_cast<char*>(self) + 0x4;
        for (const uint32_t type : kMessageTypes)
            Virtual<uint8_t>(At<void*>(self, field::kMessages), 0x18, target, type);
        if (void* const server = At<void*>(self, field::kMessages)) {
            At<void*>(self, field::kMessages) = nullptr;
            Virtual<uint32_t>(server, 0x8);
        }
    }
    if (void* const city = At<void*>(self, field::kCity)) {
        At<void*>(self, field::kCity) = nullptr;
        Virtual<uint32_t>(city, 0x8);
    }
    for (int i = 0; i < 6; ++i) {
        void** const slot = Sims(self) + i;
        if (void* const sim = *slot) {
            *slot = nullptr;
            Virtual<uint32_t>(sim, 0x8);
        }
    }
    return 1;
}

// --- Save -----------------------------------------------------------------

// cIGZSerializable slot 3 (the game's shared 0x0042E440): version 1, no data.
uint32_t __fastcall Write(void*, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (the game's shared 0x006B8A70): version 1 only.
uint32_t __fastcall Read(void*, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Script commands --------------------------------------------------------

using ScriptValueFn = void*(__thiscall*)(void* value, uintptr_t content);

// get_count (0x0042E460): the size of the MySim vector of the simulator
// registered with the command; true when not empty.
uint8_t __cdecl ScriptGetCount(void* state, void*, void* result, void* script)
{
    static bool logged = false;
    FirstCall("ScriptGetCount", logged);
    void* const simulator = Virtual<void*>(script, 0x48, Virtual<void*>(script, 0x20, state));
    const int32_t count = simulator ? static_cast<int32_t>(SimCount(simulator)) : 0;
    alignas(4) char value[0x14];
    reinterpret_cast<ScriptValueFn>(addr::SC4ScriptNumber_FromInt)(value, static_cast<uintptr_t>(count));
    Virtual<uint32_t>(result, 0x1C, 1u, static_cast<void*>(value));
    reinterpret_cast<StringFn>(addr::SC4ScriptNumber_Destroy)(value);
    return count > 0 ? 1 : 0;
}

// get_sim (0x0042EBA0): the MySim at the index (its slot 27) of the object
// the arguments give (their slot 13 of their slot 6), or none.
uint8_t __cdecl ScriptGetSim(void* state, void* args, void* result, void* script)
{
    static bool logged = false;
    FirstCall("ScriptGetSim", logged);
    void* const simulator = Virtual<void*>(script, 0x48, Virtual<void*>(script, 0x20, state));
    void* sim = nullptr;
    if (simulator) {
        if (void* const object = Virtual<void*>(args, 0x18, Virtual<uint32_t>(args, 0x34))) {
            const int32_t index = Virtual<int32_t>(object, 0x6C);
            if (index >= 0 && static_cast<uint32_t>(index) < SimCount(simulator))
                sim = Sims(simulator)[index];
        }
    }
    alignas(4) char value[0x14];
    reinterpret_cast<ScriptValueFn>(addr::SC4ScriptNumber_FromObject)(value, reinterpret_cast<uintptr_t>(sim));
    Virtual<uint32_t>(result, 0x1C, 1u, static_cast<void*>(value));
    reinterpret_cast<StringFn>(addr::SC4ScriptNumber_Destroy)(value);
    return sim ? 1 : 0;
}

// --- Installation ---------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kInterfaceSlots[] = {
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 5, 0x0042F4A0, GetActiveMySims),
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 6, 0x0042E990, MoveIn),
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 7, 0x0042E9F0, TrackLocation),
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 8, 0x0042EA70, HideLocation),
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 9, 0x0042EB60, MySimLivesHere),
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 11, 0x0042E910, SetActiveUIPanelSim),
    OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 12, 0x0042E960, GetActiveUIPanelSim),
};
const Slot kMessageSlots[] = {OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable_4, 3, 0x0042EFA0, DoMessage)};
const Slot kInitSlots[] = {OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 3, 0x0042EC30, Init)};
const Slot kShutdownSlots[] = {OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable, 4, 0x0042E500, Shutdown)};
const Slot kSaveSlots[] = {OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable_8, 3, 0x0042E440, Write)};
const Slot kLoadSlots[] = {OPENSC4_SLOT(addr::cSC4MySimAgentSimulator_vftable_8, 4, 0x006B8A70, Read)};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("mysim %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("mysim %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool InstallInterface(registry::Mode) { return SwapSlots(kInterfaceSlots, "interface"); }
bool InstallMessages(registry::Mode) { return SwapSlots(kMessageSlots, "messages"); }
bool InstallInit(registry::Mode) { return SwapSlots(kInitSlots, "init"); }
bool InstallShutdown(registry::Mode) { return SwapSlots(kShutdownSlots, "shutdown"); }
bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }
bool InstallLoad(registry::Mode) { return SwapSlots(kLoadSlots, "load"); }

using ScriptFn = uint8_t(__cdecl*)(void*, void*, void*, void*);
ScriptFn g_originalCount = nullptr;
ScriptFn g_originalSim = nullptr;

bool InstallScripts(registry::Mode)
{
    const addr::Entry* count = addr_check::Find(addr::cSC4MySimAgentSimulator_ScriptGetCount);
    const addr::Entry* sim = addr_check::Find(addr::cSC4MySimAgentSimulator_ScriptGetSim);
    return game::IsBuild641() && count && sim && hook::Install(*count, &ScriptGetCount, &g_originalCount) &&
           hook::Install(*sim, &ScriptGetSim, &g_originalSim);
}

const bool g_registered[] = {
    registry::Register({"simulators.mysim.interface", "simulators/mysim", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_vftable,
                        "Our cISC4MySimAgentSimulator queries: active MySims, move in, locations, homes, the UI "
                        "panel (docs/mysim.md)",
                        false, InstallInterface}),
    registry::Register({"simulators.mysim.messages", "simulators/mysim", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_vftable_4,
                        "Our MySim DoMessage: messages handed to the MySims, the cheat (docs/mysim.md)", false,
                        InstallMessages}),
    registry::Register({"simulators.mysim.init", "simulators/mysim", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_vftable,
                        "Our MySim Init: the MySims from the city, messages, script commands (docs/mysim.md)", false,
                        InstallInit}),
    registry::Register({"simulators.mysim.shutdown", "simulators/mysim", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_vftable,
                        "Our MySim Shutdown (docs/mysim.md)", false, InstallShutdown}),
    registry::Register({"simulators.mysim.save", "simulators/mysim", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_vftable_8,
                        "Our MySim Write: the save record, version 1 (docs/mysim.md)", false, InstallSave}),
    registry::Register({"simulators.mysim.load", "simulators/mysim", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_vftable_8,
                        "Our MySim Read (docs/mysim.md)", false, InstallLoad}),
    registry::Register({"simulators.mysim.scripts", "simulators/mysim", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4MySimAgentSimulator_ScriptGetCount,
                        "Our MySim script commands get_count and get_sim (docs/mysim.md)", false, InstallScripts}),
};

// --- Our object -------------------------------------------------------------

constexpr uint32_t kClassId = 0x29E37DEC;
constexpr uint32_t kIidUnknown = 0x00000001;
constexpr uint32_t kIidSimulator = 0xA9E37EBB;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr std::size_t kSize = 0x3C;

constexpr int kSlots = 14, kSlots4 = 4, kSlots8 = 6;
void* g_vtable[kSlots];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];

uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x0042E310).
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidSimulator:
    case kIidUnknown: offset = 0; break;
    case kIidMessageTarget: offset = 0x4; break;
    case kIidSerializable: offset = 0x8; break;
    default: *object = nullptr; return 0;
    }
    *object = self ? self + offset : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x0042E410): at the last reference, Shutdown and the
// deleting destructor (slot 13).
uint32_t __fastcall Release(char* self, void*)
{
    int32_t& refs = At<int32_t>(self, field::kRefCount);
    if (refs > 1)
        return static_cast<uint32_t>(--refs);
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x34, 1u);
    return 0;
}

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, 0x4) = g_vtable4;
    At<void**>(self, 0x8) = g_vtable8;
}

// As the game's (0x0042F450).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    using DestroyFn = void(__thiscall*)(void* vector);
    using FreeFn = void(__cdecl*)(void* block);
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("mysim object: destroying %p", static_cast<void*>(self));
    SetVtables(self);
    if (void* const messages = At<void*>(self, field::kMessages))
        Virtual<uint32_t>(messages, 0x8);
    if (void* const city = At<void*>(self, field::kCity))
        Virtual<uint32_t>(city, 0x8);
    reinterpret_cast<DestroyFn>(addr::SC4RefVector_Destroy)(self + field::kSims);
    if (flags & 1)
        reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(self);
    return self;
}

uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return Release(part - Offset, nullptr);
}

// As the game's constructor (0x0042F5D0): six empty MySim places.
void Construct(char* self)
{
    using FillFn = void(__thiscall*)(void* vector, void** where, uint32_t count, void* const* value);
    using EraseFn = void**(__thiscall*)(void* vector, void** first, void** last);
    SetVtables(self);
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<void*>(self, field::kSims) = nullptr;
    At<void*>(self, field::kSims + 4) = nullptr;
    At<void*>(self, field::kSims + 8) = nullptr;
    At<uint32_t>(self, field::kDisasterKey) = 0;
    for (int i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kKeys + 4 * i) = Global<uint32_t>(addr::cSC4MySimAgentSimulator_KeyDefault);
    At<void*>(self, field::kCity) = nullptr;
    At<void*>(self, field::kMessages) = nullptr;
    At<int32_t>(self, field::kPanelSlot) = -1;
    void* const vector = self + field::kSims;
    void* const none = nullptr;
    const uint32_t size = SimCount(self);
    if (size > 6)
        reinterpret_cast<EraseFn>(addr::SC4RefVector_Erase)(vector, Sims(self) + 6, At<void**>(self, field::kSims + 4));
    else
        reinterpret_cast<FillFn>(addr::SC4RefVector_FillInsert)(vector, At<void**>(self, field::kSims + 4), 6 - size,
                                                                &none);
    for (int i = 0; i < 6; ++i) {
        void** const slot = Sims(self) + i;
        if (void* const sim = *slot) {
            *slot = nullptr;
            Virtual<uint32_t>(sim, 0x8);
        }
    }
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4MySimAgentSimulator_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4MySimAgentSimulator_vftable_4), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4MySimAgentSimulator_vftable_8), sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&Release);
    g_vtable[13] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<0x4>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<0x8>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
}

// The factory, as the game's (0x0042F6B0).
cIGZUnknown* Create()
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    auto* const self = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(kSize));
    if (!self)
        return nullptr;
    Construct(self);
    static int created = 0;
    if (created++ < 3)
        log::Write("mysim object: created %p", static_cast<void*>(self));
    return reinterpret_cast<cIGZUnknown*>(self);
}

bool ApplyObject(registry::Mode) { return game::IsBuild641() && director::AddClass(kClassId, Create); }

const bool g_registeredObject = registry::Register({
    "simulators.mysim.object",
    "simulators/mysim",
    registry::Kind::Gzcom,
    registry::Phase::DirectorInit,
    kClassId,
    "Our cSC4MySimAgentSimulator object under the game's class ID: factory, constructor, destructor, identity; "
    "methods from the (patched) vtables (docs/mysim.md)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::mysim_sim

// OPENSC4_TEST_T7=<day>: the U-Drive-It control's first piece (ui.tools.udrive1,
// docs/tools.md). It calls only the game's addresses and the control's vtable
// slots, never our functions, so a run with the group on and one with it off
// compare. Nothing changes the city: the control is made on its own (the way
// the park automaton makes it, without Init), its driven object and its
// point holder are stand-ins that record what they are told, and it is
// destroyed through slot 26 (not Release, which would run the game's
// Shutdown). Pointers are left out of the hash. Token ` udrive1`, sub-hashes
// on the log lines `autotest: udrive1: ...`.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_token[32];

// A stand-in with a vtable of 64 slots: Release (slot 2) counts, slot 0x7C
// answers from a script of flag answers, slot 0xF0 records its arguments.
struct Fake {
    void** vtable;
    char pad[0x20];
    int32_t pointX; // +0x24 for the point holder
    int32_t pointZ; // +0x28
    char rest[0x40];
};

void* g_vtable[64];
Fnv* g_log = nullptr;
int g_releases = 0;
uint32_t g_answers = 0; // bit n: the answer to the n-th flag question
int g_asked = 0;

uint32_t __fastcall FakeRelease(Fake*, void*)
{
    ++g_releases;
    return 1;
}

uint8_t __fastcall FakeFlag(Fake*, void*, uint32_t flag)
{
    const uint8_t answer = (g_answers >> g_asked) & 1;
    ++g_asked;
    if (g_log) {
        g_log->Add(uint8_t{0x7C});
        g_log->Add(flag);
        g_log->Add(answer);
    }
    return answer;
}

void __fastcall FakeCommand(Fake*, void*, uint32_t command, uint32_t value)
{
    if (g_log) {
        g_log->Add(uint8_t{0xF0});
        g_log->Add(command);
        g_log->Add(value);
    }
}

uint32_t __fastcall FakeOther(Fake*, void*)
{
    return 0;
}

void SetUpFake(Fake& fake)
{
    for (void*& slot : g_vtable)
        slot = reinterpret_cast<void*>(&FakeOther);
    g_vtable[2] = reinterpret_cast<void*>(&FakeRelease);
    g_vtable[0x7C / 4] = reinterpret_cast<void*>(&FakeFlag);
    g_vtable[0xF0 / 4] = reinterpret_cast<void*>(&FakeCommand);
    std::memset(&fake, 0, sizeof fake);
    fake.vtable = g_vtable;
}

// The fields a hash may see: everything but the list head's pointer (+0x64)
// and the stopwatch (+0x48 to +0x5F).
void AddFields(Fnv& f, const char* control)
{
    f.Bytes(control, 0x48);
    f.Bytes(control + 0x60, 4);
    f.Bytes(control + 0x68, 0xC0 - 0x68);
    void* const* head = *reinterpret_cast<void* const* const*>(control + 0x64);
    f.Add(uint8_t{head && head[0] == head && head[1] == head});
}

uint8_t Button(char* control, std::size_t slot)
{
    return Virtual<uint8_t>(control, slot, int32_t{100}, int32_t{200}, uint32_t{0});
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_T7") || !game::IsBuild641())
        return;
    if (!Global<void*>(addr::SC4_City)) {
        log::Write("autotest: udrive1: no city");
        return;
    }
    Fnv total;

    // 1: made.
    char* const control = static_cast<char*>(
        reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC0));
    std::memset(control, 0x5A, 0xC0);
    reinterpret_cast<void*(__thiscall*)(void*)>(addr::cSC4UDriveControl_Construct)(control);
    Fnv made;
    AddFields(made, control);
    log::Write("autotest: udrive1: made %08X", made.h);
    total.Add(made.h);

    // 2: the buttons with nothing to drive and no point holder.
    Fnv empty;
    for (std::size_t slot : {std::size_t{0x44}, std::size_t{0x48}, std::size_t{0x4C}})
        empty.Add(Button(control, slot));
    empty.Add(reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4UDriveControl_CanDrive)(control));
    log::Write("autotest: udrive1: empty %08X", empty.h);
    total.Add(empty.h);

    // 3: the point holder, set and already cleared.
    Fake holder;
    SetUpFake(holder);
    At<void*>(control, 0x9C) = &holder;
    Fnv point;
    for (int32_t value : {int32_t{5}, int32_t{-1}}) {
        holder.pointX = value;
        holder.pointZ = value == -1 ? -1 : 6;
        point.Add(Button(control, 0x48));
        point.Add(holder.pointX);
        point.Add(holder.pointZ);
    }
    holder.pointX = -1;
    holder.pointZ = 7;
    point.Add(Button(control, 0x48));
    point.Add(holder.pointX);
    point.Add(holder.pointZ);
    log::Write("autotest: udrive1: point %08X", point.h);
    total.Add(point.h);

    // 4: a driven stand-in with scripted flag answers. In OnMouseUpL the
    // answers past the first two are CanDrive's: those are always "set" so
    // the release never reaches command 7 and the service of 0x00602640.
    Fake driven;
    SetUpFake(driven);
    At<void*>(control, 0x30) = &driven;
    const uint8_t cityBusy = Virtual<uint8_t>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4), 0x80);
    Fnv drive;
    drive.Add(cityBusy);
    g_log = &drive;
    for (uint32_t script : {uint32_t{0x1}, uint32_t{0x2}, uint32_t{0x0}, uint32_t{0xC}, uint32_t{0x8}}) {
        for (std::size_t slot : {std::size_t{0x44}, std::size_t{0x4C}, std::size_t{0x48}}) {
            g_answers = slot == 0x48 ? (script & 3) | 0x4 : script;
            g_asked = 0;
            drive.Add(static_cast<uint32_t>(slot));
            drive.Add(Button(control, slot));
            drive.Add(g_asked);
        }
        g_answers = script >> 2;
        g_asked = 0;
        drive.Add(reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::cSC4UDriveControl_CanDrive)(control));
        drive.Add(g_asked);
    }
    g_log = nullptr;
    log::Write("autotest: udrive1: drive %08X (city answer %u)", drive.h, cityBusy);
    total.Add(drive.h);

    // 5: destroyed through slot 26: the two stand-ins are let go.
    g_releases = 0;
    Virtual<void*>(control, 0x68, uint32_t{1});
    total.Add(g_releases);
    log::Write("autotest: udrive1: destroyed (%d releases)", g_releases);

    std::snprintf(g_token, sizeof g_token, " udrive1 %08X", total.h);
    log::Write("autotest: udrive1: total %08X", total.h);
}

const DayHook kUDrive1{"udrive1", 68, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"udrive1", 29, &Fingerprint};

} // namespace
} // namespace opensc4::autotest

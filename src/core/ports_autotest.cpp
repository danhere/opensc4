// Autotest: builds an airport and a seaport, which no test city has
// (src/core/autotest_hooks.h; docs/ports.md).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cIGZMessage2Standard.h>
#include <cISC4City.h>

#include <cstdint>

namespace opensc4::autotest {
namespace {

// The first stage's lot configuration of a port developer's type `index`
// (the port base is at +0x4 of the developer: its types at +0x48, a type's
// stages at +0x14, a stage's configuration ID at +0x4); 0 when there is
// none.
uint32_t FirstStage(void* developer, uint32_t index)
{
    char* const base = static_cast<char*>(developer) + 4;
    char** const begin = At<char**>(base, 0x48);
    char** const end = At<char**>(base, 0x4C);
    if (!begin || index >= static_cast<uint32_t>(end - begin) || !begin[index])
        return 0;
    if (At<uint32_t>(begin[index], 0xC) == 0 || !At<char*>(begin[index], 0x14))
        return 0;
    return At<uint32_t>(At<char*>(begin[index], 0x14), 0x4);
}

// Makes a lot of the configuration at the first place the lot manager
// accepts, the way the port developers replace a port's lot when it grows
// (0x006E92A0): the lot manager's slot 23 makes the lot, the lot takes the
// configuration (slots 10, 32) and the lot developer builds it (slot 7).
// The lot manager's slot 20 is asked first (zone not needed, with the
// configuration); `accept` is the answer that means the lot can be made.
bool BuildLot(const char* what, uint32_t configurationId, int32_t accept, int32_t cellsX, int32_t cellsZ,
              const uint8_t* sea)
{
    void* const configurations = Global<void*>(addr::SC4_Service43D18);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const development = Global<void*>(addr::SC4_Service43D68);
    if (!configurations || !lots || !development || !configurationId)
        return false;
    void* const configuration = Virtual<void*>(configurations, 0x20, configurationId);
    if (!configuration) {
        log::Write("autotest: ports: no configuration %08X for the %s", configurationId, what);
        return false;
    }
    uint32_t width = 0, depth = 0;
    Virtual<uint32_t>(configuration, 0x1C, &width, &depth);
    width &= 0xFF;
    depth &= 0xFF;
    uint32_t seen[16] = {};
    for (int32_t facing = 0; facing < 4; ++facing) {
        const uint32_t w = (facing & 1) ? depth : width;
        const uint32_t d = (facing & 1) ? width : depth;
        for (int32_t z = 2; z + static_cast<int32_t>(d) < cellsZ - 2; ++z)
            for (int32_t x = 2; x + static_cast<int32_t>(w) < cellsX - 2; ++x) {
                const int32_t verdict = Virtual<int32_t>(lots, 0x50, x, z, w, d, 1u, 0u, configuration);
                if (verdict >= 0 && verdict < 16)
                    ++seen[verdict];
                if (verdict != accept)
                    continue;
                if (sea) {
                    // A cell next to the lot must be water an edge of the
                    // map reaches (the seaport's sea access map).
                    bool coast = false;
                    const auto Sea = [&](int32_t cx, int32_t cz) {
                        const uint8_t cell = sea[cz * cellsX + cx];
                        return (cell & 0x80) != 0 && (cell & 0x0F) != 0;
                    };
                    for (uint32_t i = 0; i < w && !coast; ++i)
                        coast = Sea(x + static_cast<int32_t>(i), z - 1) ||
                                Sea(x + static_cast<int32_t>(i), z + static_cast<int32_t>(d));
                    for (uint32_t i = 0; i < d && !coast; ++i)
                        coast = Sea(x - 1, z + static_cast<int32_t>(i)) ||
                                Sea(x + static_cast<int32_t>(w), z + static_cast<int32_t>(i));
                    if (!coast)
                        continue;
                }
                void* created = nullptr;
                if (!Virtual<uint8_t>(lots, 0x5C, x, z, w, d, facing, &created) || !created)
                    continue;
                Virtual<uint32_t>(created, 0x28, configuration);
                Virtual<uint32_t>(created, 0x80);
                void* const developer = Virtual<void*>(development, 0x90);
                Virtual<uint32_t>(developer, 0x4);
                const uint8_t built = Virtual<uint8_t>(developer, 0x1C, created, configuration, 0u, 1u);
                Virtual<uint32_t>(developer, 0x8);
                log::Write("autotest: ports: %s lot %ux%u (configuration %08X) at %d,%d facing %d: built %u", what,
                           w, d, configurationId, x, z, facing, built);
                return built != 0;
            }
    }
    log::Write("autotest: ports: no place for the %s (%ux%u, configuration %08X); verdicts 0:%u 1:%u 2:%u 3:%u "
               "4:%u 5:%u 6:%u 7:%u 8:%u 9:%u",
               what, width, depth, configurationId, seen[0], seen[1], seen[2], seen[3], seen[4], seen[5], seen[6],
               seen[7], seen[8], seen[9]);
    return false;
}

// OPENSC4_TEST_PLOP_PORTS=<n>: on the n-th day of a run, an airport and a
// seaport of the developers' first types (OPENSC4_TEST_PLOP_PORT_TYPE picks
// another type index; OPENSC4_TEST_PLOP_ACCEPT the lot manager's answer to
// take, 8 by default: the lot manager answers 7 or 8 where a lot can go).
// The seaport goes next to water that an edge of the map reaches.
void Plop(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PLOP_PORTS") || !game::IsBuild641())
        return;
    const uint32_t type = static_cast<uint32_t>(EnvInt("OPENSC4_TEST_PLOP_PORT_TYPE", 0));
    const int32_t accept = EnvInt("OPENSC4_TEST_PLOP_ACCEPT", 8);
    void* const seaport = Virtual<void*>(day.city, 0x130);
    if (!seaport)
        return;
    // The city's size in cells, from the seaport developer's sea access map.
    const int32_t cellsX = At<int32_t>(seaport, 0x74), cellsZ = At<int32_t>(seaport, 0x78);
    if (void* const airport = Virtual<void*>(day.city, 0x134))
        BuildLot("airport", FirstStage(airport, type), accept, cellsX, cellsZ, nullptr);
    BuildLot("seaport", FirstStage(seaport, type), accept, cellsX, cellsZ, At<const uint8_t*>(seaport, 0x68));
}

const DayHook kPlop{"ports.plop", 60, &Plop};

// A message to a port developer's message target (+0x8), with data 2 and
// the object the message carries.
void Send(void* developer, uint32_t type, uint32_t data2, void* object)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
    using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    cIGZMessage2Standard* const message =
        memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (!message)
        return;
    message->AddRef();
    message->SetType(type);
    message->SetData2(static_cast<intptr_t>(data2));
    if (object)
        message->SetIGZUnknown(static_cast<cIGZUnknown*>(object));
    void* const target = static_cast<char*>(developer) + 0x8;
    reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3])(target, message);
    message->Release();
}

// The first port of a developer (the port base's list at +0x44 of the
// base, the base at +0x4 of the developer), or null.
char* FirstPort(void* developer)
{
    char* const head = At<char*>(static_cast<char*>(developer) + 4, 0x44);
    char* const first = At<char*>(head, 0x0);
    return first == head ? nullptr : At<char*>(first, 0x8);
}

// OPENSC4_TEST_GROW_PORTS=<n>: on the n-th day of a run every airport gets
// its stage's capacity as its usage (+0x24, which the airport's usage
// update otherwise counts from traffic), and both developers get the growth
// message (0x8A394DC4): a port at 96% of its capacity with a further stage
// has its lot replaced by the next stage's.
void Grow(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_GROW_PORTS") || !game::IsBuild641())
        return;
    if (void* const airport = Virtual<void*>(day.city, 0x134)) {
        char* const head = At<char*>(static_cast<char*>(airport) + 4, 0x44);
        int ports = 0;
        for (char* node = At<char*>(head, 0x0); node != head; node = At<char*>(node, 0x0), ++ports) {
            char* const port = At<char*>(node, 0x8);
            char* const stage = At<char*>(At<char*>(port, 0x8), 0x14) + At<uint32_t>(port, 0x4) * 0x20;
            At<uint32_t>(port, 0x24) = At<uint32_t>(stage, 0x10);
        }
        Send(airport, 0x8A394DC4, 0, nullptr);
        char* const first = FirstPort(airport);
        log::Write("autotest: ports: growth asked of %d airport(s); the first is at stage %d of %u now", ports,
                   first ? At<int32_t>(first, 0x4) : -1, first ? At<uint32_t>(At<char*>(first, 0x8), 0xC) : 0u);
    }
    if (void* const seaport = Virtual<void*>(day.city, 0x130))
        Send(seaport, 0x8A394DC4, 0, nullptr);
}

const DayHook kGrow{"ports.grow", 61, &Grow};

// OPENSC4_TEST_REMOVE_PORTS=<n>: on the n-th day of a run each developer
// gets the lot message (0xE8BDA2E7, data 2 = 5) for its first port's lot,
// as when the lot is taken down: the port leaves the demand and the list
// and its ground is dezoned. The lot itself stays.
void Remove(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_REMOVE_PORTS") || !game::IsBuild641())
        return;
    for (const std::size_t slot : {std::size_t{0x134}, std::size_t{0x130}}) {
        void* const developer = Virtual<void*>(day.city, slot);
        char* const port = developer ? FirstPort(developer) : nullptr;
        void* const lot = port ? At<void*>(port, 0x20) : nullptr;
        if (!lot)
            continue;
        Send(developer, 0xE8BDA2E7, 5, lot);
        log::Write("autotest: ports: lot message for the first %s; %s", slot == 0x134 ? "airport" : "seaport",
                   FirstPort(developer) == port ? "the port stays" : "the port is gone");
    }
}

const DayHook kRemove{"ports.remove", 62, &Remove};

} // namespace
} // namespace opensc4::autotest

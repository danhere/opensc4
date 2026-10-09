// What every part of our cSC4TrafficNetworkMap shares (docs/traffic-map.md):
// first-call logging and the vtable swap.
#include "core/trafficmap.h"

#include "core/log.h"

#include <windows.h>

namespace opensc4::trafficmap {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("trafficmap: first %s", name);
}

bool PatchSlots(const char* group, std::uintptr_t vtable, int size, const Slot* slots, std::size_t count)
{
    auto* const table = reinterpret_cast<std::uintptr_t*>(vtable);
    for (std::size_t i = 0; i < count; ++i) {
        if (slots[i].index >= size || table[slots[i].index] != slots[i].original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", group, slots[i].index,
                       static_cast<unsigned>(vtable), static_cast<unsigned>(table[slots[i].index]),
                       static_cast<unsigned>(slots[i].original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(table, size * sizeof(*table), PAGE_READWRITE, &old))
        return false;
    for (std::size_t i = 0; i < count; ++i)
        table[slots[i].index] = reinterpret_cast<std::uintptr_t>(slots[i].ours);
    VirtualProtect(table, size * sizeof(*table), old, &old);
    log::Write("%s: %u of %d slots of %08X are ours", group, static_cast<unsigned>(count), size,
               static_cast<unsigned>(vtable));
    return true;
}

} // namespace opensc4::trafficmap

// The zone developer's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/zone_sim.h"

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

namespace field = zone_sim::field;

struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(double v)
    {
        const float f = static_cast<float>(v);
        uint32_t bits = 0;
        std::memcpy(&bits, &f, 4);
        Mix(bits);
    }
};

// A lot as numbers two runs can agree on: its own cell (lot slot 21), or
// -1 for none.
uint32_t LotCell(void* lot)
{
    if (!lot)
        return 0xFFFFFFFF;
    int32_t x = 0, z = 0;
    Virtual<void>(lot, 0x54, &x, &z);
    return (static_cast<uint32_t>(x) & 0xFFFF) | (static_cast<uint32_t>(z) << 16);
}

// The pure queries of the lot manager and the zone developer on a lattice
// of cells (OPENSC4_TEST_ZONE=1): the lot of a cell (slot 6, both ways), of
// a position (slot 5), the lots in rectangles (slot 7) and beside lots
// (slot 9, which goes through slot 10), the side lines and rows (slots 11,
// 12), the altitudes (slots 17, 18, 19) and limits (slot 26), CanCreateLot
// (slot 20) for 2 x 2 cells, and the developer's ExistsNetworkOfType (slot
// 8, mask 0x1F4F) and IsCellWater (slot 9). Not the random lot (slot 13: it
// draws from a generator) nor the iterator (slot 15: it allocates).
uint32_t Queries(char* self)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using FreeFn = void(__cdecl*)(void* block);
    const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
    const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
    char* const lots = self + zone_sim::kLotManagerPart;
    const int32_t cellsX = At<int32_t>(self, field::kCellsX), cellsZ = At<int32_t>(self, field::kCellsZ);
    Hash hash;
    const auto NewList = [&] {
        auto* const head = static_cast<ListNode*>(Allocate(sizeof(ListNode)));
        head->next = head;
        head->prev = head;
        return head;
    };
    // The list's lots mixed in, released and freed.
    const auto EndList = [&](ListNode* head) {
        for (ListNode* node = head->next; node != head;) {
            ListNode* const current = node;
            node = node->next;
            hash.Mix(LotCell(current->value));
            if (current->value)
                Virtual<uint32_t>(current->value, 0x8);
            Free(current);
        }
        Free(head);
    };
    for (int32_t z = 1; z < cellsZ; z += 5)
        for (int32_t x = 1; x < cellsX; x += 5) {
            void* const lot = Virtual<void*>(lots, 0x18, x, z, 0u);
            hash.Mix(LotCell(lot));
            hash.Mix(Virtual<void*>(lots, 0x18, x, z, 1u) != nullptr);
            const float position[3] = {x * 16.0f + 3.0f, 0.0f, z * 16.0f + 11.0f};
            hash.Mix(LotCell(Virtual<void*>(lots, 0x14, position)));
            uint8_t found = 2;
            hash.MixFloat(Virtual<double>(lots, 0x44, x, z, static_cast<void*>(nullptr), &found));
            hash.Mix(found);
            hash.MixFloat(Virtual<double>(lots, 0x48, position[0], position[2], &found));
            hash.Mix(found);
            hash.MixFloat(Virtual<double>(lots, 0x4C, x, z, &found));
            hash.Mix(found);
            uint32_t a = 0, b = 0;
            Virtual<void>(lots, 0x68, x, z, &a, &b);
            hash.Mix(a);
            hash.Mix(b);
            if (x + 2 < cellsX && z + 2 < cellsZ)
                hash.Mix(Virtual<uint32_t>(lots, 0x50, x, z, 2u, 2u, 0u, 0u, 0u));
            hash.Mix(Virtual<uint8_t>(self, 0x20, x, z, 0x1F4Fu));
            hash.Mix(Virtual<uint8_t>(self, 0x24, x, z));
            if (lot && (x + z) % 3 == 0) {
                ListNode* list = NewList();
                hash.Mix(Virtual<uint32_t>(lots, 0x24, lot, &list, 0xFu) != 0);
                EndList(list);
            }
        }
    for (int32_t z = 0; z < cellsZ; z += 16)
        for (int32_t x = 0; x < cellsX; x += 16) {
            ListNode* list = NewList();
            hash.Mix(Virtual<uint32_t>(lots, 0x1C, &list, x, z, x + 7, z + 7, static_cast<uint32_t>((x >> 4) & 1)));
            EndList(list);
            const int32_t rect[4] = {x, z, x + 7, z + 7};
            for (const uint32_t side : {1u, 2u, 4u, 8u}) {
                int32_t out[4] = {};
                Virtual<void>(lots, 0x2C, rect, side, out);
                for (const int32_t v : out)
                    hash.Mix(static_cast<uint32_t>(v));
                hash.Mix(Virtual<uint8_t>(lots, 0x30, rect, side, out));
                for (const int32_t v : out)
                    hash.Mix(static_cast<uint32_t>(v));
            }
        }
    hash.Mix(Virtual<uint32_t>(lots, 0x40));
    return hash.h;
}

// The zone developer with its lot manager (build 641; src/core/zone_sim.h),
// from the city's slot 75: the lot count (+0x68), a checksum of the lot
// list in its order (each lot's cell, zone type, state, flags and facing),
// a checksum of the grid of lots by cell (each cell's lot as that lot's own
// cell), the sizes of the maps +0x78, +0xD8, +0xE4 and +0xF0, the value
// +0xD4, the operation's zone type +0xA0, the lot sizes of the last
// parcellization (+0x84 to +0x94), the option bytes +0x98 to +0x9C and a
// checksum of the grid +0x74. OPENSC4_TEST_ZONE=1 adds a checksum of the
// pure queries (Queries above).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | zone none";
        return;
    }
    char* const self = Virtual<char*>(city, 0x12C);
    if (!self || !At<uint8_t>(self, field::kInitialized)) {
        line += " | zone none";
        return;
    }
    Hash list;
    uint32_t listed = 0;
    void** const head = At<void**>(self, field::kLotList);
    for (void** node = static_cast<void**>(head[0]); node != head && listed < 0x100000;
         node = static_cast<void**>(node[0]), ++listed) {
        void* const lot = node[2];
        list.Mix(LotCell(lot));
        if (!lot)
            continue;
        list.Mix(Virtual<uint32_t>(lot, 0x7C));
        list.Mix(Virtual<uint32_t>(lot, 0x84));
        list.Mix(Virtual<uint32_t>(lot, 0x30) & 0xFF);
        list.Mix(Virtual<uint32_t>(lot, 0x5C));
    }
    Hash grid;
    uint32_t occupied = 0;
    char* const columns = At<char*>(self, field::kLotGrid);
    const int32_t count = static_cast<int32_t>((At<char*>(self, field::kLotGrid + 4) - columns) / 12);
    for (int32_t x = 0; x < count; ++x) {
        void** const begin = At<void**>(columns + x * 12, 0x0);
        void** const end = At<void**>(columns + x * 12, 0x4);
        for (void** lot = begin; lot != end; ++lot) {
            grid.Mix(LotCell(*lot));
            occupied += *lot != nullptr;
        }
    }
    char text[256];
    std::snprintf(text, sizeof text,
                  " | zone %u,%u,%08X,%u,%08X,%u,%u,%u,%u,%X,%u,%d,%d,%d,%d,%d,%02X%02X%02X%02X%02X,%08X",
                  At<uint32_t>(self, field::kLotCount), listed, list.h, occupied, grid.h,
                  At<uint32_t>(self, field::kTree78 + 4), At<uint32_t>(self, field::kCellsD8 + 4),
                  At<uint32_t>(self, field::kCellsE4 + 4), At<uint32_t>(self, field::kMapF0 + 4),
                  At<uint32_t>(self, field::kValueD4), At<uint32_t>(self, field::kZoneType),
                  At<int32_t>(self, 0x84), At<int32_t>(self, 0x88), At<int32_t>(self, 0x8C), At<int32_t>(self, 0x90),
                  At<int32_t>(self, 0x94), At<uint8_t>(self, 0x98), At<uint8_t>(self, 0x99), At<uint8_t>(self, 0x9A),
                  At<uint8_t>(self, 0x9B), At<uint8_t>(self, 0x9C),
                  GridChecksum(At<cISC4SimGrid<uint16_t>*>(self, field::kGrid)));
    line += text;
    if (EnvInt("OPENSC4_TEST_ZONE", 0) == 1) {
        char hash[16];
        std::snprintf(hash, sizeof hash, ",q%08X", Queries(self));
        line += hash;
    }
}

const FingerprintPart kFingerprint{"zone", 98, &Fingerprint};

} // namespace
} // namespace opensc4::autotest

// The prop developer's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void Bytes(const void* data, std::size_t size)
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ p[i]) * 16777619u;
    }
};

uint32_t ListSize(char* self, std::size_t offset)
{
    Node* const head = At<Node*>(self, offset);
    uint32_t n = 0;
    for (Node* node = head->next; node != head && n < 1000000; node = node->next)
        ++n;
    return n;
}

uint32_t VectorSize(char* self, std::size_t offset)
{
    return static_cast<uint32_t>((At<char*>(self, offset + 4) - At<char*>(self, offset)) >> 2);
}

// The prop developer (build 641; src/core/prop_object.cpp, prop_sim.h),
// from the city's slot 41: the initialized flag, the optional props'
// state and the requester flag (+0xC, +0x14, +0xA00), the optional props' cell iterator (state +0x18,
// cell +0x1C, +0x20), the counts of requesters (+0xA04) and timed props
// (+0xA20), the night flag (+0xA3C), the table's count (+0xA58) with a sum
// over its entries (key and list length; the order does not count), the
// lengths of the lists +0xA5C, +0xA60, +0xA64 and of the signs +0xA6C, the
// signs flag (+0xA68) and a checksum of the wealth requester's static
// generator 0x00B4A5C0. The day and night stepping (+0xA2C, +0xA30, +0xA38)
// and the developer's own generator +0x38 (0x9C8 bytes) follow frames, not
// game days (the original's generator differs between two runs from the
// first day on), and are left out: the g field is 0.
// OPENSC4_TEST_PROP=1 adds a checksum of queries no unattended run makes:
// AreSignsAndLabelsVisible (slot 10), each requester's type (slot 4) and,
// for the wealth requester (type 0x49A1E995), its slots 3 and 5 and its
// choice (slot 6) from an empty list of candidates at five cells (0: no
// draw is made). OPENSC4_TEST_PROP=2 adds the frame-driven state too: the
// generator's checksum (g), the three stepping fields and the count of timed props that are on (their
// slot 12).
void Fingerprint(cISC4City* city, std::string& line)
{
    char* const self = game::IsBuild641() ? Virtual<char*>(city, 0xA4) : nullptr;
    if (!self) {
        line += " | prop none";
        return;
    }
    char* const table = self + 0xA48;
    uint32_t entries = 0;
    {
        char** const buckets = At<char**>(table, 0x4);
        const uint32_t count = static_cast<uint32_t>((At<char*>(table, 0x8) - At<char*>(table, 0x4)) >> 2);
        for (uint32_t i = 0; i < count; ++i)
            for (char* node = buckets[i]; node; node = At<char*>(node, 0x0)) {
                Hash one;
                one.Mix(At<uint32_t>(node, 0x4));
                Node* const head = At<Node*>(node, 0x8);
                uint32_t n = 0;
                for (Node* item = head->next; item != head && n < 1000000; item = item->next)
                    ++n;
                one.Mix(n);
                entries += one.h;
            }
    }
    const int test = EnvInt("OPENSC4_TEST_PROP", 0);
    Hash random;
    random.Bytes(self + 0x38, 0x9C8);
    Hash wealth;
    wealth.Bytes(reinterpret_cast<const void*>(addr::cSC4WealthRequester_Random), 0x9C8);
    char text[256];
    std::snprintf(text, sizeof text, " | prop %u%u%u it%d,%d,%d r%u t%u n%u tb%u:%08X l%u,%u,%u s%u:%u g%08X w%08X",
                  At<uint8_t>(self, 0xC), At<uint8_t>(self, 0x14), At<uint8_t>(self, 0xA00),
                  At<int8_t>(self, 0x18), At<int32_t>(self, 0x1C), At<int32_t>(self, 0x20), VectorSize(self, 0xA04),
                  VectorSize(self, 0xA20), At<uint8_t>(self, 0xA3C), At<uint32_t>(table, 0x10), entries,
                  ListSize(self, 0xA5C), ListSize(self, 0xA60), ListSize(self, 0xA64), At<uint8_t>(self, 0xA68),
                  ListSize(self, 0xA6C), test >= 2 ? random.h : 0u, wealth.h);
    line += text;
    if (test < 1)
        return;
    Hash q;
    q.Mix(Virtual<uint8_t>(self, 0x28));
    void** const requesters = At<void**>(self, 0xA04);
    for (uint32_t i = 0, n = VectorSize(self, 0xA04); i < n; ++i) {
        void* const requester = requesters[i];
        const uint32_t type = Virtual<uint32_t>(requester, 0x10);
        q.Mix(type);
        if (type != 0x49A1E995)
            continue;
        q.Mix(Virtual<uint32_t>(requester, 0xC, 0u));
        q.Mix(Virtual<uint8_t>(requester, 0x14));
        // An empty vector of candidates: plain data of ours the requester
        // only reads.
        char* empty[3] = {nullptr, nullptr, nullptr};
        static const int32_t cells[5][2] = {{4, 4}, {20, 9}, {33, 40}, {50, 50}, {60, 12}};
        for (const auto& cell : cells)
            q.Mix(Virtual<uint32_t>(requester, 0x18, static_cast<void*>(empty), cell[0], cell[1]));
    }
    std::snprintf(text, sizeof text, " q%08X", q.h);
    line += text;
    if (test < 2)
        return;
    uint32_t on = 0;
    void** const timed = At<void**>(self, 0xA20);
    for (uint32_t i = 0, n = VectorSize(self, 0xA20); i < n; ++i)
        if (timed[i] && Virtual<uint32_t>(timed[i], 0x30) == 1)
            ++on;
    std::snprintf(text, sizeof text, " f%u,%u,%u,%u", At<uint32_t>(self, 0xA2C), At<uint32_t>(self, 0xA30),
                  At<uint32_t>(self, 0xA38), on);
    line += text;
}

const FingerprintPart kFingerprint{"prop", 96, &Fingerprint};

} // namespace
} // namespace opensc4::autotest

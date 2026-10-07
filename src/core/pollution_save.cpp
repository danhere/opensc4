// Our cSC4PollutionSimulator, fifth part: saving (docs/save-format.md,
// "cSC4PollutionSimulator"; docs/pollution.md).
//
// Write is cIGZSerializable slot 3; `this` is the object's serializable
// part at +0xC. The record is written field by field in the game's order
// and types; every parity run ends with a save whose pollution record is
// decoded and compared, so this is checked on all of the corpus.
#include "core/pollution_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::pollution_sim {
namespace {

constexpr std::size_t kSerializable = 0xC; // the cIGZSerializable part

using WriteObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
const auto WriteReference = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReference);
const auto WriteReferenceList = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WriteReferenceList);
const auto WriteIterator = reinterpret_cast<WriteObjectFn>(addr::SC4Stream_WritePollutionStruct);

// cIGZOStream, as the game calls it.
struct Out {
    void* stream;
    void Uint8(uint8_t v) const { Virtual<uint8_t>(stream, 0x14, v); }
    void Uint16(uint16_t v) const { Virtual<uint8_t>(stream, 0x1C, v); }
    void Sint32(int32_t v) const { Virtual<uint8_t>(stream, 0x20, v); }
    void Uint32(uint32_t v) const { Virtual<uint8_t>(stream, 0x24, v); }
    void Sint64(int64_t v) const { Virtual<uint8_t>(stream, 0x28, v); }
    void Float32(uint32_t bits) const { Virtual<uint8_t>(stream, 0x30, bits); } // the float's bits, as the game pushes them
    void Float64(double v) const { Virtual<uint8_t>(stream, 0x34, v); }
};

uint8_t __fastcall Write(void* serializable, void*, void* stream)
{
    char* const self = static_cast<char*>(serializable) - kSerializable;
    const Out out{stream};
    auto u32 = [self](std::size_t offset) { return At<uint32_t>(self, offset); };
    auto i32 = [self](std::size_t offset) { return At<int32_t>(self, offset); };
    auto i64 = [self](std::size_t offset) { return At<int64_t>(self, offset); };

    out.Uint16(4); // version
    for (std::size_t grid = 0; grid < 4; ++grid)
        WriteReference(stream, At<void*>(self, 0x40 + grid * 4));
    for (std::size_t grid = 0; grid < 4; ++grid)
        WriteReference(stream, At<void*>(self, 0x50 + grid * 4));
    out.Uint32(u32(0x60));
    out.Uint32(u32(0x64));
    for (std::size_t list = 0; list < 4; ++list)
        WriteReferenceList(stream, self + 0x68 + list * 4);
    out.Uint32(u32(0xFC));
    out.Uint32(u32(0x100));
    out.Uint32(u32(0x104));
    for (std::size_t offset = 0x108; offset <= 0x11C; offset += 4)
        out.Sint32(i32(offset));
    out.Uint32(u32(0x120));
    out.Uint16(At<uint16_t>(self, 0x128));
    out.Uint32(u32(0x12C));
    out.Uint32(u32(0x130));
    out.Float32(u32(0x134));
    out.Uint8(At<uint8_t>(self, 0x138));
    out.Uint32(u32(0x13C));
    out.Sint64(i64(0x1C0));
    out.Sint64(i64(0x1C8));
    out.Uint32(u32(0x1B8));
    // Last month's garbage figures, then this month's.
    for (const std::size_t base : {std::size_t{0x178}, std::size_t{0x140}}) {
        out.Sint64(i64(base));
        out.Sint64(i64(base + 8));
        out.Sint64(i64(base + 0x10));
        out.Float64(At<double>(self, base + 0x18));
        out.Sint64(i64(base + 0x20));
        out.Sint64(i64(base + 0x28));
        out.Sint64(i64(base + 0x30));
    }
    out.Uint32(u32(0x1B4));
    out.Sint32(i32(0x1B0));
    out.Uint32(u32(0x1BC));
    out.Uint32(u32(0x1D4));
    WriteIterator(stream, self + 0x1D8);
    for (std::size_t flag = 0x208; flag <= 0x20B; ++flag)
        out.Uint8(At<uint8_t>(self, flag));
    out.Float32(u32(0x228));
    out.Sint32(i32(0x22C));
    out.Sint32(i32(0x230));
    WriteReference(stream, At<void*>(self, 0xD8));
    return 1;
}

using WriteFn = uint8_t(__fastcall*)(void*, void*, void*);
WriteFn g_originalWrite = nullptr;

bool Install(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_Write);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &Write, &g_originalWrite);
}

const bool g_registered = registry::Register({
    "simulators.pollution.save",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_Write,
    "Our pollution simulator save (cIGZSerializable::Write) (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim

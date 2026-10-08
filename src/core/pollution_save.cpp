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

// --- Read ------------------------------------------------------------------

using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void** object, uint32_t flags);
using ReadObjectFn = uint8_t(__cdecl*)(void* stream, void* object);
using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto ReadReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
const auto ReadReferenceList = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadReferenceList);
const auto ReadIterator = reinterpret_cast<ReadObjectFn>(addr::SC4Stream_ReadPollutionStruct);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

constexpr uint32_t kGridInterface = 0xC9BD939A;       // cISC4SimGrid<short>
constexpr uint32_t kDepartmentInterface = 0x223E2B67; // the department budget at +0xD8

// cIGZIStream, as the game calls it; every result is ignored, as the game does.
struct In {
    void* stream;
    void Uint8(std::size_t offset, char* self) const { Virtual<uint8_t>(stream, 0x14, self + offset); }
    void Uint16(void* to) const { Virtual<uint8_t>(stream, 0x1C, to); }
    void Sint32(std::size_t offset, char* self) const { Virtual<uint8_t>(stream, 0x20, self + offset); }
    void Uint32(void* to) const { Virtual<uint8_t>(stream, 0x24, to); }
    void Sint64(std::size_t offset, char* self) const { Virtual<uint8_t>(stream, 0x28, self + offset); }
    void Float32(void* to) const { Virtual<uint8_t>(stream, 0x30, to); }
    void Float64(std::size_t offset, char* self) const { Virtual<uint8_t>(stream, 0x34, self + offset); }
};

// The game's cIGZString over an STL-style buffer, as Read builds it on the
// stack for a string it drops.
struct DroppedString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

uint8_t __fastcall Read(void* serializable, void*, void* stream)
{
    char* const self = static_cast<char*>(serializable) - kSerializable;
    const In in{stream};
    auto u32 = [self](std::size_t offset) { return self + offset; };
    uint16_t version = 0;
    in.Uint16(&version);
    if (version > 4)
        return 0;
    for (std::size_t grid = 0; grid < 4; ++grid)
        ReadReference(stream, kGridInterface, reinterpret_cast<void**>(self + 0x40 + grid * 4), 0);
    for (std::size_t grid = 0; grid < 4; ++grid)
        ReadReference(stream, kGridInterface, reinterpret_cast<void**>(self + 0x50 + grid * 4), 0);
    in.Uint32(u32(0x60));
    in.Uint32(u32(0x64));
    for (std::size_t list = 0; list < 4; ++list)
        ReadReferenceList(stream, self + 0x68 + list * 4);
    in.Uint32(u32(0xFC));
    in.Uint32(u32(0x100));
    in.Uint32(u32(0x104));
    for (std::size_t offset = 0x108; offset <= 0x11C; offset += 4)
        in.Sint32(offset, self);
    in.Uint32(u32(0x120));
    if (version < 3) {
        DroppedString dropped{};
        char* const buffer = static_cast<char*>(Allocate(8));
        dropped.begin = buffer;
        dropped.end = buffer;
        dropped.capacity = buffer + 8;
        buffer[0] = 0;
        dropped.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        Virtual<uint8_t>(stream, 0x3C, &dropped); // GetGZStr
        if (dropped.begin)
            Free(dropped.begin);
    }
    in.Uint16(u32(0x128));
    in.Uint32(u32(0x12C));
    in.Uint32(u32(0x130));
    uint32_t bits = 0;
    in.Float32(&bits);
    At<uint32_t>(self, 0x134) = bits;
    in.Uint8(0x138, self);
    in.Uint32(u32(0x13C));
    in.Sint64(0x1C0, self);
    in.Sint64(0x1C8, self);
    if (version >= 4)
        in.Uint32(u32(0x1B8));
    // Last month's garbage figures, then this month's.
    for (const std::size_t base : {std::size_t{0x178}, std::size_t{0x140}}) {
        in.Sint64(base, self);
        in.Sint64(base + 8, self);
        in.Sint64(base + 0x10, self);
        in.Float64(base + 0x18, self);
        in.Sint64(base + 0x20, self);
        in.Sint64(base + 0x28, self);
        in.Sint64(base + 0x30, self);
    }
    in.Uint32(u32(0x1B4));
    in.Sint32(0x1B0, self);
    in.Uint32(u32(0x1BC));
    in.Uint32(u32(0x1D4));
    ReadIterator(stream, self + 0x1D8);
    for (std::size_t flag = 0x208; flag <= 0x20B; ++flag)
        in.Uint8(flag, self);
    if (version < 3)
        in.Uint32(&bits);
    in.Float32(&bits);
    At<uint32_t>(self, 0x228) = bits;
    in.Sint32(0x22C, self);
    in.Sint32(0x230, self);
    void*& department = At<void*>(self, 0xD8);
    if (void* const old = department) {
        department = nullptr;
        Virtual<uint32_t>(old, 0x8); // Release
    }
    ReadReference(stream, kDepartmentInterface, &department, 0);
    At<uint8_t>(self, 0x11) = 1;
    return 1;
}

using ReadFn = uint8_t(__fastcall*)(void*, void*, void*);
ReadFn g_originalRead = nullptr;

bool InstallRead(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_Read);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &Read, &g_originalRead);
}

const bool g_registeredRead = registry::Register({
    "simulators.pollution.load",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_Read,
    "Our pollution simulator load (cIGZSerializable::Read) (docs/pollution.md)",
    false,
    InstallRead,
});

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

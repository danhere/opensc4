// Our cSC4WeatherSimulator (build 641), fourth part: Init, Shutdown and the
// save (Write, Read), each a replacement on the original object
// (docs/weather.md).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/weather_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::weather_sim {
namespace {

using FreeFn = void(__cdecl*)(void* p);
using AllocateFn = void*(__cdecl*)(std::size_t size);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);

void* Service(std::uintptr_t global) { return Global<void*>(global); }

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallC(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// Rows of bytes: `count` rows, each `length` long, new bytes `fill`, as the
// game's Init does (the row vector resized, then each row cut or grown).
void SizeByteRows(void* self, std::size_t offset, uint32_t count, uint32_t length, uint8_t fill)
{
    Vector empty{};
    CallThis<void>(addr::SC4RowVector_Resize, static_cast<char*>(self) + offset, count, &empty);
    const int32_t n = static_cast<int32_t>((At<char*>(self, offset + 4) - At<char*>(self, offset)) / 12);
    for (int32_t i = 0; i < n; ++i) {
        Vector* const row = reinterpret_cast<Vector*>(At<char*>(self, offset)) + i;
        const uint32_t size = static_cast<uint32_t>(row->end - row->begin);
        if (length < size)
            row->end = row->begin + length;
        else
            CallThis<void>(addr::SC4ByteVector_InsertN, row, row->end, length - size, &fill);
    }
}

uint8_t __fastcall Init(void* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    char* const s = static_cast<char*>(self);
    if (At<uint8_t>(s, field::kInitialised))
        return 1;
    CallThis<void>(addr::cSC4WeatherSimulator_LoadTuning, s);
    if (void* const server = Service(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x14, s + 0x8, 0x26D31EC1u);
    if (!At<void*>(s, field::kCity)) {
        At<void*>(s, field::kCity) = Service(addr::SC4_City);
        Virtual<uint32_t>(At<void*>(s, field::kCity), 0x4);
    }
    if (!At<uint8_t>(s, field::kLoaded)) {
        void* const city = At<void*>(s, field::kCity);
        At<float>(s, field::kSizeX) = static_cast<float>(Virtual<double>(city, 0x164));
        const double sizeZ = Virtual<double>(city, 0x168);
        At<float>(s, field::kSizeZ) = static_cast<float>(sizeZ);
        At<int32_t>(s, field::kCellsX) = Ftol(static_cast<double>(At<float>(s, field::kSizeX)) * 0.0625);
        At<int32_t>(s, field::kCellsZ) = Ftol(sizeZ * 0.0625);
        At<uint32_t>(s, field::kCellWidthX) = 0x41800000; // 16.0f
        At<uint32_t>(s, field::kCellWidthZ) = 0x41800000;
        At<uint32_t>(s, field::kInverseWidthX) = 0x3D800000; // 0.0625f
        At<uint32_t>(s, field::kInverseWidthZ) = 0x3D800000;
        At<int32_t>(s, field::kCityCellsX) =
            Ftol(static_cast<double>(At<float>(s, field::kCellWidthX)) / Virtual<double>(city, 0x16C));
        At<int32_t>(s, field::kCityCellsZ) =
            Ftol(static_cast<double>(At<float>(s, field::kCellWidthZ)) / Virtual<double>(At<void*>(s, field::kCity), 0x170));
        const int32_t cellsX = At<int32_t>(s, field::kCellsX);
        At<int32_t>(s, field::kCellsPerTick) = (At<int32_t>(s, field::kCellsZ) * cellsX) / 8;
        SizeByteRows(s, field::kCellMoisture, static_cast<uint32_t>(cellsX), At<uint32_t>(s, field::kCellsZ), 0x7F);
        SizeByteRows(s, field::kMoisture, At<uint32_t>(s, field::kCellsX), At<uint32_t>(s, field::kCellsZ), 0x40);
        {
            uint32_t emptyBits[5] = {};
            char* const begin = At<char*>(s, field::kWaterBits);
            const uint32_t n = static_cast<uint32_t>((At<char*>(s, field::kWaterBits + 4) - begin) / 20);
            const uint32_t want = At<uint32_t>(s, field::kCellsX);
            if (want < n)
                CallThis<void>(addr::SC4BitRowVector_Erase, s + field::kWaterBits, begin + 20 * want,
                               At<char*>(s, field::kWaterBits + 4));
            else
                CallThis<void>(addr::SC4BitRowVector_InsertN, s + field::kWaterBits, At<char*>(s, field::kWaterBits + 4),
                               want - n, emptyBits);
            const int32_t rows = static_cast<int32_t>((At<char*>(s, field::kWaterBits + 4) - At<char*>(s, field::kWaterBits)) / 20);
            for (int32_t i = 0; i < rows; ++i)
                CallThis<void>(addr::SC4BitRow_Resize, At<char*>(s, field::kWaterBits) + 20 * i,
                               At<uint32_t>(s, field::kCellsZ), 0u);
        }
        {
            Vector empty{};
            char* const begin = At<char*>(s, field::kManual);
            const uint32_t n = static_cast<uint32_t>((At<char*>(s, field::kManual + 4) - begin) / 12);
            const uint32_t want = At<uint32_t>(s, field::kCellsX);
            if (want < n)
                CallThis<void>(addr::SC4RowVector_Destroy, s + field::kManual, begin + 12 * want,
                               At<char*>(s, field::kManual + 4));
            else
                CallThis<void>(addr::SC4RowVector_InsertN, s + field::kManual, At<char*>(s, field::kManual + 4),
                               want - n, &empty);
            const int32_t rows = static_cast<int32_t>((At<char*>(s, field::kManual + 4) - At<char*>(s, field::kManual)) / 12);
            uint8_t zero = 0;
            for (int32_t i = 0; i < rows; ++i) {
                Vector* const row = reinterpret_cast<Vector*>(At<char*>(s, field::kManual)) + i;
                const uint32_t length = At<uint32_t>(s, field::kCellsZ);
                const uint32_t size = static_cast<uint32_t>(row->end - row->begin);
                if (length < size)
                    row->end = row->begin + length;
                else
                    CallThis<void>(addr::SC4ByteVector_InsertN, row, row->end, length - size, &zero);
            }
        }
        CallThis<void>(addr::cSC4WeatherSimulator_SeedCellMoisture, s);
    }
    if (!At<void*>(s, field::kTerrain)) {
        At<void*>(s, field::kTerrain) = Service(0x00B43CF4);
        Virtual<uint32_t>(At<void*>(s, field::kTerrain), 0x4);
    }
    if (!At<void*>(s, field::kSimulators)) {
        At<void*>(s, field::kSimulators) = Service(addr::SC4_SimulatorService);
        Virtual<uint32_t>(At<void*>(s, field::kSimulators), 0x4);
        struct {
            const void* vtable;
            char* begin;
            char* end;
            char* capacity;
            uint32_t zero;
        } name{};
        char* const text = static_cast<char*>(Allocate(0x15));
        name.begin = text;
        name.capacity = text + 0x15;
        std::memmove(text, reinterpret_cast<const char*>(addr::WeatherSimulator_Name), 0x14);
        name.end = text + 0x14;
        text[0x14] = 0;
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        name.zero = 0;
        for (const uint32_t phase : {6u, 5u, 4u})
            Virtual<uint32_t>(At<void*>(s, field::kSimulators), 0x4C, s + 0x8, phase, &name, 2u);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }
    char* const iterator = s + field::kTractIterator;
    At<int32_t>(iterator, 0x4) = 0;
    At<int32_t>(iterator, 0x8) = 0;
    At<int32_t>(iterator, 0xC) = At<int32_t>(s, field::kCellsX) - 1;
    At<int32_t>(iterator, 0x10) = At<int32_t>(s, field::kCellsZ) - 1;
    if (At<int32_t>(iterator, 0xC) < At<int32_t>(iterator, 0x4))
        At<int32_t>(iterator, 0xC) = At<int32_t>(iterator, 0x4);
    if (At<int32_t>(iterator, 0x10) < At<int32_t>(iterator, 0x8))
        At<int32_t>(iterator, 0x10) = At<int32_t>(iterator, 0x8);
    At<uint8_t>(iterator, 0x2) = 1;
    if (At<uint8_t>(iterator, 0x1) != 1) {
        At<uint8_t>(iterator, 0x1) = 1;
        At<uint8_t>(iterator, 0x2) = 1;
    }
    CallThis<void>(addr::TractIterator_Restart, iterator, 1u);
    const int32_t perTick = (At<int32_t>(s, field::kCellsZ) * At<int32_t>(s, field::kCellsX)) /
                            At<int32_t>(s, field::kTickDivisor);
    At<uint8_t>(s, field::kInitialised) = 1;
    At<uint32_t>(s, field::kHumidity) = At<uint32_t>(s, field::kHumidityBase);
    At<uint32_t>(s, field::kTemperature) = At<uint32_t>(s, field::kBaseTemperature);
    At<int32_t>(s, field::kCellsPerTick) = perTick;
    return 1;
}

uint8_t __fastcall Shutdown(void* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    char* const s = static_cast<char*>(self);
    if (!At<uint8_t>(s, field::kInitialised))
        return 1;
    if (void*& terrain = At<void*>(s, field::kTerrain)) {
        Virtual<uint32_t>(terrain, 0x8);
        terrain = nullptr;
    }
    if (void*& simulators = At<void*>(s, field::kSimulators)) {
        Virtual<uint32_t>(simulators, 0x50, s + 0x8);
        Virtual<uint32_t>(At<void*>(s, field::kSimulators), 0x8);
        At<void*>(s, field::kSimulators) = nullptr;
    }
    if (void*& city = At<void*>(s, field::kCity)) {
        Virtual<uint32_t>(city, 0x8);
        city = nullptr;
    }
    if (void* const server = Service(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x18, s + 0x8, 0x26D31EC1u);
    At<uint8_t>(s, field::kInitialised) = 0;
    return 1;
}

// --- Saving: cIGZSerializable (vtable 0x00AB35D4) at +0xC -------------------------

uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const s = part - 0xC;
    auto u32 = [stream](uint32_t v) { Virtual<uint32_t>(stream, 0x20, v); };
    auto f32 = [stream](uint32_t bits) { Virtual<uint32_t>(stream, 0x30, bits); };
    auto bits = [s](std::size_t offset) { return At<uint32_t>(s, offset); };
    Virtual<uint32_t>(stream, 0x1C, uint32_t{2});
    u32(bits(0x2C));
    u32(bits(0x30));
    for (std::size_t offset = 0x34; offset <= 0x48; offset += 4)
        f32(bits(offset));
    for (std::size_t offset = 0x4C; offset <= 0x60; offset += 4)
        u32(bits(offset));
    for (std::size_t offset = 0x64; offset <= 0x70; offset += 4)
        f32(bits(offset));
    CallC<uint8_t>(addr::SC4Stream_WriteByteRows, stream, s + 0x74);
    CallC<uint8_t>(addr::SC4Stream_WriteBitRows, stream, s + 0x80);
    const uint32_t bodies = static_cast<uint32_t>((At<char*>(s, 0x90) - At<char*>(s, 0x8C)) >> 4);
    Virtual<uint32_t>(stream, 0x24, bodies);
    for (uint32_t i = 0; i < bodies; ++i)
        CallC<uint8_t>(addr::SC4Stream_WriteRect, stream, At<char*>(s, 0x8C) + 16 * i);
    CallC<uint8_t>(addr::SC4Stream_WritePollutionStruct, stream, s + 0x98);
    u32(bits(0xC8));
    u32(bits(0xCC));
    for (std::size_t offset = 0xD0; offset <= 0xD8; offset += 4)
        f32(bits(offset));
    CallC<uint8_t>(addr::SC4Stream_WriteByteRows, stream, s + 0xDC);
    CallC<uint8_t>(addr::SC4Stream_WriteByteRows, stream, s + 0xE8);
    for (std::size_t offset = 0xF4; offset <= 0x10C; offset += 4)
        f32(bits(offset));
    Virtual<uint32_t>(stream, 0x24, bits(0x110));
    for (std::size_t offset = 0x118; offset <= 0x13C; offset += 0xC) {
        f32(bits(offset));
        CallC<uint8_t>(addr::SC4Stream_WriteVector2, stream, s + offset + 4);
    }
    CallC<uint8_t>(addr::SC4Stream_WriteRandom, stream, s + 0x148);
    u32(bits(0x160));
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const s = part - 0xC;
    At<uint8_t>(s, field::kLoaded) = 1;
    uint16_t version = 0;
    Virtual<uint32_t>(stream, 0x1C, &version);
    if (version > 2)
        return 0;
    auto u32 = [stream, s](std::size_t offset) { Virtual<uint32_t>(stream, 0x20, &At<uint32_t>(s, offset)); };
    auto f32 = [stream, s](std::size_t offset) { Virtual<uint32_t>(stream, 0x30, &At<uint32_t>(s, offset)); };
    u32(0x2C);
    u32(0x30);
    for (std::size_t offset = 0x34; offset <= 0x48; offset += 4)
        f32(offset);
    for (std::size_t offset = 0x4C; offset <= 0x60; offset += 4)
        u32(offset);
    for (std::size_t offset = 0x64; offset <= 0x70; offset += 4)
        f32(offset);
    CallC<uint8_t>(addr::SC4Stream_ReadByteRows, stream, s + 0x74);
    CallC<uint8_t>(addr::SC4Stream_ReadBitRows, stream, s + 0x80);
    uint32_t bodies = 0;
    Virtual<uint32_t>(stream, 0x24, &bodies);
    CallThis<void>(addr::SC4RectVector_Resize, s + 0x8C, bodies);
    for (uint32_t i = 0; i < bodies; ++i)
        CallC<uint8_t>(addr::SC4Stream_ReadRect, stream, At<char*>(s, 0x8C) + 16 * i);
    CallC<uint8_t>(addr::SC4Stream_ReadPollutionStruct, stream, s + 0x98);
    u32(0xC8);
    u32(0xCC);
    for (std::size_t offset = 0xD0; offset <= 0xD8; offset += 4)
        f32(offset);
    CallC<uint8_t>(addr::SC4Stream_ReadByteRows, stream, s + 0xDC);
    CallC<uint8_t>(addr::SC4Stream_ReadByteRows, stream, s + 0xE8);
    for (std::size_t offset = 0xF4; offset <= 0x10C; offset += 4)
        f32(offset);
    Virtual<uint32_t>(stream, 0x24, &At<uint32_t>(s, 0x110));
    for (std::size_t offset = 0x118; offset <= 0x13C; offset += 0xC) {
        f32(offset);
        CallC<uint8_t>(addr::SC4Stream_ReadVector2, stream, s + offset + 4);
    }
    if (version <= 1) {
        // An older layout: rectangles of two vectors and two row maps, read
        // and dropped.
        uint32_t count = 0;
        Virtual<uint32_t>(stream, 0x24, &count);
        Vector rects{};
        CallThis<void>(addr::SC4RectVector_ResizeZeroed, &rects, count);
        for (uint32_t i = 0; i < count; ++i) {
            CallC<uint8_t>(addr::SC4Stream_ReadVector2, stream, rects.begin + 16 * i);
            CallC<uint8_t>(addr::SC4Stream_ReadVector2, stream, rects.begin + 16 * i + 8);
        }
        Vector rows{};
        CallC<uint8_t>(addr::SC4Stream_ReadByteRows, stream, &rows);
        CallThis<void>(addr::SC4RowVector_Destroy, &rows, rows.begin, rows.end);
        CallC<uint8_t>(addr::SC4Stream_ReadByteRows, stream, &rows);
        for (char* row = rows.begin; row != rows.end; row += 12)
            if (void* const data = *reinterpret_cast<void**>(row))
                Free(data);
        if (rows.begin)
            Free(rows.begin);
        if (rects.begin)
            Free(rects.begin);
    }
    CallC<uint8_t>(addr::SC4Stream_ReadRandom, stream, s + 0x148);
    u32(0x160);
    return Virtual<uint8_t>(stream, 0x48) == 0 ? 1 : 0;
}

void* g_original[4];

bool InstallAll(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4WeatherSimulator_Init, reinterpret_cast<void*>(&Init)},
        {addr::cSC4WeatherSimulator_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    };
    int i = 0;
    for (const auto& h : hooks) {
        const addr::Entry* entry = addr_check::Find(h.address);
        if (!entry || !hook::Install(*entry, h.ours, &g_original[i++]))
            return false;
    }
    return true;
}

bool InstallSave(registry::Mode)
{
    const addr::Entry* write = addr_check::Find(addr::cSC4WeatherSimulator_Write);
    const addr::Entry* read = addr_check::Find(addr::cSC4WeatherSimulator_Read);
    return game::IsBuild641() && write && read && hook::Install(*write, reinterpret_cast<void*>(&Write), &g_original[2]) &&
           hook::Install(*read, reinterpret_cast<void*>(&Read), &g_original[3]);
}

const bool g_registeredInit = registry::Register({
    "simulators.weather.init",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_Init,
    "Our weather Init and Shutdown (docs/weather.md)",
    false,
    InstallAll,
});

const bool g_registeredSave = registry::Register({
    "simulators.weather.save",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_Write,
    "Our weather Write and Read, version 2 (docs/weather.md)",
    false,
    InstallSave,
});

} // namespace
} // namespace opensc4::weather_sim

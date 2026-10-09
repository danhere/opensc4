// Our cSC4WeatherSimulator (build 641), second part: the moisture map
// rebuild (0x00726BF0), a replacement on the original object
// (docs/weather.md, "The moisture map").
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

uint32_t BitsOf(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

// A value rounded to an integer the game's way: through 1.5 * 2^23 in float.
int32_t RoundThroughFloat(double value)
{
    return static_cast<int32_t>(BitsOf(static_cast<float>(value + 12582912.0)) + 0xB4C00000u);
}

uint8_t* Row(void* self, std::size_t offset, int32_t x)
{
    return *reinterpret_cast<uint8_t**>(At<char*>(self, offset) + 12 * x);
}

template <typename T>
T* NewZeroed(std::size_t count)
{
    if (count == 0)
        return nullptr;
    auto* const p = static_cast<T*>(Allocate(count * sizeof(T)));
    std::memset(p, 0, count * sizeof(T));
    return p;
}

int32_t Max(int32_t a, int32_t b) { return a > b ? a : b; }
int32_t Min(int32_t a, int32_t b) { return a < b ? a : b; }

// 0x00726BF0: the moisture map over a rectangle of weather cells (and the
// cells around it it needs). Heights (A) are blurred [1 2 1] both ways into
// B and scaled; each cell's own moisture (C, from the manual map; 0xFF over
// water) gets, per city cell, the humidity less the slope towards the wind
// where the ground curves down and is higher than its neighbours, the
// humidity plus rain in hollows, the humidity elsewhere; C is then blurred
// [1 4 6 4 1] both ways into the map.
void __fastcall RebuildMoisture(void* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("RebuildMoisture", logged);
    void* const terrain = At<void*>(self, field::kTerrain);
    Virtual<int32_t>(terrain, 0x1C);
    Virtual<int32_t>(terrain, 0x20);
    char* const moistureRows = At<char*>(self, field::kMoisture);
    const int32_t rows = static_cast<int32_t>((At<char*>(self, field::kMoisture + 4) - moistureRows) / 12);
    const int32_t columns = static_cast<int32_t>(*reinterpret_cast<uint8_t**>(moistureRows + 4) -
                                                 *reinterpret_cast<uint8_t**>(moistureRows));
    const int32_t x0 = Max(rect[0], 0), z0 = Max(rect[1], 0);
    const int32_t x1 = Min(rect[2], rows), z1 = Min(rect[3], columns);
    const int32_t x2lo = Max(x0 - 2, 0), z2lo = Max(z0 - 2, 0);
    const int32_t x2hi = Min(x1 + 2, rows), z2hi = Min(z1 + 2, columns);
    const int32_t x4lo = Max(x0 - 4, 0), z4lo = Max(z0 - 4, 0);
    const int32_t x4hi = Min(x1 + 4, rows), z4hi = Min(z1 + 4, columns);
    const uint32_t cityCellsX = At<uint32_t>(self, field::kCityCellsX);
    const uint32_t cityCellsZ = At<uint32_t>(self, field::kCityCellsZ);
    const uint32_t cells = cityCellsZ * cityCellsX;
    const float inverse = static_cast<float>(1.0 / static_cast<double>(static_cast<int32_t>(cells)));
    const std::size_t count = static_cast<std::size_t>(columns * rows);
    float* const heights = NewZeroed<float>(count);   // A
    float* const blurred = NewZeroed<float>(count);   // B
    uint8_t* const own = NewZeroed<uint8_t>(count);   // C
    if (x4lo > 0 || z4lo > 0 || static_cast<uint32_t>(x4hi) < static_cast<uint32_t>(rows) ||
        static_cast<uint32_t>(z4hi) < static_cast<uint32_t>(columns))
        std::memset(heights, 0, count * sizeof(float));
    if (cells == 1) {
        for (uint32_t x = static_cast<uint32_t>(x4lo); x < static_cast<uint32_t>(x4hi); ++x) {
            int32_t cellRect[4] = {static_cast<int32_t>(x), z4lo, static_cast<int32_t>(x), z4hi - 1};
            Virtual<uint32_t>(terrain, 0x3C, cellRect, heights + (x * columns + z4lo), 1, 1);
        }
        for (uint32_t x = static_cast<uint32_t>(x2lo); x < static_cast<uint32_t>(x2hi); ++x)
            for (uint32_t z = static_cast<uint32_t>(z2lo); z < static_cast<uint32_t>(z2hi); ++z) {
                uint8_t value = Row(self, field::kManual, static_cast<int32_t>(x))[z];
                if (Virtual<uint8_t>(terrain, 0x88, x, z))
                    value = 0xFF;
                own[x * columns + z] = value;
            }
    } else {
        for (uint32_t x = static_cast<uint32_t>(x4lo); x < static_cast<uint32_t>(x4hi); ++x)
            for (uint32_t z = static_cast<uint32_t>(z4lo); z < static_cast<uint32_t>(z4hi); ++z) {
                uint8_t value = Row(self, field::kManual, static_cast<int32_t>(x))[z];
                float sum = 0.0f;
                for (uint32_t i = 0; i < At<uint32_t>(self, field::kCityCellsX); ++i)
                    for (uint32_t j = 0; j < At<uint32_t>(self, field::kCityCellsZ); ++j) {
                        const uint32_t cz = At<uint32_t>(self, field::kCityCellsZ) * z + j;
                        const uint32_t cx = At<uint32_t>(self, field::kCityCellsX) * x + i;
                        sum = static_cast<float>(Virtual<double>(terrain, 0x38, cx, cz) + sum);
                        if (Virtual<uint8_t>(terrain, 0x88, cx, cz))
                            value = 0xFF;
                    }
                heights[x * columns + z] = sum;
                Row(self, field::kMoisture, static_cast<int32_t>(x))[z] = value;
            }
    }
    // [1 2 1] along each row (A to B), then along each column (B to B,
    // scaled); a line buffer with reflected ends, kept between lines.
    const int32_t lineLength = Max(columns, rows) + 2;
    float* const line = NewZeroed<float>(static_cast<std::size_t>(lineLength));
    for (uint32_t x = static_cast<uint32_t>(x4lo); x < static_cast<uint32_t>(x4hi); ++x) {
        const float* const source = heights + x * columns;
        for (int32_t z = z4lo; z < z4hi; ++z)
            line[z + 1] = source[z];
        line[0] = line[2];
        line[rows + 1] = line[rows - 1];
        float* const target = blurred + x * columns;
        for (int32_t k = 0; k < columns; ++k)
            target[k] = static_cast<float>((static_cast<double>(line[k]) + line[k + 2]) +
                                           (static_cast<double>(line[k + 1]) + line[k + 1]));
    }
    const float scale = static_cast<float>(static_cast<double>(inverse) * 0.0625);
    for (uint32_t z = static_cast<uint32_t>(z4lo); z < static_cast<uint32_t>(z4hi); ++z) {
        for (int32_t x = x4lo; x < x4hi; ++x)
            line[x + 1] = blurred[x * columns + z];
        line[0] = line[2];
        line[rows + 1] = line[rows - 1];
        for (int32_t x = x4lo; x < x4hi; ++x)
            blurred[x * columns + z] = static_cast<float>(
                ((static_cast<double>(line[x]) + line[x + 2]) + (static_cast<double>(line[x + 1]) + line[x + 1])) *
                scale);
    }
    if (line)
        Free(line);
    // Each cell's own moisture.
    const int32_t humidity = RoundThroughFloat(static_cast<double>(At<float>(self, field::kHumidity)) * 255.0);
    const int32_t wetRaw =
        RoundThroughFloat(static_cast<double>(At<float>(self, field::kRainFactor)) * 255.0) + humidity;
    const int32_t wet = wetRaw > 0xFF ? 0xFF : (wetRaw > 0 ? wetRaw : 0);
    float wind[2];
    Virtual<double>(self, 0x58, wind);
    const float normalDefault = Global<float>(0x00B4B384);
    for (uint32_t x = static_cast<uint32_t>(x2lo); x < static_cast<uint32_t>(x2hi); ++x) {
        const float* const up = blurred + (x > 0 ? x - 1 : x) * columns;
        const float* const here = blurred + x * columns;
        const float* const down = blurred + (x < static_cast<uint32_t>(rows - 1) ? x + 1 : x) * columns;
        uint8_t* const ownRow = own + x * columns;
        for (uint32_t z = static_cast<uint32_t>(z2lo); z < static_cast<uint32_t>(z2hi); ++z) {
            if (ownRow[z] == 0xFF)
                continue;
            const float u = up[z];
            const float d = down[z];
            const float l = here[z > 0 ? z - 1 : z];
            const float r = here[z < static_cast<uint32_t>(columns - 1) ? z + 1 : z];
            const float c = here[z];
            const float h = heights[x * columns + z];
            const double twice = static_cast<double>(c) + c;
            const float alongX = static_cast<float>((static_cast<double>(d) + u) - twice);
            const float alongZ = static_cast<float>((static_cast<double>(l) + r) - twice);
            uint32_t sum = 0;
            for (uint32_t i = 0; i < At<uint32_t>(self, field::kCityCellsX); ++i)
                for (uint32_t j = 0; j < At<uint32_t>(self, field::kCityCellsZ); ++j) {
                    const uint32_t cz = At<uint32_t>(self, field::kCityCellsZ) * z + j;
                    const uint32_t cx = At<uint32_t>(self, field::kCityCellsX) * x + i;
                    if ((alongX < 0.0f || alongZ < 0.0f) && h > c) {
                        float normal[3] = {normalDefault, normalDefault, normalDefault};
                        Virtual<uint32_t>(terrain, 0x70, cx, cz, normal);
                        const double dot = (static_cast<double>(normal[2]) * wind[1] +
                                            static_cast<double>(wind[0]) * normal[0]) +
                                           static_cast<double>(normal[1]) * 0.0;
                        int32_t value =
                            RoundThroughFloat(static_cast<double>(humidity) - dot * At<float>(self, field::kSlopeScale));
                        if (value & 0xFFFFFF00)
                            value = ~(value >> 31);
                        sum += static_cast<uint8_t>(value);
                    } else if (static_cast<double>(alongX) + alongZ > At<float>(self, field::kSlopeLimit) &&
                               alongX > 0.0f && alongZ > 0.0f) {
                        sum += static_cast<uint32_t>(wet);
                    } else {
                        sum += static_cast<uint32_t>(humidity);
                    }
                }
            uint32_t value = ((cells >> 1) + sum) / cells + ownRow[z];
            if (static_cast<int32_t>(value) > 0xFF)
                value = 0xFF;
            ownRow[z] = static_cast<uint8_t>(value);
        }
    }
    // [1 4 6 4 1] / 16 along rows (in place), then along columns into the map.
    const int32_t byteLength = Max(columns, rows) + 4;
    uint8_t* const bytes = NewZeroed<uint8_t>(static_cast<std::size_t>(byteLength));
    auto filter = [bytes](int32_t k) {
        const uint8_t* const p = bytes + k;
        return static_cast<uint8_t>((p[0] + 4 * (p[1] + p[3]) + 6 * p[2] + p[4] + 8) >> 4);
    };
    for (int32_t x = x2lo; x < x2hi; ++x) {
        uint8_t* const ownRow = own + x * columns;
        for (int32_t z = z2lo; z < z2hi; ++z)
            bytes[z + 2] = ownRow[z];
        bytes[0] = bytes[4];
        bytes[1] = bytes[3];
        bytes[columns + 2] = bytes[columns];
        bytes[columns + 3] = bytes[columns - 1];
        for (int32_t z = z0; z < z1; ++z)
            ownRow[z] = filter(z);
    }
    for (int32_t z = z0; z < z1; ++z) {
        for (int32_t x = x2lo; x < x2hi; ++x)
            bytes[x + 2] = own[x * columns + z];
        bytes[0] = bytes[4];
        bytes[1] = bytes[3];
        bytes[rows + 2] = bytes[rows];
        bytes[rows + 3] = bytes[rows - 1];
        for (int32_t x = x0; x < x1; ++x)
            Row(self, field::kMoisture, x)[z] = filter(x);
    }
    if (bytes)
        Free(bytes);
    {
        using NewFn = void*(__cdecl*)(std::size_t size);
        using ConstructFn = void*(__thiscall*)(void* memory);
        void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
        void* const message = memory ? reinterpret_cast<ConstructFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        Virtual<uint32_t>(message, 0x4);
        Virtual<void>(message, 0x14, 0x09B6D58Eu);
        Virtual<void>(message, 0x30, self);
        Virtual<void>(message, 0x84, self);
        Virtual<uint32_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0);
        Virtual<uint32_t>(message, 0x8);
    }
    if (own)
        Free(own);
    if (blurred)
        Free(blurred);
    if (heights)
        Free(heights);
}

using RebuildFn = void(__fastcall*)(void*, void*, const int32_t*);
RebuildFn g_originalRebuild = nullptr;

bool InstallRebuild(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4WeatherSimulator_RebuildMoisture);
    return game::IsBuild641() && entry && hook::Install(*entry, &RebuildMoisture, &g_originalRebuild);
}

const bool g_registeredRebuild = registry::Register({
    "simulators.weather.moisture",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_RebuildMoisture,
    "Our weather moisture map rebuild (docs/weather.md)",
    false,
    InstallRebuild,
});

} // namespace
} // namespace opensc4::weather_sim

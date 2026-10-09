// Our cSC4WeatherSimulator (build 641), first part: the interface, the
// messages, the day's wind, the month (season, temperature, humidity, the
// month's winds), the per-cell moisture of the tick and the tuning, each a
// replacement on the original object (docs/weather.md).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/weather_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>

namespace opensc4::weather_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("weather: first %s", name);
}

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

// x87 transcendental instructions, as the game executes them (the
// precision control is the game's, 53 bits).
double X87Cos(double x)
{
    double result;
    __asm {
        fld x
        fcos
        fstp result
    }
    return result;
}

double X87Tan(double x)
{
    double result;
    __asm {
        fld x
        fptan
        fstp st(0)
        fstp result
    }
    return result;
}

float FloatFromBits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

uint32_t BitsOf(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    return bits;
}

// The game's rounding of p down to a cell index: (p - 0.5) rounded to an
// integer through 1.5 * 2^23 in float, plus one when p is that integer
// plus one (so floor(p) for p >= 0).
int32_t FloorIndex(double p)
{
    const float rounded = static_cast<float>((p - 0.5) + 12582912.0);
    int32_t index = static_cast<int32_t>(BitsOf(rounded) + 0xB4C00000u);
    if (p == static_cast<double>(index + 1))
        index = index + 1;
    return index;
}

// A byte grid's row (vector of row vectors, 12 bytes each).
uint8_t* Row(void* self, std::size_t offset, int32_t x)
{
    return *reinterpret_cast<uint8_t**>(At<char*>(self, offset) + 12 * x);
}

// --- The interface (cISC4WeatherSimulator slots 6-23) --------------------------

float __fastcall CellWidthX(void* self, void*) { return At<float>(self, field::kCellWidthX); }
float __fastcall CellWidthZ(void* self, void*) { return At<float>(self, field::kCellWidthZ); }
int32_t __fastcall CellCountX(void* self, void*) { return At<int32_t>(self, field::kCellsX); }
int32_t __fastcall CellCountZ(void* self, void*) { return At<int32_t>(self, field::kCellsZ); }

// 10 (0x007255C0).
void __fastcall PositionToCell(void* self, void*, float x, float z, int32_t* cellX, int32_t* cellZ)
{
    *cellX = Ftol(static_cast<double>(x) / At<float>(self, field::kCellWidthX));
    *cellZ = Ftol(static_cast<double>(z) / At<float>(self, field::kCellWidthZ));
}

// 11 (0x007255F0).
void __fastcall CellToPosition(void* self, void*, int32_t cellX, int32_t cellZ, float* x, float* z)
{
    *x = static_cast<float>(static_cast<double>(cellX) * At<float>(self, field::kCellWidthX));
    *z = static_cast<float>(static_cast<double>(cellZ) * At<float>(self, field::kCellWidthZ));
}

// 12 (0x00725610): both from the x cell, as the game's does.
void __fastcall CellToStandardCityCell(void* self, void*, int32_t cellX, int32_t, int32_t* x, int32_t* z)
{
    *x = At<int32_t>(self, field::kCityCellsX) * cellX;
    *z = At<int32_t>(self, field::kCityCellsZ) * cellX;
}

// 13 (0x00725640).
void __fastcall StandardCityCellToCell(void* self, void*, int32_t x, int32_t z, int32_t* cellX, int32_t* cellZ)
{
    *cellX = x / At<int32_t>(self, field::kCityCellsX);
    *cellZ = z / At<int32_t>(self, field::kCityCellsZ);
}

// 14 (0x00725660).
int32_t __fastcall GetCurrentSeason(void* self, void*)
{
    return (Virtual<int32_t>(self, 0x3C) - 1) / 3;
}

// 15 (0x00725680): the month, shifted by six for the other hemisphere.
int32_t __fastcall GetCurrentSeasonMonth(void* self, void*)
{
    const int32_t hemisphere = Virtual<int32_t>(At<void*>(self, field::kCity), 0x78);
    void* const date = Virtual<void*>(At<void*>(self, field::kSimulators), 0x1C);
    const int32_t month = Virtual<int32_t>(date, 0x5C);
    if (hemisphere == 1)
        return month;
    return (month + 5) % 12 + 1;
}

// 16 (0x00727F90): the moisture over a rectangle of city cells.
void __fastcall UpdateMoisture(void* self, void*, const int32_t* rect)
{
    if (!At<void*>(self, field::kTerrain))
        return;
    int32_t cells[4] = {rect[0] / 16, rect[1] / 16, (rect[2] + 15) / 16, (rect[3] + 15) / 16};
    CallThis<void>(addr::cSC4WeatherSimulator_RebuildMoisture, self, cells);
}

// 17 (0x00726150): the moisture at a position, clamped into the city.
uint32_t __fastcall GetMoistureValue(void* self, void*, float x, float z)
{
    if (!At<uint8_t>(self, field::kInitialised))
        return 0;
    if (x < 0.0f)
        x = 0.0f;
    else if (x >= At<float>(self, field::kSizeX))
        x = static_cast<float>(static_cast<double>(At<float>(self, field::kSizeX)) - 1.0);
    double zz = z;
    if (z < 0.0f)
        zz = 0.0;
    else if (z >= At<float>(self, field::kSizeZ))
        zz = static_cast<double>(At<float>(self, field::kSizeZ)) - 1.0;
    const int32_t cellX = FloorIndex(static_cast<double>(x) * At<float>(self, field::kInverseWidthX));
    const int32_t cellZ = FloorIndex(zz * At<float>(self, field::kInverseWidthZ));
    return Row(self, field::kMoisture, cellX)[cellZ];
}

// 18 (0x007256C0).
void __fastcall GetMoistureValueMapPtr(void* self, void*, void** map)
{
    *map = static_cast<char*>(self) + field::kMoisture;
}

// 19 (0x007256D0).
uint32_t __fastcall GetMoistureType(void* self, void*, float x, float z)
{
    return Virtual<uint32_t>(self, 0x44, x, z) >> 4;
}

// 20 (0x00726250): a manual moisture value (0-255, scaled to 0-325 and
// kept as its low byte) over a rectangle of positions.
void __fastcall SetManualMoistureAddition(void* self, void*, float x0, float z0, float x1, float z1, float value)
{
    float v = value;
    if (v < 0.0f)
        v = 0.0f;
    else if (v > 255.0f)
        v = 255.0f;
    const float scaled = static_cast<float>(static_cast<double>(v) * 1.2749999761581421 + 12582912.0);
    const auto byte = static_cast<uint8_t>(BitsOf(scaled));
    int32_t cellX0 = Ftol(static_cast<double>(x0) * At<float>(self, field::kInverseWidthX));
    int32_t cellZ0 = Ftol(static_cast<double>(z0) * At<float>(self, field::kInverseWidthZ));
    int32_t cellX1 = Ftol(static_cast<double>(x1) * At<float>(self, field::kInverseWidthX));
    int32_t cellZ1 = Ftol(static_cast<double>(z1) * At<float>(self, field::kInverseWidthZ));
    if (cellX0 < 0)
        cellX0 = 0;
    else if (cellX1 >= At<int32_t>(self, field::kCellsX))
        cellX1 = At<int32_t>(self, field::kCellsX) - 1;
    if (cellZ0 < 0)
        cellZ0 = 0;
    else if (cellZ1 >= At<int32_t>(self, field::kCellsZ))
        cellZ1 = At<int32_t>(self, field::kCellsZ) - 1;
    for (int32_t cx = cellX0; cx < cellX1; ++cx)
        for (int32_t cz = cellZ0; cz < cellZ1; ++cz)
            Row(self, field::kManual, cx)[cz] = byte;
}

// 21 (0x007256F0): halfway from the base humidity to 1.
double __fastcall GetHumidity(void* self, void*, float, float)
{
    const double base = At<float>(self, field::kHumidityBase);
    return (1.0 - base) * 0.5 + base;
}

// 22 (0x00725A00): today's wind speed; its direction scaled to that length.
float __fastcall GetAmbientWind(void* self, void*, float* direction)
{
    const float speed = At<float>(self, field::kWindSpeed);
    if (direction) {
        direction[0] = At<float>(self, field::kWindX);
        direction[1] = At<float>(self, field::kWindZ);
        reinterpret_cast<void(__cdecl*)(float*, float)>(addr::SC4Vector2_SetLength)(direction, speed);
    }
    return speed;
}

// 23 (0x00725710): the same everywhere.
float __fastcall GetWindAtCell(void* self, void*, int32_t, int32_t, float* direction)
{
    return Virtual<float>(self, 0x58, direction);
}

// --- Messages ------------------------------------------------------------------

// 0x00725A70: a message with this object, two values and itself as the
// unknown, posted.
void __fastcall PostWeatherMessage(void* self, void*, uint32_t type, void* object, uint32_t data2, uint32_t data3)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using ConstructFn = void*(__thiscall*)(void* memory);
    void* const server = Service(addr::SC4_MessageServer);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<ConstructFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<void>(message, 0x14, type);
    Virtual<void>(message, 0x30, object);
    Virtual<void>(message, 0x84, self);
    Virtual<void>(message, 0x40, data2);
    Virtual<void>(message, 0x54, data3);
    Virtual<uint32_t>(server, 0x10, message, 0);
    Virtual<uint32_t>(message, 0x8);
}

void Post(void* self, uint32_t type, uint32_t data2)
{
    CallThis<void>(addr::cSC4WeatherSimulator_PostMessage, self, type, self, data2, 0u);
}

// The game's date object (a vtable and the day number).
struct Date {
    uint32_t vtable;
    uint32_t day;
};

int32_t DateMonth(Date& date) { return reinterpret_cast<int32_t(__thiscall*)(Date*)>(addr::SC4Date_Month)(&date); }
int32_t DateDay(Date& date) { return reinterpret_cast<int32_t(__thiscall*)(Date*)>(addr::SC4Date_Day)(&date); }
int32_t DateYear(Date& date) { return reinterpret_cast<int32_t(__thiscall*)(Date*)>(addr::SC4Date_Year)(&date); }

// The day's wind: between the month's start and target (the day of the
// month / 30), the direction made unit length.
void UpdateDayWind(char* self, const Date& date)
{
    Date copy = date;
    const double f = static_cast<double>(DateDay(copy) - 1) * 0.033333333333333333;
    const double endSpeed = At<float>(self, field::kMonthEndSpeed);
    const double endX = endSpeed * At<float>(self, field::kMonthEndX);
    const float endZ = static_cast<float>(endSpeed * At<float>(self, field::kMonthEndZ));
    const double startSpeed = At<float>(self, field::kMonthStartSpeed);
    const float startX = static_cast<float>(startSpeed * At<float>(self, field::kMonthStartX));
    const float startZ = static_cast<float>(startSpeed * At<float>(self, field::kMonthStartZ));
    const float stepX = static_cast<float>((endX - startX) * f);
    const float stepZ = static_cast<float>((static_cast<double>(endZ) - startZ) * f);
    At<float>(self, field::kWindX) = static_cast<float>(static_cast<double>(stepX) + startX);
    At<float>(self, field::kWindZ) = static_cast<float>(static_cast<double>(stepZ) + startZ);
    const double x = At<float>(self, field::kWindX);
    const double z = At<float>(self, field::kWindZ);
    const double length = std::sqrt(z * z + x * x);
    At<float>(self, field::kWindSpeed) = static_cast<float>(length);
    if (length > 0.0) {
        const double scale = static_cast<double>(Global<float>(0x00B0D498)) / length;
        At<float>(self, field::kWindX) = static_cast<float>(scale * At<float>(self, field::kWindX));
        At<float>(self, field::kWindZ) = static_cast<float>(scale * At<float>(self, field::kWindZ));
    }
    const double start = At<float>(self, field::kMonthStartSpeed);
    const double end = At<float>(self, field::kMonthEndSpeed);
    At<float>(self, field::kWindSpeed) = static_cast<float>((end - start) * f + start);
}

// 0x00729720 (slot 3 of the +0x8 vtable): the day (wind), the tick (0x66956815,
// the cell walk and the moisture map), the month, and a city reload
// (0x26D31EC1, DoPostCityInit).
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x8;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x66956814u: {
        const Date date{0x00A827D8, Virtual<uint32_t>(message, 0x24)};
        Date copy = date;
        if (DateMonth(copy) == At<int32_t>(self, field::kMonth))
            UpdateDayWind(self, date);
        return 1;
    }
    case 0x66956815u:
    case 0x66956816u: {
        Date date{0x00A827D8, Virtual<uint32_t>(message, 0x24)};
        const int32_t data3 = Virtual<int32_t>(message, 0x4C);
        const int32_t year = DateYear(date);
        CallThis<uint8_t>(type == 0x66956815u ? addr::cSC4WeatherSimulator_Tick : addr::cSC4WeatherSimulator_OnMonth,
                          self, data3, year);
        return 1;
    }
    case 0x26D31EC1u:
        Virtual<uint8_t>(self, 0x14);
        return 1;
    default: return 1;
    }
}

// --- The month -------------------------------------------------------------------

struct EdgeCount {
    int32_t count;
    float x, z;
};

bool EdgeLess(const EdgeCount& a, const EdgeCount& b)
{
    if (a.count != b.count)
        return a.count < b.count;
    if (a.x < b.x)
        return true;
    if (!(a.x <= b.x))
        return false;
    return a.z < b.z;
}

// 0x00729350: the month's temperature and humidity (cosines of the month),
// the wind's start (last month's target) and a new target: a random speed
// and the direction from the city edge with the most water, turned by a
// random amount.
void __fastcall NewMonthWeather(void* self, void*, int32_t month)
{
    static bool logged = false;
    FirstCall("NewMonthWeather", logged);
    using UniformFn = double(__thiscall*)(void* random, double low, double high);
    using GaussFn = double(__thiscall*)(void* random);
    const int32_t index = month - 1;
    const double swing = static_cast<double>(At<float>(self, field::kTemperatureScale)) *
                         At<float>(self, field::kTemperatureSwing);
    const double k = 0.5235987901687622;
    At<uint32_t>(self, field::kMonthStartSpeed) = At<uint32_t>(self, field::kMonthEndSpeed);
    At<float>(self, field::kTemperature) = static_cast<float>(
        (X87Cos(static_cast<double>(index - 7) * k) * swing + 1.0) * At<float>(self, field::kBaseTemperature));
    const double humiditySwing = static_cast<double>(At<float>(self, field::kHumidityScale)) *
                                 At<float>(self, field::kHumiditySwing);
    At<float>(self, field::kHumidity) = static_cast<float>(
        (X87Cos(static_cast<double>(index) * k) * humiditySwing + 1.0) * At<float>(self, field::kHumidityBase));
    At<uint32_t>(self, field::kMonthStartX) = At<uint32_t>(self, field::kMonthEndX);
    At<uint32_t>(self, field::kMonthStartZ) = At<uint32_t>(self, field::kMonthEndZ);
    void* const random = static_cast<char*>(self) + field::kRandom;
    At<float>(self, field::kMonthEndSpeed) =
        static_cast<float>(reinterpret_cast<UniformFn>(addr::SC4Random_Uniform)(random, 2.0, 4.0));
    void* const terrain = Service(0x00B43CF4);
    const int32_t sizeX = Virtual<int32_t>(terrain, 0x1C);
    const int32_t sizeZ = Virtual<int32_t>(terrain, 0x20);
    auto* const edges = static_cast<EdgeCount*>(Allocate(0x30));
    for (int i = 0; i < 4; ++i)
        edges[i] = {0, FloatFromBits(0), FloatFromBits(0)};
    for (int32_t i = 0; i < sizeZ; ++i) {
        edges[0].count += static_cast<uint8_t>(Virtual<uint32_t>(terrain, 0x88, 0, i));
        edges[2].count += static_cast<uint8_t>(Virtual<uint32_t>(terrain, 0x88, sizeX - 1, i));
    }
    for (int32_t i = 0; i < sizeX; ++i) {
        edges[1].count += static_cast<uint8_t>(Virtual<uint32_t>(terrain, 0x88, i, 0));
        edges[3].count += static_cast<uint8_t>(Virtual<uint32_t>(terrain, 0x88, i, sizeZ - 1));
    }
    edges[0].x = 1.0f;
    edges[0].z = 0.0f;
    edges[1].x = 0.0f;
    edges[1].z = 1.0f;
    edges[2].x = -1.0f;
    edges[2].z = 0.0f;
    edges[3].x = 0.0f;
    edges[3].z = -1.0f;
    // std::sort of four distinct keys: any correct sort gives the same order.
    for (int i = 1; i < 4; ++i)
        for (int j = i; j > 0 && EdgeLess(edges[j], edges[j - 1]); --j) {
            const EdgeCount t = edges[j];
            edges[j] = edges[j - 1];
            edges[j - 1] = t;
        }
    double g;
    do {
        g = reinterpret_cast<GaussFn>(addr::cRZRandom_Gaussian)(random) * At<float>(self, field::kWindSpread);
    } while (std::fabs(g) > 1.0);
    const double s = std::sqrt(1.0 - g * g);
    const double x = edges[3].x;
    const double z = edges[3].z;
    At<float>(self, field::kMonthEndX) = static_cast<float>(x * s - z * g);
    At<float>(self, field::kMonthEndZ) = static_cast<float>(z * s + x * g);
    At<int32_t>(self, field::kMonth) = month;
    Free(edges);
}

// 0x00729630: in January the water bodies are found again; every month the
// month's weather; a new season brings its wind and a message.
void __fastcall OnMonth(void* self, void*, int32_t month, int32_t)
{
    static bool logged = false;
    FirstCall("OnMonth", logged);
    using FindFn = uint8_t(__cdecl*)(void* terrain, void* bodies, uint32_t minimum, uint32_t maximum, int32_t cellsX,
                                     int32_t cellsZ, int32_t widthX, int32_t widthZ, const int32_t* rect);
    if (month == 1) {
        if (void* const terrain = At<void*>(self, field::kTerrain)) {
            char* const bodies = static_cast<char*>(self) + field::kWaterBodies;
            const int32_t widthZ = Ftol(At<float>(self, field::kCellWidthZ));
            const int32_t widthX = Ftol(At<float>(self, field::kCellWidthX));
            reinterpret_cast<FindFn>(addr::cSC4WeatherSimulator_FindWaterBodies)(
                terrain, bodies, 3, 5, At<int32_t>(self, field::kCellsX), At<int32_t>(self, field::kCellsZ), widthX,
                widthZ, nullptr);
            CallThis<void>(addr::cSC4WeatherSimulator_KeepEdgeBodies, self, bodies);
            CallThis<void>(addr::cSC4WeatherSimulator_MarkWaterCells, self, bodies,
                           static_cast<char*>(self) + field::kWaterBits);
        }
    }
    CallThis<void>(addr::cSC4WeatherSimulator_NewMonthWeather, self, Virtual<int32_t>(self, 0x3C));
    const int32_t season = Virtual<int32_t>(self, 0x38);
    if (At<int32_t>(self, field::kSeason) != season) {
        At<int32_t>(self, field::kSeason) = season;
        At<uint32_t>(self, field::kWindSpeed) = At<uint32_t>(self, field::kSeasonWind);
        At<uint32_t>(self, field::kWindX) = At<uint32_t>(self, field::kSeasonWind + 4);
        At<uint32_t>(self, field::kWindZ) = At<uint32_t>(self, field::kSeasonWind + 8);
        Post(self, 0x6716A4E5, static_cast<uint32_t>(At<int32_t>(self, field::kSeason)));
    }
}

// --- The tick --------------------------------------------------------------------

// 0x007259B0: a temperature-like value, clamped to -15..45, to 0..255.
int32_t __cdecl MoistureFromValue(float value)
{
    double v = value;
    if (value < -15.0f)
        v = -15.0;
    else if (value > 45.0f)
        v = 45.0;
    const float scaled = static_cast<float>((v - -15.0) * 4.25 + 12582912.0);
    return static_cast<int32_t>(BitsOf(scaled) + 0xB4C00000u);
}

// 0x00726360: a cell's own moisture, averaged into the cells within the
// spread radius (or set, without one).
void __fastcall SpreadCellMoisture(void* self, void*, int32_t x, int32_t z, int32_t value)
{
    const int32_t radius = At<int32_t>(self, field::kSpreadRadius);
    if (radius <= 0) {
        Row(self, field::kCellMoisture, x)[z] = static_cast<uint8_t>(value);
        return;
    }
    for (int32_t cx = x - radius; cx < radius + x; ++cx) {
        if (cx < 0 || cx >= At<int32_t>(self, field::kCellsX))
            continue;
        const int32_t r = At<int32_t>(self, field::kSpreadRadius);
        for (int32_t cz = z - r; cz < r + z; ++cz) {
            if (cz < 0 || cz >= At<int32_t>(self, field::kCellsZ))
                continue;
            uint8_t& cell = Row(self, field::kCellMoisture, cx)[cz];
            cell = static_cast<uint8_t>((static_cast<int32_t>(cell) + value) / 2);
        }
    }
}

// 0x00726890: one cell's moisture: the month's temperature, warmer by the
// water factor over water bodies, less the height and the wind.
void __fastcall UpdateCell(void* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("UpdateCell", logged);
    uint32_t mask;
    const bool water = (*BitWord(At<void*>(self, field::kWaterBits), x, z, mask) & mask) != 0;
    const float height = static_cast<float>(Virtual<double>(At<void*>(self, field::kTerrain), 0x38, x, z));
    const float difference = static_cast<float>(static_cast<double>(At<float>(self, field::kBaseTemperature)) -
                                                At<float>(self, field::kTemperature));
    const double wind = Virtual<double>(self, 0x5C, x, z, static_cast<float*>(nullptr));
    double base = static_cast<double>(At<float>(self, field::kTemperature)) + 273.0;
    if (water)
        base = static_cast<double>(difference) * At<float>(self, field::kWaterFactor) + base;
    const double value = ((base - static_cast<double>(height) * At<float>(self, field::kHeightFactor)) -
                          wind * At<float>(self, field::kWindFactor)) -
                         273.0;
    const int32_t moisture = reinterpret_cast<int32_t(__cdecl*)(float)>(addr::cSC4WeatherSimulator_MoistureFromValue)(
        static_cast<float>(value));
    CallThis<void>(addr::cSC4WeatherSimulator_SpreadCellMoisture, self, x, z, moisture);
}

// 0x00728A40: the tick: the next cells of the walk (a message when it
// wraps), then the whole moisture map. True when the walk wrapped.
uint8_t __fastcall Tick(void* self, void*, int32_t, int32_t)
{
    static bool logged = false;
    FirstCall("Tick", logged);
    using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    using RestartFn = void(__thiscall*)(void* iterator, uint32_t);
    int32_t left = At<int32_t>(self, field::kCellsPerTick);
    uint8_t wrapped = 0;
    void* const iterator = static_cast<char*>(self) + field::kTractIterator;
    while (left != 0) {
        int32_t x, z;
        if (reinterpret_cast<NextFn>(addr::TractIterator_Next)(iterator, &x, &z)) {
            CallThis<void>(addr::cSC4WeatherSimulator_UpdateCell, self, x, z);
            --left;
        } else {
            reinterpret_cast<RestartFn>(addr::TractIterator_Restart)(iterator, 1);
            CallThis<void>(addr::cSC4WeatherSimulator_PostMessage, self, 0x6716A4E6u, self, 0u, 0u);
            wrapped = 1;
        }
    }
    int32_t rect[4] = {0, 0, At<int32_t>(self, field::kCellsX), At<int32_t>(self, field::kCellsZ)};
    CallThis<void>(addr::cSC4WeatherSimulator_RebuildMoisture, self, rect);
    return wrapped;
}

// 0x007296F0 (slot 5): once, the month as for January and ticks until the
// walk wraps.
uint8_t __fastcall DoPostCityInit(void* self, void*)
{
    static bool logged = false;
    FirstCall("DoPostCityInit", logged);
    if (At<uint8_t>(self, field::kSettled))
        return 1;
    CallThis<void>(addr::cSC4WeatherSimulator_OnMonth, self, 1, 0);
    while (!CallThis<uint8_t>(addr::cSC4WeatherSimulator_Tick, self, 0, 0)) {
    }
    At<uint8_t>(self, field::kSettled) = 1;
    return 1;
}

// --- The tuning (0x00725AF0) --------------------------------------------------------

void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
    using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
    using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
    const auto getFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
    const auto getSint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
    const auto getUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
    const uint32_t key[3] = {0x6534284A, 0x7A4A8458, 0x1A2FDB6B};
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Service(addr::SC4_ResourceManager), 0xC, key, 0xA52160F5u, &exemplar, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    char* const s = static_cast<char*>(self);
    getSint32(holder, 0xA7607D70, &At<int32_t>(s, field::kTickDivisor));
    getSint32(holder, 0xA7607D71, &At<int32_t>(s, 0x58));
    getSint32(holder, 0xA7607D72, &At<int32_t>(s, field::kSpreadRadius));
    getFloat(holder, 0xA7607D73, &At<float>(s, field::kBaseTemperature));
    getFloat(holder, 0xA7607D74, &At<float>(s, field::kTemperatureSwing));
    getFloat(holder, 0xA7607D75, &At<float>(s, field::kTemperatureScale));
    getSint32(holder, 0xA7607D76, &At<int32_t>(s, field::kBodyMargin));
    getFloat(holder, 0xA7607D77, &At<float>(s, field::kWaterFactor));
    getFloat(holder, 0xA7607D78, &At<float>(s, field::kHeightFactor));
    getFloat(holder, 0xA7607D79, &At<float>(s, field::kWindFactor));
    getFloat(holder, 0xA7607D86, &At<float>(s, field::kSeasonWind));
    getFloat(holder, 0xA7607D87, &At<float>(s, field::kSeasonWind + 4));
    getFloat(holder, 0xA7607D88, &At<float>(s, field::kSeasonWind + 8));
    getFloat(holder, 0xA7607D7A, &At<float>(s, field::kHumidityBase));
    getFloat(holder, 0xA7607D7B, &At<float>(s, field::kHumiditySwing));
    getFloat(holder, 0xA7607D7C, &At<float>(s, field::kHumidityScale));
    if (getFloat(holder, 0xA7607D7D, &At<float>(s, field::kSlopeScale)))
        At<float>(s, field::kSlopeScale) = static_cast<float>(static_cast<double>(At<float>(s, field::kSlopeScale)) * 255.0);
    getFloat(holder, 0xA7607D7E, &At<float>(s, field::kRainFactor));
    getFloat(holder, 0xA7607D7F, &At<float>(s, field::kSlopeLimit));
    getUint32(holder, 0xA7607D80, &At<uint32_t>(s, 0x110));
    getUint32(holder, 0xA7607D81, &At<uint32_t>(s, 0x114));
    if (getFloat(holder, 0xA7607D89, &At<float>(s, field::kWindSpread)))
        At<float>(s, field::kWindSpread) = static_cast<float>(
            X87Tan(static_cast<double>(At<float>(s, field::kWindSpread)) * 0.008726646192371845) * 0.5);
    if (At<int32_t>(s, field::kTickDivisor) <= 0)
        At<int32_t>(s, field::kTickDivisor) = 1;
    if (At<int32_t>(s, 0x58) <= 0)
        At<int32_t>(s, 0x58) = 1;
    float& swing = At<float>(s, field::kTemperatureSwing);
    if (swing < 0.0f)
        swing = 0.0f;
    else if (swing >= 1.0f)
        swing = 1.0f;
    if (At<float>(s, field::kWaterFactor) < 0.0f)
        At<float>(s, field::kWaterFactor) = 0.0f;
    float& humiditySwing = At<float>(s, field::kHumiditySwing);
    if (humiditySwing < 0.0f)
        humiditySwing = 0.0f;
    else if (humiditySwing >= 1.0f)
        humiditySwing = 1.0f;
    Virtual<uint32_t>(exemplar, 0x8);
}

// --- Installation ----------------------------------------------------------------

struct Hook {
    std::uintptr_t address;
    void* ours;
    void** original;
};

bool InstallHooks(const Hook* hooks, std::size_t count)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* entry = addr_check::Find(hooks[i].address);
        if (!entry || !hook::Install(*entry, hooks[i].ours, hooks[i].original))
            return false;
    }
    return true;
}

void* g_original[16];

bool InstallMonth(registry::Mode)
{
    const Hook hooks[] = {
        {addr::cSC4WeatherSimulator_OnMonth, reinterpret_cast<void*>(&OnMonth), &g_original[0]},
        {addr::cSC4WeatherSimulator_NewMonthWeather, reinterpret_cast<void*>(&NewMonthWeather), &g_original[1]},
        {addr::cSC4WeatherSimulator_PostMessage, reinterpret_cast<void*>(&PostWeatherMessage), &g_original[2]},
    };
    return InstallHooks(hooks, sizeof hooks / sizeof hooks[0]);
}

bool InstallTick(registry::Mode)
{
    const Hook hooks[] = {
        {addr::cSC4WeatherSimulator_Tick, reinterpret_cast<void*>(&Tick), &g_original[3]},
        {addr::cSC4WeatherSimulator_UpdateCell, reinterpret_cast<void*>(&UpdateCell), &g_original[4]},
        {addr::cSC4WeatherSimulator_SpreadCellMoisture, reinterpret_cast<void*>(&SpreadCellMoisture), &g_original[5]},
        {addr::cSC4WeatherSimulator_MoistureFromValue, reinterpret_cast<void*>(&MoistureFromValue), &g_original[6]},
    };
    return InstallHooks(hooks, sizeof hooks / sizeof hooks[0]);
}

bool InstallTuning(registry::Mode)
{
    const Hook hooks[] = {
        {addr::cSC4WeatherSimulator_LoadTuning, reinterpret_cast<void*>(&LoadTuning), &g_original[7]},
    };
    return InstallHooks(hooks, sizeof hooks / sizeof hooks[0]);
}

const bool g_registeredMonth = registry::Register({
    "simulators.weather.month",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_OnMonth,
    "Our weather month: season, temperature, humidity, the month's winds (docs/weather.md)",
    false,
    InstallMonth,
});

const bool g_registeredTick = registry::Register({
    "simulators.weather.tick",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_Tick,
    "Our weather tick: the cell walk and each cell's own moisture (docs/weather.md)",
    false,
    InstallTick,
});

const bool g_registeredTuning = registry::Register({
    "simulators.weather.tuning",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_LoadTuning,
    "Our weather tuning load (docs/weather.md)",
    false,
    InstallTuning,
});

// DoMessage (slot 3 of the +0x8 vtable) and slot 5 of the primary.
bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t expected, void* ours)
{
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(vtableAddress);
    if (vtable[index] != expected) {
        log::Write("weather: slot %d of %08X holds %08X; not installing", index, static_cast<unsigned>(vtableAddress),
                   static_cast<unsigned>(vtable[index]));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(&vtable[index], sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    vtable[index] = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(&vtable[index], sizeof(*vtable), old, &old);
    return true;
}

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() && SwapSlot(addr::cSC4WeatherSimulator_vftable_8, 3, 0x00729720,
                                          reinterpret_cast<void*>(&DoMessage));
}

const bool g_registeredMessages = registry::Register({
    "simulators.weather.messages",
    "simulators/weather",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_vftable_8,
    "Our weather DoMessage: the day's wind, the tick, the month (docs/weather.md)",
    false,
    InstallMessages,
});

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x007296F0, DoPostCityInit),
    OPENSC4_SLOT(6, 0x00477850, CellWidthX),
    OPENSC4_SLOT(7, 0x00477860, CellWidthZ),
    OPENSC4_SLOT(8, 0x004ABAD0, CellCountX),
    OPENSC4_SLOT(9, 0x006AAC50, CellCountZ),
    OPENSC4_SLOT(10, 0x007255C0, PositionToCell),
    OPENSC4_SLOT(11, 0x007255F0, CellToPosition),
    OPENSC4_SLOT(12, 0x00725610, CellToStandardCityCell),
    OPENSC4_SLOT(13, 0x00725640, StandardCityCellToCell),
    OPENSC4_SLOT(14, 0x00725660, GetCurrentSeason),
    OPENSC4_SLOT(15, 0x00725680, GetCurrentSeasonMonth),
    OPENSC4_SLOT(16, 0x00727F90, UpdateMoisture),
    OPENSC4_SLOT(17, 0x00726150, GetMoistureValue),
    OPENSC4_SLOT(18, 0x007256C0, GetMoistureValueMapPtr),
    OPENSC4_SLOT(19, 0x007256D0, GetMoistureType),
    OPENSC4_SLOT(20, 0x00726250, SetManualMoistureAddition),
    OPENSC4_SLOT(21, 0x007256F0, GetHumidity),
    OPENSC4_SLOT(22, 0x00725A00, GetAmbientWind),
    OPENSC4_SLOT(23, 0x00725710, GetWindAtCell),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 25;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4WeatherSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("weather interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("weather interface: %u of %d cISC4WeatherSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.weather.interface",
    "simulators/weather",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_vftable,
    "Our cISC4WeatherSimulator slots 5-23 on the original object (docs/weather.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::weather_sim

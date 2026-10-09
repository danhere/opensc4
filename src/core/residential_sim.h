// Shared between the parts of our cSC4ResidentialSimulator (build 641;
// docs/residential.md): the object's fields as far as they are known, and
// the helpers the parts share. The residential simulator is the game's
// education and health simulator: schools, hospitals, EQ and HQ, strikes,
// and the population grids.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::residential_sim {

// Fields of cSC4ResidentialSimulator (offsets from the object's start;
// 0x3D0 bytes; constructor 0x00700510). Names are ours.
namespace field {
constexpr std::size_t kInitialized = 0x0C;   // uint8
constexpr std::size_t kSetUp = 0x0D;         // uint8: the grids were sized (Init)
constexpr std::size_t kRefCount = 0x10;
constexpr std::size_t kMessages = 0x14;      // the message server
constexpr std::size_t kCity = 0x18;
constexpr std::size_t kService1C = 0x1C;     // 0x00B43D0C
constexpr std::size_t kDevelopment = 0x20;   // the building development simulator (0x00B43D68)
constexpr std::size_t kDeveloper24 = 0x24;   // 0x00B43D74's slot 6 (0x1000, 0)
constexpr std::size_t kDeveloper28 = 0x28;   // the development simulator's slot 32 for 0x1010, 0x1020, 0x1030
constexpr std::size_t kMapFlag = 0x34;       // uint32, 1; handed to new proximity maps
constexpr std::size_t kProximity = 0x38;     // three proximity maps (SC4SimGridBool8) by wealth 1-3
constexpr std::size_t kPopulationGrids = 0x48; // three population grids (uint16)
constexpr std::size_t kEQGrids = 0x7C;       // three EQ grids (float)
constexpr std::size_t kHQGrids = 0x88;       // three HQ grids (float)
constexpr std::size_t kPopulation = 0xA4;    // int32
constexpr std::size_t kBudget = 0xD0;        // the budget simulator (the city's slot 52)
constexpr std::size_t kHealth = 0xD4;        // the health block (strike +0x44 = +0x118 of the object)
constexpr std::size_t kHealthStrike = 0x118; // uint8
constexpr std::size_t kHealthStrikeDay = 0x11C;
constexpr std::size_t kSchool = 0x134;       // the school block
constexpr std::size_t kSchoolStrike = 0x178; // uint8
constexpr std::size_t kSchoolStrikeDay = 0x17C;
constexpr std::size_t kSchoolBuildings = 0x1A8; // vector of lists (by building type)
constexpr std::size_t kHospitalBuildings = 0x1B4;
constexpr std::size_t kSchoolAutoBudget = 0x3C8; // uint8
constexpr std::size_t kHospitalAutoBudget = 0x3C9; // uint8
} // namespace field

void FirstCall(const char* name, bool& logged);

// 0x006FE0F0 and 0x006FC0C0, written in residential_messages.cpp, also
// called by Init.
void LoadRecordsFor(void* first, void* second);
void LoadParamsFor(char* self, char* params);

} // namespace opensc4::residential_sim

// Our cSC4NetworkLotManager, second part (build 641; the class maps, the
// vtables and the fields are in src/core/netlot_sim.h): life and save. The
// factory, the constructor and the two destructors; Init and Shutdown; the
// loading of the network lot configurations (LoadConfigurations, the
// callback the resource key list calls, AddConfiguration,
// LoadLotConfiguration) with the configuration's constructor and deleting
// destructor; Write and Read. Each is a detour at the game's address, and
// every call from one of them to another function of the class goes through
// the game's address, so a group that is off, or a function on the skip
// list, is the game's.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that keep
// both on some path: Init 0x00610120 for a manager that is initialised
// already and Shutdown 0x0060E570 for one that is not (AL only);
// LoadConfigurations 0x006100D0 keeps EDX without a resource manager. Init
// and Shutdown are reached through the vtable only; LoadConfigurations from
// Init's 0x0061020E, which loads EAX and ECX anew and clears EBP right after
// it; the resource callback from the key list's walk; AddConfiguration from
// the callback's 0x0060FC13 (then `mov eax, [esi]`); LoadLotConfiguration
// from the callback's 0x0060FC07 (`test al, al`) and from the lot editor's
// 0x0049C570; the configuration's constructor from 0x0060FBF1 (`mov esi,
// eax`) and from the lot editor window's constructor 0x00499E00; the
// destructor from the deleting destructor's 0x0060EF23 (then `test byte
// [esp+8], 1`).
//
// Left as the game's and called at their addresses: the hash tables'
// constructor 0x00829BC0, clear 0x008EBA60, begin 0x004B0390 and the
// erase by a uint16 key 0x00710910; the clear of a table of vectors
// 0x00584660; the pointer vector's growing insert 0x0051CA60; the bit
// vector's resize 0x0060D310 and bit address 0x00477A40; the property
// getters; the string's formatter 0x0090F574; the stream's reference
// helpers; and the lot configuration's (another class, ours in
// src/core/lotcfg_*.cpp) LoadObjects 0x00479D10, vector copy 0x00479670,
// vector destructor 0x00479760 and ClampObjects 0x00477BB0. The third
// part's WriteLotConfiguration is called through the manager's vtable (slot
// 14), as the game calls it.
//
// Not touched by this part: the lot manager's static generator 0x00B44870
// (only BuildLotObjects and CreateLot draw from it) and the texture and
// style tables at 0x00AA7758 and 0x00AA7728 (no instruction of the 14
// functions has either address).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - operator new 0x005E55E0 and operator delete 0x005E5620 (cdecl, one:
//     `add esp, 4`); the allocation 0x0090CF54 and the free 0x0090CF63
//     (cdecl, one);
//   - the table's constructor 0x00829BC0 one (the bucket hint; ret 4), clear
//     0x008EBA60 none (plain ret; it frees the nodes and looks at no value),
//     begin 0x004B0390 one (the out pair; ret 4), the erase 0x00710910 one
//     (the address of a uint16 key; ret 4; it frees the nodes and releases
//     nothing); the clear 0x00584660 none (plain ret; a node's vector
//     storage and the node freed);
//   - the pointer vector's insert 0x0051CA60 five (where, the value's
//     address, a tag that is not read, 1, 1; ret 0x14);
//   - the bit vector's resize 0x0060D310 two (the count, the value; ret 8)
//     and bit address 0x00477A40 two (the out pair, the index; ret 8);
//   - the property getters 0x005FD3C0 (uint8), 0x005FD480 (uint32),
//     0x005FD4F0 (float), 0x005FD570 (string) and 0x005FFB40 (byte vector):
//     cdecl, three each (the holder, the property, the out address; plain
//     ret; the game takes its pushes off in groups: `add esp, 0x18`, `0x24`,
//     `0xC`, `0x34`, `0x20`);
//   - cSC4LotConfiguration::LoadObjects 0x00479D10 (cdecl, four: the holder,
//     the vector, the version, 3; in the `add esp, 0x34` with the nine
//     pushes of the three getters before it; it reads the version as a byte,
//     `mov al, [esp+0x60]` at 0x00479D36), the vector copy 0x00479670 one (ret 4),
//     the vector destructor 0x00479760 none, ClampObjects 0x00477BB0 (cdecl,
//     two: the vector, four floats; in the `add esp, 0x20`);
//   - the string's formatter 0x0090F574 (cdecl: the string, the format, one
//     value; `add esp, 0x10` with the allocation's one push);
//   - SC4Stream::WriteReference 0x005D99A0 (cdecl, two: the stream, the
//     object; `add esp, 8`), SC4Stream::ReadReference 0x005D9730 (cdecl,
//     four: the stream, the IID, the out pointer, 0; `add esp, 0x10`; it
//     clears the out pointer before anything else);
//   - a stream's 0x1C and 0x24 one (the value when writing, its address when
//     reading: one push before each call and no clean-up), 0x48 none;
//   - the message server's (0x00B43CCC) 0x14 and 0x18 two (the target, the
//     message ID: two pushes before each of the twelve calls);
//   - the city's (0x00B43CEC) 0x174 and 0x178 none;
//   - the resource manager's (0x00B43CA8) 0x0C five (the key, the IID, the
//     out pointer, 0, 0), 0x48 two (the out pointer, the type), 0x80 two
//     (the key, the out pointer); the key list's 0x1C two (the callback, its
//     context) and 0x8 none; a resource's 0x1C none and 0x8 none;
//   - a lot's 0x4 and 0x8 none; a configuration's 0x4, 0x8, 0x10 and 0x18
//     none; a pothole occupant's 0x20 one (the 1 pushed at 0x0060E735 is
//     all the frame has, and the registers popped after it are right) and
//     0x8 none;
//   - this class: the constructor 0x0060EE50, the destructor 0x0060E480 and
//     LoadConfigurations 0x006100D0 none; the deleting destructor one (ret
//     4); AddConfiguration 0x0060E7A0 one (ret 4); LoadLotConfiguration
//     0x0060F5F0 two (ret 8); the callback 0x0060FB90 cdecl with two (plain
//     ret; the walk gives it a key and the context); the lot map's
//     operator[] 0x0060DA50 and the configuration map's 0x0060E3C0 one (ret
//     4); the manager's slot 14 WriteLotConfiguration two (0x0060E810, ret
//     8); the configuration's constructor 0x0060FA90 none and deleting
//     destructor 0x00610070 one (ret 4).
#include "core/netlot_sim.h"

#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::netlot_sim {
namespace {

void Free(void* memory)
{
    CallCdecl<void, void*>(addr::SC4_Free, memory);
}

// ====================================================================================
// simulators.netlot.own: with it the objects carry our copies of the class's
// three vtables (16, 4 and 6 slots) instead of the game's tables. The copies
// are taken at the first use (BuildOwnVtables, with the installation: it
// needs the object group's detour list).
// ====================================================================================

bool g_own = false;
occ_vtables::VtableCopy<kSlots> g_vtable;
occ_vtables::VtableCopy<kMessageSlots> g_vtable4;
occ_vtables::VtableCopy<kSerializableSlots> g_vtable8;
void BuildOwnVtables();

// The three vtables the constructor and the destructor store, in the game's
// order (the game's, or with simulators.netlot.own our copies of them).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4NetworkLotManager_vftable;
    At<std::uintptr_t>(self, kMessagePart) = g_own ? g_vtable4.Address() : kMessageVtable;
    At<std::uintptr_t>(self, kSerializablePart) = g_own ? g_vtable8.Address() : kSerializableVtable;
}

// ====================================================================================
// simulators.netlot.life: the network lot configuration's constructor and
// deleting destructor, and the loading of the configurations
// ====================================================================================

using GetUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetStringFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* text);
using GetBytesFn = uint8_t(__cdecl*)(void* holder, uint32_t property, Vector* bytes);

uint8_t GetUint8(void* holder, uint32_t id, uint8_t* value)
{
    return reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8)(holder, id, value);
}

uint8_t GetUint32(void* holder, uint32_t id, uint32_t* value)
{
    return reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32)(holder, id, value);
}

uint8_t GetFloat(void* holder, uint32_t id, float* value)
{
    return reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat)(holder, id, value);
}

uint8_t GetBytes(void* holder, uint32_t id, Vector* bytes)
{
    return reinterpret_cast<GetBytesFn>(addr::SC4_GetPropertyUint8Vector)(holder, id, bytes);
}

// The configuration's constructor (0x0060FA90, no arguments; the resource
// callback's 0x0060FBF1 and the lot editor window's constructor call it),
// every store in the game's order: the vtable; the object vector and the ID
// cleared; the name, a string of the game built in place: its three
// pointers cleared, eight bytes from the game's allocator (0x0090CF54) as
// its characters with the first one 0, then its vtable (0x00A80810) and its
// own count at +0x24 cleared; the network piece ID 0; the period 4; the two
// bit vectors' five words each cleared; the least slope 0 and the greatest
// 64; the piece mask 0x0F and the rule byte 0; the reference count 0; then
// the zone types' vector is sized to 16 clear bits and the wealths' to 4
// (0x0060D310, at its address), and last the four mask bytes are set to
// 0x0F. The bytes at +0x31 to +0x33 and at +0x66 and +0x67 are never
// written. The answer is the object.
char* __fastcall ConfigurationConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Configuration constructor", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4NetworkLotConfiguration_vftable;
    Vector* const objects = reinterpret_cast<Vector*>(self + configuration::kObjects);
    objects->begin = nullptr;
    objects->end = nullptr;
    objects->capacity = nullptr;
    At<uint32_t>(self, configuration::kId) = 0;
    GameString* const name = reinterpret_cast<GameString*>(self + configuration::kName);
    name->begin = nullptr;
    name->end = nullptr;
    name->capacity = nullptr;
    char* const text = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, std::size_t{8});
    name->begin = text;
    name->end = text;
    name->capacity = text + 8;
    *text = 0;
    name->vtable = addr::SC4String_Vtable;
    name->references = 0;
    At<uint32_t>(self, configuration::kPieceId) = 0;
    At<uint8_t>(self, configuration::kPeriod) = configuration::kDefaultPeriod;
    for (std::size_t offset = 0; offset <= bitvector::kCapacity; offset += 4) {
        At<uint32_t>(self, configuration::kZones + offset) = 0;
        At<uint32_t>(self, configuration::kWealths + offset) = 0;
    }
    At<uint32_t>(self, configuration::kLeastSlope) = 0;
    At<float>(self, configuration::kGreatestSlope) = configuration::kDefaultGreatestSlope;
    At<uint8_t>(self, configuration::kPieceMask) = configuration::kDefaultMask;
    At<uint8_t>(self, configuration::kPieceFlagRule) = 0;
    At<uint32_t>(self, configuration::kRefCount) = 0;
    CallThis<void, uint32_t, uint32_t>(addr::SC4BitVector_Resize60D310, self + configuration::kZones, kZoneTypes, 0u);
    CallThis<void, uint32_t, uint32_t>(addr::SC4BitVector_Resize60D310, self + configuration::kWealths, kWealths, 0u);
    for (std::size_t i = 0; i < configuration::kCellMaskCount; ++i)
        At<uint8_t>(self, configuration::kCellMasks + i) = configuration::kDefaultMask;
    return self;
}

// The configuration's slot 7 (0x00610070, ret 4: the flags; its Release
// calls it with 1): the vtable stored again; the wealths' words freed, then
// the zone types' (0x0090CF63; every free here only for a pointer that is
// not null); the name's vtable stored again and its characters freed; the
// object vector destroyed (the lot configuration's 0x00479760, at its
// address: every object's list of IDs and the storage); and with bit 0 of
// the flags' low byte the memory freed (operator delete). The answer is the
// object.
void* __fastcall ConfigurationDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("Configuration deleting destructor", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4NetworkLotConfiguration_vftable;
    if (void* const words = At<void*>(self, configuration::kWealths + bitvector::kWords))
        Free(words);
    if (void* const words = At<void*>(self, configuration::kZones + bitvector::kWords))
        Free(words);
    GameString* const name = reinterpret_cast<GameString*>(self + configuration::kName);
    name->vtable = addr::SC4String_Vtable;
    if (name->begin)
        Free(name->begin);
    CallThis<void>(addr::cSC4LotConfiguration_DestroyObjects, self + configuration::kObjects);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// 0x0060F5F0 (ret 8: a property holder and a configuration; the resource
// callback's 0x0060FC07 and the lot editor's 0x0049C570 call it): the
// configuration filled from an exemplar's properties. AL 0 for a null
// configuration, and for a holder without the network piece ID (0xC9A5A1BE:
// nothing of the configuration is written then, but the version was read).
// Else, in the game's order:
//   - the version byte (0x88EDC789; 1 when the property is missing);
//   - the configuration referenced for the length of the call (slot 1);
//   - the piece ID to +0x28; the name (0x20) into the string at +0x14; the
//     ID (0x21) to +0x10 (left as it is without the property);
//   - the zone types (0x88EDC793, bytes): for every byte that is below the
//     number of bits of the vector at +0x34 that bit is set (its address
//     from the game's 0x00477A40); the vector is not cleared first, and
//     without the property no bit is set;
//   - the wealths (0x88EDC795, bytes) the same into the vector at +0x48,
//     with the bit's word and mask worked out in place: the first bit's
//     offset plus the wealth, divided by 32 toward zero, the remainder the
//     bit, and for a negative remainder the word before and the bit plus 32;
//   - the cell masks (0x49D55951): only when the property has exactly four
//     bytes, they go to +0x2C;
//   - the period (0xCA81B8D4, a byte) to +0x30, the least slope
//     (0xAA120972) to +0x5C and the greatest (0xAA120973) to +0x60, each
//     left as it is without its property;
//   - the objects into the vector at +0x4: the lot configuration's
//     LoadObjects (0x00479D10) with the version and the detail 3; then,
//     when the vector's capacity is not its size (both counted in objects of
//     0x34 bytes, signed), the vector is replaced by an exact copy
//     (0x00479670) and the old storage let go as the game has it inline:
//     every object's list of IDs freed, then the storage;
//   - the piece mask (0xEC3BD470, a byte) to +0x64 and the rule byte
//     (0xCC3E4755) to +0x65;
//   - the objects moved into the cell (ClampObjects 0x00477BB0 with the
//     rectangle 16, 16, 0, 0);
//   - for a version below 2 the exemplar is written back in the current
//     form: the resource manager's (0x00B43CA8, not tested for null) slot 32
//     gives the segment of the key (type 0x6534284A, group 0x89AC5643, the
//     configuration's ID by its slot 4), and the manager's own slot 14,
//     WriteLotConfiguration, gets the configuration and that segment (the
//     reference slot 32 added is never released, as in the game);
//   - the configuration released (slot 2), the byte vector's storage freed.
// AL 1.
//
// The game keeps the version in a byte of a stack word whose other three
// bytes it never sets, and hands LoadObjects the whole word; LoadObjects
// reads the byte only. The flag "version below 2" lives in the low byte of
// the stack slot of the second argument, the temporary copy's end in the
// slot of the first: plain locals here. The out pointer of slot 32 is the
// first argument's slot (the holder, or after an exact copy the old
// storage's end): slot 32 clears it first (src/core/lotcfg_load.cpp has the
// same call), so it starts as null here. The piece ID's word is not set
// before its read; nothing reads it when the read fails.
uint8_t __fastcall LoadLotConfiguration(char* self, void*, void* holder, char* config)
{
    static bool logged = false;
    FirstCall("LoadLotConfiguration", logged);
    if (!config)
        return 0;
    uint8_t version = 1;
    GetUint8(holder, property::kVersion, &version);
    const bool old = version < 2;
    uint32_t pieceId = 0;
    if (!GetUint32(holder, property::kPieceId, &pieceId))
        return 0;
    AddRefOf(config);
    At<uint32_t>(config, configuration::kPieceId) = pieceId;
    reinterpret_cast<GetStringFn>(addr::SC4_GetPropertyString)(holder, property::kName, config + configuration::kName);
    GetUint32(holder, property::kId, &At<uint32_t>(config, configuration::kId));

    Vector bytes = {nullptr, nullptr, nullptr};
    if (GetBytes(holder, property::kZoneTypes, &bytes)) {
        char* const zones = config + configuration::kZones;
        const uint32_t size = BitCount(zones);
        const int32_t count = static_cast<int32_t>(bytes.end - bytes.begin);
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t zone = static_cast<uint8_t>(bytes.begin[i]);
            if (zone >= size)
                continue;
            struct {
                uint32_t* word;
                uint32_t mask;
            } bit;
            CallThis<void*, void*, uint32_t>(addr::SC4BitVector_At, zones, &bit, zone);
            *bit.word |= bit.mask;
        }
    }
    if (GetBytes(holder, property::kWealths, &bytes)) {
        char* const wealths = config + configuration::kWealths;
        const uint32_t size = BitCount(wealths);
        const int32_t count = static_cast<int32_t>(bytes.end - bytes.begin);
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t wealth = static_cast<uint8_t>(bytes.begin[i]);
            if (wealth >= size)
                continue;
            const int32_t at = At<int32_t>(wealths, bitvector::kOffset) + static_cast<int32_t>(wealth);
            uint32_t* word = At<uint32_t*>(wealths, bitvector::kWords) + at / 32;
            int32_t shift = at % 32;
            if (shift < 0) {
                shift += 32;
                --word;
            }
            *word |= 1u << shift;
        }
    }
    if (GetBytes(holder, property::kCellMasks, &bytes) &&
        bytes.end - bytes.begin == static_cast<std::ptrdiff_t>(configuration::kCellMaskCount))
        for (std::size_t i = 0; i < configuration::kCellMaskCount; ++i)
            At<uint8_t>(config, configuration::kCellMasks + i) = static_cast<uint8_t>(bytes.begin[i]);
    GetUint8(holder, property::kPeriod, &At<uint8_t>(config, configuration::kPeriod));
    GetFloat(holder, property::kLeastSlope, &At<float>(config, configuration::kLeastSlope));
    GetFloat(holder, property::kGreatestSlope, &At<float>(config, configuration::kGreatestSlope));

    Vector* const objects = reinterpret_cast<Vector*>(config + configuration::kObjects);
    CallCdecl<void, void*, Vector*, uint32_t, uint32_t>(addr::cSC4LotConfiguration_LoadObjects, holder, objects,
                                                        uint32_t{version}, 3u);
    const int32_t capacity = static_cast<int32_t>(objects->capacity - objects->begin) /
                             static_cast<int32_t>(configuration::kObjectSize);
    const int32_t size =
        static_cast<int32_t>(objects->end - objects->begin) / static_cast<int32_t>(configuration::kObjectSize);
    if (size != capacity) {
        Vector copy;
        CallThis<Vector*, Vector*>(addr::cSC4LotConfiguration_CopyObjectVector, &copy, objects);
        const Vector exact = copy;
        copy = *objects;
        *objects = exact;
        for (char* object = copy.begin; object != copy.end; object += configuration::kObjectSize)
            if (void* const ids = At<void*>(object, configuration::kObjectIds))
                Free(ids);
        if (copy.begin)
            Free(copy.begin);
    }
    GetUint8(holder, property::kPieceMask, &At<uint8_t>(config, configuration::kPieceMask));
    GetUint8(holder, property::kPieceFlagRule, &At<uint8_t>(config, configuration::kPieceFlagRule));
    const float cell[4] = {16.0f, 16.0f, 0.0f, 0.0f};
    CallCdecl<uint8_t, Vector*, const float*>(addr::cSC4LotConfiguration_ClampObjects, objects, cell);
    if (old) {
        uint32_t key[3] = {kExemplarType, kResaveGroup, 0};
        key[2] = Virtual<uint32_t>(config, configuration_slot::kGetId);
        void* segment = nullptr;
        Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), resources_slot::kFindSegment,
                         static_cast<uint32_t*>(key), &segment);
        Virtual<uint8_t>(self, slot::kWriteLotConfiguration, config, segment);
    }
    ReleaseOf(config);
    if (bytes.begin)
        Free(bytes.begin);
    return 1;
}

// 0x0060E7A0 (ret 4: a configuration; the resource callback's 0x0060FC13 is
// its only caller): the configuration referenced (slot 1) and appended to
// the vector of its network piece ID (slot 6) in the configuration map
// (operator[] 0x0060E3C0, at its address, which makes the entry when it is
// the first of its piece): in place when the vector has room (the store is
// skipped for a null end, the game's placement test; the end is read again
// for the step), else through the game's growing insert 0x0051CA60 (at the
// end, the value's address, a tag the insert does not read: the game hands
// it the same address, one copy, 1). EAX is not set.
void __fastcall AddConfiguration(char* self, void*, void* config)
{
    static bool logged = false;
    FirstCall("AddConfiguration", logged);
    AddRefOf(config);
    uint32_t pieceId = Virtual<uint32_t>(config, configuration_slot::kGetNetworkPieceId);
    Vector* const configs = CallThis<Vector*, uint32_t*>(addr::cSC4NetworkLotManager_ConfigMapIndex,
                                                          self + field::kConfigurations, &pieceId);
    char* const end = configs->end;
    if (end != configs->capacity) {
        if (end)
            At<void*>(end, 0) = config;
        configs->end = configs->end + 4;
        return;
    }
    CallThis<void, char*, void**, void**, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, configs, end, &config,
                                                              &config, 1u, 1u);
}

// 0x0060FB90 (cdecl, two arguments: a resource key and the context, here
// the manager; LoadConfigurations hands its address to the key list's walk,
// which calls it once per key of the exemplar type): the resource of the
// key is asked of the resource manager (0x00B43CA8, slot 3, as an exemplar
// 0xA52160F5; nothing more when that fails). The exemplar's property holder
// (its slot 7); when its exemplar type property (0x10) is there and is 0x21,
// a configuration is made (0x6C bytes from operator new, the constructor at
// its address), referenced, filled by LoadLotConfiguration (at its address)
// and, when that answers yes, handed to AddConfiguration (at its address);
// then the callback's own reference is given back (a configuration that was
// not added is destroyed by it). Last the resource is released. The game
// has no answer in EAX that anything reads.
//
// When operator new gives nothing the game calls the null configuration's
// slot 1; here the constructor is called with the null pointer first. Both
// end the same way.
void __cdecl OnConfigurationResource(void* key, char* self)
{
    static bool logged = false;
    FirstCall("OnConfigurationResource", logged);
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), resources_slot::kGetResource, key, kIidExemplar,
                          &resource, 0u, 0u))
        return;
    void* const holder = Virtual<void*>(resource, resources_slot::kAsPropertyHolder);
    uint32_t type = 0;
    if (GetUint32(holder, property::kExemplarType, &type) && type == kExemplarTypeValue) {
        void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, kConfigurationSize);
        char* const config = CallThis<char*>(addr::cSC4NetworkLotConfiguration_cSC4NetworkLotConfiguration, raw);
        AddRefOf(config);
        if (CallThis<uint8_t, void*, char*>(addr::cSC4NetworkLotManager_LoadLotConfiguration, self, holder, config))
            CallThis<void, char*>(addr::cSC4NetworkLotManager_AddConfiguration, self, config);
        ReleaseOf(config);
    }
    ReleaseOf(resource);
}

// 0x006100D0 (no arguments; Init's 0x0061020E is its only caller): every
// network lot configuration of the loaded resources. Nothing without a
// resource manager (0x00B43CA8). Its slot 18 gives the list of the keys of
// the exemplar type (0x6534284A) into a pointer that starts as null; when
// its answer (the whole of EAX) is not zero, the list's slot 7 walks the
// keys with the callback at the game's address 0x0060FB90 and the manager
// as context, and the list is released.
void __fastcall LoadConfigurations(char* self, void*)
{
    static bool logged = false;
    FirstCall("LoadConfigurations", logged);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (!resources)
        return;
    void* list = nullptr;
    if (!Virtual<void*>(resources, resources_slot::kGetKeyList, &list, kExemplarType))
        return;
    Virtual<uint32_t>(list, resources_slot::kListEnumerate,
                      reinterpret_cast<void*>(addr::cSC4NetworkLotManager_OnConfigurationResource), self);
    ReleaseOf(list);
}

// ====================================================================================
// simulators.netlot.life: Init and Shutdown
// ====================================================================================

// The configuration of the map with the ID, or null: the map's nodes in the
// table's order (0x004B0390 gives the first) and each node's vector from its
// begin to its end (both read once per node), each configuration's slot 4
// asked and compared with the lot's saved ID, which is read again for every
// compare. Init has this inline.
void* ConfigurationOfLot(char* configurations, char* networkLot)
{
    HashIterator first;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, configurations, &first);
    char* const table = first.table;
    for (char* found = first.node; found; found = NextNode32(table, found)) {
        char* const end = At<char*>(found, hashnode::kValue + 4);
        for (char* at = At<char*>(found, hashnode::kValue); at != end; at += 4) {
            void* const config = At<void*>(at, 0);
            if (Virtual<uint32_t>(config, configuration_slot::kGetId) ==
                At<uint32_t>(networkLot, lot::kSavedConfigurationId))
                return config;
        }
    }
    return nullptr;
}

// "Missing lot configuration 0x%08x" and a line feed (0x00AA79B8).
const char kMissingFormat[] = "Missing lot configuration 0x%08x\n";

// Slot 3 (0x00610120, no arguments): once per object (the byte at +0x70,
// set first). In the game's order:
//   - the part at +4 subscribed to the six messages (kMessages, in that
//     order) at the message server (0x00B43CCC, read anew for each and not
//     tested for null);
//   - the cell iterator at +0x38 set to the whole city: the city's
//     (0x00B43CEC) slot 94 (the cell count in z) is asked first, then slot
//     93; the rectangle is 0, 0, x count - 1, z count - 1, a high bound
//     below its low bound raised to it; the current cell 0, 0; the state
//     byte 0, or 2 when the cell 0, 0 is outside the rectangle;
//   - LoadConfigurations, at its address;
//   - for every lot of the lot map, in the table's order, the configuration
//     with the ID Read left in the lot's +0xC is looked for (ConfigurationOfLot:
//     the first one whose slot 4 gives it). Found: unless the lot has it
//     already, it is referenced, stored at the lot's +0x8, and what was
//     there is released when it was not null. Not found: the game formats
//     the text "Missing lot configuration 0x%08x" into a string on its
//     stack (eight bytes from the allocator 0x0090CF54, then the formatter
//     0x0090F574) and throws it away (the storage freed), and the lot's cell
//     key goes into a vector of words on the stack, which grows as the
//     game's vectors do (to its size plus the greater of its size and 1;
//     the allocator and the free called as the game calls them, so the heap
//     sees the same requests in the same order);
//   - every key of that vector is erased from the lot map (0x00710910, at
//     its address, with the address of a word holding the key). The erase
//     frees the node and nothing else: such a lot keeps the one reference
//     Read gave it and its network piece, and nobody can reach it again.
//     That is the game's leak, kept. The vector's storage is freed.
// AL is 1; the rest of the game's EAX is not reproduced. One line of ours in
// the log says how many lots went that way, when any did.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    char* const target = self + kMessagePart;
    for (const uint32_t message : kMessages)
        Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), server_slot::kAddNotification, target, message);

    void* const city = Global<void*>(addr::SC4_City);
    const int32_t highZ = Virtual<int32_t>(city, city_slot::kCellCountZ) - 1;
    const int32_t highX = Virtual<int32_t>(city, city_slot::kCellCountX) - 1;
    char* const cells = self + field::kIterator;
    At<int32_t>(cells, iterator::kLowX) = 0;
    At<int32_t>(cells, iterator::kLowZ) = 0;
    At<int32_t>(cells, iterator::kHighX) = highX;
    At<int32_t>(cells, iterator::kHighZ) = highZ;
    if (At<int32_t>(cells, iterator::kHighX) < At<int32_t>(cells, iterator::kLowX))
        At<int32_t>(cells, iterator::kHighX) = At<int32_t>(cells, iterator::kLowX);
    if (At<int32_t>(cells, iterator::kHighZ) < At<int32_t>(cells, iterator::kLowZ))
        At<int32_t>(cells, iterator::kHighZ) = At<int32_t>(cells, iterator::kLowZ);
    At<int32_t>(cells, iterator::kX) = 0;
    At<int32_t>(cells, iterator::kZ) = 0;
    At<uint8_t>(cells, iterator::kState) = 0;
    if (At<int32_t>(cells, iterator::kLowX) > 0 || At<int32_t>(cells, iterator::kLowZ) > 0 ||
        At<int32_t>(cells, iterator::kHighX) < 0 || At<int32_t>(cells, iterator::kHighZ) < 0)
        At<uint8_t>(cells, iterator::kState) = 2;

    CallThis<void>(addr::cSC4NetworkLotManager_LoadConfigurations, self);

    char* const lots = self + field::kLots;
    char* const configurations = self + field::kConfigurations;
    Vector missing = {nullptr, nullptr, nullptr};
    HashIterator first;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, lots, &first);
    char* const table = first.table;
    for (char* node = first.node; node; node = NextNode16(table, node)) {
        char* const networkLot = At<char*>(node, hashnode::kValue);
        if (void* const config = ConfigurationOfLot(configurations, networkLot)) {
            void* const before = At<void*>(networkLot, lot::kConfiguration);
            if (config != before) {
                AddRefOf(config);
                At<void*>(networkLot, lot::kConfiguration) = config;
                if (before)
                    ReleaseOf(before);
            }
            continue;
        }
        GameString text = {0, nullptr, nullptr, nullptr, 0};
        char* const buffer = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, std::size_t{8});
        text.begin = buffer;
        text.end = buffer;
        text.capacity = buffer + 8;
        *buffer = 0;
        text.vtable = addr::SC4String_Vtable;
        text.references = 0;
        reinterpret_cast<void(__cdecl*)(void*, const char*, ...)>(addr::cRZBaseString_Sprintf)(
            &text, kMissingFormat, At<uint32_t>(networkLot, lot::kSavedConfigurationId));
        const uint32_t key = At<uint16_t>(node, hashnode::kKey);
        if (missing.end != missing.capacity) {
            if (missing.end)
                At<uint32_t>(missing.end, 0) = key;
            missing.end += 4;
        } else {
            const std::ptrdiff_t bytes = missing.end - missing.begin;
            const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(bytes) >> 2);
            const uint32_t room = count + (count < 1 ? 1u : count);
            char* const grown =
                room != 0 ? CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, std::size_t{room} * 4) : nullptr;
            char* at = grown;
            if (missing.end != missing.begin) {
                std::memmove(grown, missing.begin, static_cast<std::size_t>(bytes));
                at = grown + bytes;
            }
            At<uint32_t>(at, 0) = key;
            if (missing.begin)
                Free(missing.begin);
            missing.begin = grown;
            missing.end = at + 4;
            missing.capacity = grown + std::size_t{room} * 4;
        }
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            Free(text.begin);
    }

    const uint32_t gone = static_cast<uint32_t>(static_cast<int32_t>(missing.end - missing.begin) >> 2);
    for (uint32_t i = 0; i < gone; ++i) {
        uint32_t key = At<uint16_t>(missing.begin, i * 4);
        CallThis<uint32_t, uint32_t*>(addr::SC4LinkTable_Erase, lots, &key);
    }
    if (gone != 0)
        log::Write("netlot: Init: %u lots had no configuration of their saved ID and left the lot map", gone);
    if (missing.begin)
        Free(missing.begin);
    return 1;
}

// Slot 4 (0x0060E570, no arguments; the destructor does NOT call it):
// nothing for a manager that is not initialised. Else the byte at +0x70 is
// cleared; the part at +4 is taken off the six messages, in Init's order
// (the message server read anew for each and not tested for null); every
// lot of the lot map is released (the table's order; the node's link is
// read after the Release) and the map KEEPS its nodes, which then point at
// lots that may be gone; every configuration of the configuration map is
// released (the vector's bounds read again after each one) and the map
// cleared (0x00584660, which frees the vectors and the nodes); every
// pothole's occupant gets its slot 8 with 1 and is released, and the pothole
// map is cleared (0x008EBA60). The configuration forced for the next piece
// and the waiting set are not touched. AL is 1.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 0;
    char* const target = self + kMessagePart;
    for (const uint32_t message : kMessages)
        Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), server_slot::kRemoveNotification, target, message);

    HashIterator lots;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, self + field::kLots, &lots);
    char* const lotTable = lots.table;
    for (char* node = lots.node; node; node = NextNode16(lotTable, node))
        ReleaseOf(At<void*>(node, hashnode::kValue));

    HashIterator configs;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, self + field::kConfigurations, &configs);
    char* const configTable = configs.table;
    for (char* node = configs.node; node; node = NextNode32(configTable, node))
        for (uint32_t i = 0; i < ConfigurationCount(node); ++i)
            ReleaseOf(At<void*>(At<char*>(node, hashnode::kValue), i * 4));
    CallThis<void>(addr::SC4PtrVectorHashMap_Clear, self + field::kConfigurations);

    char* const potholes = self + field::kPotholes;
    for (char* node = FirstNode(potholes); node; node = NextNode16(potholes, node)) {
        void* const occupant = At<void*>(node, hashnode::kValue);
        Virtual<void>(occupant, pothole_slot::kStop, 1u);
        ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4Uint32HashSet_Clear, potholes);
    return 1;
}

// ====================================================================================
// simulators.netlot.save: Write and Read
// ====================================================================================

// The serializable part's slot 3 (0x0060D6B0, ret 4: the stream; `this` is
// the object + 8): the version 2 as a uint16; the lot map's count as a
// uint32; for every lot in the table's order (0x004B0390 gives the first)
// its cell key as a uint16, a reference to its network piece
// (SC4Stream::WriteReference, whose answer is not looked at) and the ID of
// its configuration (the configuration's slot 4; a lot without one would be
// a call through a null pointer, as in the game) as a uint32; the pothole
// map's count; for every pothole its cell key and a reference to its
// occupant. The answer is 1 when the stream has no error, else 0 (the whole
// of EAX).
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - kSerializablePart;
    Virtual<uint8_t>(stream, stream_slot::kUint16, uint32_t{kRecordVersion});
    Virtual<uint8_t>(stream, stream_slot::kUint32, At<uint32_t>(self, field::kLots + hashtable::kCount));
    HashIterator lots;
    CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, self + field::kLots, &lots);
    char* const table = lots.table;
    for (char* node = lots.node; node; node = NextNode16(table, node)) {
        char* const networkLot = At<char*>(node, hashnode::kValue);
        Virtual<uint8_t>(stream, stream_slot::kUint16, uint32_t{At<uint16_t>(node, hashnode::kKey)});
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(networkLot, lot::kOccupant));
        Virtual<uint8_t>(stream, stream_slot::kUint32,
                         Virtual<uint32_t>(At<void*>(networkLot, lot::kConfiguration), configuration_slot::kGetId));
    }
    char* const potholes = self + field::kPotholes;
    Virtual<uint8_t>(stream, stream_slot::kUint32, At<uint32_t>(potholes, hashtable::kCount));
    for (char* node = FirstNode(potholes); node; node = NextNode16(potholes, node)) {
        void* const occupant = At<void*>(node, hashnode::kValue);
        Virtual<uint8_t>(stream, stream_slot::kUint16, uint32_t{At<uint16_t>(node, hashnode::kKey)});
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, occupant);
    }
    return Virtual<uint8_t>(stream, stream_slot::kGetError) ? 0u : 1u;
}

// The serializable part's slot 4 (0x0060DAC0, ret 4: the stream; `this` is
// the object + 8): refused (AL 0) for any version but 2; the stream's state
// is not asked first. Then the count of lots, and that many times: a lot
// built in place (0x18 bytes from operator new; the vtable, the piece, the
// configuration and the count cleared and the forced byte 0; the word at
// +0xC is not set), referenced (its slot 1), the cell key read as a uint16,
// the piece read as a reference into the lot's +0x4
// (SC4Stream::ReadReference with the network occupant interface 0xA821EF94
// and 0), the configuration's ID read into the lot's +0xC, and the lot
// stored in the lot map under the key (operator[] 0x0060DA50, at its
// address; a lot that was there under the key is overwritten, not
// released). Then the count of potholes, and that many times a key, a
// reference (interface 0x09820792) and the store in the pothole map. Nothing
// is emptied first, and no read's answer is looked at. The answer after the
// last one is 1 when the stream has no error, else 0 (the whole of EAX). The
// lots' configurations are still null: Init resolves them.
//
// The game's locals that a failing stream leaves as they were: the version
// is read into the stack slot of the stream argument (the low word of the
// stream's address: the same here); the two counts share one stack word that
// is not set before the first read (so a stream that ends after the version
// makes the game loop over whatever the word held: zero here, and the test
// never feeds that); the key's word is not set either (zero here); the
// pothole's pointer is the stack slot of `this` (the part's address here
// too; ReadReference clears it before anything else).
//
// When operator new gives nothing the game calls a null lot's slot 1: the
// same here.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, stream_slot::kUint16, &version);
    if (version != kRecordVersion)
        return 0;
    uint32_t count = 0;
    Virtual<uint8_t>(stream, stream_slot::kUint32, &count);
    uint16_t key = 0;
    for (uint32_t i = 0; i < count; ++i) {
        char* const made = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, kLotSize);
        if (made) {
            At<std::uintptr_t>(made, 0x0) = addr::cSC4NetworkLot_vftable;
            At<void*>(made, lot::kOccupant) = nullptr;
            At<void*>(made, lot::kConfiguration) = nullptr;
            At<uint32_t>(made, lot::kRefCount) = 0;
            At<uint8_t>(made, lot::kForced) = 0;
        }
        Virtual<uint32_t>(made, lot_slot::kAddRef);
        Virtual<uint8_t>(stream, stream_slot::kUint16, &key);
        CallCdecl<uint8_t, void*, uint32_t, void**, uint32_t>(addr::SC4Stream_ReadReference, stream,
                                                              kIidNetworkOccupant, &At<void*>(made, lot::kOccupant),
                                                              0u);
        Virtual<uint8_t>(stream, stream_slot::kUint32, &At<uint32_t>(made, lot::kSavedConfigurationId));
        *CallThis<void**, uint16_t*>(addr::cSC4NetworkLotManager_LotMapIndex, self + field::kLots, &key) = made;
    }
    Virtual<uint8_t>(stream, stream_slot::kUint32, &count);
    void* occupant = part;
    char* const potholes = self + field::kPotholes;
    for (uint32_t i = 0; i < count; ++i) {
        Virtual<uint8_t>(stream, stream_slot::kUint16, &key);
        CallCdecl<uint8_t, void*, uint32_t, void**, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidPothole,
                                                              &occupant, 0u);
        *CallThis<void**, uint16_t*>(addr::cSC4NetworkLotManager_LotMapIndex, potholes, &key) = occupant;
    }
    return Virtual<uint8_t>(stream, stream_slot::kGetError) ? 0u : 1u;
}

// ====================================================================================
// simulators.netlot.object: the factory, the constructor and the destructors
// ====================================================================================

// One of the manager's four tables as the constructor makes it: the bucket
// vector's three words and the count cleared, then the table's constructor
// 0x00829BC0 with 100 buckets. The table's first word is never written.
void MakeTable(char* table)
{
    At<uint32_t>(table, hashtable::kBuckets) = 0;
    At<uint32_t>(table, hashtable::kBucketsEnd) = 0;
    At<uint32_t>(table, hashtable::kBucketsCapacity) = 0;
    At<uint32_t>(table, hashtable::kCount) = 0;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, table, kBuckets);
}

// And as the destructor lets one go: the nodes freed by `clear` (0x008EBA60,
// or 0x00584660 for the configuration map), then the bucket array when
// there is one.
void DropTable(char* table, std::uintptr_t clear)
{
    CallThis<void>(clear, table);
    if (void* const buckets = At<void*>(table, hashtable::kBuckets))
        Free(buckets);
}

// The constructor (0x0060EE50, no arguments; reached only by the factory's
// jump), every store in the game's order. The part at +4 and the part at +8
// first get the vtables of their base classes (0x00A81174 and 0x00A80784),
// then the three of the class are stored. The lot map and the configuration
// map are made; the forced configuration is null; the cell iterator at
// +0x38: the step 1, the rectangle 0x80000000 twice and 0x7FFFFFFF twice,
// the current cell 0, 0, the state byte 0 (and 2 if the cell 0, 0 were
// outside the rectangle: the game has the test; the byte at +0x39 is never
// written); the pothole map; the reference count and the initialised byte
// cleared; the waiting set. Nothing is subscribed and no service is asked:
// that is Init's. The game's EAX is the object.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, kMessagePart) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, kSerializablePart) = addr::SC4SimGrid_VtableBase;
    SetVtables(self);
    MakeTable(self + field::kLots);
    MakeTable(self + field::kConfigurations);
    At<void*>(self, field::kForced) = nullptr;
    char* const cells = self + field::kIterator;
    At<int32_t>(cells, iterator::kStep) = 1;
    At<int32_t>(cells, iterator::kLowX) = iterator::kResetLow;
    At<int32_t>(cells, iterator::kLowZ) = iterator::kResetLow;
    At<int32_t>(cells, iterator::kHighX) = iterator::kResetHigh;
    At<int32_t>(cells, iterator::kHighZ) = iterator::kResetHigh;
    At<int32_t>(cells, iterator::kX) = 0;
    At<int32_t>(cells, iterator::kZ) = 0;
    At<uint8_t>(cells, iterator::kState) = 0;
    if (At<int32_t>(cells, iterator::kLowX) > 0 || At<int32_t>(cells, iterator::kLowZ) > 0 ||
        At<int32_t>(cells, iterator::kHighX) < 0 || At<int32_t>(cells, iterator::kHighZ) < 0)
        At<uint8_t>(cells, iterator::kState) = 2;
    MakeTable(self + field::kPotholes);
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    MakeTable(self + field::kPending);
    return self;
}

// The destructor (0x0060E480, no arguments; the deleting destructor is its
// only caller): the three vtables stored again; the waiting set's nodes and
// buckets freed, then the pothole map's; the configuration forced for the
// next piece released when there is one (the word keeps its pointer); the
// configuration map's vectors, nodes and buckets freed; last the lot map's
// nodes and buckets. Shutdown is not called, and nothing a table holds is
// released here: the lots, the configurations and the potholes are
// Shutdown's.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    DropTable(self + field::kPending, addr::SC4Uint32HashSet_Clear);
    DropTable(self + field::kPotholes, addr::SC4Uint32HashSet_Clear);
    if (void* const forced = At<void*>(self, field::kForced))
        ReleaseOf(forced);
    DropTable(self + field::kConfigurations, addr::SC4PtrVectorHashMap_Clear);
    DropTable(self + field::kLots, addr::SC4Uint32HashSet_Clear);
}

// Slot 15 (0x0060EF20, ret 4: the flags; Release calls it with 1): the
// destructor at its address, and with bit 0 of the flags' low byte the
// memory freed (operator delete). The answer is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4NetworkLotManager_Destroy, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// The factory (0x0060F4F0; cdecl, no arguments; the class registration at
// 0x00460443 hands its address to the framework with the class ID): 0x88
// bytes from operator new, null when there are none, else the constructor
// (the game jumps to it; here a call at its address) and its answer, the
// object, with a reference count of 0.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    void* const raw = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, kObjectSize);
    if (!raw)
        return nullptr;
    return CallThis<void*>(addr::cSC4NetworkLotManager_cSC4NetworkLotManager, raw);
}

// ====================================================================================
// Installation
// ====================================================================================

const Detour kLife[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Init, Init),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Shutdown, Shutdown),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_LoadConfigurations, LoadConfigurations),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_OnConfigurationResource, OnConfigurationResource),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_AddConfiguration, AddConfiguration),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_LoadLotConfiguration, LoadLotConfiguration),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotConfiguration_cSC4NetworkLotConfiguration, ConfigurationConstruct),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotConfiguration_DeletingDestructor, ConfigurationDeletingDestructor),
};

const Detour kSave[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Write, Write),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Read, Read),
};

const Detour kObject[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Create, Create),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_cSC4NetworkLotManager, Construct),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Destroy, Destroy),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_DeletingDestructor, DeletingDestructor),
};

bool InstallLife(registry::Mode) { return InstallDetours(kLife, "life"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.netlot.own: the copies, taken at the first construction or
// destruction that wants them. That is a city's, so long after every
// PostAppInit replacement: the primary table is copied with whatever
// simulators.netlot.slots put in its slot 13 (ours, or the game's address
// when that group is off or the slot is on the skip list), word for word.
// Then the entries that hold a function of the object group directly are
// pointed at ours: slot 15 of the primary table, the deleting destructor
// (one entry, unless it is on the skip list). Every other entry stays the
// game's address or adjustor thunk, which the other groups' detours still
// decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkLotManager_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(kMessageVtable));
    g_vtable8.Build(reinterpret_cast<const void*>(kSerializableVtable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8);
    }
    log::Write("netlot own: vtables copied (16, 4 and 6 slots), %u entries redirected", redirected);
}

// simulators.netlot.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.netlot.life", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_Init,
                        "Our network lot manager life: Init, Shutdown, LoadConfigurations with its resource "
                        "callback, AddConfiguration and LoadLotConfiguration, and the network lot configuration's "
                        "constructor and deleting destructor (8 detours; src/core/netlot_life.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.netlot.save", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_Write,
                        "Our network lot manager save record: Write and Read (version 2; 2 detours; "
                        "src/core/netlot_life.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.netlot.object", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_cSC4NetworkLotManager,
                        "Our network lot manager factory, constructor, destructor and deleting destructor (4 "
                        "detours; src/core/netlot_life.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.netlot.own", "simulators/netlot", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_vftable,
                        "Our own network lot manager: objects carry our copies of the class's three vtables; only "
                        "has an effect with simulators.netlot.object (src/core/netlot_life.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::netlot_sim

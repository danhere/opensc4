// Our cSC4NetworkLotManager, third part (build 641; the class maps, the
// vtables and the fields are in src/core/netlot_sim.h): the lots themselves.
// The message target's DoMessage with the two functions that work the
// waiting set off (UpdateCell, ProcessPending); the lot of a cell kept,
// chosen, chosen again or removed (UpdateLot, CreateLot, RemoveLot); a lot's
// objects made into props and flora (BuildLotObjects, ObjectPosition,
// CreateProp, RedevelopLot); the potholes; and the styling of a connected
// group of pieces (UpdateConnectedPieces). Each is a detour at the game's
// address, and every call from one of them to another function of the class
// goes through the game's address or the vtable, as the game calls it, so a
// group that is off, or a function on the skip list, is the game's.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. Of the game's twelve functions
// only ProcessPending 0x0060FA10 keeps a register: it pushes ECX at its
// entry and pops it at its exit (always), and with an empty waiting set it
// touches EDX not at all (EAX is 0 then). Its two callers, DoMessage's
// 0x0060FE53 and 0x0060FFDE, pop their registers or load ECX from the stack
// right after it. The others make a call on every path before they return
// (Pothole's and UpdateLot's shortest paths leave ECX as the pothole table
// and the cell key). Callers of the twelve, all inside the class: 0x0060CFC2
// and 0x0060D546 (ObjectPosition), 0x0060D639, 0x0060DF1D and 0x00610002
// (BuildLotObjects), 0x0060E391 and 0x0060FEAF (RemoveLot), 0x0060E328 and
// 0x0060E39F (CreateLot), 0x0060FD5C (Pothole), 0x0060F424 and 0x0060F9FA
// (UpdateLot), 0x0060F554 (UpdateConnectedPieces, from the game's
// UpdatePieceStyle), 0x0060FA68 and 0x0060FD19 (UpdateCell), the two of
// ProcessPending; CreateProp, RedevelopLot and DoMessage are reached through
// vtables only. Each caller loads its registers anew after the call.
//
// Left as the game's:
//   - UpdatePieceStyle 0x0060F510, for good: the plugin
//     sc4-network-texture-test patches it at 0x0060F580. UpdateCell calls it
//     at its address;
//   - WriteLotConfiguration 0x0060E810 (slot 14; 1,560 bytes): only the lot
//     editor and LoadLotConfiguration's rewrite of an exemplar below version
//     2 call it, no coverage run reached it, and it was not read instruction
//     by instruction. The second part calls it through the vtable;
//   - the shared containers: the hash set of cell keys (find 0x0060D170,
//     count 0x0060D1B0, erase at a node 0x0060D1F0, insert 0x0060D270, the
//     growing insert 0x0060E210, the growth 0x00710DD0, the erase by key
//     0x00710910), the tables' constructor 0x00829BC0, clear 0x008EBA60 and
//     begin 0x004B0390, the cell iterators (0x0060CE90, 0x0060CEE0), the
//     pointer vector's reserve 0x0045D4F0 and growing insert 0x0051CA60.
//
// THE TABLES OF TEXTURES AND STYLES. 0x00AA7758 (a texture per zone type and
// wealth, 16 by 4 words) and 0x00AA7728 (a style byte per zone type and
// wealth column, 16 by 3) are written in place by another DLL. Only
// UpdateConnectedPieces reads them here (and the game's UpdatePieceStyle),
// from the game's memory at every call.
//
// THE GENERATOR. The lot manager's static generator at 0x00B44870 is drawn
// from in three places, each through the game's own SC4Random::Below
// 0x00913A06 (thiscall on the generator, one argument: the bound; `ret 4`;
// it takes one number of the stream and answers number x bound >> 32):
//   - CreateLot, 0x0060DE59: Below(the number of configurations that fit),
//     only when that number is not 0 and no configuration is forced;
//   - BuildLotObjects, 0x0060D4AC: per object of kind 1 without flag bit 0,
//     Below(the number of candidate types), only when that is not 0;
//   - BuildLotObjects, 0x0060D512: per object of kind 4, the same.
// Ours calls the same function on the same object with the same bound in the
// same places, so the stream moves exactly as with the game's code.
//
// Reference counts (every AddRef and Release of the twelve functions, in the
// game's order):
//   - DoMessage: the piece QueryInterface gave (0x89D3E9F3, 0x99EF1143) is
//     released once on every path after a successful QueryInterface; the
//     city lot (0xE8BDA2E7) once after ProcessPending;
//   - UpdateLot: the old lot's piece referenced before RemoveLot (which
//     releases the lot and so the lot's own reference to the piece) and
//     released after CreateLot;
//   - RemoveLot: the lot released once (the map's reference); the erase
//     frees the node only;
//   - CreateLot: the new lot referenced once (the map's; the map's entry is
//     overwritten without a release, and UpdateLot never calls CreateLot for
//     a cell that has an entry); the configuration referenced once for the
//     lot, the piece once for the lot; a forced configuration is released
//     once after the manager's word is cleared (the manager's reference);
//   - Pothole, add: the effect comes referenced from the framework and that
//     reference goes into the pothole map; the occupant filter is referenced
//     after its construction and released at the end; the occupant the
//     occupant manager found is released at the end; the piece QueryInterface
//     gave is released when not null; remove: the effect released once (the
//     map's);
//   - the others take and give no reference (CreateProp's prop and
//     BuildLotObjects' flora belong to their managers).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - a message's 0x10, 0x24, 0x28, 0x38, 0x4C and 0x80 none (0x009CEDE1,
//     0x0099BE66, 0x009D4714, 0x00995D93, 0x009D858F: plain ret);
//   - QueryInterface (slot 0 of whatever a message carries) two;
//   - a piece's 0x20, 0x24, 0x6C, 0x74 and 0x98 one (ret 4); 0x30, 0x4C,
//     0x5C, 0x60, 0x64, 0xA0, 0xC0 and 0xDC none; 0xE0 and 0xEC two (ret 8);
//     0x4 and 0x8 none;
//   - an occupant's 0x20 and 0x24 one (the address of three floats), 0x70
//     and 0x74 one;
//   - the city's 0xA0, 0x174 and 0x178 none; 0x17C and 0x184 four;
//   - the traffic network's 0x20 four (0x004B0B20, ret 0x10) and 0x90 one
//     (0x004B27E0, ret 4);
//   - the prop manager's 0x80 three (0x004A2120, ret 0xC) and 0x84 two
//     (0x004A3990, ret 8); the flora simulator's 0x44 three (0x006B1EA0, ret
//     0xC) and 0x50 two (0x006B24F0, ret 8); the lot configuration manager's
//     0x6C two (0x0048BA10, ret 8);
//   - the occupant manager's 0x50 two (0x0049EF60, ret 8), 0x54 three
//     (0x0049F240, ret 0xC) and 0x80 four (0x0049E690, ret 0x10); the
//     terrain's 0x34 two (0x00741260, ret 8);
//   - the framework's COM 0x0090DDF1 (cdecl, none) and its 0xC three; the
//     effect's 0xC none (0x005F8400), 0x18, 0x1C and 0x20 one (0x005F0780,
//     0x005EDA50, 0x005EDAA0: ret 4), 0x8 none; the occupant filter's
//     constructor 0x0061F530 one (the mask; ret 4), its 0x4 and 0x8 none;
//   - a city lot's 0x38 one (0x006BF320, ret 4) and 0x8 none; a network
//     lot's 0x4, 0x8, 0xC and 0x14 none; a configuration's 0x4 and 0x8 none;
//   - SC4Random::Below 0x00913A06 one (ret 4);
//   - the set's find 0x0060D170 two (the out pair, the key's address; ret
//     8), count 0x0060D1B0 one (ret 4), erase at a node 0x0060D1F0 one (the
//     address of a pair; ret 4), insert 0x0060D270 and growing insert
//     0x0060E210 two (the out record of nine bytes, the key's address; ret
//     8), growth 0x00710DD0 one (ret 4), erase by key 0x00710910 one (ret
//     4); the table's constructor 0x00829BC0 one (ret 4), clear 0x008EBA60
//     none, begin 0x004B0390 one (ret 4);
//   - the cell iterator's constructor 0x0060CE90 none, next 0x0060CEE0 two
//     (x out, z out; ret 8), this class's reset 0x0060C930 none and
//     rectangle 0x0060C960 four (ret 0x10);
//   - the pointer vector's reserve 0x0045D4F0 one (ret 4) and growing
//     insert 0x0051CA60 five (ret 0x14); operator new 0x005E55E0, the
//     allocation 0x0090CF54 and the free 0x0090CF63 (cdecl, one);
//   - this class: the manager's slots 0x20 CreateProp three (ret 0xC), 0x24
//     and 0x28 two (ret 8); ObjectPosition 0x0060C9B0 four (ret 0x10),
//     BuildLotObjects 0x0060D410 and RemoveLot 0x0060D8B0 one (ret 4),
//     CreateLot 0x0060DCE0 three (ret 0xC), Pothole 0x0060DF50 three (ret
//     0xC), UpdateLot 0x0060E2D0 five (ret 0x14), UpdatePieceStyle
//     0x0060F510 three (ret 0xC), UpdateCell 0x0060F980 two (ret 8),
//     ProcessPending 0x0060FA10 none, ConfigurationFits 0x0060CD30 seven
//     (ret 0x1C), the lot map's operator[] 0x0060DA50 one (ret 4).
#include "core/netlot_sim.h"

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

// The piece the traffic network has on a cell for a mask of networks (its
// slot 8 with the flag 1 the game always passes), or null.
void* PieceAt(void* traffic, uint32_t x, uint32_t z, uint32_t mask)
{
    return Virtual<void*>(traffic, traffic_slot::kGetPiece, x, z, mask, 1u);
}

// A cell key in the hash set of cell keys, by the game's growing insert
// 0x0060E210 (the table grows for one more first; a key that is there
// already stays alone).
void AddKey(char* table, uint32_t key)
{
    SetInsertResult result;
    CallThis<void*, SetInsertResult*, uint32_t*>(addr::SC4Uint16Set_ReserveAndInsert, table, &result, &key);
}

// The same as the game has it inline in UpdateConnectedPieces: the growth
// 0x00710DD0 for the count plus one, then the insert 0x0060D270.
void AddKeyInline(char* table, uint32_t key)
{
    CallThis<void, uint32_t>(addr::SC4Uint16Set_Reserve, table, At<uint32_t>(table, hashtable::kCount) + 1);
    SetInsertResult result;
    CallThis<void*, SetInsertResult*, uint32_t*>(addr::SC4Uint16Set_Insert, table, &result, &key);
}

// The cell of a piece: its occupant part's position (slot 8 of that part)
// handed to the city's slot 95.
void CellOfPiece(void* piece, int32_t& cellX, int32_t& cellZ)
{
    float position[3];
    void* const occupant = Virtual<void*>(piece, piece_slot::kAsOccupant);
    Virtual<void>(occupant, occupant_slot::kGetPosition, &position[0]);
    Virtual<void>(Global<void*>(addr::SC4_City), city_slot::kPositionToCell, position[0], position[2], &cellX,
                  &cellZ);
}

// ====================================================================================
// simulators.netlot.lots: ObjectPosition, CreateProp, BuildLotObjects,
// RedevelopLot, RemoveLot, CreateLot, UpdateLot
// ====================================================================================

// 0x0060C9B0 (ret 0x10: a lot, one of its configuration's objects, three
// floats out, the rotation out; `this` is not read; CreateProp's 0x0060CFC2
// and BuildLotObjects' 0x0060D546 call it): where an object of a lot stands
// in the city and how it is turned. In the game's order:
//   - the lot's piece (the lot's slot 3, asked anew before every use) gives
//     its cell (slot 59); the cell's corner is 16 x the cell as an unsigned
//     number made a float (the game adds 2^32 to a negative product), and
//     the far corner that plus 16.0 (the constant at 0x00A8D45C), each
//     rounded to a float;
//   - the piece's rotation (slot 24; the jump table at 0x0060CB68 has four
//     entries) turns the object's place (the floats at +4 and +0xC of the
//     object) on the cell:
//       0: x = far x - place x,    z = far z - place z
//       1: x = place z + corner x, z = far z - place x
//       2: x = place x + corner x, z = place z + corner z
//       3: x = far x - place z,    z = place x + corner z
//     any other rotation leaves x and z 0;
//   - the height is the piece's height at (x, z) (slot 56, in ST0) plus the
//     object's float at +8;
//   - a flipped piece (slot 25) mirrors x in the cell: x = (16 - 2 (x -
//     corner x)) + x, and the rotation is the entry (rotation + 4 x the
//     object's orientation byte at +2) of the table of sixteen words at
//     0x00AA78A0, read from the game's memory; else the rotation is
//     (orientation + rotation) modulo 4 (the game stores the piece's
//     rotation there first).
// The sums are the game's x87 ones: double intermediates in its operand
// order, each stored as a float. EAX is the address of the rotation.
int32_t* __fastcall ObjectPosition(void*, void*, void* lot, char* object, float* out, int32_t* outRotation)
{
    static bool logged = false;
    FirstCall("ObjectPosition", logged);
    int32_t cellX = 0, cellZ = 0;
    Virtual<void>(Virtual<void*>(lot, lot_slot::kGetNetworkOccupant), piece_slot::kGetOccupiedCell, &cellX, &cellZ);
    const float cornerX = static_cast<float>(static_cast<double>(static_cast<uint32_t>(cellX) << 4));
    const float cornerZ = static_cast<float>(static_cast<double>(static_cast<uint32_t>(cellZ) << 4));
    const float farX = static_cast<float>(static_cast<double>(cornerX) + 16.0);
    const float farZ = static_cast<float>(static_cast<double>(cornerZ) + 16.0);
    const uint32_t rotation =
        Virtual<uint8_t>(Virtual<void*>(lot, lot_slot::kGetNetworkOccupant), piece_slot::kGetRotation);
    const float placeX = At<float>(object, lotobject::kPlace);
    const float placeY = At<float>(object, lotobject::kPlace + 4);
    const float placeZ = At<float>(object, lotobject::kPlace + 8);
    switch (rotation) {
    case 0:
        out[0] = static_cast<float>(static_cast<double>(farX) - static_cast<double>(placeX));
        out[2] = static_cast<float>(static_cast<double>(farZ) - static_cast<double>(placeZ));
        break;
    case 1:
        out[0] = static_cast<float>(static_cast<double>(placeZ) + static_cast<double>(cornerX));
        out[2] = static_cast<float>(static_cast<double>(farZ) - static_cast<double>(placeX));
        break;
    case 2:
        out[0] = static_cast<float>(static_cast<double>(placeX) + static_cast<double>(cornerX));
        out[2] = static_cast<float>(static_cast<double>(placeZ) + static_cast<double>(cornerZ));
        break;
    case 3:
        out[0] = static_cast<float>(static_cast<double>(farX) - static_cast<double>(placeZ));
        out[2] = static_cast<float>(static_cast<double>(placeX) + static_cast<double>(cornerZ));
        break;
    default:
        out[0] = 0.0f;
        out[2] = 0.0f;
        break;
    }
    const double height =
        Virtual<double>(Virtual<void*>(lot, lot_slot::kGetNetworkOccupant), piece_slot::kGetAltitude, out[0], out[2]);
    out[1] = static_cast<float>(height + static_cast<double>(placeY));
    if (Virtual<uint8_t>(Virtual<void*>(lot, lot_slot::kGetNetworkOccupant), piece_slot::kGetFlip)) {
        double mirrored = static_cast<double>(out[0]) - static_cast<double>(cornerX);
        mirrored = mirrored + mirrored;
        mirrored = 16.0 - mirrored;
        mirrored = mirrored + static_cast<double>(out[0]);
        out[0] = static_cast<float>(mirrored);
        const uint32_t index = rotation + At<uint8_t>(object, lotobject::kOrientation) * 4u;
        *outRotation = Global<int32_t>(addr::cSC4NetworkLotManager_MirroredRotations + index * 4u);
    } else {
        *outRotation = static_cast<int32_t>(rotation);
        *outRotation = static_cast<int32_t>((At<uint8_t>(object, lotobject::kOrientation) + rotation) & 3u);
    }
    return outRotation;
}

// The word at 0x00B44868 (a static of the game that nothing writes: 0) as
// the three floats of a position before ObjectPosition fills them, as
// CreateProp and BuildLotObjects set theirs.
void ClearPosition(float* position)
{
    const uint32_t zero = Global<uint32_t>(addr::cSC4NetworkLotManager_ZeroWord);
    std::memcpy(&position[0], &zero, sizeof zero);
    std::memcpy(&position[1], &zero, sizeof zero);
    std::memcpy(&position[2], &zero, sizeof zero);
}

// Slot 8 (0x0060CF80, ret 0xC: a lot, the index of one of its
// configuration's objects, a prop type): the prop of that object. The
// object is the index-th of the vector the lot's slot 5 gives;
// ObjectPosition (at its address) gives the place and the rotation; with a
// type of 0 nothing more happens. The prop manager (0x00B43D10, not tested
// for null) makes a street prop of the type there (its slot 32); what it
// answers, the prop's occupant part, gets the object's ID (the word at
// +0x24) as its lot tag (slot 28) and, when the lot's piece is flipped (the
// piece's slot 25), the flag 0x80 (slot 29). EAX is the occupant part, or
// null. No random number is drawn here.
void* __fastcall CreateProp(char* self, void*, void* lot, uint32_t index, uint32_t type)
{
    static bool logged = false;
    FirstCall("CreateProp", logged);
    const Vector* const objects = Virtual<const Vector*>(lot, lot_slot::kGetObjectArray);
    char* const object = objects->begin + index * configuration::kObjectSize;
    float position[3];
    ClearPosition(position);
    int32_t rotation = 0;
    CallThis<int32_t*, void*, char*, float*, int32_t*>(addr::cSC4NetworkLotManager_ObjectPosition, self, lot, object,
                                                       &position[0], &rotation);
    if (type == 0)
        return nullptr;
    void* const prop = Virtual<void*>(Global<void*>(addr::cSC4PropManager_Global), props_slot::kAddStreetProp, type,
                                      &position[0], rotation);
    if (!prop)
        return nullptr;
    Virtual<void>(prop, occupant_slot::kSetLotTag, At<uint32_t>(object, lotobject::kId));
    if (Virtual<uint8_t>(Virtual<void*>(lot, lot_slot::kGetNetworkOccupant), piece_slot::kGetFlip))
        Virtual<void>(prop, occupant_slot::kSetFlag, occupant_slot::kMirroredFlag);
    return prop;
}

// The number of objects of a vector of lot configuration objects, as the
// game divides (signed, toward zero).
uint32_t ObjectCount(const Vector* objects)
{
    return static_cast<uint32_t>(static_cast<int32_t>(objects->end - objects->begin) /
                                 static_cast<int32_t>(configuration::kObjectSize));
}

// 0x0060D410 (ret 4: a lot; CreateLot's 0x0060DF1D, RedevelopLot's
// 0x0060D639 and DoMessage's 0x00610002 call it): the lot's objects made
// into props and flora. The vector of objects is the lot's slot 5 (nothing
// is built without one); the flora simulator's pointer (0x00B43D80) is read
// once before the walk. Per object, by its index, the count read again
// after every object:
//   - kind 1 without bit 0 of the flag byte at +1: the candidate types from
//     the lot configuration manager (0x00B43D18, slot 27, into a vector of
//     its own). With candidates, ONE DRAW: SC4Random::Below(the number of
//     candidates) on the generator 0x00B44870 (0x0060D4AC), and the
//     manager's own slot 8 (CreateProp) with the lot, the index and the
//     drawn type. The vector's memory is freed when it has any;
//   - kind 4: the candidates the same way. With candidates, ONE DRAW the
//     same way (0x0060D512), then ObjectPosition (at its address) and the
//     flora simulator's slot 17 with the drawn type, the place and 1; the
//     flora occupant it answers gets the object's INDEX as its lot tag
//     (slot 28; a prop gets the object's ID). The rotation is not used.
//   - any other kind: nothing, and no draw.
// At the end the traffic network (the global 0x00B43D54), when there is
// one, is told of the lot's piece (its slot 36), when the lot has one.
void __fastcall BuildLotObjects(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("BuildLotObjects", logged);
    const Vector* const objects = Virtual<const Vector*>(lot, lot_slot::kGetObjectArray);
    if (objects) {
        void* const flora = Global<void*>(addr::SC4_FloraSimulator43D80);
        for (uint32_t index = 0; index < ObjectCount(objects); ++index) {
            char* const object = objects->begin + index * configuration::kObjectSize;
            const uint8_t kind = At<uint8_t>(object, lotobject::kKind);
            if (kind == lotobject::kProp && !(At<uint8_t>(object, lotobject::kFlags) & 1)) {
                Vector types = {nullptr, nullptr, nullptr};
                Virtual<void>(Global<void*>(addr::SC4_Service43D18), lotconfigs_slot::kGetObjectIdArray, object,
                              &types);
                const int32_t count = static_cast<int32_t>(types.end - types.begin) >> 2;
                if (count != 0) {
                    const uint32_t drawn =
                        CallThis<uint32_t, int32_t>(addr::SC4Random_Below,
                                                    reinterpret_cast<void*>(addr::cSC4NetworkLotManager_Random), count);
                    const uint32_t type = At<uint32_t>(types.begin, drawn * 4);
                    Virtual<void*>(self, slot::kCreateProp, lot, index, type);
                }
                if (types.begin)
                    Free(types.begin);
            } else if (kind == lotobject::kFlora) {
                Vector types = {nullptr, nullptr, nullptr};
                Virtual<void>(Global<void*>(addr::SC4_Service43D18), lotconfigs_slot::kGetObjectIdArray, object,
                              &types);
                const int32_t count = static_cast<int32_t>(types.end - types.begin) >> 2;
                if (count != 0) {
                    const uint32_t drawn =
                        CallThis<uint32_t, int32_t>(addr::SC4Random_Below,
                                                    reinterpret_cast<void*>(addr::cSC4NetworkLotManager_Random), count);
                    const uint32_t type = At<uint32_t>(types.begin, drawn * 4);
                    float position[3];
                    ClearPosition(position);
                    int32_t rotation;
                    CallThis<int32_t*, void*, char*, float*, int32_t*>(addr::cSC4NetworkLotManager_ObjectPosition, self,
                                                                       lot, object, &position[0], &rotation);
                    if (void* const plant = Virtual<void*>(flora, flora_slot::kAddFlora, type, &position[0], 1u))
                        Virtual<void>(plant, occupant_slot::kSetLotTag, index);
                }
                if (types.begin)
                    Free(types.begin);
            }
        }
    }
    void* const traffic = Global<void*>(addr::SC4_Service43D54);
    if (!traffic)
        return;
    if (void* const piece = Virtual<void*>(lot, lot_slot::kGetNetworkOccupant))
        Virtual<void>(traffic, traffic_slot::kOnLotBuilt, piece);
}

// Slot 12 (0x0060D5E0, ret 0xC: x, z, a lot): the lot's objects built
// anew. The prop manager (0x00B43D10) removes the street props of the cell
// (its slot 33 with the low bytes of x and of z), the flora simulator
// (0x00B43D80) the flora in the cell's rectangle (its slot 20 with the
// rectangle x, z, x, z as given, not cut to bytes, and 0), and
// BuildLotObjects (at its address) builds the lot's objects. Neither
// service is tested for null. EAX is not set.
void __fastcall RedevelopLot(char* self, void*, int32_t x, int32_t z, void* lot)
{
    static bool logged = false;
    FirstCall("RedevelopLot", logged);
    Virtual<void>(Global<void*>(addr::cSC4PropManager_Global), props_slot::kRemoveStreetPropsFromTile,
                  static_cast<uint32_t>(x) & 0xFFu, static_cast<uint32_t>(z) & 0xFFu);
    int32_t rectangle[4] = {x, z, x, z};
    Virtual<void>(Global<void*>(addr::SC4_FloraSimulator43D80), flora_slot::kRemoveFloraInRect, &rectangle[0], 0u);
    CallThis<void, void*>(addr::cSC4NetworkLotManager_BuildLotObjects, self, lot);
}

// 0x0060D8B0 (ret 4: a piece; UpdateLot's 0x0060E391 and DoMessage's
// 0x0060FEAF call it): the lot of a piece taken away. The piece's cell
// (its position through the city's slot 95); the street props of the cell
// removed (the prop manager's slot 33 with the cell as it came, not cut to
// bytes) and the flora in the cell's rectangle (the flora simulator's slot
// 20 with 0); then the cell's key, ((x's low byte) << 8) | z cut to 16
// bits, is looked up in the lot map: a lot there is released (its slot 2)
// and the key erased (0x00710910, which frees the node). The props and the
// flora go also when the cell has no lot. EAX is not set.
void __fastcall RemoveLot(char* self, void*, void* piece)
{
    static bool logged = false;
    FirstCall("RemoveLot", logged);
    int32_t cellX = 0, cellZ = 0;
    CellOfPiece(piece, cellX, cellZ);
    Virtual<void>(Global<void*>(addr::cSC4PropManager_Global), props_slot::kRemoveStreetPropsFromTile, cellX, cellZ);
    int32_t rectangle[4] = {cellX, cellZ, cellX, cellZ};
    Virtual<void>(Global<void*>(addr::SC4_FloraSimulator43D80), flora_slot::kRemoveFloraInRect, &rectangle[0], 0u);
    char* const lots = self + field::kLots;
    uint32_t key = ((static_cast<uint32_t>(cellX) & 0xFFu) << 8) | static_cast<uint32_t>(cellZ);
    const uint16_t key16 = static_cast<uint16_t>(key);
    for (char* found = BucketHead(lots, key16); found; found = At<char*>(found, hashnode::kNext))
        if (At<uint16_t>(found, hashnode::kKey) == key16) {
            ReleaseOf(At<void*>(found, hashnode::kValue));
            CallThis<uint32_t, uint32_t*>(addr::SC4LinkTable_Erase, lots, &key);
            return;
        }
}

// 0x0060DCE0 (ret 0xC: a piece, a zone type, a wealth; UpdateLot's
// 0x0060E328 and 0x0060E39F call it): a lot for a piece. In the game's
// order:
//   - the configurations of the piece's ID (the piece's slot 23) in the
//     configuration map; without an entry and without a forced
//     configuration (+0x34) nothing happens;
//   - the piece's cell (as RemoveLot);
//   - a forced configuration is the one. Else the candidates: a vector of
//     the function's own, reserved for as many as the entry has
//     (0x0045D4F0), and every configuration of the entry, in the
//     entry's order, that ConfigurationFits (at its address) says fits the
//     cell, the zone, the wealth, the piece's rotation (slot 24) and its
//     flip (slot 25; asked first, both anew for every configuration) is
//     appended. With none the vector is freed and nothing happens. Else
//     ONE DRAW: SC4Random::Below(the number that fit) on the generator
//     0x00B44870 (0x0060DE59) picks one, and the vector is freed;
//   - the lot: 0x18 bytes from operator new built in place (the vtable
//     0x00AA78E0; piece, configuration and count cleared; the forced byte
//     0), referenced once (the game calls slot 1 of what operator new gave
//     without a test); the configuration referenced and stored, the piece
//     referenced and stored;
//   - the lot stored in the lot map under the cell's key (operator[]
//     0x0060DA50 at its address; an entry that is there is overwritten, not
//     released), and BuildLotObjects (at its address);
//   - when a configuration is forced (the word is read again): the lot's
//     forced byte is set, the manager's word cleared and the configuration
//     released.
// EAX is not set.
void __fastcall CreateLot(char* self, void*, void* piece, int32_t zone, int32_t wealth)
{
    static bool logged = false;
    FirstCall("CreateLot", logged);
    const uint32_t pieceId = Virtual<uint32_t>(piece, piece_slot::kGetPieceId);
    char* entry = BucketHead(self + field::kConfigurations, pieceId);
    while (entry && At<uint32_t>(entry, hashnode::kKey) != pieceId)
        entry = At<char*>(entry, hashnode::kNext);
    if (!entry && !At<void*>(self, field::kForced))
        return;
    int32_t cellX = 0, cellZ = 0;
    CellOfPiece(piece, cellX, cellZ);
    void* chosen = At<void*>(self, field::kForced);
    if (!chosen) {
        const Vector* const all = reinterpret_cast<const Vector*>(entry + hashnode::kValue);
        const int32_t count = static_cast<int32_t>(all->end - all->begin) >> 2;
        Vector fitting = {nullptr, nullptr, nullptr};
        CallThis<void, int32_t>(addr::PtrVector_Reserve, &fitting, count);
        for (int32_t i = 0; i < count; ++i) {
            char* const begin = all->begin;
            const uint32_t flip = Virtual<uint32_t>(piece, piece_slot::kGetFlip);
            const uint32_t rotation = Virtual<uint32_t>(piece, piece_slot::kGetRotation);
            if (!CallThis<uint8_t, void*, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t>(
                    addr::cSC4NetworkLotManager_ConfigurationFits, self, At<void*>(begin, i * 4), cellX, cellZ, zone,
                    wealth, rotation, flip))
                continue;
            char* const from = all->begin + i * 4;
            if (fitting.end != fitting.capacity) {
                if (fitting.end)
                    At<void*>(fitting.end, 0) = At<void*>(from, 0);
                fitting.end += 4;
            } else {
                int32_t tag = i;
                CallThis<void, char*, char*, int32_t*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &fitting,
                                                                           fitting.end, from, &tag, 1u, 1u);
            }
        }
        const int32_t fits = static_cast<int32_t>(fitting.end - fitting.begin) >> 2;
        if (fits == 0) {
            if (fitting.begin)
                Free(fitting.begin);
            return;
        }
        const uint32_t drawn = CallThis<uint32_t, int32_t>(
            addr::SC4Random_Below, reinterpret_cast<void*>(addr::cSC4NetworkLotManager_Random), fits);
        chosen = At<void*>(fitting.begin, drawn * 4);
        if (fitting.begin)
            Free(fitting.begin);
    }
    char* const made = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, kLotSize);
    if (made) {
        At<std::uintptr_t>(made, 0) = addr::cSC4NetworkLot_vftable;
        At<void*>(made, lot::kOccupant) = nullptr;
        At<void*>(made, lot::kConfiguration) = nullptr;
        At<uint32_t>(made, lot::kRefCount) = 0;
        At<uint8_t>(made, lot::kForced) = 0;
    }
    AddRefOf(made);
    void* const oldConfiguration = At<void*>(made, lot::kConfiguration);
    if (chosen != oldConfiguration) {
        if (chosen)
            AddRefOf(chosen);
        At<void*>(made, lot::kConfiguration) = chosen;
        if (oldConfiguration)
            ReleaseOf(oldConfiguration);
    }
    void* const oldPiece = At<void*>(made, lot::kOccupant);
    if (piece != oldPiece) {
        AddRefOf(piece);
        At<void*>(made, lot::kOccupant) = piece;
        if (oldPiece)
            ReleaseOf(oldPiece);
    }
    uint32_t key = ((static_cast<uint32_t>(cellX) & 0xFFu) << 8) | static_cast<uint32_t>(cellZ);
    *CallThis<void**, uint32_t*>(addr::cSC4NetworkLotManager_LotMapIndex, self + field::kLots, &key) = made;
    CallThis<void, void*>(addr::cSC4NetworkLotManager_BuildLotObjects, self, made);
    if (At<void*>(self, field::kForced)) {
        At<uint8_t>(made, lot::kForced) = 1;
        if (void* const forced = At<void*>(self, field::kForced)) {
            At<void*>(self, field::kForced) = nullptr;
            ReleaseOf(forced);
        }
    }
}

// 0x0060E2D0 (ret 0x14: a piece, x, z, a zone type, a wealth; UpdateCell's
// 0x0060F9FA and UpdateConnectedPieces' 0x0060F424 call it): the lot of a
// cell brought up to date. The cell's key (x's low byte, all of z, cut to
// 16 bits) in the lot map:
//   - no lot: CreateLot (at its address) for the piece when the piece is
//     usable (its slot 19);
//   - a lot whose forced byte is set stays;
//   - a lot whose configuration still fits (ConfigurationFits at its
//     address, with the piece's rotation and flip as CreateLot asks them)
//     stays; a lot without a configuration counts as not fitting;
//   - else the lot is chosen again, for the LOT's piece (+4; nothing when
//     it has none): the piece is referenced, RemoveLot and CreateLot (both
//     at their addresses) run for it with the zone and the wealth, and the
//     piece is released. CreateLot does not ask whether that piece is
//     usable.
// EAX is not set.
void __fastcall UpdateLot(char* self, void*, void* piece, int32_t x, int32_t z, int32_t zone, int32_t wealth)
{
    static bool logged = false;
    FirstCall("UpdateLot", logged);
    const uint16_t key = CellKey(static_cast<uint32_t>(x), static_cast<uint32_t>(z));
    char* found = BucketHead(self + field::kLots, key);
    while (found && At<uint16_t>(found, hashnode::kKey) != key)
        found = At<char*>(found, hashnode::kNext);
    if (!found) {
        if (Virtual<uint8_t>(piece, piece_slot::kIsUsable))
            CallThis<void, void*, int32_t, int32_t>(addr::cSC4NetworkLotManager_CreateLot, self, piece, zone, wealth);
        return;
    }
    char* const networkLot = At<char*>(found, hashnode::kValue);
    if (At<uint8_t>(networkLot, lot::kForced))
        return;
    if (At<void*>(networkLot, lot::kConfiguration)) {
        const uint32_t flip = Virtual<uint32_t>(piece, piece_slot::kGetFlip);
        const uint32_t rotation = Virtual<uint32_t>(piece, piece_slot::kGetRotation);
        if (CallThis<uint8_t, void*, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t>(
                addr::cSC4NetworkLotManager_ConfigurationFits, self, At<void*>(networkLot, lot::kConfiguration), x, z,
                zone, wealth, rotation, flip))
            return;
    }
    void* const lotPiece = At<void*>(networkLot, lot::kOccupant);
    if (!lotPiece)
        return;
    AddRefOf(lotPiece);
    CallThis<void, void*>(addr::cSC4NetworkLotManager_RemoveLot, self, lotPiece);
    CallThis<void, void*, int32_t, int32_t>(addr::cSC4NetworkLotManager_CreateLot, self, lotPiece, zone, wealth);
    ReleaseOf(lotPiece);
}

// ====================================================================================
// simulators.netlot.messages: UpdateCell, ProcessPending, DoMessage
// ====================================================================================

// 0x0060F980 (ret 8: x, z; ProcessPending's 0x0060FA68 and DoMessage's
// 0x0060FD19 call it): one cell brought up to date. The piece of a surface
// network (mask 0x1F4F) the traffic network (the city's slot 40; the city
// 0x00B43CEC is not tested for null) has on the cell; nothing without one
// or when it has any flag of 0x80200000 (its slot 9). Then the game's
// UpdatePieceStyle 0x0060F510, AT ITS ADDRESS (another DLL patches it), with
// x, z and the piece. Then, unless the piece has the flag 0x40 (slot 8: a
// piece of the network whose pieces are styled as a group, which
// UpdatePieceStyle hands to UpdateConnectedPieces): the manager's own slot
// 10 (the wealth) and then slot 9 (the zone type) for the cell, and UpdateLot
// (at its address) with them. EAX is not set.
void __fastcall UpdateCell(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("UpdateCell", logged);
    void* const traffic = Virtual<void*>(Global<void*>(addr::SC4_City), city_slot::kTrafficNetwork);
    void* const piece = Virtual<void*>(traffic, traffic_slot::kGetPiece, x, z, kSurfaceMask, 1u);
    if (!piece)
        return;
    if (Virtual<uint8_t>(piece, piece_slot::kHasAnyNetworkFlag, kNoUpdateFlags))
        return;
    CallThis<void, int32_t, int32_t, void*>(addr::cSC4NetworkLotManager_UpdatePieceStyle, self, x, z, piece);
    if (Virtual<uint8_t>(piece, piece_slot::kHasNetworkFlag, kGroupedNetworkFlag))
        return;
    const int32_t wealth = Virtual<int32_t>(self, slot::kGetWealthForNetwork, x, z);
    const int32_t zone = Virtual<int32_t>(self, slot::kGetZoneTypeForNetwork, x, z);
    CallThis<void, void*, int32_t, int32_t, int32_t, int32_t>(addr::cSC4NetworkLotManager_UpdateLot, self, piece, x,
                                                              z, zone, wealth);
}

// 0x0060FA10 (no arguments; DoMessage's 0x0060FE53 and 0x0060FFDE call
// it): the waiting set (+0x74) worked off. While its count is not 0: the
// first node of the table (the first bucket that holds one, the bucket
// count read again after every empty bucket; the game reads the key
// through a null pointer should a table with a count have no node, and so
// does this), UpdateCell (at its address) with the key's high byte as x and
// its low byte as z, and the key erased (0x00710910). UpdateCell can take
// keys out of the set itself (UpdateConnectedPieces erases every piece of a
// group), the cell's own among them. The game keeps ECX; EAX is 0 at the
// end.
uint32_t __fastcall ProcessPending(char* self, void*)
{
    static bool logged = false;
    FirstCall("ProcessPending", logged);
    char* const pending = self + field::kPending;
    while (At<uint32_t>(pending, hashtable::kCount) != 0) {
        char* const node = FirstNode(pending);
        uint32_t key = At<uint16_t>(node, hashnode::kKey);
        CallThis<void, uint32_t, uint32_t>(addr::cSC4NetworkLotManager_UpdateCell, self, key >> 8, key & 0xFFu);
        CallThis<uint32_t, uint32_t*>(addr::SC4LinkTable_Erase, pending, &key);
    }
    return 0;
}

// Whether a piece is one the manager keeps lots for: any flag of the
// surface networks (slot 9 with 0x1F4F) and not the flag 0x200000 (slot 8).
bool KeepsLots(void* piece)
{
    if (!Virtual<uint8_t>(piece, piece_slot::kHasAnyNetworkFlag, kSurfaceMask))
        return false;
    return !Virtual<uint8_t>(piece, piece_slot::kHasNetworkFlag, kNoLotFlag);
}

// The message part's slot 3 (0x0060FC30, ret 4: a message; `part` is the
// object + 4). AL is 1 whatever the message. By the message's type (its
// slot 4):
//   0x89D3E9F3, a piece's usable flag changed: the object the message
//     carries (slot 32) is asked for the network occupant interface
//     0xA821EF94; nothing when it refuses. A piece that keeps lots
//     (KeepsLots): when the message's first data word (slot 9) is not 0,
//     UpdateCell for the piece's cell (its position through the city's slot
//     95); when it is 0, RemoveLot. The piece is released.
//   0x99EF1143, an occupant was removed: the message's first data word as a
//     pointer (slot 10) is asked the same way; a piece that keeps lots gets
//     RemoveLot; the piece is released.
//   0x07699455, cells changed: the rectangle the second data word points at
//     (slot 14: low x, low z, high x, high z) grown by one cell on every
//     side; every cell of it inside the city (the city's slots 93 and 94,
//     asked once each, signed compares) goes into the waiting set (the
//     growing insert 0x0060E210), row by row from the low corner, x first;
//     then ProcessPending. The game walks a cell iterator of its own on the
//     stack here (constructed by 0x0060CE90, the rectangle set by 0x0060C960
//     at its address) with the stepping written out in place; the walk
//     below is that code.
//   0xE8BDA2E7, a city lot's state changed: the object the message carries
//     is asked for the lot interface 0xE818FCED; nothing when it refuses.
//     The lot's cell rectangle (its slot 14) grown by one on every side goes
//     to a cell iterator (reset by 0x0060C930, the rectangle by 0x0060C960,
//     both at their addresses) and every cell the game's 0x0060CEE0 gives,
//     at its address, that is inside the city goes into the waiting set;
//     then ProcessPending, and the lot is released. What 0x0060CEE0 gives is
//     its business: it is a walk along a rectangle's border that begins at
//     the iterator's current cell, which the reset left at (0, 0) and the
//     rectangle does not move (so the first row walked is z = 0 from x = 0
//     to the high x, then the two end columns of every row up to the high
//     z, whose row is walked whole).
//   0x69247DC8, a pothole: Pothole with the first data word as x, the
//     second as z and whether the third (slot 19) is not 0; the game asks
//     the third first, then the second, then the first.
//   0xA9B6C2D6, a lot's objects are to be built: BuildLotObjects with the
//     first data word as a pointer (a network lot).
// The functions of the class are called at their addresses with the object
// (part - 4). Nothing the message gives is tested for null.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - kMessagePart;
    switch (Virtual<uint32_t>(message, message_slot::kGetType)) {
    case 0x89D3E9F3: {
        void* const carried = Virtual<void*>(message, message_slot::kGetIGZUnknown);
        void* piece = nullptr;
        if (!Virtual<uint8_t>(carried, 0x0, kIidNetworkOccupant, &piece))
            return 1;
        if (KeepsLots(piece)) {
            if (Virtual<uint32_t>(message, message_slot::kGetData1) != 0) {
                int32_t cellX = 0, cellZ = 0;
                CellOfPiece(piece, cellX, cellZ);
                CallThis<void, int32_t, int32_t>(addr::cSC4NetworkLotManager_UpdateCell, self, cellX, cellZ);
            } else {
                CallThis<void, void*>(addr::cSC4NetworkLotManager_RemoveLot, self, piece);
            }
        }
        ReleaseOf(piece);
        return 1;
    }
    case 0x99EF1143: {
        void* const removed = Virtual<void*>(message, message_slot::kGetVoid1);
        void* piece = nullptr;
        if (!Virtual<uint8_t>(removed, 0x0, kIidNetworkOccupant, &piece))
            return 1;
        if (KeepsLots(piece))
            CallThis<void, void*>(addr::cSC4NetworkLotManager_RemoveLot, self, piece);
        ReleaseOf(piece);
        return 1;
    }
    case 0x69247DC8: {
        const uint32_t add = Virtual<uint32_t>(message, message_slot::kGetData3) != 0;
        const uint32_t z = Virtual<uint32_t>(message, message_slot::kGetData2);
        const uint32_t x = Virtual<uint32_t>(message, message_slot::kGetData1);
        CallThis<void, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkLotManager_Pothole, self, x, z, add);
        return 1;
    }
    case 0x07699455: {
        const int32_t* const changed = Virtual<const int32_t*>(message, message_slot::kGetData2);
        int32_t cells[iterator::kSize / 4];
        CallThis<void*>(addr::SC4CellIterator_Construct, cells);
        int32_t x = changed[0] - 1;
        int32_t z = changed[1] - 1;
        CallThis<int32_t, int32_t, int32_t, int32_t, int32_t>(addr::cSC4NetworkLotManager_CellIteratorSetRectangle,
                                                              cells, x, z, changed[2] + 1, changed[3] + 1);
        uint8_t state = 0;
        At<int32_t>(cells, iterator::kX) = x;
        if (x < At<int32_t>(cells, iterator::kLowX) || z < At<int32_t>(cells, iterator::kLowZ) ||
            x > At<int32_t>(cells, iterator::kHighX) || z > At<int32_t>(cells, iterator::kHighZ))
            state = 2;
        void* const city = Global<void*>(addr::SC4_City);
        const int32_t sizeX = Virtual<int32_t>(city, city_slot::kCellCountX);
        const int32_t sizeZ = Virtual<int32_t>(city, city_slot::kCellCountZ);
        const int32_t step = At<int32_t>(cells, iterator::kStep);
        for (;;) {
            if (state == 2)
                break;
            if (state == 0) {
                state = 1;
            } else {
                x += step;
                At<int32_t>(cells, iterator::kX) = x;
                if (x > At<int32_t>(cells, iterator::kHighX)) {
                    x = At<int32_t>(cells, iterator::kLowX);
                    z += step;
                    At<int32_t>(cells, iterator::kX) = x;
                    if (z > At<int32_t>(cells, iterator::kHighZ))
                        break;
                }
            }
            if (x < 0 || z < 0 || x >= sizeX || z >= sizeZ)
                continue;
            AddKey(self + field::kPending, ((static_cast<uint32_t>(x) & 0xFFu) << 8) | static_cast<uint32_t>(z));
        }
        CallThis<uint32_t>(addr::cSC4NetworkLotManager_ProcessPending, self);
        return 1;
    }
    case 0xE8BDA2E7: {
        void* const carried = Virtual<void*>(message, message_slot::kGetIGZUnknown);
        void* cityLot = nullptr;
        if (!Virtual<uint8_t>(carried, 0x0, kIidCityLot, &cityLot))
            return 1;
        int32_t bounds[4];
        Virtual<void>(cityLot, citylot_slot::kGetBoundingRect, &bounds[0]);
        --bounds[0];
        --bounds[1];
        ++bounds[2];
        ++bounds[3];
        int32_t cells[iterator::kSize / 4];
        CallThis<char*>(addr::cSC4NetworkLotManager_CellIteratorReset, cells);
        CallThis<int32_t, int32_t, int32_t, int32_t, int32_t>(addr::cSC4NetworkLotManager_CellIteratorSetRectangle,
                                                              cells, bounds[0], bounds[1], bounds[2], bounds[3]);
        void* const city = Global<void*>(addr::SC4_City);
        const int32_t sizeX = Virtual<int32_t>(city, city_slot::kCellCountX);
        const int32_t sizeZ = Virtual<int32_t>(city, city_slot::kCellCountZ);
        int32_t x = 0, z = 0;
        while (CallThis<uint8_t, int32_t*, int32_t*>(addr::SC4RectIterator_Next, cells, &x, &z)) {
            if (x < 0 || z < 0 || x >= sizeX || z >= sizeZ)
                continue;
            AddKey(self + field::kPending, ((static_cast<uint32_t>(x) & 0xFFu) << 8) | static_cast<uint32_t>(z));
        }
        CallThis<uint32_t>(addr::cSC4NetworkLotManager_ProcessPending, self);
        ReleaseOf(cityLot);
        return 1;
    }
    case 0xA9B6C2D6:
        CallThis<void, void*>(addr::cSC4NetworkLotManager_BuildLotObjects, self,
                              Virtual<void*>(message, message_slot::kGetVoid1));
        return 1;
    default:
        return 1;
    }
}

// ====================================================================================
// simulators.netlot.pothole: Pothole
// ====================================================================================

// 0x0060DF50 (ret 0xC: x, z, a byte; DoMessage's 0x0060FD5C is the only
// caller): a pothole put on a cell or taken off it. The cell's key (x's low
// byte, all of z; the set's functions read 16 bits) in the pothole map
// (+0x58), looked up by the set's find 0x0060D170.
//
// Add (the byte is not 0), nothing when the cell has one already:
//   - a visual effect prop by its class ID 0x2977AA49 and interface
//     0x09820792 from the framework's COM (0x0090DDF1, its slot 3); nothing
//     more when that fails;
//   - the place: the cell's position from the city (slot 97: x and z out)
//     and the terrain's height there (0x00B43CF4, slot 13);
//   - the piece on the cell: an occupant filter for the surface networks
//     (0x10 bytes from operator new, the network manager's filter
//     constructor 0x0061F530 with 0x1F4F, referenced when made) is handed
//     to the occupant manager's (0x00B43D0C) slot 32 with the cell. When
//     that finds an occupant, it is asked for the network occupant
//     interface (the answer is not tested), and of the piece: the height at
//     the place (slot 56) replaces the terrain's; the place moves by the
//     pair of signed bytes at 0x00AA7910 + 2 x the piece's active sides
//     (slot 40; the table is read from the game's memory), each times 4.0
//     (the constant at 0x00AA790C), x then z; and a piece whose flags
//     (slot 12) are, in their low 13 bits, exactly 2 (rail alone) makes it
//     a rail pothole. The piece is released when it is not null;
//   - the effect is named (its slot 6) with the game's string
//     "pothole_rail" (0x00AA78FC) for rail and "pothole" (0x00A9F2B0) else,
//     also when no occupant was found; its occupant part (slot 3) gets the
//     place (slot 9) and goes into the occupant manager (slot 20 with 0);
//     the effect is started (slot 7 with 1) and stored in the pothole map
//     under the key (the map's operator[] 0x0060DA50 at its address) with
//     the reference the framework gave;
//   - the filter and the occupant found are released.
// Remove (the byte is 0), nothing when the cell has none: the node is
// erased (0x0060D1F0), the effect stopped (slot 8 with 1), its occupant
// part taken out of the occupant manager (slot 21 with 1 and 0) and the
// effect released.
// Nothing here draws from a generator. EAX is not set.
void __fastcall Pothole(char* self, void*, uint32_t x, uint32_t z, uint32_t add)
{
    static bool logged = false;
    FirstCall("Pothole", logged);
    char* const potholes = self + field::kPotholes;
    uint32_t key = ((x & 0xFFu) << 8) | z;
    HashIterator found;
    CallThis<void*, HashIterator*, uint32_t*>(addr::SC4Uint16Set_Find, potholes, &found, &key);
    if (static_cast<uint8_t>(add) == 0) {
        if (!found.node)
            return;
        void* const effect = At<void*>(found.node, hashnode::kValue);
        HashIterator where = {found.node, found.table};
        CallThis<void, HashIterator*>(addr::SC4Uint16Set_Erase, potholes, &where);
        Virtual<void>(effect, effect_slot::kStop, 1u);
        void* const occupants = Global<void*>(addr::SC4_NetworkService);
        void* const occupant = Virtual<void*>(effect, effect_slot::kAsOccupant);
        Virtual<void>(occupants, occupants_slot::kRemoveOccupant, occupant, 1u, 0u);
        ReleaseOf(effect);
        return;
    }
    if (found.node)
        return;
    void* effect = nullptr;
    void* const com = CallCdecl<void*>(addr::RZGetCOM);
    if (!Virtual<uint8_t>(com, com_slot::kGetClassObject, kPotholeClass, kIidPothole, &effect))
        return;
    float position[3];
    Virtual<void>(Global<void*>(addr::SC4_City), city_slot::kCellToPosition, x, z, &position[0], &position[2]);
    position[1] = static_cast<float>(
        Virtual<double>(Global<void*>(addr::SC4_Terrain), terrain_slot::kGetAltitude, position[0], position[2]));
    void* occupantHere = nullptr;
    void* filter = nullptr;
    if (void* const memory = CallCdecl<void*, std::size_t>(addr::SC4_operator_new, 0x10)) {
        filter = CallThis<void*, uint32_t>(addr::cSC4NetworkManager_NetworkFilterConstruct, memory, kSurfaceMask);
        if (filter)
            AddRefOf(filter);
    }
    std::uintptr_t name = addr::cSC4NetworkLotManager_PotholeName;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_NetworkService), occupants_slot::kGetFirstByCell, &occupantHere, x,
                         z, filter)) {
        void* piece = nullptr;
        Virtual<uint8_t>(occupantHere, 0x0, kIidNetworkOccupant, &piece);
        position[1] = static_cast<float>(Virtual<double>(piece, piece_slot::kGetAltitude, position[0], position[2]));
        const uint32_t sides = Virtual<uint8_t>(piece, piece_slot::kGetActiveEdges);
        const int32_t offsetX = Global<int8_t>(addr::cSC4NetworkLotManager_PotholeOffsets + sides * 2u);
        const int32_t offsetZ = Global<int8_t>(addr::cSC4NetworkLotManager_PotholeOffsets + sides * 2u + 1u);
        position[0] = static_cast<float>(static_cast<double>(offsetX) * 4.0 + static_cast<double>(position[0]));
        position[2] = static_cast<float>(static_cast<double>(offsetZ) * 4.0 + static_cast<double>(position[2]));
        const bool rail = (Virtual<uint32_t>(piece, piece_slot::kGetNetworkFlags) & kNetworkTypeBits) == kRailOnly;
        if (piece)
            ReleaseOf(piece);
        if (rail)
            name = addr::cSC4NetworkLotManager_PotholeRailName;
    }
    Virtual<void>(effect, effect_slot::kSetEffectName, reinterpret_cast<const char*>(name));
    Virtual<void>(Virtual<void*>(effect, effect_slot::kAsOccupant), occupant_slot::kSetPosition, &position[0]);
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    void* const occupant = Virtual<void*>(effect, effect_slot::kAsOccupant);
    Virtual<void>(occupants, occupants_slot::kInsertOccupant, occupant, 0u);
    Virtual<void>(effect, effect_slot::kStart, 1u);
    *CallThis<void**, uint32_t*>(addr::cSC4NetworkLotManager_LotMapIndex, potholes, &key) = effect;
    if (filter)
        ReleaseOf(filter);
    if (occupantHere)
        ReleaseOf(occupantHere);
}

// ====================================================================================
// simulators.netlot.styles: UpdateConnectedPieces
// ====================================================================================

// A hash set of cell keys of the function's own (0x14 bytes, as the
// manager's: netlot_sim.h), made as the game makes its three on the stack:
// the words cleared, then the table's constructor 0x00829BC0 with 100
// buckets; and let go as the game does: the nodes freed (0x008EBA60), then
// the buckets.
struct KeySet {
    uint32_t words[hashtable::kSize / 4];
    char* Table() { return reinterpret_cast<char*>(&words[0]); }
};

void MakeKeySet(KeySet& set)
{
    set = KeySet{};
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, set.Table(), kBuckets);
}

void DropKeySet(KeySet& set)
{
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set.Table());
    if (void* const buckets = At<void*>(set.Table(), hashtable::kBuckets))
        Free(buckets);
}

// 0x0060EF40 (ret 8: x, z; the game's UpdatePieceStyle calls it, at
// 0x0060F554, for a usable piece with the network flag 0x40): one style and
// one kind of lot for a whole connected run of pieces. In the game's order:
//   - three sets of cell keys (to visit, the group, seen and not of the
//     group), then 16 counters of zone types and 4 of wealths from the
//     game's allocator (0x0090CF54), cleared; the city's sizes (slot 94,
//     then slot 93);
//   - the piece of the network 0x40 on the cell (the traffic network, the
//     city's slot 40, its slot 8); without one everything is freed and
//     nothing happens. Its slot 55 gives the byte the group shares;
//   - the flood: the cell's key goes into the first set. While that set has
//     a key: its first key (the table's begin 0x004B0390) is taken out
//     (0x0060D1F0); the cell's piece of the network 0x40 with the same slot
//     55 byte joins the group (any other cell goes into the third set): its
//     zone type and wealth (the manager's own slots 9 and 10) are counted,
//     and for each of its four sides whose byte of the piece's edge value
//     for network type 6 (slot 38) has one of its low three bits, the
//     neighbour cell is put into the first set unless it is in the group or
//     in the third set. THE NEIGHBOUR is computed in 16 bits (the low words
//     of the offset tables at 0x00AA7858 and 0x00AA7878, read from the
//     game's memory, added to x and to z), is passed over when x or z is
//     negative or ABOVE the city's size (so x equal to the size passes, as
//     in the game), and its key is (z's 16 bits) | (x's low byte << 8): on
//     a city 256 cells wide a neighbour at x = 256 becomes x = 0 of the same
//     row, and one at z = 256 the cell (x | 1, 0). Copied as it is;
//   - the group's zone type: the type of 1 to 15 counted most often, the
//     lowest of equals (0 when none was counted; type 0's counter is never
//     looked at); its wealth: the wealth of 1 to 3 counted most often, the
//     lowest of equals (0 when none);
//   - the texture is the word (wealth + 4 x zone type) of the table at
//     0x00AA7758 and the style the byte (3 x zone type + the wealth less
//     one, or 0 for wealth 0) of the table at 0x00AA7728: BOTH READ FROM THE
//     GAME'S MEMORY, here, at every call;
//   - every cell of the group, in the group set's order: its piece, when
//     there is one and it is usable (slot 19), gets the texture (slot 29)
//     and the style (slot 27), and UpdateLot (at its address) with the
//     group's zone type and wealth; the cell's key is erased from the
//     waiting set (+0x74) either way;
//   - the counters and the three sets are freed.
// EAX is not set.
void __fastcall UpdateConnectedPieces(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("UpdateConnectedPieces", logged);
    void* const traffic = Virtual<void*>(Global<void*>(addr::SC4_City), city_slot::kTrafficNetwork);
    KeySet toVisit, group, others;
    MakeKeySet(toVisit);
    MakeKeySet(group);
    MakeKeySet(others);
    int32_t* const zones = CallCdecl<int32_t*, std::size_t>(addr::SC4_stl_allocate, kZoneTypes * 4);
    for (uint32_t i = 0; i < kZoneTypes; ++i)
        zones[i] = 0;
    int32_t* const wealths = CallCdecl<int32_t*, std::size_t>(addr::SC4_stl_allocate, kWealths * 4);
    for (uint32_t i = 0; i < kWealths; ++i)
        wealths[i] = 0;
    const int32_t sizeZ = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountZ);
    const int32_t sizeX = Virtual<int32_t>(Global<void*>(addr::SC4_City), city_slot::kCellCountX);
    void* const first = PieceAt(traffic, x, z, kGroupedNetworkFlag);
    if (!first) {
        if (wealths)
            Free(wealths);
        if (zones)
            Free(zones);
        DropKeySet(others);
        DropKeySet(group);
        DropKeySet(toVisit);
        return;
    }
    const uint8_t kind = Virtual<uint8_t>(first, piece_slot::kIsIntersection);
    AddKeyInline(toVisit.Table(), ((x & 0xFFu) << 8) | z);
    while (At<uint32_t>(toVisit.Table(), hashtable::kCount) != 0) {
        HashIterator next;
        CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, toVisit.Table(), &next);
        const uint32_t key = At<uint16_t>(next.node, hashnode::kKey);
        HashIterator again;
        CallThis<void, HashIterator*>(addr::SC4HashTable_Begin, toVisit.Table(), &again);
        HashIterator where = {again.node, again.table};
        CallThis<void, HashIterator*>(addr::SC4Uint16Set_Erase, toVisit.Table(), &where);
        const uint32_t cellX = key >> 8;
        const uint32_t cellZ = key & 0xFFu;
        void* const piece = PieceAt(traffic, cellX, cellZ, kGroupedNetworkFlag);
        if (!piece || Virtual<uint8_t>(piece, piece_slot::kIsIntersection) != kind) {
            AddKeyInline(others.Table(), key);
            continue;
        }
        AddKeyInline(group.Table(), key);
        ++zones[Virtual<uint32_t>(self, slot::kGetZoneTypeForNetwork, cellX, cellZ)];
        ++wealths[Virtual<uint32_t>(self, slot::kGetWealthForNetwork, cellX, cellZ)];
        uint32_t edges = Virtual<uint32_t>(piece, piece_slot::kGetEdgeTypesFlag, kGroupedNetworkType);
        for (uint32_t side = 0; side < kNeighbours; ++side, edges >>= 8) {
            if (!(edges & 0x7u))
                continue;
            const uint32_t stepX = Global<uint16_t>(addr::cSC4NetworkLotManager_NeighborDx + side * 4u);
            const uint32_t stepZ = Global<uint16_t>(addr::cSC4NetworkLotManager_NeighborDz + side * 4u);
            const uint16_t x16 = static_cast<uint16_t>(stepX + cellX);
            const uint16_t z16 = static_cast<uint16_t>(stepZ + cellZ);
            const int32_t nextX = static_cast<int16_t>(x16);
            const int32_t nextZ = static_cast<int16_t>(z16);
            if (nextX < 0 || nextZ < 0 || nextX > sizeX || nextZ > sizeZ)
                continue;
            const uint16_t neighbour16 = static_cast<uint16_t>(z16 | ((x16 & 0xFFu) << 8));
            uint32_t inGroup = 0;
            for (char* node = BucketHead(group.Table(), neighbour16); node; node = At<char*>(node, hashnode::kNext))
                if (At<uint16_t>(node, hashnode::kKey) == neighbour16)
                    ++inGroup;
            if (inGroup != 0)
                continue;
            uint32_t neighbour = neighbour16;
            if (CallThis<uint32_t, uint32_t*>(addr::SC4Uint16Set_Count, others.Table(), &neighbour) != 0)
                continue;
            AddKey(toVisit.Table(), neighbour);
        }
    }
    int32_t zone = 0;
    int32_t most = 0;
    for (int32_t type = 1; type < static_cast<int32_t>(kZoneTypes); ++type)
        if (zones[type] > most) {
            zone = type;
            most = zones[type];
        }
    int32_t wealth = 0;
    most = 0;
    if (wealths[1] > 0) {
        wealth = 1;
        most = wealths[1];
    }
    if (wealths[2] > most) {
        wealth = 2;
        most = wealths[2];
    }
    if (wealths[3] > most)
        wealth = 3;
    const uint32_t texture = Global<uint32_t>(addr::cSC4NetworkLotManager_TextureTable +
                                              static_cast<uint32_t>(wealth + zone * 4) * 4u);
    const int32_t column = wealth > 0 ? wealth - 1 : 0;
    const uint32_t style =
        Global<uint8_t>(addr::cSC4NetworkLotManager_StyleTable + static_cast<uint32_t>(zone * 3 + column));
    for (char* node = FirstNode(group.Table()); node; node = NextNode16(group.Table(), node)) {
        uint32_t key = At<uint16_t>(node, hashnode::kKey);
        const uint32_t cellX = key >> 8;
        const uint32_t cellZ = key & 0xFFu;
        void* const piece = PieceAt(traffic, cellX, cellZ, kGroupedNetworkFlag);
        if (piece && Virtual<uint8_t>(piece, piece_slot::kIsUsable)) {
            Virtual<void>(piece, piece_slot::kSetUnderTexture, texture);
            Virtual<void>(piece, piece_slot::kSetVariation, style);
            CallThis<void, void*, uint32_t, uint32_t, int32_t, int32_t>(addr::cSC4NetworkLotManager_UpdateLot, self,
                                                                        piece, cellX, cellZ, zone, wealth);
        }
        CallThis<uint32_t, uint32_t*>(addr::SC4LinkTable_Erase, self + field::kPending, &key);
    }
    Free(wealths);
    if (zones)
        Free(zones);
    DropKeySet(others);
    DropKeySet(group);
    DropKeySet(toVisit);
}

// ====================================================================================
// Installation
// ====================================================================================

const Detour kMessageDetours[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_DoMessage, DoMessage),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_UpdateCell, UpdateCell),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_ProcessPending, ProcessPending),
};

const Detour kLotDetours[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_UpdateLot, UpdateLot),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_CreateLot, CreateLot),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_RemoveLot, RemoveLot),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_BuildLotObjects, BuildLotObjects),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_ObjectPosition, ObjectPosition),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_CreateProp, CreateProp),
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_RedevelopLot, RedevelopLot),
};

const Detour kPotholeDetours[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_Pothole, Pothole),
};

const Detour kStyleDetours[] = {
    OPENSC4_NETLOT_DETOUR(addr::cSC4NetworkLotManager_UpdateConnectedPieces, UpdateConnectedPieces),
};

bool InstallMessages(registry::Mode) { return InstallDetours(kMessageDetours, "messages"); }
bool InstallLots(registry::Mode) { return InstallDetours(kLotDetours, "lots"); }
bool InstallPothole(registry::Mode) { return InstallDetours(kPotholeDetours, "pothole"); }
bool InstallStyles(registry::Mode) { return InstallDetours(kStyleDetours, "styles"); }

const bool g_registered[] = {
    registry::Register({"simulators.netlot.messages", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_DoMessage,
                        "Our network lot manager messages: DoMessage (six messages), UpdateCell and ProcessPending; "
                        "the game's UpdatePieceStyle is called at its address (3 detours; "
                        "src/core/netlot_lots.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.netlot.lots", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_UpdateLot,
                        "Our network lots: UpdateLot, CreateLot, RemoveLot, BuildLotObjects, ObjectPosition, "
                        "CreateProp and RedevelopLot; the draws go through the game's generator functions (7 "
                        "detours; src/core/netlot_lots.cpp)",
                        false, InstallLots}),
    registry::Register({"simulators.netlot.pothole", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_Pothole,
                        "Our network lot manager potholes: Pothole (1 detour; src/core/netlot_lots.cpp)", false,
                        InstallPothole}),
    registry::Register({"simulators.netlot.styles", "simulators/netlot", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkLotManager_UpdateConnectedPieces,
                        "Our styling of a connected group of network pieces: UpdateConnectedPieces; the texture and "
                        "style tables are read from the game's memory (1 detour; src/core/netlot_lots.cpp)",
                        false, InstallStyles}),
};

} // namespace
} // namespace opensc4::netlot_sim

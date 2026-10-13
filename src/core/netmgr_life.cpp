// Our cSC4NetworkManager, second part (build 641; the class map, the vtables
// in their true order and the fields are in src/core/netmgr_sim.h): life and
// save. The factory, the constructor and the two destructors; Init and
// Shutdown; LoadRemapping with the four callbacks the ini reader calls and
// the inserts they use; the helpers that let go of the tools, the crews and
// the retrofit list; Write and Read. Each is a detour at the game's address,
// and every call from one of them to another goes through the game's
// address, so a group that is off, or a function on the skip list, is the
// game's.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that keep
// both on some path: Init 0x00610C10 for a manager that is initialised
// already and Shutdown 0x00611EB0 for one that is not (their `push ecx`
// only makes room for a local, and `pop ecx` puts ECX back while the local
// was not written); ShutdownCrews 0x00610B80 without crews; ReleaseTools
// 0x00610880 for an empty range or slots that hold the value already; the
// retrofit list's clear 0x00611A20 for an empty list while the allocator is
// cached. None of their callers reads a register it did not load again
// after the call: Init and Shutdown are reached through the vtable and from
// the destructor (0x00612019, then `lea edi, [esi+0xBC]`); the helpers from
// 0x00611AA4, 0x00611F20, 0x00611F40, 0x00612055, 0x00612593, 0x00612600,
// 0x00612DD5, 0x006130D3, 0x00613128, 0x00613436, 0x0061399B and 0x006139BB;
// the callbacks from the ini reader's loop 0x0091DAC9 (`add esp, 0xC`, then
// EBX and its own frame).
//
// Left as the game's: the tools' constructors (called at their addresses);
// the string, the ini reader and the resource record's opener; the hash
// table's constructor 0x00829BC0, clear 0x008EBA60 and resize 0x004F0940
// (shared with other classes); the vectors' growing inserts 0x0051CA60,
// 0x00638C60 and, inside 0x00627100, 0x0082CDF0; sscanf 0x009EF536 and the
// name compare 0x00A04487 (the game's C library).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - operator new 0x005E55E0 and operator delete 0x005E5620 (cdecl, one);
//     the free 0x0090CF63 and the allocation 0x0090CF54 (cdecl, one);
//   - the table's constructor 0x00829BC0 one (the bucket hint, ret 4), clear
//     0x008EBA60 none (plain ret), resize 0x004F0940 one (the element
//     count, ret 4);
//   - the retrofit list's node allocation 0x00611240 two (1, 0; ret 8); the
//     node allocator's slot 0x10 one (the node: the only push before each
//     call at 0x00611A7C and 0x00611AF3);
//   - the framework's slot 0x14 three (class, IID, out; pushed together at
//     0x00610C3A); the language service's slot 0x98 one (0;
//     GetLanguageUtility in gzcom-dll, used the same way by
//     src/core/ui_mysim_dialog_1.cpp) and the utility's slot 0x64 none
//     (GetLanguageRoadDrivingSide; between the two calls the one push at
//     0x00610C51 is all the frame gives, and the local's ESP offsets after
//     them fit); SC4::ReleaseRef2 0x00610830 none (this the local);
//   - the message server's slots 0x14 and 0x18 two (the target, the message
//     ID; two pushes before each of the 22 calls and no clean-up);
//   - a tool's, a piece's, a crew's and a record's 0x4 AddRef and 0x8
//     Release none; a crew's 0x14 none;
//   - the tools' constructors: 0x0062C5F0 one (the type, ret 4), 0x00648EC0,
//     0x006503C0 and 0x00653D60 none;
//   - SC4::OpenResourceRecord 0x005F9670 (cdecl, three: the key, the out
//     record, 0; `add esp, 0x20` at 0x006131CA takes them with the five of
//     the copy before); the ini reader's constructor 0x0091CF46 one (the
//     record, ret 4), ForEachEntry 0x0091D91C three (the section name, the
//     callback, its context; ret 0xC) and destructor 0x0091CDE3 none; the
//     string's constructor 0x00408480 one (the text, ret 4);
//   - sscanf 0x009EF536 (cdecl; text, format and 8, 6 or 4 addresses:
//     `add esp, 0x28`, `0x20`, `0x18`); the compare 0x00A04487 (cdecl, two);
//   - the 0x18-byte vector's push_back 0x00627100 one (the entry, ret 4);
//     the 0x10-byte vector's growing insert 0x00638C60 five (where, the
//     entry, a tag that is not read, 1, 1; ret 0x14); the pointer vector's
//     0x0051CA60 five (the same shape; ret 0x14);
//   - a stream's 0x1C and 0x24 one (the value when writing, its address
//     when reading), 0x48 none; SC4Stream::WriteReference 0x005D99A0 (cdecl,
//     two: the stream, the object), SC4Stream::ReadReference 0x005D9730
//     (cdecl, four: the stream, the IID, the out pointer, 0);
//   - this class: Shutdown 0x00611EB0, LoadRemapping 0x00613140,
//     ShutdownCrews, the list's clear 0x00611A20 and destroy 0x00611AA0 and
//     the destructor 0x00612000 none; the deleting destructor one (ret 4);
//     ReleaseTools 0x00610880 (cdecl, three); AddShadowEdgeRemap 0x006125B0
//     five (ret 0x14); AddPathRemap 0x00612610 four (ret 0x10); the table's
//     insert 0x00611890 two (ret 8); Write and Read one (ret 4).
#include "core/netmgr_sim.h"

#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace opensc4::netmgr_sim {
namespace {

void Free(void* memory)
{
    CallCdecl<void, void*>(addr::SC4_Free, memory);
}

// ====================================================================================
// simulators.netmgr.own: with it the objects carry our copies of the class's
// three vtables (25, 4 and 6 slots) instead of the game's tables. The copies
// are taken at the first use (BuildOwnVtables, with the installation: it
// needs the object group's detour list).
// ====================================================================================

bool g_own = false;
occ_vtables::VtableCopy<kSlots> g_vtable;
occ_vtables::VtableCopy<kMessageSlots> g_vtable4;
occ_vtables::VtableCopy<kSerializableSlots> g_vtable8;
void BuildOwnVtables();

// The three vtables the constructor and the destructor store, in the game's
// order (the game's, or with simulators.netmgr.own our copies of them).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4NetworkManager_vftable;
    At<std::uintptr_t>(self, kMessagePart) = g_own ? g_vtable4.Address() : addr::cSC4NetworkManager_vftable_4;
    At<std::uintptr_t>(self, kSerializablePart) = g_own ? g_vtable8.Address() : addr::cSC4NetworkManager_vftable_8;
}

// ====================================================================================
// simulators.netmgr.life: LoadRemapping, its callbacks and their inserts
// ====================================================================================

// The table's insert (0x00611890, ret 8: an out pair and the address of a
// value of five words: key, network type, base, mask, rotation; `this` is
// the table at the manager's +0x30; AddShadowEdgeRemap is its only caller):
// an insert that keeps equal keys. The key's bucket is the key modulo the
// bucket count (unsigned); the chain is searched for the first node with the
// key. A node of 0x18 bytes is allocated (0x0090CF54), its link cleared and
// the five words copied behind it (the game tests the copy's address for
// null first). With a node of the key the new one goes in right behind it;
// else at the head of the bucket (the bucket array is read again). The count
// at +0x10 goes up, and the out pair gets the node and the table. EAX is the
// out pair.
void** __fastcall InsertShadowEdge(char* table, void*, void** out, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("InsertShadowEdge", logged);
    const uint32_t key = value[0];
    const uint32_t index = key % BucketCount(table);
    char* const first = At<char*>(At<char*>(table, edge::kBuckets), index * 4);
    char* same = first;
    while (same && At<uint32_t>(same, edge::kKey) != key)
        same = At<char*>(same, edge::kNext);
    char* const node = CallCdecl<char*, std::size_t>(addr::SC4_stl_allocate, kEdgeNodeSize);
    At<char*>(node, edge::kNext) = nullptr;
    for (std::size_t i = 0; i < 5; ++i)
        At<uint32_t>(node, edge::kKey + 4 * i) = value[i];
    if (same) {
        At<char*>(node, edge::kNext) = At<char*>(same, edge::kNext);
        At<char*>(same, edge::kNext) = node;
    } else {
        At<char*>(node, edge::kNext) = first;
        At<char*>(At<char*>(table, edge::kBuckets), index * 4) = node;
    }
    ++At<uint32_t>(table, edge::kCount);
    out[1] = table;
    out[0] = node;
    return out;
}

// 0x006125B0 (ret 0x14: the network type, the key, the base, the mask and
// the rotation, of which the low byte is read): an edge into the table at
// +0x30. The table is resized for one element more (0x004F0940), then the
// value goes in through the insert at its address. The value's fifth word
// is the rotation byte and three bytes the game leaves as its stack had
// them (they land in the node at +0x15 to +0x17, which nothing reads): zero
// here. The game's EAX is the insert's out pair, a place in this function's
// own frame: not returned.
void __fastcall AddShadowEdgeRemap(char* self, void*, uint32_t type, uint32_t key, uint32_t base, uint32_t mask,
                                   uint32_t rotation)
{
    static bool logged = false;
    FirstCall("AddShadowEdgeRemap", logged);
    char* const table = self + field::kShadowEdges;
    uint32_t value[5] = {key, type, base, mask, rotation & 0xFFu};
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, table, At<uint32_t>(table, edge::kCount) + 1);
    void* placed[2];
    CallThis<void**, void**, uint32_t*>(addr::cSC4NetworkManager_InsertShadowEdge, table, placed, value);
}

// 0x00612610 (ret 0x10: match, mask, base, keep mask): a 0x10-byte entry
// appended to the vector at +0x5C, in place when there is room (the store
// is skipped for a null end, the game's placement test; the end is read
// again for the step) and through the game's growing insert 0x00638C60 when
// the vector is full (at the end, the entry, a tag the insert does not read
// (the game hands it the address of its first argument), one copy, at the
// end).
void __fastcall AddPathRemap(char* self, void*, uint32_t match, uint32_t mask, uint32_t base, uint32_t keep)
{
    static bool logged = false;
    FirstCall("AddPathRemap", logged);
    Vector* const paths = reinterpret_cast<Vector*>(self + field::kPathRemap);
    uint32_t entry[4] = {match, mask, base, keep};
    char* const end = paths->end;
    if (end != paths->capacity) {
        if (end)
            for (std::size_t i = 0; i < 4; ++i)
                At<uint32_t>(end, 4 * i) = entry[i];
        paths->end = paths->end + remap::kPathSize;
        return;
    }
    CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4Vector16_InsertOverflow, paths, end, entry,
                                                                    entry, 1u, 1u);
}

using Scan = int32_t(__cdecl*)(const char* text, const char* format, ...);

// The lines of "Texture remapping" and "Shadow remapping" (the format at
// 0x00AA7BA8): four hexadecimal words, three decimal numbers and one more
// hexadecimal word, separated by commas.
const char kRemapFormat[] = " %lx , %lx , %lx , %lx , %d , %d , %d , %lx";
// "Shadow edge remapping" (0x00AA7BD4): a name of up to 64 characters that
// ends at a comma or a blank, three hexadecimal words, two decimal numbers.
const char kEdgeFormat[] = " %64[^, ] , %lx , %lx , %lx , %d , %d";
// "UK path remapping" (0x00AA7BFC): four hexadecimal words.
const char kPathFormat[] = " %lx , %lx , %lx , %lx";

// The rotation byte of an entry: the low two bits of the line's rotation,
// and bit 7 when the line's mirror number is not zero.
uint8_t RotationByte(int32_t rotation, int32_t mirror)
{
    return static_cast<uint8_t>((mirror != 0 ? 0x80 : 0) + (static_cast<uint8_t>(rotation) & 3));
}

// What the two callbacks of the 0x18-byte entries share (0x00612F70 and
// 0x00612EB0 differ in the vector only): the value's text scanned (the
// game's sscanf) for match, mask, base, keep mask, rotation, mirror, go on
// and drop mask; a line that gives fewer than four values is left out; the
// four after the fourth are 0 when the line ends before them. The entry:
// +0 match, +4 mask, +8 base, +0xC keep mask, +0x10 drop mask, +0x14 the
// rotation byte, +0x15 whether go on is not zero; it is appended by the
// game's push_back 0x00627100. The entry's last two bytes are what the
// game's stack held (nothing reads them): zero here.
void AddRemapEntry(const GameString* value, char* vector)
{
    uint32_t match = 0, mask = 0, base = 0, keep = 0, drop = 0;
    int32_t rotation = 0, mirror = 0, goOn = 0;
    const int32_t given = reinterpret_cast<Scan>(addr::SC4_sscanf)(value->begin, kRemapFormat, &match, &mask, &base,
                                                                   &keep, &rotation, &mirror, &goOn, &drop);
    if (given < 4)
        return;
    alignas(4) char entry[remap::kSize] = {};
    At<uint32_t>(entry, remap::kMatch) = match;
    At<uint32_t>(entry, remap::kMask) = mask;
    At<uint32_t>(entry, remap::kBase) = base;
    At<uint32_t>(entry, remap::kKeep) = keep;
    At<uint32_t>(entry, remap::kDrop) = drop;
    At<uint8_t>(entry, remap::kRotation) = RotationByte(rotation, mirror);
    At<uint8_t>(entry, remap::kGoOn) = goOn != 0 ? 1 : 0;
    CallThis<void, char*>(addr::SC4CornerVector_PushBack, vector, entry);
}

// The ini reader's callback for a line of "Texture remapping" (0x00612F70;
// cdecl, three arguments as the reader's loop at 0x0091DAC1 pushes them: the
// line's key string, its value string and the context, here the manager;
// the key is not read): an entry of the vector at +0x44.
void __cdecl OnTextureRemapEntry(const GameString*, const GameString* value, char* self)
{
    static bool logged = false;
    FirstCall("OnTextureRemapEntry", logged);
    AddRemapEntry(value, self + field::kPreviewRemap);
}

// The same for "Shadow remapping" (0x00612EB0): the vector at +0x50.
void __cdecl OnShadowRemapEntry(const GameString*, const GameString* value, char* self)
{
    static bool logged = false;
    FirstCall("OnShadowRemapEntry", logged);
    AddRemapEntry(value, self + field::kShadowRemap);
}

// The callback for a line of "Shadow edge remapping" (0x00613030): a name,
// the edge key, the base, the mask, the rotation and the mirror number (the
// last two 0 when the line ends before them; fewer than four values: left
// out). The name is looked up in the thirteen English names (0x00AA845C)
// without regard to case (the game's 0x00A04487); a name that is none of
// them leaves the line out. Else the edge goes into the table through
// AddShadowEdgeRemap with the name's network type.
//
// The game's buffer for the name is the 64 bytes right under its return
// address, so a name of 64 characters puts its end mark into the return
// address; the buffer here has room for it.
void __cdecl OnShadowEdgeRemapEntry(const GameString*, const GameString* value, char* self)
{
    static bool logged = false;
    FirstCall("OnShadowEdgeRemapEntry", logged);
    char name[68];
    uint32_t key = 0, base = 0, mask = 0;
    int32_t rotation = 0, mirror = 0;
    const int32_t given = reinterpret_cast<Scan>(addr::SC4_sscanf)(value->begin, kEdgeFormat, name, &key, &base, &mask,
                                                                   &rotation, &mirror);
    if (given < 4)
        return;
    uint32_t type = 0;
    while (CallCdecl<int32_t, const char*, const char*>(
               addr::SC4_stricmp, name, Global<const char*>(addr::cSC4NetworkManager_EnglishNames + type * 4u)) != 0)
        if (++type >= kTypes)
            return;
    CallThis<void, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkManager_AddShadowEdgeRemap, self,
                                                                     type, key, base, mask,
                                                                     uint32_t{RotationByte(rotation, mirror)});
}

// The callback for a line of "UK path remapping" (0x006130E0): match, mask,
// base and keep mask; a line with fewer than the four is left out; else an
// entry of the vector at +0x5C through AddPathRemap.
void __cdecl OnPathRemapEntry(const GameString*, const GameString* value, char* self)
{
    static bool logged = false;
    FirstCall("OnPathRemapEntry", logged);
    uint32_t match = 0, mask = 0, base = 0, keep = 0;
    if (reinterpret_cast<Scan>(addr::SC4_sscanf)(value->begin, kPathFormat, &match, &mask, &base, &keep) < 4)
        return;
    CallThis<void, uint32_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkManager_AddPathRemap, self, match, mask,
                                                           base, keep);
}

// One section of the ini text: the section's name as a string of the game
// on the stack (0x00408480), the reader's ForEachEntry with the callback's
// address and the manager, then the string's end as the game has it inline
// (the base vtable stored, the characters freed).
void RunSection(void* reader, const char* name, std::uintptr_t callback, char* self)
{
    GameString section;
    CallThis<GameString*, const char*>(addr::SC4String_FromText, &section, name);
    CallThis<uint8_t, GameString*, std::uintptr_t, char*>(addr::cRZIniFile_ForEachEntry, reader, &section, callback,
                                                          self);
    char* const text = section.begin;
    section.vtable = addr::SC4String_Vtable;
    if (text)
        Free(text);
}

// 0x00613140 (no arguments; the constructor is its only caller): the remap
// tables read from the ini resource. First they are emptied: the vectors at
// +0x44 and +0x50 (the game copies the empty range at their end onto their
// start, 0x006240D0, which gives the start as the new end), the table at
// +0x30 (0x008EBA60: every node freed) and the vector at +0x5C (0x007257C0,
// the same). Then the record of the key (type 0, group 0x8A5971C5, instance
// 0x8A5993B9) is opened (0x005F9670). When it opens, the game's ini reader
// (0x3C bytes on the stack) is made over it and run over four sections, in
// this order, each with its callback at the game's address and the manager
// as context: "Texture remapping" 0x00612F70, "Shadow remapping"
// 0x00612EB0, "Shadow edge remapping" 0x00613030, "UK path remapping"
// 0x006130E0; then the reader is destroyed. Whether it opened or not, a
// record the opener left is released last.
void __fastcall LoadRemapping(char* self, void*)
{
    static bool logged = false;
    FirstCall("LoadRemapping", logged);
    Vector* const previews = reinterpret_cast<Vector*>(self + field::kPreviewRemap);
    previews->end = previews->begin;
    Vector* const shadows = reinterpret_cast<Vector*>(self + field::kShadowRemap);
    shadows->end = shadows->begin;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + field::kShadowEdges);
    Vector* const paths = reinterpret_cast<Vector*>(self + field::kPathRemap);
    paths->end = paths->begin;

    void* record = nullptr;
    uint32_t key[3] = {0u, kRemapGroup, kRemapInstance};
    if (CallCdecl<uint8_t, uint32_t*, void**, uint32_t>(addr::SC4_OpenResourceRecord, key, &record, 0u)) {
        alignas(4) uint8_t reader[0x3C];
        CallThis<void*, void*>(addr::cRZIniFile_ConstructFromRecord, reader, record);
        RunSection(reader, "Texture remapping", addr::cSC4NetworkManager_OnTextureRemapEntry, self);
        RunSection(reader, "Shadow remapping", addr::cSC4NetworkManager_OnShadowRemapEntry, self);
        RunSection(reader, "Shadow edge remapping", addr::cSC4NetworkManager_OnShadowEdgeRemapEntry, self);
        RunSection(reader, "UK path remapping", addr::cSC4NetworkManager_OnPathRemapEntry, self);
        CallThis<void>(addr::cRZIniFile_Destruct, reader);
    }
    if (record)
        ReleaseOf(record);
}

// ====================================================================================
// simulators.netmgr.life: Init, Shutdown and the helpers that let go
// ====================================================================================

// A new tool of a network type, as Init's thirteen blocks and GetNetworkTool
// make them: the pipe tool for 4, the power line tool for 5, the subway tool
// for 7, the general class with the type for the others.
void* MakeToolOf(uint32_t type)
{
    switch (type) {
    case kTypePipe:
        return MakeOwnTool(kPipeToolSize, addr::cSC4NetworkManager_PipeToolConstruct);
    case kTypePowerLine:
        return MakeOwnTool(kPowerLineToolSize, addr::cSC4NetworkManager_PowerLineToolConstruct);
    case kTypeSubway:
        return MakeOwnTool(kPipeToolSize, addr::cSC4NetworkManager_SubwayToolConstruct);
    default:
        return MakeTool(type);
    }
}

// A referenced pointer's assignment as the game has it inline: nothing when
// the slot holds the pointer already; else the new one referenced (when not
// null), stored, and the old one released (when not null).
void Assign(void** place, void* now)
{
    void* const old = *place;
    if (now == old)
        return;
    if (now)
        AddRefOf(now);
    *place = now;
    if (old)
        ReleaseOf(old);
}

// Slot 3 (0x00610C10, no arguments): once per object (the byte at +0x10,
// set first). The language service (class 0x441E5070, IID 0x0054B7D5) is
// fetched from the framework and asked for the current language's utility
// (slot 38 with 0), and that for the side of the road (slot 25): the byte at
// +0x11 is 1 when the answer's low byte is 0. The game does not test the
// service or the utility for null. Then, unless there is no message server
// (0x00B43CCC, read once): the part at +4 is subscribed to the eleven
// messages (kMessages, in that order), the byte at +0x12 is cleared and the
// thirteen tools are made in the order of their types, each allocated and
// constructed before the slot at +0x88 + 4 x type is read and assigned.
// Last the service is released (0x00610830). AL is 1; the rest of the
// game's EAX is not reproduced.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    void* language = nullptr;
    if (void* const framework = CallCdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, kLanguageClass, kIidLanguage, &language);
    void* const utility = Virtual<void*>(language, language_slot::kGetLanguageUtility, 0u);
    At<uint8_t>(self, field::kLeftHand) = Virtual<uint8_t>(utility, language_slot::kGetRoadDrivingSide) ? 0 : 1;
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        char* const target = self + kMessagePart;
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, server_slot::kAddNotification, target, message);
        At<uint8_t>(self, field::kBatching) = 0;
        for (uint32_t type = 0; type < kTypes; ++type) {
            void* const tool = MakeToolOf(type);
            Assign(&At<void*>(self, field::kTools + 4 * type), tool);
        }
    }
    CallThis<void>(addr::SC4_ReleaseRef2, &language);
    return 1;
}

// 0x00610880 (cdecl: first, last, the address of a tool pointer; Shutdown
// and DoMessage call it with the thirteen slots and the address of a null):
// every slot of the range is assigned the pointer, which is read again for
// each slot.
void __cdecl ReleaseTools(void** first, void** last, void** value)
{
    static bool logged = false;
    FirstCall("ReleaseTools", logged);
    for (; first != last; ++first)
        Assign(first, *value);
}

// 0x00610B80 (no arguments; DoMessage's 0x26D31EC2 is its only caller):
// while the vector at +0x7C holds a crew, the first crew's slot 5 is called
// (the crew unregisters itself there; one that did not would be called for
// ever, as in the game).
void __fastcall ShutdownCrews(char* self, void*)
{
    static bool logged = false;
    FirstCall("ShutdownCrews", logged);
    const Vector* const crews = reinterpret_cast<const Vector*>(self + field::kCrews);
    while (crews->begin != crews->end)
        Virtual<void>(At<void*>(crews->begin, 0), crew_slot::kShutdown);
}

// 0x00611A20 (no arguments; `this` is the list's word at the manager's
// +0x78): every node but the head node goes back to the node allocator
// cached at 0x00B45238 (its slot 4; the cache is filled from the framework
// when it is empty, as in the node allocation 0x00611240), each node's link
// read before it is freed; then the head node points at itself both ways.
// The pieces the nodes hold are not touched: the callers release them first.
void __fastcall ClearRetrofitList(ListNode** list, void*)
{
    static bool logged = false;
    FirstCall("ClearRetrofitList", logged);
    ListNode* node = (*list)->next;
    while (node != *list) {
        ListNode* const gone = node;
        node = node->next;
        Virtual<void>(NodeAllocatorAt(addr::cSC4NetworkManager_NodeAllocator), allocator_slot::kFree, gone);
    }
    (*list)->next = *list;
    (*list)->previous = *list;
}

// 0x00611AA0 (no arguments; `this` the list's word; the destructor is its
// only caller): the list cleared (at the clear's address) and the head node
// given back to the allocator. The word keeps the freed node's address.
void __fastcall DestroyRetrofitList(ListNode** list, void*)
{
    static bool logged = false;
    FirstCall("DestroyRetrofitList", logged);
    CallThis<void>(addr::cSC4NetworkManager_ClearRetrofitList, list);
    ListNode* const head = *list;
    Virtual<void>(NodeAllocatorAt(addr::cSC4NetworkManager_NodeAllocator), allocator_slot::kFree, head);
}

// Slot 4 (0x00611EB0, no arguments; also called by the destructor): nothing
// for a manager that is not initialised. Else the byte at +0x10 is cleared;
// every piece of the vector at +0x24 is released and the vector emptied
// (the game's erase of the whole range: its compare of the end with itself
// never takes the branch that moves entries); every piece of the retrofit
// list is released (a node's link is read after its piece's Release) and
// the list cleared (0x00611A20, at its address); the thirteen tool slots
// are assigned null (ReleaseTools, at its address); and unless there is no
// message server the part at +4 is taken off the eleven messages, in Init's
// order but for 0x26D31EC1, which here comes before 0x26D31EC2. The queue
// of removals and the crews are not touched. AL is 1.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    Vector* const managed = reinterpret_cast<Vector*>(self + field::kManaged);
    char* const end = managed->end;
    char* at = managed->begin;
    At<uint8_t>(self, field::kInitialized) = 0;
    for (; at != end; at += 4)
        ReleaseOf(At<void*>(at, 0));
    managed->end = managed->begin;
    ListNode* const head = At<ListNode*>(self, field::kRetrofit);
    for (ListNode* node = head->next; node != head; node = node->next)
        ReleaseOf(node->value);
    CallThis<void>(addr::cSC4NetworkManager_ClearRetrofitList, self + field::kRetrofit);
    void* none = nullptr;
    CallCdecl<void, char*, char*, void**>(addr::cSC4NetworkManager_ReleaseTools, self + field::kTools,
                                          self + field::kTools + 4 * kTypes, &none);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        char* const target = self + kMessagePart;
        for (const std::size_t i : {0u, 1u, 2u, 3u, 4u, 6u, 5u, 7u, 8u, 9u, 10u})
            Virtual<uint8_t>(server, server_slot::kRemoveNotification, target, kMessages[i]);
    }
    return 1;
}

// ====================================================================================
// simulators.netmgr.save: Write and Read
// ====================================================================================

// The serializable part's slot 3 (0x006111E0, ret 4: the stream; `this` is
// the object + 8): the version 1 as a uint16, the number of entries of the
// vector at +0x24 as a uint32 (the byte difference shifted right as a signed
// number), then every entry as a reference (SC4Stream::WriteReference, whose
// answer is not looked at; the vector's bounds are read again after the
// count). The answer is 1 when the stream has no error, else 0 (the whole of
// EAX).
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - kSerializablePart;
    Virtual<uint8_t>(stream, stream_slot::kUint16, uint32_t{kRecordVersion});
    const Vector* const managed = reinterpret_cast<const Vector*>(self + field::kManaged);
    Virtual<uint8_t>(stream, stream_slot::kUint32,
                     static_cast<uint32_t>(static_cast<int32_t>(managed->end - managed->begin) >> 2));
    char* at = managed->begin;
    char* const end = managed->end;
    for (; at != end; at += 4)
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(at, 0));
    return Virtual<uint8_t>(stream, stream_slot::kGetError) ? 0u : 1u;
}

// The serializable part's slot 4 (0x006124C0, ret 4: the stream; `this` is
// the object + 8): refused (AL 0) for a stream that is in error already and
// for any version but 1. Then the count, and that many times a reference is
// read (SC4Stream::ReadReference with the network occupant interface
// 0xA821EF94 and 0) and appended to the vector at +0x24 as it is (the
// reference the read gave is the vector's; in place when there is room,
// else the game's InsertN 0x0051CA60); a reference that cannot be read ends
// it with AL 0 and the pieces read so far stay. The vector is not emptied
// first. The answer after the last one is 1 when the stream has no error,
// else 0.
//
// The game reads the version into the stack slot of its stream argument, so
// a stream that gives no version leaves the low word of the stream's address
// there: the same here. Its count's slot is not set before the read (zero
// here; a stream that fails there is in error, and the answer is 0 either
// way: in the game through a reference that cannot be read or through the
// stream's state at the end).
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;
    if (Virtual<uint8_t>(stream, stream_slot::kGetError))
        return 0;
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, stream_slot::kUint16, &version);
    if (version != kRecordVersion)
        return 0;
    uint32_t count = 0;
    Virtual<uint8_t>(stream, stream_slot::kUint32, &count);
    Vector* const managed = reinterpret_cast<Vector*>(self + field::kManaged);
    while (count != 0) {
        --count;
        void* piece = nullptr;
        if (!CallCdecl<uint8_t, void*, uint32_t, void**, uint32_t>(addr::SC4Stream_ReadReference, stream,
                                                                   kIidNetworkOccupant, &piece, 0u))
            return 0;
        char* const end = managed->end;
        if (end != managed->capacity) {
            if (end)
                At<void*>(end, 0) = piece;
            managed->end += 4;
        } else {
            CallThis<void, char*, void**, void**, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, managed, end, &piece,
                                                                      &piece, 1u, 1u);
        }
    }
    return Virtual<uint8_t>(stream, stream_slot::kGetError) ? 0u : 1u;
}

// ====================================================================================
// simulators.netmgr.object: the factory, the constructor and the destructors
// ====================================================================================

// The constructor (0x00613330, no arguments; reached only by the factory's
// jump), every store in the game's order. The part at +4 and the part at +8
// first get the vtables of their base classes (0x00A81174 and 0x00A80784),
// then the three of the class are stored. The reference count and the three
// bytes at +0x10 are cleared; the vector at +0x24; the table at +0x30 (its
// bucket vector and its count cleared, then the table's constructor
// 0x00829BC0 with 100 buckets; the word at +0x30 itself is never written);
// the three remap vectors and the queue (+0x44 to +0x70) and the byte at
// +0x74; the retrofit list (the word cleared, a head node from the node
// allocation 0x00611240 at its address with 1 and 0, the node pointed at
// itself both ways and stored); the crews (+0x7C to +0x84); the thirteen
// tools; the dirty rectangle empty (+0x18 before +0x14: 0x7FFFFFFF, then
// +0x1C and +0x20: -1). Last LoadRemapping, at its address. Nothing is
// subscribed and no service but the resource manager is asked: that is
// Init's. The game's EAX is the object.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    At<std::uintptr_t>(self, kMessagePart) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, kSerializablePart) = addr::SC4SimGrid_VtableBase;
    SetVtables(self);
    At<uint32_t>(self, field::kRefCount) = 0;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint8_t>(self, field::kLeftHand) = 0;
    At<uint8_t>(self, field::kBatching) = 0;
    for (std::size_t offset = field::kManaged; offset < field::kShadowEdges; offset += 4)
        At<uint32_t>(self, offset) = 0;
    char* const table = self + field::kShadowEdges;
    At<uint32_t>(table, 0x4) = 0;
    At<uint32_t>(table, 0x8) = 0;
    At<uint32_t>(table, 0xC) = 0;
    At<uint32_t>(table, edge::kCount) = 0;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, table, kEdgeBuckets);
    for (std::size_t offset = field::kPreviewRemap; offset < field::kQueueing; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint8_t>(self, field::kQueueing) = 0;
    At<ListNode*>(self, field::kRetrofit) = nullptr;
    ListNode* const head = CallThis<ListNode*, uint32_t, uint32_t>(addr::cSC4NetworkManager_AllocateRetrofitNodes,
                                                                    self + field::kRetrofit, 1u, 0u);
    head->next = head;
    head->previous = head;
    At<ListNode*>(self, field::kRetrofit) = head;
    for (std::size_t offset = field::kCrews; offset < kObjectSize; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<int32_t>(self, field::kDirtyRect + 0x4) = 0x7FFFFFFF;
    At<int32_t>(self, field::kDirtyRect + 0x0) = 0x7FFFFFFF;
    At<int32_t>(self, field::kDirtyRect + 0x8) = -1;
    At<int32_t>(self, field::kDirtyRect + 0xC) = -1;
    CallThis<void>(addr::cSC4NetworkManager_LoadRemapping, self);
    return self;
}

// The destructor (0x00612000, no arguments; the deleting destructor is its
// only caller): the three vtables stored again, Shutdown (at its address,
// not through the vtable), then the thirteen tools from the last to the
// first: one that is still there is released (the slot keeps its pointer);
// the crews' memory freed (0x0090CF63; every free here only for a pointer
// that is not null); the retrofit list destroyed (0x00611AA0, at its
// address); the queue's memory, the path vector's, the shadow vector's and
// the preview vector's freed; the table at +0x30 cleared (0x008EBA60) and
// its bucket array freed; last the memory of the vector at +0x24. The
// queued occupants and the crews themselves are not released.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destroy", logged);
    SetVtables(self);
    CallThis<uint8_t>(addr::cSC4NetworkManager_Shutdown, self);
    for (uint32_t type = kTypes; type-- > 0;)
        if (void* const tool = At<void*>(self, field::kTools + 4 * type))
            ReleaseOf(tool);
    if (void* const crews = At<void*>(self, field::kCrews))
        Free(crews);
    CallThis<void>(addr::cSC4NetworkManager_DestroyRetrofitList, self + field::kRetrofit);
    for (const std::size_t vector : {field::kQueue, field::kPathRemap, field::kShadowRemap, field::kPreviewRemap})
        if (void* const memory = At<void*>(self, vector))
            Free(memory);
    char* const table = self + field::kShadowEdges;
    CallThis<void>(addr::SC4Uint32HashSet_Clear, table);
    if (void* const buckets = At<void*>(table, edge::kBuckets))
        Free(buckets);
    if (void* const managed = At<void*>(self, field::kManaged))
        Free(managed);
}

// Slot 24 (0x00612590, ret 4: the flags; Release calls it with 1): the
// destructor at its address, and with bit 0 of the flags' low byte the
// memory freed (operator delete). The answer is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    CallThis<void>(addr::cSC4NetworkManager_Destroy, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// The factory (0x00613750; cdecl, no arguments; the class registration at
// 0x004603B8 hands its address to the framework with the class ID): 0xBC
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
    return CallThis<void*>(addr::cSC4NetworkManager_cSC4NetworkManager, raw);
}

// ====================================================================================
// Installation
// ====================================================================================

const Detour kLife[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_Init, Init),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_Shutdown, Shutdown),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_LoadRemapping, LoadRemapping),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_OnTextureRemapEntry, OnTextureRemapEntry),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_OnShadowRemapEntry, OnShadowRemapEntry),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_OnShadowEdgeRemapEntry, OnShadowEdgeRemapEntry),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_OnPathRemapEntry, OnPathRemapEntry),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_AddShadowEdgeRemap, AddShadowEdgeRemap),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_AddPathRemap, AddPathRemap),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_InsertShadowEdge, InsertShadowEdge),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ReleaseTools, ReleaseTools),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ShutdownCrews, ShutdownCrews),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_ClearRetrofitList, ClearRetrofitList),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_DestroyRetrofitList, DestroyRetrofitList),
};

const Detour kSave[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_Write, Write),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_Read, Read),
};

const Detour kObject[] = {
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_Create, Create),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_cSC4NetworkManager, Construct),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_Destroy, Destroy),
    OPENSC4_NETMGR_DETOUR(addr::cSC4NetworkManager_DeletingDestructor, DeletingDestructor),
};

bool InstallLife(registry::Mode) { return InstallDetours(kLife, "life"); }
bool InstallSave(registry::Mode) { return InstallDetours(kSave, "save"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.netmgr.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each
// is the game's table word for word; then the entries that hold a function
// of the object group directly are pointed at ours: slot 24 of the primary
// table, the deleting destructor (one entry, unless it is on the skip list).
// Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4NetworkManager_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4NetworkManager_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4NetworkManager_vftable_8));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8);
    }
    log::Write("netmgr own: vtables copied (25, 4 and 6 slots), %u entries redirected", redirected);
}

// simulators.netmgr.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.netmgr.life", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_Init,
                        "Our network manager life: Init, Shutdown, LoadRemapping with its four ini callbacks, the "
                        "shadow edge and path appends with the table's insert, ReleaseTools, ShutdownCrews and the "
                        "retrofit list's clear and destroy (14 detours; src/core/netmgr_life.cpp)",
                        false, InstallLife}),
    registry::Register({"simulators.netmgr.save", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_Write,
                        "Our network manager save record: Write and Read (version 1; 2 detours; "
                        "src/core/netmgr_life.cpp)",
                        false, InstallSave}),
    registry::Register({"simulators.netmgr.object", "simulators/netmgr", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_cSC4NetworkManager,
                        "Our network manager factory, constructor, destructor and deleting destructor (4 detours; "
                        "src/core/netmgr_life.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.netmgr.own", "simulators/netmgr", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4NetworkManager_vftable,
                        "Our own network manager: objects carry our copies of the class's three vtables; only has "
                        "an effect with simulators.netmgr.object (src/core/netmgr_life.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::netmgr_sim

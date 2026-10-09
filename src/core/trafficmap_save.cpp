// The traffic network map's save record: cIGZSerializable at +0xC, Write
// (0x004B1CE0, version 7) and Read (0x004B4DE0, versions 3 to 7)
// (docs/traffic-map.md).
//
// The record: the cell grid's records in cell order, the signals, the objects
// waiting on cells, the view rectangle, the two blink timers and the lanes.
// The three hash tables are written in bucket order.
#include "core/game.h"
#include "core/game_access.h"
#include "core/registry.h"
#include "core/trafficmap.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::trafficmap {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto OperatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);

using WriteRefFn = void*(__cdecl*)(void* stream, void* object);
using WriteBoolFn = void*(__cdecl*)(void* stream, uint32_t value);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t flags);
using ReadBoolFn = void*(__cdecl*)(void* stream, void* out);
const auto WriteRef = reinterpret_cast<WriteRefFn>(addr::SC4Stream_WriteReference);
const auto WriteBool = reinterpret_cast<WriteBoolFn>(addr::SC4Stream_WriteBool);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);
const auto ReadBool = reinterpret_cast<ReadBoolFn>(addr::SC4Stream_ReadBool);

constexpr std::size_t kSerializable = 0xC;
constexpr uint16_t kVersion = 7;

// Interface IDs Read asks references for.
constexpr uint32_t kOccupantIid = 0xA821EF94;
constexpr uint32_t kAutomatonIid = 0xA9B40F05;
constexpr uint32_t kWaitingIid = 0x8B99EE7C;

// cIGZOStream / cIGZIStream slots (byte offsets).
constexpr std::size_t kUint16 = 0x1C;
constexpr std::size_t kSint32 = 0x20;
constexpr std::size_t kUint32 = 0x24;
constexpr std::size_t kFloat32 = 0x30;
constexpr std::size_t kGetError = 0x48;

void PutU16(void* stream, uint16_t value) { Virtual<uint8_t>(stream, kUint16, value); }
void PutS32(void* stream, int32_t value) { Virtual<uint8_t>(stream, kSint32, value); }
void PutU32(void* stream, uint32_t value) { Virtual<uint8_t>(stream, kUint32, value); }
void PutF32(void* stream, float value) { Virtual<uint8_t>(stream, kFloat32, value); }
void Get(void* stream, std::size_t slot, void* out) { Virtual<uint8_t>(stream, slot, out); }

uint32_t ListSize(const ListNode* head)
{
    uint32_t n = 0;
    for (const ListNode* node = head->next; node != head; node = node->next)
        ++n;
    return n;
}

// 3 (0x004B1CE0): writes the record, version 7.
uint8_t __fastcall Write(char* serializable, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = serializable - kSerializable;
    PutU16(stream, kVersion);
    const uint32_t cells = At<uint32_t>(self, field::kHeight) * At<uint32_t>(self, field::kWidth);
    PutU32(stream, cells);
    uint32_t records = 0;
    for (uint32_t i = 0; i < cells; ++i)
        for (CellRecord* rec = At<CellRecord**>(self, field::kGrid)[i]; rec; rec = rec->next)
            ++records;
    PutU32(stream, records);
    for (uint32_t i = 0; i < cells; ++i) {
        for (CellRecord* rec = At<CellRecord**>(self, field::kGrid)[i]; rec; rec = rec->next) {
            PutU32(stream, i);
            WriteRef(stream, rec->occupant);
            const uint32_t ways = static_cast<uint32_t>(
                (static_cast<char*>(rec->lanes.end) - static_cast<char*>(rec->lanes.begin)) / 12);
            PutU32(stream, ways);
            for (uint32_t j = 0; j < ways; ++j) {
                PutU32(stream, j);
                GameVector& lane = static_cast<GameVector*>(rec->lanes.begin)[j];
                PutU32(stream, static_cast<uint32_t>(
                                   (static_cast<char*>(lane.end) - static_cast<char*>(lane.begin)) >> 4));
                for (char* entry = static_cast<char*>(lane.begin); entry != lane.end; entry += 16) {
                    PutU32(stream, At<uint32_t>(entry, 0));
                    PutF32(stream, At<float>(entry, 4));
                }
            }
            PutU32(stream, ListSize(rec->automata));
            for (ListNode* node = rec->automata->next; node != rec->automata; node = node->next)
                WriteRef(stream, node->value);
            WriteBool(stream, 0);
            PutU32(stream, rec->flags20);
            PutU32(stream, rec->flags24);
            PutU32(stream, rec->flags28);
            for (int a = 0; a < 4; ++a) {
                PutF32(stream, static_cast<float>(static_cast<int32_t>(rec->approachTraffic[a])));
                for (int t = 0; t < 4; ++t)
                    PutU16(stream, rec->turnTraffic[a][t]);
            }
            PutU16(stream, 0);
            const uint32_t paths = static_cast<uint32_t>(
                (static_cast<char*>(rec->paths.end) - static_cast<char*>(rec->paths.begin)) / 12);
            PutU32(stream, paths);
            for (uint32_t k = 0; k < paths; ++k) {
                char* const entry = static_cast<char*>(rec->paths.begin) + k * 12;
                PutS32(stream, At<int32_t>(entry, 0));
                PutF32(stream, At<float>(entry, 4));
                PutU16(stream, At<uint16_t>(entry, 8));
            }
        }
    }

    const Hashtable& signals = Table(self, field::kSignals);
    PutU32(stream, signals.count);
    for (SignalNode* node = First<SignalNode>(signals); node; node = Next(signals, node)) {
        WriteRef(stream, reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)));
        WriteBool(stream, node->byte58);
        PutU32(stream, ListSize(node->waiting));
        for (ListNode* item = node->waiting->next; item != node->waiting; item = item->next)
            WriteRef(stream, item->value);
        for (const Approach& approach : node->approaches) {
            PutU32(stream, static_cast<uint32_t>(approach.kind));
            WriteBool(stream, approach.green);
            PutF32(stream, approach.greenTime);
            PutF32(stream, approach.redTime);
            PutF32(stream, approach.timer);
        }
    }

    const Hashtable& objects = Table(self, field::kCellObjects);
    PutU32(stream, objects.count);
    for (CellObjectsNode* node = First<CellObjectsNode>(objects); node; node = Next(objects, node)) {
        PutU32(stream, node->key);
        PutU32(stream, static_cast<uint32_t>(
                           (static_cast<char*>(node->objects.end) - static_cast<char*>(node->objects.begin)) >> 2));
        for (void** item = static_cast<void**>(node->objects.begin); item != node->objects.end; ++item)
            WriteRef(stream, *item);
    }

    for (std::size_t i = 0; i < 4; ++i)
        PutS32(stream, At<int32_t>(self, field::kRect + 4 * i));
    PutF32(stream, At<float>(self, field::kBlinkTimerA));
    WriteBool(stream, At<uint8_t>(self, field::kBlinkA));
    PutF32(stream, At<float>(self, field::kBlinkTimerB));
    WriteBool(stream, At<uint8_t>(self, field::kBlinkB));

    const Hashtable& lanes = Table(self, field::kLanes);
    PutU32(stream, lanes.count);
    for (LaneNode* node = First<LaneNode>(lanes); node; node = Next(lanes, node)) {
        WriteRef(stream, reinterpret_cast<void*>(static_cast<std::uintptr_t>(node->key)));
        for (LaneWay& way : node->ways) {
            const uint32_t n = static_cast<uint32_t>(
                (static_cast<char*>(way.values.end) - static_cast<char*>(way.values.begin)) >> 2);
            PutU32(stream, n);
            for (uint32_t k = 0; k < n; ++k)
                PutU32(stream, static_cast<uint32_t*>(way.values.begin)[k]);
            PutU16(stream, way.index);
        }
    }
    return Virtual<uint8_t>(stream, kGetError) == 0 ? 1 : 0;
}

// Read's inline push_back of an N-byte element: on a full vector the new
// storage holds size + max(size, 1) elements (SC4::stl_allocate), the old is
// freed after the copy.
template <std::size_t N>
void PushBack(GameVector& v, const void* value)
{
    auto* const end = static_cast<char*>(v.end);
    if (end != v.capacity) {
        if (end)
            std::memcpy(end, value, N);
        v.end = end + N;
        return;
    }
    auto* const begin = static_cast<char*>(v.begin);
    const auto size = static_cast<uint32_t>((end - begin) / static_cast<std::ptrdiff_t>(N));
    const uint32_t capacity = size + (size < 1 ? 1 : size);
    auto* const fresh = capacity ? static_cast<char*>(Allocate(capacity * N)) : nullptr;
    char* out = fresh;
    if (begin != end) {
        std::memmove(fresh, begin, static_cast<std::size_t>(end - begin));
        out = fresh + (end - begin);
    }
    if (out)
        std::memcpy(out, value, N);
    if (v.begin)
        Free(v.begin);
    v.capacity = fresh + capacity * N;
    v.begin = fresh;
    v.end = out + N;
}

ListNode* NewList()
{
    auto* const head = static_cast<ListNode*>(Allocate(0xC));
    head->next = head;
    head->prev = head;
    return head;
}

void ListPushBack(ListNode* head, void* value)
{
    auto* const node = static_cast<ListNode*>(Allocate(0xC));
    node->value = value;
    ListNode* const last = head->prev;
    node->next = head;
    node->prev = last;
    last->next = node;
    head->prev = node;
}

// Frees a list's nodes, then its head.
void DestroyList(ListNode* head)
{
    for (ListNode* node = head->next; node != head;) {
        ListNode* const next = node->next;
        Free(node);
        node = next;
    }
    head->next = head;
    head->prev = head;
    Free(head);
}

// A cell record from the pool at 0x00B09588 (as slot 48 and Read inline it).
CellRecord* NewRecord()
{
    using GrowFn = char*(__thiscall*)(void* pool);
    const std::uintptr_t pool = addr::SC4_CellRecordPool;
    char* item = At<char*>(Global<char*>(pool + 4), 8);
    if (!item)
        item = reinterpret_cast<GrowFn>(addr::SC4EdgePool_Grow)(reinterpret_cast<void*>(pool));
    At<char*>(Global<char*>(pool + 4), 8) = At<char*>(item, 0);
    ++At<uint32_t>(Global<char*>(pool + 4), 0xC);
    return reinterpret_cast<CellRecord*>(item);
}

// A saved approach or turn count back to uint16: rounded to nearest with the
// game's 12582912 idiom, at most 0xFFFF.
uint16_t RoundCount(float value)
{
    const float shifted = static_cast<float>(static_cast<double>(value) + 12582912.0);
    uint32_t bits = 0;
    std::memcpy(&bits, &shifted, sizeof bits);
    bits -= 0x4B400000u;
    return bits > 0xFFFFu ? static_cast<uint16_t>(0xFFFF) : static_cast<uint16_t>(bits);
}

// The part of Read's stack frame whose leftover bytes reach the map: Read
// builds its signal and lane values there and copies bytes it never sets
// (the approaches' padding, the signal byte at +0x58 of records older than
// version 7, vector-entry padding). The thunk below hands over the bytes the
// original's frame would hold at entry: [entry ESP - 0x120, entry ESP), the
// original's ESP + 0x5C to + 0x17C after its prologue.
constexpr std::size_t kFrameBase = 0x5C;
constexpr std::size_t kFrameSize = 0x120;

// 4 (0x004B4DE0): reads the record (versions 3 to 7) into an empty map.
uint8_t __fastcall ReadImpl(char* serializable, void*, void* stream, const uint8_t* entryStack)
{
    static bool logged = false;
    FirstCall("Read", logged);
    using ResizeFn = void(__thiscall*)(void* table, uint32_t count);
    using InsertFn = void*(__thiscall*)(void* table, void* result, const void* value);
    using EraseFn = void*(__thiscall*)(void* vector, void* first, void* last);
    using InsertFillFn = void(__thiscall*)(void* vector, void* where, uint32_t count, const void* value);
    using ReserveFn = void(__thiscall*)(void* vector, uint32_t count);
    using CopyFn = void*(__thiscall*)(void* to, const void* from);
    const auto Resize = reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize);

    uint8_t frame[kFrameSize];
    std::memcpy(frame, entryStack, kFrameSize);
    auto F = [&frame](std::size_t offset) { return frame + offset - kFrameBase; };

    char* const self = serializable - kSerializable;
    At<uint8_t>(self, field::kFlag25) = 1;
    uint16_t version = 0;
    Get(stream, kUint16, &version);
    if (version < 3)
        return 0;

    uint32_t cells = 0;
    Get(stream, kUint32, &cells);
    At<CellRecord**>(self, field::kGrid) = static_cast<CellRecord**>(OperatorNew(cells * 4));
    for (uint32_t i = 0; i < cells; ++i)
        At<CellRecord**>(self, field::kGrid)[i] = nullptr;

    uint32_t records = 0;
    Get(stream, kUint32, &records);
    const GameVector emptyLane{};
    for (uint32_t r = 0; r < records; ++r) {
        uint32_t cell = 0;
        Get(stream, kUint32, &cell);
        CellRecord** link = &At<CellRecord**>(self, field::kGrid)[cell];
        while (*link)
            link = &(*link)->next;
        CellRecord* const rec = NewRecord();
        rec->lanes = GameVector{};
        rec->paths = GameVector{};
        rec->automata = nullptr;
        rec->automata = NewList();
        *link = rec;
        ReadRef(stream, kOccupantIid, &rec->occupant, 0);

        uint32_t ways = 0;
        Get(stream, kUint32, &ways);
        const auto size = static_cast<uint32_t>(
            (static_cast<char*>(rec->lanes.end) - static_cast<char*>(rec->lanes.begin)) / 12);
        if (ways < size)
            reinterpret_cast<EraseFn>(addr::SC4LaneVectorVector_Erase)(
                &rec->lanes, static_cast<char*>(rec->lanes.begin) + ways * 12, rec->lanes.end);
        else
            reinterpret_cast<InsertFillFn>(addr::SC4LaneVectorVector_InsertFill)(&rec->lanes, rec->lanes.end,
                                                                                 ways - size, &emptyLane);
        for (uint32_t j = 0; j < ways; ++j) {
            uint32_t index = 0;
            uint32_t count = 0;
            Get(stream, kUint32, &index);
            Get(stream, kUint32, &count);
            GameVector& lane = static_cast<GameVector*>(rec->lanes.begin)[j];
            if (count) {
                At<uint32_t>(F(0x60), 0) = 0;
                At<uint16_t>(F(0x64), 0) = 0;
            }
            for (uint32_t k = 0; k < count; ++k) {
                uint32_t entry[4] = {};
                float weight = 0.0f;
                Get(stream, kUint32, &entry[0]);
                Get(stream, kFloat32, &weight);
                std::memcpy(&entry[1], &weight, 4);
                std::memcpy(&entry[2], F(0x60), 8);
                PushBack<16>(lane, entry);
            }
        }

        uint32_t automata = 0;
        Get(stream, kUint32, &automata);
        for (uint32_t k = 0; k < automata; ++k) {
            void* automaton = nullptr;
            ReadRef(stream, kAutomatonIid, &automaton, 0);
            ListPushBack(rec->automata, automaton);
        }
        uint8_t unused = 0;
        ReadBool(stream, &unused);
        Get(stream, kUint32, &rec->flags20);
        Get(stream, kUint32, &rec->flags24);
        Get(stream, kUint32, &rec->flags28);
        for (int a = 0; a < 4; ++a) {
            float approach = 0.0f;
            Get(stream, kFloat32, &approach);
            rec->approachTraffic[a] = RoundCount(approach);
            for (int t = 0; t < 4; ++t) {
                if (version < 4) {
                    float turn = 0.0f;
                    Get(stream, kFloat32, &turn);
                    rec->turnTraffic[a][t] = RoundCount(turn);
                } else {
                    Get(stream, kUint16, &rec->turnTraffic[a][t]);
                }
            }
        }
        if (version < 4) {
            float tail = 0.0f;
            Get(stream, kFloat32, &tail);
        } else {
            uint16_t tail = 0;
            Get(stream, kUint16, &tail);
        }
        if (version > 4) {
            uint32_t paths = 0;
            Get(stream, kUint32, &paths);
            reinterpret_cast<ReserveFn>(addr::SC4Vector12_Reserve)(&rec->paths, paths);
            for (uint32_t k = 0; k < paths; ++k) {
                int32_t key = 0;
                Get(stream, kSint32, &key);
                At<int32_t>(F(0x6C), 0) = key;
                At<uint32_t>(F(0x70), 0) = 0;
                At<uint16_t>(F(0x74), 0) = 0;
                Get(stream, kFloat32, F(0x70));
                Get(stream, kUint16, F(0x74));
                PushBack<12>(rec->paths, F(0x6C));
            }
        }
        rec->next = nullptr;
    }

    // The signals: each value is built at the frame's +0xA8 (key, then the
    // node's fields from +0x8) and inserted without a resize.
    uint32_t signals = 0;
    Get(stream, kUint32, &signals);
    for (uint32_t s = 0; s < signals; ++s) {
        void* occupant = nullptr;
        ReadRef(stream, kOccupantIid, &occupant, 0);
        ListNode* const temp = NewList();
        At<void*>(F(0xA8), 0) = occupant;
        std::memcpy(F(0xAC), F(0x114), 0x50);
        *F(0xFC) = *F(0x164);
        At<uint32_t>(F(0x100), 0) = 0;
        At<uint32_t>(F(0x108), 0) = 0;
        At<uint32_t>(F(0x104), 0) = 0;
        ListNode* const copy = NewList();
        At<ListNode*>(F(0x10C), 0) = copy;
        for (ListNode* node = temp->next; node != temp; node = node->next)
            ListPushBack(copy, node->value);
        Hashtable& table = Table(self, field::kSignals);
        const uint32_t count = table.count;
        *F(0x110) = *F(0x178);
        Resize(&table, count + 1);
        reinterpret_cast<InsertFn>(addr::SC4SignalTable_InsertUnique)(&table, F(0x78), F(0xA8));
        DestroyList(At<ListNode*>(F(0x10C), 0));
        if (void* const begin = At<void*>(F(0x100), 0))
            Free(begin);
        DestroyList(temp);
        auto* const node = At<SignalNode*>(F(0x78), 0);
        if (version > 6)
            ReadBool(stream, &node->byte58);
        node->changed = 0;
        if (version > 6) {
            uint32_t waiting = 0;
            Get(stream, kUint32, &waiting);
            for (uint32_t k = 0; k < waiting; ++k) {
                void* automaton = nullptr;
                ReadRef(stream, kWaitingIid, &automaton, 0);
                ListPushBack(node->waiting, automaton);
            }
        }
        for (Approach& approach : node->approaches) {
            uint32_t kind = 0;
            Get(stream, kUint32, &kind);
            approach.kind = static_cast<int32_t>(kind);
            ReadBool(stream, &approach.green);
            Get(stream, kFloat32, &approach.greenTime);
            Get(stream, kFloat32, &approach.redTime);
            Get(stream, kFloat32, &approach.timer);
            approach.amber = 0;
        }
    }

    uint32_t cellObjects = 0;
    Get(stream, kUint32, &cellObjects);
    if (version < 2) {
        // Old records: a plain list, each added through slot 47.
        for (uint32_t i = 0; i < cellObjects; ++i) {
            void* object = nullptr;
            ReadRef(stream, kOccupantIid, &object, 0);
            Virtual<void>(self, 0xBC, object);
        }
    } else {
        for (uint32_t i = 0; i < cellObjects; ++i) {
            uint32_t key = 0;
            uint32_t count = 0;
            Get(stream, kUint32, &key);
            Get(stream, kUint32, &count);
            struct {
                uint32_t key;
                GameVector objects;
            } value{key, {}};
            Hashtable& table = Table(self, field::kCellObjects);
            Resize(&table, table.count + 1);
            reinterpret_cast<InsertFn>(addr::SC4PtrVectorHashMap_InsertUnique)(&table, F(0x78), &value);
            if (value.objects.begin)
                Free(value.objects.begin);
            auto* const node = At<CellObjectsNode*>(F(0x78), 0);
            void* object = nullptr;
            for (uint32_t k = 0; k < count; ++k) {
                ReadRef(stream, kOccupantIid, &object, 0);
                PushBack<4>(node->objects, &object);
            }
        }
    }

    for (std::size_t i = 0; i < 4; ++i)
        Get(stream, kSint32, &At<int32_t>(self, field::kRect + 4 * i));
    Get(stream, kFloat32, &At<float>(self, field::kBlinkTimerA));
    ReadBool(stream, &At<uint8_t>(self, field::kBlinkA));
    Get(stream, kFloat32, &At<float>(self, field::kBlinkTimerB));
    ReadBool(stream, &At<uint8_t>(self, field::kBlinkB));

    // The lanes (version 6 on): the ways are read into the frame's +0xA8,
    // copied into a value at +0x114 and inserted without a resize.
    if (version >= 6) {
        uint32_t lanes = 0;
        Get(stream, kUint32, &lanes);
        for (uint32_t l = 0; l < lanes; ++l) {
            void* occupant = nullptr;
            ReadRef(stream, kOccupantIid, &occupant, 0);
            for (std::size_t w = 0; w < 4; ++w)
                At<GameVector>(F(0xA8 + 0x10 * w), 0) = GameVector{};
            for (std::size_t w = 0; w < 4; ++w) {
                auto& values = At<GameVector>(F(0xA8 + 0x10 * w), 0);
                uint32_t count = 0;
                Get(stream, kUint32, &count);
                uint8_t k = 0;
                if (count) {
                    do {
                        uint32_t value = 0;
                        Get(stream, kUint32, &value);
                        PushBack<4>(values, &value);
                        ++k;
                    } while (k < count);
                }
                Get(stream, kUint16, F(0xB4 + 0x10 * w));
            }
            At<void*>(F(0x114), 0) = occupant;
            for (std::size_t w = 0; w < 4; ++w) {
                reinterpret_cast<CopyFn>(addr::SC4Uint32Vector_Copy)(F(0x118 + 0x10 * w), F(0xA8 + 0x10 * w));
                At<uint16_t>(F(0x124 + 0x10 * w), 0) = At<uint16_t>(F(0xB4 + 0x10 * w), 0);
            }
            Hashtable& table = Table(self, field::kLanes);
            Resize(&table, table.count + 1);
            reinterpret_cast<InsertFn>(addr::SC4LaneTable_InsertUnique)(&table, F(0x78), F(0x114));
            for (std::size_t w = 4; w-- > 0;)
                if (void* const begin = At<void*>(F(0x118 + 0x10 * w), 0))
                    Free(begin);
            for (std::size_t w = 4; w-- > 0;)
                if (void* const begin = At<void*>(F(0xA8 + 0x10 * w), 0))
                    Free(begin);
        }
    }
    return Virtual<uint8_t>(stream, kGetError) == 0 ? 1 : 0;
}

// Read's entry: copies the stack bytes below the return address that the
// original's frame would start with (kFrameSize bytes under the entry ESP)
// before any of our code touches them, then calls ReadImpl. thiscall: ECX =
// the serializable part, the stream on the stack.
__declspec(naked) void Read()
{
    __asm {
        sub esp, 0x240
        push esi
        push edi
        push ecx
        lea esi, [esp + 0x24C - 0x120]
        lea edi, [esp + 0xC]
        mov ecx, 0x48
        rep movsd
        pop ecx
        pop edi
        pop esi
        mov eax, esp
        push eax
        push dword ptr [esp + 0x248]
        call ReadImpl
        add esp, 0x240
        ret 4
    }
}

const Slot kSaveSlots[] = {OPENSC4_TM_SLOT(3, 0x004B1CE0, Write)};
const Slot kLoadSlots[] = {OPENSC4_TM_SLOT(4, 0x004B4DE0, Read)};

bool InstallSave(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    return PatchSlots("trafficmap save", addr::cSC4TrafficNetworkMap_vftable_0xC, 6, kSaveSlots, 1);
}

bool InstallLoad(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    return PatchSlots("trafficmap load", addr::cSC4TrafficNetworkMap_vftable_0xC, 6, kLoadSlots, 1);
}

const bool g_registeredSave = registry::Register({
    "simulators.trafficmap.save",
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable_0xC,
    "Our traffic network map Write: the save record, version 7 (docs/traffic-map.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.trafficmap.load",
    "simulators/trafficmap",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TrafficNetworkMap_vftable_0xC,
    "Our traffic network map Read: the save record, versions 3 to 7 (docs/traffic-map.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::trafficmap

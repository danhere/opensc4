// The query tool of the city view, seventh piece, translator A: what a click
// asks about (0x004D4140), the click itself (0x004D4D70) and the tool's
// message handler (0x004D7160). Replacement, off by default:
//
//   ui.tools.query7a: 3 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): DescribePick (0x004D4140, cdecl, called
//     only by OnMouseDownL), OnMouseDownL (0x004D4D70, slot 16 of the main
//     vtable) and DoMessage (0x004D7160, slot 3 of the target part's vtable
//     0x00A90A78; `this` is the tool's +0x28).
//
// The other five functions of the range (0x004D4F90, 0x004D4FD0,
// 0x004D5C90, 0x004D5E30, 0x004D6C70) are ui.tools.query7b
// (tool_query_7b.cpp); DoMessage calls two of them by address.
//
// sc4-query-ui-hooks patches all three of these functions inside: CALLs at
// 0x004D4406 (the query sound, only when its ini turns query sounds off)
// and 0x004D4494 (the query dialog) in DescribePick, a JMP at 0x004D4EAA in
// OnMouseDownL (its shift click; back to 0x004D4F19 or 0x004D4F30), and
// CALLs at 0x004D7335 (the terrain text's Sprintf), 0x004D7464 (a
// building's tip, 0x004D5E30), 0x004D747F (flora name), 0x004D74A8 (a
// network piece's name) and 0x004D74BF (a prop's name) in DoMessage. The
// rows in foreign_sites.cpp make the hook layer leave each function the
// game's when its site holds a CALL or JMP that leaves the exe; the plugin's
// own code calls 0x004D5E30, 0x004D3C30, 0x004CF8A0 and 0x004C3810 by
// address and so reaches whatever is at those entries. Every call from here
// to a function that holds such a site (0x004D4140; 0x004D07C0, NAM) goes
// through the game's entry.
//
// No random number is drawn and no clock is read. DescribePick runs modal
// windows (the query dialog 0x004D3C30, the edge window 0x004CC6E0, the
// cell window 0x004CCCB0: real time, the player's clicks), plays sounds
// (0x004C4F40; 0x00B43CB8 slot 11) and, through the query dialog's walks,
// can change the city. OnMouseDownL with shift picks an automaton (frame
// driven) and hands it to the vehicle manager, or sends 0x2ACD2CF3 to all.
// DoMessage reads the mouse position for 0xA6B79602 (the window manager's
// slot 33: real input). Nothing here draws.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_7a {
namespace {

namespace base = tool_query::base;
namespace field = tool_query::field;
namespace message_id = tool_query::message;

constexpr uint32_t kTypeBuilding = 0x278128A0;
constexpr uint32_t kTypeNetwork = 0xC772BF98;
constexpr uint32_t kTypeFlora = 0x74758926;
constexpr uint32_t kTypeProp = 0xA823821E;
constexpr uint32_t kTypePowerPole = 0x2890D4DE;
constexpr uint32_t kTypeLandfill = 0x6A11BD2B;
constexpr uint32_t kNetworkOccupant = 0xA821EF94; // the network occupant's interface
constexpr uint32_t kEdgeObject = 0xA8C86142;      // what else gets the edge window
constexpr uint32_t kModelPiece = 0x4A10AFC2;      // a network piece with a model (its slot 96)
constexpr uint32_t kLayoutProperty = 0x2A499F85;  // an occupant's query dialog (layout instance)
constexpr uint32_t kCursorText = 0x2A456CB1;      // the tool's text at the pointer (view slots 40, 41)
constexpr uint32_t kWindowIid = 0x22BA0121;
constexpr uint32_t kQueryTextGroup = 0xEA5524EB;  // the group of the query's game strings
constexpr uint32_t kPowerPoleTitle = 0x0A5BB9FD;
constexpr uint32_t kLandfillTitle = 0x4A5BBA21;
constexpr uint32_t kNoQuerySound = 0xAA62C9D5;    // the sound of a click on nothing to ask about
constexpr uint32_t kAutomatonClick = 0x2ACD2CF3;  // sent to all when a shift click finds no automaton
constexpr uint32_t kImageType = 0x856DDBAC;       // the tip's pictures: PNG,
constexpr uint32_t kImageGroup = 0x46A006B0;      // of this group
constexpr float kHalf = 0.5f;                     // the tip meter's share before anything sets it
constexpr double kSixteenth = 0.0625;             // 0x00A8E174 (float)

// The zone types (the zone manager's slot 12) that have a cell window:
// 0xB, 0xC and 0xE.
bool HasCellWindow(uint32_t zone)
{
    return zone == 0xB || zone == 0xC || zone == 0xE;
}

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// The game's _ftol2 (0x009EEF04): truncation toward zero to 64 bits, the low
// 32 kept; what does not fit in 64 bits (a NaN among them) is the x87
// "integer indefinite", whose low word is 0.
int32_t Ftol(double value)
{
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0))
        return 0;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(value))));
}

// --- The game's strings --------------------------------------------------------------------

// The body of an STL string: begin, end, end of the buffer.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

// SC4String as the game keeps one on the stack: the vtable 0x00A80810, an
// STL body and a zero dword.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// SC4LocalizedString: the string, then the key. 0x20 bytes.
struct LocalizedString {
    GameString text;
    uint32_t key[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The record 0x00414210 makes (the residential simulator's school record,
// tool_query.h school_record): 0x40 bytes, a string first.
struct SchoolRecord {
    GameString text;
    char rest[0x2C];
};
static_assert(sizeof(SchoolRecord) == tool_query::school_record::kSize);

// The image holder the game keeps on the stack (SC4ImageLoader, 0x30 bytes;
// the image at +4).
struct ImageLoader {
    void* first;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// An empty string as SC4String::SC4String (0x00578390) makes one: an 8-byte
// buffer from the STL allocator.
void MakeEmpty(GameString& text)
{
    This<void*>(addr::SC4String_SC4String, &text);
}

// The game's call of SC4String::Destroy (0x00414250): the vtable set to the
// string's again, the buffer freed.
void Destroy(GameString& text)
{
    This<void>(addr::SC4String_Destroy, &text);
}

// The same done in line, as the game does in places.
void FreeBody(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// The body of an SC4String the caller passed (its +4).
char* BodyOf(void* text)
{
    return static_cast<char*>(text) + 0x4;
}

// cIGZString's slot 27 (+0x6C): Sprintf, cdecl and variadic, through the
// string's own vtable.
template <typename... Args>
void Format(void* text, const char* format, Args... args)
{
    using FormatFn = void(__cdecl*)(void*, const char*, ...);
    reinterpret_cast<FormatFn>((*static_cast<void***>(text))[27])(text, format, args...);
}

// One of DescribePick's opening assignments: a body made from a literal
// range (0x00407FA0), assigned to the string's body (0x00408A70; skipped if
// the bodies were one, which they never are) and freed.
void SetLiteral(void* text, const char* begin, std::size_t length, uint8_t* tag)
{
    StringBody made = {nullptr, nullptr, nullptr};
    This<void>(addr::SC4String_FromRange, static_cast<void*>(&made), begin, begin + length, static_cast<void*>(tag));
    char* const body = BodyOf(text);
    if (reinterpret_cast<char*>(&made) != body)
        This<void>(addr::SC4String_AssignRange, body, made.begin, made.end, static_cast<void*>(tag));
    if (made.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(made.begin));
}

// --- DescribePick ----------------------------------------------------------------------------

// The literals the three strings start as (0x00A92818, 0x00A92804,
// 0x00A927F0).
constexpr char kQueryText[] = "Query";
constexpr char kExtraText[] = "Query extra: none";
constexpr char kExtraSecondText[] = "Query extra2: none";

// The game's 0x004D4140 (cdecl: screen x, screen z, three strings, the
// debug byte; plain ret; only caller OnMouseDownL 0x004D4F6F with the
// static strings 0x00B2185C, 0x00B21870, 0x00B21884 and 0x004C4700's
// answer; sc4-query-ui-hooks calls it DescribePick). The three strings
// start as "Query", "Query extra: none", "Query extra2: none"; nothing
// more without the city view (0x00B43DC4). An occupant filter of the
// unmarked kind (0xC bytes, vtables 0x00A90B18 / 0x00A90B00, referenced)
// picks at the point (0x004C4C90); without an occupant there the view's
// ground point (slot 36) gives a cell (city slot 95) and its lot (lot
// manager slot 6). A picked occupant or a found lot's building (lot slot 48,
// its slot 3) is what is asked about. For it: its type (slot 7), the query
// sound (0x004C4F40), then the edge window (0x004CC6E0) for a network piece
// (interface 0xA821EF94; kept until the end) or an object of 0xA8C86142,
// else the query dialog (0x004D3C30) when it has a layout (0x2A499F85),
// else the cell window (0x004CCCB0) at its cell when that is zoned 0xB,
// 0xC or 0xE, else the sound 0xAA62C9D5 (0x00B43CB8 slot 11). A building
// then is kept (referenced) for the cell text, its position read (slot 8;
// not after a cell window), and a school record made and freed unused (as
// the game). A network piece prints into the first string: with interface
// 0x4A10AFC2 its ID, flip, rotation, model height and floor heights (its
// slot 96 and the +4 part's position), else its piece ID, rotation and
// flip and appends the traffic at its cell (traffic simulator slot 23
// GetCapacity of kind 1, of kind 2 when that is 0; its slot 8 map's
// volume times ten; volume over capacity), with the cell log (0x004D12A0)
// under the debug byte. After a cell window run for the occupant nothing
// more. Otherwise (and with no occupant once the view's ground point is
// found again) for the cell of the point: the cell window unless a window
// was shown, the cell log under the debug byte, then the texts: the lot's
// configuration as "name_ID" (lot slots 14, 21, 9; the cell moves to the
// lot's location), the building (the kept one, or the first building the
// occupant manager finds in the lot's rectangle) with its figures
// (0x004CCF60), the lot shares and demand (0x004CF2B0, 0x004CD820) into the
// second string, else ": no lot" and the demand; " EQ: %f HQ: %f" from the
// residential simulator (slots 35 and 36) appended to the second, the
// cell's figures (0x004CE8A0) into the first, the third "[Building ..]
// [Lot: ..]" with the first's old text when that was longer than 10. The
// filter, the kept building and the strings let go.
//
// Oddities kept: the school record; the lot from the lot manager's slot 6 is
// not referenced; only the second string's AppendRange goes through the
// body pointer taken at the start; the building found in the lot's
// rectangle is asked for even when the point's own lot had none.
void __cdecl DescribePick(int32_t x, int32_t z, void* first, void* second, void* third, uint32_t debugWord)
{
    const bool debug = static_cast<uint8_t>(debugWord) != 0;
    uint8_t tag = 0;
    SetLiteral(first, kQueryText, sizeof kQueryText - 1, &tag);
    SetLiteral(second, kExtraText, sizeof kExtraText - 1, &tag);
    char* const secondBody = BodyOf(second);
    SetLiteral(third, kExtraSecondText, sizeof kExtraSecondText - 1, &tag);

    void* held = nullptr;
    void* const view = Global<void*>(addr::SC4_Service43DC4);
    if (!view)
        return;
    GameString cellText = {};
    This<void*>(addr::SC4String_FromText, &cellText, "empty");
    bool describeCell = false; // a building or network piece: the cell's texts follow
    bool windowShown = false;  // no cell window for the point's cell
    bool cellWindow = false;   // a cell window ran for the occupant: nothing more

    void* filter = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xC});
    if (filter) {
        This<void*>(addr::SC4_OccupantFilterBase, filter);
        At<std::uintptr_t>(filter, 0x0) = addr::SC4QueryTool_UnmarkedFilterVtable;
        At<std::uintptr_t>(filter, 0x4) = addr::SC4QueryTool_UnmarkedFilterVtable2;
        AddRef(filter);
    }
    float point[3] = {0.0f, 0.0f, 0.0f};
    void* picked = nullptr;
    void* occupant = nullptr;
    void* const lotManager = Global<void*>(addr::SC4_LotManager);
    if (Cdecl<uint8_t>(addr::SC4QueryTool_PickOccupant, x, z, &picked, filter)) {
        occupant = picked;
        if (void* const lot = Virtual<void*>(lotManager, 0x38, picked)) {
            AddRef(lot);
            if (void* const building = Virtual<void*>(lot, 0xC0)) {
                AddRef(building);
                Release(picked);
                occupant = Virtual<void*>(building, 0xC);
                AddRef(occupant);
                Release(building);
            }
            Release(lot);
        }
    } else {
        occupant = picked;
        if (Virtual<uint8_t>(view, 0x90, x, z, static_cast<float*>(point), uint32_t{0})) {
            int32_t cellX = 0, cellZ = 0;
            Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &cellX, &cellZ);
            if (void* const lot = Virtual<void*>(lotManager, 0x18, cellX, cellZ, uint32_t{0})) {
                AddRef(lot);
                if (void* const building = Virtual<void*>(lot, 0xC0)) {
                    AddRef(building);
                    occupant = Virtual<void*>(building, 0xC);
                    AddRef(occupant);
                    Release(building);
                }
                Release(lot);
            }
        }
    }

    bool lookAgain = true; // with no occupant: the view's ground point again
    void* network = nullptr;
    if (occupant) {
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        Cdecl<void>(addr::SC4QueryTool_QuerySound, occupant);
        bool edge = false;
        if (Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &network)) {
            Cdecl<uint8_t>(addr::SC4QueryTool_RunEdgeWindow, occupant);
            windowShown = false;
        } else {
            void* other = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kEdgeObject, &other)) {
                Cdecl<uint8_t>(addr::SC4QueryTool_RunEdgeWindow, occupant);
                windowShown = true;
            }
            if (other)
                Release(other);
            edge = windowShown;
        }
        if (!edge) {
            uint32_t instance = 0;
            if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, Virtual<void*>(occupant, 0x18), kLayoutProperty,
                               &instance)) {
                Cdecl<uint8_t>(addr::SC4QueryTool_DoQueryDialog, occupant);
                windowShown = true;
            } else {
                float position[3] = {0.0f, 0.0f, 0.0f};
                Virtual<void>(occupant, 0x20, static_cast<float*>(position));
                int32_t cellX = 0, cellZ = 0;
                Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, position[0], position[2], &cellX, &cellZ);
                const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_ZoneManager), 0x30, cellX, cellZ);
                if (HasCellWindow(zone)) {
                    Cdecl<void>(addr::SC4QueryTool_RunCellWindow, zone, cellX, cellZ);
                    windowShown = true;
                    cellWindow = true;
                } else if (void* const sounds = Global<void*>(addr::SC4_Sound43CB8)) {
                    AddRef(sounds);
                    Virtual<void>(sounds, 0x2C, uint32_t{1}, kNoQuerySound, uint32_t{0}, uint32_t{0}, uint32_t{0});
                    Release(sounds);
                }
            }
        }

        if (type == kTypeBuilding) {
            describeCell = true;
            SchoolRecord record;
            This<void*>(addr::cSC4Advisors_F414210, &record);
            if (!cellWindow) {
                held = occupant;
                Virtual<void>(occupant, 0x20, static_cast<float*>(point));
                AddRef(occupant);
            }
            if (record.text.begin)
                Cdecl<void>(addr::SC4_Free, static_cast<void*>(record.text.begin));
        } else if (type == kTypeNetwork) {
            describeCell = true;
            void* part = nullptr;
            void* piece = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kModelPiece, &part)) {
                uint32_t id = 0;
                uint32_t unused = 0; // the slot's second out (+0x268), not printed
                float floors[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float height = 0.0f;
                Virtual<void>(part, 0x180, &id, &unused, static_cast<float*>(floors), &height);
                Virtual<void>(static_cast<char*>(part) + 4, 0x20, static_cast<float*>(point));
                const uint8_t flipped = Virtual<uint8_t>(part, 0x64);
                const int32_t rotation = Virtual<uint8_t>(part, 0x60);
                int32_t cellX = 0, cellZ = 0;
                Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &cellX, &cellZ);
                Format(first,
                       "Query cell (%d, %d): Id %08lx, flip: %s, rotation: %d, model Y Height %f, floor heights %f "
                       "%f %f %f\n",
                       cellX, cellZ, id, flipped ? "yes" : "no", rotation, static_cast<double>(height),
                       static_cast<double>(floors[0]), static_cast<double>(floors[1]),
                       static_cast<double>(floors[2]), static_cast<double>(floors[3]));
                Release(part);
            } else if (Virtual<uint8_t>(occupant, 0x0, kNetworkOccupant, &piece)) {
                const uint32_t pieceId = Virtual<uint32_t>(piece, 0x5C);
                const int32_t rotation = Virtual<uint8_t>(piece, 0x60);
                const int32_t flipped = Virtual<uint8_t>(piece, 0x64) != 0 ? 1 : 0;
                Virtual<void>(occupant, 0x20, static_cast<float*>(point));
                int32_t cellX = 0, cellZ = 0;
                Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &cellX, &cellZ);
                Format(first, "Query cell (%d, %d): Piece Id: %08lx, Rotation:%d, Flipped:%d \n", cellX, cellZ,
                       pieceId, rotation, flipped);
                GameString traffic;
                MakeEmpty(traffic);
                void* const simulator = Global<void*>(addr::SC4_Service43DA8);
                float capacity = Virtual<float>(simulator, 0x5C, uint32_t{1}, cellX, cellZ);
                if (capacity == 0.0f)
                    capacity = Virtual<float>(simulator, 0x5C, uint32_t{2}, cellX, cellZ);
                void* const volumes = Virtual<void*>(simulator, 0x20);
                const int32_t volume = static_cast<int32_t>(Virtual<uint8_t>(volumes, 0x1C, cellX, cellZ)) * 10;
                Cdecl<void>(addr::cRZBaseString_Sprintf, &traffic, ", traffic cap(%.0f) vol(%d) cong(%f)",
                            static_cast<double>(capacity), volume,
                            static_cast<double>(volume) / static_cast<double>(capacity));
                This<void>(addr::SC4String_AppendStd, BodyOf(first), static_cast<void*>(&traffic.begin));
                if (debug)
                    Cdecl<void>(addr::SC4QueryTool_LogCell, cellX, cellZ);
                Release(piece);
                Destroy(traffic);
            }
        }

        Release(occupant);
        if (cellWindow) {
            if (network)
                Release(network);
            if (filter)
                Release(filter);
            FreeBody(cellText);
            return;
        }
        if (network)
            Release(network);
        lookAgain = !describeCell;
    }

    if (lookAgain && !Virtual<uint8_t>(view, 0x90, x, z, static_cast<float*>(point), uint32_t{0})) {
        if (filter)
            Release(filter);
        FreeBody(cellText);
        if (held)
            Release(held);
        return;
    }

    // The cell of the point.
    int32_t cellX = 0, cellZ = 0;
    Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &cellX, &cellZ);
    if (!windowShown) {
        const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_ZoneManager), 0x30, cellX, cellZ);
        if (HasCellWindow(zone))
            Cdecl<void>(addr::SC4QueryTool_RunCellWindow, zone, cellX, cellZ);
    }
    if (debug)
        Cdecl<void>(addr::SC4QueryTool_LogCell, cellX, cellZ);
    int32_t corner[2] = {cellX, cellZ};
    int32_t otherCorner[2] = {cellX, cellZ};
    GameString lotText = {};
    This<void*>(addr::SC4String_FromText, &lotText, "no lot template");
    void* const lot = Virtual<void*>(lotManager, 0x18, cellX, cellZ, uint32_t{0});
    if (lot) {
        int32_t rect[4] = {0, 0, 0, 0};
        Virtual<void>(lot, 0x38, static_cast<int32_t*>(rect));
        corner[0] = rect[0];
        corner[1] = rect[1];
        otherCorner[0] = rect[2];
        otherCorner[1] = rect[3];
        Virtual<void>(lot, 0x54, &cellX, &cellZ);
        if (void* const configuration = Virtual<void*>(lot, 0x24)) {
            GameString name;
            MakeEmpty(name);
            Virtual<void>(configuration, 0x10, static_cast<void*>(&name));
            const char* const nameText = name.begin;
            const uint32_t id = Virtual<uint32_t>(configuration, 0xC);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &lotText, "%s_%08x", nameText, id);
            FreeBody(name);
        }
    }
    const uint32_t zone = Virtual<uint32_t>(Global<void*>(addr::SC4_ZoneManager), 0x30, cellX, cellZ);
    if (!held) {
        void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, uint32_t{0xC}));
        sentinel[0] = sentinel;
        sentinel[1] = sentinel;
        void* list = sentinel;
        void* const occupants = Global<void*>(addr::SC4_NetworkService);
        Virtual<void>(occupants, 0xA4, &list, static_cast<int32_t*>(corner), static_cast<int32_t*>(otherCorner),
                      kTypeBuilding, uint32_t{1});
        void** const end = static_cast<void**>(list);
        if (static_cast<void**>(end[0]) != end) {
            held = static_cast<void**>(end[0])[2];
            AddRef(held);
        }
        Virtual<void>(occupants, 0xC0, &list);
        This<void>(addr::SC4List_Destroy2, &list);
    }
    if (held) {
        Cdecl<void>(addr::SC4QueryTool_AppendBuildingText, held, static_cast<void*>(&cellText));
        GameString shares;
        MakeEmpty(shares);
        Cdecl<void>(addr::SC4QueryTool_AppendLotShares, cellX, cellZ, static_cast<void*>(&shares));
        GameString demand;
        MakeEmpty(demand);
        Cdecl<void>(addr::SC4QueryTool_AppendDemandText, cellX, cellZ, static_cast<void*>(&demand));
        char* const demandText = demand.begin;
        char* const sharesText = shares.begin;
        Format(second, "%s %s\n", sharesText, demandText);
        if (demandText)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(demandText));
        if (sharesText)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(sharesText));
    } else {
        GameString none;
        MakeEmpty(none);
        char* const begin = none.begin;
        char* const end = none.end;
        if (begin != end) {
            This<void*>(addr::cSC4Advisor_F410F00, BodyOf(&cellText), ": ");
            This<void>(addr::SC4String_AppendRange, BodyOf(&cellText), begin, end, static_cast<void*>(&tag));
        } else if (!lot) {
            This<void*>(addr::cSC4Advisor_F410F00, BodyOf(&cellText), ": no lot");
        }
        Cdecl<void>(addr::SC4QueryTool_AppendDemandText, cellX, cellZ, second);
        if (begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
    }

    GameString quotients;
    MakeEmpty(quotients);
    void* const residential = Global<void*>(addr::SC4_Service43DA4);
    const double health = Virtual<double>(residential, 0x90, cellX, cellZ);
    const double education = Virtual<double>(residential, 0x8C, cellX, cellZ);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &quotients, " EQ: %f HQ: %f", education, health);
    This<void>(addr::SC4String_AppendRange, secondBody, quotients.begin, quotients.end, static_cast<void*>(&tag));
    FreeBody(quotients);

    GameString cell;
    MakeEmpty(cell);
    Cdecl<void>(addr::SC4QueryTool_AppendCellText, zone, static_cast<float*>(point), static_cast<void*>(&cell));
    if (Virtual<uint32_t>(first, 0x1C) > 10)
        Format(third, "[Building %s] [Lot: %s] %s\n", cellText.begin, lotText.begin, At<char*>(first, 0x4));
    else
        Format(third, "[Building %s] [Lot: %s]\n", cellText.begin, lotText.begin);
    char* const cellTextBegin = cell.begin;
    Format(first, "Query cell (%d, %d): %s\n", cellX, cellZ, cellTextBegin);
    if (cellTextBegin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(cellTextBegin));
    FreeBody(lotText);

    if (filter)
        Release(filter);
    FreeBody(cellText);
    if (held)
        Release(held);
}

// --- OnMouseDownL ----------------------------------------------------------------------------

// The game's 0x004D4D70 (slot 16 of the main vtable, word 0x00A90AC8:
// screen x, screen z, modifier keys (1 shift, 2 control); ret 0xC). The
// timer at +0x50 stopped (its slot 7), the view's text at the pointer
// cleared (view slot 41), the tip's window hidden. With shift: the
// automaton at the point (0x004C4D10) goes to the city's vehicle manager
// (city slot 43, its slot 34) and is let go; with none, the message
// 0x2ACD2CF3 (x, z) is sent to all (the message server's slot 3, a
// cGZMessage of its own); then the prop of the two groups at the point
// (0x004C4DC0) goes to the traffic network map (city slot 40, its slot 37).
// With control: the view's ground point (slot 36) as a cell (city slot 95)
// and the residential simulator's slot 59 with the cell over 4 (a stub that
// does nothing in this build). +0x89 set; kind 1: the routes (0x004CAC50),
// true. Else, unless shift found an automaton, +0x89 cleared and the pick
// described (0x004D4140 with the static strings and the debug keys,
// 0x004C4700); the tool's slot 28 with 0. True.
//
// sc4-query-ui-hooks jumps at 0x004D4EAA (the control test) to its own code
// for kind 0 with any modifier; back to 0x004D4F19 or straight to the end
// at 0x004D4F30 (+0x89 untouched).
//
// Oddities kept: with no memory for the message the game calls through
// null (so does this); the game writes the cell's z into the modifier
// argument's place (a local here; not read after); the ground point is not
// set before slot 36 (0 here: the game's stack leftovers).
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    Virtual<void>(self + field::kTimer, 0x1C);
    Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText);
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x118);
    void* shown = nullptr;
    const bool shift = (modifiers & 1) != 0;
    if (shift) {
        void* automaton = nullptr;
        if (Cdecl<uint8_t>(addr::SC4QueryTool_PickAutomaton, x, z, &automaton)) {
            void* const vehicles = Virtual<void*>(Global<void*>(addr::SC4_City), 0xAC);
            shown = automaton;
            Virtual<void>(vehicles, 0x88, automaton);
            Release(automaton);
        } else {
            if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
                void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
                void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
                if (message)
                    AddRef(message);
                Virtual<void>(message, 0x14, kAutomatonClick);
                Virtual<void>(message, 0x2C, x);
                Virtual<void>(message, 0x40, z);
                Virtual<uint8_t>(server, 0xC, message);
                Release(message);
            }
            shown = automaton;
        }
    }
    if (shift) {
        void* prop = nullptr;
        if (Cdecl<uint8_t>(addr::SC4QueryTool_PickGroupProp, x, z, &prop)) {
            void* const map = Virtual<void*>(Global<void*>(addr::SC4_City), 0xA0);
            Virtual<void>(map, 0x94, prop);
            Release(prop);
        }
    }
    if (modifiers & 2) {
        float point[3] = {0.0f, 0.0f, 0.0f};
        Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, static_cast<float*>(point), uint32_t{0});
        int32_t cellX = 0, cellZ = 0;
        Virtual<void>(Global<void*>(addr::SC4_City), 0x17C, point[0], point[2], &cellX, &cellZ);
        Virtual<void>(Global<void*>(addr::SC4_Service43DA4), 0xEC, cellX / 4, cellZ / 4);
    }
    At<uint8_t>(self, field::kPressed) = 1;
    if (At<uint32_t>(self, field::kKind) == 1) {
        This<uint8_t>(addr::SC4QueryTool_ShowRoutes, self);
        return 1;
    }
    if (!shown) {
        At<uint8_t>(self, field::kPressed) = 0;
        const uint32_t debug = This<uint8_t>(addr::SC4QueryTool_DebugKeysDown, self);
        Cdecl<void>(addr::SC4QueryTool_DescribePick, x, z, reinterpret_cast<void*>(addr::SC4QueryTool_DebugText),
                    reinterpret_cast<void*>(addr::SC4QueryTool_DebugTextExtra),
                    reinterpret_cast<void*>(addr::SC4QueryTool_DebugTextExtra2), debug);
    }
    Virtual<void>(self, 0x70, uint32_t{0});
    return 1;
}

// --- DoMessage -------------------------------------------------------------------------------

// What DoMessage does for the tip timer's message (0x0A456D95) while +0x78
// is set: the tip for what the tool points at. A title and a text (empty
// game strings), a picture (0), a meter picture (0) and a meter share
// (0.5). An occupant at +0x2C: kind 1 a building's route summary
// (0x004D0190, the picture with it) or a network piece's traffic
// (0x004D07C0, through the game's entry: NAM's site); kind 0 by its type a
// building's tip (0x004D5E30: all five), flora or a prop by name
// (0x004C3810), a network piece by name (0x004CF8A0), a power pole or a
// landfill by a title (game strings 0x0A5BB9FD, 0x4A5BBA21). No occupant:
// the place's tip (0x004D6C70) when +0x88 says something was picked; else
// only with +0x94, or nothing at all. With +0x94 the point is added: the
// title "Terrain" when empty, the text (a new line first unless empty)
// "x=.. y=.. z=..\ncell x=.. cell z=.." (+0x7C to +0x87; the cells are the
// coordinates over 16, truncated). With a picture and the tip: the picture
// (PNG {0x856DDBAC, 0x46A006B0, picture}) as the tip's background (its slot
// 4 with 0xC, 0xA), the meter picture and share (slot 14 with 5; 0, 0, 0
// without a meter picture), the tip shown (0x004C5BA0), the view's text at
// the pointer cleared (slot 41; the view is not checked here). Else the
// tip's window hidden (when there is a tip) and the view (when there is
// one) shows both as its text at the pointer (slot 40: 0x2A456CB1, 0,
// title, text, 0).
void ShowTipFor(char* tool)
{
    GameString title;
    MakeEmpty(title);
    GameString text;
    MakeEmpty(text);
    uint32_t picture = 0;
    uint32_t meter = 0;
    uint32_t meterPicture = 0;
    float share = kHalf;
    void* const occupant = At<void*>(tool, field::kOccupant);
    bool describe = true;
    if (occupant) {
        const uint32_t type = Virtual<uint32_t>(occupant, 0x1C);
        if (At<uint32_t>(tool, field::kKind) == 1) {
            if (type == kTypeBuilding)
                This<void>(addr::SC4QueryTool_RouteSummary, tool, static_cast<void*>(&title),
                           static_cast<void*>(&text), &picture);
            else if (type == kTypeNetwork)
                This<void>(addr::SC4QueryTool_NetworkTrafficInfo, tool, static_cast<void*>(&title),
                           static_cast<void*>(&text));
        } else if (type == kTypeFlora || type == kTypeProp) {
            This<uint8_t>(addr::SC4QueryTool_OccupantName, tool, static_cast<void*>(&title),
                          static_cast<void*>(&text));
        } else if (type == kTypeBuilding) {
            This<void>(addr::SC4QueryTool_GetBuildingOccupantTipInfo, tool, static_cast<void*>(&title),
                       static_cast<void*>(&text), &picture, &meter, &share);
            meterPicture = meter;
        } else if (type == kTypePowerPole || type == kTypeLandfill) {
            LocalizedString made;
            This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &made, kQueryTextGroup,
                        type == kTypePowerPole ? kPowerPoleTitle : kLandfillTitle, uint32_t{1});
            This<void>(addr::cRZBaseString_Assign, static_cast<void*>(&title), static_cast<void*>(&made));
            Destroy(made.text);
        } else if (type == kTypeNetwork) {
            This<void>(addr::SC4QueryTool_NetworkName, tool, static_cast<void*>(&title), static_cast<void*>(&text));
        }
    } else if (At<uint8_t>(tool, field::kFlag88)) {
        This<void>(addr::SC4QueryTool_GetCellTipInfo, tool, static_cast<void*>(&title),
                   static_cast<void*>(&text));
    } else if (!At<uint8_t>(tool, field::kFlag94)) {
        describe = false;
    }

    if (describe) {
        if (At<uint8_t>(tool, field::kFlag94)) {
            if (title.begin == title.end) {
                GameString terrain = {};
                This<void*>(addr::SC4String_FromText, &terrain, "Terrain");
                This<void>(addr::cRZBaseString_Assign, static_cast<void*>(&title), static_cast<void*>(&terrain));
                Destroy(terrain);
            }
            GameString place;
            MakeEmpty(place);
            const float placeX = At<float>(tool, field::kPlace);
            const float placeY = At<float>(tool, field::kPlace + 4);
            const float placeZ = At<float>(tool, field::kPlace + 8);
            const int32_t cellZ = Ftol(static_cast<double>(placeZ) * kSixteenth);
            const int32_t cellX = Ftol(static_cast<double>(placeX) * kSixteenth);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &place, "x=%.1f y=%.1f z=%.1f\ncell x=%d cell z=%d",
                        static_cast<double>(placeX), static_cast<double>(placeY), static_cast<double>(placeZ),
                        cellX, cellZ);
            if (text.begin != text.end)
                This<void*>(addr::cSC4Advisor_F410F00, BodyOf(&text), "\n");
            This<void>(addr::SC4String_AppendStd, BodyOf(&text), static_cast<void*>(&place.begin));
            Destroy(place);
        }
        void* const tip = At<void*>(tool, field::kTip);
        if (tip && picture) {
            uint32_t key[3] = {kImageType, kImageGroup, picture};
            ImageLoader background;
            This<void*>(addr::SC4ImageLoader_ConstructKey, &background, static_cast<uint32_t*>(key), uint32_t{1},
                        uint32_t{0});
            Virtual<void>(At<void*>(tool, field::kTip), 0x10, background.image, uint32_t{0xC}, uint32_t{0xA});
            if (meterPicture) {
                uint32_t meterKey[3] = {kImageType, kImageGroup, meterPicture};
                ImageLoader bar;
                This<void*>(addr::SC4ImageLoader_ConstructKey, &bar, static_cast<uint32_t*>(meterKey), uint32_t{1},
                            uint32_t{0});
                Virtual<void>(At<void*>(tool, field::kTip), 0x38, bar.image, uint32_t{5}, share);
                This<void>(addr::SC4ImageLoader_Destruct, &bar);
            } else {
                Virtual<void>(At<void*>(tool, field::kTip), 0x38, static_cast<void*>(nullptr), uint32_t{0}, 0.0f);
            }
            This<void>(addr::SC4QueryTool_ShowTip, tool, static_cast<void*>(&title), static_cast<void*>(&text));
            Virtual<void>(At<void*>(tool, base::kView), 0xA4, kCursorText);
            This<void>(addr::SC4ImageLoader_Destruct, &background);
        } else {
            if (tip)
                Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
            if (void* const view = At<void*>(tool, base::kView))
                Virtual<void>(view, 0xA0, kCursorText, uint32_t{0}, static_cast<void*>(&title),
                              static_cast<void*>(&text), uint32_t{0});
        }
    }
    Destroy(text);
    Destroy(title);
}

// The game's 0x004D7160 (slot 3 of the target part's vtable 0x00A90A78,
// word 0x00A90A84: the message; ret 4; `self` is the tool's +0x28). By the
// message's type (slot 4):
//   0x0A456D95 (the tool's tip timer, 0x004C4E70): the tip (ShowTipFor)
//     while +0x78 is set;
//   0x0A456D96 (the query dialog's timer, 0x004C4EE0): the view's markers
//     (view slot 49) shown again (0x007D20E0);
//   0x26D31EC2: +0x78 cleared;
//   0x66956814 (the simulator's day): on every tenth day (the clock's slot
//     26) a query window by cell or the query dialog that is running is
//     filled again: its walk (0x00B21848) over the window (0x00B2184C,
//     slot 32) for each query kept at 0x00B21840 and 0x00B21844;
//   0x69247DC7 (the traffic simulator is done): with a route line at +0x9C,
//     the line emptied (0x007DDC30) and the routes for the box at +0x30
//     made again (0x004CA460);
//   0xA6B79602 (the view changed): the tool's OnMouseMove (slot 20) at the
//     mouse position (the window manager's slot 33: real input).
// False for all.
//
// Oddities kept: 0x66956814 reads the window and the walk again only after
// a first query and does not test them again before the second; the mouse
// position's places are not set before slot 33 (0 here).
uint8_t __fastcall DoMessage(char* self, void*, void* message)
{
    char* const tool = self - field::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case message_id::kTipOff:
        At<uint8_t>(tool, field::kFlag78) = 0;
        return 0;
    case message_id::kMarkerTimer:
        This<void>(addr::SC4ViewMarkers_F7D20E0, Virtual<void*>(At<void*>(tool, base::kView), 0xC4));
        return 0;
    case message_id::kTipTimer:
        if (At<uint8_t>(tool, field::kFlag78))
            ShowTipFor(tool);
        return 0;
    case message_id::kDay: {
        void* const clock = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
        if (Virtual<uint32_t>(clock, 0x68) % 10 != 0)
            return 0;
        void* window = Global<void*>(addr::SC4QueryTool_FillCellWindow);
        if (!window)
            return 0;
        std::uintptr_t walk = Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk);
        if (!walk)
            return 0;
        if (void* const query = Global<void*>(addr::SC4QueryTool_FillCellQueries)) {
            Virtual<uint8_t>(window, 0x80, kWindowIid, walk, query);
            window = Global<void*>(addr::SC4QueryTool_FillCellWindow);
            walk = Global<std::uintptr_t>(addr::SC4QueryTool_FillCellWalk);
        }
        if (void* const query = Global<void*>(addr::SC4QueryTool_FillCellQueries + 4))
            Virtual<uint8_t>(window, 0x80, kWindowIid, walk, query);
        return 0;
    }
    case message_id::kTrafficDone:
        if (void* const line = At<void*>(tool, field::kObject9C)) {
            This<void>(addr::SC4RouteLine_Clear, line);
            This<void>(addr::SC4QueryTool_BuildRoutes, tool, tool + field::kBox);
        }
        return 0;
    case message_id::kViewChanged: {
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        int32_t mouseX = 0, mouseZ = 0;
        Virtual<uint8_t>(manager, 0x84, &mouseX, &mouseZ);
        Virtual<uint8_t>(tool, 0x50, mouseX, mouseZ, uint32_t{0});
        return 0;
    }
    default:
        return 0;
    }
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery7a(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_DescribePick, reinterpret_cast<void*>(&DescribePick)},
        {addr::SC4QueryTool_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::SC4QueryTool_DoMessage, reinterpret_cast<void*>(&DoMessage)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.query7a",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_DescribePick,
    "Our query tool, seventh piece (translator A): what a click asks about, the click and the tool's messages "
    "(docs/tools.md)",
    false,
    InstallQuery7a,
});

} // namespace
} // namespace opensc4::tool_query_7a

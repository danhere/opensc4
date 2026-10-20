// Our cSC4PropOccupantSign's logic (build 641; the class map, the vtables
// and the fields are in propkinds.h): the sign standing on the terrain, a
// prop with a line of text that the sign tool (src/core/tool_signtext.cpp)
// puts down, drags and edits, and the sign removal tool
// (src/core/tool_signpick.cpp) highlights and removes. It keeps the text, a
// position, whether it is highlighted and whether its text is being edited
// (then the caret blinks with the frames), and asks for the text's
// textures and ten texture bindings of its own. The prop underneath
// (cSC4PropOccupant) is called at its addresses.
//
// The drawing is in propkinds_sign_draw.cpp, a group of its own: the
// geometry builder 0x005F5FB0 (called here at its address, so ours or the
// game's as that group says; it lays the text and the board out from the
// textures and the transform, and tells the renderer) and the functions
// behind the model interface (the virtual base at +0x58C). The model
// part's constructor and destructor (0x00801CF0, 0x00801810) stay the
// game's.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`, or, where the callee is behind a service the code does not name,
// from the caller's stack (it reads its own frame at fixed offsets after
// the call, so the callee took what was pushed):
//   - the object's primary vtable 0x00AA5478: 0x18 SetHighlight one
//     (0x005F4BA0), 0x1C SetEditing one (0x005F7320);
//   - the prop part 0x00AA5418: 0x20 SetPropType one (0x005EF4C0);
//   - the occupant part 0x00AA5380: 0x14 IsInitialized none (0x00458A40),
//     0x74 SetFlag one (0x005E8B70), 0x7C ClearFlag one (0x005E8BA0), 0x80
//     IsFlagSet one (0x005E8BC0);
//   - the virtual base 0x00AA5298: 0x34 none (0x005F5A50 to 0x008018E0),
//     0x38 one (0x005F5A60 to 0x008018F0);
//   - the renderer 0x00ABAB60 (the global at 0x00B43DD0): 0x6C one
//     (0x007CA180), 0x70 one (0x007CA200), 0x74 one (0x007CA3D0), 0x7C two
//     (0x007C9320);
//   - the frame service 0x00A86760 (SC4::GetService86AD10EC): 0x14 two
//     (0x00448B70), 0x18 two (0x00448C70);
//   - the message server: 0x14 and 0x18 two;
//   - the texture binding factory 0x00ABD8C8 (the global at 0x00B43CD4):
//     0x18 two (0x0080A380);
//   - the service 0x00B43CE4: 0x10 three (from the caller's stack);
//   - the service 0x00B43DB8: 0x28 none (whether signs and labels are
//     shown); the visibility filter: 0x0C one;
//   - cGZMessage 0x00ACEC00: 0x10 none, 0x38 and 0x3C none (0x009D4714);
//   - the string 0x00A80810: 0x1C none (0x0090E4B6), 0x30 two (0x0090EE61),
//     0x34 one (0x0090E230);
//   - the streams 0x00AA2310 (out) and 0x00AA2280 (in): 0x1C, 0x24, 0x3C
//     one, 0x48 none.
// Direct callees: 0x005EC960 (ret 8), 0x005E89B0 (ret), 0x005E8C90 (ret),
// 0x005E8B90 (ret 4), 0x005EDE00 (ret 4), 0x005ECE90 and 0x005EF360 (ret 4),
// 0x005ECFB0 (ret 4), 0x005EE160 (ret), 0x005EE050 (ret), 0x00801CF0 (ret
// 4), 0x00801810 (ret), 0x008018E0 (ret), 0x005E8C40 (ret), 0x00527180 (ret
// 8), 0x006C6E90 (ret), 0x008090E0 (ret), 0x007D2B50 (ret 0x38), 0x005F5FB0
// (ret), 0x0090CF9A (ret), 0x0090D031 and 0x0090CFF9 (ret 4); cdecl:
// 0x00602600 (none), 0x0090CF54, 0x0090CF63 (one), 0x005DA580, 0x005DA550
// (two).
//
// Callers from outside the class: the class registration (0x00460844,
// which calls GetGZCLSID and keeps the factory's address, before any
// replacement is installed) and the sign tool's 0x004D82C5 / 0x004D82D9
// (allocator, then constructor with 1: it reads only EAX after either call
// and keeps nothing in ECX or EDX across them). The caret's clock and
// MakeGlyphs are called from the class's own functions only, the
// destructor body from the deleting destructor only. Everything else is
// reached through the vtables and their thunks. No jump or call in the exe
// lands inside the first five bytes of any of the 20. The detours are plain
// functions.
#include "core/propkinds.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::propkinds {
namespace {

constexpr uint32_t kMessageTick = 0xA6B79602;   // the geometry built again
constexpr uint32_t kMessageFilter = 0x06CDB65B; // a new visibility filter: data 0 is the filter
constexpr uint32_t kMessageFrame = 0x86AD10EE;  // a frame: its time (data 1) over 1000 goes to the caret's clock
constexpr uint32_t kFrameCode = 0x80;           // what the frame service is asked with
constexpr uint32_t kCaretPeriod = 500;          // 0x1F4: the caret flips when its clock reaches it
constexpr uint32_t kGlyphStyle = 0x2C1FD612;    // what the service 0x00B43CE4 is given after an empty name
constexpr uint32_t kOne = 0x3F800000;           // 1.0f
constexpr std::uintptr_t kPool = addr::cSC4PropOccupantSign_Pool;
constexpr uint32_t kPoolReopenAt = 0x40000022;
constexpr uint32_t kPoolPageMask = 0xFFFF0000;
constexpr uint32_t kVbaseOffset = 0x51C; // the vbptr table's second entry: the virtual base at +0x70 + 0x51C

// The ten texture bindings Init asks for: the texture 0x7AB50E44,
// 0x1ABE787D and these instances (the table at 0x00AA5214).
constexpr uint32_t kBindingType = 0x7AB50E44;
constexpr uint32_t kBindingGroup = 0x1ABE787D;
constexpr uint32_t kBindingInstances[sign::kBindingCount] = {0x8B4A6560, 0x8B4A6561, 0x8B4A6562, 0x8B4A6563, 0x8B4A6564,
                                                              0x8B4A6565, 0x8B4A6564, 0x8B4A6565, 0x8B4A6566, 0x8B4A6567};

// The colour SetHighlight gives the board's four corners, by highlight
// (the table at 0x00AA5204): its bytes as they lie, R, G, B and A in the
// vertex's order B, G, R, A.
constexpr uint32_t kHighlightColours[2] = {0xFFBEAC90, 0xCCFFFFFF};

// The virtual base (the model interface) of an object whose vbptr is set:
// the vbptr + its table's second entry.
char* VirtualBase(char* self)
{
    return self + sign::kVbptr + At<int32_t>(At<char*>(self, sign::kVbptr), 0x4);
}

// The message target part (null for a null object, as the game computes
// it).
void* Target(char* self)
{
    return self ? self + layout::kMessageTarget : nullptr;
}

bool SignsShown()
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x28) != 0;
}

// The geometry builder, at its address (propkinds_sign_draw.cpp).
void BuildGeometry(char* self)
{
    This<void>(addr::cSC4PropOccupantSign_BuildGeometry, self);
}

// The text's textures let go: the reference vector's erase of all it
// holds.
void DropGlyphs(char* self)
{
    char* const glyphs = self + sign::kGlyphs;
    This<void*, void*, void*>(addr::SC4RefVector_Erase, glyphs, At<void*>(glyphs, 0x0), At<void*>(glyphs, 0x4));
}

// ====================================================================================
// Not virtual (`self` is the object)
// ====================================================================================

// 0x005F4960 (ret 4): the caret's clock moves on by `elapsed`; when it
// reaches 500 (unsigned) the caret flips and the clock starts again at 0.
void __fastcall CaretClock(char* self, void*, uint32_t elapsed)
{
    static bool logged = false;
    FirstCall("sign CaretClock", logged);
    const uint32_t time = At<uint32_t>(self, sign::kCaretTime) + elapsed;
    At<uint32_t>(self, sign::kCaretTime) = time;
    if (time < kCaretPeriod)
        return;
    At<uint8_t>(self, sign::kCaret) = At<uint8_t>(self, sign::kCaret) == 0 ? 1 : 0;
    At<uint32_t>(self, sign::kCaretTime) = 0;
}

// 0x005F5EF0: the text's textures let go. With text (the string's slot 7),
// the service at 0x00B43CE4 (not tested for null) is asked through its
// slot 4 for new ones: a style (an empty game string of its own, then
// 0x2C1FD612), the text and the vector to fill. Without text the vector is
// emptied again.
void __fastcall MakeGlyphs(char* self, void*)
{
    static bool logged = false;
    FirstCall("sign MakeGlyphs", logged);
    DropGlyphs(self);
    if (Virtual<uint32_t>(self + sign::kText, 0x1C) == 0) {
        DropGlyphs(self);
        return;
    }
    // The style: a game string (vtable, begin, end, capacity, a zero) and
    // the dword after it.
    uint32_t style[6] = {0, 0, 0, 0, 0, 0};
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    style[1] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(buffer));
    style[2] = style[1];
    style[3] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(buffer + 8));
    *buffer = 0;
    style[0] = static_cast<uint32_t>(addr::SC4String_Vtable);
    style[4] = 0;
    style[5] = kGlyphStyle;
    Virtual<void>(Global<void*>(addr::SC4_Service43CE4), 0x10, static_cast<void*>(style), static_cast<void*>(self + sign::kText),
                  static_cast<void*>(self + sign::kGlyphs));
    style[0] = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (style[1])
        Cdecl<void>(addr::SC4_Free, reinterpret_cast<void*>(static_cast<std::uintptr_t>(style[1])));
}

// ====================================================================================
// The sign's own interface (`self` is the object)
// ====================================================================================

// Slot 0 (0x005F4930): the sign interface 0x4B44FBE2 gives the object with
// the base's AddRef on the object + 0xC (the game goes through 0x006491C0
// with the object + 4); everything else is the prop's (object + 4). The
// class ID is not answered here. AL only.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("sign QueryInterface", logged);
    if (iid == sign::kIid) {
        *object = self;
        This<uint32_t>(addr::cSC4Occupant_AddRef, self + layout::kOccupant);
        return 1;
    }
    return This<uint8_t, uint32_t, void**>(addr::cSC4PropOccupant_QueryInterface, self + layout::kProp, iid, object);
}

// Slot 4 (0x005F7600): nothing when the argument equals the text (the
// string's slot 12 with 1 answers 0) and the text is not empty. Otherwise
// the text assigned (slot 13), the glyphs made again and the geometry
// built again. No return value.
void __fastcall SetText(char* self, void*, void* source)
{
    static bool logged = false;
    FirstCall("sign SetText", logged);
    char* const text = self + sign::kText;
    if (Virtual<uint32_t>(text, 0x30, source, 1u) == 0 && At<char*>(self, sign::kTextBegin) != At<char*>(self, sign::kTextEnd))
        return;
    Virtual<void*>(text, 0x34, source);
    MakeGlyphs(self, nullptr);
    BuildGeometry(self);
}

// Slot 5 (0x005F5920): the argument assigned from the text (its slot 13, a
// tail jump); what that answers.
void* __fastcall GetText(char* self, void*, void* target)
{
    static bool logged = false;
    FirstCall("sign GetText", logged);
    return Virtual<void*>(target, 0x34, static_cast<void*>(self + sign::kText));
}

// Slot 6 (0x005F4BA0): nothing when the argument's byte equals bit 0 of the
// model part's flags (0x008018E0). Otherwise bit 0 is set (argument not 0)
// or cleared through the virtual base's slots 13 and 14, the board's four
// corners get the colour of the highlight, a shown sign (base flag 2, asked
// before) is handed to the renderer's slot 31 with 5, and a sign whose
// text is being edited and that loses the highlight stops the edit (its
// slot 7 with 0). No return value.
void __fastcall SetHighlight(char* self, void*, uint32_t highlight)
{
    static bool logged = false;
    FirstCall("sign SetHighlight", logged);
    const uint8_t on = static_cast<uint8_t>(highlight);
    if (on == (This<uint32_t>(addr::SC4Model_GetModelFlags, self + sign::kModelThis) & 1))
        return;
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    const uint8_t shown = Virtual<uint8_t>(self + layout::kOccupant, 0x80, 2u);
    char* const model = VirtualBase(self);
    uint32_t flags = Virtual<uint32_t>(model, 0x34);
    flags = on ? flags | 1u : flags & ~1u;
    Virtual<void>(model, 0x38, flags);
    const uint32_t colour = kHighlightColours[on ? 1 : 0];
    uint8_t bytes[4];
    std::memcpy(bytes, &colour, 4);
    for (std::size_t corner = 0; corner < 0x40; corner += 0x10) {
        At<uint8_t>(self, sign::kHighlight + corner + 2) = bytes[0];
        At<uint8_t>(self, sign::kHighlight + corner + 1) = bytes[1];
        At<uint8_t>(self, sign::kHighlight + corner + 0) = bytes[2];
        At<uint8_t>(self, sign::kHighlight + corner + 3) = bytes[3];
    }
    if (shown)
        Virtual<void>(renderer, 0x7C, static_cast<void*>(VirtualBase(self)), 5u);
    if (At<uint8_t>(self, sign::kEditing) && !on)
        Virtual<void>(self, sign::slot::kSetEditing, 0u);
}

// Slot 7 (0x005F7320): another value of the editing byte is kept. Editing:
// highlighted (slot 6 with 1), listening to the frame service (0x80, not
// tested for null), the caret's clock at 0 and the caret shown, the
// geometry built again. Not editing: no longer listening, the highlight
// taken off (slot 6 with 0), the geometry built again. No return value.
void __fastcall SetEditing(char* self, void*, uint32_t editing)
{
    static bool logged = false;
    FirstCall("sign SetEditing", logged);
    const uint8_t on = static_cast<uint8_t>(editing);
    if (At<uint8_t>(self, sign::kEditing) == on)
        return;
    At<uint8_t>(self, sign::kEditing) = on;
    if (on) {
        Virtual<void>(self, sign::slot::kSetHighlight, 1u);
        Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetService86AD10EC), 0x14, static_cast<void*>(self + layout::kMessageTarget),
                         kFrameCode);
        At<uint32_t>(self, sign::kCaretTime) = 0;
        At<uint8_t>(self, sign::kCaret) = 1;
        BuildGeometry(self);
        return;
    }
    Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetService86AD10EC), 0x18, static_cast<void*>(self + layout::kMessageTarget), kFrameCode);
    Virtual<void>(self, sign::slot::kSetHighlight, 0u);
    BuildGeometry(self);
}

// ====================================================================================
// The message target (`target` is the object + 8)
// ====================================================================================

// Slot 3 (0x005F7510). 0x06CDB65B (data 0 a new filter): with that filter
// taking the sign and signs and labels shown, the sign is shown (SetFlag 2)
// unless it is; otherwise it is hidden (ClearFlag 2) if it is shown. The
// frame message 0x86AD10EE: its time (data 1) over 1000 to the caret's
// clock. 0xA6B79602: the geometry built again. Every message then goes to
// the prop's DoMessage, whose answer is the answer.
uint32_t __fastcall DoMessage(char* target, void*, void* message)
{
    static bool logged = false;
    FirstCall("sign DoMessage", logged);
    char* const self = target - layout::kMessageTarget;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case kMessageFilter: {
        void* const filter = *Virtual<void**>(message, 0x3C);
        char* const occupant = self + layout::kOccupant;
        if (Virtual<uint8_t>(filter, 0xC, static_cast<void*>(self ? occupant : nullptr)) && SignsShown()) {
            if (!Virtual<uint8_t>(occupant, 0x80, 2u))
                Virtual<void>(occupant, 0x74, 2u);
        } else if (Virtual<uint8_t>(occupant, 0x80, 2u)) {
            Virtual<void>(occupant, 0x7C, 2u);
        }
        break;
    }
    case kMessageFrame:
        CaretClock(self, nullptr, Virtual<uint32_t>(message, 0x38) / 1000u);
        break;
    case kMessageTick:
        BuildGeometry(self);
        break;
    default:
        break;
    }
    return This<uint32_t, void*>(addr::cSC4PropOccupant_DoMessage, target, message);
}

// ====================================================================================
// The occupant part (`occupant` is the object + 0xC)
// ====================================================================================

// Slot 3 (0x005F73A0): nothing when initialized. A sign not read from a save
// gets its boxes (the first from 0 to 1 each way, the second from 0 to 1
// too; the other two keep theirs), the prop's box's high corner at 1 each
// way, the prop type 0xAB72FBB3 (the prop's slot 8) and base flag 0x10.
// Then the base's Init (through 0x005EC940, which tests again), the render
// state (1, 1, 1, 0, 1, 1, 0, 3, 4, 5, -1, -1.0, 1, -1), the ten texture
// bindings from the framework's factory (each one there let go first; the
// factory not tested for null), the messages 0xA6B79602 and 0x06CDB65B
// asked for, the glyphs made and the geometry built. True.
uint8_t __fastcall Init(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("sign Init", logged);
    char* const self = occupant - layout::kOccupant;
    if (Virtual<uint8_t>(occupant, 0x14))
        return 1;
    if (!(At<uint8_t>(self, layout::kBaseState) & 2)) {
        for (std::size_t i = 0; i < 12; i += 4) {
            At<uint32_t>(self, sign::kBoxes + 12 + i) = kOne;
            At<uint32_t>(self, sign::kBoxes + i) = 0;
        }
        for (std::size_t i = 0; i < 12; i += 4) {
            At<uint32_t>(self, sign::kBoxes + 0x24 + i) = kOne;
            At<uint32_t>(self, sign::kBoxes + 0x18 + i) = 0;
        }
        for (std::size_t i = 0; i < 12; i += 4)
            At<uint32_t>(self, layout::kMax + i) = kOne;
        Virtual<void>(self + layout::kProp, 0x20, sign::kClassId);
        Virtual<void>(occupant, 0x74, 0x10u);
    }
    if (!Virtual<uint8_t>(occupant, 0x14))
        This<uint8_t>(addr::cSC4Occupant_Init, occupant);
    This<void, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t,
         uint32_t, uint32_t, int32_t>(addr::SC4RenderState_Init, self + sign::kRenderState, 1u, 1u, 1u, 0u, 1u, 1u, 0u, 3u, 4u,
                                      5u, -1, 0xBF800000u, 1u, -1);
    void* const factory = Global<void*>(addr::SC4_TextureFactory43CD4);
    for (uint32_t i = 0; i < sign::kBindingCount; ++i) {
        void** const slot = reinterpret_cast<void**>(self + sign::kBindings + 4 * i);
        uint32_t key[3] = {kBindingType, kBindingGroup, kBindingInstances[i]};
        if (void* const old = *slot) {
            *slot = nullptr;
            This<void>(addr::SC4TextureBinding_Release, old);
        }
        Virtual<uint8_t>(factory, 0x18, static_cast<const uint32_t*>(key), slot);
    }
    void* const server = Global<void*>(addr::SC4_MessageServer);
    for (const uint32_t type : {kMessageTick, kMessageFilter})
        Virtual<uint8_t>(server, 0x14, Target(self), type);
    MakeGlyphs(self, nullptr);
    BuildGeometry(self);
    return 1;
}

// Slot 4 (0x005F5E20): nothing unless initialized. The two messages given
// up, the ten texture bindings let go (each set to null first), the
// glyphs let go, the frame service given up while the text is edited (the
// editing byte stays), then the prop's own Shutdown (0x005EE160). True.
uint8_t __fastcall Shutdown(char* occupant, void*)
{
    static bool logged = false;
    FirstCall("sign Shutdown", logged);
    char* const self = occupant - layout::kOccupant;
    if (!Virtual<uint8_t>(occupant, 0x14))
        return 1;
    void* const server = Global<void*>(addr::SC4_MessageServer);
    Virtual<uint8_t>(server, 0x18, Target(self), kMessageTick);
    Virtual<uint8_t>(Global<void*>(addr::SC4_MessageServer), 0x18, Target(self), kMessageFilter);
    for (uint32_t i = 0; i < sign::kBindingCount; ++i)
        if (void* const binding = At<void*>(self, sign::kBindings + 4 * i)) {
            At<void*>(self, sign::kBindings + 4 * i) = nullptr;
            This<void>(addr::SC4TextureBinding_Release, binding);
        }
    DropGlyphs(self);
    if (At<uint8_t>(self, sign::kEditing))
        Virtual<uint8_t>(Cdecl<void*>(addr::SC4_GetService86AD10EC), 0x18, Target(self), kFrameCode);
    This<uint8_t>(addr::cSC4PropOccupant_Shutdown, occupant);
    return 1;
}

// Slot 9 (0x005F4E90): the move from the position kept to the new one
// added to the last three boxes (on the FPU: the move along x stored as a
// float first, the other two not), the position kept, the transform's
// flag 1 set and its count raised, the prop's SetPosition (the box moved);
// a shown sign (base flag 2) is handed to the renderer's slot 31 with 4.
// No return value.
void __fastcall SetPosition(char* occupant, void*, const float* position)
{
    static bool logged = false;
    FirstCall("sign SetPosition", logged);
    char* const self = occupant - layout::kOccupant;
    const float dx = static_cast<float>(static_cast<double>(position[0]) - static_cast<double>(At<float>(self, sign::kPosition)));
    const double dy = static_cast<double>(position[1]) - static_cast<double>(At<float>(self, sign::kPosition + 4));
    const double dz = static_cast<double>(position[2]) - static_cast<double>(At<float>(self, sign::kPosition + 8));
    for (std::size_t box = 0x18; box < 0x60; box += 0xC) {
        float* const corner = &At<float>(self, sign::kBoxes + box);
        corner[0] = static_cast<float>(static_cast<double>(dx) + static_cast<double>(corner[0]));
        corner[1] = static_cast<float>(dy + static_cast<double>(corner[1]));
        corner[2] = static_cast<float>(dz + static_cast<double>(corner[2]));
    }
    std::memcpy(self + sign::kPosition, position, 12);
    At<uint8_t>(self, sign::kTransformFlags) |= 1;
    ++At<uint8_t>(self, sign::kTransformCount);
    This<void, const float*>(addr::cSC4PropOccupant_SetPosition, occupant, position);
    if (Virtual<uint8_t>(occupant, 0x80, 2u))
        Virtual<void>(Global<void*>(addr::SC4_View43DD0), 0x7C, static_cast<void*>(self ? VirtualBase(self) : nullptr), 4u);
}

// Slot 30 (0x005F4A90): a change of base flag 2 to set is dropped (the flag
// stays clear) while the city does not show signs and labels. When flag 2
// still changes, the model (the virtual base; null for a null object) goes
// to the renderer: shown, it is added unless the renderer has it (slots 27
// and 28); hidden, it is taken off when the renderer has it (slots 27 and
// 29). The renderer is not tested for null. Then the base's SetAllFlags. AL
// is the flags' byte, from the base.
uint32_t __fastcall SetAllFlags(char* occupant, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("sign SetAllFlags", logged);
    char* const self = occupant - layout::kOccupant;
    uint32_t changed = At<uint8_t>(self, layout::kFlags) ^ flags;
    if (changed & 2) {
        if ((flags & 2) && !SignsShown()) {
            flags &= ~2u;
            changed = At<uint8_t>(self, layout::kFlags) ^ flags;
        }
        if (changed & 2) {
            void* const renderer = Global<void*>(addr::SC4_View43DD0);
            void* const model = self ? VirtualBase(self) : nullptr;
            if (flags & 2) {
                if (!Virtual<uint8_t>(renderer, 0x6C, model))
                    Virtual<void>(renderer, 0x70, model);
            } else if (Virtual<uint8_t>(renderer, 0x6C, model)) {
                Virtual<void>(renderer, 0x74, model);
            }
        }
    }
    return This<uint32_t>(addr::cSC4Occupant_SetAllFlags, occupant, flags);
}

// ====================================================================================
// Save (`part` is the object + 0x10)
// ====================================================================================

// Slot 3 (0x005F5080): version 2, the prop's record (its Write, with the
// same `this`), then the text and the position (from a copy). When the
// prop's Write fails nothing more is written; the answer is in both cases
// whether the stream is free of errors.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("sign Write", logged);
    char* const self = part - layout::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (This<uint8_t>(addr::cSC4PropOccupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + sign::kText));
        uint32_t position[3];
        std::memcpy(position, self + sign::kPosition, 12);
        Cdecl<void*>(addr::SC4Stream_WriteVector3, stream, static_cast<void*>(position));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x005F50F0): the version (16 bits, read into the argument's stack
// slot, the stream pointer until then), the prop's record; when that is
// read, the text, in version 1 a dword that is dropped, and the position
// (read into a copy that starts as the game's zero float, then kept), the
// transform's flag 1 set and its count raised. No version is refused. The
// answer is whether the stream is free of errors.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("sign Read", logged);
    char* const self = part - layout::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (This<uint8_t>(addr::cSC4PropOccupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x3C, static_cast<void*>(self + sign::kText));
        if (static_cast<uint16_t>(version) == 1) {
            uint32_t dropped = 0;
            Virtual<uint8_t>(stream, 0x24, &dropped);
        }
        const uint32_t zero = Global<uint32_t>(addr::cSC4PropOccupantSign_Zero);
        uint32_t position[3] = {zero, zero, zero};
        Cdecl<void*>(addr::SC4Stream_ReadVector3, stream, static_cast<void*>(position));
        std::memcpy(self + sign::kPosition, position, 12);
        At<uint8_t>(self, sign::kTransformFlags) |= 1;
        ++At<uint8_t>(self, sign::kTransformCount);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 5 (0x005F5940).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("sign GetGZCLSID", logged);
    return sign::kClassId;
}

// ====================================================================================
// Installation
// ====================================================================================

// GetGZCLSID's code is six bytes (mov eax, the ID; ret) and GetText's first
// two instructions six: the detour's jump covers whole instructions.
const Detour kSignDetours[] = {
    {addr::cSC4PropOccupantSign_QueryInterface, reinterpret_cast<void*>(&QueryInterface)},
    {addr::cSC4PropOccupantSign_CaretClock, reinterpret_cast<void*>(&CaretClock)},
    {addr::cSC4PropOccupantSign_SetAllFlags, reinterpret_cast<void*>(&SetAllFlags)},
    {addr::cSC4PropOccupantSign_SetHighlight, reinterpret_cast<void*>(&SetHighlight)},
    {addr::cSC4PropOccupantSign_SetPosition, reinterpret_cast<void*>(&SetPosition)},
    {addr::cSC4PropOccupantSign_Write, reinterpret_cast<void*>(&Write)},
    {addr::cSC4PropOccupantSign_Read, reinterpret_cast<void*>(&Read)},
    {addr::cSC4PropOccupantSign_GetText, reinterpret_cast<void*>(&GetText)},
    {addr::cSC4PropOccupantSign_GetGZCLSID, reinterpret_cast<void*>(&GetGZCLSID)},
    {addr::cSC4PropOccupantSign_Shutdown, reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4PropOccupantSign_MakeGlyphs, reinterpret_cast<void*>(&MakeGlyphs)},
    {addr::cSC4PropOccupantSign_SetEditing, reinterpret_cast<void*>(&SetEditing)},
    {addr::cSC4PropOccupantSign_Init, reinterpret_cast<void*>(&Init)},
    {addr::cSC4PropOccupantSign_DoMessage, reinterpret_cast<void*>(&DoMessage)},
    {addr::cSC4PropOccupantSign_SetText, reinterpret_cast<void*>(&SetText)},
};

bool InstallSign(registry::Mode) { return InstallDetours(kSignDetours, "sign"); }

const bool g_registered = registry::Register({
    "simulators.propkinds.sign",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantSign_QueryInterface,
    "Our cSC4PropOccupantSign's logic (the sign on the terrain): QueryInterface, the text, highlight and editing, the "
    "caret's clock, the text's glyphs, the messages, Init, Shutdown, SetPosition, SetAllFlags, Write, Read and GetGZCLSID "
    "(15 detours; the drawing is simulators.propkinds.sign_draw; src/core/propkinds.h)",
    false,
    InstallSign,
});

// ====================================================================================
// Our object
// ====================================================================================

// Copies of the game's nine vtables, taken at the first construction,
// after every PostAppInit slot swap of the other groups (the prop's
// GetRandomChance in 0x00AA5418). The virtual base table 0x00AA5498 is data
// and stays the game's.
constexpr int kSlots0 = 8, kSlots4 = 24, kSlots8 = 4, kSlotsC = 34, kSlots10 = 6, kSlots14 = 25, kSlots28 = 5, kSlots6C = 1,
              kSlotsVbase = 19;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable10[kSlots10];
void* g_vtable14[kSlots14];
void* g_vtable28[kSlots28];
void* g_vtable6C[kSlots6C];
void* g_vtableVbase[kSlotsVbase];

void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags);

// The holder part's slot 22 (0x005F59A0).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - layout::kHolder, nullptr, flags);
}

// The model part's slot 0 (0x005F59C0).
void* __fastcall DeletingDestructorAtModel(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - sign::kModelPart, nullptr, flags);
}

void BuildVtables()
{
    static bool built = false;
    if (built)
        return;
    built = true;
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4PropOccupant_DerivedVftable_AA5418), sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_8), sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_C), sizeof(g_vtableC));
    std::memcpy(g_vtable10, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_10), sizeof(g_vtable10));
    std::memcpy(g_vtable14, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_14), sizeof(g_vtable14));
    std::memcpy(g_vtable28, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_28), sizeof(g_vtable28));
    std::memcpy(g_vtable6C, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_6C), sizeof(g_vtable6C));
    std::memcpy(g_vtableVbase, reinterpret_cast<const void*>(addr::cSC4PropOccupantSign_vftable_58C), sizeof(g_vtableVbase));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable10[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtable14[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable6C[0] = reinterpret_cast<void*>(&DeletingDestructorAtModel);
    log::Write("propkinds sign object: vtables copied (+0 slots 4/5/6/7 %p %p %p %p, +8 slot 3 %p, +C slots 3/4/9/30 %p %p %p %p)",
               g_vtable[4], g_vtable[5], g_vtable[6], g_vtable[7], g_vtable8[3], g_vtableC[3], g_vtableC[4], g_vtableC[9],
               g_vtableC[30]);
}

// The nine vtables; the virtual base's through the vbptr, and its
// displacement (the vbptr table's second entry less 0x51C) before it.
void SetVtables(char* self)
{
    BuildVtables();
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, layout::kProp) = g_vtable4;
    At<void**>(self, layout::kMessageTarget) = g_vtable8;
    At<void**>(self, layout::kOccupant) = g_vtableC;
    At<void**>(self, layout::kSerializable) = g_vtable10;
    At<void**>(self, layout::kHolder) = g_vtable14;
    At<void**>(self, layout::kExemplarPart) = g_vtable28;
    At<void**>(self, sign::kModelPart) = g_vtable6C;
    const int32_t offset = At<int32_t>(At<char*>(self, sign::kVbptr), 0x4);
    At<void**>(self, sign::kVbptr + offset) = g_vtableVbase;
    At<int32_t>(self, sign::kModelPart + offset) = offset - static_cast<int32_t>(kVbaseOffset);
}

// The constructor (detour of 0x005F5510): as the most derived object (the
// argument not 0) the vbptr and the virtual base's first vtable; then the
// prop constructed at its address on the object + 4, the model part's
// constructor (as a base, 0), our vtables, an empty text (8 bytes from the
// game's allocator, not tested for null), not editing, the transform's
// count and flags 0, the rotation the identity and the position 0 (after
// the game's zero float in all twelve), scale 1, the four boxes from
// (1, 1, 1) to (0, 0, 0) (after the zero float), and the buffer, the eleven
// texture bindings, the glyphs and the render state empty. (The game
// stores an abstract vtable, 0x00AA5268, at +0 before the prop's
// constructor, which nothing reads; and it ends with a test of the
// transform's flags, just set to 0, whose body never runs.)
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("sign Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, sign::kVbptr) = addr::cSC4PropOccupantSign_vbtable;
        At<std::uintptr_t>(self, sign::kVirtualBase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    This<void*>(addr::cSC4PropOccupant_cSC4PropOccupant, self + layout::kProp);
    This<void*, uint32_t>(addr::SC4Model_ConstructModelPart, self + sign::kModelPart, 0u);
    SetVtables(self);
    At<char*>(self, sign::kTextBegin) = nullptr;
    At<char*>(self, sign::kTextEnd) = nullptr;
    At<char*>(self, sign::kTextCapacity) = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    At<char*>(self, sign::kTextBegin) = buffer;
    At<char*>(self, sign::kTextEnd) = buffer;
    At<char*>(self, sign::kTextCapacity) = buffer + 8;
    *buffer = 0;
    At<std::uintptr_t>(self, sign::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, sign::kTextZero) = 0;
    At<uint8_t>(self, sign::kEditing) = 0;
    At<uint8_t>(self, sign::kTransformCount) = 0;
    At<uint8_t>(self, sign::kTransformFlags) = 0;
    const uint32_t zero = Global<uint32_t>(addr::cSC4PropOccupantSign_Zero);
    for (std::size_t i = 0; i < 48; i += 4) // the rotation and the position
        At<uint32_t>(self, sign::kRotation + i) = zero;
    for (std::size_t i = 0; i < 36; i += 4)
        At<uint32_t>(self, sign::kRotation + i) = i % 16 == 0 ? kOne : 0;
    At<uint32_t>(self, sign::kScale) = kOne;
    for (std::size_t i = 0; i < 12; i += 4)
        At<uint32_t>(self, sign::kPosition + i) = 0;
    for (std::size_t box = 0; box < 0x60; box += 0x18) {
        for (std::size_t i = 0; i < 24; i += 4)
            At<uint32_t>(self, sign::kBoxes + box + i) = zero;
        for (std::size_t i = 0; i < 12; i += 4) {
            At<uint32_t>(self, sign::kBoxes + box + i) = kOne;
            At<uint32_t>(self, sign::kBoxes + box + 12 + i) = 0;
        }
    }
    for (std::size_t i = 0; i < 12; i += 4)
        At<uint32_t>(self, sign::kGlyphVertices + i) = 0;
    for (std::size_t i = 0; i < 44; i += 4) // the one binding and the ten
        At<uint32_t>(self, sign::kBinding + i) = 0;
    for (std::size_t i = 0; i < 20; i += 4) // the glyphs and the render state
        At<uint32_t>(self, sign::kGlyphs + i) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("propkinds sign object: constructed %p", static_cast<void*>(self));
    return self;
}

// The destructor's body (detour of 0x005F5D80; the deleting destructor is
// its one caller): the glyphs released and their buffer freed, the eleven
// texture bindings released from the last to the first (not set to null),
// the buffer at +0x4BC freed, the text's buffer freed, the model part
// destroyed (null for a null object), then the occupant base. Like the
// text decal's it does not store the vtables first.
void __fastcall Destroy(char* self, void*)
{
    static bool logged = false;
    FirstCall("sign Destroy", logged);
    This<void>(addr::SC4RefVector_Destroy, self + sign::kGlyphs);
    for (std::size_t i = 44; i > 0; i -= 4)
        if (void* const binding = At<void*>(self, sign::kBinding + i - 4))
            This<void>(addr::SC4TextureBinding_Release, binding);
    if (void* const buffer = At<void*>(self, sign::kGlyphVertices))
        Cdecl<void>(addr::SC4_Free, buffer);
    At<std::uintptr_t>(self, sign::kText) = addr::SC4String_Vtable;
    if (char* const text = At<char*>(self, sign::kTextBegin))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text));
    This<void>(addr::SC4Model_DestroyModelPart, self ? self + sign::kModelPart : nullptr);
    This<void>(addr::cSC4Occupant_Destroy, self ? self + layout::kOccupant : nullptr);
}

// The deleting destructor (detour of 0x005F5D20): the body, and with flag 1
// the memory back to the pool (64 KB pages). A null object is still
// destroyed first, as in the game.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("sign DeletingDestructor", logged);
    Destroy(self, nullptr);
    if ((flags & 1) && self)
        PoolFree(kPool, self, kPoolReopenAt, kPoolPageMask);
    return self;
}

// The pool allocator (detour of 0x005F5050; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("sign Allocate", logged);
    return PoolAllocate(kPool);
}

// The factory (detour of 0x005F5CE0): an item of the pool, constructed as
// the most derived object, not tested for null; the object itself (the sign
// interface) is what it hands out.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("sign Create", logged);
    return Construct(static_cast<char*>(PoolAllocate(kPool)), nullptr, 1u);
}

const Detour kSignObjectDetours[] = {
    {addr::cSC4PropOccupantSign_Construct, reinterpret_cast<void*>(&Construct)},
    {addr::cSC4PropOccupantSign_Allocate, reinterpret_cast<void*>(&Allocate)},
    {addr::cSC4PropOccupantSign_Create, reinterpret_cast<void*>(&Create)},
    {addr::cSC4PropOccupantSign_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestructor)},
    {addr::cSC4PropOccupantSign_Destroy, reinterpret_cast<void*>(&Destroy)},
};

// Every construction path: the factory (through the class registration's
// pointer when a save is read) and the sign tool's allocator and
// constructor pair (the game's, or ours in tool_signtext.cpp, which calls
// the same addresses). The deleting destructor is reached through the
// holder part's slot 22 and the model part's slot 0: ours in the copies, and
// by the detour for the game's thunks.
bool InstallSignObject(registry::Mode) { return InstallDetours(kSignObjectDetours, "sign object"); }

const bool g_registeredObject = registry::Register({
    "simulators.propkinds.sign_object",
    "simulators/propocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PropOccupantSign_Construct,
    "Our cSC4PropOccupantSign object: the constructor, the pool allocation, the factory, the destructor and the deleting "
    "destructor (5 detours), identity; methods from copies of the game vtables taken once (src/core/propkinds.h)",
    false,
    InstallSignObject,
});

} // namespace
} // namespace opensc4::propkinds

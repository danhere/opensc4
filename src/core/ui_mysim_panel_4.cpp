// Three large functions of the MySim panel, 0x0076FDB0 to 0x0077141E: the
// rows of its advice list and the dialog that lets the player pick the MySim's
// car or its walking figure. Replacement, off by default:
//
//   ui.mysim_panel.part4: the dialog's page of choices (0x0076FDB0), opening
//       the dialog and taking its answer (0x00770420), and the advice list's
//       rows (0x00770740).
//
// The panel's layout is in ui_mysim_panel_1.cpp. Fields used only here: +0x38
// the list of the chosen MySim's advices (sentinel node), +0x5C how many
// choices the dialog found, +0x60 choices on a page (28 cars or 14 figures),
// +0x64 the page, +0x68 the number on the first of the six page buttons,
// +0x6C the chosen button's ID, +0x70 the wealth tab (cars only), +0x74 a
// byte: 0 the dialog offers cars, else walking figures, +0x78 the exemplar
// picked last (counted).
//
// The 28 HTML pieces of the rows are static strings of the game (0x00B4DB30
// to 0x00B4DD60, 0x14 bytes each) that the original builds on the first call,
// guarded by a bit each at 0x00B4DD60; ours builds the same objects under the
// same bits, so either side may run first. None of this draws.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

namespace panel {
constexpr std::size_t kWindow = 0x10;     // the panel's window (cIGZWinGen)
constexpr std::size_t kList = 0x18;       // the advice list shown (interface 0xCA1492A2)
constexpr std::size_t kMySims = 0x1C;     // five cISC4MySim, one per place
constexpr std::size_t kAdvices = 0x38;    // list of the chosen MySim's advices
constexpr std::size_t kCurrent = 0x48;    // the chosen place, 0 to 4
constexpr std::size_t kTotal = 0x5C;      // choices the dialog found
constexpr std::size_t kPerPage = 0x60;    // choices on a page
constexpr std::size_t kPage = 0x64;
constexpr std::size_t kPageBase = 0x68;
constexpr std::size_t kChoice = 0x6C;     // the chosen button's ID
constexpr std::size_t kTab = 0x70;        // the wealth tab, 0 to 2
constexpr std::size_t kFigures = 0x74;    // byte: the dialog offers walking figures, not cars
constexpr std::size_t kPicked = 0x78;     // the exemplar picked last
} // namespace panel

// The static strings, in the order the game builds them.
namespace piece {
constexpr int32_t kGrey = 0;
constexpr int32_t kBlue = 1;
constexpr int32_t kGreen = 2;
constexpr int32_t kRed = 3;
constexpr int32_t kBlinkRed = 4;
constexpr int32_t kTitleClose = 6;
constexpr int32_t kMessageClose = 7;
constexpr int32_t kBlinkClose = 8;
constexpr int32_t kFace = 9;
constexpr int32_t kMessageBlue = 10;
constexpr int32_t kMessageGrey = 11;
constexpr int32_t kImages = 12; // sixteen, four for each of four moods
constexpr int32_t kCount = 28;
} // namespace piece

constexpr const char* kPieceTexts[piece::kCount] = {
    "<FONT COLOR=\"#8e9499\"",
    "<FONT COLOR=\"#3f4967\"",
    "<FONT COLOR=\"#095a22\"",
    "<FONT COLOR=\"#b00000\"",
    "<BLINK><FONT COLOR=\"#f70a0b\"",
    "<FONT COLOR=\"#0f0f0f\"",
    "</B></FONT>",
    "</I></FONT>",
    "</B></FONT></BLINK>",
    " FACE=\"StocletITC TT\" SIZE=3><B>",
    "<FONT COLOR=\"#3f4967\" FACE=\"Arta\" SIZE=3><I>",
    "<FONT COLOR=\"#8e9499\" FACE=\"Arta\" SIZE=3><I>",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416250\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416251\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416252\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416253\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416254\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416255\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416256\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416257\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416258\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/14416259\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/1441625A\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/1441625B\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/1441625C\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/1441625D\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/1441625E\">",
    "<IMG SRC=\"sc4://HTML/46a006b0/1441625F\">",
};

constexpr uint32_t kListStyle = 0x14416264;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kImageWinIid = 0xC12CEA13;
constexpr uint32_t kExemplarIid = 0xA52160F5;

// Windows of the dialog: the choices' buttons and pictures, the six page
// buttons, the three wealth tabs, and the key of what a button stands for.
constexpr uint32_t kChoiceButtons = 0x34560000;
constexpr uint32_t kChoicePictures = 0x23450000;
constexpr uint32_t kPageButtons = 0xCC07EA2D;
constexpr uint32_t kTabButtons = 0x2C201CB0;
constexpr uint32_t kChoiceData = 0x8C0855D4;
constexpr uint32_t kDialogWindow = 0xCBF32603;

// Properties of a car's or a figure's exemplar.
constexpr uint32_t kWealthProperty = 0xEBFC5E26;
constexpr uint32_t kGenderProperty = 0x2C0C922E;
constexpr uint32_t kOrderProperty = 0xEBFC5FBA;
constexpr uint32_t kBitmapProperty = 0xEBFC5E5E;
constexpr uint32_t kNameProperty = 0x899AFBAD;
constexpr uint32_t kNameKeyProperty = 0x8A416A99;
constexpr uint32_t kExemplarName = 0x20;

constexpr std::size_t kModelRecord = 0x6C; // one record of a model list
constexpr std::size_t kModelExemplar = 0x30;

struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// An image by resource key (0x30 bytes, vtable 0x00A856CC).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t rest[10];
};
static_assert(sizeof(ImageLoader) == 0x30);

struct ModalGuard {
    uint8_t flags[4];
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// A node of the game's red-black tree keyed by a signed number.
struct TreeNode {
    uint8_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    int32_t key;
    void* value;
};
static_assert(sizeof(TreeNode) == 0x18);

struct Tree {
    TreeNode* header;
    uint32_t count;
};

struct TreePair {
    int32_t key;
    void* value;
};

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

void* Allocate(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_stl_allocate, size);
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

void* AsWin(void* object)
{
    return Virtual<void*>(object, 0xC); // AsIGZWin
}

void SetWindowFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<uint32_t>(window, 0x110, flag, on);
}

void ShowChild(void* window, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, show, uint32_t{0});
}

char* CurrentMySim(char* self)
{
    return At<char*>(self, panel::kMySims + 4 * static_cast<std::size_t>(At<int32_t>(self, panel::kCurrent)));
}

GameString* Piece(int32_t index)
{
    return reinterpret_cast<GameString*>(addr::SC4MySimPanel_Strings +
                                         0x14 * static_cast<std::uintptr_t>(piece::kCount - 1 - index));
}

// The game's destructor stub of a piece, as it registers them with atexit.
std::uintptr_t PieceAtExit(int32_t index)
{
    if (index < 5)
        return 0x00A7DFD0 - 0x20 * static_cast<std::uintptr_t>(index);
    return 0x00A7E2B0 - 0x20 * static_cast<std::uintptr_t>(index - 5);
}

// Builds the pieces that are not built yet, each under its own bit.
void BuildPieces()
{
    uint32_t& built = Global<uint32_t>(addr::SC4MySimPanel_StringsBuilt);
    for (int32_t index = 0; index < piece::kCount; ++index) {
        const uint32_t bit = uint32_t{1} << index;
        if (built & bit)
            continue;
        built |= bit;
        This<void*>(addr::SC4String_FromText, Piece(index), kPieceTexts[index]);
        Cdecl<int32_t>(addr::SC4_atexit, PieceAtExit(index));
    }
}

void MakeEmpty(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const memory = static_cast<char*>(Allocate(8));
    text.body.begin = memory;
    text.body.end = memory;
    text.body.capacity = memory + 8;
    *memory = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// A copy of a piece made by the game's substring constructor.
void MakeCopyOfPiece(GameString& text, int32_t index, uint8_t* tag)
{
    This<void>(addr::SC4String_CopyBuffer, &text.body, &Piece(index)->body, uint32_t{0}, uint32_t{0xFFFFFFFF}, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void MakeCopy(GameString& text, const char* begin, const char* end, uint8_t* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::cSC4Advice_F408420, &text.body, begin, end, tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void AssignRange(GameString& text, int32_t index, uint8_t* tag)
{
    const StringBody& from = Piece(index)->body;
    This<void*>(addr::SC4String_AssignRange, &text.body, from.begin, from.end, tag);
}

void AssignString(GameString& text, int32_t index)
{
    This<void*>(addr::cRZBaseString_Assign, &text, Piece(index));
}

void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.body.begin)
        Free(text.body.begin);
}

void FreeBody(StringBody& body)
{
    if (body.begin)
        Free(body.begin);
}

// The game's 0x0076FDB0. Fills the dialog's page: gathers the cars of the
// chosen wealth tab or the walking figures of the MySim's gender from the
// model list, sorted by their order property; gives each button of the page a
// tip, the exemplar and a picture; hides the buttons left over; numbers the
// six page buttons.
void __fastcall FillChoices(char* self, void*, void* window)
{
    void* const models =
        Global<void*>(At<uint8_t>(self, panel::kFigures) ? addr::SC4_Service43D48 : addr::SC4_Service43D44);
    char** const records = Virtual<char**>(models, 0x70);

    Tree tree;
    tree.header = nullptr;
    tree.header = static_cast<TreeNode*>(Allocate(0x18));
    tree.count = 0;
    tree.header->colour = 0;
    tree.header->parent = nullptr;
    tree.header->left = tree.header;
    tree.header->right = tree.header;

    char* const mysim = CurrentMySim(self);
    char* const recordsEnd = records[1];
    // 1 unless the MySim is male; later also the tag the string helpers take.
    uint8_t flag = 1;
    if (mysim && Virtual<uint8_t>(mysim, 0x48)) // IsMale
        flag = 0;

    for (char* record = records[0]; record != recordsEnd; record += kModelRecord) {
        void* const exemplar = At<void*>(record, kModelExemplar);
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        int32_t order = 0;
        uint32_t wealth;
        if (!Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kWealthProperty, &wealth))
            continue;
        if (!At<uint8_t>(self, panel::kFigures)) {
            if (wealth != At<uint32_t>(self, panel::kTab))
                continue;
        } else {
            uint32_t gender;
            if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kGenderProperty, &gender)) {
                if ((gender == 0) != (flag == 0))
                    continue;
            }
        }
        Cdecl<uint8_t>(addr::SC4_GetPropertySint32, holder, kOrderProperty, &order);

        TreePair pair;
        pair.key = order;
        pair.value = exemplar;
        TreeNode* parent = tree.header;
        TreeNode* node = tree.header->parent;
        while (node) {
            parent = node;
            node = order < node->key ? node->left : node->right;
        }
        TreeNode* inserted;
        This<void>(addr::SC4Sint32Multimap_Insert, &tree, &inserted, node, parent, &pair,
                   static_cast<TreeNode*>(nullptr));
    }

    const int32_t perPage = At<int32_t>(self, panel::kPerPage);
    const int32_t total = static_cast<int32_t>(tree.count);
    int32_t pages = (perPage + total - 1) / perPage;
    At<int32_t>(self, panel::kTotal) = total;
    if (pages == 0)
        pages = 1;
    if (At<int32_t>(self, panel::kPage) < 0)
        At<int32_t>(self, panel::kPage) = 0;
    if (At<int32_t>(self, panel::kPage) >= pages)
        At<int32_t>(self, panel::kPage) = pages - 1;

    TreeNode* const end = tree.header;
    TreeNode* at = tree.header->left;
    int32_t first = At<int32_t>(self, panel::kPage) * perPage;
    if (first > total)
        first = total;
    Cdecl<void>(addr::SC4TreeIterator_Advance, &at, first, &flag);

    int32_t index = 0;
    if (perPage > 0) {
        do {
            if (at == end)
                break;
            void* button = nullptr;
            void* picture = nullptr;
            void* const exemplar = at->value;
            void* const holder = Virtual<void*>(exemplar, 0x1C);
            if (Virtual<uint8_t>(window, 0x94, kChoiceButtons + static_cast<uint32_t>(index), kButtonIid, &button)) {
                GameString tip;
                MakeEmpty(tip);
                Virtual<void>(AsWin(button), 0x114);
                if (!Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kNameProperty, &tip) &&
                    !Cdecl<uint8_t>(addr::SC4_GetPropertyKeyString, &tip, holder, kNameKeyProperty, uint32_t{0}) &&
                    !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kExemplarName, &tip)) {
                    static const char kNoName[] = "(Automata needs a name!)";
                    GameString none;
                    none.body.begin = nullptr;
                    none.body.end = nullptr;
                    none.body.capacity = nullptr;
                    This<void>(addr::SC4String_FromRange, &none.body, &kNoName[0], &kNoName[sizeof(kNoName) - 1],
                               &flag);
                    char* const begin = none.body.begin;
                    none.zero = 0;
                    This<void*>(addr::SC4String_AssignRange, &tip.body, begin, none.body.end, &flag);
                    none.vtable = addr::SC4String_Vtable;
                    if (begin)
                        Free(begin);
                }
                Virtual<void>(button, 0x8C, &tip);
                void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
                void* const data = memory ? This<void*>(addr::SC4ScriptNumber_FromObject, memory, exemplar) : nullptr;
                Virtual<void>(AsWin(button), 0x1C8, kChoiceData, data);
                DestroyString(tip);
            }
            if (Virtual<uint8_t>(window, 0x94, kChoicePictures + static_cast<uint32_t>(index), kImageWinIid,
                                 &picture)) {
                Virtual<void>(AsWin(picture), 0x114);
                uint32_t bitmap;
                if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kBitmapProperty, &bitmap)) {
                    ImageLoader loader;
                    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, 0x4C06F888u, bitmap,
                                uint32_t{1}, uint32_t{0});
                    if (loader.image)
                        Virtual<void>(picture, 0x10, loader.image);
                    This<void>(addr::SC4ImageLoader_Destruct, &loader);
                }
            }
            if (picture)
                Release(picture);
            if (button)
                Release(button);

            ++index;
            // The next node in order.
            if (at->right) {
                at = at->right;
                while (at->left)
                    at = at->left;
            } else {
                TreeNode* up = at->parent;
                while (at == up->right) {
                    at = up;
                    up = up->parent;
                }
                if (at->right != up)
                    at = up;
            }
        } while (index < At<int32_t>(self, panel::kPerPage));
    }

    for (; index < At<int32_t>(self, panel::kPerPage); ++index) {
        ShowChild(window, kChoiceButtons + static_cast<uint32_t>(index), 0);
        ShowChild(window, kChoicePictures + static_cast<uint32_t>(index), 0);
    }

    const uint32_t page = At<uint32_t>(self, panel::kPage);
    At<uint32_t>(self, panel::kPageBase) = 0;
    if (page >= 6)
        At<uint32_t>(self, panel::kPageBase) = page - 5;
    for (uint32_t slot = 0; slot < 6; ++slot) {
        const int32_t number = static_cast<int32_t>(At<uint32_t>(self, panel::kPageBase) + slot);
        void* button = nullptr;
        GameString text;
        MakeEmpty(text);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", number + 1);
        if (Virtual<uint8_t>(window, 0x94, kPageButtons + slot, kButtonIid, &button)) {
            Virtual<void>(AsWin(button), 0x128, &text);
            SetWindowFlag(AsWin(button), 2, number < pages);
            // White for the page shown, red for the others.
            const uint32_t colour = number == At<int32_t>(self, panel::kPage) ? 0xFFFFFFFFu : 0xFF0000FFu;
            Virtual<void>(button, 0x58, uint32_t{0}, colour);
        }
        DestroyString(text);
        if (button)
            Release(button);
    }

    if (tree.count != 0) {
        TreeNode* node = tree.header->parent;
        while (node) {
            This<void>(addr::SC4UI_F76E9B0, &tree, node->right);
            TreeNode* const left = node->left;
            Free(node);
            node = left;
        }
        tree.header->left = tree.header;
        tree.header->parent = nullptr;
        tree.header->right = tree.header;
        tree.count = 0;
    }
    if (tree.header)
        Free(tree.header);
}

// The game's 0x00770420. Opens the dialog for the chosen MySim's car (0) or
// walking figure (not 0), runs it, and on a yes gives the MySim the exemplar
// of the chosen button and the panel's vehicle button its picture.
void __fastcall PickModel(char* self, void*, uint32_t figures)
{
    if (void* const old = At<void*>(self, panel::kPicked)) {
        At<void*>(self, panel::kPicked) = nullptr;
        Release(old);
    }
    const uint8_t which = static_cast<uint8_t>(figures);
    At<uint8_t>(self, panel::kFigures) = which;
    char* const mysim = CurrentMySim(self);
    if (!mysim)
        return;
    AddRef(mysim);

    ModalGuard guard;
    This<void*>(addr::SC4ModalGuard_Construct, &guard, uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    uint32_t key[3] = {0, 0x96A006B0, which ? 0xABFAEF15u : 0x4BF325E8u};
    void* loaded = nullptr;
    void* const own = AsWin(At<void*>(self, panel::kWindow));
    void* const parent = Virtual<void*>(own, 0x2C); // GetParentWin
    if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], parent, kDialogWindow, kWinGenIid, &loaded)) {
        void* const dialog = AsWin(loaded);
        Virtual<void>(loaded, 0x1C, self); // SetWinProc

        GameString name;
        This<void*>(addr::SC4String_FromIGZString, &name, Virtual<void*>(Virtual<void*>(mysim, 0xC), 0x20));
        void* const panelParent = Virtual<void*>(AsWin(At<void*>(self, panel::kWindow)), 0x2C);
        Cdecl<uint8_t>(addr::SC4Win_SetChildText, panelParent, 0xABF32693u, &name);

        if (Virtual<void*>(dialog, 0x8C, kTabButtons)) {
            const int32_t wealth = Virtual<int32_t>(mysim, 0x4C); // GetWealth
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, dialog, kTabButtons, uint32_t{1});
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, dialog, kTabButtons + 1, static_cast<uint32_t>(wealth >= 2));
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, dialog, kTabButtons + 2, static_cast<uint32_t>(wealth >= 3));
            if (At<int32_t>(self, panel::kTab) > wealth - 1)
                At<int32_t>(self, panel::kTab) = wealth - 1;
            Cdecl<void>(addr::SC4Win_SelectChildInRange, dialog, kTabButtons, uint32_t{3},
                        At<uint32_t>(self, panel::kTab) + kTabButtons);
        }
        Cdecl<void>(addr::SC4Win_SetChildrenTarget, dialog, dialog);
        At<uint32_t>(self, panel::kPage) = 0;
        At<uint32_t>(self, panel::kPerPage) = which ? 0xE : 0x1C;
        FillChoices(self, nullptr, dialog);
        const uint32_t perPage = At<uint32_t>(self, panel::kPerPage);
        At<uint32_t>(self, panel::kChoice) = kChoiceButtons;
        Cdecl<void>(addr::SC4Win_SelectChildInRange, dialog, kChoiceButtons, perPage, kChoiceButtons);

        Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, dialog, uint32_t{1});
        const uint32_t answer = Virtual<uint32_t>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, dialog); // DoModalWin
        Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, dialog);
        if (answer) {
            void* button = nullptr;
            if (Virtual<uint8_t>(dialog, 0x94, At<uint32_t>(self, panel::kChoice), kButtonIid, &button)) {
                void* data;
                if (Virtual<uint8_t>(AsWin(button), 0x1C4, kChoiceData, &data)) {
                    void** const picked = This<void**>(addr::SC4RefPtr_Reset, self + panel::kPicked);
                    // The second call is IsActive.
                    if (Virtual<uint8_t>(data, 0xF0, kExemplarIid, picked) && Virtual<uint8_t>(mysim, 0x50)) {
                        void* const exemplar = At<void*>(self, panel::kPicked);
                        if (which)
                            Virtual<uint8_t>(mysim, 0xE8, exemplar); // SetPedestrian
                        else
                            Virtual<uint8_t>(mysim, 0xE4, exemplar); // SetCar
                        void* const holder = Virtual<void*>(At<void*>(self, panel::kPicked), 0x1C);
                        uint32_t bitmap;
                        if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kBitmapProperty, &bitmap))
                            This<void>(addr::SC4UI_F76EE90, self, bitmap, figures);
                    }
                }
            }
            if (button)
                Release(button);
        }
        Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0x5C, dialog);
        DestroyString(name);
    }
    if (loaded)
        Release(loaded);
    This<void>(addr::SC4ModalGuard_Destruct, &guard);
    Release(mysim);
}

// The game's 0x00770740. Fills the advice list with a row for each advice of
// the chosen MySim: a title and a message in the colours of the advice's mood
// (grey when the advice is not active) and the mood's images.
void __fastcall FillAdviceList(char* self, void*)
{
    BuildPieces();
    if (!At<void*>(self, panel::kList))
        return;
    Virtual<void>(At<void*>(self, panel::kList), 0x1C);
    Virtual<void>(At<void*>(self, panel::kList), 0x18, kListStyle);
    char* const mysim = CurrentMySim(self);
    if (!mysim)
        return;
    if (!Virtual<uint8_t>(mysim, 0x50)) // IsActive
        return;

    ListNode* const end = At<ListNode*>(self, panel::kAdvices);
    ListNode* node = end->next;
    if (node != end) {
        // The game pushes the whole dword its byte result sits in; the other
        // three bytes are whatever the stack held.
        uint32_t flagged = 0;
        do {
            void* const advice = node->value;
            if (advice)
                AddRef(advice);
            const uint32_t mood = Virtual<uint32_t>(advice, 0x18); // GetMood
            uint8_t tag = 0;
            GameString titleOpen;
            GameString titleClose;
            GameString images[4];
            GameString messageOpen;
            MakeEmpty(titleOpen);
            MakeCopyOfPiece(titleClose, piece::kTitleClose, &tag);
            MakeEmpty(images[0]);
            MakeEmpty(images[1]);
            MakeEmpty(images[2]);
            MakeEmpty(images[3]);
            MakeCopyOfPiece(messageOpen, piece::kMessageBlue, &tag);

            switch (mood) {
            case 0:
                AssignString(titleClose, piece::kBlinkClose);
                AssignString(titleOpen, piece::kBlinkRed);
                for (int32_t image = 0; image < 4; ++image)
                    AssignString(images[image], piece::kImages + image);
                break;
            case 1:
                AssignString(titleOpen, piece::kRed);
                for (int32_t image = 0; image < 4; ++image)
                    AssignString(images[image], piece::kImages + 4 + image);
                break;
            case 3:
                AssignString(titleOpen, piece::kBlue);
                for (int32_t image = 0; image < 4; ++image)
                    AssignString(images[image], piece::kImages + 8 + image);
                break;
            case 5:
                AssignString(titleOpen, piece::kGreen);
                for (int32_t image = 0; image < 4; ++image)
                    AssignString(images[image], piece::kImages + 12 + image);
                break;
            default:
                AssignRange(titleOpen, piece::kBlue, &tag);
                for (int32_t image = 0; image < 4; ++image)
                    AssignRange(images[image], piece::kImages + 8 + image, &tag);
                break;
            }

            if (!Virtual<uint8_t>(advice, 0x3C, uint32_t{2})) { // CheckFlags
                AssignRange(titleOpen, piece::kGrey, &tag);
                for (int32_t image = 0; image < 4; ++image)
                    AssignRange(images[image], piece::kImages + 8 + image, &tag);
                AssignRange(messageOpen, piece::kMessageGrey, &tag);
            }

            StringBody joined1;
            StringBody joined2;
            StringBody joined3;
            const char* text = Virtual<const char*>(Virtual<void*>(advice, 0x48), 0x14); // GetTextTitle, ToChar
            StringBody* whole =
                Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined1, &titleOpen.body, &Piece(piece::kFace)->body);
            whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined2, whole, text);
            whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined3, whole, &titleClose.body);
            GameString title;
            MakeCopy(title, whole->begin, whole->end, &tag);
            FreeBody(joined3);
            FreeBody(joined2);
            FreeBody(joined1);

            if (mood == 7) {
                Virtual<void>(At<void*>(self, panel::kList), 0x10, &images[0], &images[0], &title, &title,
                              uint32_t{0}, advice, &images[3], uint32_t{0}, kListStyle);
            } else {
                text = Virtual<const char*>(Virtual<void*>(advice, 0x50), 0x14); // GetTextMessage, ToChar
                StringBody joined4;
                StringBody joined5;
                whole = Cdecl<StringBody*>(addr::SC4String_ConcatBodyText, &joined4, &messageOpen.body, text);
                whole = Cdecl<StringBody*>(addr::cSC4Advice_F408AE0, &joined5, whole,
                                           &Piece(piece::kMessageClose)->body);
                GameString message;
                This<void*>(addr::cSC4Advice_F408B90, &message, whole);
                FreeBody(joined5);
                FreeBody(joined4);
                flagged = (flagged & 0xFFFFFF00u) | Virtual<uint8_t>(advice, 0x3C, uint32_t{0x20});
                Virtual<void>(At<void*>(self, panel::kList), 0x10, &images[2], &images[1], &message, &title, flagged,
                              advice, &images[3], uint32_t{0}, kListStyle);
                DestroyString(message);
            }

            DestroyString(title);
            DestroyString(messageOpen);
            DestroyString(images[3]);
            DestroyString(images[2]);
            DestroyString(images[1]);
            DestroyString(images[0]);
            DestroyString(titleClose);
            DestroyString(titleOpen);
            Release(advice);
            node = node->next;
        } while (node != end);
    }
    Virtual<void>(At<void*>(self, panel::kList), 0x14, uint32_t{1});
}

bool InstallMySimPanel4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[3] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F76FDB0, reinterpret_cast<void*>(&FillChoices)},
        {addr::SC4UI_F770420, reinterpret_cast<void*>(&PickModel)},
        {addr::SC4UI_F770740, reinterpret_cast<void*>(&FillAdviceList)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_mySimPanel4Registered = registry::Register({
    "ui.mysim_panel.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F770740,
    "Our MySim panel advice rows and the dialog that picks a MySim's car or walking figure",
    false,
    InstallMySimPanel4,
});

} // namespace
} // namespace opensc4::ui

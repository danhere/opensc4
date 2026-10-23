// The HTML parser's handlers, written by hand from the game's code: the
// recursive descent from the page down to each tag's node (docs/html.md,
// "The parser"). Included by html_parser.cpp, inside its namespace, after
// the tokenizer, whose helpers and conventions these share: every call to
// another of the parser's functions goes through the game's address.
//
// The handlers share a shape. Most make their node first (the game's
// operator new and the class's constructor; a failed allocation leaves a
// null node, which some handlers then use as the game does), read the
// tokens that belong inside it, give back a closing tag that is not theirs
// (+0x28) or skip to their own end tag, and hand the node to the parent
// (slot 1) or delete it (slot 0) when it stayed empty. They answer whether
// the parse went on well (AL).

// ====================================================================================
// What the handlers share

template <typename R, typename... A>
R CallThis(std::uintptr_t fn, void* self, A... a)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(self, a...);
}

template <typename R, typename... A>
R CallPlain(std::uintptr_t fn, A... a)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(fn)(a...);
}

// Detours on thiscall functions of the document (or another object) with
// none to five stack arguments: g_<Name> is the game's function, <Name>
// ours (defined further on), D<Name> the detour.
#define OPENSC4_HTML_DETOUR_0(Ret, Name, Address)                                                                  using Name##Fn = Ret(__thiscall*)(char*);                                                                      Name##Fn g_##Name = nullptr;                                                                                   Ret Name(char* self);                                                                                          Ret __fastcall D##Name(char* self, void*)                                                                      {                                                                                                                  if (g_useGame)                                                                                                     return g_##Name(self);                                                                                     OPENSC4_FIRST(#Name " " #Address);                                                                             return Name(self);                                                                                         }

#define OPENSC4_HTML_DETOUR_1(Ret, Name, T1, Address)                                                              using Name##Fn = Ret(__thiscall*)(char*, T1);                                                                  Name##Fn g_##Name = nullptr;                                                                                   Ret Name(char* self, T1 a1);                                                                                   Ret __fastcall D##Name(char* self, void*, T1 a1)                                                               {                                                                                                                  if (g_useGame)                                                                                                     return g_##Name(self, a1);                                                                                 OPENSC4_FIRST(#Name " " #Address);                                                                             return Name(self, a1);                                                                                     }

#define OPENSC4_HTML_DETOUR_2(Ret, Name, T1, T2, Address)                                                          using Name##Fn = Ret(__thiscall*)(char*, T1, T2);                                                              Name##Fn g_##Name = nullptr;                                                                                   Ret Name(char* self, T1 a1, T2 a2);                                                                            Ret __fastcall D##Name(char* self, void*, T1 a1, T2 a2)                                                        {                                                                                                                  if (g_useGame)                                                                                                     return g_##Name(self, a1, a2);                                                                             OPENSC4_FIRST(#Name " " #Address);                                                                             return Name(self, a1, a2);                                                                                 }

#define OPENSC4_HTML_DETOUR_3(Ret, Name, T1, T2, T3, Address)                                                      using Name##Fn = Ret(__thiscall*)(char*, T1, T2, T3);                                                          Name##Fn g_##Name = nullptr;                                                                                   Ret Name(char* self, T1 a1, T2 a2, T3 a3);                                                                     Ret __fastcall D##Name(char* self, void*, T1 a1, T2 a2, T3 a3)                                                 {                                                                                                                  if (g_useGame)                                                                                                     return g_##Name(self, a1, a2, a3);                                                                         OPENSC4_FIRST(#Name " " #Address);                                                                             return Name(self, a1, a2, a3);                                                                             }

constexpr uint32_t kFontTags = 0x00B624B8;    // TT, I, B, STRIKE, BIG, SMALL, U, FONT
constexpr uint32_t kPhraseTags = 0x00B624C4;  // EM to ACRONYM
constexpr uint32_t kSpecialTags = 0x00B624D0; // A, IMG, OBJECT, BR, SCRIPT, MAP, Q, SUB, SUP, SPAN, BDO, BLINK
constexpr uint32_t kFormTags = 0x00B624DC;    // INPUT, SELECT, TEXTAREA, LABEL, BUTTON
constexpr uint32_t kInlineTags = 0x00B624E8;  // text and the four sets above
constexpr uint32_t kHeadingTags = 0x00B624F4; // H1 to H6
constexpr uint32_t kListTags = 0x00B62500;    // UL, OL
constexpr uint32_t kBlockTags = 0x00B6250C;
constexpr uint32_t kFlowTags = 0x00B62518; // inline and block

uint32_t& Id(char* doc) { return At<uint32_t>(doc, kTagId); }
void GiveBack(char* doc) { At<uint8_t>(doc, kPushedBack) = 1; }

void Next(char* doc, bool stopAtText)
{
    CallThis<void>(addr::HTMLDocument_NextTag, doc, static_cast<uint8_t>(stopAtText));
}

bool InSet(uint32_t id, uint32_t set)
{
    return CallPlain<uint8_t>(addr::HTMLTagSet_Contains, id, reinterpret_cast<void*>(set)) != 0;
}

bool Inline(char* doc, char* parent) { return CallThis<bool>(addr::HTMLDocument_ParseInline, doc, parent); }
bool Block(char* doc, char* parent) { return CallThis<bool>(addr::HTMLDocument_ParseBlock, doc, parent); }
bool Flow(char* doc, char* parent) { return CallThis<bool>(addr::HTMLDocument_ParseFlow, doc, parent); }
bool SkipTo(char* doc, uint32_t end) { return CallThis<bool>(addr::HTMLDocument_SkipTo, doc, end); }
bool SkipScript(char* doc) { return CallThis<bool>(addr::HTMLDocument_SkipScript, doc); }

bool Handler(std::uintptr_t fn, char* doc, char* parent) { return CallThis<bool>(fn, doc, parent); }

void PushFont(char* doc) { CallThis<void>(addr::HTMLDocument_PushFont, doc); }
void PopFont(char* doc) { CallThis<void>(addr::HTMLDocument_PopFont, doc); }

// The text run (+0x100) or any string as a text node in `parent`.
void AddText(char* doc, Text* text, char* parent)
{
    CallThis<void>(addr::HTMLDocument_AddText, doc, text, parent);
}

bool Attribute(char* doc, const char* name, Text* value)
{
    return CallThis<bool>(addr::HTMLDocument_GetAttribute, doc, name, value, uint32_t{1});
}

// 0x008FF7FD, called after each tag's attributes are read with the
// handler's and the tag's names; it walks the attribute map and does
// nothing else (its report is not in the game).
void CheckAttributes(char* doc, const char* handler, const char* tag)
{
    CallThis<void>(addr::HTMLDocument_CheckAttributes, doc, handler, tag);
}

char* NewObject(std::size_t size) { return CallPlain<char*>(addr::SC4_operator_new, size); }

// A node from the game's allocator and its constructor (null when the
// allocation failed, as the game's `new` gives).
char* NewNode(std::size_t size, std::uintptr_t construct)
{
    char* const memory = NewObject(size);
    return memory ? CallThis<char*>(construct, memory) : nullptr;
}

void AddChild(char* parent, char* child) { Virtual<void*>(parent, 0x4, child); }
void DeleteNode(char* node) { Virtual<void*>(node, 0x0, uint32_t{1}); }

int32_t Children(char* node) { return (At<int32_t>(node, 0x20) - At<int32_t>(node, 0x1C)) >> 2; }

// The node goes to the parent when it has children, else it is deleted.
void KeepIfFilled(char* parent, char* node)
{
    if (Children(node) != 0)
        AddChild(parent, node);
    else if (node)
        DeleteNode(node);
}

void PushPointer(void* vector, char** value) { CallThis<void>(addr::SC4PtrVector_PushBack8FF785, vector, value); }

// The alignment the text takes (+0x158), kept on a stack at +0x168 while a
// block sets its own.
constexpr std::size_t kAlign = 0x158;
constexpr std::size_t kAlignStack = 0x168;

void PushAlign(char* doc) { CallThis<void>(addr::HTMLIntVector_PushBack, doc + kAlignStack, doc + kAlign); }

void PopAlign(char* doc)
{
    int32_t*& first = At<int32_t*>(doc, kAlignStack);
    int32_t*& last = At<int32_t*>(doc, kAlignStack + 4);
    if (first != last) {
        At<int32_t>(doc, kAlign) = last[-1];
        --last;
    }
}

// The string the attribute values are read into, freed as the game frees it
// at each handler's end: with the destructor's vtable, or without it.
struct Value {
    Text text;
    Value() { MakeEmpty(&text); }
    void Release() { DestroyInline(&text); }
    void ReleaseBody()
    {
        if (text.body.begin)
            Free(text.body.begin);
    }
};

// The font record (+0x130): face (a string), +0x144 its kind (0 a named
// face, 2 preformatted, 9 a heading, 0xA fixed width), +0x148 the size,
// +0x14C the colour, +0x150 the style bits (1 bold, 2 italic, 4
// underline, 8 struck through), +0x154 preformatted, +0x155 the colour
// not set by the page.
constexpr std::size_t kFont = 0x130;
constexpr std::size_t kFontFace = 0x134; // the face's body
constexpr std::size_t kFontKind = 0x144;
constexpr std::size_t kFontSize = 0x148;
constexpr std::size_t kFontColour = 0x14C;
constexpr std::size_t kFontStyle = 0x150;
constexpr std::size_t kFontPre = 0x154;
constexpr std::size_t kFontDefaultColour = 0x155;
constexpr std::size_t kFontSteps = 0x1A8; // the seven sizes

constexpr uint32_t kFixedFace = 0xA;

// A colour from the framework's colour maker (slot 34 of the singleton at
// 0x00913C1A), as the handlers make one from the bytes of an 0xAARRGGBB
// value: red, green, blue. The game pushes each as the dword at that byte,
// with the bytes above it from its stack; the maker reads only the low
// byte of each (its slot 34 ends in 0x00825DA0), so ours pass them clean.
uint32_t MakeColour(uint32_t colour)
{
    void* const maker = CallPlain<void*>(addr::SC4_Singleton913C1A);
    return Virtual<uint32_t>(maker, 0x88, colour >> 16, colour >> 8, colour);
}

// ====================================================================================
// The document (ui.html.parser.document)

OPENSC4_HTML_DETOUR_1(bool, Parse, const Text*, 0x00908122)
OPENSC4_HTML_DETOUR_0(bool, ParseFromHTML, 0x0090818E)
OPENSC4_HTML_DETOUR_0(bool, ParseHTML, 0x009080A2)
OPENSC4_HTML_DETOUR_0(bool, ParseHead, 0x00906E94)
OPENSC4_HTML_DETOUR_0(bool, ParseTitle, 0x00906476)
OPENSC4_HTML_DETOUR_0(bool, ParseBody, 0x00907E42)
OPENSC4_HTML_DETOUR_0(bool, ParsePlain, 0x00905985)
OPENSC4_HTML_DETOUR_2(void, AddImage, void*, const Text*, 0x009059FC)
OPENSC4_HTML_DETOUR_2(void*, InsertImage, void*, const void*, 0x00901191)
OPENSC4_HTML_DETOUR_0(char*, Construct, 0x00905AC5)
OPENSC4_HTML_DETOUR_0(void, PushFontRecord, 0x00902D28)
OPENSC4_HTML_DETOUR_1(void, PushFontCopy, const void*, 0x00901111)

// The game's 0x00908122 (slot 8): the page into +0x180, read from the
// start; the first text and tag decide: `<!DOCTYPE` goes to slot 9,
// `<HTML>` to ParseHTML, anything else (the end, text first, another tag)
// shows the whole page as plain text (slot 11), the run emptied first.
bool Parse(char* doc, const Text* page)
{
    AssignText(TextAt(doc, kPage), page);
    At<uint32_t>(doc, kPosition) = 0;
    CallReadText(doc);
    Next(doc, false);
    switch (Id(doc)) {
    case kDoctypeId:
        return Virtual<bool>(doc, 0x24);
    case 4:
        return CallThis<bool>(addr::HTMLDocument_ParseHTML, doc);
    default:
        Erase(&TextAt(doc, kRunText)->body, 0, kAll);
        At<uint32_t>(doc, kPosition) = 0;
        return Virtual<bool>(doc, 0x2C);
    }
}

// The game's 0x0090818E (slot 10, after a DOCTYPE): spaces skipped
// (0x008FE7B8), then the page must open with `<HTML>`.
bool ParseFromHTML(char* doc)
{
    CallThis<void>(addr::HTMLDocument_SkipSpaces, doc);
    CallReadText(doc);
    Next(doc, false);
    if (Id(doc) != 4)
        return false;
    return CallThis<bool>(addr::HTMLDocument_ParseHTML, doc);
}

// The game's 0x009080A2: the body node (0x44 bytes, with the link, visited
// link and active link colours of 0x00B62490, 0x00B62498, 0x00B6249C) as
// the root (+0x14), the head (0x00906E94), the body (0x00907E42); a tag
// after it other than `</HTML>` is given back.
bool ParseHTML(char* doc)
{
    char* const memory = NewObject(0x44);
    At<char*>(doc, 0x14) =
        memory ? CallThis<char*>(addr::HTMLBodyNode_Construct, memory, Global<uint32_t>(addr::HTMLDocument_LinkColour),
                                 Global<uint32_t>(addr::HTMLDocument_VisitedColour),
                                 Global<uint32_t>(addr::HTMLDocument_ActiveColour))
               : nullptr;
    CallThis<void>(addr::HTMLDocument_SkipSpaces, doc);
    if (Id(doc) == 4)
        Next(doc, false);
    CallThis<bool>(addr::HTMLDocument_ParseHead, doc);
    CallThis<void>(addr::HTMLDocument_SkipSpaces, doc);
    Next(doc, false);
    CallThis<bool>(addr::HTMLDocument_ParseBody, doc);
    Next(doc, true);
    if (Id(doc) != 5)
        GiveBack(doc);
    return true;
}

// The game's 0x00906E94: the head's tags up to `</HEAD>` (true) or anything
// else (given back): TITLE (0x00906476), BASE (0x0090092D), META
// (0x00900D78) and LINK (0x00900C31) read; STYLE, OBJECT and SCRIPT skipped;
// ISINDEX passed over.
bool ParseHead(char* doc)
{
    if (Id(doc) == 6)
        Next(doc, true);
    for (;; Next(doc, true)) {
        switch (Id(doc)) {
        case 7:
            return true;
        case 0xA:
            CallThis<bool>(addr::HTMLDocument_ParseTitle, doc);
            break;
        case 0xC:
            CallThis<bool>(addr::HTMLDocument_ParseBase, doc);
            break;
        case 0xD:
            SkipTo(doc, 0xE);
            break;
        case 0xF:
            CallThis<bool>(addr::HTMLDocument_ParseMeta, doc);
            break;
        case 0x10:
            CallThis<bool>(addr::HTMLDocument_ParseLinkTag, doc);
            break;
        case 0x11:
            break;
        case 0x39:
            SkipTo(doc, 0x3A);
            break;
        case 0x3C:
            SkipScript(doc);
            break;
        default:
            GiveBack(doc);
            return true;
        }
    }
}

// The game's 0x00906476: the title's text into +0x98; true when `</TITLE>`
// follows.
bool ParseTitle(char* doc)
{
    Next(doc, false);
    if (Id(doc) == kTextId)
        AssignText(TextAt(doc, 0x98), TextAt(doc, kRunText));
    Next(doc, true);
    return Id(doc) == 0xB;
}

// The game's 0x00907E42: the body's attributes, then its content up to
// `</BODY>` (or the end of the page, or a handler that fails). BACKGROUND
// is resolved against the base address (+0xB8) and asked for as an image
// (slot 16) for the root's background; BGCOLOR, LINK, ALINK and VLINK go
// to the root; TEXT makes the text colour; BGPROPERTIES fixes the
// background (root +0x40).
bool ParseBody(char* doc)
{
    bool ok = true;
    Value background;
    Value value;
    char* const root = At<char*>(doc, 0x14);
    if (Attribute(doc, "BACKGROUND", &value.text) && At<Text*>(doc, 0xB8)) {
        alignas(4) char address[0xC0];
        CallThis<void>(addr::SC4Url_Construct, address);
        CallThis<void>(addr::SC4Url_Resolve, address, &value.text, At<Text*>(doc, 0xB8));
        AssignText(&background.text, CallThis<Text*>(addr::SC4Url_Text, address));
        CallThis<void>(addr::SC4Url_Destroy, address);
    }
    if (Attribute(doc, "BGCOLOR", &value.text)) {
        uint32_t colour = 0xFF000000;
        CallPlain<uint8_t>(addr::HTMLColor_Parse, &value.text, &colour);
        CallThis<void>(addr::HTMLBodyNode_SetBackground, root, &colour);
    }
    if (Attribute(doc, "LINK", &value.text))
        CallThis<void>(addr::HTMLBodyNode_SetLink, root, &value.text);
    if (Attribute(doc, "ALINK", &value.text))
        CallThis<void>(addr::HTMLBodyNode_SetActiveLink, root, &value.text);
    if (Attribute(doc, "VLINK", &value.text))
        CallThis<void>(addr::HTMLBodyNode_SetVisitedLink, root, &value.text);
    if (Attribute(doc, "TEXT", &value.text)) {
        uint32_t colour = 0xFF000000;
        CallPlain<uint8_t>(addr::HTMLColor_Parse, &value.text, &colour);
        At<uint32_t>(doc, kFontColour) = MakeColour(colour);
        At<uint8_t>(doc, kFontDefaultColour) = 0;
    }
    if (Attribute(doc, "BGPROPERTIES", &value.text))
        At<uint8_t>(root, 0x40) = 1;
    CheckAttributes(doc, "ParseBODY", "BODY");
    if (Length(background.text.body) != 0)
        Virtual<void>(doc, 0x40, root ? root + 0x28 : nullptr, &background.text);
    if (Id(doc) == 8)
        Next(doc, false);
    for (;;) {
        const uint32_t id = Id(doc);
        if (InSet(id, kBlockTags))
            ok = Block(doc, root);
        else if (id == 0x3C)
            ok = SkipScript(doc);
        else if (InSet(id, kInlineTags))
            ok = Inline(doc, root);
        else if (id == kEndId)
            break;
        Next(doc, false);
        if (!ok)
            break;
    }
    if (Id(doc) != 9)
        GiveBack(doc);
    value.Release();
    background.Release();
    return ok;
}

// The game's 0x00905985 (slot 11): the whole page as one text run in a new
// root, the position at the end.
bool ParsePlain(char* doc)
{
    if (char* const old = At<char*>(doc, 0x14))
        DeleteNode(old);
    char* const memory = NewObject(0x44);
    At<char*>(doc, 0x14) =
        memory ? CallThis<char*>(addr::HTMLBodyNode_Construct, memory, Global<uint32_t>(addr::HTMLDocument_LinkColour),
                                 Global<uint32_t>(addr::HTMLDocument_VisitedColour),
                                 Global<uint32_t>(addr::HTMLDocument_ActiveColour))
               : nullptr;
    PushFont(doc);
    At<uint8_t>(doc, kFontPre) = 1;
    AddText(doc, TextAt(doc, kPage), At<char*>(doc, 0x14));
    At<uint32_t>(doc, kPosition) = Length(TextAt(doc, kPage)->body);
    PopFont(doc);
    return true;
}

// The game's 0x009059FC (slot 16): an image for `target` (an image node's
// part with slot 0 taking a picture) by address. Under the document's lock
// (+0x34): an address already in the image map (+0x198) gives its picture
// at once (and a layout, slot 15, when the target takes it) or queues the
// target with the others waiting; a new address gets an entry with the
// target and is asked of the Internet service (its slot 17).
void AddImage(char* doc, void* target, const Text* address)
{
    char* const lock = doc + 0x34;
    void* const map = doc + 0x198;
    Virtual<void>(lock, 0x4);
    char* const found = CallThis<char*>(addr::HTMLImageMap_Find, map, address);
    if (found != At<char*>(map, 0x0)) {
        if (void* const picture = At<void*>(found, 0x24)) {
            if (Virtual<bool>(target, 0x0, picture))
                Virtual<void>(doc, 0x3C, uint32_t{0});
        } else {
            CallThis<void>(addr::SC4PtrVector_PushBack8FF785, found + 0x28, &target);
        }
        Virtual<void>(lock, 0x8);
        return;
    }
    uint32_t empty[5] = {};
    alignas(4) char entry[0x28];
    CallThis<void>(addr::HTMLImageEntry_Construct, entry, address, empty);
    struct {
        char* node;
        uint32_t inserted; // the byte the game reads
    } result;
    CallThis<void*>(addr::HTMLImageMap_InsertUnique, map, &result, entry);
    const bool inserted = static_cast<uint8_t>(result.inserted) != 0;
    CallThis<void>(addr::HTMLImageEntry_Destroy, entry);
    if (inserted)
        CallThis<void>(addr::SC4PtrVector_PushBack8FF785, result.node + 0x28, &target);
    Virtual<void>(lock, 0x8);
    Virtual<void>(At<void*>(doc, 0x30), 0x44, address, doc + 0x4, uint32_t{3}, uint32_t{0}, uint32_t{1});
}

// The game's 0x00901191: the image map's insert unless the address is
// there (an STLport tree ordered by 0x004B5E10). `result` gets the node
// and whether it is new.
void* InsertImage(char* map, void* result, const void* entry)
{
    char* const header = At<char*>(map, 0x0);
    void* const less = map + 0x8;
    auto Less = [&](const void* a, const void* b) { return CallThis<bool>(addr::SC4String_Less, less, a, b); };
    char* parent = header;
    char* node = At<char*>(header, 0x4);
    bool left = true;
    while (node) {
        parent = node;
        left = Less(entry, node + 0x10);
        node = left ? At<char*>(node, 0x8) : At<char*>(node, 0xC);
    }
    char* before = parent;
    bool insert = false;
    if (left) {
        if (parent == At<char*>(header, 0x8))
            insert = true;
        else
            before = CallPlain<char*>(addr::cSC4Advisor_F40D500, parent);
    }
    if (insert || Less(before + 0x10, entry)) {
        char* made = nullptr;
        CallThis<void*>(addr::HTMLImageMap_Insert, map, &made, nullptr, parent, entry, uint32_t{0});
        At<char*>(result, 0x0) = made;
        At<uint8_t>(result, 0x4) = 1;
    } else {
        At<char*>(result, 0x0) = before;
        At<uint8_t>(result, 0x4) = 0;
    }
    return result;
}

// The game's 0x00902D28: the current font (+0x130) onto the font stack
// (+0x15C); 0x00900E5F takes it back.
void PushFontRecord(char* doc) { CallThis<void>(addr::HTMLFontStack_Push, doc + 0x15C, doc + kFont); }

// The game's 0x00901111: the font stack's push_back (records of 0x28 bytes,
// copied by 0x008FFE11; a full vector grows through 0x00900E91).
void PushFontCopy(char* vector, const void* font)
{
    char*& last = At<char*>(vector, 0x4);
    if (last != At<char*>(vector, 0x8)) {
        if (last)
            CallThis<void>(addr::HTMLFont_Copy, last, font);
        last += 0x28;
    } else {
        uint8_t tag = 0;
        CallThis<void>(addr::HTMLFontStack_Grow, vector, last, font, &tag, uint32_t{1}, uint32_t{1});
    }
}

// The game's 0x00905AC5: a document. The vtables, the entity map (+8), the
// lock (+0x34), the colour maker (+0x3C), the strings and vectors, the
// attribute map (+0x114), the image map (+0x198). The first document makes
// the four colours (link, visited link, active link, text: 0x00B62490 to
// 0x00B6249C), the tag table and the tag sets; each sets the seven font
// steps (0x00ACD4A0) and the faces arial (+0x40) and courier new (+0x58),
// both at the fourth size.
char* Construct(char* doc)
{
    At<uint32_t>(doc, 0x4) = static_cast<uint32_t>(addr::cSC4TrafficSimulator_VtableBase4);
    At<uint32_t>(doc, 0x0) = static_cast<uint32_t>(addr::HTMLDocument_Vtable);
    At<uint32_t>(doc, 0x4) = static_cast<uint32_t>(addr::HTMLDocument_SecondVtable);
    CallThis<void>(addr::SC4StringMap_Construct, doc + 0x8);
    At<uint32_t>(doc, 0x14) = 0;
    At<uint8_t>(doc, 0x18) = 0;
    At<uint32_t>(doc, 0x1C) = 0;
    At<uint32_t>(doc, 0x20) = 1;
    At<uint32_t>(doc, 0x24) = 0;
    At<uint8_t>(doc, 0x28) = 0;
    At<uint8_t>(doc, 0x29) = 0;
    At<uint8_t>(doc, 0x2A) = 0;
    At<uint32_t>(doc, 0x2C) = 0;
    At<uint32_t>(doc, 0x30) = 0;
    CallThis<void>(addr::RZCriticalSection_Construct, doc + 0x34);
    At<void*>(doc, 0x3C) = CallPlain<void*>(addr::SC4_Singleton913C1A);
    for (const std::size_t at : {0x40u, 0x58u, 0x70u, 0x84u, 0x98u})
        MakeEmpty(TextAt(doc, at));
    for (const std::size_t at : {0xACu, 0xB0u, 0xB4u, 0xB8u, 0xBCu, 0xC0u, 0xC4u, 0xC8u, 0xCCu, 0xD0u})
        At<uint32_t>(doc, at) = 0;
    MakeEmpty(TextAt(doc, kTagText));
    MakeEmpty(TextAt(doc, kTagRest));
    MakeEmpty(TextAt(doc, kRunText));
    CallThis<void>(addr::cGZDBSegmentPackedFile_KeyTableConstruct, doc + kAttributes);
    At<uint32_t>(doc, 0x128) = 0;
    At<uint32_t>(doc, 0x12C) = 0;
    MakeEmpty(TextAt(doc, kFont));
    At<uint32_t>(doc, kAlign) = 0;
    for (std::size_t at = 0x15C; at < 0x180; at += 4)
        At<uint32_t>(doc, at) = 0;
    MakeEmpty(TextAt(doc, kPage));
    CallThis<void>(addr::cGZCOM_F87B904, doc + 0x198);
    At<uint32_t>(doc, 0x1A4) = 0;
    if (!Global<uint8_t>(addr::HTMLDocument_ColoursMade)) {
        Global<uint8_t>(addr::HTMLDocument_ColoursMade) = 1;
        void* const maker = At<void*>(doc, 0x3C);
        Global<uint32_t>(addr::HTMLDocument_TextColour) = Virtual<uint32_t>(maker, 0x88, 0u, 0u, 0u);
        Global<uint32_t>(addr::HTMLDocument_VisitedColour) = Virtual<uint32_t>(maker, 0x88, 0x64u, 0x20u, 0xA4u);
        Global<uint32_t>(addr::HTMLDocument_LinkColour) = Virtual<uint32_t>(maker, 0x88, 0u, 0u, 0xFFu);
        Global<uint32_t>(addr::HTMLDocument_ActiveColour) = Virtual<uint32_t>(maker, 0x88, 0xC0u, 0u, 0xFFu);
        CallPlain<void>(addr::HTMLTags_Init);
        CallPlain<void>(addr::HTMLTagSets_Init);
    }
    CallThis<void>(addr::HTMLDocument_SetFontSteps, doc, reinterpret_cast<const void*>(addr::HTMLDocument_FontSteps),
                   uint32_t{7});
    for (const std::size_t at : {0x40u, 0x58u}) {
        Text face;
        FromCString(&face, at == 0x40 ? "arial" : "courier new");
        AssignText(TextAt(doc, at), &face);
        if (face.body.begin)
            Free(face.body.begin);
    }
    At<uint32_t>(doc, 0x54) = At<uint32_t>(doc, 0x1B4);
    At<uint32_t>(doc, 0x6C) = At<uint32_t>(doc, 0x1B4);
    return doc;
}

// ====================================================================================
// Blocks (ui.html.parser.blocks)

OPENSC4_HTML_DETOUR_1(bool, ParseBlock, char*, 0x00907135)
OPENSC4_HTML_DETOUR_1(bool, SkipToTag, uint32_t, 0x009065C5)
OPENSC4_HTML_DETOUR_0(bool, SkipScriptTag, 0x00906ACD)
OPENSC4_HTML_DETOUR_1(bool, ParseParagraph, char*, 0x0090614F)
OPENSC4_HTML_DETOUR_3(bool, ParseAligned, char*, uint32_t, int32_t, 0x009065FD)
OPENSC4_HTML_DETOUR_1(bool, ParseCenter, char*, 0x00906D22)
OPENSC4_HTML_DETOUR_1(bool, ParseDiv, char*, 0x00906D32)
OPENSC4_HTML_DETOUR_1(bool, ParseQuote, char*, 0x00906C6B)
OPENSC4_HTML_DETOUR_1(bool, ParseForm, char*, 0x00906D9B)
OPENSC4_HTML_DETOUR_1(bool, ParseHeading, char*, 0x00906754)
OPENSC4_HTML_DETOUR_1(bool, ParsePre, char*, 0x00906A5D)
OPENSC4_HTML_DETOUR_1(bool, ParseDefinitions, char*, 0x009066A3)
OPENSC4_HTML_DETOUR_1(bool, ParseDD, char*, 0x00905ED9)
OPENSC4_HTML_DETOUR_1(bool, ParseDT, char*, 0x00905F63)
OPENSC4_HTML_DETOUR_1(bool, ParseList, char*, 0x00906F1D)
OPENSC4_HTML_DETOUR_1(bool, ParseBullets, char*, 0x00907D6B)
OPENSC4_HTML_DETOUR_1(bool, ParseNumbers, char*, 0x0090689F)
OPENSC4_HTML_DETOUR_2(bool, ParseItem, char*, int32_t, 0x00905FED)

// The game's 0x00907135: a block tag by the current ID (HR is the game's
// 0x0090097A; NOSCRIPT, FIELDSET, ADDRESS, DIR and MENU are skipped to
// their end tag); false for anything else.
bool ParseBlock(char* doc, char* parent)
{
    const uint32_t id = Id(doc);
    switch (id) {
    case 0x77:
        return Handler(addr::HTMLDocument_ParseHR, doc, parent);
    case 0x65:
        return Handler(addr::HTMLDocument_ParseParagraph, doc, parent);
    case 0x67:
        return Handler(addr::HTMLDocument_ParsePre, doc, parent);
    case 0x6D:
        return Handler(addr::HTMLDocument_ParseDefinitions, doc, parent);
    case 0x6F:
        return Handler(addr::HTMLDocument_ParseDiv, doc, parent);
    case 0x71:
        return SkipTo(doc, 0x72);
    case 0x73:
        return Handler(addr::HTMLDocument_ParseQuote, doc, parent);
    case 0x75:
        return Handler(addr::HTMLDocument_ParseForm, doc, parent);
    case 0x78:
        return Handler(addr::HTMLDocument_ParseTable, doc, parent);
    case 0x7A:
        return SkipTo(doc, 0x7B);
    case 0x7C:
        return SkipTo(doc, 0x7D);
    case 0x7E:
        return Handler(addr::HTMLDocument_ParseCenter, doc, parent);
    case 0x80:
        return SkipTo(doc, 0x81);
    case 0x82:
        return SkipTo(doc, 0x83);
    default:
        if (InSet(id, kHeadingTags))
            return Handler(addr::HTMLDocument_ParseHeading, doc, parent);
        if (InSet(id, kListTags))
            return Handler(addr::HTMLDocument_ParseList, doc, parent);
        return false;
    }
}

// The game's 0x009065C5: tags read until `end` (true) or the end of the
// page (false).
bool SkipToTag(char* doc, uint32_t end)
{
    while (At<uint32_t>(doc, kPosition) < Length(TextAt(doc, kPage)->body)) {
        Next(doc, false);
        if (Id(doc) == end)
            return true;
    }
    return false;
}

// The game's 0x00906ACD: to the end of a script (an empty string made and
// freed around it).
bool SkipScriptTag(char* doc)
{
    Value unused;
    const bool found = SkipTo(doc, 0x3D);
    unused.ReleaseBody();
    return found;
}

// The game's 0x0090614F: P, its inline content.
bool ParseParagraph(char* doc, char* parent)
{
    char* const node = NewNode(0x28, addr::HTMLParagraphNode_Construct);
    Value unused;
    Next(doc, false);
    while (InSet(Id(doc), kInlineTags)) {
        Inline(doc, node);
        Next(doc, false);
    }
    if (Id(doc) != 0x66)
        GiveBack(doc);
    AddChild(parent, node);
    unused.ReleaseBody();
    return true;
}

// The game's 0x009065FD: CENTER and DIV, a block with its alignment pushed.
bool ParseAligned(char* doc, char* parent, uint32_t end, int32_t align)
{
    char* const node = NewNode(0x28, addr::HTMLAlignNode_Construct);
    AddChild(parent, node);
    PushAlign(doc);
    At<int32_t>(doc, kAlign) = align;
    for (Next(doc, false); InSet(Id(doc), kFlowTags); Next(doc, false))
        Flow(doc, node);
    if (Id(doc) != end)
        SkipTo(doc, end);
    PopAlign(doc);
    return true;
}

// The game's 0x00906D22: CENTER, centred (1).
bool ParseCenter(char* doc, char* parent)
{
    return CallThis<bool>(addr::HTMLDocument_ParseAligned, doc, parent, uint32_t{0x7F}, int32_t{1});
}

// The game's 0x00906D32: DIV, aligned by its ALIGN (0x008FE8D6) or as the
// text around it.
bool ParseDiv(char* doc, char* parent)
{
    Value value;
    int32_t align = At<int32_t>(doc, kAlign);
    if (Attribute(doc, "ALIGN", &value.text))
        align = CallPlain<int32_t>(addr::HTMLText_ParseAlign, &value.text);
    const bool ok = CallThis<bool>(addr::HTMLDocument_ParseAligned, doc, parent, uint32_t{0x70}, align);
    value.Release();
    return ok;
}

// The game's 0x00906C6B: BLOCKQUOTE, its blocks and inline content (SCRIPT
// skipped).
bool ParseQuote(char* doc, char* parent)
{
    char* const node = NewNode(0x28, addr::HTMLQuoteNode_Construct);
    for (Next(doc, false);; Next(doc, false)) {
        const uint32_t id = Id(doc);
        if (id == 0x3C)
            SkipScript(doc);
        else if (InSet(id, kBlockTags))
            Block(doc, node);
        else if (InSet(id, kInlineTags))
            Inline(doc, node);
        else
            break;
    }
    if (Id(doc) != 0x74)
        SkipTo(doc, 0x74);
    KeepIfFilled(parent, node);
    return true;
}

// The game's 0x00906D9B: FORM, its content (SCRIPT skipped, a FORM inside
// skipped to its end).
bool ParseForm(char* doc, char* parent)
{
    char* const node = NewNode(0x28, addr::HTMLFormNode_Construct);
    for (;;) {
        Next(doc, false);
        const uint32_t id = Id(doc);
        if (id == 0x3C)
            SkipScript(doc);
        else if (id == 0x75)
            SkipTo(doc, 0x76);
        else if (InSet(id, kInlineTags))
            Inline(doc, node);
        else if (InSet(id, kBlockTags))
            Block(doc, node);
        else
            break;
    }
    if (Id(doc) != 0x76)
        SkipTo(doc, 0x76);
    KeepIfFilled(parent, node);
    return true;
}

// The game's 0x00906754: H1 to H6, in the heading face (kind 9), the
// heading's size and bold.
bool ParseHeading(char* doc, char* parent)
{
    constexpr int32_t kSizes[6] = {0x28, 0x20, 0x1C, 0x18, 0x14, 0x10}; // 0x00ACD4D4
    Value unused;
    const uint32_t id = Id(doc);
    int level;
    switch (id) {
    case 0x59: level = 0; break;
    case 0x5B: level = 1; break;
    case 0x5D: level = 2; break;
    case 0x5F: level = 3; break;
    case 0x61: level = 4; break;
    case 0x63: level = 5; break;
    default:
        unused.ReleaseBody();
        return false;
    }
    const uint32_t end = id + 1;
    char* const node = NewNode(0x2C, addr::HTMLHeadingNode_Construct);
    PushFont(doc);
    At<uint32_t>(doc, kFontKind) = 9;            // 0x00ACD4BC, the same for all six
    At<int32_t>(doc, kFontSize) = kSizes[level]; // 0x00ACD4D4
    At<uint32_t>(doc, kFontStyle) = 1;           // 0x00ACD4EC, bold for all six
    for (;;) {
        Next(doc, false);
        const uint32_t next = Id(doc);
        if (InSet(next, kInlineTags))
            Inline(doc, node);
        else if (InSet(next, kBlockTags))
            Block(doc, node);
        else
            break;
    }
    if (Id(doc) != end)
        SkipTo(doc, end);
    PopFont(doc);
    KeepIfFilled(parent, node);
    unused.ReleaseBody();
    return true;
}

// The game's 0x00906A5D: PRE, preformatted (kind 2) inline content.
bool ParsePre(char* doc, char* parent)
{
    PushFont(doc);
    At<uint32_t>(doc, kFontKind) = 2;
    At<uint8_t>(doc, kFontPre) = 1;
    for (Next(doc, false); InSet(Id(doc), kInlineTags); Next(doc, false))
        Inline(doc, parent);
    if (Id(doc) != 0x68)
        SkipTo(doc, 0x68);
    PopFont(doc);
    return true;
}

// The game's 0x009066A3: DL, a container (kind 8) of DT, DD and blocks.
bool ParseDefinitions(char* doc, char* parent)
{
    bool ok = true;
    char* const memory = NewObject(0x28);
    char* const node = memory ? CallThis<char*>(addr::HTMLContainerNode_Construct, memory, uint32_t{8}) : nullptr;
    for (;;) {
        Next(doc, true);
        const uint32_t id = Id(doc);
        if (id == 0x97)
            Handler(addr::HTMLDocument_ParseDT, doc, node);
        else if (id == 0x99)
            Handler(addr::HTMLDocument_ParseDD, doc, node);
        else if (InSet(id, kBlockTags))
            Block(doc, node);
        else
            break;
    }
    if (Id(doc) != 0x6E)
        ok = SkipTo(doc, 0x6E);
    if (ok && Children(node) != 0)
        AddChild(parent, node);
    else if (node)
        DeleteNode(node);
    return ok;
}

// DD and DT (0x00905ED9, 0x00905F63): inline content up to their end tag
// or a handler that fails; the node is kept unless one failed.
bool ParseTerm(char* doc, char* parent, std::uintptr_t construct, uint32_t end)
{
    bool ok = true;
    char* const node = NewNode(0x28, construct);
    Next(doc, false);
    while (InSet(Id(doc), kInlineTags)) {
        ok = Inline(doc, node);
        Next(doc, false);
        if (!ok)
            break;
    }
    if (Id(doc) != end)
        GiveBack(doc);
    if (ok)
        AddChild(parent, node);
    else if (node)
        DeleteNode(node);
    return ok;
}

bool ParseDD(char* doc, char* parent) { return ParseTerm(doc, parent, addr::HTMLDDNode_Construct, 0x9A); }
bool ParseDT(char* doc, char* parent) { return ParseTerm(doc, parent, addr::HTMLDTNode_Construct, 0x98); }

// The game's 0x00906F1D: UL or OL.
bool ParseList(char* doc, char* parent)
{
    switch (Id(doc)) {
    case 0x69:
        return Handler(addr::HTMLDocument_ParseBullets, doc, parent);
    case 0x6B:
        return Handler(addr::HTMLDocument_ParseNumbers, doc, parent);
    default:
        return false;
    }
}

// UL and OL (0x00907D6B, 0x0090689F): a list node (kind 6 or 7) with its
// depth from the count at +0x12C or +0x128 (counted up for each list ever
// made, never down), items bulleted (-1) or numbered from 1, blocks and
// inline content. Kept when it has children and the last handler did not
// fail.
bool ParseListOf(char* doc, char* parent, uint32_t kind, std::size_t counter, uint32_t end, bool numbered)
{
    bool ok = true;
    char* node = nullptr;
    if (char* const memory = NewObject(0x2C)) {
        const uint32_t depth = At<uint32_t>(doc, counter)++;
        node = CallThis<char*>(addr::HTMLListNode_Construct, memory, kind, depth);
    }
    int32_t number = 1;
    for (;;) {
        Next(doc, false);
        const uint32_t id = Id(doc);
        if (id == 0x95)
            ok = CallThis<bool>(addr::HTMLDocument_ParseItem, doc, node, numbered ? number++ : -1);
        else if (InSet(id, kBlockTags))
            ok = Block(doc, node);
        else if (InSet(id, kInlineTags))
            ok = Inline(doc, node);
        else
            break;
    }
    if (Id(doc) != end)
        SkipTo(doc, end);
    if (ok && Children(node) != 0) {
        AddChild(parent, node);
        return true;
    }
    if (node)
        DeleteNode(node);
    return false;
}

bool ParseBullets(char* doc, char* parent) { return ParseListOf(doc, parent, 6, 0x12C, 0x6A, false); }
bool ParseNumbers(char* doc, char* parent) { return ParseListOf(doc, parent, 7, 0x128, 0x6C, true); }

// The game's 0x00905FED: LI. The item gets its bullet (0x008FF8AB) or its
// number as text (`%3d. `), then an empty node of its own, then the item's
// blocks and inline content (in the item, after that node).
bool ParseItem(char* doc, char* parent, int32_t number)
{
    Value label;
    char* const item = NewNode(0x28, addr::HTMLItemNode_Construct);
    AddChild(parent, item);
    if (number < 0) {
        if (char* const bullet = CallThis<char*>(addr::HTMLDocument_MakeBullet, doc, uint32_t{0}))
            AddChild(item, bullet);
    } else {
        CallPlain<void>(addr::cRZBaseString_Sprintf, &label.text, "%3d. ", number);
        AddText(doc, &label.text, item);
    }
    AddChild(item, NewNode(0x28, addr::HTMLItemBodyNode_Construct));
    for (Next(doc, false); InSet(Id(doc), kFlowTags); Next(doc, false))
        Flow(doc, item);
    if (Id(doc) != 0x96)
        GiveBack(doc);
    label.Release();
    return true;
}

// ====================================================================================
// Tables (ui.html.parser.table)

OPENSC4_HTML_DETOUR_1(bool, ParseTable, char*, 0x00906F3C)
OPENSC4_HTML_DETOUR_1(bool, ParseRow, char*, 0x009064B0)
OPENSC4_HTML_DETOUR_1(bool, ParseCell, char*, 0x009061DC)
OPENSC4_HTML_DETOUR_1(bool, ParseHeaderCell, char*, 0x009063E6)
OPENSC4_HTML_DETOUR_1(bool, ParseTableBody, char*, 0x00906B90)
OPENSC4_HTML_DETOUR_1(bool, ParseTableFoot, char*, 0x00906BE1)
OPENSC4_HTML_DETOUR_1(bool, ParseTableHead, char*, 0x00906C26)
OPENSC4_HTML_DETOUR_1(bool, SkipColumns, char*, 0x00905EA7)

// The game's 0x00906F3C: TABLE (0x94 bytes). BORDER (+0x28), WIDTH
// (0x009083C5), BGCOLOR (0x008FE64E), CELLPADDING (+0x34), CELLSPACING
// (+0x3C); then CAPTION skipped, COL and COLGROUP passed over, THEAD,
// TFOOT, TBODY and rows read in the order the game checks for them. Kept
// unless the last of them failed; the answer is that too.
bool ParseTable(char* doc, char* parent)
{
    bool ok = true;
    char* const table = NewNode(0x94, addr::HTMLTableNode_Construct);
    Value value;
    if (Attribute(doc, "BORDER", &value.text))
        CallPlain<void>(addr::HTMLText_ParseNumber, &value.text, table + 0x28);
    if (Attribute(doc, "WIDTH", &value.text))
        CallThis<void>(addr::HTMLTableNode_SetWidth, table, &value.text);
    if (Attribute(doc, "BGCOLOR", &value.text))
        CallThis<void>(addr::HTMLTableNode_SetBackground, table, &value.text);
    if (Attribute(doc, "CELLPADDING", &value.text))
        CallPlain<void>(addr::HTMLText_ParseLength, &value.text, table + 0x34);
    if (Attribute(doc, "CELLSPACING", &value.text))
        CallPlain<void>(addr::HTMLText_ParseLength, &value.text, table + 0x3C);
    CheckAttributes(doc, "ParseTABLE", "TABLE");
    Next(doc, true);
    if (Id(doc) == 0x84) {
        SkipTo(doc, 0x85);
        Next(doc, true);
    }
    for (;;) {
        if (Id(doc) == 0x86) {
            ok = true;
            Next(doc, true);
            continue;
        }
        if (Id(doc) == 0x87) {
            ok = Handler(addr::HTMLDocument_SkipColumns, doc, table);
            Next(doc, true);
            if (ok)
                continue;
        }
        break;
    }
    if (Id(doc) == 0x89) {
        ok = Handler(addr::HTMLDocument_ParseTableHead, doc, table);
        Next(doc, true);
    }
    if (Id(doc) == 0x8D) {
        ok = Handler(addr::HTMLDocument_ParseTableFoot, doc, table);
        Next(doc, true);
    }
    while (Id(doc) == 0x8B || Id(doc) == 0x93) {
        ok = Handler(addr::HTMLDocument_ParseTableBody, doc, table);
        Next(doc, true);
    }
    if (Id(doc) != 0x79)
        ok = SkipTo(doc, 0x79);
    if (ok)
        AddChild(parent, table);
    else if (table)
        DeleteNode(table);
    value.Release();
    return ok;
}

// The game's 0x009064B0: TR (0x3C bytes), taking the table's colour
// (+0x44, when +0x48) unless its BGCOLOR says otherwise, then its TH and
// TD cells. Kept, and listed in the table's rows (+0x4C), when it has
// cells and the last one did not fail.
bool ParseRow(char* doc, char* table)
{
    bool ok = true;
    char* row = NewNode(0x3C, addr::HTMLRowNode_Construct);
    if (At<uint8_t>(table, 0x48)) {
        At<uint32_t>(row, 0x34) = At<uint32_t>(table, 0x44);
        At<uint8_t>(row, 0x38) = 1;
    }
    Value value;
    if (Attribute(doc, "BGCOLOR", &value.text))
        CallThis<void>(addr::HTMLNode_SetBackground, row, &value.text);
    for (;;) {
        Next(doc, true);
        if (Id(doc) == 0x91)
            ok = Handler(addr::HTMLDocument_ParseHeaderCell, doc, row);
        else if (Id(doc) == 0x8F)
            ok = Handler(addr::HTMLDocument_ParseCell, doc, row);
        else
            break;
    }
    if (Id(doc) != 0x94)
        GiveBack(doc);
    if (ok && Children(row) != 0) {
        AddChild(table, row);
        PushPointer(table + 0x4C, &row);
        value.Release();
        return true;
    }
    if (row)
        DeleteNode(row);
    value.Release();
    return false;
}

// The game's 0x009061DC: TD (0x3C bytes), in the row and in its cells
// (+0x28), taking the row's colour. VALIGN (TOP 1, MIDDLE 2, BOTTOM 3,
// BASELINE 4 at +0x28), WIDTH (+0x2C), BGCOLOR, NOWRAP; ALIGN becomes the
// alignment while the cell's content is read.
bool ParseCell(char* doc, char* row)
{
    char* cell = NewNode(0x3C, addr::HTMLCellNode_Construct);
    if (At<uint8_t>(row, 0x38)) {
        At<uint32_t>(cell, 0x34) = At<uint32_t>(row, 0x34);
        At<uint8_t>(cell, 0x38) = 1;
    }
    AddChild(row, cell);
    PushPointer(row + 0x28, &cell);
    Value value;
    if (Attribute(doc, "VALIGN", &value.text)) {
        MakeUpper(&value.text);
        auto Is = [&](const char* name) { return CallPlain<uint8_t>(addr::SC4String_Equals, &value.text.body, name) != 0; };
        if (Is("TOP"))
            At<uint32_t>(cell, 0x28) = 1;
        else if (Is("MIDDLE"))
            At<uint32_t>(cell, 0x28) = 2;
        else if (Is("BOTTOM"))
            At<uint32_t>(cell, 0x28) = 3;
        else if (Is("BASELINE"))
            At<uint32_t>(cell, 0x28) = 4;
    }
    if (Attribute(doc, "WIDTH", &value.text))
        CallPlain<void>(addr::HTMLText_ParseLength, &value.text, cell + 0x2C);
    if (Attribute(doc, "BGCOLOR", &value.text))
        CallThis<void>(addr::HTMLNode_SetBackground, cell, &value.text);
    if (Attribute(doc, "NOWRAP", &value.text))
        CallThis<void>(addr::HTMLCellNode_SetNoWrap, cell, &value.text);
    int32_t align = 0;
    if (Attribute(doc, "ALIGN", &value.text))
        align = CallPlain<int32_t>(addr::HTMLText_ParseAlign, &value.text);
    PushAlign(doc);
    At<int32_t>(doc, kAlign) = align;
    CheckAttributes(doc, "ParseTD", "TD");
    for (Next(doc, false); InSet(Id(doc), kFlowTags); Next(doc, false))
        Flow(doc, cell);
    if (Id(doc) != 0x90)
        GiveBack(doc);
    PopAlign(doc);
    value.Release();
    return true;
}

// The game's 0x009063E6: TH, in the row only (not in its cells).
bool ParseHeaderCell(char* doc, char* row)
{
    char* const cell = NewNode(0x28, addr::HTMLHeaderCellNode_Construct);
    AddChild(row, cell);
    Value unused;
    for (Next(doc, false); InSet(Id(doc), kFlowTags); Next(doc, false))
        Flow(doc, cell);
    if (Id(doc) != 0x92)
        GiveBack(doc);
    unused.ReleaseBody();
    return true;
}

// TBODY, TFOOT and THEAD (0x00906B90, 0x00906BE1, 0x00906C26): their rows,
// straight into the table. TBODY is also how rows without one are read.
bool ParseRows(char* doc, char* table, uint32_t end)
{
    for (; Id(doc) == 0x93; Next(doc, true))
        Handler(addr::HTMLDocument_ParseRow, doc, table);
    if (Id(doc) != end)
        GiveBack(doc);
    return true;
}

bool ParseTableBody(char* doc, char* table)
{
    if (Id(doc) == 0x8B)
        Next(doc, true);
    return ParseRows(doc, table, 0x8C);
}

bool ParseTableFoot(char* doc, char* table)
{
    Next(doc, true);
    return ParseRows(doc, table, 0x8E);
}

bool ParseTableHead(char* doc, char* table)
{
    Next(doc, true);
    return ParseRows(doc, table, 0x8A);
}

// The game's 0x00905EA7: COLGROUP, its COL tags passed over.
bool SkipColumns(char* doc, char*)
{
    Next(doc, true);
    while (Id(doc) == 0x86)
        Next(doc, true);
    GiveBack(doc);
    return true;
}

// ====================================================================================
// Inline content (ui.html.parser.inline)

OPENSC4_HTML_DETOUR_1(bool, ParseInline, char*, 0x00907CCA)
OPENSC4_HTML_DETOUR_1(bool, ParseFontTag, char*, 0x0090727D)
OPENSC4_HTML_DETOUR_1(bool, ParsePhrase, char*, 0x00906985)
OPENSC4_HTML_DETOUR_1(bool, ParseSpecial, char*, 0x00907C3E)
OPENSC4_HTML_DETOUR_1(bool, ParseLink, char*, 0x00907A47)
OPENSC4_HTML_DETOUR_1(bool, ParseSpan, char*, 0x009075FD)
OPENSC4_HTML_DETOUR_1(bool, ParseBlink, char*, 0x009079A8)
OPENSC4_HTML_DETOUR_1(bool, ParseMap, char*, 0x009074D3)
OPENSC4_HTML_DETOUR_1(bool, ParseFormInline, char*, 0x00906E59)
OPENSC4_HTML_DETOUR_1(bool, ParseSelect, char*, 0x00906AFF)
OPENSC4_HTML_DETOUR_1(bool, ParseOption, char*, 0x009060DF)

// The game's 0x00907CCA: text (ID 3) as a text node, else by the tag's set:
// font tags, phrase tags, the special ones, form tags; false for anything
// else.
bool ParseInline(char* doc, char* parent)
{
    const uint32_t id = Id(doc);
    if (id == kTextId) {
        AddText(doc, TextAt(doc, kRunText), parent);
        return true;
    }
    if (InSet(id, kFontTags))
        return Handler(addr::HTMLDocument_ParseFontTag, doc, parent);
    if (InSet(id, kPhraseTags))
        return Handler(addr::HTMLDocument_ParsePhrase, doc, parent);
    if (InSet(id, kSpecialTags))
        return Handler(addr::HTMLDocument_ParseSpecial, doc, parent);
    if (InSet(id, kFormTags))
        return Handler(addr::HTMLDocument_ParseFormInline, doc, parent);
    return false;
}

// The content of an inline tag that changes the font: inline and block
// content into `parent` up to `end`, which is skipped to when it is not
// next. The answer of that skip, or true.
bool InlineContent(char* doc, char* parent, uint32_t end)
{
    for (;;) {
        Next(doc, false);
        const uint32_t id = Id(doc);
        if (InSet(id, kInlineTags))
            Inline(doc, parent);
        else if (InSet(id, kBlockTags))
            Block(doc, parent);
        else
            break;
    }
    return Id(doc) == end || SkipTo(doc, end);
}

// The game's 0x0090727D: TT (fixed width), I, B, STRIKE, U (style bits),
// BIG and SMALL (the fifth and third font steps), FONT (SIZE as a step,
// absolute or +n and -n from the fourth; COLOR; FACE). The font is pushed
// first and popped at the end; for an ID that is none of these the game
// returns false with the font still pushed, and so does ours.
bool ParseFontTag(char* doc, char* parent)
{
    PushFont(doc);
    const uint32_t id = Id(doc);
    const uint32_t end = id + 1;
    switch (id) {
    case 0x12:
        Erase(&At<Body>(doc, kFontFace), 0, kAll);
        At<uint32_t>(doc, kFontKind) = kFixedFace;
        break;
    case 0x14:
        At<uint32_t>(doc, kFontStyle) |= 2;
        break;
    case 0x16:
        At<uint32_t>(doc, kFontStyle) |= 1;
        break;
    case 0x18:
        At<uint32_t>(doc, kFontStyle) |= 8;
        break;
    case 0x1A:
        At<uint32_t>(doc, kFontSize) = At<uint32_t>(doc, kFontSteps + 4 * 4);
        break;
    case 0x1C:
        At<uint32_t>(doc, kFontSize) = At<uint32_t>(doc, kFontSteps + 2 * 4);
        break;
    case 0x1E:
        At<uint32_t>(doc, kFontStyle) |= 4;
        break;
    case 0x20: {
        Value value;
        if (Attribute(doc, "SIZE", &value.text)) {
            auto StartsWith = [&](char c) { return CallPlain<uint8_t>(addr::HTMLText_StartsWith, &value.text, c) != 0; };
            auto Number = [](const char* text) { return CallPlain<int32_t>(addr::SC4_TextToUint32, text); };
            int32_t step;
            if (StartsWith('+')) {
                step = Number(value.text.body.begin + 1) + 3;
                if (step > 6)
                    step = 6;
            } else if (StartsWith('-')) {
                step = 3 - Number(value.text.body.begin + 1);
                if (step < 0)
                    step = 0;
            } else {
                step = Number(value.text.body.begin);
                if (step > 6)
                    step = 6;
            }
            At<uint32_t>(doc, kFontSize) = At<uint32_t>(doc, kFontSteps + 4 * step);
        }
        if (Attribute(doc, "COLOR", &value.text)) {
            uint32_t colour = 0xFF000000;
            CallPlain<uint8_t>(addr::HTMLColor_Parse, &value.text, &colour);
            At<uint32_t>(doc, kFontColour) = MakeColour(colour);
            At<uint8_t>(doc, kFontDefaultColour) = 0;
        }
        if (Attribute(doc, "FACE", &value.text)) {
            AssignText(TextAt(doc, kFont), &value.text);
            At<uint32_t>(doc, kFontKind) = 0;
        }
        CheckAttributes(doc, "ParseFONT", "FONT");
        value.Release();
        break;
    }
    default:
        return false;
    }
    const bool ok = InlineContent(doc, parent, end);
    PopFont(doc);
    return ok;
}

// The game's 0x00906985: EM, DFN and CITE italic; STRONG bold; CODE, SAMP,
// KBD and VAR fixed width; ABBR and ACRONYM as they are; their end tags
// (and anything else) false, with the content read all the same. Only
// inline content is read.
bool ParsePhrase(char* doc, char* parent)
{
    bool ok = true;
    PushFont(doc);
    const uint32_t id = Id(doc);
    const uint32_t end = id + 1;
    switch (id) {
    case 0x22:
    case 0x26:
    case 0x30:
        At<uint32_t>(doc, kFontStyle) |= 2;
        break;
    case 0x24:
        At<uint32_t>(doc, kFontStyle) |= 1;
        break;
    case 0x28:
    case 0x2A:
    case 0x2C:
    case 0x2E:
        Erase(&At<Body>(doc, kFontFace), 0, kAll);
        At<uint32_t>(doc, kFontKind) = kFixedFace;
        break;
    case 0x32:
    case 0x34:
        break;
    default:
        ok = false;
        break;
    }
    for (Next(doc, false); InSet(Id(doc), kInlineTags); Next(doc, false))
        Inline(doc, parent);
    if (Id(doc) != end)
        SkipTo(doc, end);
    PopFont(doc);
    return ok;
}

// The game's 0x00907C3E: A, IMG (0x00900A58), BR (0x008FF725), MAP, SPAN,
// BLINK and SCRIPT read; OBJECT, Q, SUB, SUP and BDO skipped to their end
// tag; false for anything else.
bool ParseSpecial(char* doc, char* parent)
{
    switch (Id(doc)) {
    case 0x36:
        return Handler(addr::HTMLDocument_ParseLink, doc, parent);
    case 0x38:
        return Handler(addr::HTMLDocument_ParseIMG, doc, parent);
    case 0x39:
        return SkipTo(doc, 0x3A);
    case 0x3B:
        return Handler(addr::HTMLDocument_ParseBR, doc, parent);
    case 0x3C:
        return SkipScript(doc);
    case 0x3E:
        return Handler(addr::HTMLDocument_ParseMap, doc, parent);
    case 0x40:
        return SkipTo(doc, 0x41);
    case 0x42:
        return SkipTo(doc, 0x43);
    case 0x44:
        return SkipTo(doc, 0x45);
    case 0x46:
        return Handler(addr::HTMLDocument_ParseSpan, doc, parent);
    case 0x48:
        return SkipTo(doc, 0x49);
    case 0x4A:
        return Handler(addr::HTMLDocument_ParseBlink, doc, parent);
    default:
        return false;
    }
}

// The game's 0x00907A47: A (0x34 bytes). HREF kept (+0x28) and, with a base
// address, resolved against it (+0x2C); NAME kept (+0x30) and the link
// listed among the anchors (+0xC8). A link takes the link colour (the
// visited one when the Internet service says so, slot 24), from the root
// or the defaults, and is underlined. Its content goes inside it.
bool ParseLink(char* doc, char* parent)
{
    Value value;
    bool ok = true;
    bool linked = false;
    char* link = NewNode(0x34, addr::HTMLLinkNode_Construct);
    if (Attribute(doc, "HREF", &value.text)) {
        CallThis<void>(addr::HTMLLinkNode_SetHref, link, &value.text);
        if (Text* const base = At<Text*>(doc, 0xB8)) {
            alignas(4) char address[0xC0];
            CallThis<void>(addr::SC4Url_Construct, address);
            CallThis<void>(addr::SC4Url_Resolve, address, &value.text, base);
            CallThis<void>(addr::HTMLLinkNode_SetAddress, link, CallThis<Text*>(addr::SC4Url_Text, address));
            CallThis<void>(addr::SC4Url_Destroy, address);
        }
        linked = true;
    }
    if (Attribute(doc, "NAME", &value.text)) {
        CallThis<void>(addr::HTMLLinkNode_SetName, link, &value.text);
        PushPointer(doc + 0xC8, &link);
    }
    CheckAttributes(doc, "ParseA", "A");
    PushFont(doc);
    At<uint8_t>(doc, kFontDefaultColour) = 1;
    if (linked) {
        void* const service = At<void*>(doc, 0x30);
        void* const address = At<void*>(link, 0x2C);
        char* const root = At<char*>(doc, 0x14);
        uint32_t colour;
        if (service && address && Virtual<bool>(service, 0x60, address))
            colour = root ? At<uint32_t>(root, 0x34) : Global<uint32_t>(addr::HTMLDocument_VisitedColour);
        else
            colour = root ? At<uint32_t>(root, 0x30) : Global<uint32_t>(addr::HTMLDocument_LinkColour);
        At<uint32_t>(doc, kFontStyle) |= 4;
        At<uint32_t>(doc, kFontColour) = colour;
    }
    for (;;) {
        Next(doc, false);
        const uint32_t id = Id(doc);
        if (InSet(id, kInlineTags))
            Inline(doc, link);
        else if (InSet(id, kBlockTags))
            Block(doc, link);
        else
            break;
    }
    if (Id(doc) != 0x37)
        ok = SkipTo(doc, 0x37);
    PopFont(doc);
    if (!ok) {
        if (link)
            DeleteNode(link);
        value.Release();
        return false;
    }
    AddChild(parent, link);
    value.Release();
    return true;
}

// The game's 0x009075FD: SPAN, its STYLE read as `key: value;` pairs, each
// trimmed and lower-cased: color (`#rrggbb` straight into the font's
// colour, or `rgb(r,g,b)` through the colour maker), text-decoration (none,
// underline, line-through), font-family, font-size. font-size looks for
// `px` or `pt` in the key, not the value, so it never sets a size; it only
// puts a size of 0 or above 255 back to 14.
bool ParseSpan(char* doc, char* parent)
{
    Text style;
    MakeEmpty(&style);
    bool ok = true;
    char* const span = NewNode(0x28, addr::HTMLSpanNode_Construct);
    PushFont(doc);
    if (Attribute(doc, "STYLE", &style)) {
        Body& text = style.body;
        auto Part = [&](Text* into, uint32_t from, uint32_t count) {
            Body part;
            CallThis<Body*>(addr::SC4StringBody_Substr, &text, &part, from, count);
            CallThis<void>(addr::cSC4Advice_F408B90, into, &part);
            if (part.begin)
                Free(part.begin);
        };
        auto Is = [](const Text& t, const char* name) {
            return CallPlain<uint8_t>(addr::SC4String_Equals, &t.body, name) != 0;
        };
        uint32_t start = 0;
        uint32_t colon = FindChar(&text, ':', start);
        while (colon != kAll) {
            const uint32_t semicolon = FindChar(&text, ';', colon);
            Text key;
            Text value;
            Part(&key, start, colon - start);
            Part(&value, colon + 1, semicolon - colon - 1);
            CallThis<void>(addr::cRZBaseString_Trim, &key);
            CallThis<void>(addr::cRZBaseString_MakeLower, &key);
            CallThis<void>(addr::cRZBaseString_Trim, &value);
            CallThis<void>(addr::cRZBaseString_MakeLower, &value);
            if (Is(key, "color")) {
                uint32_t colour = 0;
                if (CallPlain<int32_t>(addr::SC4_sscanf, value.body.begin, "#%lx", &colour) == 1) {
                    At<uint32_t>(doc, kFontColour) = colour;
                } else {
                    int32_t red, green, blue;
                    if (CallPlain<int32_t>(addr::SC4_sscanf, value.body.begin, "rgb(%ld,%ld,%ld)", &red, &green,
                                           &blue) == 3)
                        At<uint32_t>(doc, kFontColour) = Virtual<uint32_t>(At<void*>(doc, 0x3C), 0x88, red, green, blue);
                }
            } else if (Is(key, "text-decoration")) {
                if (Is(value, "none"))
                    At<uint32_t>(doc, kFontStyle) &= ~4u;
                else if (Is(value, "underline"))
                    At<uint32_t>(doc, kFontStyle) |= 4;
                else if (Is(value, "line-through"))
                    At<uint32_t>(doc, kFontStyle) |= 8;
            } else if (Is(key, "font-family")) {
                AssignText(TextAt(doc, kFont), &value);
                At<uint32_t>(doc, kFontKind) = 0;
            } else if (Is(key, "font-size")) {
                CallThis<void>(addr::cRZBaseString_MakeLower, &key);
                if (Find(&key.body, "px", 0) != kAll || Find(&key.body, "pt", 0) != kAll)
                    At<uint32_t>(doc, kFontSize) = CallPlain<uint32_t>(addr::SC4_atol, key.body.begin);
                const uint32_t size = At<uint32_t>(doc, kFontSize);
                if (size == 0 || size >= 0x100)
                    At<uint32_t>(doc, kFontSize) = 0xE;
            }
            value.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
            if (semicolon == kAll || semicolon + 1 >= Length(text)) {
                if (value.body.begin)
                    Free(value.body.begin);
                DestroyInline(&key);
                break;
            }
            start = semicolon + 1;
            if (value.body.begin)
                Free(value.body.begin);
            DestroyInline(&key);
            colon = FindChar(&text, ':', start);
        }
    }
    CheckAttributes(doc, "ParseSPAN", "SPAN");
    for (;;) {
        Next(doc, false);
        const uint32_t id = Id(doc);
        if (InSet(id, kInlineTags))
            Inline(doc, span);
        else if (InSet(id, kBlockTags))
            Block(doc, span);
        else
            break;
    }
    if (Id(doc) != 0x47)
        ok = SkipTo(doc, 0x47);
    PopFont(doc);
    if (!ok) {
        if (span)
            DeleteNode(span);
        if (style.body.begin)
            Free(style.body.begin);
        return false;
    }
    AddChild(parent, span);
    if (style.body.begin)
        Free(style.body.begin);
    return true;
}

// The game's 0x009079A8: BLINK; sets the document's blinking flag (+0x2A)
// for the window's timer.
bool ParseBlink(char* doc, char* parent)
{
    char* const node = NewNode(0x28, addr::HTMLBlinkNode_Construct);
    for (;;) {
        Next(doc, false);
        const uint32_t id = Id(doc);
        if (InSet(id, kInlineTags))
            Inline(doc, node);
        else if (InSet(id, kBlockTags))
            Block(doc, node);
        else
            break;
    }
    At<uint8_t>(doc, 0x2A) = 1;
    if (Id(doc) == 0x4B || SkipTo(doc, 0x4B)) {
        AddChild(parent, node);
        return true;
    }
    if (node)
        DeleteNode(node);
    return false;
}

// The game's 0x009074D3: MAP, its NAME (0x00900519) and either its AREA
// tags (0x0090083A) or blocks; kept when the last of them did not fail.
bool ParseMap(char* doc, char* parent)
{
    bool ok;
    char* const map = NewNode(0x2C, addr::HTMLMapNode_Construct);
    Value value;
    if (Attribute(doc, "NAME", &value.text))
        CallThis<void>(addr::HTMLMapNode_SetName, map, &value.text);
    CheckAttributes(doc, "ParseMAP", "MAP");
    Next(doc, false);
    if (Id(doc) == 0x9B) {
        do {
            ok = Handler(addr::HTMLDocument_ParseArea, doc, map);
            Next(doc, false);
        } while (Id(doc) == 0x9B);
    } else if (InSet(Id(doc), kBlockTags)) {
        do {
            ok = Block(doc, map);
            Next(doc, false);
        } while (InSet(Id(doc), kBlockTags));
    } else {
        ok = false;
    }
    if (Id(doc) != 0x3F)
        SkipTo(doc, 0x3F);
    if (ok) {
        AddChild(parent, map);
        value.ReleaseBody();
        return true;
    }
    if (map)
        DeleteNode(map);
    value.ReleaseBody();
    return false;
}

// The game's 0x00906E59: INPUT (0x008FF82E) and SELECT read; TEXTAREA,
// LABEL and BUTTON skipped to their end tag; false for anything else.
bool ParseFormInline(char* doc, char* parent)
{
    switch (Id(doc)) {
    case 0x4C:
        return Handler(addr::HTMLDocument_ParseInput, doc, parent);
    case 0x4D:
        return Handler(addr::HTMLDocument_ParseSelect, doc, parent);
    case 0x53:
        return SkipTo(doc, 0x54);
    case 0x55:
        return SkipTo(doc, 0x56);
    case 0x57:
        return SkipTo(doc, 0x58);
    default:
        return false;
    }
}

// The game's 0x00906AFF: SELECT, its OPTION tags (OPTGROUP skipped to its
// end); kept when it has options.
bool ParseSelect(char* doc, char* parent)
{
    bool ok = true;
    char* const node = NewNode(0x28, addr::HTMLSelectNode_Construct);
    for (;;) {
        Next(doc, true);
        if (Id(doc) == 0x4F)
            Handler(addr::HTMLDocument_ParseOption, doc, node);
        else if (Id(doc) == 0x51)
            SkipTo(doc, 0x52);
        else
            break;
    }
    if (Id(doc) != 0x4E)
        ok = SkipTo(doc, 0x4E);
    if (ok && Children(node) != 0)
        AddChild(parent, node);
    else if (node)
        DeleteNode(node);
    return ok;
}

// The game's 0x009060DF: OPTION, its text; dropped when no text follows.
bool ParseOption(char* doc, char* parent)
{
    bool ok = true;
    char* const node = NewNode(0x28, addr::HTMLOptionNode_Construct);
    Next(doc, false);
    if (Id(doc) == kTextId) {
        AddText(doc, TextAt(doc, kRunText), node);
        AddChild(parent, node);
    } else {
        if (node)
            DeleteNode(node);
        ok = false;
    }
    if (Id(doc) != 0x50)
        GiveBack(doc);
    return ok;
}

// The third effect class (vtables 0x00AA0B88 and 0x00AA0B40), written by
// hand from the game's code: three small functions of it. Its maker,
// constructor and destructor are with the other classes' in
// water_effects.cpp. Included by water_effects.cpp, which has the calls
// the comparisons note.
//
// The class is made for a description with flag 1 (the byte +8) whose
// float at +0x1D8 is not zero; no test city has one. Its own fields, from
// what its functions do: +0x220 a block of storage, +0x22C a byte (1 when
// made), +0x230 a vector of 0x40-byte batches, +0x23C a vector of words,
// +0x248 a vector of held textures, +0x254 a model (an object with its own
// count, slot 8 its release), +0x25C a point, +0x268 a float (10.0 when
// made), +0x26C to +0x274 three floats its particles' step uses and +0x278
// a word.

// Letting go of what the effect holds (0x005A6310, slot 0x14 of the
// sub-object; `base` is the effect's +4), for an effect in use (+4): the
// model at +0x250 released (slot 8), the field cleared first; then the
// base class's (a jump to 0x005B2440).
uint32_t LetGoEffectC(char* base)
{
    if (!At<uint8_t>(base, 4))
        return 0;
    if (char* const model = At<char*>(base, 0x250)) {
        At<char*>(base, 0x250) = nullptr;
        ReleaseHeld(model);
    }
    return CallThisTraced(addr::SC4EffectBase_LetGo, base);
}

// Giving a particle back (0x005A6200, slot 0x40 of the sub-object): a
// particle that is not null is pushed onto the list at 0x00B0A5A0. The
// base class's take (slot 0x3C) takes from the pool at 0x00B0A688, so a
// particle given back here does not go back to that pool.
constexpr std::uintptr_t kParticleFreeC = 0x00B0A5A0;

uint32_t GiveBackParticleC(uint32_t particle)
{
    if (particle) {
        M<uint32_t>(particle) = Global<uint32_t>(kParticleFreeC);
        Global<uint32_t>(kParticleFreeC) = particle;
    }
    return particle;
}

// One of the effect's values set by id (0x005AAC90, slot 0x14 of the
// class, ret 0xC): 8 the point at +0x25C from three words; 9 handed to
// 0x005AA700 (the value and the value + 0xC); 0x101 the word at +0x278;
// each answering true. Any other id goes on to the base class's
// (0x005B2490) with the same three arguments, and its answer is the
// answer. `call` makes the two calls (the comparison notes them).
template <typename Call>
uint32_t SetEffectValueC(char* self, int32_t id, const uint32_t* value, uint32_t third, Call&& call)
{
    if (id == 8) {
        At<uint32_t>(self, 0x25C) = value[0];
        At<uint32_t>(self, 0x260) = value[1];
        At<uint32_t>(self, 0x264) = value[2];
        return 1;
    }
    if (id == 9) {
        call(0x005AA700u, reinterpret_cast<uint32_t>(value), reinterpret_cast<uint32_t>(value) + 0xC, 0u);
        return 1;
    }
    if (id == 0x101) {
        At<uint32_t>(self, 0x278) = value[0];
        return 1;
    }
    return call(addr::SC4Effect_SetValue, static_cast<uint32_t>(id), reinterpret_cast<uint32_t>(value), third);
}

// --- Its description and its model -------------------------------------------------------------
//
// The class's slot 0x28 (an effect given its description, 0x005AB190) and
// slot 0x2C (an effect given a model's instance, 0x005AAFE0), with what
// they call of the class's own: the model loaded by its ID (0x005AB0F0),
// the model's materials kept with their textures (0x005AAE80), a batch
// for each texture (0x005AAD70) and the game's std::vector code for the
// textures (+0x248) and the batches (+0x230), written out here.

// A batch is 0x40 bytes: a table of functions, four words, a vector
// (+0x14), three words, a vector (+0x2C) and two words. The game's own
// functions make one empty (0x005EC690), copy one (0x005EC750), set one up
// (0x005EC7C0, ret 0x18) and destroy one (0x005EC6E0; in a vector, its
// slot 0 with 0); its vectors are assigned by 0x00461210 and 0x00465A10.
constexpr std::uintptr_t kBatchConstruct = 0x005EC690, kBatchCopy = 0x005EC750, kBatchSetUp = 0x005EC7C0,
                         kBatchAssignFirst = 0x00461210, kBatchAssignSecond = 0x00465A10;

void McCopyBatch(char* to, char* from)
{
    CallThis<uint32_t>(kBatchCopy, to, from);
}

void McDestroyBatch(char* batch)
{
    CallThis<uint32_t>(M<uint32_t>(At<uint32_t>(batch, 0)), batch, 0u);
}

// One batch assigned to another (the loops of 0x005A8FF0, 0x005A9D00 and
// 0x005A9D80): the words as they are, the vectors by their own
// assignments, in the game's order.
void McAssignBatch(char* to, char* from)
{
    for (std::size_t at = 4; at < 0x14; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(from, at);
    CallThis<uint32_t>(kBatchAssignFirst, to + 0x14, from + 0x14);
    for (std::size_t at = 0x20; at < 0x2C; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(from, at);
    CallThis<uint32_t>(kBatchAssignSecond, to + 0x2C, from + 0x2C);
    At<uint32_t>(to, 0x38) = At<uint32_t>(from, 0x38);
    At<uint32_t>(to, 0x3C) = At<uint32_t>(from, 0x3C);
}

// Copies of `from` made one after another from `to`, `count` of them
// (0x005A6450), or one of each of [first, last) (0x005A6420); nothing is
// made at a null place. The answer is the place past the last.
char* McFillBatches(char* to, uint32_t count, char* from)
{
    for (; count; --count, to += 0x40) {
        if (to)
            McCopyBatch(to, from);
    }
    return to;
}

char* McCopyBatches(char* first, char* last, char* to)
{
    for (; first != last; first += 0x40, to += 0x40) {
        if (to)
            McCopyBatch(to, first);
    }
    return to;
}

// The batches' vector (begin, end, room at `vector`) growing for `count`
// copies of `value` at `where` (0x005A8C30, ret 0x14): room for as many
// again as it holds or for the new ones, whichever is more; those before
// copied, the new ones, those after unless `atEnd`; the old ones
// destroyed and their storage freed.
void McGrowBatches(char* vector, char* where, char* value, uint32_t count, bool atEnd)
{
    char* const begin = At<char*>(vector, 0);
    const uint32_t size = static_cast<uint32_t>((At<char*>(vector, 4) - begin) >> 6);
    const uint32_t room = size + (size < count ? count : size);
    char* const grown = room ? StlAllocate(room * 0x40) : nullptr;
    char* end = McCopyBatches(begin, where, grown);
    end = McFillBatches(end, count, value);
    if (!atEnd)
        end = McCopyBatches(where, At<char*>(vector, 4), end);
    for (char* batch = At<char*>(vector, 0); batch != At<char*>(vector, 4); batch += 0x40)
        McDestroyBatch(batch);
    if (At<char*>(vector, 0))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<char*>(vector, 0));
    At<char*>(vector, 4) = end;
    At<char*>(vector, 0) = grown;
    At<char*>(vector, 8) = grown + room * 0x40;
}

// `count` copies of `value` put in at `where` (0x005AA290, ret 0xC). With
// room: a copy of the value made first; when fewer than `count` follow
// `where`, those past the end made, the ones after `where` copied after
// them and the rest assigned; else the last `count` copied past the end,
// the rest moved up (assigned from the back) and the new ones assigned;
// the copy destroyed. Without room the vector grows.
void McInsertBatches(char* vector, char* where, uint32_t count, char* value)
{
    if (!count)
        return;
    if (static_cast<uint32_t>((At<char*>(vector, 8) - At<char*>(vector, 4)) >> 6) < count) {
        McGrowBatches(vector, where, value, count, false);
        return;
    }
    alignas(4) char copy[0x40];
    McCopyBatch(copy, value);
    char* const end = At<char*>(vector, 4);
    const uint32_t after = static_cast<uint32_t>((end - where) >> 6);
    if (after > count) {
        char* const moved = end - count * 0x40;
        McCopyBatches(moved, end, end);
        At<char*>(vector, 4) += count * 0x40;
        for (char *from = moved, *to = end; from != where;) {
            from -= 0x40;
            to -= 0x40;
            McAssignBatch(to, from);
        }
        for (char* to = where; to != where + count * 0x40; to += 0x40)
            McAssignBatch(to, copy);
    } else {
        McFillBatches(end, count - after, copy);
        At<char*>(vector, 4) += (count - after) * 0x40;
        McCopyBatches(where, end, At<char*>(vector, 4));
        At<char*>(vector, 4) += after * 0x40;
        for (char* to = where; to != end; to += 0x40)
            McAssignBatch(to, copy);
    }
    CallThis<uint32_t>(addr::SC4EffectBatch_Destruct, copy);
}

// [first, last) taken out (0x005AAC40, ret 8): those after assigned down,
// the ones left past the new end destroyed.
void McEraseBatches(char* vector, char* first, char* last)
{
    char* to = first;
    for (char* from = last; from != At<char*>(vector, 4); from += 0x40, to += 0x40)
        McAssignBatch(to, from);
    char* const end = At<char*>(vector, 4);
    for (char* batch = to; batch != end; batch += 0x40)
        McDestroyBatch(batch);
    At<char*>(vector, 4) = to;
}

// The batches made `count` (0x005AAD00, ret 4): an empty batch made, the
// ones past `count` taken out or copies of the empty one put in at the
// end, the empty one destroyed.
void McResizeBatches(char* vector, uint32_t count)
{
    alignas(4) char empty[0x40];
    CallThis<uint32_t>(kBatchConstruct, empty);
    char* const begin = At<char*>(vector, 0);
    char* const end = At<char*>(vector, 4);
    const uint32_t size = static_cast<uint32_t>((end - begin) >> 6);
    if (count < size)
        McEraseBatches(vector, begin + count * 0x40, end);
    else
        McInsertBatches(vector, end, count - size, empty);
    CallThis<uint32_t>(addr::SC4EffectBatch_Destruct, empty);
}

// The textures' vector: [first, last) taken out (0x005A8D40, ret 8; the
// ones after assigned down by 0x005A88C0, each holding its new texture
// before letting the old one go; the ones left past the new end let go).
void McEraseTextures(char* vector, char** first, char** last)
{
    char** to = first;
    for (char** from = last; from < At<char**>(vector, 4); ++from, ++to) {
        if (from == to)
            continue;
        char* const texture = *from;
        char* const old = *to;
        if (texture != old) {
            if (texture)
                CallThis<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
            *to = texture;
            if (old)
                CallThis<uint32_t>(addr::SC4TextureBinding_Release, old);
        }
    }
    char** const end = At<char**>(vector, 4);
    for (char** it = to; it != end; ++it) {
        if (*it)
            CallThis<uint32_t>(addr::SC4TextureBinding_Release, *it);
    }
    At<char**>(vector, 4) = to;
}

// A texture put at the end (held), the vector growing as 0x005AAE80 grows
// it: room for one when empty, else for twice as many; those held copied
// and held (0x005A63B0), the new one, then the old ones let go and their
// storage freed (0x005A8E90).
void McPushTexture(char* vector, char* texture)
{
    char** const end = At<char**>(vector, 4);
    if (end != At<char**>(vector, 8)) {
        if (end) {
            *end = texture;
            if (texture)
                CallThis<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
        }
        At<char**>(vector, 4) = end + 1;
        return;
    }
    char** const begin = At<char**>(vector, 0);
    const uint32_t size = static_cast<uint32_t>(end - begin);
    const uint32_t room = size + (size < 1 ? 1 : size);
    char** const grown = reinterpret_cast<char**>(StlAllocate(room * 4));
    char** to = grown;
    for (char** from = begin; from != end; ++from, ++to) {
        if (to) {
            *to = *from;
            if (*from)
                CallThis<uint32_t>(addr::SC4TextureBinding_AddRef, *from);
        }
    }
    if (to) {
        *to = texture;
        if (texture)
            CallThis<uint32_t>(addr::SC4TextureBinding_AddRef, texture);
    }
    for (char** it = At<char**>(vector, 0); it != At<char**>(vector, 4); ++it) {
        if (*it)
            CallThis<uint32_t>(addr::SC4TextureBinding_Release, *it);
    }
    if (At<char**>(vector, 0))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<char**>(vector, 0));
    At<char**>(vector, 0) = grown;
    At<char**>(vector, 4) = to + 1;
    At<char**>(vector, 8) = grown + room;
}

// 0x005AAD70: the words (+0x23C) emptied and one put for each texture of
// +0x248 (the first word its first word points at, 0 for none; a full
// vector grows by SC4PtrVector::InsertN), the batches made as many
// (0x005AAD00) and each set up (0x005EC7C0) with 0, 4, 6, 10, 0 and
// 0x005EC430 while the byte at +0x22C is set, else with 0, 3, 0, 10, 0
// and nothing.
void MakeEffectBatchesC(char* self)
{
    char* const words = self + 0x23C;
    At<char*>(words, 4) = At<char*>(words, 0);
    for (char** it = At<char**>(self, 0x248); it != At<char**>(self, 0x24C); ++it) {
        uint32_t word = *it ? M<uint32_t>(At<uint32_t>(*it, 0)) : 0;
        uint32_t* const end = At<uint32_t*>(words, 4);
        if (end == At<uint32_t*>(words, 8)) {
            uint8_t tag = 0;
            CallThis<uint32_t>(addr::SC4PtrVector_InsertN, words, end, &word, &tag, 1u, 1u);
        } else {
            if (end)
                *end = word;
            At<uint32_t*>(words, 4) = end + 1;
        }
    }
    char* const batches = self + 0x230;
    McResizeBatches(batches, static_cast<uint32_t>((At<char*>(words, 4) - At<char*>(words, 0)) >> 2));
    for (char* batch = At<char*>(batches, 0); batch != At<char*>(batches, 4); batch += 0x40) {
        if (At<uint8_t>(self, 0x22C))
            CallThis<uint32_t>(kBatchSetUp, batch, 0u, 4u, 6u, 10u, 0u, 0x005EC430u);
        else
            CallThis<uint32_t>(kBatchSetUp, batch, 0u, 3u, 0u, 10u, 0u, 0u);
    }
}

// 0x005AAE80 (ret 4): the model's materials kept at +0x254 (a new one held,
// slot 4, before the old one is released, slot 8; nothing for the same
// one); the textures let go, and for each material (their count asked
// once, slot 0xC, each at slot 0x18) with the byte at +0xB set, its
// texture (+0x10) held at the end; then the batches (0x005AAD70).
void SetEffectMaterialsC(char* self, char* materials)
{
    char* const old = At<char*>(self, 0x254);
    if (materials != old) {
        if (materials)
            SuSlot<uint32_t>(materials, 4);
        At<char*>(self, 0x254) = materials;
        if (old)
            SuSlot<uint32_t>(old, 8);
    }
    char* const textures = self + 0x248;
    McEraseTextures(textures, At<char**>(textures, 0), At<char**>(textures, 4));
    if (At<char*>(self, 0x254)) {
        const uint32_t count = SuSlot<uint32_t>(At<char*>(self, 0x254), 0xC);
        for (uint32_t i = 0; i < count; ++i) {
            char* const material = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x18, i));
            if (At<uint8_t>(material, 0xB))
                McPushTexture(textures, At<char*>(material, 0x10));
        }
    }
    MakeEffectBatchesC(self);
}

// 0x005AB0F0 (ret 4): nothing for 0. The resource manager (0x00B43CA8, slot
// 0xC) asked for the resource of type 0x5AD0E817, group 0xBADB57F1 and
// that instance by the interface 0x9AD0E7E3; with it the old materials
// released (the field cleared first), the resource's slot 0x18 with 0
// putting the new ones at +0x254, and those kept (0x005AAE80). The
// resource released at the end, found or not.
void LoadEffectModelC(char* self, uint32_t which)
{
    if (!which)
        return;
    char* resource = nullptr;
    const uint32_t key[3] = {0x5AD0E817, 0xBADB57F1, which};
    if (SuSlot<uint8_t>(M<char*>(addr::SC4_ResourceManager), 0xC, key, 0x9AD0E7E3u, &resource, 0u, 0u)) {
        if (char* const old = At<char*>(self, 0x254)) {
            At<char*>(self, 0x254) = nullptr;
            SuSlot<uint32_t>(old, 8);
        }
        SuSlot<uint32_t>(resource, 0x18, 0u, self + 0x254);
        SetEffectMaterialsC(self, At<char*>(self, 0x254));
    }
    if (resource)
        SuSlot<uint32_t>(resource, 8);
}

// 0x005AAFE0 (ret 4), slot 0x2C of the class: an effect given a model's
// instance. Its materials (the interface 0x26A68448, slot 0) held and
// released once, kept (0x005AAE80) and released; the instance's box (slot
// 0x20, given 1, 1, 1, 0, 0, 0) gives the point +0x25C: its middle, but
// y a tenth of the way up from its bottom (the game first stores the
// middle's y there too).
void SetEffectInstanceC(char* self, char* instance)
{
    char* materials = nullptr;
    SuSlot<uint32_t>(instance, 0, 0x26A68448u, &materials);
    if (materials) {
        SuSlot<uint32_t>(materials, 4);
        SuSlot<uint32_t>(materials, 8);
    }
    SetEffectMaterialsC(self, materials);
    if (materials)
        SuSlot<uint32_t>(materials, 8);
    float box[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    SuSlot<uint32_t>(instance, 0x20, box);
    const double half = M<float>(0x00A84D2C);
    const float sumZ = static_cast<float>(static_cast<double>(box[5]) + box[2]);
    At<float>(self, 0x264) = static_cast<float>(static_cast<double>(sumZ) * half);
    At<float>(self, 0x25C) = static_cast<float>((static_cast<double>(box[3]) + box[0]) * half);
    At<float>(self, 0x260) =
        static_cast<float>((static_cast<double>(box[4]) - box[1]) * M<float>(0x00A8C950) + box[1]);
}

// 0x005AB190 (ret 4), slot 0x28 of the class: nothing for a description
// without bit 0 of its third word (+8). A new description held
// (0x005AF940) and the old one let go (0x005B1C80); +0x21A cleared, the
// render state by the description's kind (+0xD4, 0x005B2260); the clock's
// rate (+0x130) one over the description's +0x14 or a small floor, two
// scales of one, +0x1D4 to +0x1DC from 0x00B0A580, the bytes +0x258 and
// +0x22C and the box +0x194 to +0x1B4 cleared, +0x268 the description's
// +0xF0 squared times 1024. For a model's ID (+0xD0, not 0 or -1) the
// model loaded (0x005AB0F0) and the point +0x25C taken from +0xB4.
void SetEffectDescriptionC(char* self, char* description)
{
    if (!(At<uint8_t>(description, 8) & 1))
        return;
    char* const old = At<char*>(self, 0x24);
    if (description != old) {
        CallThis<uint32_t>(addr::SC4Effect_Hold, description);
        At<char*>(self, 0x24) = description;
        if (old)
            CallThis<uint32_t>(addr::SC4EffectDescription_Release, old);
    }
    At<uint8_t>(self, 0x21A) = 0;
    CallThis<uint32_t>(addr::SC4Effect_SetKindState, self,
                       static_cast<uint32_t>(At<uint8_t>(At<char*>(self, 0x24), 0xD4)));
    char* const d = At<char*>(self, 0x24);
    float rate = 0.0f;
    const uint32_t floor = 0x358637BD;
    std::memcpy(&rate, &floor, 4);
    if (M<float>(0x00A86510) < At<float>(d, 0x14))
        rate = At<float>(d, 0x14);
    At<uint32_t>(self, 0x88) = 0xFFFFFFFF;
    At<float>(self, 0x1CC) = 1.0f;
    At<float>(self, 0x1D0) = 1.0f;
    At<double>(self, 0x130) = static_cast<double>(M<float>(0x00A81228)) / rate;
    for (uint32_t i = 0; i < 3; ++i)
        At<uint32_t>(self, 0x1D4 + i * 4) = M<uint32_t>(0x00B0A580 + i * 4);
    At<uint8_t>(self, 0x258) = 0;
    At<uint8_t>(self, 0x22C) = 0;
    for (uint32_t at = 0x194; at <= 0x1B4; at += 4)
        At<uint32_t>(self, at) = 0;
    const double size = At<float>(At<char*>(self, 0x24), 0xF0);
    At<float>(self, 0x268) = static_cast<float>(size * size * M<float>(0x00A94038));
    const uint32_t which = At<uint32_t>(description, 0xD0);
    if (which != 0 && which != 0xFFFFFFFF) {
        LoadEffectModelC(self, which);
        At<uint32_t>(self, 0x25C) = At<uint32_t>(self, 0xB4);
        At<uint32_t>(self, 0x260) = At<uint32_t>(self, 0xB8);
        At<uint32_t>(self, 0x264) = At<uint32_t>(self, 0xBC);
    }
}

// --- Its pieces ----------------------------------------------------------------------------------
//
// The model blown apart (what the class's emit, 0x005AA5D0, calls through
// 0x005A9E10 and 0x005AA3A0): a triangle or a quad of the model cut into
// 0x3C-byte pieces kept in the vector at +0x220, and a particle made for
// each (0x005A8930) at the piece's middle and added to the effect (slot
// 0x2C of the sub-object), its byte +0x18 the material's number and its
// word +0x44 the piece's. A piece is fifteen floats: the way from its
// middle back to its first corner (three), its two sides (three each, the
// third corner's first), the first corner's place on the texture and the
// other two's (two each).
//
// The x87 runs at 53 bits: intermediates are doubles, rounded to float
// where the game stores one, in the game's order (sums of products in
// the order the game adds them).
constexpr std::uintptr_t kParticlePoolC = 0x00B0A594, kPieceZero = 0x00B22D40;
constexpr std::size_t kPieceBytes = 0x3C;

// A piece copied (0x005A6220, its fifteen words in order).
void PcCopyPiece(char* to, char* from)
{
    for (std::size_t at = 0; at < kPieceBytes; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(from, at);
}

// A new piece (0x005A6340): nine words from 0x00B22D40, six zeros.
void PcClearPiece(char* piece)
{
    for (std::size_t at = 0; at < 0x24; at += 4)
        At<uint32_t>(piece, at) = M<uint32_t>(kPieceZero);
    for (std::size_t at = 0x24; at < kPieceBytes; at += 4)
        At<uint32_t>(piece, at) = 0;
}

// Copies of [first, last) made from `to` (0x005A63F0), none at a null
// place; the answer is the place past the last.
char* PcCopyPieces(char* first, char* last, char* to)
{
    for (; first != last; first += kPieceBytes, to += kPieceBytes) {
        if (to)
            PcCopyPiece(to, first);
    }
    return to;
}

// [first, last) assigned forwards from `to` (0x005A6480; the count by a
// signed division): the answer is the place past the last.
char* PcAssignPieces(char* first, char* last, char* to)
{
    for (int32_t count = static_cast<int32_t>(last - first) / static_cast<int32_t>(kPieceBytes); count > 0;
         --count, first += kPieceBytes, to += kPieceBytes)
        PcCopyPiece(to, first);
    return to;
}

// The pieces' vector (begin, end, room at `vector`) growing for `count`
// copies of `value` at `where` (0x005A8D90, ret 0x14): room for as many
// again as it holds or for the new ones, whichever is more; those before
// copied, the new ones, those after unless `atEnd`; the old storage freed
// (a piece has nothing to destroy).
void PcGrowPieces(char* vector, char* where, char* value, uint32_t count, bool atEnd)
{
    char* const begin = At<char*>(vector, 0);
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, 4) - begin) / static_cast<int32_t>(kPieceBytes));
    const uint32_t room = size + (size < count ? count : size);
    char* const grown = room ? StlAllocate(room * kPieceBytes) : nullptr;
    char* end = PcCopyPieces(begin, where, grown);
    for (uint32_t i = 0; i < count; ++i, end += kPieceBytes) {
        if (end)
            PcCopyPiece(end, value);
    }
    if (!atEnd)
        end = PcCopyPieces(where, At<char*>(vector, 4), end);
    if (At<char*>(vector, 0))
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(At<char*>(vector, 0));
    At<char*>(vector, 4) = end;
    At<char*>(vector, 8) = grown + room * kPieceBytes;
    At<char*>(vector, 0) = grown;
}

// A piece put at the end of +0x220 (the vector grows when full).
void PcPushPiece(char* self, char* piece)
{
    char* const vector = self + 0x220;
    char* const end = At<char*>(vector, 4);
    if (end == At<char*>(vector, 8)) {
        PcGrowPieces(vector, end, piece, 1, true);
        return;
    }
    if (end)
        PcCopyPiece(end, piece);
    At<char*>(vector, 4) = end + kPieceBytes;
}

// 0x005A8930 (ret 4): a particle made at `at`. The first of the list at
// 0x00B0A5A0 (the pool at 0x00B0A594 grown when it is empty), cleared
// (0x005B9DF0), its two vectors (+0x1C, +0x38) from 0x00B22D40, its byte 0
// set, +4 cleared, its place `at`. From the generator (0x00B22DF8), between
// the description's floats: a direction (+0x30..+0x38 to +0x3C..+0x44)
// made as long as a speed (+0x48 to +0x4C) unless it has no length; while
// the description's +0xF0 is above zero, a push of that much away from its
// point +0xF4 (none within a hair of it); its life +8 (+0xC to +0x10); +0x28
// one plus or minus +0x68, times the effect's +0x1C8; +0x2C one plus or
// minus +0x6C; +0x30 one for a +0x70 of zero, else plus or minus +0x70.
char* MakeParticleC(char* self, const float* at)
{
    char* particle = Global<char*>(kParticleFreeC);
    if (!particle) {
        CallThis<void>(addr::SC4ParticlePool_Grow, reinterpret_cast<void*>(kParticlePoolC));
        particle = Global<char*>(kParticleFreeC);
    }
    Global<uint32_t>(kParticleFreeC) = M<uint32_t>(Word(particle));
    CallThis<void>(addr::SC4Particle_Clear, particle);
    for (const std::size_t field : {std::size_t{0x1C}, std::size_t{0x20}, std::size_t{0x24}, std::size_t{0x38},
                                    std::size_t{0x3C}, std::size_t{0x40}})
        At<uint32_t>(particle, field) = M<uint32_t>(kPieceZero);
    At<uint8_t>(particle, 0) = 1;
    At<uint32_t>(particle, 4) = 0;
    for (std::size_t i = 0; i < 3; ++i)
        At<uint32_t>(particle, 0xC + i * 4) = reinterpret_cast<const uint32_t*>(at)[i];
    char* const d = At<char*>(self, 0x24);
    const float vx = static_cast<float>(SuUniform(At<float>(d, 0x30), At<float>(d, 0x3C)));
    const float vy = static_cast<float>(SuUniform(At<float>(d, 0x34), At<float>(d, 0x40)));
    At<float>(particle, 0x24) = static_cast<float>(SuUniform(At<float>(d, 0x38), At<float>(d, 0x44)));
    At<float>(particle, 0x1C) = vx;
    At<float>(particle, 0x20) = vy;
    const double speed = SuUniform(At<float>(At<char*>(self, 0x24), 0x48), At<float>(At<char*>(self, 0x24), 0x4C));
    const double x = At<float>(particle, 0x1C), y = At<float>(particle, 0x20), z = At<float>(particle, 0x24);
    const double length = (x * x + y * y) + z * z;
    const double zero = M<float>(0x00A81054);
    if (length > zero) {
        const double scale = speed / std::sqrt(length);
        At<float>(particle, 0x1C) = static_cast<float>(scale * At<float>(particle, 0x1C));
        At<float>(particle, 0x20) = static_cast<float>(scale * At<float>(particle, 0x20));
        At<float>(particle, 0x24) = static_cast<float>(scale * At<float>(particle, 0x24));
    }
    char* e = At<char*>(self, 0x24);
    if (At<float>(e, 0xF0) > zero) {
        const float dx = static_cast<float>(static_cast<double>(At<float>(particle, 0xC)) - At<float>(e, 0xF4));
        const float dy = static_cast<float>(static_cast<double>(At<float>(particle, 0x10)) - At<float>(e, 0xF8));
        const double dzWide = static_cast<double>(At<float>(particle, 0x14)) - At<float>(e, 0xFC);
        const float dz = static_cast<float>(dzWide);
        // The sum is compared with zero before it is rounded; its rounded
        // value is the one tested for a hair and rooted.
        const double sum = (dzWide * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
        const float rounded = static_cast<float>(sum);
        if (!(sum < zero) && !(std::fabs(static_cast<double>(rounded)) < M<float>(0x00AA0B28))) {
            const double push = At<float>(e, 0xF0) / std::sqrt(static_cast<double>(rounded));
            const double px = dx * push;
            const float py = static_cast<float>(dy * push), pz = static_cast<float>(dz * push);
            At<float>(particle, 0x1C) = static_cast<float>(px + At<float>(particle, 0x1C));
            At<float>(particle, 0x20) = static_cast<float>(static_cast<double>(py) + At<float>(particle, 0x20));
            At<float>(particle, 0x24) = static_cast<float>(static_cast<double>(pz) + At<float>(particle, 0x24));
        }
    }
    At<float>(particle, 8) =
        static_cast<float>(SuUniform(At<float>(At<char*>(self, 0x24), 0xC), At<float>(At<char*>(self, 0x24), 0x10)));
    const double one = M<float>(0x00A81228);
    const double size = At<float>(At<char*>(self, 0x24), 0x68);
    At<float>(particle, 0x28) = static_cast<float>(SuUniform(one - size, one + size) * At<float>(self, 0x1C8));
    const double turn = At<float>(At<char*>(self, 0x24), 0x6C);
    At<float>(particle, 0x2C) = static_cast<float>(SuUniform(one - turn, one + turn));
    const float spread = At<float>(At<char*>(self, 0x24), 0x70);
    if (spread == static_cast<float>(zero)) {
        At<uint32_t>(particle, 0x30) = 0x3F800000;
        return particle;
    }
    At<float>(particle, 0x30) = static_cast<float>(SuUniform(-static_cast<double>(spread), spread));
    return particle;
}

// A piece pushed and its particle made at `middle`, numbered `index`, of
// the material `which`, added to the effect.
void PcAddPiece(char* self, char* piece, const float* middle, uint32_t index, uint8_t which)
{
    PcPushPiece(self, piece);
    char* const particle = MakeParticleC(self, middle);
    At<uint8_t>(particle, 0x18) = which;
    At<uint32_t>(particle, 0x44) = index;
    SuSlot<uint32_t>(self + 4, 0x2C, particle);
}

// The effect's matrix (+0x90, by rows), its place (+0xB4) and scale
// (+0xC0).
struct PcFrame {
    double m[3][3];
    double place[3];
    double scale;
};

PcFrame PcFrameOf(char* self)
{
    PcFrame frame{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c)
            frame.m[r][c] = At<float>(self, 0x90 + (r * 3 + c) * 4);
        frame.place[r] = At<float>(self, 0xB4 + r * 4);
    }
    frame.scale = At<float>(self, 0xC0);
    return frame;
}

// One row of the matrix times a vector as the game adds it: the first and
// third products, then the second.
double PcTurn(const PcFrame& frame, std::size_t row, double x, double y, double z)
{
    const double* m = frame.m[row];
    return (m[0] * x + m[2] * z) + m[1] * y;
}

// The same with the third product first, then the second, then the first.
double PcTurnBack(const PcFrame& frame, std::size_t row, double x, double y, double z)
{
    const double* m = frame.m[row];
    return (m[2] * z + m[1] * y) + m[0] * x;
}

float PcF(double value)
{
    return static_cast<float>(value);
}

// 0x005A9820 (ret 0x10): one piece for the triangle `corner` (three
// numbers into the points `points`, 12 bytes each, and the places on the
// texture `places`, 8 bytes each), its second corner first. Its sides from
// there, its middle (the sum of the sides times 0x00B0A57C over three) and
// the second corner taken through the effect's matrix, scale and place.
void CutTriangleC(char* self, const float* points, const float* places, const int32_t* corner, uint8_t which)
{
    const PcFrame f = PcFrameOf(self);
    const double s = f.scale;
    const float* p0 = points + corner[0] * 3;
    const float* p1 = points + corner[1] * 3;
    const float* p2 = points + corner[2] * 3;
    const double ex = static_cast<double>(p0[0]) - p1[0], ey = static_cast<double>(p0[1]) - p1[1],
                 ez = static_cast<double>(p0[2]) - p1[2];
    const float fx = PcF(static_cast<double>(p2[0]) - p1[0]), fy = PcF(static_cast<double>(p2[1]) - p1[1]),
                fz = PcF(static_cast<double>(p2[2]) - p1[2]);
    const double sx = fx + ex;
    const float sy = PcF(fy + ey), sz = PcF(fz + ez);
    const float third = PcF(static_cast<double>(M<float>(0x00B0A57C)) * M<float>(0x00A92480));
    const float cx = PcF(sx * third), cy = PcF(static_cast<double>(sy) * third), cz = PcF(static_cast<double>(sz) * third);
    // The first side (to the first corner), taken through the matrix.
    const float r0 = PcF(PcTurnBack(f, 0, ex, ey, ez)), r1 = PcF(PcTurnBack(f, 1, ex, ey, ez)),
                r2 = PcF(PcTurnBack(f, 2, ex, ey, ez));
    const float e0 = PcF(s * r0), e1 = PcF(r1 * s), e2 = PcF(r2 * s);
    // The second side (to the third corner).
    const double t0 = PcTurn(f, 0, fx, fy, fz), t1 = PcTurn(f, 1, fx, fy, fz);
    const float t2 = PcF(PcTurn(f, 2, fx, fy, fz));
    const float g0 = PcF(s * t0), g1 = PcF(t1 * s), g2 = PcF(static_cast<double>(t2) * s);
    // The middle from the second corner.
    const double u0 = PcTurn(f, 0, cx, cy, cz), u1 = PcTurn(f, 1, cx, cy, cz);
    const float u2 = PcF(PcTurn(f, 2, cx, cy, cz));
    const float h0 = PcF(s * u0), h1 = PcF(u1 * s), h2 = PcF(static_cast<double>(u2) * s);
    // The second corner's place.
    const double w0 = PcTurn(f, 0, p1[0], p1[1], p1[2]), w1 = PcTurn(f, 1, p1[0], p1[1], p1[2]);
    const float w2 = PcF(PcTurn(f, 2, p1[0], p1[1], p1[2]));
    const float at0 = PcF(f.place[0] + static_cast<double>(PcF(s * w0)));
    const float at1 = PcF(f.place[1] + w1 * s);
    const float at2 = PcF(f.place[2] + static_cast<double>(w2) * s);
    const float* q0 = places + corner[0] * 2;
    const float* q1 = places + corner[1] * 2;
    const float* q2 = places + corner[2] * 2;
    const float du0 = PcF(static_cast<double>(q0[0]) - q1[0]), dv0 = PcF(static_cast<double>(q0[1]) - q1[1]);
    const float du2 = PcF(static_cast<double>(q2[0]) - q1[0]), dv2 = PcF(static_cast<double>(q2[1]) - q1[1]);
    const float u = q1[0], v = q1[1];
    char* const vector = self + 0x220;
    const uint32_t index = static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, 4) - At<char*>(vector, 0)) /
                                                 static_cast<int32_t>(kPieceBytes));
    alignas(4) char piece[kPieceBytes];
    PcClearPiece(piece);
    const float fields[15] = {-h0, -h1, -h2, g0, g1, g2, e0, e1, e2, u, v,
                              PcF(static_cast<double>(u) + du2), PcF(static_cast<double>(v) + dv2),
                              PcF(static_cast<double>(u) + du0), PcF(static_cast<double>(v) + dv0)};
    std::memcpy(piece, fields, sizeof fields);
    const float middle[3] = {PcF(static_cast<double>(at0) + h0), PcF(static_cast<double>(at1) + h1),
                             PcF(static_cast<double>(at2) + h2)};
    PcAddPiece(self, piece, middle, index, which);
}

// 0x005A9150 (ret 0x10): the quad `quad` (four points of three floats,
// the third the corner the grid starts from) cut into `count` by `count`
// pieces; `places` four places on the texture (the first not used). One
// over the count gives the steps: along the second point's side (the
// rows), the fourth's (along a row) and half the first's (to a piece's
// middle), each taken through the matrix and scale; the texture's steps
// the same along the second's and fourth's.
void CutGridC(char* self, const float* quad, const float* places, uint32_t count, uint8_t which)
{
    const PcFrame f = PcFrameOf(self);
    const double s = f.scale;
    double wide = static_cast<int32_t>(count);
    if (static_cast<int32_t>(count) < 0)
        wide += M<double>(0x00A80AB8);
    const float step = PcF(M<double>(0x00A80AB0) / wide);
    const double half = M<float>(0x00A84D2C);
    const float* c = quad + 6;
    // The second point's side.
    const double bx = static_cast<double>(quad[3]) - c[0], by = static_cast<double>(quad[4]) - c[1];
    const float bz = PcF(static_cast<double>(quad[5]) - c[2]);
    const float bxs = PcF(bx * step);
    const double bys = by * step, bzs = static_cast<double>(bz) * step;
    // The fourth's.
    const float dxs = PcF((static_cast<double>(quad[9]) - c[0]) * step),
                dys = PcF((static_cast<double>(quad[10]) - c[1]) * step);
    const float dzs = PcF(static_cast<double>(PcF(static_cast<double>(quad[11]) - c[2])) * step);
    // Half the first's.
    const float axs = PcF((static_cast<double>(quad[0]) - c[0]) * step),
                ays = PcF((static_cast<double>(quad[1]) - c[1]) * step);
    const double azs = static_cast<double>(PcF(static_cast<double>(quad[2]) - c[2])) * step;
    const float hx = PcF(static_cast<double>(axs) * half), hy = PcF(static_cast<double>(ays) * half),
                hz = PcF(azs * half);
    const float p0 = PcF(PcTurn(f, 0, bxs, bys, bzs)), p1 = PcF(PcTurn(f, 1, bxs, bys, bzs));
    const double p2 = PcTurn(f, 2, bxs, bys, bzs);
    const float b0 = PcF(s * p0), b1 = PcF(static_cast<double>(p1) * s), b2 = PcF(p2 * s);
    const double r0 = PcTurn(f, 0, dxs, dys, dzs), r1 = PcTurn(f, 1, dxs, dys, dzs);
    const float r2 = PcF(PcTurn(f, 2, dxs, dys, dzs));
    const float d0 = PcF(s * r0), d1 = PcF(r1 * s), d2 = PcF(static_cast<double>(r2) * s);
    const double m0 = PcTurn(f, 0, hx, hy, hz), m1 = PcTurn(f, 1, hx, hy, hz);
    const float m2 = PcF(PcTurn(f, 2, hx, hy, hz));
    const float h0 = PcF(s * m0), h1 = PcF(m1 * s), h2 = PcF(static_cast<double>(m2) * s);
    // The grid's corner.
    const double c0 = PcTurn(f, 0, c[0], c[1], c[2]), c1 = PcTurn(f, 1, c[0], c[1], c[2]);
    const float c2 = PcF(PcTurn(f, 2, c[0], c[1], c[2]));
    const float at0 = PcF(f.place[0] + static_cast<double>(PcF(s * c0)));
    const float at1 = PcF(f.place[1] + c1 * s);
    const float at2 = PcF(f.place[2] + static_cast<double>(c2) * s);
    // The texture's steps.
    const float ubu = PcF((static_cast<double>(places[2]) - places[4]) * step),
                ubv = PcF((static_cast<double>(places[3]) - places[5]) * step);
    const float udu = PcF((static_cast<double>(places[6]) - places[4]) * step),
                udv = PcF((static_cast<double>(places[7]) - places[5]) * step);
    char* const vector = self + 0x220;
    uint32_t index = static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, 4) - At<char*>(vector, 0)) /
                                           static_cast<int32_t>(kPieceBytes));
    for (uint32_t row = 0; row < count; ++row) {
        double fr = static_cast<int32_t>(row);
        if (static_cast<int32_t>(row) < 0)
            fr += M<float>(0x00A80AA8);
        float x = PcF(b0 * fr + at0);
        float y = PcF(static_cast<double>(PcF(b1 * fr)) + at1);
        float z = PcF(static_cast<double>(PcF(b2 * fr)) + at2);
        // The first u stays wide until it has been stored.
        double u = static_cast<double>(PcF(ubu * fr)) + places[4];
        float v = PcF(static_cast<double>(PcF(ubv * fr)) + places[5]);
        for (uint32_t left = count; left; --left) {
            const double nextU = u + udu;
            const float nextV = PcF(static_cast<double>(v) + udv);
            alignas(4) char piece[kPieceBytes];
            const float fields[15] = {-h0, -h1, -h2, d0, d1, d2, b0, b1, b2, PcF(u), v, PcF(nextU), nextV,
                                      PcF(u + ubu), PcF(static_cast<double>(v) + ubv)};
            std::memcpy(piece, fields, sizeof fields);
            const float middle[3] = {PcF(static_cast<double>(x) + h0), PcF(static_cast<double>(y) + h1),
                                     PcF(static_cast<double>(z) + h2)};
            PcAddPiece(self, piece, middle, index, which);
            v = nextV;
            x = PcF(static_cast<double>(x) + d0);
            ++index;
            y = PcF(static_cast<double>(y) + d1);
            z = PcF(static_cast<double>(z) + d2);
            u = PcF(nextU);
        }
    }
}

// 0x005A9FF0 (ret 0x14): the triangle `corner` (three numbers into the
// vectors of points `points` and texture places `places`) cut in two
// across its longest side (the later one for a tie; the third side runs
// from the third corner back to the first) until that is shorter than
// `most`, then a piece made of it (0x005A9820). The new point (the side's
// middle) is put at the end of both vectors (growing by 0x0050C3B0 and
// 0x007C61A0 when full); the halves are (that side's start, the new
// point, the corner across) and (the new point, the side's end, the
// corner across).
void SplitTriangleC(char* self, char* points, char* places, const int32_t* corner, float most, uint32_t which)
{
    const float* v = At<float*>(points, 0);
    float sides[3];
    for (std::size_t i = 0; i < 3; ++i) {
        const float* from = v + corner[i] * 3;
        const float* to = v + corner[(i + 1) % 3] * 3;
        const double dx = static_cast<double>(to[0]) - from[0], dy = static_cast<double>(to[1]) - from[1],
                     dz = static_cast<double>(to[2]) - from[2];
        sides[i] = PcF(std::sqrt((dz * dz + dy * dy) + dx * dx));
    }
    std::size_t longest = 0;
    if (sides[0] > sides[1])
        longest = sides[0] > sides[2] ? 0 : 2;
    else
        longest = sides[1] > sides[2] ? 1 : 2;
    if (sides[longest] < most) {
        CutTriangleC(self, v, At<float*>(places, 0), corner, static_cast<uint8_t>(which));
        return;
    }
    const int32_t a = corner[longest], b = corner[(longest + 1) % 3], across = corner[(longest + 2) % 3];
    const int32_t fresh = static_cast<int32_t>(At<char*>(points, 4) - At<char*>(points, 0)) / 12;
    const double half = M<float>(0x00A84D2C);
    const float* pa = v + a * 3;
    const float* pb = v + b * 3;
    const float point[3] = {PcF((static_cast<double>(pb[0]) + pa[0]) * half),
                            PcF((static_cast<double>(pa[1]) + pb[1]) * half),
                            PcF(static_cast<double>(PcF(static_cast<double>(pa[2]) + pb[2])) * half)};
    uint8_t tag = 0;
    char* const end = At<char*>(points, 4);
    if (end == At<char*>(points, 8)) {
        CallThis<uint32_t>(addr::SC4Float3Vector_Insert, points, end, point, &tag, 1u, 1u);
    } else {
        if (end)
            std::memcpy(end, point, sizeof point);
        At<char*>(points, 4) = end + 12;
    }
    const float* w = At<float*>(places, 0);
    const float place[2] = {PcF((static_cast<double>(w[a * 2]) + w[b * 2]) * half),
                            PcF((static_cast<double>(w[b * 2 + 1]) + w[a * 2 + 1]) * half)};
    char* const last = At<char*>(places, 4);
    if (last == At<char*>(places, 8)) {
        CallThis<uint32_t>(addr::SC4PointVector_InsertN, places, last, place, &tag, 1u, 1u);
    } else {
        if (last)
            std::memcpy(last, place, sizeof place);
        At<char*>(places, 4) = last + 8;
    }
    const int32_t first[3] = {a, fresh, across}, second[3] = {fresh, b, across};
    SplitTriangleC(self, points, places, first, most, which);
    SplitTriangleC(self, points, places, second, most, which);
}

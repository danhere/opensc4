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
        PlGrowFor(kParticlePoolC);
        particle = Global<char*>(kParticleFreeC);
    }
    Global<uint32_t>(kParticleFreeC) = M<uint32_t>(Word(particle));
    PlClearFor(particle);
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

// --- Its emit and step ---------------------------------------------------------------------------
//
// The class's emit (slot 0x3C, 0x005AA5D0), which the first time blows the
// model apart (0x005AA3A0 by triangles, 0x005A9E10 by quads when +0x22C is
// set), its particles' step (slot 0x50, 0x005A6520: a front that grows
// from the point +0x25C pushes the pieces out) and value 9's 0x005AA700
// (the point +0x25C put along a direction at the middle of the model). The
// model's materials (+0x254) answer their count (slot 0xC), each one's mesh
// (slot 0x10) and each material (slot 0x18, the byte +0xB whether it is
// drawn). A mesh: +0 its vertices (+0 their format, the word +4 their
// stride, the word +6 their count, +8 the data), +8 its primitives (a
// vector at +4 of 12-byte entries). The game's own code walks a
// primitive's triangles (SC4MeshTriangles: 0x0080AE60 makes the walker,
// 0x0080B320 starts it on a primitive, 0x0080B420 gives the next
// triangle's three numbers, 0x0080AE70 ends it) and gathers one element of
// each vertex (GDVertexFormat::Gather: 0 the place, 7 the place on the
// texture).

// e^x as the game takes it on the x87: x times log2(e), split at its
// nearest whole number, 2 to the rest less one (F2XM1), one added and
// scaled. F2XM1's last bits are the processor's own, so the same
// instructions are run.
double EcExp(double x)
{
    double result = 0.0;
    __asm {
        fld x
        fldl2e
        fmulp st(1), st(0)
        fld st(0)
        frndint
        fxch st(1)
        fsub st(0), st(1)
        f2xm1
        fld1
        faddp st(1), st(0)
        fscale
        fstp st(1)
        fstp result
    }
    return result;
}

// The whole number at or below `value`, as the step takes it: less the
// float at 0x00A84D2C, the float at 0x00AA0B20 (1.5 times 2^23) added, the
// float's bits less 0x4B400000, one up when the value is that one up.
int32_t EcRoundDown(double value)
{
    uint32_t whole =
        LfBits(static_cast<float>((value - M<float>(0x00A84D2C)) + M<float>(0x00AA0B20))) + 0xB4C00000u;
    const uint32_t up = whole + 1;
    if (value == static_cast<double>(static_cast<int32_t>(up)))
        whole = up;
    return static_cast<int32_t>(whole);
}

// 0x005A6520 (ret 8), slot 0x50 of the class: one particle stepped. With a
// front speed of zero (the description's +0x1EC) the base class's step
// (0x005B9BA0). Else, once a frame (while +0x270, last frame's front, is
// +0x26C), the front +0x26C grows by the step times that speed and its
// strength +0x274 is read off the description's curve (the floats of
// +0x1F4, by the clock +0x120's fraction, between two of them). The
// particle's distance from the point +0x25C (its square at least the
// float at 0x00AA0B28, else 1e-4) leaves a particle not yet aged (+4) and
// not yet reached by the front as it is (true). Else it ages (dead, its
// first byte cleared and false, at its life +8); its velocity (+0x1C) gets
// the step times the description's pull (+0xE0) and its wind (+0xEC times
// the effect's +0x1B8); then a push away from the point, by the strength
// over the distance times the difference of two fall-offs, at the front's
// distance behind (+0x26C) and last frame's (+0x270) (each over the
// description's +0x1F0, at least zero; with k the description's +0x200,
// (1/k - 1 + d) e^(-kd) / k); then slowed by one less the step times the
// description's +0x100; its place moves by half the step times the old
// velocity and the new.
uint32_t StepParticleC(char* self, char* particle, float step)
{
    char* d = At<char*>(self, 0x24);
    const double zero = M<float>(0x00A81054);
    if (At<float>(d, 0x1EC) == zero)
        return CallThis<uint32_t>(addr::SC4Effect_StepParticle, self, particle, step);
    const double s = step;
    if (At<float>(self, 0x270) == At<float>(self, 0x26C)) {
        At<float>(self, 0x26C) = PcF(s * At<float>(d, 0x1EC) + At<float>(self, 0x26C));
        const double clock = At<double>(self, 0x120);
        const int32_t whole = EcRoundDown(clock);
        const float* const curve = At<float*>(d, 0x1F4);
        const int32_t last = (static_cast<int32_t>(At<char*>(d, 0x1F8) - At<char*>(d, 0x1F4)) >> 2) - 1;
        float strength = 0.0f;
        if (last == 0) {
            strength = curve[0];
        } else {
            const double x = (clock - whole) * LfUnsignedF(static_cast<uint32_t>(last));
            const int32_t at = EcRoundDown(x);
            const double part = x - LfUnsignedF(static_cast<uint32_t>(at));
            if (part > zero)
                strength = PcF((static_cast<double>(curve[at + 1]) - curve[at]) * part + curve[at]);
            else
                strength = curve[at];
        }
        At<float>(self, 0x274) = strength;
    }
    const float dx = PcF(static_cast<double>(At<float>(particle, 0xC)) - At<float>(self, 0x25C));
    const float dy = PcF(static_cast<double>(At<float>(particle, 0x10)) - At<float>(self, 0x260));
    const float dz = PcF(static_cast<double>(At<float>(particle, 0x14)) - At<float>(self, 0x264));
    const double sum =
        (static_cast<double>(dz) * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
    float square = PcF(sum);
    if (sum < M<float>(0x00AA0B28)) {
        const uint32_t least = 0x38D1B717;
        std::memcpy(&square, &least, 4);
    }
    const float distance = PcF(std::sqrt(static_cast<double>(square)));
    if (!(At<float>(particle, 4) > zero) && !(At<float>(self, 0x26C) > distance))
        return 1;
    const double age = s + At<float>(particle, 4);
    At<float>(particle, 4) = PcF(age);
    if (age >= At<float>(particle, 8)) {
        At<uint8_t>(particle, 0) = 0;
        return 0;
    }
    d = At<char*>(self, 0x24);
    const double one = M<float>(0x00A81228);
    const double k = At<float>(d, 0x200);
    const float inverse = PcF(one / k);
    const double reach = one / At<float>(d, 0x1F0);
    float behind = PcF((static_cast<double>(At<float>(self, 0x270)) - distance) * reach);
    double ahead = (static_cast<double>(At<float>(self, 0x26C)) - distance) * reach;
    if (behind < zero)
        behind = 0.0f;
    if (ahead < zero)
        ahead = zero;
    // The pull: the first sum stored before the wind is added to it, the
    // other two kept.
    const float oldX = At<float>(particle, 0x1C), oldY = At<float>(particle, 0x20), oldZ = At<float>(particle, 0x24);
    const double gx = s * At<float>(d, 0xE0), gy = s * At<float>(d, 0xE4);
    const float gz = PcF(s * At<float>(d, 0xE8));
    const double vx1 = gx + oldX;
    const float vx1f = PcF(vx1);
    At<float>(particle, 0x1C) = vx1f;
    const double vy1 = gy + oldY;
    At<float>(particle, 0x20) = PcF(vy1);
    const double vz1 = static_cast<double>(gz) + oldZ;
    At<float>(particle, 0x24) = PcF(vz1);
    // The wind.
    d = At<char*>(self, 0x24);
    const double w = At<float>(d, 0xEC);
    const double wx = w * At<float>(self, 0x1B8);
    const float wy = PcF(w * At<float>(self, 0x1BC)), wz = PcF(w * At<float>(self, 0x1C0));
    const double hx = wx * s, hy = static_cast<double>(wy) * s;
    const float hz = PcF(static_cast<double>(wz) * s);
    const float vx2 = PcF(static_cast<double>(vx1f) + hx);
    At<float>(particle, 0x1C) = vx2;
    const float vy2 = PcF(vy1 + hy);
    At<float>(particle, 0x20) = vy2;
    const float vz2 = PcF(vz1 + hz);
    At<float>(particle, 0x24) = vz2;
    // The push.
    const float less = PcF(static_cast<double>(inverse) - one);
    const double nearer = (static_cast<double>(less) + ahead) * EcExp(-(ahead * k)) * inverse;
    const double farther =
        (static_cast<double>(less) + behind) * EcExp(-(static_cast<double>(behind) * k)) * inverse;
    const double push = (nearer - farther) * (static_cast<double>(At<float>(self, 0x274)) / distance);
    const double px = static_cast<double>(dx) * push;
    const float py = PcF(static_cast<double>(dy) * push), pz = PcF(static_cast<double>(dz) * push);
    const double vx3 = static_cast<double>(vx2) + px;
    At<float>(particle, 0x1C) = PcF(vx3);
    const double vy3 = static_cast<double>(vy2) + py;
    At<float>(particle, 0x20) = PcF(vy3);
    const float vz3 = PcF(static_cast<double>(vz2) + pz);
    At<float>(particle, 0x24) = vz3;
    // Slowed.
    const float keep = PcF(one - s * At<float>(At<char*>(self, 0x24), 0x100));
    const float vx4 = PcF(vx3 * keep);
    At<float>(particle, 0x1C) = vx4;
    const double vy4 = vy3 * keep;
    At<float>(particle, 0x20) = PcF(vy4);
    const double vz4 = static_cast<double>(vz3) * keep;
    At<float>(particle, 0x24) = PcF(vz4);
    // The place.
    const float sx = PcF(static_cast<double>(vx4) + oldX), sy = PcF(vy4 + oldY);
    const double sz = vz4 + oldZ;
    const double h = s * M<double>(0x00A92D28);
    const float mx = PcF(static_cast<double>(sx) * h), my = PcF(static_cast<double>(sy) * h);
    const double mz = sz * h;
    At<float>(particle, 0xC) = PcF(static_cast<double>(mx) + At<float>(particle, 0xC));
    At<float>(particle, 0x10) = PcF(static_cast<double>(my) + At<float>(particle, 0x10));
    At<float>(particle, 0x14) = PcF(mz + At<float>(particle, 0x14));
    return 1;
}

constexpr std::uintptr_t kPointFill = 0x00B216F8;

void EcFree(void* storage)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(storage);
}

// A mesh's number of primitives (its vector's 12-byte entries, by a signed
// division).
uint32_t EcPrimitives(char* mesh)
{
    char* const list = At<char*>(mesh, 8);
    return static_cast<uint32_t>(static_cast<int32_t>(At<char*>(list, 8) - At<char*>(list, 4)) / 12);
}

// One element of each of the vertices gathered at `out`.
void EcGather(char* vertices, void* out, int32_t type)
{
    reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t, void*, uint32_t, int32_t, uint32_t)>(
        addr::GDVertexFormat_Gather)(At<uint16_t>(vertices, 6), At<uint32_t>(vertices, 8), out,
                                     At<uint32_t>(vertices, 0), type, 0);
}

// A vector of `count` points, each three words of 0x00B216F8 (0x00490B80),
// or of `count` texture places of zeros (0x00490C10, and 0x005AA3A0's own
// loop); none made at a null place.
void EcMakeVector(char* vector, uint32_t count, uint32_t bytes)
{
    char* const storage = count ? StlAllocate(count * bytes) : nullptr;
    char* to = storage;
    for (uint32_t i = 0; i < count; ++i, to += bytes) {
        if (to) {
            for (uint32_t at = 0; at < bytes; at += 4)
                At<uint32_t>(to, at) = bytes == 12 ? M<uint32_t>(kPointFill) : 0;
        }
    }
    At<char*>(vector, 0) = storage;
    At<char*>(vector, 4) = to;
    At<char*>(vector, 8) = storage + count * bytes;
}

// The size a piece is cut by: the root of the float +0x1E8 points at times
// +0x1C4, times the description's +0x1D8.
double EcSize(char* self)
{
    return std::sqrt(static_cast<double>(*At<float*>(self, 0x1E8)) * At<float>(self, 0x1C4)) *
           At<float>(At<char*>(self, 0x24), 0x1D8);
}

// 0x005AA3A0: the pieces emptied (0x005A6480 assigning nothing), then for
// each drawn material, for each primitive of its mesh (their count asked
// again each time): a walker started on that primitive, the mesh's
// vertices' places and texture places gathered into two new vectors, and
// each triangle the walker gives halved (SplitTriangle, with the material's
// number) until its sides are below the constant at 0x00A8D45C over the
// size; the vectors freed (the texture places first).
void CutTrianglesC(char* self)
{
    char* const pieces = self + 0x220;
    At<char*>(pieces, 4) = PcAssignPieces(At<char*>(pieces, 4), At<char*>(pieces, 4), At<char*>(pieces, 0));
    const uint32_t count = SuSlot<uint32_t>(At<char*>(self, 0x254), 0xC);
    for (uint32_t i = 0; i < count; ++i) {
        char* const material = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x18, i));
        if (!At<uint8_t>(material, 0xB))
            continue;
        char* const mesh = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x10, i));
        alignas(4) char triangles[0x30];
        CallThis<uint32_t>(addr::SC4MeshTriangles_Construct, triangles);
        for (uint32_t j = 0; j < EcPrimitives(mesh); ++j) {
            char* const vertices = At<char*>(mesh, 0);
            const uint32_t n = At<uint16_t>(vertices, 6);
            CallThis<uint32_t>(addr::SC4MeshTriangles_Start, triangles, mesh, j);
            alignas(4) char points[12], places[12];
            EcMakeVector(points, n, 12);
            EcMakeVector(places, n, 8);
            EcGather(vertices, At<char*>(points, 0), 0);
            EcGather(vertices, At<char*>(places, 0), 7);
            const float most = PcF(M<float>(0x00A8D45C) / EcSize(self));
            int32_t corner[3] = {};
            while (CallThis<uint8_t>(addr::SC4MeshTriangles_Next, triangles, corner))
                SplitTriangleC(self, points, places, corner, most, i);
            if (At<char*>(places, 0))
                EcFree(At<char*>(places, 0));
            if (At<char*>(points, 0))
                EcFree(At<char*>(points, 0));
        }
        CallThis<uint32_t>(addr::SC4MeshTriangles_Destruct, triangles);
    }
}

// 0x005A9E10: the same by quads, for each primitive of a drawn material's
// mesh whose vertices are a multiple of four (the primitive itself is not
// looked at: each one cuts all the mesh's vertices again): every four of
// them (a last short four too) a quad cut into a grid (CutGrid), as many a
// side as the size (EcSize) truncated.
void CutQuadsC(char* self)
{
    char* const pieces = self + 0x220;
    At<char*>(pieces, 4) = PcAssignPieces(At<char*>(pieces, 4), At<char*>(pieces, 4), At<char*>(pieces, 0));
    const uint32_t count = SuSlot<uint32_t>(At<char*>(self, 0x254), 0xC);
    for (uint32_t i = 0; i < count; ++i) {
        char* const material = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x18, i));
        if (!At<uint8_t>(material, 0xB))
            continue;
        char* const mesh = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x10, i));
        for (uint32_t j = 0; j < EcPrimitives(mesh); ++j) {
            char* const vertices = At<char*>(mesh, 0);
            const uint32_t n = At<uint16_t>(vertices, 6);
            if (n & 3)
                continue;
            alignas(4) char points[12], places[12];
            EcMakeVector(points, n, 12);
            EcMakeVector(places, n, 8);
            float* quad = At<float*>(points, 0);
            EcGather(vertices, quad, 0);
            EcGather(vertices, At<char*>(places, 0), 7);
            const double size = EcSize(self);
            const uint32_t made =
                static_cast<uint32_t>(static_cast<int32_t>(At<char*>(points, 4) - At<char*>(points, 0)) / 12);
            if (made) {
                const uint32_t side = static_cast<uint32_t>(static_cast<int64_t>(size));
                const float* place = At<float*>(places, 0);
                for (uint32_t left = ((made - 1) >> 2) + 1; left; --left, quad += 12, place += 8)
                    CutGridC(self, quad, place, side, static_cast<uint8_t>(i));
            }
            if (At<char*>(places, 0))
                EcFree(At<char*>(places, 0));
            if (At<char*>(points, 0))
                EcFree(At<char*>(points, 0));
        }
    }
}

// 0x005AA5D0, slot 0x3C of the class: the emit. Without materials or with
// +0x188 set, the effect is stopped (slot 4 with 0) once it says it is
// done (slot 8). Else the step is the effect's +0x108; a wait (+0x190) is
// counted down by it first (still waiting: +0x18 set, nothing more; done:
// the step is what was left over). The first time (+0x258 clear and the
// clock +0x120 at zero) the front and its strength are cleared and the
// model is blown apart (by quads when +0x22C is set). Last frame's front
// (+0x270) is kept, the clock moves by the step times its rate (+0x130),
// held below one (0x00AA0BE0), and an effect with no particles (+0x18) is
// stopped.
void EmitEffectC(char* self)
{
    if (!At<char*>(self, 0x254) || At<uint32_t>(self, 0x188)) {
        if (SuSlot<uint8_t>(self, 8))
            SuSlot<uint32_t>(self, 4, 0u);
        return;
    }
    float step = At<float>(self, 0x108);
    const double zero = M<double>(0x00A80990);
    if (At<float>(self, 0x190) > zero) {
        const double left = static_cast<double>(At<float>(self, 0x190)) - step;
        At<float>(self, 0x190) = PcF(left);
        if (left > zero) {
            At<uint32_t>(self, 0x18) = 1;
            return;
        }
        step = PcF(-left);
    }
    if (!At<uint8_t>(self, 0x258) && At<double>(self, 0x120) == zero) {
        At<uint32_t>(self, 0x26C) = 0;
        At<uint32_t>(self, 0x274) = 0;
        At<uint8_t>(self, 0x258) = 1;
        if (At<uint8_t>(self, 0x22C))
            CutQuadsC(self);
        else
            CutTrianglesC(self);
    }
    At<uint32_t>(self, 0x270) = At<uint32_t>(self, 0x26C);
    const double clock = static_cast<double>(step) * At<double>(self, 0x130) + At<double>(self, 0x120);
    At<double>(self, 0x120) = clock;
    if (clock >= M<double>(0x00A80AB0))
        At<double>(self, 0x120) = M<double>(0x00AA0BE0);
    if (!At<uint32_t>(self, 0x18))
        SuSlot<uint32_t>(self, 4, 0u);
}

// 0x005AA700 (ret 8), value 9 (from 0x005AAC90): the point +0x25C put on
// the line through `at` along `along`, at the middle of the model along it
// plus +0x278. Every material's mesh (drawn or not), once for each of its
// primitives, adds its vertices' places to one vector (made as long by
// SC4TripleVector::InsertN with points of 0x00B22D40, or cut down). The
// effect's place: +0xB4 through the matrix at +0xC8 (by rows), times +0xF8,
// plus +0xEC. Each vertex moved by it and taken along `along`; the least
// and the most of those (the first one's unrounded value is the first
// least compared, a NaN always taken), their middle less `at` along it.
void SetPointAlongC(char* self, const float* at, const float* along)
{
    if (!At<char*>(self, 0x254))
        return;
    alignas(4) char points[12] = {};
    uint32_t taken = 0;
    const uint32_t count = SuSlot<uint32_t>(At<char*>(self, 0x254), 0xC);
    for (uint32_t i = 0; i < count; ++i) {
        char* const mesh = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x10, i));
        for (uint32_t j = 0; j < EcPrimitives(mesh); ++j) {
            char* const vertices = At<char*>(mesh, 0);
            const uint32_t fill[3] = {M<uint32_t>(kPieceZero), M<uint32_t>(kPieceZero), M<uint32_t>(kPieceZero)};
            const uint32_t size =
                static_cast<uint32_t>(static_cast<int32_t>(At<char*>(points, 4) - At<char*>(points, 0)) / 12);
            const uint32_t wanted = At<uint16_t>(vertices, 6) + taken;
            if (wanted < size)
                At<char*>(points, 4) = At<char*>(points, 0) + wanted * 12; // 0x00506280 copying nothing
            else
                CallThis<uint32_t>(addr::SC4TripleVector_InsertN, points, At<char*>(points, 4), wanted - size, fill);
            EcGather(vertices, At<char*>(points, 0) + taken * 12, 0);
            taken += At<uint16_t>(vertices, 6);
        }
    }
    float* const begin = At<float*>(points, 0);
    if (At<char*>(points, 0) == At<char*>(points, 4)) {
        if (begin)
            EcFree(begin);
        return;
    }
    const double x = At<float>(self, 0xB4), y = At<float>(self, 0xB8), z = At<float>(self, 0xBC);
    const auto row = [&](uint32_t from) {
        return (z * At<float>(self, from + 8) + y * At<float>(self, from + 4)) + x * At<float>(self, from);
    };
    const double scale = At<float>(self, 0xF8);
    const float q0 = PcF(static_cast<double>(PcF(static_cast<double>(PcF(row(0xC8))) * scale)) + At<float>(self, 0xEC));
    const float q1 = PcF(static_cast<double>(PcF(static_cast<double>(PcF(row(0xD4))) * scale)) + At<float>(self, 0xF0));
    const float q2 = PcF(row(0xE0) * scale + At<float>(self, 0xF4));
    const auto along3 = [&](const float* v) {
        return ((static_cast<double>(q0) + v[0]) * along[0] + (static_cast<double>(q2) + v[2]) * along[2]) +
               (static_cast<double>(q1) + v[1]) * along[1];
    };
    const uint32_t made =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(points, 4) - At<char*>(points, 0)) / 12);
    double least = along3(begin);
    float leastF = PcF(least), most = leastF;
    for (uint32_t i = 1; i < made; ++i) {
        const float value = PcF(along3(begin + i * 3));
        if (!(least < value))
            leastF = value;
        least = leastF;
        if (!(value < most))
            most = value;
    }
    const double onto = (static_cast<double>(at[0]) * along[0] + static_cast<double>(along[2]) * at[2]) +
                        static_cast<double>(at[1]) * along[1];
    const double h = ((least + most) * M<double>(0x00A92D28) - onto) + At<float>(self, 0x278);
    const float sx = PcF(h * along[0]), sy = PcF(h * along[1]);
    At<float>(self, 0x264) = PcF(h * along[2] + at[2]);
    At<float>(self, 0x25C) = PcF(static_cast<double>(sx) + at[0]);
    At<float>(self, 0x260) = PcF(static_cast<double>(sy) + at[1]);
    EcFree(begin);
}

// --- Its draws -----------------------------------------------------------------------------------
//
// The class's two draws: slot 0x44 (0x005A6990, ret 4, with the render
// context), the model drawn whole until the emit has blown it apart, then
// the pieces into the batches (+0x230, one for each drawn material's
// texture); and slot 0x48 (0x005A77F0, ret 4, its context not used), the
// pieces into the dynamic vertex buffer (+0x1EC). A piece's three corners
// (a fourth for a batch of quads, +8 four) are the particle's place plus
// the piece's way back to its first corner (that corner), plus its two
// sides scaled by the size (turned by the turn). Neither grows the box
// (+0x1A0), which starts at the point +0x194. The calls that cannot run
// twice (the context's, the batch's draw and the buffer's lock, unlock and
// draw) go through BOurs, so that the comparison's replay answers them
// from the game's notes.

// A list of floats of the description (begin at `at`) read at `t` as the
// first draw reads it: the place and the part unrounded (EcRoundDown for
// the rounding down), the answer unrounded.
double EcCurve(char* description, uint32_t at, double t)
{
    const float* const values = At<const float*>(description, at);
    const int32_t last =
        (static_cast<int32_t>(At<uint32_t>(description, at + 4) - At<uint32_t>(description, at)) >> 2) - 1;
    if (last == 0)
        return values[0];
    const double x = LfUnsignedF(static_cast<uint32_t>(last)) * t;
    const uint32_t i = static_cast<uint32_t>(EcRoundDown(x));
    const double part = x - LfUnsignedF(i);
    if (part > M<float>(0x00A81054))
        return (static_cast<double>(values[i + 1]) - values[i]) * part + values[i];
    return values[i];
}

// The same list as the second draw reads it (BfCurve with the float at
// 0x00AA0B20): the place kept as a float for the test of the rounding down
// and the part, the part tested unrounded and multiplied rounded, the
// answer a float.
float EcCurveF(char* description, uint32_t at, float t)
{
    const float* const values = At<const float*>(description, at);
    const int32_t last =
        (static_cast<int32_t>(At<uint32_t>(description, at + 4) - At<uint32_t>(description, at)) >> 2) - 1;
    if (last == 0)
        return values[0];
    const double x = LfUnsignedF(static_cast<uint32_t>(last)) * t;
    const float place = PcF(x);
    uint32_t i = LfBits(PcF((x - M<float>(0x00A84D2C)) + M<float>(0x00AA0B20))) + 0xB4C00000u;
    if (static_cast<double>(place) == static_cast<double>(static_cast<int32_t>(i + 1)))
        i += 1;
    const double part = static_cast<double>(place) - LfUnsignedF(i);
    if (part > M<float>(0x00A81054))
        return PcF((static_cast<double>(values[i + 1]) - values[i]) * PcF(part) + values[i]);
    return values[i];
}

// A colour's part (0 to 1) as a byte: the float at 0x00AA0B24 added, as
// BfColourByte.
uint8_t EcColourByte(double value)
{
    const uint32_t bits = LfBits(PcF(value + M<float>(0x00AA0B24))) + 0xB8C00000u;
    return static_cast<uint8_t>(bits - (bits >> 8));
}

// The box (+0x1A0 to +0x1B4) started at the point +0x194.
void EcStartBox(char* self)
{
    for (uint32_t i = 0; i < 3; ++i) {
        At<uint32_t>(self, 0x1A0 + i * 4) = At<uint32_t>(self, 0x194 + i * 4);
        At<uint32_t>(self, 0x1AC + i * 4) = At<uint32_t>(self, 0x194 + i * 4);
    }
}

uint32_t EcBatches(char* self)
{
    return static_cast<uint32_t>((At<char*>(self, 0x234) - At<char*>(self, 0x230)) >> 6);
}

// What the context's model calls are noted with: the transform (two bytes,
// two the game leaves as they were, then thirteen floats) and the
// material's properties (two bytes, two left, four floats), those two
// bytes cleared; the material before its properties.
constexpr std::size_t kEcTransform = 0x38, kEcProperties = 0x14;

void EcNoteBlock(char* to, const char* from, std::size_t size)
{
    std::memcpy(to, from, size);
    to[2] = to[3] = 0;
}

// The model drawn whole (the first draw before the model is blown apart,
// with materials): the context's model transform (0x007D3400) is the
// second block (+0xC8: rows, place, scale; its byte +0xC5), or with the
// first block's byte (+0x8D) set and the second's clear the first (+0x90,
// its byte), or with both set the second taken after the first: the
// first's place through the second's rows (each row's three products added
// in its own order, the third row's sum rounded first), times its scale,
// plus its place; the rows the second's times the first's; the scales
// multiplied; the bytes or'd. Then each material (their count asked each
// time, slot 0x18) set up (0x007D5310) with properties of two clear bytes,
// the floats at 0x00B0A580 to 0x00B0A588 and one, and its mesh (slot 0x10)
// drawn (0x007D4A80); +0x18 is cleared.
void DrawModelC(char* self, char* context)
{
    alignas(4) char transform[kEcTransform] = {};
    float* const m = reinterpret_cast<float*>(transform + 4);
    const uint8_t first = At<uint8_t>(self, 0x8D), second = At<uint8_t>(self, 0xC5);
    std::memcpy(m, self + 0xC8, 13 * 4);
    uint8_t flag = second;
    if (first && !second) {
        std::memcpy(m, self + 0x90, 13 * 4);
        flag = first;
    } else if (first) {
        const auto a = [&](uint32_t i) { return static_cast<double>(At<float>(self, 0x90 + i * 4)); };
        const auto b = [&](uint32_t i) { return static_cast<double>(At<float>(self, 0xC8 + i * 4)); };
        const double x = a(9), y = a(10), z = a(11), s = b(12);
        const double px = (b(0) * x + b(1) * y) + b(2) * z;
        const double py = (b(4) * y + b(3) * x) + b(5) * z;
        const float pz = PcF((b(7) * y + b(6) * x) + b(8) * z);
        const float sx = PcF(s * px), sy = PcF(py * s);
        m[9] = PcF(b(9) + sx);
        m[10] = PcF(static_cast<double>(sy) + b(10));
        m[11] = PcF(static_cast<double>(pz) * s + b(11));
        m[0] = PcF((b(0) * a(0) + b(2) * a(6)) + b(1) * a(3));
        m[1] = PcF((b(0) * a(1) + b(1) * a(4)) + b(2) * a(7));
        m[2] = PcF((b(0) * a(2) + b(1) * a(5)) + b(2) * a(8));
        m[3] = PcF((b(3) * a(0) + b(5) * a(6)) + b(4) * a(3));
        m[4] = PcF((b(4) * a(4) + b(3) * a(1)) + b(5) * a(7));
        m[5] = PcF((b(4) * a(5) + b(3) * a(2)) + b(5) * a(8));
        m[6] = PcF((b(6) * a(0) + b(8) * a(6)) + b(7) * a(3));
        m[7] = PcF((b(7) * a(4) + b(6) * a(1)) + b(8) * a(7));
        m[8] = PcF((b(7) * a(5) + b(6) * a(2)) + b(8) * a(8));
        m[12] = PcF(s * a(12));
        flag = static_cast<uint8_t>(second | first);
    }
    At<uint8_t>(transform, 0) = 0;
    At<uint8_t>(transform, 1) = flag;
    BOurs(addr::SC4RenderContext_SetModelTransform, Word(context), Word(transform), kEcTransform,
          [&] { return CallThis<uint32_t>(addr::SC4RenderContext_SetModelTransform, context, transform); });
    alignas(4) char properties[kEcProperties] = {};
    for (uint32_t i = 0; i < 3; ++i)
        At<uint32_t>(properties, 4 + i * 4) = M<uint32_t>(0x00B0A580 + i * 4);
    At<uint32_t>(properties, 0x10) = 0x3F800000u;
    for (uint32_t i = 0; i < SuSlot<uint32_t>(At<char*>(self, 0x254), 0xC); ++i) {
        char* const material = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x18, i));
        char* const mesh = reinterpret_cast<char*>(SuSlot<uint32_t>(At<char*>(self, 0x254), 0x10, i));
        alignas(4) char noted[4 + kEcProperties];
        At<char*>(noted, 0) = material;
        EcNoteBlock(noted + 4, properties, kEcProperties);
        BOurs(addr::SC4RenderContext_SetMaterial, Word(context), Word(noted), sizeof noted, [&] {
            return CallThis<uint32_t>(addr::SC4RenderContext_SetMaterial, context, material, properties);
        });
        BOurs(addr::SC4RenderContext_DrawMesh, Word(context), Word(&mesh), 4,
              [&] { return CallThis<uint32_t>(addr::SC4RenderContext_DrawMesh, context, mesh); });
    }
    At<uint32_t>(self, 0x18) = 0;
}

// 0x005A6990, slot 0x44 of the class (ret 4), with the render context.
// Before the model is blown apart (+0x258 clear), with materials, the
// model drawn whole (DrawModelC). Else every batch emptied (0x005EC4A0),
// the box started at +0x194, and for each particle of the list (+0x14,
// read again for every node) its batch (by its byte +0x18) given a quad's
// indices (0x005EC850) and vertices (0x005EC8C0) and its piece (its word
// +0x44) written as three 0x18-byte vertices (place, colour, place on the
// texture), a fourth for a batch of quads (+8 four). With `t` its age over
// its life as a float, the description's lists read at `t` (EcCurve) give
// a turn (+0xC4 times the particle's +0x30), a size (+0xAC times the
// float at 0x00A9A414), the colour (the list of three floats at +0x94, as
// the plain draw reads it, times the effect's +0x1D4 to +0x1DC: the red
// and blue rounded, the green not) and the opacity (+0xA0, unrounded). The
// corner is the particle's place plus the piece's first three floats; its
// sides the next three and the three after times the size, turned by the
// turn times the float at 0x00AA0B2C (the sine and cosine rounded; each
// side's first value turned unrounded) when the turn is not zero. The
// vertices: the corner plus the first side (the piece's last place on the
// texture), the corner (its first), the corner plus the second side (its
// second; the x kept unrounded), and for a quad that plus the first side
// (the second and third places less the first). The box is widened by the
// corner. Last each batch with vertices (+0x24) has its texture's word
// (+0x23C) bound (0x007D4070 with 0) and is drawn (0x005EC5D0).
void DrawEffectC(char* self, char* context)
{
    if (!At<uint8_t>(self, 0x258) && At<char*>(self, 0x254)) {
        DrawModelC(self, context);
        return;
    }
    for (uint32_t i = 0; i < EcBatches(self); ++i)
        CallThis<uint32_t>(addr::SC4IndexBatch_Clear, At<char*>(self, 0x230) + i * 0x40);
    char* const description = At<char*>(self, 0x24);
    const int32_t lastColour =
        static_cast<int32_t>(At<uint32_t>(description, 0x98) - At<uint32_t>(description, 0x94)) / 12 - 1;
    EcStartBox(self);
    const double zero = M<float>(0x00A81054);
    for (char* node = At<char*>(At<char*>(self, 0x14), 0); node != At<char*>(self, 0x14); node = At<char*>(node, 0)) {
        char* const p = At<char*>(node, 8);
        char* const batch = At<char*>(self, 0x230) + static_cast<uint32_t>(At<uint8_t>(p, 0x18)) * 0x40;
        const bool quad = At<uint32_t>(batch, 8) == 4;
        CallThis<uint32_t>(addr::SC4IndexBatch_AddQuads, batch, 1u);
        char* const out = Ptr(CallThis<uint32_t>(addr::SC4IndexBatch_AddVertices, batch, 1u));
        const float t = PcF(static_cast<double>(At<float>(p, 4)) / At<float>(p, 8));
        char* const d = At<char*>(self, 0x24);
        const float turn = PcF(EcCurve(d, 0xC4, t) * At<float>(p, 0x30));
        const float size = PcF(EcCurve(d, 0xAC, t) * M<float>(0x00A9A414));
        const float* e = At<const float*>(d, 0x94);
        double red = 0.0, green = 0.0;
        float blue = 0.0f;
        bool mixed = false;
        if (lastColour != 0) {
            const double x = LfUnsignedF(static_cast<uint32_t>(lastColour)) * t;
            const uint32_t i = static_cast<uint32_t>(EcRoundDown(x));
            const float part = PcF(x - LfUnsignedF(i));
            e += i * 3;
            if (part > zero) {
                mixed = true;
                const double nextRed = static_cast<double>(part) * e[3];
                const float nextGreen = PcF(static_cast<double>(part) * e[4]);
                const float nextBlue = PcF(static_cast<double>(part) * e[5]);
                const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
                const float restRed = PcF(rest * e[0]), restGreen = PcF(rest * e[1]), restBlue = PcF(rest * e[2]);
                red = nextRed + restRed;
                green = static_cast<double>(restGreen) + nextGreen;
                blue = PcF(static_cast<double>(restBlue) + nextBlue);
            }
        }
        if (!mixed) {
            red = e[0];
            green = e[1];
            blue = e[2];
        }
        const float r = PcF(red * At<float>(self, 0x1D4));
        const double g = green * At<float>(self, 0x1D8);
        const float b = PcF(static_cast<double>(blue) * At<float>(self, 0x1DC));
        const double opacity = EcCurve(d, 0xA0, t);
        const float* const piece =
            reinterpret_cast<const float*>(At<char*>(self, 0x220) + At<uint32_t>(p, 0x44) * kPieceBytes);
        const float place[3] = {PcF(static_cast<double>(At<float>(p, 0xC)) + piece[0]),
                                PcF(static_cast<double>(At<float>(p, 0x10)) + piece[1]),
                                PcF(static_cast<double>(At<float>(p, 0x14)) + piece[2])};
        float second[3], first[3];
        for (uint32_t i = 0; i < 3; ++i) {
            second[i] = PcF(static_cast<double>(size) * piece[3 + i]);
            first[i] = PcF(static_cast<double>(size) * piece[6 + i]);
        }
        if (!(static_cast<double>(turn) == M<double>(0x00A80990))) {
            float sine, cosine;
            BfSinCos(static_cast<double>(turn) * M<float>(0x00AA0B2C), &sine, &cosine);
            const double s = sine, c = cosine;
            const float g0 = PcF(second[0] * c + first[0] * s);
            const float g1 = PcF(static_cast<double>(PcF(second[1] * c)) + PcF(first[1] * s));
            const float g2 = PcF(static_cast<double>(PcF(second[2] * c)) + PcF(first[2] * s));
            const float e0 = PcF(first[0] * c - second[0] * s);
            const float e1 = PcF(static_cast<double>(PcF(first[1] * c)) - PcF(second[1] * s));
            const float e2 = PcF(static_cast<double>(PcF(first[2] * c)) - PcF(second[2] * s));
            second[0] = g0, second[1] = g1, second[2] = g2;
            first[0] = e0, first[1] = e1, first[2] = e2;
        }
        BfWiden(self, place);
        const uint32_t colour = EcColourByte(b) | EcColourByte(g) << 8 | EcColourByte(r) << 16 |
                                static_cast<uint32_t>(EcColourByte(opacity)) << 24;
        const double sideX = static_cast<double>(second[0]) + place[0];
        const float sideY = PcF(static_cast<double>(second[1]) + place[1]);
        const float sideZ = PcF(static_cast<double>(second[2]) + place[2]);
        const auto vertex = [&](uint32_t n, float x, float y, float z, float u, float v) {
            float* const to = reinterpret_cast<float*>(out + n * 0x18);
            to[0] = x, to[1] = y, to[2] = z;
            std::memcpy(to + 3, &colour, 4);
            to[4] = u, to[5] = v;
        };
        vertex(0, PcF(static_cast<double>(first[0]) + place[0]), PcF(static_cast<double>(first[1]) + place[1]),
               PcF(static_cast<double>(first[2]) + place[2]), piece[13], piece[14]);
        vertex(1, place[0], place[1], place[2], piece[9], piece[10]);
        vertex(2, PcF(sideX), sideY, sideZ, piece[11], piece[12]);
        if (quad)
            vertex(3, PcF(sideX + first[0]), PcF(static_cast<double>(sideY) + first[1]),
                   PcF(static_cast<double>(sideZ) + first[2]),
                   PcF((static_cast<double>(piece[11]) + piece[13]) - piece[9]),
                   PcF(static_cast<double>(PcF(static_cast<double>(piece[12]) + piece[14])) - piece[10]));
    }
    for (uint32_t i = 0; i < EcBatches(self); ++i) {
        char* const batch = At<char*>(self, 0x230) + i * 0x40;
        if (!At<uint32_t>(batch, 0x24))
            continue;
        uint32_t* const word = At<uint32_t*>(self, 0x23C) + i;
        BOurs(addr::SC4RenderContext_BindTexture, Word(context), Word(word), 4,
              [&] { return CallThis<uint32_t>(addr::SC4RenderContext_BindTexture, context, *word, 0u); });
        char* const drawn = At<char*>(self, 0x230) + i * 0x40;
        if (g_bReplay)
            PlKeepVertices(drawn, g_bfOurVertices);
        BOurs(addr::SC4IndexBatch_Draw, Word(drawn), Word(&context), 4,
              [&] { return CallThis<uint32_t>(addr::SC4IndexBatch_Draw, drawn, context); });
    }
}

// 0x005A77F0, slot 0x48 of the class (ret 4, the context not used): the
// pieces into the dynamic vertex buffer (+0x1EC). The box started at
// +0x194. The count (+0x18) is drawn in runs of at most the buffer's
// vertices (its slot 0x14 with 0) over four (a signed test; the count is
// tested unsigned): the first batch emptied (0x005EC4A0) and given the
// run's quads (0x005EC850), the buffer locked (slot 0x18 with 0) for the
// batch's vertices a piece (+8) times the run (none, and the draw ends
// there), so many pieces written as 0x20-byte vertices (place, colour,
// place on the texture; the last eight bytes left as they were), the
// buffer unlocked (slot 0x20 with 0) and drawn (slot 0x28 with two zeros,
// the batch's index count +0x38 and its indices +0x2C). The game never
// moves on from the list's first node: every piece written is the first
// particle's. No texture is bound. A piece as the first draw's, but for
// the arithmetic, kept to floats (the lists read by EcCurveF; the colour
// as the buffered draw reads it, without the effect's scales; each side's
// values turned on their own), the opacity a float too, and the place
// first turned about the y axis around the effect's +0xB4 when the
// description's +0x104 is not zero (by the height above +0xB8 times it;
// the sine and cosine rounded), as the buffered draw turns it; the
// vertices' sums each rounded.
void DrawEffectCBuffered(char* self)
{
    uint32_t left = At<uint32_t>(self, 0x18);
    const uint32_t room = SuSlot<uint32_t>(At<char*>(self, 0x1EC), 0x14, 0u) >> 2;
    char* const description = At<char*>(self, 0x24);
    const int32_t lastColour =
        static_cast<int32_t>(At<uint32_t>(description, 0x98) - At<uint32_t>(description, 0x94)) / 12 - 1;
    EcStartBox(self);
    const bool quad = At<uint32_t>(At<char*>(self, 0x230), 8) == 4;
    char* const node = At<char*>(At<char*>(self, 0x14), 0);
    const double zero = M<float>(0x00A81054);
    while (left) {
        const uint32_t run = static_cast<int32_t>(room) < static_cast<int32_t>(left) ? room : left;
        left -= run;
        CallThis<uint32_t>(addr::SC4IndexBatch_Clear, At<char*>(self, 0x230));
        CallThis<uint32_t>(addr::SC4IndexBatch_AddQuads, At<char*>(self, 0x230), run);
        char* out = BfLock(At<char*>(self, 0x1EC), At<uint32_t>(At<char*>(self, 0x230), 8) * run);
        if (!out)
            return;
        for (uint32_t n = 0; n < run; ++n) {
            char* const p = At<char*>(node, 8);
            const float t = PcF(static_cast<double>(At<float>(p, 4)) / At<float>(p, 8));
            char* const d = At<char*>(self, 0x24);
            const float turn = PcF(static_cast<double>(EcCurveF(d, 0xC4, t)) * At<float>(p, 0x30));
            const float size = PcF(static_cast<double>(EcCurveF(d, 0xAC, t)) * M<float>(0x00A9A414));
            const float* e = At<const float*>(d, 0x94);
            float red = e[0], green = e[1], blue = e[2];
            if (lastColour != 0) {
                const double x = LfUnsignedF(static_cast<uint32_t>(lastColour)) * t;
                const float place = PcF(x);
                uint32_t i = LfBits(PcF((x - M<float>(0x00A84D2C)) + M<float>(0x00AA0B20))) + 0xB4C00000u;
                if (static_cast<double>(place) == static_cast<double>(static_cast<int32_t>(i + 1)))
                    i += 1;
                const double between = static_cast<double>(place) - LfUnsignedF(i);
                const float part = PcF(between);
                e += i * 3;
                red = e[0], green = e[1], blue = e[2];
                if (between > zero) {
                    const float nextBlue = PcF(static_cast<double>(part) * e[5]);
                    const float nextGreen = PcF(static_cast<double>(part) * e[4]);
                    const float nextRed = PcF(static_cast<double>(part) * e[3]);
                    const double rest = static_cast<double>(M<float>(0x00A81228)) - part;
                    const float restRounded = PcF(rest);
                    const float restBlue = PcF(rest * e[2]);
                    const float restGreen = PcF(static_cast<double>(restRounded) * e[1]);
                    const float restRed = PcF(static_cast<double>(restRounded) * e[0]);
                    blue = PcF(static_cast<double>(restBlue) + nextBlue);
                    green = PcF(static_cast<double>(restGreen) + nextGreen);
                    red = PcF(static_cast<double>(restRed) + nextRed);
                }
            }
            const float opacity = EcCurveF(d, 0xA0, t);
            const float* const piece =
                reinterpret_cast<const float*>(At<char*>(self, 0x220) + At<uint32_t>(p, 0x44) * kPieceBytes);
            float place[3] = {PcF(static_cast<double>(piece[0]) + At<float>(p, 0xC)),
                              PcF(static_cast<double>(At<float>(p, 0x10)) + piece[1]),
                              PcF(static_cast<double>(At<float>(p, 0x14)) + piece[2])};
            float second[3], first[3];
            for (uint32_t i = 0; i < 3; ++i) {
                second[i] = PcF(static_cast<double>(size) * piece[3 + i]);
                first[i] = PcF(static_cast<double>(size) * piece[6 + i]);
            }
            if (!(static_cast<double>(At<float>(d, 0x104)) == M<double>(0x00A80990))) {
                const float dz = PcF(static_cast<double>(place[2]) - At<float>(self, 0xBC));
                const float dy = PcF(static_cast<double>(place[1]) - At<float>(self, 0xB8));
                const float dx = PcF(static_cast<double>(place[0]) - At<float>(self, 0xB4));
                float sine, cosine;
                BfSinCos(static_cast<double>(dy) * At<float>(d, 0x104), &sine, &cosine);
                place[0] = PcF((static_cast<double>(dx) * cosine - static_cast<double>(dz) * sine) +
                               At<float>(self, 0xB4));
                place[2] = PcF((static_cast<double>(dx) * sine + static_cast<double>(dz) * cosine) +
                               At<float>(self, 0xBC));
            }
            if (!(static_cast<double>(turn) == M<double>(0x00A80990))) {
                float sine, cosine;
                BfSinCos(static_cast<double>(turn) * M<float>(0x00AA0B2C), &sine, &cosine);
                for (uint32_t i = 0; i < 3; ++i) {
                    const float g = second[i], f = first[i];
                    first[i] = PcF(static_cast<double>(PcF(static_cast<double>(f) * cosine)) -
                                   PcF(static_cast<double>(g) * sine));
                    second[i] = PcF(static_cast<double>(PcF(static_cast<double>(g) * cosine)) +
                                    PcF(static_cast<double>(f) * sine));
                }
            }
            BfWiden(self, place);
            const uint32_t colour = EcColourByte(blue) | EcColourByte(green) << 8 | EcColourByte(red) << 16 |
                                    static_cast<uint32_t>(EcColourByte(opacity)) << 24;
            const auto vertex = [&](const float* at, float u, float v) {
                float* const to = reinterpret_cast<float*>(out);
                to[0] = at[0], to[1] = at[1], to[2] = at[2];
                std::memcpy(to + 3, &colour, 4);
                to[4] = u, to[5] = v;
                out += 0x20;
            };
            float corner[3], side[3];
            for (uint32_t i = 0; i < 3; ++i) {
                corner[i] = PcF(static_cast<double>(first[i]) + place[i]);
                side[i] = PcF(static_cast<double>(second[i]) + place[i]);
            }
            vertex(corner, piece[13], piece[14]);
            vertex(place, piece[9], piece[10]);
            vertex(side, piece[11], piece[12]);
            if (quad) {
                float last[3];
                for (uint32_t i = 0; i < 3; ++i)
                    last[i] = PcF(static_cast<double>(first[i]) + side[i]);
                const float u = PcF(static_cast<double>(piece[11]) + piece[13]);
                const float v = PcF(static_cast<double>(piece[12]) + piece[14]);
                vertex(last, PcF(static_cast<double>(u) - piece[9]), PcF(static_cast<double>(v) - piece[10]));
            }
        }
        BfUnlock(At<char*>(self, 0x1EC));
        char* const buffer = At<char*>(self, 0x1EC);
        char* const batch = At<char*>(self, 0x230);
        const uint32_t indices = At<uint32_t>(batch, 0x2C), count = At<uint32_t>(batch, 0x38);
        const uint32_t noted[4] = {0, 0, count, indices};
        BOurs(kBfBuffer + 0x28, Word(buffer), Word(noted), sizeof noted, [&] {
            return CallThis<uint32_t>(At<uint32_t>(At<char*>(buffer, 0), 0x28), buffer, 0u, 0u, count, indices);
        });
    }
}

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

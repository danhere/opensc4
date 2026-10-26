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

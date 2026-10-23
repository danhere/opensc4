// The second effect class's model, written by hand from the game's code:
// its slot 0x54, a model loaded by its instance ID (0x005AC610), and its
// slot 0x28, an effect given its description (0x005AEC90). Included by
// water_effects.cpp, which has the Bd calls (noted in a replay).

// The part of a model that counts its holders: the model's virtual base,
// found through the table at its +4.
char* BdCounted(char* model)
{
    return model + 4 + At<int32_t>(At<char*>(model, 4), 4);
}

// 0x005AC610 (ret 4), slot 0x54 of the second class, with the model's
// instance ID; nothing for 0. The resource manager (0x00B43CA8, slot 0xC)
// is asked for the resource of type 0x5AD0E817, group 0xBADB57F1 and that
// instance by the interface 0x9AD0E7E3. With it: the old model (+0x224)
// released through its counting part and a new one made by the
// resource's slot 0x10; its counting part, when it is not the instance
// already held (+0x220), held (slot 4) and kept there, the old one
// released. The model's slot 0x40 with 3; the instance's box (slot 0x1C,
// given 1, 1, 1, 0, 0, 0) and the largest of its three sides at +0x274
// (the first two rounded to float, the third compared as the x87 has it).
// Then each of the instance's materials (the interface 0x26A68448: a
// count at slot 0xC, asked again before each, each at slot 0x18) has the
// bit 8 of its flags cleared for flag 0x400 of the description's third
// word and set otherwise, and by the description's kind (+0xD4, the jump
// table at 0x005AC870): kind 0 clears 0x80, kind 2 clears 0x82, kinds 5
// and 6 the same in a word with 0x10 set and the bytes +9 and +0xA set
// to 1. The resource is released at the end, found or not.
void LoadEffectModelB(char* self, uint32_t which)
{
    if (!which)
        return;
    char* resource = nullptr;
    const uint32_t key[3] = {0x5AD0E817, 0xBADB57F1, which};
    if (SuSlot<uint8_t>(M<char*>(addr::SC4_ResourceManager), 0xC, key, 0x9AD0E7E3u, &resource, 0u, 0u)) {
        if (char* const old = At<char*>(self, 0x224)) {
            At<char*>(self, 0x224) = nullptr;
            SuSlot<uint32_t>(BdCounted(old), 8);
        }
        SuSlot<uint32_t>(resource, 0x10, 0u, self + 0x224);
        char* const model = At<char*>(self, 0x224);
        char* const counted = model ? BdCounted(model) : nullptr;
        char* const held = At<char*>(self, 0x220);
        if (counted != held) {
            if (counted)
                SuSlot<uint32_t>(counted, 4);
            At<char*>(self, 0x220) = counted;
            if (held)
                SuSlot<uint32_t>(held, 8);
        }
        SuSlot<uint32_t>(At<char*>(self, 0x224), 0x40, 3u);
        float box[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
        SuSlot<uint32_t>(At<char*>(self, 0x220), 0x1C, box);
        const float a = static_cast<float>(static_cast<double>(box[3]) - box[0]);
        const float b = static_cast<float>(static_cast<double>(box[4]) - box[1]);
        const double c = static_cast<double>(box[5]) - box[2];
        double largest = c;
        if (a > b) {
            if (a > c)
                largest = a;
        } else if (b > c) {
            largest = b;
        }
        At<float>(self, 0x274) = static_cast<float>(largest);
        char* materials = nullptr;
        SuSlot<uint32_t>(At<char*>(self, 0x220), 0, 0x26A68448u, &materials);
        if (materials) {
            SuSlot<uint32_t>(materials, 4);
            SuSlot<uint32_t>(materials, 8);
            if (SuSlot<uint32_t>(materials, 0xC) != 0 && SuSlot<uint32_t>(materials, 0xC) != 0) {
                uint32_t i = 0;
                do {
                    char* const material = reinterpret_cast<char*>(SuSlot<uint32_t>(materials, 0x18, i));
                    if (At<uint32_t>(At<char*>(self, 0x24), 8) >> 10 & 1)
                        At<uint8_t>(material, 4) &= 0xF7;
                    else
                        At<uint8_t>(material, 4) |= 0x08;
                    switch (At<uint8_t>(At<char*>(self, 0x24), 0xD4)) {
                    case 0:
                        At<uint8_t>(material, 4) &= 0x7F;
                        break;
                    case 2:
                        At<uint8_t>(material, 4) &= 0x7D;
                        break;
                    case 5:
                        At<uint16_t>(material, 4) = static_cast<uint16_t>((At<uint16_t>(material, 4) & 0xFF7F) | 0x10);
                        At<uint8_t>(material, 9) = 1;
                        At<uint8_t>(material, 0xA) = 1;
                        break;
                    case 6:
                        At<uint16_t>(material, 4) = static_cast<uint16_t>((At<uint16_t>(material, 4) & 0xFF7D) | 0x10);
                        At<uint8_t>(material, 9) = 1;
                        At<uint8_t>(material, 0xA) = 1;
                        break;
                    default:
                        break;
                    }
                    ++i;
                } while (i < SuSlot<uint32_t>(materials, 0xC));
            }
            SuSlot<uint32_t>(materials, 8);
        }
    }
    if (resource)
        SuSlot<uint32_t>(resource, 8);
}

// 0x005AEC90 (ret 4), slot 0x28 of the second class: nothing for a
// description without bit 0 of its third word of flags (+8). Else the base
// class's (0x005B7C90); the instance (+0x220) and the model (+0x224, by its
// counting part) released, +0x274 one, the object at +0x278 ended (slot
// 0x20), each field cleared first; the part at +0x228 attached to nothing
// (0x008038D0 with 0), +0x26C, +0x26F and +0x270 cleared. A model is loaded
// (slot 0x54) by the description's +0xD0, or, when it has a list of models
// (+0x1E0 to +0x1E4), by one of those picked with the generator at
// 0x00B22DF8 (0x009136B1); nothing is loaded for an ID of 0 or -1 without
// a list. With an instance: for flag 1 of the first word it is given a
// tint block (0x0048EEF0) with bit 2 of its first byte set (its slot
// 0x40); for flag 0x100 or 4 of the third word the part is set up: given
// the description for 0x100 (0x005AC110), its +8 set to 10 (0x00989A60),
// attached to the instance (the answer at +0x26C) and, when that worked,
// placed at (0, 0, -1) (0x00802DB0). Last the effects manager is asked
// whether it has effects named "%s_slave" (0x00AA0CD0, +0x26D) and
// "%s_death" (0x00AA0CC4, +0x26E) after this one.
void SetEffectDescriptionB(char* self, char* description)
{
    if (!(At<uint8_t>(description, 8) & 1))
        return;
    BdBase(self, description);
    if (char* const instance = At<char*>(self, 0x220)) {
        At<char*>(self, 0x220) = nullptr;
        BdSlot0(instance, 8);
    }
    if (char* const model = At<char*>(self, 0x224)) {
        At<char*>(self, 0x224) = nullptr;
        BdSlot0(BdCounted(model), 8);
    }
    At<float>(self, 0x274) = 1.0f;
    if (char* const object = At<char*>(self, 0x278)) {
        At<char*>(self, 0x278) = nullptr;
        BdSlot0(object, 0x20);
    }
    char* const part = self + 0x228;
    BdAttach(part, nullptr);
    At<uint8_t>(self, 0x26C) = 0;
    At<uint8_t>(self, 0x26F) = 0;
    At<uint32_t>(self, 0x270) = 0;
    uint32_t which = At<uint32_t>(description, 0xD0);
    const uint32_t* const first = At<uint32_t*>(description, 0x1E0);
    const uint32_t* const last = At<uint32_t*>(description, 0x1E4);
    if (!((which == 0 || which == 0xFFFFFFFF) && first == last)) {
        if (first != last) {
            const int32_t count = (reinterpret_cast<int32_t>(last) - reinterpret_cast<int32_t>(first)) >> 2;
            const uint32_t pick =
                CallThis<uint32_t>(addr::SC4Random_BelowLcg, reinterpret_cast<char*>(addr::SC4Effect_Random), static_cast<uint32_t>(count));
            which = At<uint32_t*>(description, 0x1E0)[pick];
        }
        BdLoadModel(self, which);
        if (At<char*>(self, 0x220)) {
            if (At<uint8_t>(At<char*>(self, 0x24), 0) & 1) {
                alignas(4) char tint[0x14];
                CallThis<uint32_t>(addr::SC4Tint_Construct, tint);
                tint[0] |= 4;
                BdTint(At<char*>(self, 0x220), tint);
            }
            char* const d = At<char*>(self, 0x24);
            const uint32_t flags = At<uint32_t>(d, 8);
            const bool named = flags >> 8 & 1;
            if (named || flags >> 2 & 1) {
                if (named)
                    BdPartDescription(part, d);
                CallThis<uint32_t>(addr::SC4EffectPartB_SetWord8, part, 10u);
                const uint8_t attached = BdAttach(part, At<char*>(self, 0x220));
                At<uint8_t>(self, 0x26C) = attached;
                if (attached) {
                    const float place[3] = {0.0f, 0.0f, -1.0f};
                    BdPlace(part, place);
                }
            }
        }
    }
    char* const manager = M<char*>(addr::SC4_Service43D1C);
    At<uint8_t>(self, 0x26D) = SuHasNamed(manager, At<char*>(self, 0x24), addr::SC4EffectB_SlaveNameFormat);
    At<uint8_t>(self, 0x26E) = SuHasNamed(manager, At<char*>(self, 0x24), addr::SC4EffectB_DeathNameFormat);
}

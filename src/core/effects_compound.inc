// The start of a compound effect, written by hand from the game's code:
// SC4EffectsManager::StartCompound (0x005919D0) and four small functions it
// calls, SC4EffectRecord::Construct (0x0058E3E0), ::CopyConstruct
// (0x0058E020), SC4EffectsManager::FadeByLevel (0x0058DC50) and
// SC4CompoundEffect::ApplyValues (0x005BFCD0). Included by water_effects.cpp,
// which has the calls that the comparison notes (the Sc functions).
// Offsets are from the manager object, 4 below the interface the 3D render
// holds.
//
// A compound effect (an entry of the manager's table at +0xB94) has a
// vector of 0x44-byte records at +0x114, one for each part of its
// description that runs. A record: a count of its changes (+0), a byte from
// the part (+1), thirteen floats (+4: nine of a matrix, three of a place,
// a scale), the effect it holds (+0x38), which part it is (+0x3C) and a
// word at +0x40. The description (0x38 bytes, the manager's table at +0xAC,
// the compound effect's index at +0x20) has a vector of 0x6C-byte parts at
// +8. A part runs between two levels of detail (the bytes +0x4C and +0x4D)
// the manager's level (+0xC84; the one before at +0xC88) must be within.

// A new record (0x0058E3E0): no changes, the matrix one, the place nothing,
// the scale 1, no effect. The bytes +2 and +3 and the part at +0x3C are
// left as they are.
char* ConstructEffectRecord(char* record)
{
    At<uint8_t>(record, 0) = 0;
    At<uint8_t>(record, 1) = 0;
    for (std::size_t i = 0; i < 12; ++i)
        At<float>(record, 4 + i * 4) = i % 4 == 0 && i < 9 ? 1.0f : 0.0f;
    At<float>(record, 0x34) = 1.0f;
    At<uint32_t>(record, 0x38) = 0;
    At<uint32_t>(record, 0x40) = 0;
    return record;
}

// A record made as a copy of another (0x0058E020): no changes, the rest
// copied, the effect held once more (its slot 0x1C).
char* CopyConstructEffectRecord(char* record, char* from)
{
    At<uint8_t>(record, 0) = 0;
    std::memcpy(record + 4, from + 4, 0x34);
    At<uint8_t>(record, 1) = At<uint8_t>(from, 1);
    char* const held = At<char*>(from, 0x38);
    At<char*>(record, 0x38) = held;
    if (held)
        CallThis<void>(At<uint32_t>(At<char*>(held, 0), 0x1C), held);
    At<uint32_t>(record, 0x3C) = At<uint32_t>(from, 0x3C);
    At<uint32_t>(record, 0x40) = At<uint32_t>(from, 0x40);
    return record;
}

// A record's effect told two values for the level of detail (0x0058DC50):
// each goes from the part's first (+0x54, +0x5C) to its second (+0x58,
// +0x60) by how far the level is into the part's levels, a whole number
// (the division is of integers); a part of one level gives the first. The
// effect's slot 0x14 gets them as the two values of 0x102.
void FadeEffectByLevel(char* self, char* part, char* record)
{
    const int32_t low = At<uint8_t>(part, 0x4C), high = At<uint8_t>(part, 0x4D);
    float values[2];
    if (low < high - 1) {
        const float along = static_cast<float>((At<int32_t>(self, 0xC84) - low) / (high - low - 1));
        values[0] = static_cast<float>(
            At<float>(part, 0x54) + (static_cast<double>(At<float>(part, 0x58)) - At<float>(part, 0x54)) * along);
        values[1] = static_cast<float>(
            At<float>(part, 0x5C) + (static_cast<double>(At<float>(part, 0x60)) - At<float>(part, 0x5C)) * along);
    } else {
        values[0] = At<float>(part, 0x54);
        values[1] = At<float>(part, 0x5C);
    }
    char* const held = At<char*>(record, 0x38);
    CallThis<void>(At<uint32_t>(At<char*>(held, 0), 0x14), held, 0x102u, values, 2u);
}

// A compound effect's own values given to each record's effect
// (0x005BFCD0): the vector at +0x12C has for each kind of value two words,
// where its values start in the table at +0x138 and how many there are;
// each kind with some goes to the effect's slot 0x14.
void ApplyCompoundValues(char* compound)
{
    char* record = At<char*>(compound, 0x114);
    if (record == At<char*>(compound, 0x118))
        return;
    do {
        for (uint32_t kind = 0;
             kind < static_cast<uint32_t>((At<char*>(compound, 0x130) - At<char*>(compound, 0x12C)) >> 2); ++kind) {
            const uint16_t* const entry = At<uint16_t*>(compound, 0x12C) + kind * 2;
            if (entry[1] == 0)
                continue;
            char* const held = At<char*>(record, 0x38);
            CallThis<void>(At<uint32_t>(At<char*>(held, 0), 0x14), held, kind, At<char*>(compound, 0x138) + entry[0] * 4,
                           static_cast<uint32_t>(entry[1]));
        }
        record += 0x44;
    } while (record != At<char*>(compound, 0x118));
}

// The count of 0x44-byte records, or of 0x6C-byte parts, between two places.
int32_t CountBetween(const char* begin, const char* end, int32_t size)
{
    return static_cast<int32_t>(end - begin) / size;
}

// The start (0x005919D0; `first` is the caller's flag, 1 from the start of
// the waiting effects).
//
// First the records the compound effect already has. One whose part is no
// longer within the level (or any, when the manager's byte +0xB8C is set)
// has its effect stopped (slot 4 with 1) and let go (slot 0x20); the
// others are kept, moved down over the gaps (0x0058DBA0), and told the
// level's values again when the level changed. A record of no part (-1) is
// kept as it is. The records past the kept ones are erased.
//
// With +0xB8C set the description is looked up again, by the compound
// effect's name (+0xC, 0x0058F5A0) or by its two IDs (+0x18 and +0x1C,
// 0x0058F2E0). Nothing more for one without a description.
//
// Then each part of the description that has an effect (+0x68 not below
// 0) and is new to the level (within it now and not before), or any when
// `first` or +0xB8C is set. Parts of one group (the word +0x64, not 0) are
// alternatives: one draw below 0x10000 from the manager's generator
// (+0x1C0) for the group, and each part's weight (+0x66) added up until
// the sum reaches the draw, which that part takes; the rest of the group
// then has nothing to reach. A part taken and within the level gets a
// record at the end of the vector: its effect made (0x00591730), the
// level's values, the part's thirteen floats and its byte +0x15, and the
// effect its transform (slot 0xC, with the compound effect's block at
// +0xA4): the compound effect's own (the floats at +0xE0, the byte +0xDD)
// for a part without +0x15, the part's for a compound effect without
// +0xDD, else the two put together. The effect of a compound effect that
// runs (+0x24 set, +0x25 clear) is started (slot 0 with 0 when `first`,
// else 1).
//
// Last the compound effect's own values go to every record's effect, when
// it has some (the vector at +0x12C).
void StartCompoundEffect(char* self, char* compound, uint8_t first)
{
    char* const records = compound + 0x114;
    if (At<char*>(records, 0) != At<char*>(records, 4)) {
        char* const description = At<char*>(self, 0xAC) + At<int32_t>(compound, 0x20) * 0x38;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(CountBetween(At<char*>(records, 0), At<char*>(records, 4), 0x44));
             ++i) {
            char* const record = At<char*>(records, 0) + i * 0x44;
            const int32_t which = At<int32_t>(record, 0x3C);
            if (which != -1) {
                char* const part = At<char*>(description, 8) + which * 0x6C;
                const int32_t level = At<int32_t>(self, 0xC84);
                if (At<uint8_t>(self, 0xB8C) || At<uint8_t>(part, 0x4C) > level || level >= At<uint8_t>(part, 0x4D)) {
                    ScStop(At<char*>(record, 0x38));
                    if (char* const held = At<char*>(record, 0x38)) {
                        At<char*>(record, 0x38) = nullptr;
                        ScEnd(held);
                    }
                    continue;
                }
                if (level != At<int32_t>(self, 0xC88))
                    ScFade(self, part, record);
            }
            if (i != kept)
                ScAssign(At<char*>(records, 0) + kept * 0x44, record);
            ++kept;
        }
        ScErase(records, At<char*>(records, 0) + kept * 0x44, At<char*>(compound, 0x118));
    }
    if (At<uint8_t>(self, 0xB8C)) {
        At<uint32_t>(compound, 0x20) =
            At<char*>(compound, 0xC) != At<char*>(compound, 0x10)
                ? CallThis<uint32_t>(kFindDescriptionByName, self, compound + 0xC)
                : CallThis<uint32_t>(kFindDescriptionByID, self, At<uint32_t>(compound, 0x18), At<uint32_t>(compound, 0x1C));
    }
    if (At<int32_t>(compound, 0x20) < 0)
        return;
    char* const description = At<char*>(self, 0xAC) + At<int32_t>(compound, 0x20) * 0x38;
    uint32_t group = 0, made = 0, draw = 0, sum = 0;
    for (uint32_t i = 0;
         i < static_cast<uint32_t>(CountBetween(At<char*>(description, 8), At<char*>(description, 0xC), 0x6C)); ++i) {
        char* const part = At<char*>(description, 8) + i * 0x6C;
        const int32_t low = At<uint8_t>(part, 0x4C), high = At<uint8_t>(part, 0x4D);
        const int32_t level = At<int32_t>(self, 0xC84), before = At<int32_t>(self, 0xC88);
        uint8_t now = low <= level && level < high;
        const bool was = low <= before && before < high;
        if (At<int32_t>(part, 0x68) < 0)
            continue;
        if ((was || !now) && !first && !At<uint8_t>(self, 0xB8C))
            continue;
        if (At<uint16_t>(part, 0x64) != 0) {
            if (At<uint16_t>(part, 0x64) != group) {
                draw = CallThis<uint32_t>(addr::SC4Random_Below, self + 0x1C0, 0x10000u);
                sum = 0;
            }
            group = At<uint16_t>(part, 0x64);
            sum += At<uint16_t>(part, 0x66);
            if (sum < draw)
                continue;
            draw = 0x10000;
        } else {
            group = 0;
        }
        if (!now)
            continue;
        {
            char fresh[0x44];
            ConstructEffectRecord(fresh);
            if (At<char*>(records, 4) != At<char*>(records, 8)) {
                if (At<char*>(records, 4))
                    CopyConstructEffectRecord(At<char*>(records, 4), fresh);
                At<char*>(records, 4) += 0x44;
            } else {
                ScGrow(records, At<char*>(records, 4), fresh, &now);
            }
            if (char* const held = At<char*>(fresh, 0x38))
                ScEnd(held);
        }
        char* const record = At<char*>(records, 4) - 0x44;
        At<uint32_t>(record, 0x3C) = i;
        ScMake(self, part, record, &made);
        ScFade(self, part, record);
        ++At<uint8_t>(record, 0);
        std::memcpy(record + 4, part + 0x18, 0x34);
        At<uint8_t>(record, 1) = At<uint8_t>(part, 0x15);

        // Two bytes and thirteen floats, as an effect's transform takes them.
        struct Block {
            uint8_t zero, flag, unset[2];
            float value[13];
        } block{};
        char* const held = At<char*>(record, 0x38);
        const float* const mine = &At<float>(compound, 0xE0);
        const float* const its = &At<float>(part, 0x18);
        const uint8_t partFlag = At<uint8_t>(part, 0x15);
        block.flag = At<uint8_t>(compound, 0xDD);
        if (!partFlag) {
            std::memcpy(block.value, mine, sizeof block.value);
        } else if (!block.flag) {
            std::memcpy(block.value, its, sizeof block.value);
            block.flag = partFlag;
        } else {
            // The part's place turned by the compound effect's matrix,
            // scaled and added to its place; the two matrices multiplied;
            // the scales multiplied. Sums are kept in double until stored,
            // as the game's are on the x87 stack.
            const auto dot = [](float a, float b, float c, float d, float e, float f) {
                return static_cast<double>(a) * b + static_cast<double>(c) * d + static_cast<double>(e) * f;
            };
            float* const out = block.value;
            const float scale = mine[12];
            const float x = static_cast<float>(dot(mine[1], its[10], mine[0], its[9], mine[2], its[11]) * scale);
            const float y = static_cast<float>(dot(mine[4], its[10], mine[3], its[9], mine[5], its[11]) * scale);
            const float z = static_cast<float>(dot(mine[7], its[10], mine[6], its[9], mine[8], its[11]));
            out[11] = static_cast<float>(static_cast<double>(z) * scale + mine[11]);
            out[9] = static_cast<float>(static_cast<double>(x) + mine[9]);
            out[10] = static_cast<float>(static_cast<double>(y) + mine[10]);
            out[6] = static_cast<float>(dot(mine[6], its[0], mine[8], its[6], mine[7], its[3]));
            out[7] = static_cast<float>(dot(mine[7], its[4], mine[8], its[7], mine[6], its[1]));
            out[8] = static_cast<float>(dot(mine[6], its[2], mine[7], its[5], mine[8], its[8]));
            out[3] = static_cast<float>(dot(mine[3], its[0], mine[5], its[6], mine[4], its[3]));
            out[4] = static_cast<float>(dot(mine[4], its[4], mine[5], its[7], mine[3], its[1]));
            out[5] = static_cast<float>(dot(mine[3], its[2], mine[4], its[5], mine[5], its[8]));
            out[0] = static_cast<float>(dot(mine[0], its[0], mine[2], its[6], mine[1], its[3]));
            out[1] = static_cast<float>(dot(mine[1], its[4], mine[2], its[7], mine[0], its[1]));
            out[2] = static_cast<float>(dot(mine[0], its[2], mine[1], its[5], mine[2], its[8]));
            out[12] = static_cast<float>(static_cast<double>(scale) * its[12]);
            block.flag |= partFlag;
        }
        ScTransform(held, reinterpret_cast<char*>(&block), compound + 0xA4);
        if (At<uint8_t>(compound, 0x24) && !At<uint8_t>(compound, 0x25))
            ScBegin(At<char*>(record, 0x38), first ? 0u : 1u);
    }
    if (At<char*>(compound, 0x12C) != At<char*>(compound, 0x130))
        ScValues(compound);
}

// A compound effect's records: the assign of one record to another and the
// vector's growth, written by hand from the game's code:
// SC4EffectRecord::Assign (0x0058DBA0), SC4EffectRecords::GrowInsert
// (0x005905C0) and SC4EffectRecords::Destroy (0x00590040) under it; the
// range copy the growth calls for an insert short of the end
// (SC4EffectRecords_CopyRange, 0x0058EF60) stays the game's, as no test
// city makes one. Included by
// water_effects.cpp, which has the calls that the comparisons note (the
// Gr functions). A record is 0x44 bytes (effects_compound.inc).

// A record given another's values (0x0058DBA0): its count of changes one
// more, the thirteen floats and the byte +1 copied, the effect taken over
// (the new one held, slot 0x1C, the old one let go, slot 0x20) unless it
// is the same, the part (+0x3C) and +0x40 copied. Answers the record.
char* AssignEffectRecord(char* to, char* from)
{
    ++At<uint8_t>(to, 0);
    std::memcpy(to + 4, from + 4, 0x34);
    At<uint8_t>(to, 1) = At<uint8_t>(from, 1);
    if (from != to) {
        char* const next = At<char*>(from, 0x38);
        char* const old = At<char*>(to, 0x38);
        if (next != old) {
            if (next)
                CallThis<void>(At<uint32_t>(At<char*>(next, 0), 0x1C), next);
            At<char*>(to, 0x38) = next;
            if (old)
                CallThis<void>(At<uint32_t>(At<char*>(old, 0), 0x20), old);
        }
    }
    At<uint32_t>(to, 0x3C) = At<uint32_t>(from, 0x3C);
    At<uint32_t>(to, 0x40) = At<uint32_t>(from, 0x40);
    return to;
}

// The vector's records ended (0x00590040): what each holds let go (slot
// 0x20), then the storage freed (0x0090CF63). The vector itself is left
// as it was.
void DestroyEffectRecords(char* vector)
{
    for (char* record = At<char*>(vector, 0); record != At<char*>(vector, 4); record += 0x44) {
        if (char* const held = At<char*>(record, 0x38))
            GrEnd(held);
    }
    if (char* const storage = At<char*>(vector, 0))
        GrFree(storage);
}

// The vector with no room left grown (0x005905C0): new storage for the
// size plus the larger of the size and `count` (0x0090CF54), the records
// before `at` copied over, `count` copies of `record` after them, the
// records from `at` on too unless `atEnd` (0x0058EF60), the old records
// ended and their storage freed.
void GrowInsertEffectRecords(char* vector, char* at, char* record, uint32_t count, bool atEnd)
{
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(vector, 4) - At<char*>(vector, 0)) / 0x44);
    const uint32_t room = size + (size < count ? count : size);
    char* const storage = room ? GrAllocate(room * 0x44) : nullptr;
    char* place = storage;
    for (char* from = At<char*>(vector, 0); from != at; from += 0x44, place += 0x44) {
        if (place)
            GrCopyConstruct(place, from);
    }
    for (uint32_t i = 0; i < count; ++i, place += 0x44) {
        if (place)
            GrCopyConstruct(place, record);
    }
    if (!atEnd)
        place = GrCopyRange(at, At<char*>(vector, 4), place);
    GrDestroy(vector);
    At<char*>(vector, 4) = place;
    At<char*>(vector, 8) = storage + room * 0x44;
    At<char*>(vector, 0) = storage;
}

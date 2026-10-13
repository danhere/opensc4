// The effects manager's clean-up after its draw, written by hand from the
// game's code: SC4EffectsManager::AfterDraw (0x005922A0), ::StartQueued
// (0x005921B0), SC4EffectRecords::Erase (0x0058FEC0) and SC4EffectRecords_Copy
// (0x0058E120). Included by water_effects.cpp, which has the calls that the
// comparisons note (CleanEnded, CleanErase, CleanStartQueued, QueueStart,
// RecordEnd). Offsets are from the manager object, 4 below the interface the
// 3D render holds.

// A vector of words as the game lays it out.
struct WordVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* room;
};

// One more word at the end of a vector. With no room left the storage grows
// by the size, or by one word when empty (the game does this in line for
// four of the lists and through 0x0051CA60 for the other two).
void PushBackWord(WordVector& vector, uint32_t value)
{
    if (vector.end != vector.room) {
        if (vector.end)
            *vector.end = value;
        ++vector.end;
        return;
    }
    const std::size_t size = static_cast<std::size_t>(vector.end - vector.begin);
    const std::size_t room = size + (size < 1 ? 1 : size);
    uint32_t* const grown = reinterpret_cast<uint32_t*(__cdecl*)(std::size_t)>(addr::SC4_stl_allocate)(room * 4);
    if (size)
        std::memmove(grown, vector.begin, size * 4);
    grown[size] = value;
    if (vector.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(vector.begin);
    vector.begin = grown;
    vector.end = grown + size + 1;
    vector.room = grown + room;
}

// The copy of 0x44-byte records down a vector (0x0058E120, cdecl; two more
// arguments are not read): each record assigned (0x0058DBA0) to the next
// place from `to`; answers the place after the last.
char* CopyEffectRecords(char* first, char* last, char* to)
{
    int32_t count = static_cast<int32_t>(last - first) / 0x44;
    if (count <= 0)
        return to;
    do {
        CallThis<void>(kAssignEffectRecord, to, first);
        first += 0x44;
        to += 0x44;
    } while (--count);
    return to;
}

// Records taken out of a vector of them (0x0058FEC0; begin at +0, end at
// +4): those after `last` are copied down to `first`, what the records
// past the new end hold at +0x38 is ended (its slot 0x20), and the end is
// set. Answers `first`.
char* EraseEffectRecords(char* records, char* first, char* last)
{
    char* const kept = CopyEffectRecords(last, At<char*>(records, 4), first);
    char* const end = At<char*>(records, 4);
    for (char* record = kept; record != end; record += 0x44) {
        if (char* const held = At<char*>(record, 0x38))
            RecordEnd(held);
    }
    At<char*>(records, 4) = kept;
    return first;
}

// The effects waiting to start (0x005921B0; the vector at +0xBA0) join the
// compound effects' index vector (+0xBAC, through the game's range insert,
// 0x00414040) and each is started (0x005919D0 with the entry of the table
// at +0xB94 and 1). A start may leave more waiting, so this repeats, ten
// times at most. Nothing while the clean-up runs (the byte at +0x1B4).
void StartQueuedEffects(char* self)
{
    if (At<uint8_t>(self, 0x1B4))
        return;
    WordVector& queued = At<WordVector>(self, 0xBA0);
    WordVector& index = At<WordVector>(self, 0xBAC);
    int rounds = 10;
    while (queued.begin != queued.end) {
        if (rounds-- <= 0)
            break;
        const std::size_t had = static_cast<std::size_t>(index.end - index.begin);
        uint8_t unused = 0;
        CallThis<void>(kInsertWords, &index, index.end, queued.begin, queued.end, &unused);
        queued.end = queued.begin;
        for (uint32_t* it = index.begin + had; it != index.end; ++it)
            QueueStart(self, At<char**>(self, 0xB94)[*it]);
    }
}

// One of the five lists of effects (a table of pointers, the vector of the
// indices in use and the vector of the free ones): an entry that only the
// manager still holds (`held` answers 1) and that is not running (its slot
// 8) has its index moved to the free ones.
template <typename Held>
void FreeEndedEffects(char* self, std::size_t table, Held&& held)
{
    WordVector& index = At<WordVector>(self, table + 0xC);
    WordVector& freed = At<WordVector>(self, table + 0x18);
    uint32_t* kept = index.begin;
    for (uint32_t* it = index.begin; it != index.end; ++it) {
        char* const entry = At<char**>(self, table)[*it];
        if (held(entry) == 1 && !(CallThis<uint32_t>(At<uint32_t>(At<char*>(entry, 0), 0x8), entry) & 0xFF))
            PushBackWord(freed, *it);
        else
            *kept++ = *it;
    }
    index.end = kept;
}

// The clean-up (0x005922A0), the last call of the manager's draw. Each
// compound effect in use (the table at +0xB94, the indices at +0xBAC) is
// asked whether it has ended (0x005BFD70); those that have not are counted
// at +0xC94. One that has, and whose word at +8 is 1, is emptied: its
// records (+0x114) erased, its string (+0xC) cut to nothing, +0x18 and
// +0x1C cleared, +0x20 set to -1, and its index moved to the free ones
// (+0xBB8). Only when that happened to one are the five other lists gone
// through. Last, the effects waiting are started.
void CleanUpEffects(char* self)
{
    At<uint32_t>(self, 0xC94) = 0;
    At<uint8_t>(self, 0x1B4) = 1;
    bool any = false;
    {
        WordVector& index = At<WordVector>(self, 0xBAC);
        WordVector& freed = At<WordVector>(self, 0xBB8);
        uint32_t* kept = index.begin;
        for (uint32_t* it = index.begin; it != index.end; ++it) {
            char* const compound = At<char**>(self, 0xB94)[*it];
            if (!CleanEnded(compound)) {
                ++At<uint32_t>(self, 0xC94);
                *kept++ = *it;
                continue;
            }
            if (At<uint32_t>(compound, 0x8) != 1) {
                *kept++ = *it;
                continue;
            }
            CleanErase(compound + 0x114, At<char*>(compound, 0x114), At<char*>(compound, 0x118));
            if (At<char*>(compound, 0xC) != At<char*>(compound, 0x10)) {
                *At<char*>(compound, 0xC) = 0;
                At<char*>(compound, 0x10) = At<char*>(compound, 0xC);
            }
            At<uint32_t>(compound, 0x18) = 0;
            At<uint32_t>(compound, 0x1C) = 0;
            At<uint32_t>(compound, 0x20) = 0xFFFFFFFFu;
            PushBackWord(freed, *it);
            any = true;
        }
        index.end = kept;
    }
    if (any) {
        const auto holders = [](char* entry) {
            return CallThis<uint32_t>(At<uint32_t>(At<char*>(entry, 4), 0x38), entry + 4);
        };
        FreeEndedEffects(self, 0xBE8, holders);
        FreeEndedEffects(self, 0xC30, holders);
        FreeEndedEffects(self, 0xC54, holders);
        FreeEndedEffects(self, 0xC0C, [](char* entry) {
            return CallThis<uint32_t>(At<uint32_t>(At<char*>(entry, 4), 0x10), entry + 4);
        });
        FreeEndedEffects(self, 0xBC4, [](char* entry) { return At<uint32_t>(entry, 4); });
    }
    At<uint8_t>(self, 0x1B4) = 0;
    CleanStartQueued(self);
}

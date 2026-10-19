// The effects manager's lists of held pointers: the vectors of pointers in
// an effect description that the effects file reader resizes and fills
// (0x005A46E0, 0x005A4820 and 0x005A4940 insert into them in place, with
// a null pointer as the value), their copies, fills, assignments, ends and
// growth, written by hand from the game's code. Included by
// water_effects.cpp, which has the calls the comparison notes (the Pt
// functions): each call the game makes to another function goes through
// one, a part the game has inlined is written out here.
//
// Three kinds of element, the same code but for how one is held and let
// go: objects whose counted base is at +4 (slots 0x10 and 0x14 of their
// table, 0x005BE3E0 and 0x005BE3F0 in every class seen), effect
// descriptions (SC4Effect::Hold 0x005AF940 and
// SC4EffectDescription::Release 0x005B1C80, the count at +0x204) and
// counted objects (slots 8 and 4, the counted base's own AddRef 0x00968B60
// and Release 0x0090D96B). A fourth, the effects manager's own lists of
// held effects (copy 0x0058DFE0, fill 0x0058DFA0, assignment 0x0058F020,
// end 0x0058F940, growth 0x0058FF70; renderer.effects.hashes), holds and
// lets go by slots 0x1C and 0x20, which hand the call to the counted base
// at +4 (0x005A8F80 and 0x005A8F90 in the class seen). A list is begin,
// end, end of storage; a null pointer is neither held nor let go.

// [first, last) copied into raw storage at `place` on (0x00586B70,
// 0x00586BF0, 0x00586C70): each pointer written and held; nothing written
// where the place is null, which still moves on. Answers the place after
// the last.
char* CopyPointers(Held kind, char* first, char* last, char* place)
{
    for (; first != last; first += 4, place += 4) {
        if (!place)
            continue;
        char* const object = At<char*>(first, 0);
        At<char*>(place, 0) = object;
        if (object)
            PtHold(kind, object);
    }
    return place;
}

// `count` copies of the pointer at `value` made in raw storage at `place`
// on (0x00586BB0, 0x00586C30, 0x00586CB0), each held; the pointer is read
// again for each. Answers the place after the last.
char* FillPointers(Held kind, char* place, uint32_t count, char* const* value)
{
    for (; count; --count, place += 4) {
        if (!place)
            continue;
        char* const object = *value;
        At<char*>(place, 0) = object;
        if (object)
            PtHold(kind, object);
    }
    return place;
}

// [first, last) assigned to `to` on (0x00586D50, 0x00586DC0; the erase's
// copy down): a place that is its own source, or that holds the same
// pointer already, is left alone; else the new pointer is held, written,
// and the old one let go. Answers the place after the last (`to` for an
// empty range).
char* AssignPointers(Held kind, char* first, char* last, char* to)
{
    int32_t count = static_cast<int32_t>(last - first) >> 2;
    if (count <= 0)
        return to;
    for (; count; --count, first += 4, to += 4) {
        if (first == to)
            continue;
        char* const object = At<char*>(first, 0);
        char* const old = At<char*>(to, 0);
        if (object == old)
            continue;
        if (object)
            PtHold(kind, object);
        At<char*>(to, 0) = object;
        if (old)
            PtLetGo(kind, old);
    }
    return to;
}

// A list ended (0x00587810, 0x00587850, 0x005877D0): each pointer let go,
// then the storage freed. The list is left as it was.
void DestroyPointers(Held kind, char* list)
{
    char* const end = At<char*>(list, 4);
    for (char* at = At<char*>(list, 0); at != end; at += 4) {
        if (char* const object = At<char*>(at, 0))
            PtLetGo(kind, object);
    }
    if (char* const storage = At<char*>(list, 0))
        StFree(storage);
}

// A list with no room left grown (0x005880C0, 0x00588190, 0x00588260):
// new storage for the size plus the larger of the size and `count`, the
// pointers before `at` copied over, `count` copies of the one at `value`
// after them (one written and held in place, more by the fill), the
// pointers from `at` on too unless `atEnd`, then the old list ended (its
// pointers let go, its storage freed). Same shape as the store's growth
// (effects_store.inc).
void GrowInsertPointers(Held kind, char* list, char* at, char* const* value, uint32_t count, bool atEnd)
{
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(list, 4) - At<char*>(list, 0)) >> 2);
    const uint32_t room = size + (size < count ? count : size);
    char* const storage = room ? StAllocate(room * 4) : nullptr;
    char* place = PtCopyPointers(kind, At<char*>(list, 0), at, storage);
    if (count == 1) {
        if (place) {
            char* const object = *value;
            At<char*>(place, 0) = object;
            if (object)
                PtHold(kind, object);
        }
        place += 4;
    } else {
        place = PtFillPointers(kind, place, count, value);
    }
    if (!atEnd)
        place = PtCopyPointers(kind, at, At<char*>(list, 4), place);
    PtDestroyPointers(kind, list);
    At<char*>(list, 0) = storage;
    At<char*>(list, 4) = place;
    At<char*>(list, 8) = storage + room * 4;
}

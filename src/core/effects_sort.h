// The sort of the effects in view by depth, written by hand from the game's
// code: the six functions the effects manager's draw sorts its vector of
// {depth, effect} with (0x005906C0, 0x0058F090, 0x0058F980, 0x0058F4B0,
// 0x0058D900, 0x0058E170; all cdecl, each with an unused last argument).
// Included by water_effects.cpp.

// An entry of the vector: the depth is a float, kept here as its bits so
// that an entry moves as the game moves it, by two words.
struct DepthEntry {
    uint32_t depth, effect;
};

// The game compares two depths on the x87 stack and goes by "below": false
// for equal ones and for a depth that is no number.
bool DepthBelow(const DepthEntry& a, const DepthEntry& b)
{
    float x, y;
    std::memcpy(&x, &a.depth, 4);
    std::memcpy(&y, &b.depth, 4);
    return x < y;
}

// 0x0058D900 (at, value): the value put in its place among the sorted
// entries below `at`, those above it moved up one; it trusts that one of
// them is not above the value.
void DepthInsertBelow(DepthEntry* at, DepthEntry value)
{
    for (DepthEntry* below = at - 1; DepthBelow(value, *below); --below) {
        *at = *below;
        at = below;
    }
    *at = value;
}

// 0x0058F4B0 (first, at, value): a value below the first entry goes to the
// front, every entry from there to `at` moved up one; another through
// DepthInsertBelow.
void DepthInsert(DepthEntry* first, DepthEntry* at, DepthEntry value)
{
    if (!DepthBelow(value, *first)) {
        DepthInsertBelow(at, value);
        return;
    }
    for (int32_t count = static_cast<int32_t>(at - first); count > 0; --count, --at)
        *at = at[-1];
    *first = value;
}

// 0x0058F980 (first, last): the insertion sort of a range.
void DepthInsertionSort(DepthEntry* first, DepthEntry* last)
{
    if (first == last)
        return;
    for (DepthEntry* at = first + 1; at != last; ++at)
        DepthInsert(first, at, *at);
}

// 0x0058E170 (first, last): the insertion sort of a range that follows
// sorted entries none of which is above any of its own.
void DepthInsertionSortAfter(DepthEntry* first, DepthEntry* last)
{
    for (; first != last; ++first) {
        const DepthEntry value = *first;
        DepthEntry* at = first;
        for (DepthEntry* below = first - 1; DepthBelow(value, *below); --below) {
            *at = *below;
            at = below;
        }
        *at = value;
    }
}

// 0x0058F090 (first, last, pivot): the entries below the pivot to the
// front; answers where the others start.
DepthEntry* DepthPartition(DepthEntry* first, DepthEntry* last, DepthEntry pivot)
{
    for (;;) {
        while (DepthBelow(*first, pivot))
            ++first;
        do
            --last;
        while (DepthBelow(pivot, *last));
        if (first >= last)
            return first;
        const DepthEntry swapped = *first;
        *first = *last;
        *last = swapped;
        ++first;
    }
}

// 0x005906C0 (first, last, unused, depth): while the range has more than
// sixteen entries it is split about the median of its first, middle and
// last, the upper part sorted by a call of this and the lower part gone on
// with; the smaller ranges are left as they are for the insertion sorts.
// With the depth used up the range goes to the game's heap sort
// (0x0058F9D0), which no test reaches.
void DepthSortLoop(DepthEntry* first, DepthEntry* last, uint32_t depth, uint32_t unused)
{
    while (((reinterpret_cast<char*>(last) - reinterpret_cast<char*>(first)) & ~7) > 0x80) {
        if (depth == 0) {
            reinterpret_cast<void(__cdecl*)(DepthEntry*, DepthEntry*, DepthEntry*, uint32_t, uint32_t)>(kDepthHeapSort)(
                first, last, last, 0, unused);
            return;
        }
        --depth;
        DepthEntry* const middle = first + (last - first) / 2;
        DepthEntry* const back = last - 1;
        DepthEntry* pick;
        if (DepthBelow(*first, *middle))
            pick = DepthBelow(*middle, *back) ? middle : DepthBelow(*first, *back) ? back : first;
        else
            pick = DepthBelow(*first, *back) ? first : DepthBelow(*middle, *back) ? back : middle;
        DepthEntry* const cut = DepthPartition(first, last, *pick);
        DepthSortLoop(cut, last, depth, unused);
        last = cut;
    }
}

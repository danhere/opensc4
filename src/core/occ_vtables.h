// Copies of a game class's vtables, for objects of our own (the network
// occupant classes' `simulators.<key>.own` groups; docs/netocc.md, "Our own
// object"). A class with such a group stores the addresses of its copies in
// the objects it constructs instead of the game's tables. A copy starts as
// the game's table word for word, so every call lands where it did; the
// class then points the entries it owns at its own functions.
//
// Header only, and nothing of the game or of Windows in it, so
// tests/occ_vtables_test.cpp can use it on tables of its own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::occ_vtables {

// Every copy built in this run with the table it was taken from, for
// IsTable. The network occupant classes have 57 tables between them and the
// two network managers three each; a copy past the limit is still made but
// IsTable would not know it, so the limit is kept well above the count.
constexpr std::size_t kMaxCopies = 128;
struct CopyRecord {
    std::uintptr_t source;
    std::uintptr_t copy;
};
inline CopyRecord g_copies[kMaxCopies] = {};
inline std::size_t g_copyCount = 0;

// A copy of a vtable of N slots. The word before the game's table is copied
// too (in build 641 it is the last slot of the table before, padding or
// unrelated data, never a type descriptor: the game has none for these
// classes), so whatever reads slot -1 sees what the game's table has.
template <std::size_t N>
class VtableCopy {
public:
    // Takes the copy: the N slots at `gameTable` and the word before them.
    void Build(const void* gameTable)
    {
        std::memcpy(words_, static_cast<const std::uintptr_t*>(gameTable) - 1, sizeof(words_));
        if (!built_ && g_copyCount < kMaxCopies)
            g_copies[g_copyCount++] = {reinterpret_cast<std::uintptr_t>(gameTable), Address()};
        built_ = true;
    }

    bool Built() const { return built_; }

    // Slot 0 of the copy: what an object's vtable pointer is set to.
    void** Table() { return reinterpret_cast<void**>(words_ + 1); }
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(words_ + 1); }

    // Points every slot that holds `from` at `to`; the result is the number
    // of slots changed. The word before the table is left alone.
    unsigned Redirect(const void* from, void* to)
    {
        unsigned changed = 0;
        for (std::size_t i = 1; i <= N; ++i)
            if (words_[i] == reinterpret_cast<std::uintptr_t>(from)) {
                words_[i] = reinterpret_cast<std::uintptr_t>(to);
                ++changed;
            }
        return changed;
    }

private:
    std::uintptr_t words_[N + 1] = {};
    bool built_ = false;
};

// Redirect on each of `copies`; the result is the total.
template <typename... Copies>
unsigned RedirectAll(const void* from, void* to, Copies&... copies)
{
    return (0u + ... + copies.Redirect(from, to));
}

// Whether `pointer` (a vtable pointer read from an object) is the game's
// table at `gameTable` or a copy of it built in this run: what a test asks
// instead of comparing the pointer with the game's address.
inline bool IsTable(std::uintptr_t pointer, std::uintptr_t gameTable)
{
    if (pointer == gameTable)
        return true;
    for (std::size_t i = 0; i < g_copyCount; ++i)
        if (g_copies[i].copy == pointer)
            return g_copies[i].source == gameTable;
    return false;
}

} // namespace opensc4::occ_vtables

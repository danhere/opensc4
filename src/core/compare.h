// Compare mode (docs/architecture.md, "Replacement registry"): run the original
// and our implementation on the same inputs, log any divergence, and return
// the original's result, so the game's behaviour is unchanged. Only for pure
// functions (no side effects, result depends only on the arguments).
//
//   int __fastcall Detour(Foo* self, void*, int x) {
//       return compare::Call("geometry.clamp", [&] { return g_original(self, nullptr, x); },
//                            [&] { return OurClamp(x); });
//   }
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

namespace opensc4::compare {

// Records one comparison; logs the first few divergences per id.
void Record(const char* id, bool same, const std::string& original, const std::string& ours);

// Logs calls and divergences per id.
void LogStats();

// Counts for an id (for tests).
struct Stats {
    std::uint64_t calls = 0;
    std::uint64_t divergences = 0;
};
Stats StatsOf(const char* id);

template <typename T>
std::string Describe(const T& value)
{
    char buffer[64];
    if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
        return buffer;
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        return buffer;
    } else if constexpr (std::is_pointer_v<T>) {
        std::snprintf(buffer, sizeof(buffer), "%p", static_cast<const void*>(value));
        return buffer;
    } else {
        return "<value>";
    }
}

// Calls `original` and `ours`, records whether their results are equal, and
// returns the original's result.
template <typename Original, typename Ours>
auto Call(const char* id, Original&& original, Ours&& ours)
{
    auto expected = original();
    auto actual = ours();
    const bool same = expected == actual;
    Record(id, same, same ? std::string() : Describe(expected), same ? std::string() : Describe(actual));
    return expected;
}

} // namespace opensc4::compare

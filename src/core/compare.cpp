#include "core/compare.h"

#include "core/log.h"

#include <map>
#include <mutex>

namespace opensc4::compare {
namespace {

constexpr std::uint64_t kLoggedDivergences = 5;

std::mutex g_mutex;
std::map<std::string, Stats> g_stats;

} // namespace

void Record(const char* id, bool same, const std::string& original, const std::string& ours)
{
    std::uint64_t divergences;
    {
        std::lock_guard lock(g_mutex);
        Stats& stats = g_stats[id];
        ++stats.calls;
        if (same)
            return;
        divergences = ++stats.divergences;
    }
    if (divergences <= kLoggedDivergences)
        log::Write("compare: %s diverged: original %s, ours %s%s", id, original.c_str(), ours.c_str(),
                   divergences == kLoggedDivergences ? " (further divergences only counted)" : "");
}

void LogStats()
{
    std::lock_guard lock(g_mutex);
    for (const auto& [id, stats] : g_stats)
        log::Write("compare: %s: %llu calls, %llu divergences", id.c_str(), stats.calls, stats.divergences);
}

Stats StatsOf(const char* id)
{
    std::lock_guard lock(g_mutex);
    const auto it = g_stats.find(id);
    return it == g_stats.end() ? Stats{} : it->second;
}

} // namespace opensc4::compare

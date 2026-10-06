#include "core/registry.h"

#include "core/log.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwchar>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace opensc4::registry {
namespace {

struct State {
    std::mutex mutex;
    std::vector<Replacement> replacements;
    std::map<std::string, Mode> applied;
};

// Function-local, so registrations from static initialisers in any order work.
State& GetState()
{
    static State state;
    return state;
}

std::string Trim(std::string text)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::optional<Mode> ParseMode(std::string text)
{
    text = Trim(text);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    if (text == "original") return Mode::Original;
    if (text == "ours") return Mode::Ours;
    if (text == "compare") return Mode::Compare;
    return std::nullopt;
}

std::string Narrow(const std::wstring& wide)
{
    std::string out;
    for (wchar_t c : wide)
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
}

// id -> configured text, from the [replacements] section of `iniPath`.
std::map<std::string, std::string> ReadIni(const std::wstring& iniPath)
{
    std::map<std::string, std::string> values;
    std::vector<wchar_t> buffer(32768);
    const DWORD length =
        GetPrivateProfileSectionW(L"replacements", buffer.data(), static_cast<DWORD>(buffer.size()), iniPath.c_str());
    for (const wchar_t* line = buffer.data(); line < buffer.data() + length && *line; line += wcslen(line) + 1) {
        const std::wstring entry(line);
        const auto equals = entry.find(L'=');
        if (equals == std::wstring::npos || entry[0] == L';')
            continue;
        values[Trim(Narrow(entry.substr(0, equals)))] = Narrow(entry.substr(equals + 1));
    }
    return values;
}

// id -> configured text, from OPENSC4_REPLACE="id=mode,id=mode".
std::map<std::string, std::string> ReadEnvironment()
{
    std::map<std::string, std::string> values;
    char buffer[4096];
    const DWORD length = GetEnvironmentVariableA("OPENSC4_REPLACE", buffer, sizeof(buffer));
    if (length == 0 || length >= sizeof(buffer))
        return values;
    std::string text(buffer, length);
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = std::min(text.find(',', start), text.size());
        const std::string pair = text.substr(start, comma - start);
        const auto equals = pair.find('=');
        if (equals != std::string::npos)
            values[Trim(pair.substr(0, equals))] = pair.substr(equals + 1);
        start = comma + 1;
    }
    return values;
}

bool IsWildcard(const std::string& key)
{
    return key.size() >= 2 && key.compare(key.size() - 2, 2, ".*") == 0;
}

// The configuration entry for `id`: an exact match, else the longest
// "prefix.*" wildcard covering it (so "simulators.*" toggles a subsystem).
std::map<std::string, std::string>::const_iterator Lookup(const std::map<std::string, std::string>& configured,
                                                         const std::string& id)
{
    if (const auto exact = configured.find(id); exact != configured.end())
        return exact;
    auto best = configured.end();
    size_t bestLength = 0;
    for (auto it = configured.begin(); it != configured.end(); ++it) {
        const std::string& key = it->first;
        const size_t prefix = key.size() - 1; // keep the dot: "simulators."
        if (IsWildcard(key) && id.compare(0, prefix, key, 0, prefix) == 0 && prefix > bestLength) {
            best = it;
            bestLength = prefix;
        }
    }
    return best;
}

std::wstring DefaultIniPath()
{
    wchar_t path[MAX_PATH];
    if (!log::Directory(path))
        return {};
    return std::wstring(path) + L"opensc4.ini";
}

const char* ToString(Kind kind)
{
    switch (kind) {
    case Kind::Gzcom: return "gzcom";
    case Kind::Detour: return "detour";
    case Kind::Vtable: return "vtable";
    case Kind::Api: return "api";
    }
    return "?";
}

} // namespace

const char* ToString(Mode mode)
{
    switch (mode) {
    case Mode::Original: return "original";
    case Mode::Ours: return "ours";
    case Mode::Compare: return "compare";
    }
    return "?";
}

bool Register(const Replacement& replacement)
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    state.replacements.push_back(replacement);
    return true;
}

void Apply(Phase phase, const std::wstring& iniPath)
{
    State& state = GetState();
    std::vector<Replacement> replacements;
    {
        std::lock_guard lock(state.mutex);
        replacements = state.replacements;
    }

    const std::wstring ini = iniPath.empty() ? DefaultIniPath() : iniPath;
    std::map<std::string, std::string> configured = ReadIni(ini);
    for (auto& [id, value] : ReadEnvironment())
        configured[id] = value; // the environment wins

    // Report typos once, in the first phase.
    if (phase == Phase::DirectorInit) {
        for (const auto& [id, value] : configured) {
            const std::string prefix = IsWildcard(id) ? id.substr(0, id.size() - 1) : std::string();
            const bool known = std::any_of(replacements.begin(), replacements.end(), [&](const Replacement& r) {
                return id == r.id || (!prefix.empty() && std::string(r.id).compare(0, prefix.size(), prefix) == 0);
            });
            if (!known)
                log::Write("registry: configuration names unknown replacement \"%s\"", id.c_str());
        }
    }

    for (const Replacement& r : replacements) {
        if (r.phase != phase)
            continue;
        Mode mode = Mode::Original;
        const char* note = "";
        if (const auto it = Lookup(configured, r.id); it != configured.end()) {
            if (const auto parsed = ParseMode(it->second)) {
                mode = *parsed;
            } else {
                note = " (unrecognised mode in configuration; using original)";
            }
        }
        if (mode == Mode::Compare && !r.supportsCompare) {
            mode = Mode::Original;
            note = " (does not support compare; using original)";
        }
        if (mode != Mode::Original && !r.apply(mode)) {
            mode = Mode::Original;
            note = " (could not be applied; the game keeps the original)";
        }
        {
            std::lock_guard lock(state.mutex);
            state.applied[r.id] = mode;
        }
        log::Write("registry: %-34s %-8s [%s, %s, 0x%08X]%s", r.id, ToString(mode), ToString(r.kind), r.subsystem,
                   static_cast<unsigned>(r.target), note);
    }
}

Mode ModeOf(const char* id)
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    const auto it = state.applied.find(id);
    return it == state.applied.end() ? Mode::Original : it->second;
}

std::size_t Count()
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    return state.replacements.size();
}

void ResetForTests()
{
    State& state = GetState();
    std::lock_guard lock(state.mutex);
    state.applied.clear();
}

} // namespace opensc4::registry

// The "growify" cheat's command and building filter (growify_command.h),
// after sc4-growify by Nicholas Hayes (MIT License; the notice is in
// game_growify.cpp).
#include "core/growify_command.h"

#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace opensc4::growify_command {
namespace {

constexpr const char* kUsage = "Usage: growify <zone type> <zone density> [make historical: true or false, "
                               "default true]. For example: growify Residential Medium";

// The game's zone types (cISC4ZoneManager::ZoneType).
constexpr int32_t kResidentialLow = 1;
constexpr int32_t kCommercialLow = 4;
constexpr int32_t kAgriculture = 7;
constexpr int32_t kIndustrialMedium = 8;

char First(const std::string& word)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
}

bool EqualsIgnoreCase(const std::string& a, const char* b)
{
    if (a.size() != std::strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

} // namespace

std::string Parse(const std::string& text, Command* command)
{
    std::istringstream stream(text);
    std::vector<std::string> words;
    for (std::string word; stream >> word;)
        words.push_back(word);
    if (words.size() < 2 || words.size() > 4)
        return kUsage;
    Command parsed;
    switch (First(words[1])) {
    case 'R': parsed.kind = Kind::Residential; break;
    case 'C': parsed.kind = Kind::Commercial; break;
    case 'A': parsed.kind = Kind::Agriculture; break;
    case 'I': parsed.kind = Kind::Industrial; break;
    default: return "The zone type must be one of: Residential, Commercial, Agriculture or Industrial.";
    }
    if (words.size() == 2) {
        // Agriculture has a single density.
        if (parsed.kind != Kind::Agriculture)
            return kUsage;
        parsed.zone = kAgriculture;
        *command = parsed;
        return {};
    }
    int density = 0; // 0 low, 1 medium, 2 high
    switch (First(words[2])) {
    case 'L': density = 0; break;
    case 'M': density = 1; break;
    case 'H': density = 2; break;
    default: return "The zone density must be one of: Low, Medium or High.";
    }
    switch (parsed.kind) {
    case Kind::Residential: parsed.zone = kResidentialLow + density; break;
    case Kind::Commercial: parsed.zone = kCommercialLow + density; break;
    case Kind::Agriculture: parsed.zone = kAgriculture; break;
    case Kind::Industrial:
        if (density == 0)
            return "Industry has no low density zone: use Medium or High.";
        parsed.zone = kIndustrialMedium + density - 1;
        break;
    }
    if (words.size() == 4) {
        const std::string& value = words[3];
        if (EqualsIgnoreCase(value, "true") || value == "1")
            parsed.historical = true;
        else if (EqualsIgnoreCase(value, "false") || value == "0")
            parsed.historical = false;
        else
            return "The make historical value must be true or false.";
    }
    *command = parsed;
    return {};
}

bool Matches(Kind kind, uint32_t purpose)
{
    switch (purpose) {
    case 1: return kind == Kind::Residential;                   // residence
    case 2: case 3: return kind == Kind::Commercial;            // services, office
    case 5: return kind == Kind::Agriculture;                   // agriculture
    case 6: case 7: case 8: return kind == Kind::Industrial;    // processing, manufacturing, high tech
    default: return false;                                      // none, tourism, other
    }
}

const char* Name(Kind kind)
{
    switch (kind) {
    case Kind::Residential: return "residential";
    case Kind::Commercial: return "commercial";
    case Kind::Agriculture: return "agricultural";
    case Kind::Industrial: return "industrial";
    }
    return "";
}

} // namespace opensc4::growify_command

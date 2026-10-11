// Which region a city file belongs to (date_sync_region.h).
#include "core/date_sync_region.h"

#include <cctype>

namespace opensc4::date_sync_region {

std::string Of(const std::string& cityPath)
{
    std::string folder = cityPath;
    for (char& c : folder)
        c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t slash = folder.find_last_of('\\');
    if (slash == std::string::npos || slash == 0)
        return {};
    folder.resize(slash);
    return folder;
}

} // namespace opensc4::date_sync_region

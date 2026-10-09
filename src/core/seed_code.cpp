// Seeds as players see them (seed_code.h).
#include "core/seed_code.h"

#include <cctype>
#include <cstdio>

namespace opensc4::seed_code {

std::string Format(uint32_t seed)
{
    char text[16];
    std::snprintf(text, sizeof(text), "%04X-%04X", seed >> 16, seed & 0xFFFF);
    return text;
}

bool Parse(const std::string& text, uint32_t* seed)
{
    uint32_t value = 0;
    int digits = 0;
    bool dash = false;
    for (const char c : text) {
        if (c == '-' && digits == 4 && !dash) {
            dash = true;
            continue;
        }
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                      : (c >= 'a' && c <= 'f')                    ? c - 'a' + 10
                      : (c >= 'A' && c <= 'F')                    ? c - 'A' + 10
                                                                  : -1;
        if (d < 0 || ++digits > 8)
            return false;
        value = (value << 4) | static_cast<uint32_t>(d);
    }
    if (digits != 8)
        return false;
    *seed = value;
    return true;
}

} // namespace opensc4::seed_code

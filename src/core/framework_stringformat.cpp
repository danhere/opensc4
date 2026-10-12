// The string class's formatter (cRZBaseString::Sprintf, 0x0090F574: a
// printf of the game's own, with 463 callers) on the game's own strings
// (docs/framework.md). One replacement, off by default:
//
//   framework.string.format  Sprintf (0x0090F574)
//
// The formatter reads the format itself. A conversion's text is made in a
// scratch buffer (64 bytes on the stack, then from the game's heap) and
// goes to the output buffer (1,024 bytes on the stack, then from the heap)
// with its sign, prefix, zeros and padding; at the end the output becomes
// the string's text. Both buffers grow the way the game's do, so the heap
// sees the same requests in the same order.
//
// Still the game's: the C runtime's _snprintf, which makes the text of the
// floating point conversions (%e %f %g %E %G), the string body's assign,
// and the heap.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::framework {
namespace {

// The game's growing buffer (0x0090E25C to 0x0090E366): {first byte, one
// past the last used, end of the block, the heap block or null}.
struct Buffer {
    char* begin;
    char* current;
    char* end;
    char* heap = nullptr;

    Buffer(char* storage, uint32_t size) : begin(storage), current(storage), end(storage + size) {}
    ~Buffer() { reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(heap); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    uint32_t Size() const { return static_cast<uint32_t>(current - begin); }
    uint32_t Capacity() const { return static_cast<uint32_t>(end - begin); }

    // 0x0090E25C: a new block when the size asked for is not below the
    // capacity.
    void Grow(uint32_t size)
    {
        if (size < Capacity())
            return;
        char* const block = reinterpret_cast<char*(__cdecl*)(uint32_t)>(addr::SC4_operator_new)(size);
        const uint32_t used = Size();
        std::memcpy(block, begin, used);
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_operator_delete)(heap);
        heap = block;
        begin = block;
        end = block + size;
        current = block + used;
    }

    // 0x0090E2C8.
    void Resize(uint32_t size)
    {
        if (size > Size())
            Grow(size);
        current = begin + size;
    }

    void Reserve(uint32_t count)
    {
        if (static_cast<uint32_t>(end - current) >= count)
            return;
        const uint32_t twice = Capacity() * 2;
        const uint32_t needed = Size() + count;
        Grow(twice < needed ? needed : twice);
    }

    // 0x0090E2EB.
    void Put(char c)
    {
        if (current == end)
            Grow(Capacity() * 2);
        *current++ = c;
    }

    // 0x0090E312.
    void Append(const char* text, uint32_t count)
    {
        Reserve(count);
        std::memcpy(current, text, count);
        current += count;
    }

    // 0x0090E366.
    void Fill(uint32_t count, char c)
    {
        Reserve(count);
        std::memset(current, c, count);
        current += count;
    }
};

bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

// The game's 0x009106A9 for UTF-8: the bytes the character at `text`
// takes, by its first two bytes only.
uint32_t CharacterSize(const char* text, uint32_t left)
{
    if (left == 0)
        return 0;
    const uint8_t first = static_cast<uint8_t>(text[0]);
    if (first < 0xC0 || first > 0xFD)
        return 1;
    const uint8_t second = static_cast<uint8_t>(text[1]);
    if (second < 0x80 || second > 0xBF)
        return 1;
    if (first < 0xE0)
        return 2;
    if (first < 0xF0)
        return 3;
    if (first < 0xF8)
        return 4;
    return first < 0xFC ? 5 : 6;
}

// The game's 0x009107D8 for UTF-8: the bytes that do not continue a
// character.
int CharacterCount(const char* text, uint32_t length)
{
    int count = 0;
    for (uint32_t i = 0; i < length; ++i) {
        const uint8_t byte = static_cast<uint8_t>(text[i]);
        if (byte < 0x80 || byte > 0xBF)
            ++count;
    }
    return count;
}

// The digits of a value as the C runtime's %u, %o, %x and %X write them.
uint32_t Digits(char* out, uint64_t value, char conversion)
{
    const uint32_t base = conversion == 'o' ? 8 : (conversion == 'x' || conversion == 'X') ? 16 : 10;
    const char* const digits = conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
    char reversed[24];
    uint32_t count = 0;
    do {
        reversed[count++] = digits[value % base];
        value /= base;
    } while (value);
    for (uint32_t i = 0; i < count; ++i)
        out[i] = reversed[count - 1 - i];
    return count;
}

// A width or a precision written in the format; `c` is its first digit and
// ends as the character after the number.
int Number(const char*& p, char& c)
{
    uint32_t value = 0;
    do {
        value = value * 10 + static_cast<uint32_t>(static_cast<int>(c) - '0');
        c = *p++;
    } while (IsDigit(c));
    return static_cast<int>(value);
}

// The game's 0x0090F574.
char* __cdecl Sprintf(char* string, const char* format, ...)
{
    va_list list;
    va_start(list, format);
    const char* arguments = reinterpret_cast<const char*>(list);
    va_end(list);
    const auto next = [&arguments] {
        const uint32_t value = *reinterpret_cast<const uint32_t*>(arguments);
        arguments += 4;
        return value;
    };

    char scratchStorage[0x40];
    Buffer scratch(scratchStorage, sizeof scratchStorage);
    char outStorage[0x400];
    Buffer out(outStorage, sizeof outStorage);

    for (char c = *format++; c; c = *format++) {
        if (c != '%') {
            const char* const start = format - 1;
            while (*format && *format != '%')
                ++format;
            out.Append(start, static_cast<uint32_t>(format - start));
            continue;
        }

        // Flags, in any order and number.
        bool left = false, plus = false, space = false, zero = false, alternate = false;
        const char* p = format;
        char b;
        for (;;) {
            b = *p++;
            if (b == '-')
                left = true;
            else if (b == '+')
                plus = true;
            else if (b == ' ')
                space = true;
            else if (b == '0')
                zero = true;
            else if (b == '#')
                alternate = true;
            else
                break;
        }
        format = p;
        if (b == '%') {
            out.Put(b);
            continue;
        }
        if (left)
            zero = false;
        if (plus)
            space = false;

        int width = -1;
        if (b == '*') {
            width = static_cast<int>(next());
            b = *p++;
        } else if (IsDigit(b)) {
            width = Number(p, b);
        }
        // A point without a number or a star leaves the precision unset.
        int precision = -1;
        if (b == '.') {
            b = *p++;
            if (b == '*') {
                precision = static_cast<int>(next());
                b = *p++;
            } else if (IsDigit(b)) {
                precision = Number(p, b);
            }
        }
        // The size: h 0, none 1, l 2, ll and I64 3.
        int size = 1;
        if (b == 'I') {
            if (p[0] == '6' && p[1] == '4') {
                p += 2;
                b = *p++;
                size = 3;
            }
        } else if (b == 'h') {
            b = *p++;
            size = 0;
        } else if (b == 'l') {
            b = *p++;
            size = 2;
            if (b == 'l') {
                b = *p++;
                size = 3;
            }
        }
        format = p;

        const char* text = scratch.begin;
        uint32_t length = 0;
        bool integer = false, hasSign = false, negative = false;
        switch (b) {
        case 'd':
        case 'i':
            hasSign = true;
            [[fallthrough]];
        case 'o':
        case 'u':
        case 'x':
        case 'X': {
            integer = true;
            uint64_t value;
            if (size == 3) {
                value = next();
                value |= static_cast<uint64_t>(next()) << 32;
                if (hasSign && static_cast<int64_t>(value) < 0) {
                    negative = true;
                    value = 0 - value;
                    b = 'u';
                }
            } else if (size == 0) {
                uint32_t word = next() & 0xFFFF;
                if (hasSign && static_cast<int16_t>(word) < 0) {
                    negative = true;
                    word = 0 - word;
                    b = 'u';
                }
                value = word & 0xFFFF;
            } else {
                uint32_t number = next();
                if (hasSign && static_cast<int32_t>(number) < 0) {
                    negative = true;
                    number = 0 - number;
                    b = 'u';
                }
                value = number;
            }
            length = Digits(scratch.begin, value, b);
            break;
        }
        case 'c':
            scratch.begin[0] = static_cast<char>(next());
            length = 1;
            break;
        case 'e':
        case 'f':
        case 'g':
        case 'E':
        case 'G': {
            // The C runtime gets the number without its sign, with only
            // the # flag and the precision.
            char conversion[0x20];
            char* f = conversion;
            *f++ = '%';
            if (alternate)
                *f++ = '#';
            if (precision >= 0) {
                *f++ = '.';
                f += Digits(f, static_cast<uint32_t>(precision), 'u');
            }
            *f++ = b;
            *f = 0;
            double value;
            std::memcpy(&value, arguments, 8);
            arguments += 8;
            hasSign = true;
            if (value < 0.0) {
                negative = true;
                value = -value;
            }
            using PrintFn = int(__cdecl*)(char*, uint32_t, const char*, ...);
            const auto print = reinterpret_cast<PrintFn>(addr::CRT__snprintf);
            int written;
            while ((written = print(scratch.begin, scratch.Size(), conversion, value)) < 0)
                scratch.Resize(scratch.Capacity() * 2);
            text = scratch.begin;
            length = static_cast<uint32_t>(written);
            break;
        }
        case 's': {
            // The precision counts characters, not bytes.
            text = reinterpret_cast<const char*>(next());
            const uint32_t whole = static_cast<uint32_t>(std::strlen(text));
            length = whole;
            if (precision >= 0) {
                length = 0;
                if (static_cast<int>(whole) > 0) {
                    for (int characters = 0; characters < precision;) {
                        const uint32_t step = CharacterSize(text + length, whole - length);
                        if (step == 0)
                            break;
                        length += step;
                        ++characters;
                        if (static_cast<int>(length) >= static_cast<int>(whole))
                            break;
                    }
                }
            }
            break;
        }
        case 'p':
            // The C runtime's %p: eight digits.
            for (uint32_t value = next(), i = 0; i < 8; ++i)
                scratch.begin[i] = "0123456789ABCDEF"[(value >> (28 - 4 * i)) & 0xF];
            length = 8;
            break;
        case 'n':
            // The length the string had before this call, not what has
            // been written so far.
            *reinterpret_cast<uint32_t*>(next()) =
                static_cast<uint32_t>(At<char*>(string, 0x8) - At<char*>(string, 0x4));
            continue;
        default:
            // Not a conversion: an empty text (the width still pads it),
            // and the character is read again as plain text.
            format = p - 1;
            break;
        }

        int characters = CharacterCount(text, length);
        int zeros = 0;
        const char* prefix = nullptr;
        uint32_t prefixLength = 0;
        if (integer) {
            if (precision > characters)
                zeros = precision - characters;
            if (alternate) {
                if (b == 'X') {
                    prefix = "0X";
                    prefixLength = 2;
                } else if (b == 'o') {
                    prefix = "0";
                    prefixLength = 1;
                } else if (b == 'x') {
                    prefix = "0x";
                    prefixLength = 2;
                }
                characters += static_cast<int>(prefixLength);
            }
        }
        char sign = 0;
        if (hasSign) {
            if (negative) {
                sign = '-';
            } else {
                if (space)
                    sign = ' ';
                if (plus)
                    sign = '+';
            }
            if (sign) {
                ++characters;
                if (zero)
                    out.Put(sign);
            }
        }
        int padding = 0;
        if (width > 0) {
            padding = width - zeros - characters;
            if (padding > 0 && !left)
                out.Fill(static_cast<uint32_t>(padding), zero ? '0' : ' ');
        }
        if (sign && !zero)
            out.Put(sign);
        // The prefix comes after the padding, zeros too.
        if (prefix)
            out.Append(prefix, prefixLength);
        if (zeros > 0)
            out.Fill(static_cast<uint32_t>(zeros), '0');
        out.Append(text, length);
        if (padding > 0 && left)
            out.Fill(static_cast<uint32_t>(padding), ' ');
    }

    using AssignFn = void*(__thiscall*)(char* body, const char* first, const char* last);
    reinterpret_cast<AssignFn>(addr::SC4StringBody_AssignChars)(string + 0x4, out.begin, out.current);
    return string;
}

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* entry = addr_check::Find(addr::cRZBaseString_Sprintf);
    void* original = nullptr;
    return entry && hook::Install(*entry, reinterpret_cast<void*>(&Sprintf), &original);
}

const bool g_registered = registry::Register({
    "framework.string.format",
    "framework/string",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZBaseString_Sprintf,
    "Our string formatter: cRZBaseString::Sprintf (docs/framework.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::framework

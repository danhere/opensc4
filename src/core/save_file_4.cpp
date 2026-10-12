// The packed file's write path (docs/save-format.md), part 4: the
// compressor, 0x00A6D936 to 0x00A6E1A4. Replacement, off by default:
//
//   saves.file.part4: the compressing half of the framework's compressor
//     class: Compress, the bound on a compressed size, the quick switch, the
//     encoder with its header, and the encoder's two helpers.
//
// The compressor (0xC bytes, constructor 0x00A6E193, vtable 0x00B00A5C,
// interface 0x8359908E) is what the packed file makes on the stack for every
// record it compresses (0x0096E4DC) or expands (0x0096E617):
//
//   +0x0  vtable: 0 QueryInterface 0x00A6D93D, 1 AddRef 0x00968B60 (shared),
//         2 Release 0x00A6D962, 3 Compress 0x00A6DFEB, 4 Decompress
//         0x00A6E065, 5 the bound 0x00A6D979, 6 the stored length 0x00A6E0C8,
//         7 the expanded length 0x00A6E105, 8 the version text 0x00A6E1A4,
//         9 the deleting destructor 0x00A6DFCE
//   +0x4  reference count
//   +0x8  a byte: quick (the packed file sets it for sources above 250,000
//         bytes)
//
// A compressed record is: a 32-bit length of everything that follows plus
// these four bytes; 0x10 0xFB; the source length in three bytes, high byte
// first; then the commands. A command copies 0 to 3 bytes of source as they
// are (or, alone, 4 to 112 of them in fours) and then repeats 3 to 1,028
// bytes that came up to 131,071 bytes earlier:
//
//   2 bytes  0oocccpp oooooooo           3 to 10 bytes, at most 1,024 back
//   3 bytes  10cccccc ppoooooo oooooooo  4 to 67 bytes, at most 16,384 back
//   4 bytes  110occpp oooooooo oooooooo cccccccc  5 to 1,028 bytes
//   1 byte   111nnnnn                    (n + 1) * 4 plain bytes
//   1 byte   111111pp                    the end, with the last 0 to 3 bytes
//
// The search: a table of 65,536 chain heads by a hash of three bytes
// ((first << 8 | third) ^ second << 4) and a ring of 131,072 links to the
// position that had the same hash before. At each position the chain is
// walked from the newest candidate back, as long as candidates are within
// 131,071 bytes; a candidate is measured only when its byte at the best
// length so far agrees, and replaces the best only when it is longer AND
// its length less its command's size is greater. So of two equal candidates
// the nearer wins, and the walk stops at once at 1,028 bytes. Every position
// passed enters the table, the repeated ones too, unless quick is set: then
// only the first position of a repeat does. The encoder reads up to two
// bytes past the source's end for the hash; the packed file allocates them
// (size + 2, zeroed).
//
// Still the game's:
//   - what expanding uses, the read path's: the decoder 0x00A6DA3F (also
//     called by 0x00920B78), Decompress 0x00A6E065, the stored and expanded
//     lengths 0x00A6E0C8 and 0x00A6E105, the validity check 0x00A6E14A and
//     its helpers 0x00A6D999 (a big-endian read), 0x00A6D9E3 (is the
//     signature 0x10FB or 0x11FB) and 0x00A6DA15 (the expanded length of a
//     stream);
//   - what both halves use: the constructor 0x00A6E193 (also the end of the
//     class factory 0x0097516B), the destructor 0x00A6D936, QueryInterface
//     0x00A6D93D, Release 0x00A6D962 and the deleting destructor 0x00A6DFCE;
//   - 0x00A6D906 and 0x00A6D91F, which are not the compressor's: they
//     destroy a vector of 0x30-byte elements of another class.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::saves {
namespace {

constexpr int32_t kLongest = 0x404;     // the longest repeat
constexpr int32_t kReach = 0x1FFFF;     // how far back a candidate may be, and the ring's mask
constexpr uint32_t kPlainMost = 0x70;   // the most plain bytes one command carries
constexpr uint32_t kHeadBytes = 0x40000;
constexpr uint32_t kLinkBytes = 0x80000;

void* Malloc(uint32_t size)
{
    return reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::CRT_malloc)(size);
}

void Free(void* block)
{
    reinterpret_cast<void(__cdecl*)(void*)>(addr::CRT_free)(block);
}

// The chain a position belongs to, from its three bytes.
inline uint32_t Hash(const uint8_t* at)
{
    return ((uint32_t{at[0]} << 8) | at[2]) ^ (uint32_t{at[1]} << 4);
}

// The game's 0x00A6D979 (slot 5). The most a compressed copy of `size` bytes
// can take: one and a half times the size and 0x400 more, in 32 bits.
uint32_t __fastcall MaxCompressedLength(char*, void*, uint32_t size)
{
    return size * 15u / 10u + 0x400;
}

// The game's 0x00A6D98F. Sets the quick byte.
void __fastcall SetQuick(char* self, void*, uint8_t quick)
{
    At<uint8_t>(self, 0x8) = quick;
}

// The game's 0x00A6D9B6. Stores the low `count` bytes of `value`, high byte
// first; answers the place after them.
uint8_t* __cdecl WriteBigEndian(uint8_t* to, uint32_t value, uint32_t count)
{
    value <<= ((4 - count) << 3) & 31;
    for (uint32_t left = count; left != 0; --left) {
        *to++ = static_cast<uint8_t>(value >> 24);
        value <<= 8;
    }
    return to;
}

// The game's 0x00A6DBD6. How many bytes two places have in common, at most
// `most`.
__declspec(noinline) uint32_t __cdecl MatchLength(const uint8_t* a, const uint8_t* b, uint32_t most)
{
    uint32_t same = 0;
    if (most > 0) {
        do {
            if (*a++ != *b++)
                break;
            ++same;
        } while (same < most);
    }
    return same;
}

// The game's 0x00A6DBFE. Writes the commands for `length` bytes at `source`
// to `out` and answers how many bytes that took; 0 when it could not get its
// two tables (and then the first table, if it was the second that failed,
// is not given back). `window` is not read.
__declspec(noinline) int32_t __cdecl Encode(const uint8_t* source, int32_t length, uint8_t* out, int32_t,
                                            int32_t quick)
{
    uint32_t run = 0;                // plain bytes waiting to be written
    const uint8_t* pending = source; // where they start
    const uint8_t* at = source;
    uint8_t* to = out;

    auto* const heads = static_cast<int32_t*>(Malloc(kHeadBytes));
    if (!heads)
        return 0;
    auto* const links = static_cast<int32_t*>(Malloc(kLinkBytes));
    if (!links)
        return 0;
    for (uint32_t i = 0; i < kHeadBytes / 4; ++i)
        heads[i] = -1;

    while (length > 0) {
        uint32_t best = 2;
        uint32_t bestOffset = 0;
        uint32_t bestCost = 2;
        const uint32_t most = static_cast<uint32_t>(length >= kLongest ? kLongest : length);
        int32_t* const head = &heads[Hash(at)];
        int32_t candidate = *head;
        const int32_t position = static_cast<int32_t>(at - source);
        int32_t oldest = position - kReach;
        if (oldest <= 0)
            oldest = 0;

        if (candidate >= oldest) {
            do {
                const uint8_t* const earlier = source + candidate;
                if (at[best] == earlier[best]) {
                    const uint32_t found = MatchLength(at, earlier, most);
                    if (found > best) {
                        const uint32_t offset = static_cast<uint32_t>(at - earlier) - 1;
                        uint32_t cost;
                        if (offset < 0x400 && found <= 10)
                            cost = 2;
                        else if (offset < 0x4000 && found <= 0x43)
                            cost = 3;
                        else
                            cost = 4;
                        if (found - cost + 4 > best - bestCost + 4) {
                            bestCost = cost;
                            best = found;
                            bestOffset = offset;
                            if (best >= static_cast<uint32_t>(kLongest))
                                break;
                        }
                    }
                }
                candidate = links[candidate & kReach];
            } while (candidate >= oldest);
        }

        if (!(bestCost < best)) {
            // Nothing worth a command: one more plain byte.
            links[position & kReach] = *head;
            ++run;
            --length;
            ++at;
            *head = position;
            continue;
        }

        while (run > 3) {
            uint32_t count = run & ~uint32_t{3};
            if (count > kPlainMost)
                count = kPlainMost;
            run -= count;
            *to++ = static_cast<uint8_t>((count >> 2) - 0x21);
            std::memcpy(to, pending, count);
            pending += count;
            to += count;
        }

        if (bestCost == 2) {
            *to++ = static_cast<uint8_t>(static_cast<uint8_t>((bestOffset >> 3) & 0xE0) +
                                         static_cast<uint8_t>(static_cast<uint8_t>(best - 3) << 2) +
                                         static_cast<uint8_t>(run));
            *to++ = static_cast<uint8_t>(bestOffset);
        } else if (bestCost == 3) {
            *to++ = static_cast<uint8_t>(best + 0x7C);
            *to++ = static_cast<uint8_t>(static_cast<uint8_t>(bestOffset >> 8) + static_cast<uint8_t>(run << 6));
            *to++ = static_cast<uint8_t>(bestOffset);
        } else {
            *to++ = static_cast<uint8_t>(static_cast<uint8_t>(((best - 5) >> 6) & 0xFC) +
                                         static_cast<uint8_t>((bestOffset >> 12) & 0xF0) +
                                         static_cast<uint8_t>(run) - 0x40);
            *to++ = static_cast<uint8_t>(bestOffset >> 8);
            *to++ = static_cast<uint8_t>(bestOffset);
            *to++ = static_cast<uint8_t>(best - 5);
        }
        if (run != 0) {
            std::memcpy(to, pending, run);
            to += run;
            run = 0;
        }

        if (quick != 0) {
            links[position & kReach] = *head;
            *head = position;
            at += best;
        } else {
            int32_t entering = position;
            for (uint32_t left = best; left != 0; --left) {
                int32_t* const place = &heads[Hash(at)];
                links[entering & kReach] = *place;
                *place = entering;
                ++entering;
                ++at;
            }
        }
        length -= static_cast<int32_t>(best);
        pending = at;
    }

    while (run > 3) {
        uint32_t count = run & ~uint32_t{3};
        if (count > kPlainMost)
            count = kPlainMost;
        run -= count;
        *to++ = static_cast<uint8_t>((count >> 2) - 0x21);
        std::memcpy(to, pending, count);
        pending += count;
        to += count;
    }
    *to++ = static_cast<uint8_t>(run - 4);
    if (run != 0) {
        std::memcpy(to, pending, run);
        to += run;
    }

    Free(links);
    Free(heads);
    return static_cast<int32_t>(to - out);
}

// The game's 0x00A6DF84. The five-byte header (0x10 0xFB, the length in
// three bytes) and the commands after it; answers the bytes written. Only
// bit 0 of `*quick` is read.
__declspec(noinline) int32_t __cdecl EncodeWithHeader(const uint8_t* source, int32_t length, uint8_t* out,
                                                      const int32_t* quick)
{
    const int32_t mode = *quick & 1;
    WriteBigEndian(out, 0x10FB, 2);
    WriteBigEndian(out + 2, static_cast<uint32_t>(length), 3);
    return Encode(source, length, out + 5, 0x20000, mode) + 5;
}

// What Compress does inside its catch of everything.
__declspec(noinline) void CompressBody(char* self, const uint8_t* source, int32_t length, uint8_t* out,
                                       uint32_t* outLength)
{
    int32_t quick = At<uint8_t>(self, 0x8) != 0 ? 1 : 0;
    uint32_t written = static_cast<uint32_t>(EncodeWithHeader(source, length, out + 4, &quick));
    *outLength = written;
    written += 4;
    *outLength = written;
    std::memcpy(out, &written, 4);
}

// The original's compiler (Visual C++ 7.1) catches structured exceptions in
// a catch of everything, an access violation in the encoder as much as a
// C++ throw, so this is a structured handler in a function of its own
// (crash.h, "SEH rule"). False when something was caught.
__declspec(noinline) bool CompressGuarded(char* self, const uint8_t* source, int32_t length, uint8_t* out,
                                          uint32_t* outLength)
{
    __try {
        CompressBody(self, source, length, out, outLength);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game's 0x00A6DFEB (slot 3). Compresses `length` bytes at `source` into
// `out`: the 32-bit length first, then the header and the commands;
// `*outLength` gets the whole length. The catch block (0x00A6E03A) zeroes
// `*outLength` and goes on to the return. Answers 1 or 0 in EAX: is
// `*outLength` not zero.
uint32_t __fastcall Compress(char* self, void*, const uint8_t* source, int32_t length, uint8_t* out,
                             uint32_t* outLength)
{
    if (!CompressGuarded(self, source, length, out, outLength))
        *outLength = 0;
    return *outLength != 0 ? 1u : 0u;
}

bool InstallSaveFile4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[7] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::cRZCompressor_GetMaxCompressedLength, reinterpret_cast<void*>(&MaxCompressedLength)},
        {addr::cRZCompressor_SetQuick, reinterpret_cast<void*>(&SetQuick)},
        {addr::cRZCompressor_WriteBigEndian, reinterpret_cast<void*>(&WriteBigEndian)},
        {addr::cRZCompressor_MatchLength, reinterpret_cast<void*>(&MatchLength)},
        {addr::cRZCompressor_Encode, reinterpret_cast<void*>(&Encode)},
        {addr::cRZCompressor_EncodeWithHeader, reinterpret_cast<void*>(&EncodeWithHeader)},
        {addr::cRZCompressor_Compress, reinterpret_cast<void*>(&Compress)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_saveFile4Registered = registry::Register({
    "saves.file.part4",
    "saves",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cRZCompressor_Compress,
    "Our packed file write path, part 4: the compressor's Compress and its encoder",
    false,
    InstallSaveFile4,
});

} // namespace
} // namespace opensc4::saves

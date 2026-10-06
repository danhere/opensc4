#include "dbpf/qfs.h"

#include <cstddef>

namespace opensc4::dbpf {
namespace {

bool Fail(std::string* error, const char* why)
{
    if (error)
        *error = why;
    return false;
}

constexpr size_t kRecordHeader = 4 + 5; // compressed size, flags, 0xFB, 3-byte size

} // namespace

bool LooksCompressed(std::span<const uint8_t> data)
{
    return data.size() >= kRecordHeader && (data[4] & 0xFE) == 0x10 && data[5] == 0xFB;
}

bool DecodeRefPack(std::span<const uint8_t> in, std::span<uint8_t> out, std::string* error)
{
    size_t i = 0, o = 0;
    for (;;) {
        if (i >= in.size())
            return Fail(error, "stream ends without a stop command");
        const uint8_t b0 = in[i];
        size_t literals = 0, copy = 0, offset = 0, length = 0;
        bool stop = false;
        if (b0 < 0x80) {
            length = 2;
            if (i + length > in.size())
                return Fail(error, "truncated 2-byte command");
            literals = b0 & 0x03;
            copy = ((b0 & 0x1C) >> 2) + 3;
            offset = ((b0 & 0x60) << 3) + in[i + 1] + 1;
        } else if (b0 < 0xC0) {
            length = 3;
            if (i + length > in.size())
                return Fail(error, "truncated 3-byte command");
            literals = (in[i + 1] >> 6) & 0x03;
            copy = (b0 & 0x3F) + 4;
            offset = ((in[i + 1] & 0x3F) << 8) + in[i + 2] + 1;
        } else if (b0 < 0xE0) {
            length = 4;
            if (i + length > in.size())
                return Fail(error, "truncated 4-byte command");
            literals = b0 & 0x03;
            copy = ((b0 & 0x0C) << 6) + in[i + 3] + 5;
            offset = ((b0 & 0x10) << 12) + (in[i + 1] << 8) + in[i + 2] + 1;
        } else if (b0 < 0xFC) {
            length = 1;
            literals = ((b0 & 0x1F) << 2) + 4;
        } else {
            length = 1;
            literals = b0 & 0x03;
            stop = true;
        }
        i += length;
        if (i + literals > in.size())
            return Fail(error, "literals run past the end of the stream");
        if (o + literals + copy > out.size())
            return Fail(error, "output larger than the stated size");
        for (size_t k = 0; k < literals; ++k)
            out[o++] = in[i++];
        if (copy) {
            if (offset > o)
                return Fail(error, "back-reference before the start of the output");
            // Byte by byte: source and destination may overlap (runs).
            for (size_t k = 0; k < copy; ++k, ++o)
                out[o] = out[o - offset];
        }
        if (stop)
            break;
    }
    if (o != out.size())
        return Fail(error, "output smaller than the stated size");
    return true;
}

bool Decompress(std::span<const uint8_t> record, std::vector<uint8_t>& out, std::string* error)
{
    if (!LooksCompressed(record))
        return Fail(error, "no QFS header");
    const uint8_t flags = record[4];
    // Flag 0x01: a 3-byte compressed size follows the uncompressed size. 0x80
    // (4-byte sizes) is not used by SimCity 4 and not supported.
    if (flags & 0x80)
        return Fail(error, "4-byte QFS sizes are not supported");
    const size_t size = (static_cast<size_t>(record[6]) << 16) | (record[7] << 8) | record[8];
    size_t start = kRecordHeader;
    if (flags & 0x01)
        start += 3;
    if (start > record.size())
        return Fail(error, "truncated QFS header");
    out.assign(size, 0);
    return DecodeRefPack(record.subspan(start), out, error);
}

} // namespace opensc4::dbpf

// QFS (RefPack) decompression, as used for compressed records in SimCity 4's
// DBPF packages (docs/resources.md, "Package format").
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace opensc4::dbpf {

// True if `data` starts with a QFS header (0x10FB or 0x11FB after a 4-byte
// compressed size, as stored in packages).
bool LooksCompressed(std::span<const uint8_t> data);

// Decompresses a record as stored in a package: a 4-byte compressed size,
// the QFS header (flags byte, 0xFB, 3-byte big-endian uncompressed size) and
// the RefPack stream. `out` gets exactly the uncompressed size; false (with a
// reason in `error`) if the stream is malformed or does not fill it exactly.
bool Decompress(std::span<const uint8_t> record, std::vector<uint8_t>& out, std::string* error = nullptr);

// Decodes a bare RefPack stream (no headers) into `out`, which must already
// have the uncompressed size.
bool DecodeRefPack(std::span<const uint8_t> stream, std::span<uint8_t> out, std::string* error = nullptr);

} // namespace opensc4::dbpf

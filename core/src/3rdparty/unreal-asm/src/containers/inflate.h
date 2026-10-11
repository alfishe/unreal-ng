#pragma once

// A zlib stream (RFC 1950: two header bytes, deflate data of RFC 1951, an Adler-32) inflated in standard C++: SZX keeps
// its RAM pages so, and the library stays free of other libraries.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace unrealasm::containers
{
/// The bytes `stream` holds; false with the reason when the stream is broken or its checksum is wrong
bool InflateZlib(std::span<const uint8_t> stream, std::vector<uint8_t>& out, std::string& error);
}  // namespace unrealasm::containers

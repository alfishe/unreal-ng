#pragma once

/// @file base64.h
/// @brief RFC 4648 base64 for the automation surfaces' uploads and downloads (symbol and assembler-source files)

#include <cstdint>
#include <string>
#include <vector>

namespace base64
{
/// Standard alphabet, with padding
std::string Encode(const std::vector<uint8_t>& bytes);
/// Standard or URL-safe alphabet, padding and blanks optional; false on any other character
bool Decode(const std::string& text, std::vector<uint8_t>& out);
}  // namespace base64

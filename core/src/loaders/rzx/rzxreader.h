#pragma once

/// @file rzxreader.h
/// @brief Parses an RZX 0.12 / 0.13 file into an rzx::File. Never touches an
/// emulator (design §3); the player applies it afterwards.
///
/// Every block length is checked against the file, every frame against its
/// block; compressed data inflates to its stated size (snapshots) or within
/// a bound (input blocks). Unknown blocks are skipped with a warning.
/// Worked example: an input block of 2 frames {fetch 100, 1 IN #BF},
/// {fetch 90, IN counter 65535} parses to frames {100, inCount 1, offset 0}
/// and {90, inCount 1, offset 0, repeated} with inValues [#BF].

#include <cstddef>
#include <cstdint>
#include <string>

#include "loaders/rzx/rzxformat.h"

class RzxReader
{
public:
    /// False with `error` set when the data is not a usable RZX file
    static bool Parse(const uint8_t* data, size_t size, rzx::File& file, std::string& error);
    /// Read and parse a file (UTF-8 path)
    static bool ParseFile(const std::string& path, rzx::File& file, std::string& error);

private:
    static bool ParseCreator(const uint8_t* block, uint32_t length, rzx::File& file, std::string& error);
    static bool ParseSecurityInfo(const uint8_t* block, uint32_t length, rzx::File& file, std::string& error);
    static bool ParseSnapshot(const uint8_t* block, uint32_t length, rzx::File& file, std::string& error);
    static bool ParseInput(const uint8_t* block, uint32_t length, rzx::File& file, std::string& error);
    static bool ParseFrames(const uint8_t* data, size_t size, uint32_t frameCount, rzx::InputBlock& input,
                            rzx::File& file, std::string& error);
};

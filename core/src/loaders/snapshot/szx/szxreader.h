#pragma once

/// @file szxreader.h
/// @brief Parses an SZX file into an szx::Stage. Never touches an emulator:
/// the loader applies the stage afterwards (design §6-§7).
///
/// Every length is checked against the file and against the block's defined
/// size; compressed payloads inflate to their exact size or fail. Unknown
/// blocks are skipped and listed. Worked example: a RAMP block of dwSize
/// 1203 with wFlags = 1 carries 1200 bytes of zlib stream that must inflate
/// to exactly 16384 bytes.

#include <cstddef>
#include <cstdint>
#include <string>

#include "loaders/snapshot/szx/szxformat.h"

class SzxReader
{
public:
    /// False with `error` set when the file is not a usable SZX file
    static bool Parse(const uint8_t* data, size_t size, szx::Stage& stage, std::string& error);

private:
    static bool ParseCreator(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseZ80Regs(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseSpecRegs(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseRamPage(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseAy(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseBeta128(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseBetaDisk(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseDskFile(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseTape(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseGs(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    static bool ParseGsRamPage(const uint8_t* body, uint32_t size, szx::Stage& stage, std::string& error);
    /// A fixed-size block shorter than its minimum: false with `error`
    static bool Short(uint32_t size, size_t minimum, const char* name, std::string& error);
    /// libspectrum up to 0.5.0 wrote A and F (and A', F') swapped
    static bool IsSwappedAfCreator(const szx::Creator& creator);
};

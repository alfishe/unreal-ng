#pragma once

// One rasm assembly from source to outputs: the preprocessor, the engine, then what rasm would write. The entry point of
// the rasm engine for the frontend, the tests and the verification tools.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "dialects/rasm/engine/rasmengine.h"
#include "unrealasm/diagnostics.h"

namespace unrealasm::rasm
{
struct RasmLabel
{
    std::string name;
    int64_t value = 0;
    int bank = 0;        ///< the memory space (0..259 BANK, 260 the default workspace, more for temporary spaces)
    int orgzone = 0;
    bool crunched = false;   ///< defined inside a crunched section (its value is where rasm put it before crunching)
    bool local = false;      ///< a label rasm makes local (macro @label, proximity .label): its full name is unique
    int file = 0;        ///< index in RasmAssembly::files
    int line = 0;
};

struct RasmAssembly
{
    bool ok = false;                       ///< no error
    Diagnostics diagnostics;               ///< errors, warnings, PRINT output (Info), "not converted: ..." warnings
    std::vector<std::string> files;        ///< source files as rasm names them (main first)

    bool hasBinary = false;                ///< rasm writes its plain binary (no SAVE, no BUILD* output)
    int binaryStart = 0;                   ///< address of the binary's first byte
    std::vector<uint8_t> binary;           ///< without the AMSDOS header (see amsdosHeader)
    bool amsdosHeader = false;             ///< AMSDOS directive: rasm writes a 128-byte AMSDOS header before the binary

    struct File
    {
        std::string name;                  ///< "" = the BUILDZX snapshot (.sna)
        std::vector<uint8_t> bytes;
        int bank = -1;                     ///< SAVE: the memory saved and the header written
        int offset = 0;
        int size = 0;
        SaveFormat format = SaveFormat::Plain;
    };
    std::vector<File> outputs;             ///< SAVE files, the BUILDZX snapshot
    std::vector<std::string> notConverted; ///< rasm outputs not produced (CPC media ...)

    int binaryBank = -1;                   ///< the memory space the plain binary is cut from

    /// BUILDZX: the 48K snapshot from banks 5 / 2 / 0 (the first 16K of each), or with BANKSET 0 the upper 48K of bank 0
    bool zxSnapshot = false;
    bool zxBankset0 = false;
    int zxRun = 0;
    int zxStack = 0;

    std::vector<TraceItem> trace;          ///< the runs of bytes put down (crunched sections: one run of the crunched bytes)
    std::map<int, std::vector<uint8_t>> memory;   ///< the final memory of every space the trace or an output uses

    std::vector<RasmLabel> labels;         ///< labels in definition order with their final values
    std::map<std::string, double> variables;   ///< variables (NAME=expr) with their last value
    std::map<std::string, std::string> aliases; ///< EQU aliases: their text as rasm resolved it
};

/// Assembles a rasm source (its bytes, its name as rasm prints it) reading INCLUDE / INCBIN files through fs
RasmAssembly AssembleRasm(const std::string& mainName, const std::vector<uint8_t>& source, const FileSystem& fs,
                          const EngineOptions& options);
}  // namespace unrealasm::rasm

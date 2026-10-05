#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "loaders/snapshot/snapshotimage.h"

#include <array>
#include <string>
#include <vector>

class EmulatorContext;
class ModuleLogger;

/// region <Info>

// ZX-Poly snapshot (.zxp): the full state of the four CPU modules of the
// ZX-Poly quad-Z80 machine, as written by the zxpoly Sprite Corrector.
// Reference: raydac/zxpoly, zxpoly-emul/src/jbbp/snapshots/zxp/
//            com.igormaznitsa.zxpoly.formats.ZXPParser.jbbp
//
// Big-endian layout:
//   0    int32   magic 0xC0BA0100
//   4    int32   flags
//   8    uint8   #3D00 (D0 nWAIT, D1 local reset, D2-D4 video mode,
//                       D5-D6 mapped CPU, D7 lock)
//   9    uint8   #FE
//   10   4 x 5   per module: #7FFD, R0, R1, R2, R3
//   30   11 x 4 x uint16   AF, AF', BC, BC', DE, DE', HL, HL', IX, IY, IR
//   118  4 x uint8 IM, 4 x bool IFF1, 4 x bool IFF2
//   130  4 x uint16 PC, 4 x uint16 SP
//   146  per module: uint8 N, then N x (uint8 page index, 16384 bytes)
//
// See testdata/machines/zxpoly/README.md for the corpus and measurements.

/// endregion </Info>

/// region <Types>

/// State of one ZX-Poly CPU module as stored in a .zxp file
struct ZXPModuleState
{
    uint8_t port7FFD = 0;
    std::array<uint8_t, 4> reg{};   // Platform registers R0..R3

    uint16_t af = 0, afAlt = 0;
    uint16_t bc = 0, bcAlt = 0;
    uint16_t de = 0, deAlt = 0;
    uint16_t hl = 0, hlAlt = 0;
    uint16_t ix = 0, iy = 0;
    uint16_t ir = 0;
    uint8_t im = 0;
    bool iff1 = false;
    bool iff2 = false;
    uint16_t pc = 0;
    uint16_t sp = 0;

    std::array<bool, 8> pageUsed{};
    std::vector<uint8_t> pages;     // 8 x PAGE_SIZE, valid where pageUsed[i]
};

/// Whole .zxp file content after parsing
struct ZXPSnapshot
{
    static constexpr uint32_t MAGIC = 0xC0BA0100u;
    static constexpr size_t MODULE_COUNT = 4;
    static constexpr size_t HEADER_SIZE = 146;

    uint32_t flags = 0;
    uint8_t port3D00 = 0;
    uint8_t portFE = 0;
    std::array<ZXPModuleState, MODULE_COUNT> modules;

    uint8_t VideoMode() const { return (port3D00 >> 2) & 0x07u; }
    bool IsLocked() const { return (port3D00 & 0x80u) != 0; }
    bool AreSlavesRunning() const { return (port3D00 & 0x01u) != 0; }
    uint8_t MappedCPU() const { return (port3D00 >> 5) & 0x03u; }
};

/// endregion </Types>

/// Loads a ZX-Poly .zxp snapshot into four emulator instances (module i into
/// instance i). The instances must be 128K-compatible models (#7FFD paging):
/// every module of a .zxp is a 128K machine.
class LoaderZXP
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_LOADER;
    const uint16_t _SUBMODULE = PlatformLoaderSubmodulesEnum::SUBMODULE_LOADER_ZXP;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    ModuleLogger* _logger = nullptr;
    std::string _path;
    ZXPSnapshot _snapshot;
    bool _parsed = false;
    std::string _error;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderZXP(ModuleLogger* logger, const std::string& path);
    virtual ~LoaderZXP() = default;
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    /// Reads and validates the file. Does not touch any emulator
    bool Parse();

    /// Parses a .zxp image already in memory
    bool ParseBuffer(const uint8_t* data, size_t size);

    /// Applies the parsed snapshot: module i -> contexts[i]. Every context
    /// must be a model with 128K #7FFD paging
    bool Apply(const std::array<EmulatorContext*, ZXPSnapshot::MODULE_COUNT>& contexts);

    const ZXPSnapshot& GetSnapshot() const { return _snapshot; }

    /// One module of the parsed file as a SnapshotImage (a .zxp holds four machines, so four images; the group-level
    /// plan - master only and replicate, or refuse - comes with the ZX-Poly decision, proposal Q7). Nothing touches
    /// a machine; call after Parse()
    snapshot::Image BuildImage(size_t module) const;
    const std::string& GetError() const { return _error; }
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    bool ApplyModule(EmulatorContext* context, const ZXPModuleState& module, uint8_t portFE);
    bool Fail(const std::string& message);
    /// endregion </Helper methods>
};

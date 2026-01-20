#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

class EmulatorContext;
class ModuleLogger;

/// region <Info>

// See: https://worldofspectrum.org/faq/reference/formats.htm

/// endregion </Info>

/// region <Types>

enum SNA_MODE : uint8_t
{
    SNA_UNKNOWN = 0,
    SNA_48 = 1,
    SNA_128 = 2
};

#pragma pack (push, 1)  // Ensure compiler doesn't apply any re-ordering or alignments to structure

// Common for 48k and 128k snapshots header
// Location: bytes[0..26]
// Length:   27
// Note:     for 48k mode PC is pushed to stack
//           in 128k mode - PC is stored in extended header
typedef struct
{
    uint8_t i;
    uint8_t _l, _h, _e, _d, _c, _b, _f, _a;     // Alt Z80 registers set
    uint8_t l, h, e, d, c, b, ly, hy, lx, hx;   // Main Z80 registers set

    uint8_t flag19;                             // Interrupt (bit 2 contains IFF2, 1=EI / 0=DI)
    uint8_t r;

    uint8_t f, a;                               // AF
    uint8_t lsp, hsp;                           // SP

    uint8_t imod;                               // Current interrupt mode (0=IM0, 1=IM1, 2=IM2)
    uint8_t border;                             // Current border color (port #FE)
} snaHeader;

// Extended header for 128k snapshot
// Location: bytes[49179..49182]
// Length:   4
typedef struct
{
    uint16_t reg_PC;
    uint8_t port_7FFD;  // Determines current bank in #C000 - #FFFF bank
    uint8_t is_TRDOS;
} sna128Header;

#pragma pack (pop)      // Restore default compiler behavior for structures

struct SNAHeader
{
    uint8_t reg_I;

    uint16_t reg_HL1;
    uint16_t reg_DE1;
    uint16_t reg_BC1;
    uint16_t reg_AF1;

    uint16_t reg_HL;
    uint16_t reg_DE;
    uint16_t reg_BC;
    uint16_t reg_IY;
    uint16_t reg_IX;

    uint8_t reg_R;
    uint16_t reg_AF;
    uint16_t reg_SP;
    uint8_t intMode;
    uint8_t borderColor;

    uint8_t ramDump48k[49152];
};

struct SNA128Header : public SNAHeader
{
    uint16_t reg_PC;
    uint8_t port_7FFD;  // Determines current bank in #C000 - #FFFF bank
    uint8_t is_TRDOS;
};

/// endregion </Types>

class LoaderSNA
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_LOADER;
    const uint16_t _SUBMODULE = PlatformLoaderSubmodulesEnum::SUBMODULE_LOADER_SNA;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Constants>
protected:
    const size_t _snaHeaderSize = sizeof(snaHeader);
    const size_t _sna128HeaderSize = _snaHeaderSize + sizeof(sna128Header);
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    std::string _path;
    FILE* _file = nullptr;
    bool _fileValidated = false;
    size_t _fileSize = 0;
    SNA_MODE _snapshotMode = SNA_UNKNOWN;
    bool _stagingLoaded = false;

    // Staging memory buffers
    snaHeader _header;
    sna128Header _ext128Header;
    uint8_t _memoryPages[8][PAGE_SIZE];
    bool _memoryPagesUsed[8];
    uint8_t _borderColor;

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderSNA(EmulatorContext* context, const std::string& path);
    virtual ~LoaderSNA();
    /// region </Constructors / destructors>

    /// region <Methods>
public:
    bool load();
    bool save();
    /// endregion </Methods

    /// region <Helper methods>
protected:
    bool validate();

    bool is48kSnapshot(FILE* file);
    bool is128kSnapshot(FILE* file);

    bool loadToStaging();
    bool load48kToStaging();
    bool load128kToStaging();
    bool applySnapshotFromStaging();
    
    // Save helpers
    SNA_MODE determineOutputFormat();
    bool captureStateToStaging();
    bool save48kFromStaging();
    bool save128kFromStaging();
    bool isPageEmpty(int pageNum);
    /// endregion </Helper methods>

    /// region <Debug methods>
public:
    std::string dumpSnapshotInfo();
    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderSNACUT : public LoaderSNA
{
public:
    LoaderSNACUT(EmulatorContext* context, std::string path) : LoaderSNA(context, path) {};

public:
    using LoaderSNA::_context;
    using LoaderSNA::_logger;
    using LoaderSNA::_path;
    using LoaderSNA::_file;
    using LoaderSNA::_fileValidated;
    using LoaderSNA::_fileSize;
    using LoaderSNA::_snapshotMode;
    using LoaderSNA::_stagingLoaded;

    using LoaderSNA::_header;
    using LoaderSNA::_ext128Header;
    using LoaderSNA::_memoryPages;
    using LoaderSNA::_memoryPagesUsed;

    using LoaderSNA::validate;

    using LoaderSNA::is48kSnapshot;
    using LoaderSNA::is128kSnapshot;

    using LoaderSNA::loadToStaging;
    using LoaderSNA::load48kToStaging;
    using LoaderSNA::load128kToStaging;
    using LoaderSNA::applySnapshotFromStaging;
    
    // Save helpers
    using LoaderSNA::determineOutputFormat;
    using LoaderSNA::captureStateToStaging;
    using LoaderSNA::save48kFromStaging;
    using LoaderSNA::save128kFromStaging;
    using LoaderSNA::isPageEmpty;
};
#endif // _CODE_UNDER_TEST
#pragma once

#ifndef UNREAL_LOADER_TZX_H
#define UNREAL_LOADER_TZX_H

#include "stdafx.h"
#include "emulator/io/tape/tapetypes.h"
#include "loaders/tape/loader_tape.h"

/// region <Documentation>

/// region <TZX format>
// @see TZX spec 1.20/1.21 (authoritative framing implemented here):
//      http://worldofspectrum.net/TZXformat.html
// @see in-tree transcription: docs/file-formats/tape-images/tzx-tape.md
// @see libspectrum tzx_read.c (behavioral reference; block enum == file IDs)
// @see ZOT tzx.c: https://github.com/antirez/ZOT
//
//    10-byte header: "ZXTape!" 0x1A major minor. All multi-byte values little-endian.
//
//    Layouts settled against the in-tree spec + libspectrum + ZOT (design
//    checkpoints C1/C2 closed — tzx-loader-design.md's u32-pilot and
//    $26-select readings matched none of the three):
//      $11 turbo   18-byte header, pilot half-period u16 (not u32)
//      $13 pulses  count is u8 (1-255)
//      $26 call    [count:2][offsets:2s x N] — no length prefix
//      $27 return  empty body
//      $28 select  [length:2][count:1]{ [offset:2s][descLen:1][text] }
//      $18/$19/$2A/$2B carry a u32 length prefix; $2B body = [u32 = 1][level:1]
//      $30 text [len:1][text]; $32 archive [len:2][count:1]{triples};
//      $33 hardware [count:1]{triples} — no prefix; $35 custom [ASCII:16][len:4][data]
//      $5A glue    ID + 9 bytes "XTape!" 0x1A major minor
//
//    Jump/call/select offsets are signed and relative to the block carrying
//    them: +1 = next block (same convention for $23/$26/$28).
//
//    Implemented per docs/inprogress/2026-09-01-tape-manager/tzx-loader-design.md:
//      D1 version policy (major==1, minor>0x15 warns — the minor byte is the
//      plain decimal number: 1.20=0x14, 1.21=0x15), D2 1.20 framing only,
//      D3 unknown ID stops the scan with a warning, C3 loop count = total
//      executions, linearization budget 4096 with linear-tail degradation.

/// endregion </TZX format>

/// endregion </Documentation>

class EmulatorContext;
class ModuleLogger;

/// region <Linearization budget>

// Design §5.6: the flattened block count a single image may reach before the
// linearizer gives up and switches to linear-tail mode (remaining blocks in
// file order, control operations inert, controlFlowLinearized = false).
constexpr size_t TAPE_LINEARIZE_MAX_BLOCKS = 4096;

/// endregion </Linearization budget>

class LoaderTZX : public LoaderTapeBase
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    // Legacy file-bound fields (validateFile path only — the contract's Load()
    // never touches the filesystem)
    std::string _path;
    FILE* _file = nullptr;
    bool _fileValidated = false;
    size_t _fileSize = 0;
    void* _buffer = nullptr;

    /// endregion </Fields>

    /// region <Parsed entry (file order)>
protected:
    /// One block as it sits in the file, before linearization. Only the
    /// fields the block ID implies are meaningful.
    struct TzxRawEntry
    {
        uint8_t id = 0;
        size_t fileOffset = 0;

        std::vector<uint8_t> data;      // $10/$11/$14 payload ($14: raw bits, no flag)
        std::vector<uint32_t> pulses;   // $12/$13/$15/$18 rendered edges

        TapeTimingProfile timing;       // $11/$14: useTiming; others: pauseMs only
        bool useTiming = false;

        int32_t jumpOffset = 0;                 // $23
        uint16_t loopCount = 0;                 // $24 — TOTAL body executions (C3)
        std::vector<int32_t> flowOffsets;       // $26 callees / $28 selections (relative to this block)
        std::vector<std::string> flowTexts;     // $28 selection labels (warning listing)

        uint8_t level = 0;                      // $2B signal level (0 = low, 1 = high)

        std::string text;                       // $21 group name, $30/$31 text, $32 title
        bool unplayable = false;                // $19, zlib-compressed $18
    };

    /// Emission context carried across the linearized stream (design §5.6):
    /// $21 group labels, $2B polarity and $20 <= 5 ms merges resolve HERE, in
    /// output order — so a block duplicated by a loop body inherits its own
    /// context per iteration.
    struct TzxEmitContext
    {
        std::string pendingGroupLabel;
        bool hasPendingGroupLabel = false;

        bool pendingInvertedLevel = false;
        bool hasPendingInvertedLevel = false;

        size_t lastPlayableIndex = SIZE_MAX;    // image.blocks index — $20 <= 5 ms merge target
    };

    /// endregion </Parsed entry (file order)>

    /// region <Constructors / destructors>
public:
    /// Context-free construction for the loader registry (design §5.3).
    LoaderTZX();

    /// Legacy context- and path-bound construction (existing tests, CUT wrapper).
    LoaderTZX(EmulatorContext* context, std::string path);
    virtual ~LoaderTZX();
    /// endregion </Constructors / destructors>

    /// region <LoaderTapeBase contract>
public:
    TapeImage Load(std::span<const uint8_t> bytes, const std::string& sourceName) override;
    const TapeFormatInfo& Format() const override;
    /// endregion </LoaderTapeBase contract>

    /// region <Methods>
public:
    /// Magic probe: "ZXTape!" + 0x1A answers 100, anything else 0 (design §5.3 —
    /// magic-bearing formats are binary, never graded).
    static int Probe(std::span<const uint8_t> bytes);
    /// endregion </Methods>

    /// region <Parse helpers>
protected:
    bool ParseHeaderAndScan(std::span<const uint8_t> bytes, const std::string& sourceName,
                            std::vector<TzxRawEntry>& entries, TapeImage& image);
    bool ScanOneBlock(std::span<const uint8_t> bytes, size_t& offset, TzxRawEntry& entry, TapeImage& image);
    void Linearize(const std::vector<TzxRawEntry>& entries, TapeImage& image);
    void EmitEntry(const TzxRawEntry& entry, TapeImage& image, TzxEmitContext& context);

    static void AppendDirectRecordingRuns(const uint8_t* samples, size_t bitCount, uint16_t tstatesPerSample,
                                          std::vector<uint32_t>& pulses);
    static bool DecodeCswRle16(const uint8_t* data, size_t dataLen, uint32_t pulseCount,
                               std::vector<uint32_t>& pulses);

    /// endregion </Parse helpers>

    /// region <Legacy file validation (existing test coverage)>
protected:
    bool validateFile();
    void parseHardware(uint8_t* data);
    /// endregion </Legacy file validation (existing test coverage)>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderTZXCUT : public LoaderTZX
{
public:
    LoaderTZXCUT(EmulatorContext* context, std::string path) : LoaderTZX(context, path) {};
    LoaderTZXCUT() : LoaderTZX() {};

public:
    using LoaderTZX::_context;
    using LoaderTZX::_logger;
    using LoaderTZX::_path;
    using LoaderTZX::_file;

    using LoaderTZX::validateFile;
    using LoaderTZX::parseHardware;
    using LoaderTZX::Load;
    using LoaderTZX::ParseHeaderAndScan;
    using LoaderTZX::ScanOneBlock;
    using LoaderTZX::Linearize;
    using LoaderTZX::EmitEntry;
};
#endif // _CODE_UNDER_TEST


#endif //UNREAL_LOADER_TZX_H

#pragma once

/// @file ttdregion.h
/// @brief Memory regions of the time-travel engine (engine decisions D3, D27).
///
/// A region is a block of emulated memory the engine tracks in 4 KB pieces:
/// machine RAM (region 0) or memory a device owns. Region ids are their own
/// stable table, stored in files: appended, never reused; the first change to
/// reach master takes the next free number. A region's size has no fixed cap.
/// Design: docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.4.

#include <cstdint>
#include <functional>
#include <string>

namespace ttd
{

enum class TTDRegionId : uint16_t
{
    MachineRam = 0,
    GeneralSoundRam = 1,
    GeneralSoundUploadStore = 2,
    MoonSoundWaveMemory = 3,
    NeoGSRam = 4,
    NeoGSFlash = 5,
    SprinterVideoRam = 6,
    SprinterFastRam = 7,
    Vdac2GraphicsMemory = 8,
    Vdac2DisplayList = 9,
    Vdac2Registers = 10,
    Vdac2CommandFifo = 11,
    EvoAvrEeprom = 12,
    SmucEeprom = 13,
};

/// Size of one piece of memory, the unit the engine stores
constexpr uint32_t kTTDPieceSize = 4096;

/// Restores one piece through its device instead of copying it (memory where
/// a write has side effects or words are computed, e.g. VDAC2 registers)
using TTDPieceRestoreFn = std::function<void(uint32_t piece, const uint8_t* bytes)>;
/// Called after a region was restored, so its device rebuilds derived caches
using TTDRegionRestoredFn = std::function<void()>;

struct TTDRegionDesc
{
    TTDRegionId id = TTDRegionId::MachineRam;
    std::string name;                       ///< "ram", "neogs.ram", ...
    uint16_t ownerType = kNoOwner;          ///< owning device's type id (Phase 2 registry); kNoOwner for machine RAM
    std::string ownerInstance;              ///< e.g. "ngs0"; empty for machine RAM
    uint8_t* memory = nullptr;              ///< live memory, owned by its device (unused when fed from a file)
    uint32_t pieces = 0;                    ///< capacity in 4 KB pieces
    uint32_t bytes = 0;                     ///< real size; the last piece may be partial
    uint32_t dirtyGranularity = kTTDPieceSize;  ///< bytes per dirty bit: 16 KB for machine RAM, 4 KB for devices
    uint32_t blockPieces = 0;               ///< reference-table block size in pieces; 0 = by region size
    TTDPieceRestoreFn restorePiece;         ///< optional: restore through the device
    TTDRegionRestoredFn onRestored;         ///< optional: after-restore call

    static constexpr uint16_t kNoOwner = 0xFFFF;
};

}  // namespace ttd

#pragma once

/// @file ttdcddrive.h
/// @brief TTD serializer for the IDE board's ATAPI CD drives, beyond the task
/// file and transfer the AtaChannel blob carries: the CD audio state (head,
/// play range, status, page 0Eh volume and routing) and the READ CD sector
/// waiting for the 2048-byte data buffer. Registered only when a unit of the
/// board is a CD drive, so the blobs of every machine without one stay as they
/// were.
///
/// The disc is media, not here: the media set is fixed while a recording runs
/// (integration-ttd-snapshots.md §2), and the audio a replay plays is read from
/// the same image at the same head position (sealed replay). The blob carries the
/// disc's identity (CdImage::ContentId: an image's path and layout, an audio CD
/// folder's file names and bytes): a restore onto another disc logs a warning and
/// counts it (DiscMismatches).

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDCdDrive : public TTDSerializable
{
public:
    /// v2 (2026-10-02): the disc's identity per unit. A v1 blob is a size mismatch (not restored)
    static constexpr uint8_t kVersion = 2;

    explicit TTDCdDrive(EmulatorContext* context);

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "CdDrive"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::CdDrive; }
    uint64_t TTDHashState() const override;

    /// Units whose recorded disc identity differed from the inserted disc's on TTDLoadState
    /// (each logged as a warning; the state is restored anyway)
    uint32_t DiscMismatches() const { return _discMismatches; }

private:
    EmulatorContext* _context = nullptr;
    int _units = 0;  ///< fixed at registration: the board's unit count
    uint32_t _discMismatches = 0;
};

}  // namespace ttd

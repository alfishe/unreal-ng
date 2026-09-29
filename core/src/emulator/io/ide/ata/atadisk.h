#pragma once

/// @file atadisk.h
/// @brief An ATA hard disk: the command set of IDE design §6.3 over any
/// IBlockDevice. PIO only, instant (no BSY window, like every reference
/// emulator). CHS, LBA28 and LBA48 addressing; after each sector the address
/// registers point at the next one (UnrealSpeccy `update_regs`).
///
/// Worked example (CHS, geometry 16 heads × 63 sectors): cylinder 2, head 3,
/// sector 5 is LBA (2 × 16 + 3) × 63 + 4 = 2209.

#include <optional>

#include "emulator/io/ide/ata/atadevice.h"

class AtaDisk : public AtaDevice
{
public:
    AtaDisk();

    bool IsPresent() const override { return _medium != nullptr; }

    /// The geometry the disk reports: the config's, else the image header's,
    /// else the largest standard CHS for the size (up to 16383 / 16 / 63)
    BlockGeometry DefaultGeometry() const;
    static BlockGeometry GeometryFor(uint64_t sectors);

    /// The geometry a Profi disk was formatted with (IDE design §8.3): the
    /// ProfiHiDD header at the first sector of cylinder 1 - LBA 256 for the
    /// SYS ROM's 16 x 16, LBA 1008 for Karabas's 16 x 63 - carries H and S as
    /// big-endian words, and "ProfiHiDD" byte-swapped ("rPfoHiDD") at offset 16
    static std::optional<BlockGeometry> DetectProfiGeometry(IBlockDevice& medium);

    /// The 512-byte IDENTIFY DEVICE answer for the current state
    void BuildIdentify(uint8_t* out) const;

protected:
    void ExecuteCommand(uint8_t command) override;
    void DataInDone() override;
    void DataOutDone() override;
    void SetSignature() override;
    uint8_t ReadyStatus() const override;
    /// A new disk on the bus: power-on state, geometry from the new medium
    void MediumChanged() override;

private:
    std::optional<BlockGeometry> _detected;  ///< found on the medium when it came (Profi)

    uint64_t Capacity() const { return _medium ? _medium->SectorCount() : 0; }
    bool WriteAllowed() const;
    void RestoreTranslation();

    /// The start address and count of a read / write / verify command from
    /// the task file; false (and the command aborted) when it is invalid
    bool DecodeTransfer(bool ext, uint64_t& lba, uint32_t& count);
    /// The address registers point at `lba` (and the count at what is left)
    void StoreAddress(uint64_t lba, uint32_t left);

    void StartRead(bool multiple, bool ext);
    void StartWrite(bool multiple, bool ext);
    void StartVerify(bool ext);
    void ReadIntoBuffer(bool interrupt);
    bool IsReadCommand() const;
    bool IsWriteCommand() const;
    uint8_t BlockSize() const;
};

#pragma once

/// @file atadevice.h
/// @brief One unit on an IDE channel: the register file and the data transfer
/// engine every ATA device shares, and the hooks its command set fills in
/// (AtaDisk: hard disk; AtapiCdrom: CD-ROM drive).
///
/// The unit never owns its medium: the media manager does, and the unit's
/// slot attaches it (implementation-plan.md D1). All state that changes while
/// the machine runs is in one POD struct (AtaDeviceState) for TTD.
/// Design: docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md §6.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

#include "emulator/io/ide/ata/ataregisters.h"
#include "emulator/io/storage/iblockdevice.h"

enum class AtaDeviceKind : uint8_t
{
    Disk = 1,
    Cdrom = 2
};

/// How a unit presents its medium (from the config or the image header)
struct DriveConfig
{
    BlockGeometry geometry;     ///< all zero: from the image header, else the largest standard CHS
    bool writeProtect = false;  ///< the drive's jumper: writes fail with ABRT
    std::string model;          ///< IDENTIFY model string; empty: "UNREAL-NG HDD" / "UNREAL-NG CD-ROM"
    std::string serial;         ///< IDENTIFY serial; empty: derived from the medium
    /// Profi board: without `geometry`, take it from the disk's ProfiHiDD
    /// header, else the SYS ROM's format (16 heads x 16 sectors)
    bool profiGeometry = false;
};

enum class AtaPhase : uint8_t
{
    Idle = 0,
    DataIn = 1,      ///< the host reads the buffer
    DataOut = 2,     ///< the host fills the buffer
    PacketCommand = 3 ///< ATAPI: the host writes the 12-byte command packet
};

/// Everything that changes while the machine runs, for TTD and snapshots.
/// Fields are ordered by size so the struct has no padding (checked below):
/// its bytes are its value
struct AtaDeviceState
{
    uint64_t lba = 0;            ///< next sector (disk) / next 2048-byte block (CD)
    uint32_t sectorsLeft = 0;    ///< sectors (disk) / blocks (CD) still to move in this command
    uint32_t cylinders = 0;      ///< current CHS translation
    uint32_t transferLeft = 0;   ///< ATAPI: bytes still to move in this command
    uint16_t bufferPos = 0;      ///< bytes of the buffer the host has moved
    uint16_t bufferLen = 0;      ///< bytes in the buffer for this DRQ block
    uint16_t heads = 0;          ///< current CHS translation (INITIALIZE DEVICE PARAMETERS)
    uint16_t sectors = 0;
    uint16_t byteLimit = 0;      ///< ATAPI: the host's byte count limit for one DRQ block
    uint16_t bufferFill = 0;     ///< ATAPI: bytes of the buffer still to hand over

    // Task file as the host wrote it, and the previous values (LBA48 "HOB")
    uint8_t features = 0;
    uint8_t sectorCount = 0;
    uint8_t lbaLow = 0;
    uint8_t lbaMid = 0;
    uint8_t lbaHigh = 0;
    uint8_t device = 0;
    uint8_t hobFeatures = 0;
    uint8_t hobSectorCount = 0;
    uint8_t hobLbaLow = 0;
    uint8_t hobLbaMid = 0;
    uint8_t hobLbaHigh = 0;
    uint8_t status = 0;
    uint8_t error = 0;
    uint8_t control = 0;         ///< device control as last written
    uint8_t command = 0;         ///< the command in progress
    uint8_t phase = 0;           ///< AtaPhase
    uint8_t intrq = 0;           ///< the device's interrupt pending (before nIEN)
    uint8_t multiple = 0;        ///< SET MULTIPLE block size; 0: multiple commands disabled
    uint8_t blockLeft = 0;       ///< sectors left in the current DRQ block (multiple commands)
    uint8_t lba48 = 0;           ///< the command uses 48-bit addresses
    uint8_t senseKey = 0;        ///< ATAPI sense
    uint8_t asc = 0;
    uint8_t ascq = 0;
    uint8_t unitAttention = 0;   ///< ATAPI: a disc change not reported yet
    uint8_t reserved8[4] = {};
    uint8_t cdb[12] = {};        ///< ATAPI command packet
    uint8_t buffer[2048] = {};   ///< one sector (disk) or one CD block
};
static_assert(std::has_unique_object_representations_v<AtaDeviceState>,
              "AtaDeviceState must have no padding: its bytes are the TTD blob");

class AtaDevice
{
public:
    static constexpr uint16_t kSectorSize = 512;

    explicit AtaDevice(AtaDeviceKind kind);
    virtual ~AtaDevice() = default;

    AtaDevice(const AtaDevice&) = delete;
    AtaDevice& operator=(const AtaDevice&) = delete;

    AtaDeviceKind Kind() const { return _kind; }

    /// region <Medium>
    /// A disk: the unit is on the bus from now on. A CD: the disc is in
    void AttachMedium(IBlockDevice& medium, const DriveConfig& config);
    void DetachMedium();
    IBlockDevice* Medium() const { return _medium; }
    const DriveConfig& Config() const { return _config; }
    /// On the bus: a disk with a medium, a CD drive always
    virtual bool IsPresent() const = 0;
    /// endregion </Medium>

    /// region <Bus>
    /// Registers 1..6 and Control are written to every unit on the channel;
    /// StatusCommand only to the selected one (AtaChannel's job)
    void WriteRegister(uint8_t reg, uint8_t value);
    uint8_t ReadRegister(uint8_t reg);
    uint16_t ReadData();
    void WriteData(uint16_t word);
    /// The interrupt line, after nIEN
    bool Intrq() const;
    /// Power-on / the machine's reset line
    void HardReset();
    /// EXECUTE DEVICE DIAGNOSTIC: every unit on the channel runs it, whichever is selected
    void RunDiagnostic(bool interrupt);
    /// bit 4 of the device register
    uint8_t DeviceBit() const { return (_s.device & ata::DeviceBits::DEV) ? 1 : 0; }
    /// endregion </Bus>

    /// region <State>
    const AtaDeviceState& State() const { return _s; }
    void SetState(const AtaDeviceState& state) { _s = state; }
    /// endregion </State>

    /// Called after every sector the guest writes to the medium (a TTD replay barrier)
    void SetWriteListener(std::function<void()> listener) { _onWrite = std::move(listener); }

    /// The slot's write-protect switch: WRITE commands abort while it is on.
    /// Any thread (the API thread flips it while the machine runs)
    void SetWriteProtectSwitch(bool on) { _protectSwitch.store(on, std::memory_order_relaxed); }
    /// Where to count the blocks moved through the data register (activity
    /// LEDs; host-side, not machine state). nullptr: not counted
    void SetActivityCounter(std::atomic<uint64_t>* counter) { _activity = counter; }

protected:
    /// region <Command set hooks>
    virtual void ExecuteCommand(uint8_t command) = 0;
    /// The host read the whole buffer
    virtual void DataInDone() = 0;
    /// The host filled the whole buffer
    virtual void DataOutDone() = 0;
    /// The task file after a reset or EXECUTE DEVICE DIAGNOSTIC
    virtual void SetSignature() = 0;
    /// The status a command ends with
    virtual uint8_t ReadyStatus() const = 0;
    /// The status a reset ends with (an ATAPI device: 0)
    virtual uint8_t ResetStatus() const { return ReadyStatus(); }
    /// A new medium came (the CD raises unit attention)
    virtual void MediumChanged() {}
    /// HardReset ran (power-on, the reset line, DEVICE RESET): state outside AtaDeviceState
    virtual void PowerOnReset() {}
    /// endregion </Command set hooks>

    /// region <Helpers for the command sets>
    void Complete();                              ///< success: ready, INTRQ
    void Abort(uint8_t errorBits = ata::Error::ABRT); ///< ERR + error bits, INTRQ
    /// A block of `length` bytes waits in the buffer for the host
    void StartDataIn(uint16_t length, bool interrupt = true);
    /// The host is to send `length` bytes
    void StartDataOut(uint16_t length, bool interrupt = false);
    void NotifyWrite();
    /// endregion </Helpers for the command sets>

    AtaDeviceState _s;
    IBlockDevice* _medium = nullptr;
    DriveConfig _config;

private:
    void SoftResetDone();

    AtaDeviceKind _kind;
    std::function<void()> _onWrite;
    std::atomic<uint64_t>* _activity = nullptr;

protected:
    std::atomic<bool> _protectSwitch{false};
};

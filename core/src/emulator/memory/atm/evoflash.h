#pragma once

/// @file evoflash.h
/// @brief The ZX-Evo's ROM chip as a flash memory: CPU writes reach it (TS-Conf and the ATM3 / BaseConf firmware).
///
/// The board's ROM is a 512 KB 29F040 NOR flash, PLCC32 (D3; schematic `pcad/rev_c|rev_d/zxevo.sch`,
/// datasheets `docs/Chips/am29f040b.pdf`, `m29f040b.pdf` in the pentevo tree). The FPGA drives its address
/// A13..A0 from the Z80, A18..A14 from the window's ROM page (`rompg[4:0]`), /OE from the read strobe and /WE from
/// the write strobe when the firmware lets writes through:
///
/// | firmware | /WE asserted for a write when | RTL |
/// |:--|:--|:--|
/// | TS-Conf | `MEM_CONFIG.W0_WE` = 1 and window 0 shows ROM (not `W0_RAM`, not vdos) | `fpga/current/z80/zmem.v:67-81,294-298` |
/// | BaseConf (ATM3) | port `#BF` bit 1 (`romrw_en`) = 1, the window shows ROM and its `#xBF7` write protection is off | `fpga/base/z80/zmem.v:186-196`, `zports.v:841-857`, `mem/atm_pager.v:114-186` |
///
/// The chip itself is Flash29F040B (`emulator/io/flash/`), working on the machine's ROM memory (Memory::ROMBase(),
/// the first 512 KB): a programmed or erased byte is what the CPU reads from that ROM page afterwards. While the chip
/// answers status instead of array data (a program or erase running, autoselect / ID mode, a failed operation) every
/// CPU read from a ROM window returns what the chip drives, as on the board.
///
/// Cost: this object is a host bus overlay installed only while a window lets writes through or the chip is not in
/// read-array mode (`Sync`). Otherwise the CPU's memory path does not see it at all; the decoders tell it the write
/// windows when they map the banks (port writes, not per access).
///
/// Time: the chip counts in the machine's base clock t-states (EvoAvrWait::BaseNow, the frame-start t-state count
/// plus the current t-state at the base clock). A reset restarts that count at 0, so the decoder's reset finishes a
/// running operation at once (the chip has no reset pin and would have finished it within its own time anyway).
///
/// Persistence (owner decision 2026-10-06): what the chip programs is saved to a file of its own per machine, never
/// to the shipped ROM image: `<writable folder>/zxevo-flash-<machine>-<SHA-256 of the loaded ROM image>.rom`, the
/// whole 512 KB array (machine = `tsconf` or `atm3`; the folder is FileHelper::GetWritablePath(), where NeoGS keeps
/// its reprogrammed flash). When the ROM image is loaded the file with that image's hash, if any, replaces the array
/// (a flashed board stays flashed). A file of the same machine made from another ROM image is not used (a new
/// zxevo.rom is never mixed with an old flash) and is reported. The file is written once the chip has been quiet for
/// kSaveQuietFrames frames after a program / erase, and when the machine goes away; never while a TTD replay owns
/// the machine (inside a session TTD is the source of truth). Discard deletes the file; the shipped image comes back
/// with the next ROM load (a reset). docs/inprogress/2026-09-27-tsconf/tdd-evo-flash.md section 6.
///
/// TTD: the chip's command state is the blob EvoFlash (PeripheralId 61, TTDEvoFlash); the 512 KB array is the engine
/// region EvoFlash (18), the pieces marked by the chip's programs and erases.

#include <cstdint>
#include <vector>

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "emulator/io/flash/flash29f040b.h"
#include "emulator/memory/hostbusoverlay.h"

class EmulatorContext;
class Memory;

class EvoFlash final : public HostBusOverlay, public ttd::ITTDRegionSource
{
public:
    /// The board's chip. The pentevo tree carries the AMD Am29F040B datasheet next to the ST one; the AMD part
    /// reports manufacturer #01, device #A4 (evoflash.com knows both)
    static constexpr Flash29F040B::Vendor kVendor = Flash29F040B::Vendor::AMD;
    static constexpr uint32_t kPageSize = 0x4000;
    static constexpr uint32_t kPages = Flash29F040B::SIZE / kPageSize;   ///< 32: rompg[4:0]

    /// @param memory the machine's memory; the chip works on its ROM pages (null: the chip's own array, tests)
    /// @param machine the persistence file's machine part: "tsconf" or "atm3"
    EvoFlash(EmulatorContext* context, Memory* memory, const char* machine);
    /// The owning decoder removes the overlay in its destructor (Core::RemoveBusOverlay), as for its other overlays
    ~EvoFlash() override;

    EvoFlash(const EvoFlash&) = delete;
    EvoFlash& operator=(const EvoFlash&) = delete;

    Flash29F040B& Chip() { return _chip; }
    const Flash29F040B& Chip() const { return _chip; }

    /// Bit n set: a CPU write to window n reaches the chip (the decoder's /WE rule; that window must show ROM).
    /// Called by the decoders whenever they map the banks; cheap when nothing changed
    void SetWriteWindows(uint8_t mask);
    uint8_t WriteWindows() const { return _writeWindows; }

    /// Install the overlay while it has work (a write window, or the chip answering status), remove it otherwise.
    /// Also at the frame end: an operation that finished since is noticed there
    void Sync();
    bool IsInstalled() const { return _installed; }

    /// A Z80 reset: the time base restarts at 0, so a running operation completes now (see the file comment)
    void OnMachineReset();

    /// The chip's time now (base clock t-states)
    int64_t Now() const;

    /// region <Persistence>
    /// Frames without a program / erase before a change is saved (50 frames: about one second)
    static constexpr uint32_t kSaveQuietFrames = 50;
    static constexpr const char* kFilePrefix = "zxevo-flash-";

    /// The ROM loader has put a new image into the ROM pages (@p imageBytes of it from the file): its hash names
    /// the persistence file, which replaces the array when it exists
    void OnRomImageLoaded(size_t imageBytes);
    /// The frame ended: removes the overlay after an operation (Sync) and saves a change once the chip is quiet
    void OnFrameEnd();
    /// Write the array to the persistence file now (false: no file name yet, a replay owns the machine, or the
    /// write failed). Not while the chip works: then the save waits for the frame end
    bool Save();
    /// The machine goes away: save what is not saved yet
    void SaveIfUnsaved();
    /// Delete the persistence file; nothing is saved until the chip changes again. The ROM pages keep their bytes
    /// until the next ROM load (the decoder's discard requests one at the next reset)
    bool Discard();
    /// Where the persistence files go (tests); empty: FileHelper::GetWritablePath()
    void SetPersistFolder(const std::string& folder) { _persistFolder = folder; }
    /// Process-wide switch, on by default. core-tests turn it off in main() (a test that flashes must not change
    /// the ROM later tests boot) and on in the tests of the persistence itself. Read at a ROM load
    static void SetPersistenceAllowed(bool allowed);
    static bool PersistenceAllowed();

    struct PersistStatus
    {
        std::string machine;
        std::string path;           ///< the persistence file for the loaded ROM image (empty before a ROM load)
        std::string baseDigest;     ///< SHA-256 of the loaded ROM image
        bool fileExists = false;
        bool loadedFromFile = false;  ///< the array came from the persistence file at the last ROM load
        bool unsaved = false;         ///< changed since the last save / load / discard
        uint32_t changes = 0;         ///< programs and erases completed since the machine started
        std::vector<std::string> otherImageFiles;  ///< files of this machine made from other ROM images (not used)
    };
    PersistStatus Status() const;
    /// endregion </Persistence>

    /// region <HostBusOverlay>
    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;
    /// endregion </HostBusOverlay>

    /// region <TTD>
    static constexpr uint8_t kStateVersion = 1;
    static constexpr size_t kStateSize = 1 + Flash29F040B::STATE_SIZE;   ///< version + the chip's state
    void SaveState(uint8_t* dst) const;
    /// Restores the chip's state; the overlay follows (Sync)
    void LoadState(const uint8_t* src);

    void TTDRegions(std::vector<ttd::TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool on) override;
    /// endregion </TTD>

private:
    /// Byte offset in the chip of a CPU address in a ROM window; false when the window shows no ROM page the chip
    /// holds (RAM, or a ROM image larger than the chip)
    bool ChipOffset(uint16_t addr, uint32_t& offset) const;

    std::string Folder() const;
    bool ReplayOwnsMachine() const;

    EmulatorContext* _context;
    Memory* _memory;
    std::string _machine;
    std::string _persistFolder;
    std::string _path;
    std::string _baseDigest;
    bool _loadedFromFile = false;
    bool _unsaved = false;
    uint32_t _seenChanges = 0;
    uint32_t _quietFrames = 0;
    Flash29F040B _chip;
    ttd::TTDRegionTracker _tracker;
    uint8_t _writeWindows = 0;
    bool _installed = false;
};

#pragma once

/// @file p2666betadisk.h
/// @brief The Pentagon 2.666 LE's Beta Disk emulator (common/betadisk.vhd, wired at PaE.vhd:618-625, 1330-1337,
/// 1530-1534, 1609-1625; hardware-reference.md §5.6): a WD1793 imitation in the FPGA that reads and writes the RAM
/// disk in SRAM pages #4E..#76 instead of a floppy. The board as built (owner decision 2026-11-11): every quirk of
/// the VHDL is copied, the fixes stay as commented `P2666-FIX` blocks.
///
/// - Drive A only: a command, track, sector or data write with system bits 1..0 /= 00 is lost; the registers still
///   read.
/// - Image (betadisk.vhd:520-531): SRAM byte `'1' & (#36 - pos(6..1)) & pos(0) & NOT system.4 & (sector - 1)(3..0) &
///   byte`, i.e. page #76 - cylinder / 2, the TRD order stored downwards.
/// - Time (mainclk edges, 114 MHz): a byte every 127 edges after the CPU took the last one (plus the RAM slot the
///   controller waits for, a few edges: not modelled, nor the CPU clock it stops, vg_tormoz); type I commands end
///   127 edges after the command, restore 0x3D40000 edges (0.56 s) after it; the index pulse 4 ms in 200 ms.
/// - Status (betadisk.vhd:169-177): never not ready, write protect, record not found, CRC or lost data; bit 5 and
///   bit 2 (track 0) only while a type I command runs; bit 1 is DRQ while a read / write / read address flag is
///   set, else the index pulse; the read and write flags stay set after their command ends, until the next type I
///   command or force interrupt.

#include <array>
#include <cstdint>
#include <vector>

class P2666BetaDisk
{
public:
    static constexpr uint64_t kByteEdges = 127;           ///< read_delay / write_delay / radr_delay: 7 bits of ones
    static constexpr uint64_t kTypeIEdges = 127;          ///< delay_vg(6 downto 0) = 1111111
    static constexpr uint64_t kRestoreEdges = 0x3D40000;  ///< delay_vg = 0b00011110101 << 18
    static constexpr uint64_t kIndexPeriod = 0x15B94A3 + 1;
    static constexpr uint64_t kIndexOn = 0x154A106;
    static constexpr uint64_t kIndexOff = 0x15B94A2;
    static constexpr uint8_t kFirstPage = 0x4E;  ///< the RAM disk's lowest page (cylinder 79 and up)
    static constexpr uint8_t kLastPage = 0x76;   ///< cylinder 0 / 1

    /// The board's SRAM, page by page (16 KB): the decoder hands the emulator's RAM page lookup
    using PageResolver = uint8_t* (*)(void* owner, uint8_t page);
    void SetMemory(void* owner, PageResolver resolver)
    {
        _owner = owner;
        _resolver = resolver;
    }

    /// The MCU's power-on (hardware_reset): every register to its reset value, the system register stays
    void PowerOn(uint64_t edge);

    /// region <Ports (DOS on, A1 = A0 = 1)>
    /// A7 = 0: register A6..5 (0 status / command, 1 track, 2 sector, 3 data). The read mux answers any odd port
    /// (PaE.vhd:1530-1534), but only vg93_cs (A1 = A0 = 1) clears INTRQ / takes the data byte: `strobe` = false
    /// for the other odd ports
    uint8_t ReadRegister(uint8_t reg, uint64_t edge, bool strobe = true);
    void WriteRegister(uint8_t reg, uint8_t value, uint64_t edge);
    /// A7 = 1 read: INTRQ, DRQ, 111111
    uint8_t ReadSystem(uint64_t edge);
    /// A7 = 1 write: betadisk_reg (drive 1..0, reset 2, motor 3, side 4 inverted)
    void WriteSystem(uint8_t value, uint64_t edge);
    uint8_t System() const { return _system; }
    /// endregion

    /// The RAM disk byte of `cylinder` / `side` / `sector` (1..16) / `byte` (TRD order)
    uint8_t* ImageByte(uint8_t cylinder, uint8_t side, uint8_t sector, uint8_t byte);
    /// Copy a TRD (cylinders x 2 sides x 16 x 256) into the RAM disk, as the MCU loads firmware/test.trd
    size_t LoadTrd(const uint8_t* image, size_t size);

    /// region <State (tests, reports)>
    uint8_t Track() const { return _track; }
    uint8_t Sector() const { return _sector; }
    uint8_t Position() const { return _position; }
    bool Intrq() const { return _intrq; }
    bool Drq() const { return _drq; }
    /// endregion

private:
    friend struct P2666Codec;  // the TTD blobs (debugger/ttd/p2666/ttdp2666.cpp)
    /// Run the controller to `edge`
    void Advance(uint64_t edge);
    void Complete(uint64_t edge);   ///< the intrq branch of the main process
    void Terminate();               ///< force interrupt: every command flag off
    void ResetRegisters();
    uint8_t Status(uint64_t edge) const;
    bool IndexPulse(uint64_t edge) const;
    bool DriveA() const { return (_system & 0x03) == 0; }
    uint8_t* RamByte(uint8_t sector, uint16_t byte);

    void* _owner = nullptr;
    PageResolver _resolver = nullptr;

    uint8_t _system = 0xFF;  ///< betadisk_reg: no reset term, #FF at configuration
    uint8_t _command = 0x03;
    uint8_t _track = 0;
    uint8_t _sector = 1;
    uint8_t _data = 0;      ///< vg93_data_from_cpu
    uint8_t _position = 0;  ///< track_position (the head)
    bool _stepIn = true;    ///< step_direction
    bool _intrq = false;    ///< command_intrq
    bool _nrd = false;      ///< vg93_nrd: status bit 0 (busy)
    bool _drq = false;
    bool _multi = false;    ///< mbit (kept from the last read / write sector command)

    // The command flags (active-low in the VHDL; true here = the flag is '0')
    bool _restore = false, _seek = false, _step = false, _readAddr = false, _read = false, _write = false;
    bool _sectorCmd = false, _trackCmd = false;

    // Type I and force interrupt: vg_delay ends at this edge (0: none)
    uint64_t _delayEnd = 0;
    // The data phase of read / write / read address
    uint64_t _byteEdge = 0;       ///< the next DRQ (read, write) / data load (read address) edge
    bool _waitCpu = false;        ///< DRQ is up: nothing moves until the CPU takes / gives the byte
    uint16_t _byte = 0;           ///< read_byte_number / write_byte_number
    uint8_t _ioSector = 1;        ///< read_sector_num / write_sector_num
    uint8_t _radrState = 0;       ///< radr_state: 6 bytes, 6 down to 1
    uint8_t _radrData = 0;        ///< radr_out_data
    uint8_t _ramData = 0;         ///< vg93_data_from_ram: the byte the last read slot fetched
    uint64_t _indexEpoch = 0;     ///< the index counter restarts with the motor (seek / step)
};

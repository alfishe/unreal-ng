#pragma once

/// @file vdac4bomzh.h
/// @brief The Quake II FPGA core ("bomzh-3D", personality 0x51 'Q') the VDAC4 Tang runs after core_load("Q2").
///
/// Port of Quake-II-for-ZX-Evolution-VDAC4 `gpu/rtl/bz_top.sv` (MIT, Andrew Lazarev) as the ESP sees it over the
/// LINK; the drawing is the core's C model (3rdparty/bomzh, the reference its RTL is checked against).
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/quake2.md.
///
/// LINK map (byte addresses of 32-bit words):
///   0x00 ID "VDC4"   0x04 VERSION 0x51010003   0x08 STATUS {rd_busy, M_SEL now, armed, boot_req, mem_ready,
///   bad_cmd, fifo_ovf}   0x0C FIFO_FREE   0x10 FRAMES   0x14 EVO_PAGE   0x18 EVO_BYTES   0x1C FREE_GRAY
///   {0x47, gray(FIFO_FREE)}   0x3C SCRATCH   0x84 SIZE {0x535A, size}   0x88 RD_ADDR   0x8C RD_LEN (starts the copy
///   into the window)   0x90 MARK   0xEC SD_TRAIN   0xF0 BOOT   0x1000..0x1FFC the 4 KB read window
///   0x40000..0x7FFFC the command queue (2048 words).
///
/// Timing: the model draws a command as soon as its last word arrives, so the queue is always empty to the ESP
/// (FIFO_FREE 2048) and an SDRAM copy into the window is done when RD_LEN is written (rd_busy stays 0). WRITE
/// data goes to SDRAM in pieces as it comes (the core streams it from the queue as well).
///
/// Back to slot 0 (boot_req): BOOT written over the LINK; the Evo's reset (the FT812 RST_PULSE 68 00 00 the Evo
/// BIOS sends over the SPI); or M_SEL was 1 and is now 0 with the Evo's SPI quiet for 0.5 s. The Evo's other SPI
/// traffic (bz_evo_in, SDRAM uploads from the Evo) is not used by Quake II and is not modelled.

#include <cstddef>
#include <cstdint>
#include <vector>

struct bz_s;

class Vdac4BomzhCore
{
public:
    /// The core's VDAC4 mark (UserCode 0x564434 'Q') and its LINK VERSION word
    static constexpr uint32_t kUserCode = 0x5644'3451;
    static constexpr uint32_t kVersion = 0x5101'0003;
    /// The board's SDRAM (8 MB)
    static constexpr uint32_t kMemoryBytes = 8u << 20;
    /// The command queue in BSRAM
    static constexpr uint32_t kQueueWords = 2048;
    /// The largest picture taken from SETUP (the core's HDMI mode is 424 x 240)
    static constexpr int kMaxWidth = 1024;
    static constexpr int kMaxHeight = 768;

    Vdac4BomzhCore();
    ~Vdac4BomzhCore();
    Vdac4BomzhCore(const Vdac4BomzhCore&) = delete;
    Vdac4BomzhCore& operator=(const Vdac4BomzhCore&) = delete;

    /// The model has its SDRAM (allocation can fail)
    bool IsReady() const { return _model != nullptr; }

    uint32_t LinkRead(uint32_t address) const;
    void LinkWrite(uint32_t address, uint32_t value);

    /// M_SEL as the core samples it (the Evo's plugin shows the Tang's picture: the reset rule's arming)
    void SetMsel(bool msel, uint64_t nowMs);
    /// One byte of the Evo's SPI to the Tang, and the end of the transfer
    void EvoByte(uint8_t mosi, uint64_t nowMs);
    void EvoDeselect(uint64_t nowMs);
    /// boot_req: the BL616 loads slot 0 now. `nowMs` checks the M_SEL idle rule first
    bool BootRequested(uint64_t nowMs);

    /// FRAME commands executed (the FRAMES register); a new one means a new picture
    uint32_t Frames() const { return _frames; }
    /// The shown picture as RGBA8888 (W x H from SETUP); false while there is none (no SETUP yet, or a size
    /// over kMaxWidth x kMaxHeight)
    bool ShownPicture(std::vector<uint8_t>& rgba, uint16_t& width, uint16_t& height) const;

    /// For the automation surfaces
    struct Stats
    {
        uint64_t words = 0;         ///< words written into the queue
        uint64_t commands = 0;      ///< commands executed
        uint64_t badWords = 0;      ///< words dropped as not a command
        uint32_t frames = 0;
        uint32_t mark = 0;
        uint32_t width = 0, height = 0;
        bool badCommand = false;
        bool bootRequest = false;
    };
    Stats GetStats() const;

private:
    void Push(uint32_t word);
    /// The command in _pending is whole: run it
    void Execute();
    void FlushWrite();
    void CopyToWindow(uint32_t bursts);

    bz_s* _model = nullptr;
    std::vector<uint32_t> _pending;  ///< the command being received (not WRITE data)
    uint32_t _need = 0;              ///< its length, 0 until known
    // WRITE data streamed into SDRAM
    uint32_t _writeLeft = 0;
    uint32_t _writeAddress = 0;
    std::vector<uint32_t> _writeChunk;

    uint32_t _frames = 0;
    uint32_t _scratch = 0;
    uint32_t _size = 0;
    uint32_t _evoPage = 1;
    uint32_t _rdAddress = 0;
    uint32_t _window[1024] = {};
    bool _badCommand = false;
    bool _bootRequest = false;
    bool _armed = false;
    bool _msel = false;
    uint64_t _quietSinceMs = 0;      ///< the last M_SEL = 1 or Evo SPI byte
    // The Evo's transfer in flight (RST_PULSE 68 00 00)
    uint32_t _evoBytes = 0;
    uint8_t _evoFirst = 0;
    bool _evoRestZero = true;

    uint64_t _words = 0;
    uint64_t _commands = 0;
    uint64_t _badWords = 0;
};

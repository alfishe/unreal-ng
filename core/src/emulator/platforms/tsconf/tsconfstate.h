#pragma once

#include <cstddef>
#include <cstdint>

/// @file tsconfstate.h
/// @brief TS-Conf machine state (TSConf technical-design §3.3).
///
/// Owned by PortDecoder_TSConf and nowhere else: no shared struct carries a
/// TS-Conf field. Plain fixed-width fields only (no bitfield unions, no
/// pointers) and no implicit padding, so the TTD blob is the struct itself.
/// Hardware facts: docs/inprogress/2026-09-27-tsconf/hardware-spec.md.

/// `#xxAF` register numbers (register = A[15:8]; hardware-spec §3.2)
namespace TsConfReg
{
    constexpr uint8_t VConfig = 0x00;    ///< W: V_CONFIG, R: STATUS
    constexpr uint8_t VPage = 0x01;
    constexpr uint8_t GXOffsL = 0x02;
    constexpr uint8_t GXOffsH = 0x03;
    constexpr uint8_t GYOffsL = 0x04;
    constexpr uint8_t GYOffsH = 0x05;
    constexpr uint8_t TConfig = 0x06;
    constexpr uint8_t PalSel = 0x07;
    constexpr uint8_t Border = 0x0F;
    constexpr uint8_t Page0 = 0x10;
    constexpr uint8_t Page1 = 0x11;
    constexpr uint8_t Page2 = 0x12;
    constexpr uint8_t Page3 = 0x13;
    constexpr uint8_t FMaps = 0x15;
    constexpr uint8_t TMapPage = 0x16;
    constexpr uint8_t T0GPage = 0x17;
    constexpr uint8_t T1GPage = 0x18;
    constexpr uint8_t SGPage = 0x19;
    constexpr uint8_t DmaSAl = 0x1A;
    constexpr uint8_t DmaSAh = 0x1B;
    constexpr uint8_t DmaSAx = 0x1C;
    constexpr uint8_t DmaDAl = 0x1D;
    constexpr uint8_t DmaDAh = 0x1E;
    constexpr uint8_t DmaDAx = 0x1F;
    constexpr uint8_t SysConfig = 0x20;
    constexpr uint8_t MemConfig = 0x21;
    constexpr uint8_t HsInt = 0x22;
    constexpr uint8_t VsIntL = 0x23;
    constexpr uint8_t VsIntH = 0x24;
    constexpr uint8_t DmaWpd = 0x25;
    constexpr uint8_t DmaLen = 0x26;
    constexpr uint8_t DmaCtrl = 0x27;    ///< W: DMA_CTRL, R: DMA_STATUS
    constexpr uint8_t DmaNum = 0x28;
    constexpr uint8_t FddVirt = 0x29;
    constexpr uint8_t IntMask = 0x2A;
    constexpr uint8_t CacheConfig = 0x2B;
    constexpr uint8_t DmaWpa = 0x2D;
    constexpr uint8_t T0XOffsL = 0x40;   ///< 0x40-0x47: T0/T1 X/Y offsets
    constexpr uint8_t T1YOffsH = 0x47;

    constexpr size_t kCount = 0x48;      ///< register file size (0x00-0x47)
}

/// MEM_CONFIG bits (hardware-spec §2.2)
namespace TsConfMemConfig
{
    constexpr uint8_t Rom128 = 0x01;     ///< copy of 7FFD bit 4: 0 = BASIC-128, 1 = BASIC-48
    constexpr uint8_t W0We = 0x02;       ///< window 0 writable (RAM)
    constexpr uint8_t W0NoMap = 0x04;    ///< 1 = normal mode (page = PAGE0), 0 = mapped mode
    constexpr uint8_t W0Ram = 0x08;      ///< window 0 shows RAM
    constexpr uint8_t Lck128Mask = 0xC0; ///< 7FFD decode mode (§2.3)
    constexpr uint8_t Reset = 0x04;
}

/// Interrupt sources: INT_MASK bits and TsConfState::intPending bits (§5)
namespace TsConfInt
{
    constexpr uint8_t Frame = 0x01;      ///< vector 0xFF
    constexpr uint8_t Line = 0x02;       ///< vector 0xFD
    constexpr uint8_t Dma = 0x04;        ///< vector 0xFB
    constexpr uint8_t WaitPort = 0x08;   ///< vector 0xF9
}

/// TsConfState::dmaFlags
namespace TsConfDmaFlag
{
    constexpr uint8_t Active = 0x01;     ///< DMA_STATUS[7]
    constexpr uint8_t Opt = 0x02;        ///< DMA_CTRL[6]: BLT2 saturation
    constexpr uint8_t SrcAlign = 0x04;   ///< DMA_CTRL[5]
    constexpr uint8_t DstAlign = 0x08;   ///< DMA_CTRL[4]
    constexpr uint8_t BlockSize = 0x10;  ///< DMA_CTRL[3] (ASZ): 512-byte blocks, byte granularity in the blitters
    constexpr uint8_t Loaded = 0x20;     ///< FILL: the fill word was read
}

/// LCK128 modes (MEM_CONFIG[7:6], hardware-spec §2.3)
enum class TsConfLck128 : uint8_t
{
    Mode512K = 0,
    Mode128K = 1,
    Auto = 2,
    Mode1024K = 3,
};

struct TsConfState
{
    /// region <Palette and sprite files (not reset by a Z80 reset)>
    uint16_t cram[256];         ///< palette, [14:10] R, [9:5] G, [4:0] B, [15] VDAC flag (§4.3)
    uint16_t sfile[256];        ///< sprite descriptors (§4.4)
    /// endregion

    /// region <CPU cache (§2.5): 256 one-word entries indexed by A[8:1]>
    uint16_t cacheTag[256];     ///< bit 15 valid, [12:0] {page[7:0], A[13:9]}
    uint16_t cacheWord[256];    ///< {odd byte, even byte}
    /// endregion

    /// Register file, indexed by TsConfReg (the value last written; the
    /// write-only registers read 0xFF on the bus, §3.2)
    uint8_t regs[TsConfReg::kCount];

    /// region <Latches outside the register file>
    uint8_t fmStash;            ///< FM window: even byte waiting for its odd half (§2.4)
    uint8_t eff7;               ///< #EFF7 (bit 7: CMOS enable outside DOS, §9)
    uint8_t dos;                ///< DOS signal (TR-DOS paged, §2.2)
    uint8_t vdos;               ///< virtual TR-DOS active (§8.2, phase 6)
    uint8_t lock48;             ///< 7FFD lock (D5 of a 7FFD write outside 1024K mode)
    uint8_t opcodeLatch128;     ///< auto LCK128: !(D7 ^ D6) of the last M1 opcode
    uint8_t pwrUp;              ///< STATUS bit 6, cleared after the first read
    uint8_t reserved;           ///< keeps the struct free of padding
    /// endregion

    /// region <Interrupt controller (§5), TsConfInterrupts>
    uint32_t intLastRaster;     ///< raster tact (0..71679) the events are evaluated up to
    int32_t intFrameRaster;     ///< raster tact of the latched frame INT (its 32-clock pulse runs from there; negative after a rollover)
    uint8_t intPending;         ///< latched sources, TsConfInt bits
    uint8_t intReserved[3];     ///< keeps the struct free of padding
    /// endregion

    /// region <Line engine (§4.1, §4.2), TsConfEngine>
    /// Line-latched copies of the registers ([V] video_ports.v: taken at the
    /// last dot of every line, used by the next line; #7FFD writes V_PAGE here
    /// at once)
    uint8_t latVConfig;
    uint8_t latVPage;
    uint8_t latPalSel;
    uint8_t latGXOffsL;
    uint8_t latGXOffsH;
    uint8_t latT0GPage;
    uint8_t latT1GPage;
    uint8_t latT0XOffsL;
    uint8_t latT0XOffsH;
    uint8_t latT1XOffsL;
    uint8_t latT1XOffsH;
    uint8_t yOffsPending;       ///< G_Y_OFFS written: the row counter reloads at the next line start
    uint16_t cntRow;            ///< graphics row counter of the current line (9 bit)
    uint16_t engNextLine;       ///< next raster line (0..320) whose start is not processed yet
    /// endregion

    /// region <DMA (§6) and the DRAM budget, TsConfDma / TsConfEngine>
    uint32_t dmaSrc;            ///< live source word address (21 bit)
    uint32_t dmaDst;            ///< live destination word address
    uint32_t dmaCredit;         ///< DRAM accesses the DMA may still use (budget carry)
    uint32_t cpuAccesses;       ///< CPU DRAM reads since the engine last accounted them
    uint32_t budgetRaster;      ///< raster tact the DRAM budget is accounted up to
    uint16_t dmaData;           ///< the word in flight (FILL: the word being filled)
    uint16_t dmaBlocks;         ///< block counter, bit 8 set = done
    uint8_t dmaSrcLow;          ///< source low address as the CPU wrote it (the aligned reload)
    uint8_t dmaDstLow;          ///< destination low address as the CPU wrote it
    uint8_t dmaBurst;           ///< words left in the block - 1
    uint8_t dmaDevice;          ///< {DMA_CTRL[7], DMA_CTRL[2:0]}
    uint8_t dmaFlags;           ///< TsConfDmaFlag bits
    uint8_t dmaReserved;        ///< keeps the struct free of padding
    uint16_t cpuLineAccesses;   ///< CPU DRAM reads of the previous line (the TSU's budget)
    /// endregion

    /// region <Virtual TR-DOS (§8.2)>
    uint8_t vgDrive;            ///< drive select bits of the last #FF write (drive_sel_raw)
    uint8_t preVdos;            ///< a trapped FDC access: vdos starts at the next M1
    uint8_t vdosReserved[2];    ///< keeps the struct free of tail padding
    /// endregion

    /// Accessors
    uint8_t Page(uint8_t window) const { return regs[TsConfReg::Page0 + (window & 3)]; }
    uint8_t MemConfig() const { return regs[TsConfReg::MemConfig]; }
    TsConfLck128 Lck128() const { return static_cast<TsConfLck128>(regs[TsConfReg::MemConfig] >> 6); }
    bool FmEnabled() const { return (regs[TsConfReg::FMaps] & 0x10) != 0; }
    uint16_t FmBase() const { return static_cast<uint16_t>((regs[TsConfReg::FMaps] & 0x0F) << 12); }
};

static_assert(sizeof(TsConfState) == 2048 + TsConfReg::kCount + 8 + 12 + 16 + 32 + 4, "TsConfState must stay padding-free (TTD blob)");

#pragma once

#include <cstdint>
#include <functional>

struct TsConfState;
class IdeAdapter;
class Memory;
class TsConfInterrupts;

/// TS-Conf DMA engine (hardware-spec §6, [V] common/dma.v).
///
/// One transfer at a time; `DMA_CTRL` launches (or relaunches) it. Addresses
/// are 21-bit word addresses into DRAM; with S_ALGN / D_ALGN the low 7 (ASZ=0)
/// or 8 (ASZ=1) word bits wrap inside a block and reload to the value the CPU
/// wrote at every block end, while the base steps 256 / 512 bytes.
///
/// | Code | Task | DRAM accesses per word (the budget unit) |
/// |:--|:--|:--|
/// | 0x1 | RAM -> RAM | 2 |
/// | 0x9 | BLT1: copy, keep dst where the source byte / nibble is 0 | 3 |
/// | 0x6 | BLT2: dst += src per byte / nibble, saturating with OPT (XTR_FEAT builds only) | 3 |
/// | 0x4 | FILL: the first source word once, then written | 1 (+1 for the first read) |
/// | 0xC / 0xD | RAM -> CRAM / SFILE, entry = dst word address [7:0] | 2 |
/// | 0x2 / 0xA | SPI -> RAM / RAM -> SPI, 2 bytes per word, low byte first | 8 (SPI time) |
/// | 0x3 / 0xB | IDE -> RAM / RAM -> IDE (a board fitted) | 2 |
/// | others (0x7 wait port, undefined) | busy forever, no INT, until the next DMA_CTRL | - |
///
/// The engine gives the DMA its share of the line's DRAM cycles (Run); the
/// completion raises the DMA INT. All state is in TsConfState (TTD).
class TsConfDma
{
public:
    /// The emulated firmware build: the standard `quartus` build (Nemo IDE,
    /// no XTR_FEAT) has no BLT2 (hardware-spec §0.1)
    static constexpr bool kBuildHasBlt2 = false;

    TsConfDma(TsConfState& state, TsConfInterrupts& interrupts) : _ts(state), _interrupts(interrupts) {}

    /// Memory and devices the DMA reaches. `memory` learns every RAM page a
    /// DMA write touches (TTD's dirty pages: the writes bypass the CPU path)
    void Attach(uint8_t* ram, IdeAdapter* ide, Memory* memory = nullptr)
    {
        _ram = ram;
        _ide = ide;
        _memory = memory;
    }
    /// The board's SPI master, shared with the CPU's #57 ([V] top.v:1056-1062,
    /// spi.v): a DMA read takes the byte of the PREVIOUS exchange and starts a
    /// new one sending #FF (as IN #57 does); a DMA write starts an exchange
    /// sending the byte. `read` selects which; none = no card, reads give #FF
    void SetSpi(std::function<uint8_t(bool read, uint8_t out)> spi) { _spi = std::move(spi); }
    /// Tests: emulate a firmware build with XTR_FEAT (BLT2)
    void SetBlt2Built(bool built) { _blt2 = built; }

    void Reset();

    /// region <Registers (#xxAF)>
    void Launch(uint8_t ctrl);
    void WriteAddress(uint8_t reg, uint8_t value);  ///< DMAS/DMAD AL / AH / AX: the live counters
    bool Busy() const;
    /// endregion

    /// Spend up to `credit` DRAM accesses on the running transfer
    /// @return accesses used
    uint32_t Run(uint32_t credit);

    /// DRAM accesses one word of the running transfer costs (0: it makes no progress)
    uint32_t WordCost() const;

    /// The running transfer writes CRAM, which the picture reads at the dot (TIM-5)
    bool WritesCram() const;
    /// CRAM words the DMA has written (the screen's palette cache)
    uint32_t CramWrites() const { return _cramWrites; }
    /// Run, calling `before(used)` ahead of each word with the accesses used so
    /// far (the engine places each CRAM write in time)
    template <typename Before>
    uint32_t RunEach(uint32_t credit, Before before)
    {
        if (!_ram)
            return 0;
        uint32_t used = 0;
        for (uint32_t cost = WordCost(); cost && used + cost <= credit; cost = WordCost())
        {
            before(used);
            Word();
            used += cost;
        }
        return used;
    }

private:
    enum Device : uint8_t
    {
        Ram = 0x1,
        SpiIn = 0x2,
        IdeIn = 0x3,
        Fill = 0x4,
        Blt2 = 0x6,
        Blt1 = 0x9,
        SpiOut = 0xA,
        IdeOut = 0xB,
        Cram = 0xC,
        Sfile = 0xD,
    };

    bool Stuck() const;
    void Word();
    uint16_t ReadWord(uint32_t wordAddress) const;
    void WriteWord(uint32_t wordAddress, uint16_t value);
    uint32_t NextAddress(uint32_t address, uint8_t low, bool align) const;
    void Finish();

    TsConfState& _ts;
    TsConfInterrupts& _interrupts;
    uint8_t* _ram = nullptr;
    IdeAdapter* _ide = nullptr;
    Memory* _memory = nullptr;
    std::function<uint8_t(bool, uint8_t)> _spi;
    bool _blt2 = kBuildHasBlt2;
    uint32_t _cramWrites = 0;
};

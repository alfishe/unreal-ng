#include "tsconfdma.h"

#include "emulator/io/ide/ideadapter.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfinterrupts.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

namespace
{
    constexpr uint32_t kWordMask = 0x1FFFFF;  // 21-bit word address: 4 MB

    uint16_t BlitKeep(uint16_t src, uint16_t dst, bool bytes)
    {
        uint16_t out = 0;
        if (bytes)
        {
            for (int shift = 0; shift < 16; shift += 8)
                out |= static_cast<uint16_t>((((src >> shift) & 0xFF) ? ((src >> shift) & 0xFF) : ((dst >> shift) & 0xFF)) << shift);
        }
        else
        {
            for (int shift = 0; shift < 16; shift += 4)
                out |= static_cast<uint16_t>((((src >> shift) & 0xF) ? ((src >> shift) & 0xF) : ((dst >> shift) & 0xF)) << shift);
        }
        return out;
    }

    uint16_t BlitAdd(uint16_t src, uint16_t dst, bool bytes, bool saturate)
    {
        uint16_t out = 0;
        const int width = bytes ? 8 : 4;
        const uint32_t mask = bytes ? 0xFF : 0xF;
        for (int shift = 0; shift < 16; shift += width)
        {
            uint32_t sum = ((src >> shift) & mask) + ((dst >> shift) & mask);
            if (sum > mask)
                sum = saturate ? mask : (sum & mask);
            out |= static_cast<uint16_t>(sum << shift);
        }
        return out;
    }
}

void TsConfDma::Reset()
{
    _ts.dmaFlags = 0;
    _ts.dmaBlocks = 0x100;
    _ts.dmaCredit = 0;
}

bool TsConfDma::Busy() const
{
    return (_ts.dmaFlags & TsConfDmaFlag::Active) != 0;
}

/// DMA_CTRL write: launch, or relaunch a running transfer with its counters
/// reloaded - busy never falls, so the aborted transfer raises no INT
/// ([V] dma.v:220-231, 309-313)
void TsConfDma::Launch(uint8_t ctrl)
{
    _ts.dmaDevice = static_cast<uint8_t>(((ctrl >> 4) & 0x08) | (ctrl & 0x07));
    uint8_t flags = TsConfDmaFlag::Active;
    if (ctrl & 0x40)
        flags |= TsConfDmaFlag::Opt;
    if (ctrl & 0x20)
        flags |= TsConfDmaFlag::SrcAlign;
    if (ctrl & 0x10)
        flags |= TsConfDmaFlag::DstAlign;
    if (ctrl & 0x08)
        flags |= TsConfDmaFlag::BlockSize;
    _ts.dmaFlags = flags;
    _ts.dmaBurst = _ts.regs[TsConfReg::DmaLen];
    _ts.dmaBlocks = _ts.regs[TsConfReg::DmaNum];
    _ts.dmaCredit = 0;
}

/// The address registers are the live counters; AL also sets the low reload
/// value, AH bit 0 its bit 7 ([V] dma.v:325-339, 385-399)
void TsConfDma::WriteAddress(uint8_t reg, uint8_t value)
{
    const bool source = reg <= TsConfReg::DmaSAx;
    uint32_t& address = source ? _ts.dmaSrc : _ts.dmaDst;
    uint8_t& low = source ? _ts.dmaSrcLow : _ts.dmaDstLow;
    switch (source ? reg - TsConfReg::DmaSAl : reg - TsConfReg::DmaDAl)
    {
        case 0:  // AL: word bits 6:0
            address = (address & ~0x7Fu) | (value >> 1);
            low = static_cast<uint8_t>((low & 0x80) | (value >> 1));
            break;
        case 1:  // AH: word bits 12:7
            address = (address & ~(0x3Fu << 7)) | (static_cast<uint32_t>(value & 0x3F) << 7);
            low = static_cast<uint8_t>((low & 0x7F) | ((value & 0x01) << 7));
            break;
        default:  // AX: word bits 20:13
            address = (address & 0x1FFF) | (static_cast<uint32_t>(value) << 13);
            break;
    }
}

bool TsConfDma::Stuck() const
{
    switch (_ts.dmaDevice)
    {
        case Ram:
        case Blt1:
        case Fill:
        case Cram:
        case Sfile:
        case SpiIn:
        case SpiOut:
            return false;
        case Blt2:
            return !_blt2;
        case IdeIn:
        case IdeOut:
            return !_ide || _ide->Scheme() == IDE_NONE;  // an IDE-less build
        default:
            return true;  // wait port (AVR) and undefined codes hang
    }
}

uint32_t TsConfDma::WordCost() const
{
    if (!Busy() || Stuck())
        return 0;
    switch (_ts.dmaDevice)
    {
        case Blt1:
        case Blt2:
            return 3;
        case Fill:
            return (_ts.dmaFlags & TsConfDmaFlag::Loaded) ? 1 : 2;
        case SpiIn:
        case SpiOut:
            // Two SPI bytes of 17 fclk each ([V] spi.v: the start clock + a
            // 16-clock shift) and the DRAM cycle, aligned to the 4-fclk DRAM
            // cycle: ~40 fclk = 10 DRAM cycles (phase 8 TIM-3)
            return 10;
        case IdeIn:
        case IdeOut:
            // The IDE bus cycle (~6 fclk, [V] ide.v) rounded up to 2 DRAM
            // cycles, then the DRAM cycle (TIM-3)
            return 3;
        default:
            return 2;  // RAM copy (read + write); CRAM / SFILE (a 1-fclk device write + the idle half)
    }
}

uint16_t TsConfDma::ReadWord(uint32_t wordAddress) const
{
    const uint8_t* p = _ram + (wordAddress & kWordMask) * 2;
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

void TsConfDma::WriteWord(uint32_t wordAddress, uint16_t value)
{
    const uint32_t byteAddress = (wordAddress & kWordMask) * 2;
    uint8_t* p = _ram + byteAddress;
    p[0] = static_cast<uint8_t>(value);
    p[1] = static_cast<uint8_t>(value >> 8);
    if (_memory)
        _memory->MarkRamPageEdited(static_cast<uint16_t>(byteAddress >> 14));
}

/// [V] dma.v:343-349: linear, or aligned - the low 7 / 8 word bits wrap in the
/// block and reload at its end, the base steps one block
uint32_t TsConfDma::NextAddress(uint32_t address, uint8_t low, bool align) const
{
    const bool blockEnd = _ts.dmaBurst == 0;
    if (!align)
        return (address + 1) & kWordMask;

    if (_ts.dmaFlags & TsConfDmaFlag::BlockSize)  // 512 bytes: 8 low bits
    {
        if (blockEnd)
            return (((address >> 8) + 1) << 8 | low) & kWordMask;
        return (address & ~0xFFu) | ((address + 1) & 0xFF);
    }
    if (blockEnd)  // 256 bytes: 7 low bits
        return (((address >> 7) + 1) << 7 | (low & 0x7F)) & kWordMask;
    return (address & ~0x7Fu) | ((address + 1) & 0x7F);
}

void TsConfDma::Word()
{
    const bool srcAlign = _ts.dmaFlags & TsConfDmaFlag::SrcAlign;
    const bool dstAlign = _ts.dmaFlags & TsConfDmaFlag::DstAlign;
    const bool bytes = _ts.dmaFlags & TsConfDmaFlag::BlockSize;
    auto readSource = [&] {
        const uint16_t word = ReadWord(_ts.dmaSrc);
        _ts.dmaSrc = NextAddress(_ts.dmaSrc, _ts.dmaSrcLow, srcAlign);
        return word;
    };
    auto spiRead = [&] { return _spi ? _spi(true, 0xFF) : static_cast<uint8_t>(0xFF); };
    auto spiWrite = [&](uint8_t out) {
        if (_spi)
            _spi(false, out);
    };

    switch (_ts.dmaDevice)
    {
        case Ram:
            _ts.dmaData = readSource();
            WriteWord(_ts.dmaDst, _ts.dmaData);
            break;
        case Blt1:
            _ts.dmaData = BlitKeep(readSource(), ReadWord(_ts.dmaDst), bytes);
            WriteWord(_ts.dmaDst, _ts.dmaData);
            break;
        case Blt2:
            _ts.dmaData = BlitAdd(readSource(), ReadWord(_ts.dmaDst), bytes, _ts.dmaFlags & TsConfDmaFlag::Opt);
            WriteWord(_ts.dmaDst, _ts.dmaData);
            break;
        case Fill:
            // The source word is read once per transaction ([V] dma.v fil_hook)
            if (!(_ts.dmaFlags & TsConfDmaFlag::Loaded))
            {
                _ts.dmaData = readSource();
                _ts.dmaFlags |= TsConfDmaFlag::Loaded;
            }
            WriteWord(_ts.dmaDst, _ts.dmaData);
            break;
        case Cram:
            _ts.dmaData = readSource();
            _ts.cram[_ts.dmaDst & 0xFF] = _ts.dmaData;
            break;
        case Sfile:
            _ts.dmaData = readSource();
            _ts.sfile[_ts.dmaDst & 0xFF] = _ts.dmaData;
            break;
        case SpiIn:
        {
            // Two pipelined reads, low byte first ([V] dma.v:252-257, 430-433)
            const uint8_t lowByte = spiRead();
            const uint8_t highByte = spiRead();
            _ts.dmaData = static_cast<uint16_t>(lowByte | (highByte << 8));
            WriteWord(_ts.dmaDst, _ts.dmaData);
            break;
        }
        case SpiOut:
            _ts.dmaData = readSource();
            spiWrite(static_cast<uint8_t>(_ts.dmaData));
            spiWrite(static_cast<uint8_t>(_ts.dmaData >> 8));
            break;
        case IdeIn:
            _ts.dmaData = _ide->DmaReadWord();
            WriteWord(_ts.dmaDst, _ts.dmaData);
            break;
        case IdeOut:
            _ts.dmaData = readSource();
            _ide->DmaWriteWord(_ts.dmaData);
            break;
        default:
            return;
    }

    // The destination counter steps with every write strobe, a device's too
    // (CRAM / SFILE take their entry from it); a device read steps the
    // source ([V] dma.v:351-352, 411-412: dram_next || dev_stb)
    if (_ts.dmaDevice == SpiIn || _ts.dmaDevice == IdeIn)
        _ts.dmaSrc = NextAddress(_ts.dmaSrc, _ts.dmaSrcLow, srcAlign);
    _ts.dmaDst = NextAddress(_ts.dmaDst, _ts.dmaDstLow, dstAlign);

    // Block and transfer counters ([V] dma.v:286-313)
    if (_ts.dmaBurst == 0)
    {
        _ts.dmaBurst = _ts.regs[TsConfReg::DmaLen];  // DMA_LEN applies at the block reload
        _ts.dmaBlocks = static_cast<uint16_t>((_ts.dmaBlocks - 1) & 0x1FF);
        if (_ts.dmaBlocks & 0x100)
            Finish();
    }
    else
    {
        _ts.dmaBurst--;
    }
}

void TsConfDma::Finish()
{
    _ts.dmaFlags &= static_cast<uint8_t>(~TsConfDmaFlag::Active);
    _ts.dmaCredit = 0;
    _interrupts.RaiseDma();
}

bool TsConfDma::WritesCram() const
{
    return Busy() && _ts.dmaDevice == Cram;
}

uint32_t TsConfDma::Run(uint32_t credit)
{
    if (!_ram)
        return 0;

    uint32_t used = 0;
    for (uint32_t cost = WordCost(); cost && used + cost <= credit; cost = WordCost())
    {
        Word();
        used += cost;
    }
    return used;
}

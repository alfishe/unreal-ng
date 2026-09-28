#pragma once

/// @file neogsdma.h
/// @brief NeoGS DMA modules (neogs-tdd.md §3.9, §5.7; FPGA dma/*.v).
///
/// Registers per module, selected by DMA_MOD (#1B = 1 ZX, 2 SD, 3 MP3) and
/// shown at #1C-#1F: HAD (6 bits stored, address 21:16), MAD, LAD, CST (bit 7 =
/// run; bits 6:0 read as 1). Only 21 address bits reach the memory - HAD bit 5
/// is ignored, so DMA sees the lower 2 MB - and the address advances after
/// every byte.
///
/// SD module: clocks #FF on the SD master until a byte other than #FF comes
/// (no timeout); #FE is the data token: 512 data bytes and 2 CRC bytes (not
/// checked) come in at 18 card clocks each, then a burst writes the 512 bytes
/// to RAM with the CPU stalled (2 clocks a byte). Any other first byte ends
/// the module at once with nothing written. Either way CST bit 7 clears and
/// INTREQ bit 1 is raised.
///
/// MP3 module: a burst reads 512 bytes from RAM (CPU stalled), then each byte
/// goes to the decoder's data port when DREQ = 1, at the MD master's byte time.
/// Then CST bit 7 clears and INTREQ bit 2 is raised.
///
/// Clearing CST bit 7 aborts a module, without an interrupt. The ZX module
/// (host access to card RAM) is phase 5 and only keeps its registers here.
///
/// Byte-level work is batched into events: nothing observes the SD card or
/// the decoder between the start of a transfer and its events, apart from a
/// CPU access to the same SPI master, which on the real card corrupts the
/// transfer (not modelled).

#include <cstdint>
#include <limits>

class NeoGSMemory;
class NeoGSInterrupts;
class SpiDevice;
class Vs10xxDecoder;

class NeoGSDma
{
public:
    enum Module : uint8_t { ZX = 0, SD = 1, MP3 = 2 };

    /// What the card lends the modules
    struct Host
    {
        virtual ~Host() = default;
        virtual void dmaStall(int64_t units) = 0;        // CPU off the bus for `units`
        virtual int64_t dmaUnitsPerCycle() const = 0;     // current card clock
        virtual void dmaSdByteDone(uint8_t received) = 0; // SD master's last byte (SD_READ)
    };

    static constexpr int BLOCK = 512;
    static constexpr int SD_BYTE_CLOCKS = 18;      // SPI byte paced on the ready signal
    static constexpr int BURST_CLOCKS_PER_BYTE = 2;
    static constexpr int GRANT_OVERHEAD_CLOCKS = 4; // BUSRQ -> BUSAK and release
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

    NeoGSDma(Host& host, NeoGSMemory& memory, NeoGSInterrupts& irq)
        : _host(host), _mem(memory), _irq(irq) {}

    void attachSd(SpiDevice* sd) { _sd = sd; }
    void attachMp3(Vs10xxDecoder* mp3) { _mp3 = mp3; }

    /// FPGA reset: every run bit clears (addresses have no reset)
    void reset();

    // Registers (#1B-#1F)
    void writeModuleSelect(uint8_t value) { _select = static_cast<uint8_t>(value & 7); }
    uint8_t moduleSelect() const { return _select; }
    void writeRegister(int reg, uint8_t value, int64_t now);
    uint8_t readRegister(int reg) const;

    /// Next time run() must be called (kNever when idle)
    int64_t nextEvent() const;
    /// Do all module work due at `now`
    void run(int64_t now);

    /// Linear address of a module (21 bits in effect)
    uint32_t address(Module m) const;
    bool running(Module m) const { return (_regs[m][3] & 0x80) != 0; }

    uint8_t (&rawRegisters())[3][4] { return _regs; }

    /// Snapshot (TTD): registers, module phases and times, FIFO contents
    static constexpr size_t STATE_SIZE = 1088;
    void saveState(uint8_t* dst) const;
    void loadState(const uint8_t* src);

private:
    enum class Phase : uint8_t { Idle, SdWait, SdBurst, Mp3Send };

    void start(Module m, int64_t now);
    void finish(Module m, bool raiseInterrupt);
    void advanceAddress(Module m, int bytes);
    void runSd(int64_t now);
    void runMp3(int64_t now);

    Host& _host;
    NeoGSMemory& _mem;
    NeoGSInterrupts& _irq;
    SpiDevice* _sd = nullptr;
    Vs10xxDecoder* _mp3 = nullptr;

    uint8_t _select = 0;
    uint8_t _regs[3][4] = {};  // HAD MAD LAD CST

    Phase _sdPhase = Phase::Idle;
    int64_t _sdAt = kNever;
    uint8_t _sdBuffer[BLOCK] = {};

    Phase _mp3Phase = Phase::Idle;
    int64_t _mp3At = kNever;
    uint8_t _mp3Buffer[BLOCK] = {};
    int _mp3Sent = 0;
};

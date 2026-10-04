#pragma once

/// @file profixtkbc.h
/// @brief The PROFI-XT keyboard controller of the ZX Profi: a PC/XT keyboard
/// on the Profi's keyboard connector X9 (v5 boards), acting as a virtual
/// Spectrum matrix with a 6th data line (KD5).
///
/// The board (schematic PROFI-XT.PDF, research-profi-keyboard.md section 2): a
/// 1816VE35 (8035) at 8 MHz runs a 2 KB EPROM. The XT keyboard's clock edge
/// pulses the MCU's /INT, its data bit is latched onto T0 (inverted). P1 reads
/// the Z80's A8..A15. Every Z80 read of an even port (/CSKBD) clocks a WAIT
/// flip-flop with D = P2.7: while a key is held (P2.7 = 1) the Z80 waits, the
/// MCU sees T1 = 0, reads P1, writes the answer with MOVX (address bit 7 = 0:
/// the output latch, KD0..KD5 and the reset line) and ends the wait with
/// another MOVX (address bit 5 = 0). While no key is held (P2.7 = 0) there is
/// no wait and the latch is off the bus: the pull-ups read KD0..KD5 = 1.
///
/// Two engines ([PROFI] Keyboard=):
///   - XT, the firmware: the real firmware on the MCS-48 core (low-level). The
///     image is data/rom/profixt/profi-xt-v1.27.rom, a RECONSTRUCTION: the only
///     known dump never enables interrupts, so it receives no key; 5 bytes at
///     02Eh..032h are patched (see data/rom/profixt/README.md);
///   - XTTable, the table: the key table read off that firmware (profixtkeymap)
///     and its answer rules, without the MCU (high-level), for anyone who does
///     not want to depend on the reconstructed image.
///
/// Time: the MCU runs lazily, caught up to the Z80's emulated time on every
/// #FE read, key event and frame end (as Atm2Kbc).
///
/// Design: docs/inprogress/2026-10-01-profi-v3-v5/design.md, section "Keyboard"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "emulator/cpu/mcs48/mcs48.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/io/keyboard/profixtkeymap.h"

class EmulatorContext;

class ProfiXtKbc : public IPs2KeySink
{
public:
    enum class Engine : uint8_t
    {
        Firmware = 0,   ///< [PROFI] Keyboard=XT: the firmware on the MCS-48 core
        Table = 1,      ///< [PROFI] Keyboard=XTTable: the key table, no MCU
    };

    /// The reconstructed firmware image (data/rom/profixt/README.md)
    static constexpr const char* kDefaultRom = "rom/profixt/profi-xt-v1.27.rom";
    static constexpr uint32_t kCrystalHz = 8000000;
    /// CRC-32 of the reconstructed image, and of the original dump it was made from
    static constexpr uint32_t kReconstructedCrc = 0x59C7A98Cu;
    static constexpr uint32_t kOriginalDumpCrc = 0x9A8E2686u;

    /// The XT keyboard's wire: about 10 kHz clock, 9-bit frames (start bit 1,
    /// 8 data bits LSB first). The /INT pulse is the board's differentiator on
    /// the clock edge; its RC is not on the schematic, 4 machine cycles is the
    /// datasheet's 3-cycle minimum for a level interrupt plus margin
    static constexpr uint32_t kBitMicros = 100;
    static constexpr uint32_t kIntPulseClocks = 4 * mcs48::Mcs48::kClocksPerCycle;
    static constexpr uint32_t kByteGapMicros = 200;
    static constexpr uint32_t kTypematicDelayMicros = 500000;
    static constexpr uint32_t kTypematicPeriodMicros = 91743;   ///< 10.9 per second
    /// The table engine's wait per read while a key is held: the firmware's
    /// answer from its idle loop, 49 machine cycles of the read path plus half
    /// its 8-cycle poll, 53 cycles (99 us)
    static constexpr uint32_t kTableWaitClocks = 53 * mcs48::Mcs48::kClocksPerCycle;

    explicit ProfiXtKbc(EmulatorContext* context);
    ~ProfiXtKbc() override;

    /// The emulated time in base T-states at `baseHz` instead of the machine's
    /// (a controller without a machine: unit tests, tools). Set before Load
    void SetTimeSource(std::function<uint64_t()> nowBaseT, uint32_t baseHz = 3500000)
    {
        _timeSource = std::move(nowBaseT);
        _timeSourceHz = baseHz;
    }

    /// Fit the controller (its power-on). Firmware: `romPath` overrides the
    /// default image. False + reason when the image cannot be read
    bool Load(Engine engine, const std::string& romPath, std::string& error);
    bool Present() const { return _loaded; }
    Engine GetEngine() const { return _engine; }
    /// Warning text when the image is the original (unpatched) dump; empty otherwise
    const std::string& ImageNote() const { return _imageNote; }
    uint32_t ImageCrc() const { return _imageCrc; }

    /// region <IPs2KeySink>
    void OnPcKey(PcKey key, bool pressed) override;
    void ReleaseAllPcKeys() override;
    bool ReplacesMatrix() const override { return true; }
    /// Caps Shift -> Left Ctrl, Symbol Shift -> Left Shift, the other matrix
    /// keys 1:1; a ZX key that is a combination (Up = Caps Shift + 7) -> its parts
    std::vector<PcKey> PcKeysForZxKey(ZXKeysEnum key) const override;
    /// Through the ZX combination: '&' = Symbol Shift + 6 -> Left Shift + 6
    std::vector<PcKey> PcKeysForCharacter(char c, const std::vector<ZXKeysEnum>& zxKeys) const override;
    std::string ControllerName() const override;
    /// endregion </IPs2KeySink>

    /// A Z80 read of an even port (/CSKBD): KD0..KD5 in bits 0..5 (0 = key
    /// closed), bits 6..7 = 1. While a key is held the Z80 waits for the answer
    /// (wait states added to the Z80); otherwise no wait and #FF
    uint8_t ReadPort(uint16_t port);

    /// Frame end: the controller and its keyboard run between reads
    void OnFrameEnd();

    /// The half-rows as the controller holds them (bits 0..5, 0 = closed):
    /// firmware RAM 20h..27h, or the table engine's matrix
    uint8_t Row(int row) const;
    /// The second mode (Scroll Lock / #AAFE), Num Lock
    bool Mode2() const;
    bool NumLock() const;
    /// The reset line (latch bit 7 low while the latch drives X9): Ctrl + Alt + Del
    bool ResetAsserted() const { return _resetOut; }

    mcs48::Mcs48* Cpu() const { return _cpu.get(); }
    uint64_t Reads() const { return _reads; }
    /// The last read's wait, in MCU oscillator clocks (0: no wait)
    uint64_t LastWaitClocks() const { return _lastWaitMcu; }
    uint8_t Latch() const { return _latch; }
    bool KeyHeld(PcKey key) const;

    /// The keyboard's side of the wire (state report, TTD). Trivial: XtLine{} is
    /// the idle keyboard
    struct XtLine
    {
        uint8_t queue[64];          ///< set-1 bytes waiting to be sent
        uint8_t head, count;
        uint8_t sending, frameByte;
        uint8_t bit;                ///< 0..8: start bit, 8 data bits
        uint8_t phase;              ///< 0: clock edge due, 1: /INT pulse ends
        uint8_t repeatKey;          ///< PcKey repeating (typematic), None = 0
        uint8_t reserved;
        uint64_t nextEdge;          ///< MCU clock of the next line change
        uint64_t idleUntil;         ///< the gap after a frame ends here
        uint64_t repeatAt;
        uint8_t held[16];           ///< bitmap of the PC keys held, by PcKey
    };

    /// TTD (fixed size, trivially copyable): everything that runs, not the ROM image
    struct State
    {
        uint32_t version;            ///< kStateVersion
        uint8_t engine;              ///< Engine: a blob of the other engine is refused
        uint8_t latch, waitSet, inRead, answered, resetOut, resetPending, tableNumLock;
        uint8_t tableMode2, reserved[5];
        uint16_t readPort;
        uint64_t tBase, mcuBase, frac, lastNow, answerClock, reads, lastWaitMcu;
        mcs48::Mcs48::State cpu;
        XtLine line;
        /// Table engine: the positions each held key closed when it was made, by PcKey
        uint64_t tableClosed[static_cast<size_t>(PcKey::Count)];
    };
    static_assert(std::is_trivial_v<State>, "the controller's TTD blob is cleared and copied as bytes");
    static constexpr uint32_t kStateVersion = 1;
    void SaveState(State& out) const;
    /// False (and nothing changed) when the blob is of another engine or version
    bool LoadState(const State& in);

private:
    uint64_t NowBase() const;
    /// CPU clocks of the current frame -> base T-states (follows the board's clock ratio; see the .cpp)
    uint64_t CpuTToBaseT(uint64_t cpuT) const;
    uint64_t BaseHz() const;
    uint64_t McuClockAt(uint64_t baseT) const;
    uint64_t MicrosToClocks(uint64_t micros) const { return micros * kCrystalHz / 1000000u; }
    void CatchUp(uint64_t baseT);
    /// Run the MCU to `target`, the keyboard's line changes in between; with `untilAnswer` it ends at the wait's release
    void RunTo(uint64_t target, bool untilAnswer);
    void LineProcess(uint64_t clock);
    uint64_t LineNextEvent() const;
    void LineEnqueue(const std::vector<uint8_t>& bytes);
    void MovxWrite(uint8_t address, uint8_t value);
    void OnPortOut(int port, uint8_t latch);
    /// The reset line follows the latch and P2.7; a new assertion resets the machine (after the MCU run)
    void UpdateResetLine();
    void FireReset();
    void AddZ80Wait(uint64_t mcuClocks);

    // Table engine
    uint8_t TableRead(uint16_t port);
    void TableKey(PcKey key, bool pressed);
    profixt::MatrixMask TableClosed() const;
    bool TableIdle() const { return TableClosed() == 0; }

    EmulatorContext* _context = nullptr;
    std::function<uint64_t()> _timeSource;
    uint32_t _timeSourceHz = 3500000;
    Engine _engine = Engine::Firmware;
    bool _loaded = false;
    std::unique_ptr<mcs48::Mcs48> _cpu;
    std::string _imageNote;
    uint32_t _imageCrc = 0;

    uint8_t _latch = 0xFF;           ///< DD3: KD0..KD5, RESC, RES
    bool _waitSet = false;           ///< DD5: the Z80 waits
    bool _inRead = false;
    bool _answered = false;
    bool _resetOut = false;
    bool _resetPending = false;
    uint16_t _readPort = 0xFFFE;

    uint64_t _tBase = 0;
    uint64_t _mcuBase = 0;
    uint64_t _frac = 0;
    uint64_t _lastNow = 0;
    uint64_t _answerClock = 0;
    uint64_t _reads = 0;
    uint64_t _lastWaitMcu = 0;

    XtLine _line{};

    // Table engine
    std::array<uint64_t, static_cast<size_t>(PcKey::Count)> _tableClosed{};
    bool _tableNumLock = false;
    bool _tableMode2 = false;
};

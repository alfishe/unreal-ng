#pragma once

/// @file zifi.h
/// @brief ZiFi (TS-Labs): the ZX-Evo AVR's TS firmware passes bytes between the
/// Z80 and an ESP module on its own UART (USART0, 115200, no flow control)
/// through two rings and a register set on #xxEF
/// (docs/inprogress/2026-10-02-tsconf-zifi/reference-zifi.md §2).
///
/// AVR register index (the FPGA packs the port's high byte, the port decoder
/// maps it): #00 the data register DR (#00EF..#BFEF), #C0..#C9 the registers:
///  - C0 ZIFR / C1 ZOFR: ZiFi bytes waiting / room, capped at #BF; the read
///    selects ZiFi for DR. C2 RIFR / C3 ROFR: the same for the RS-232 rings
///    (the 16550's own), selecting the "enhanced RS-232" DR
///  - C4 IMR (write: OR-in, one-shot) / ISR (read, then cleared)
///  - C5 ZIBTR, C6 ZITOR, C8 RIBTR, C9 RITOR: interrupt threshold (bytes) and
///    timeout (ms since the last received byte)
///  - C7 CR (SETAPI, GETVER, CLRFIFO) / ER (the last result)
/// With the API off (the reset state) every register and DR read #FF and DR
/// writes are dropped; the threshold / mask registers take writes anyway.
/// Copied from the firmware (rs232.c), not the document: counts cap at #BF,
/// no REJ result.
///
/// While ISR is not zero the AVR pulses the TS-Conf wait-port interrupt on
/// every main-loop pass: raised here at every access, received byte and frame
/// boundary until ISR is read. The AVR keeps all of this through a Z80 reset.

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include "emulator/io/serial/comport.h"

class EmulatorContext;
class ISerialPeer;

class ZiFi final
{
public:
    static constexpr uint8_t kVersion = 0x01;   ///< ZF_VER: the only API version there is
    static constexpr uint8_t kDataLimit = 0xBF; ///< the data area's last index, and the count cap

    // AVR register index
    static constexpr uint8_t kData = 0x00, kZifr = 0xC0, kZofr = 0xC1, kRifr = 0xC2, kRofr = 0xC3, kImrIsr = 0xC4,
                             kZibtr = 0xC5, kZitor = 0xC6, kCrEr = 0xC7, kRibtr = 0xC8, kRitor = 0xC9,
                             kLastRegister = 0xCF;
    // IMR / ISR bits
    static constexpr uint8_t kZfIbt = 0x01, kZfIto = 0x02, kRsIbt = 0x04, kRsIto = 0x08;

    /// @param rs the 16550's UART (its rings are the enhanced RS-232 data register's)
    /// @param raiseInterrupt the wait-port INT (TS-Conf), or nullptr (a BaseConf FPGA has none)
    ZiFi(EmulatorContext* context, Uart16550& rs, std::unique_ptr<ISerialPeer> peer,
         std::function<void()> raiseInterrupt);
    ~ZiFi();

    ZiFi(const ZiFi&) = delete;
    ZiFi& operator=(const ZiFi&) = delete;

    /// A Z80 access through #xxEF: `index` #00 (DR) or #C0..#CF
    uint8_t Read(uint8_t index);
    void Write(uint8_t index, uint8_t value);

    /// Frame boundary (machine thread): the line catches up, the peer flushes, the AVR task runs
    void OnFrame();

    /// The AVR's USART0 and the ESP (or whatever ZiFi= put there)
    ComPort& Line() { return _line; }
    const ComPort& Line() const { return _line; }

    /// Registers without side effects (status views)
    struct View
    {
        uint8_t api = 0, err = 0, imr = 0, isr = 0, zibtr = 0, zitor = 0, ribtr = 0, ritor = 0;
        bool selectZf = false;
        uint16_t zfRx = 0, zfTx = 0, rsRx = 0, rsTx = 0;   ///< ring fill
    };
    View GetView() const;

    /// TTD state (fixed size, trivial); the rings are in the UARTs' state
    struct State
    {
        uint8_t version;
        uint8_t api, err, selectZf, imr, isr, zibtr, zitor, ribtr, ritor;
        uint8_t reserved[5];
        uint64_t zfLastRx, rsLastRx;   ///< base T-states of the last received byte, kNever: none yet
    };
    static constexpr uint8_t kStateVersion = 1;
    void SaveState(State& out) const;
    void LoadState(const State& in);

private:
    static constexpr uint64_t kNever = ~0ull;

    void Task(uint64_t now);
    /// The AVR's ms counter since the last received byte (saturates at 255)
    uint8_t TimeoutCount(uint64_t lastRx, uint64_t now) const;

    EmulatorContext* _context = nullptr;
    Uart16550& _rs;
    ComPort _line;
    std::function<void()> _raiseInterrupt;
    uint32_t _clockHz = 3500000;

    uint8_t _api = 0, _err = 0, _imr = 0, _isr = 0;
    uint8_t _zibtr = 0x80, _zitor = 0x01, _ribtr = 0x80, _ritor = 0x01;
    bool _selectZf = false;
    uint64_t _zfLastRx = kNever, _rsLastRx = kNever;
};

static_assert(std::is_trivially_copyable_v<ZiFi::State>);
static_assert(sizeof(ZiFi::State) == 32, "the TTD blob layout (ttd.ksy id 40)");

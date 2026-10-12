#pragma once

/// @file bustraceevent.h
/// @brief The bus analyzer's event model (docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §2):
/// transactions on a peripheral bus (chip select, byte exchanged, early access) and the protocol events of the
/// devices on it (an SD command with its R1, a block read or written), plus the findings the checks raise.
///
/// Transaction level, not wire level: no bus here runs wires in the emulator, and a transaction is what a
/// driver writer reasons about. Wires are synthesized only for export (busexport.h).

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

enum class BusProtocol : uint8_t
{
    Spi = 0,
    I2c,
    Uart,
    Ps2,
    Ata,
    Fdc,
    Parallel,
};

/// Who drove the bus for the event
enum class BusSource : uint8_t
{
    Cpu = 0,  ///< the machine's CPU (an I/O instruction)
    Dma,      ///< the machine's DMA (TS-Conf SPI DMA)
    CardCpu,  ///< a card's own CPU (NeoGS)
    CardDma,  ///< a card's own DMA (NeoGS)
    Host,     ///< the emulator itself (automation, a shell hand-off)
};

enum class BusEventKind : uint8_t
{
    // SPI, recorded by the master
    SpiSelect = 0,  ///< a chip select became active (`slot`)
    SpiDeselect,    ///< a chip select became inactive
    SpiByte,        ///< one byte exchanged: `a` = MOSI, `b` = MISO, flag Read = clocked by a read
    SpiEarly,       ///< an access before the byte in flight finished: flag Write, `a` = the value written
    SpiTruncated,   ///< the byte in flight was cut off by a restart
    // SD protocol, recorded by the card
    SdCommand,      ///< `a` = index, `b` = R1 (#FF: none), `arg`, flags App / CrcValid
    SdReadBlock,    ///< a block queued for the host: `block`
    SdWriteBlock,   ///< a block received: `block`, flag Ok = stored
    // ATA, recorded by the channel (slot = the unit the device register selects)
    AtaRegRead,     ///< `a` = register (0..7, 8 = alternate status), `b` = value
    AtaRegWrite,    ///< `a` = register (8 = device control), `b` = value
    AtaCommand,     ///< `a` = command (written to register 7)
    AtaData,        ///< one data word: `arg`, flag Write
    Count,
};

namespace BusEventFlag
{
constexpr uint8_t Read = 0x01;
constexpr uint8_t App = 0x02;
constexpr uint8_t CrcValid = 0x04;
constexpr uint8_t Ok = 0x08;
constexpr uint8_t Write = 0x10;
}  // namespace BusEventFlag

struct BusTraceEvent
{
    uint64_t seq = 0;
    uint64_t block = 0;
    uint32_t epoch = 0;
    uint32_t frame = 0;
    uint32_t t = 0;
    uint32_t arg = 0;
    uint16_t pc = 0;
    BusEventKind kind = BusEventKind::SpiByte;
    BusSource source = BusSource::Cpu;
    uint8_t slot = 0;
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t flags = 0;
};
static_assert(std::is_trivially_copyable_v<BusTraceEvent>);

/// The checks (requirements.md FR-7)
enum class BusRule : uint8_t
{
    EarlyAccess = 0,     ///< a read or write before the byte in flight finished
    SelectMidByte,       ///< a chip select changed with a byte in flight
    DeselectMidCommand,  ///< the card was deselected inside a 6-byte command
    CommandWhileBusy,    ///< a command started while the card still signalled write busy
    CommandDropsAnswer,  ///< a command started while a data block was still queued for the host
    CrcError,            ///< the card answered with the CRC error bit
    DmaCollision,        ///< the CPU accessed the bus ports while an SPI DMA was running
    AtaDataWithoutDrq,   ///< ATA: the data register accessed while DRQ was clear
    AtaCommandDropsData, ///< ATA: a command written while DRQ was set (a transfer not finished)
    AtaWriteWhileBusy,   ///< ATA: a task-file register written while BSY was set
    Count,
};

enum class BusSeverity : uint8_t
{
    Info = 0,
    Warning,
    Error,
};

struct BusFinding
{
    uint64_t seq = 0;
    uint64_t eventSeq = 0;  ///< the newest event recorded when the finding was raised (0: tracing off)
    uint32_t epoch = 0;
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    BusRule rule = BusRule::EarlyAccess;
    BusSource source = BusSource::Cpu;
    uint8_t slot = 0;
    uint8_t value = 0;      ///< the byte involved (the value written, the command index, the R1)
};
static_assert(std::is_trivially_copyable_v<BusFinding>);

const char* BusProtocolName(BusProtocol protocol);
const char* BusSourceName(BusSource source);
const char* BusEventKindName(BusEventKind kind);
bool BusEventKindFromName(std::string_view name, BusEventKind& kind);
const char* BusRuleName(BusRule rule);
bool BusRuleFromName(std::string_view name, BusRule& rule);
BusSeverity BusRuleSeverity(BusRule rule);
const char* BusSeverityName(BusSeverity severity);
/// What the rule means on the real hardware, one sentence
const char* BusRuleDetail(BusRule rule);
/// "CMD17", "ACMD41"
std::string SdCommandName(uint8_t index, bool app);
/// "READ SECTORS (#20)"
std::string AtaCommandName(uint8_t command);
/// "status", "sector_count", ...
const char* AtaRegisterName(uint8_t reg, bool write);

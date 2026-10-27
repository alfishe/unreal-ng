#pragma once

/// @file bustap.h
/// @brief One bus as the analyzer sees it (technical-design.md §3.2): its id, protocol and devices, the trace
/// and the checks. Owned by the bus master's owner (a port decoder, a card), registered with the emulator's
/// BusManager by Attach and unregistered by the destructor.
///
/// The master calls the recording methods only when Active() - the one hot-path test. Recording takes the
/// tap's mutex; readers on other threads get copies.
///
/// Time and PC come from the stamp function: the default reads the main Z80 (frame counter, T in the frame,
/// the PC of the instruction) and records nothing while TTD replays history; a card with its own CPU sets
/// its own.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "emulator/bus/bustraceevent.h"
#include "emulator/bus/bustrace.h"

class BusManager;
class EmulatorContext;
class SdCardSpi;

struct BusStamp
{
    uint32_t frame = 0;
    uint32_t t = 0;
    uint16_t pc = 0;
    bool skip = false;  ///< record nothing (TTD replay)
};

struct BusDevice
{
    uint8_t slot = 0;
    std::string name;
};

struct BusTapStatus
{
    std::string id;
    BusProtocol protocol = BusProtocol::Spi;
    std::string label;
    double byteTimeNs = 0;
    std::vector<BusDevice> devices;
    bool tracing = false;
    bool checking = false;
    bool protocolOnly = false;
    size_t size = 0;
    size_t capacity = 0;
    uint64_t evicted = 0;
    uint64_t rewound = 0;
    uint64_t lastSeq = 0;
    uint64_t findings = 0;      ///< raised since the last clear
    size_t findingsKept = 0;
    std::array<uint64_t, static_cast<size_t>(BusRule::Count)> byRule{};
};

class BusTap
{
public:
    /// The slot of a byte exchanged with no device selected
    static constexpr uint8_t kNoSlot = 0xFF;

    BusTap(std::string id, BusProtocol protocol, std::string label);
    ~BusTap();
    BusTap(const BusTap&) = delete;
    BusTap& operator=(const BusTap&) = delete;

    /// Register with the context's BusManager (nullptr context or manager: stays unregistered and inactive
    /// unless controlled directly, as unit tests do)
    void Attach(EmulatorContext* context);
    void Detach();

    /// Trace or checks on
    bool Active() const { return _active.load(std::memory_order_relaxed); }
    bool Checking() const { return _checking.load(std::memory_order_relaxed); }

    const std::string& Id() const { return _id; }
    BusProtocol Protocol() const { return _protocol; }

    void SetStamp(std::function<void(BusStamp&)> stamp);
    void SetDevice(uint8_t slot, std::string name);
    void SetByteTimeNs(double ns);
    /// The source of the next events (a DMA brackets its exchanges)
    void SetSource(BusSource source) { _source = source; }
    BusSource Source() const { return _source; }

    /// region <Recording (only when Active)>
    void SpiSelect(uint8_t slot, bool selected, bool byteInFlight = false);
    void SpiByte(uint8_t slot, uint8_t mosi, uint8_t miso, bool read);
    void SpiEarly(uint8_t slot, bool write, uint8_t value);
    void SpiTruncated(uint8_t slot);
    /// The CPU accessed the SPI ports while an SPI DMA was running
    void DmaCollision(bool write, uint8_t value);
    void SdCommand(uint8_t slot, uint8_t index, bool app, uint32_t argument, bool crcValid, uint8_t r1);
    void SdBlock(uint8_t slot, bool write, uint64_t block, bool ok);
    /// ATA (the channel; `unit` = the selected one)
    void AtaRegister(uint8_t unit, uint8_t reg, uint8_t value, bool write);
    void AtaCommand(uint8_t unit, uint8_t command);
    void AtaData(uint8_t unit, uint16_t word, bool write);
    /// A check failed (only when Checking)
    void Finding(BusRule rule, uint8_t slot, uint8_t value = 0);
    /// endregion

    /// A machine reset: the frame counter restarts
    void NextEpoch();

    /// The SD card in a slot (SdCardSpi::SetBusTap keeps it; nullptr removes it). WithSdCard runs `fn` under the
    /// tap's lock, so the card cannot go away meanwhile; false when the slot has no card
    void SetSdCard(uint8_t slot, SdCardSpi* card);
    bool WithSdCard(uint8_t slot, const std::function<void(SdCardSpi&)>& fn) const;
    /// The slots that hold an SD card
    std::vector<uint8_t> SdCardSlots() const;

    /// region <Control and reads (any thread)>
    /// `enable` / `checks`: 1 on, 0 off, -1 keep; `capacity` > 0 resizes the event ring (content dropped); `protocolOnly`
    /// 1 records no single bytes / data words / status reads (selects, early accesses, SD commands and blocks, ATA
    /// commands and register writes stay: a whole boot fits the ring), 0 records everything, -1 keeps
    void Control(int enable, int checks, bool clear, size_t capacity = 0, int protocolOnly = -1);
    BusTapStatus Status() const;
    std::vector<BusTraceEvent> Events(const BusEventQuery& query) const;
    std::vector<BusFinding> Findings(const BusFindingQuery& query) const;
    /// endregion

private:
    friend class BusManager;
    void SetIdLocked(std::string id) { _id = std::move(id); }
    bool Stamp(BusStamp& stamp) const;
    void Record(BusEventKind kind, uint8_t slot, uint8_t a = 0, uint8_t b = 0, uint8_t flags = 0, uint32_t arg = 0, uint64_t block = 0);

    const std::string _baseId;
    std::string _id;  ///< _baseId, or with a suffix when the id was taken
    BusProtocol _protocol;
    std::string _label;
    double _byteTimeNs = 0;
    std::vector<BusDevice> _devices;
    std::vector<std::pair<uint8_t, SdCardSpi*>> _sdCards;

    EmulatorContext* _context = nullptr;
    BusManager* _manager = nullptr;
    std::function<void(BusStamp&)> _stamp;
    BusSource _source = BusSource::Cpu;
    uint32_t _epoch = 0;

    std::atomic<bool> _active{false};
    std::atomic<bool> _tracing{false};
    std::atomic<bool> _checking{false};
    bool _protocolOnly = false;
    mutable std::mutex _mutex;
    BusTrace _trace;
};

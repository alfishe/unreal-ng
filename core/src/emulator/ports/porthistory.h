#pragma once

/// @file porthistory.h
/// @brief The port accesses of each registry device, a history apiece (debugger/history/devicehistory.h): every
/// built-in and card of the slot manager's registry has `<device>.ports` (`beta128.ports`, `zxbus.1.ports`), and the
/// ports no device claims go to `board.ports`. Each has its own mode, limit and stream: one device's traffic never
/// crowds out another's. The machine-wide port trace (portdiagrecorder.h) stays the decode-debugging tool.
///
/// Cost on the IN / OUT path: with every port history off, one relaxed load of the count of those on. With one on,
/// the owner from a snapshot of the registry's claims (taken when the histories are built), then a push into that
/// owner's history when it is on.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "debugger/history/devicehistory.h"

class EmulatorContext;
struct StateNode;
namespace slots
{
class PortOwners;
}

/// One IN or OUT
struct PortAccessRecord
{
    uint64_t frame = 0;
    uint16_t tacts = 0;
    uint16_t pc = 0;          ///< PC of the IN / OUT (the main CPU's)
    uint16_t port = 0;        ///< the address bus
    uint8_t value = 0;        ///< read or written
    bool out = false;
    bool dos = false;         ///< the TR-DOS ROM was paged

    void ToState(StateNode& node) const;
};

class PortHistories
{
public:
    static constexpr size_t kDefaultRing = 4096;

    PortHistories();
    ~PortHistories();

    /// A history for every device the registry has now (kept across a rebuild by name), the claims snapshot. After
    /// the slot manager built the cards
    void Build(EmulatorContext* context);

    /// The whole cost while every port history is off
    bool AnyOn() const
    {
        return _on.load(std::memory_order_relaxed) != 0;
    }

    /// One access, after the decoder applied it (emulation thread). The caller checks AnyOn(). The owner comes from a
    /// table (port, direction, DOS) -> device, built at the first access after Build: one load an access
    void Record(uint16_t port, uint8_t value, bool out, bool dos, uint16_t pc);

    /// The registry ids that have one ("beta128", ..., "board"); tests
    std::vector<std::string> Devices() const;

private:
    using History = history::Registered<PortAccessRecord>;
    struct Device
    {
        std::string id;
        std::unique_ptr<History> history;
    };

    void BuildTable();

    EmulatorContext* _context = nullptr;
    std::atomic<int> _on{ 0 };
    std::unique_ptr<slots::PortOwners> _owners;  ///< the claims snapshot of the last Build
    std::vector<Device> _devices;   ///< the board's first
    std::vector<uint8_t> _table;    ///< device index by (out << 17 | dos << 16 | port); empty until the first access
};

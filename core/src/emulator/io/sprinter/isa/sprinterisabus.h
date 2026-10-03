#pragma once

/// @file sprinterisabus.h
/// @brief The Sprinter Sp2000's two ISA-8 slots (docs/inprogress/2026-10-02-sprinter-isa/tdd.md §4, phase I1).
///
/// A program reaches a slot through memory window 3: with `#1FFD` bit 4 set, the pages #D0 / #D2 / #D4 / #D6
/// mean ISA instead of RAM. Page bit 2 picks the space (0 memory, 1 I/O), bit 1 the slot (0 = slot 1 / J6,
/// 1 = slot 2 / J7). The ISA address is 20 bits: the `#9FBD` latch (port-table code #1B) gives A19-A14, the
/// CPU gives A13-A0. The latch's bit 6 is AEN, bit 7 the slots' RESET DRV (1 = cards held in reset).
///
/// Worked example (the RTL8019AS kit reads the chip ID in slot 2): `#1FFD` <- #11, `OUT (#E2),#D6`,
/// `#9FBD` <- #00, `LD A,(#C30A)`: SprinterMemory sees window 3 in ISA mode and calls
/// Read(Space::Io, 1, #030A); the address is (#00 << 14) | #030A = #0030A; the NE2000 in slot 2 at base #300
/// answers register #0A of page 0: #50 ('P').
///
/// - An empty slot, a card that does not drive the bus, or a cycle while RESET is held reads #FF (the data
///   bus pull-ups); writes vanish.
/// - The latch has no reset input (research §4.3): a machine reset keeps it; power-on starts it at 0
///   (MAME, unreal-ng). ISA RESET is driven only by software.
/// - The bus is owned by PortDecoder_Sprinter; it owns the card objects (network cards are wrappers around
///   devices NetworkManager owns). Everything runs on the emulator thread.
/// - Cost for other machines: none (all of it sits behind the Sprinter's own window-3 bank actions). The
///   Sprinter pays only while window 3 maps ISA.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "emulator/io/sprinter/isa/iisacard.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"
#include "emulator/state/statenode.h"

class SprinterIsaBus
{
public:
    enum class Space : uint8_t
    {
        Memory = 0,
        Io = 1,
    };

    static constexpr int kSlots = sprinterisa::kSlots;
    static constexpr uint8_t kLatchAddressMask = 0x3F;
    static constexpr uint8_t kLatchAen = 0x40;
    static constexpr uint8_t kLatchReset = 0x80;

    /// The window-3 page that shows a slot: true for #D0 / #D2 / #D4 / #D6
    static bool PageToSlot(uint8_t page, Space& space, int& slot);
    /// The page a program writes for (space, slot): #D0 + slot * 2 + (Io ? 4 : 0)
    static uint8_t SlotPage(Space space, int slot)
    {
        return static_cast<uint8_t>(0xD0 + (slot & 1) * 2 + (space == Space::Io ? 4 : 0));
    }

    SprinterIsaBus();
    ~SprinterIsaBus();

    SprinterIsaBus(const SprinterIsaBus&) = delete;
    SprinterIsaBus& operator=(const SprinterIsaBus&) = delete;

    // --- The #9FBD latch (code #1B) -------------------------------------------

    void WriteLatch(uint8_t value);
    uint8_t Latch() const { return _latch; }
    /// Power-on: the latch starts at 0 (RESET released); the cards stay as they are
    void PowerOn();

    /// The 20-bit ISA address of a window-3 offset (A13-A0)
    uint32_t Address(uint16_t offset) const
    {
        return (static_cast<uint32_t>(_latch & kLatchAddressMask) << 14) | (offset & 0x3FFFu);
    }

    // --- Cycles (SprinterMemory, window 3 in ISA mode) --------------------------

    uint8_t Read(Space space, int slot, uint16_t offset) { return ReadAt(space, slot, Address(offset)); }
    void Write(Space space, int slot, uint16_t offset, uint8_t value) { WriteAt(space, slot, Address(offset), value); }
    /// Debugger, memory viewer, automation reads: no side effect
    uint8_t Peek(Space space, int slot, uint16_t offset) const { return PeekAt(space, slot, Address(offset)); }

    /// A cycle at a full 20-bit ISA address (automation's control/isa; the window path above uses the latch)
    uint8_t ReadAt(Space space, int slot, uint32_t address);
    void WriteAt(Space space, int slot, uint32_t address, uint8_t value);
    uint8_t PeekAt(Space space, int slot, uint32_t address) const;

    // --- Population ------------------------------------------------------------

    /// The configured population ([ISA], read at instance creation). Kinds this build does not have are
    /// refused with the reason (shown in the report); the machine always starts
    void Configure(const sprinterisa::IsaConfig& config);
    const sprinterisa::SlotConfig& Configured(int slot) const { return _slots[slot & 1].config; }

    /// Put a card into a slot (the previous one leaves). `card` = nullptr empties it
    void Fit(int slot, std::unique_ptr<sprinterisa::IIsaCard> card);
    sprinterisa::IIsaCard* Card(int slot) const { return _slots[slot & 1].card.get(); }
    /// Why the configured card is not in the slot (empty: fitted, or nothing configured)
    void SetRefusal(int slot, std::string reason) { _slots[slot & 1].refusal = std::move(reason); }
    const std::string& Refusal(int slot) const { return _slots[slot & 1].refusal; }

    /// The frame boundary: the cards' FrameEnd
    void FrameEnd();

    // --- Observation (not machine state) ----------------------------------------

    struct Counters
    {
        uint64_t ioReads = 0;
        uint64_t ioWrites = 0;
        uint64_t memReads = 0;
        uint64_t memWrites = 0;
        uint64_t resetPulses = 0;   ///< RESET DRV 0 -> 1 edges
    };
    const Counters& GetCounters(int slot) const { return _slots[slot & 1].counters; }

    /// Every real cycle (not peeks): the Sprinter port trace shows ISA cycles (dispositions isa_io / isa_mem)
    using Tracer = std::function<void(bool write, Space space, int slot, uint32_t address, uint8_t value)>;
    void SetTracer(Tracer tracer) { _tracer = std::move(tracer); }

    /// The slot report every automation surface prints (DeviceState::Isa): latch, slots, cards, their resources
    /// (ISA I/O range, memory window, IRQ) and how the Z80 reaches them now, conflicts, counters
    StateNode Describe() const;

    // --- Access journal (observation, not machine state) ------------------------------------------

    /// One ISA cycle or bus event with its time: frame, base T-state in the frame, PC
    struct JournalEntry
    {
        uint64_t frame = 0;
        uint32_t t = 0;
        uint16_t pc = 0;
        int8_t slot = -1;        ///< 0 / 1; -1 for a bus event (RESET)
        bool io = true;
        bool write = false;
        uint32_t address = 0;
        uint8_t value = 0;
        std::string what;        ///< the card's register name, or the event ("reset asserted", "stall")
    };
    static constexpr size_t kJournalLength = 512;
    /// Who touched which card register, newest last (every cycle while the machine runs or replays a recording)
    const std::deque<JournalEntry>& Journal() const { return _journal; }
    void ClearJournal() { _journal.clear(); }
    void SetJournalEnabled(bool on) { _journalOn = on; }
    bool JournalEnabled() const { return _journalOn; }
    /// Where the time comes from (the decoder: frame counter, base T, the PC of the access)
    using Clock = std::function<void(uint64_t& frame, uint32_t& t, uint16_t& pc)>;
    void SetClock(Clock clock) { _clock = std::move(clock); }

    /// A card that never finishes a cycle (UM9003 reset port): the decoder hangs the CPU
    using StallHandler = std::function<void(int slot)>;
    void SetStallHandler(StallHandler handler) { _stall = std::move(handler); }

    // --- TTD (PeripheralId::SprinterIsa = 33) -----------------------------------

    static constexpr uint8_t kStateVersion = 1;
    /// version, latch, the fitted kind of each slot, then each card's own state
    size_t StateSize() const;
    void SaveState(uint8_t* dst) const;
    /// False with `why` when the blob was recorded with another population (nothing is loaded)
    bool LoadState(const uint8_t* src, size_t size, std::string& why);
    /// Whether a recorded blob's population matches this bus (the session-load guard)
    bool PopulationMatches(const uint8_t* src, size_t size, std::string& why) const;

private:
    struct Slot
    {
        sprinterisa::SlotConfig config{};
        std::unique_ptr<sprinterisa::IIsaCard> card;
        std::string refusal;
        Counters counters;
    };

    sprinterisa::CardKind FittedKind(int slot) const;

    void Note(int slot, bool io, bool write, uint32_t address, uint8_t value, std::string what);
    void AfterCycle(int slot);

    std::array<Slot, kSlots> _slots;
    uint8_t _latch = 0;
    Tracer _tracer;
    Clock _clock;
    StallHandler _stall;
    bool _journalOn = true;
    std::deque<JournalEntry> _journal;
};

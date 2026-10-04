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
    /// A UART card's line changed at runtime (the card is fitted again by NetworkManager)
    void SetConfiguredPeer(int slot, int channel, const std::string& peer);

    /// Put a card into a slot (the previous one leaves). `card` = nullptr empties it
    void Fit(int slot, std::unique_ptr<sprinterisa::IIsaCard> card);
    sprinterisa::IIsaCard* Card(int slot) const { return _slots[slot & 1].card.get(); }
    /// Why the configured card is not in the slot (empty: fitted, or nothing configured)
    void SetRefusal(int slot, std::string reason) { _slots[slot & 1].refusal = std::move(reason); }
    const std::string& Refusal(int slot) const { return _slots[slot & 1].refusal; }

    /// The frame boundary: the cards' FrameEnd
    void FrameEnd();

    // --- Interrupt and DMA lines (phase I4, tdd §4.5) -------------------------------------------
    //
    // The board (SP2000 schematic, SPRINT_3): every IRQ pin of a slot (B4 IRQ2/9, B21-B25 IRQ7-IRQ3) is one net,
    // IRQ1 for J6 and IRQ2 for J7, with a 3.9 kOhm pull-up (R167 / R168), wired straight to the Z84C15's PIO port
    // B: PB0 = slot 1 IRQ, PB1 = slot 2 IRQ, PB2 = slot 2 DRQ, PB3 = slot 2 DACK, PB4 = slot 1 DRQ, PB5 = slot 1
    // DACK (DRQ / DACK pulled up too: R165, R166, R169, R170), PB6 / PB7 the printer. ISA IRQs are active high: a
    // card that drives its pin holds it low until it requests; a pin nobody drives reads high.
    //
    // Worked example (BC-Term, a modem in slot 1): the program writes PIO B control #00 (vector 0), #CF (mode 3),
    // #01 (PB0 input), #B7 (interrupt enabled, OR, active high, a mask follows), #FE (PB0 monitored), #83; the UART
    // drives IRQ low; a received character raises INTR, PB0 goes high, the PIO requests and the CPU takes IM 2
    // vector #00 through the Z84C15 daisy chain.

    static constexpr uint8_t kPioIrqBit[kSlots] = {0x01, 0x02};
    static constexpr uint8_t kPioDrqBit[kSlots] = {0x10, 0x04};
    static constexpr uint8_t kPioDackBit[kSlots] = {0x20, 0x08};
    /// The PIO port B input byte the slots give: IRQ and DRQ per slot (a card's level, the pull-up where nobody
    /// drives), every other bit high (the pull-ups; the printer lines are not modeled)
    uint8_t PioLines() const;
    /// Slot `slot`'s IRQ line level (true = high) and whether a card drives it
    bool IrqLine(int slot) const;
    bool IrqDriven(int slot) const;
    /// The earliest machine time any fitted card's line may change by itself (UINT64_MAX: none)
    uint64_t NextLineEventAt() const;
    /// Bring every fitted card to now (the deadline above passed while the CPU waits for an interrupt)
    void CatchUpCards();
    /// Called when the lines (or their next event) may have changed: after every cycle, a RESET DRV edge, a refit,
    /// and whenever a card says so outside a cycle. The owner (the Sprinter decoder) pushes PioLines into the PIO
    void SetLinesHandler(std::function<void()> handler) { _linesHandler = std::move(handler); }
    void LinesMayHaveChanged()
    {
        if (_linesHandler)
            _linesHandler();
    }

    /// What the owner tells the report about the PIO port B and the CPU (the bus does not know the chip)
    struct PioView
    {
        bool valid = false;
        uint8_t mode = 1;           ///< 0 output, 1 input, 2 bidirectional, 3 bit control
        uint8_t direction = 0xFF;   ///< mode 3: 1 = input
        uint8_t mask = 0xFF;        ///< mode 3: 1 = not monitored
        uint8_t intControl = 0;     ///< bit 7 enable, 6 AND, 5 active high
        uint8_t vector = 0;
        uint8_t inputs = 0xFF;      ///< the input byte the PIO holds
        uint8_t output = 0;
        uint8_t read = 0xFF;        ///< what IN A,(#1E) returns now
        bool condition = false;
        bool pending = false;       ///< IP
        bool underService = false;  ///< IUS
        uint8_t priority = 0;       ///< #F4
        uint8_t im = 0;
        bool iff1 = false;
        uint8_t i = 0;
    };
    void SetPioView(std::function<PioView()> view) { _pioView = std::move(view); }

    // --- Observation (not machine state) ----------------------------------------

    struct Counters
    {
        uint64_t ioReads = 0;
        uint64_t ioWrites = 0;
        uint64_t memReads = 0;
        uint64_t memWrites = 0;
        uint64_t resetPulses = 0;   ///< RESET DRV 0 -> 1 edges
        // IRQ line (phase I4)
        uint64_t irqRises = 0;      ///< the slot's IRQ line went high
        uint64_t irqFalls = 0;
        uint64_t pioRequests = 0;   ///< the PIO latched an interrupt request with this slot's line active
        uint64_t acknowledged = 0;  ///< the CPU took the PIO port B interrupt while this slot's line was the cause
        uint64_t serviceEnds = 0;   ///< RETI ended that service
    };
    Counters& MutableCounters(int slot) { return _slots[slot & 1].counters; }
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
        bool irq = false;        ///< an interrupt event (line edge, PIO request, acknowledge, RETI): `what` says which
    };
    /// An interrupt event of a slot (-1: the PIO port B as a whole) into the journal, and into the interrupt ring below
    void NoteIrq(int slot, uint8_t value, std::string what);
    /// The interrupt events alone (line edges, PIO requests, acknowledges, RETI), newest last: a program that polls
    /// a card (BC-Term reads MSR thousands of times a frame) pushes them out of the access journal in a few frames
    static constexpr size_t kIrqJournalLength = 128;
    const std::deque<JournalEntry>& IrqJournal() const { return _irqJournal; }
    static constexpr size_t kJournalLength = 512;
    /// Who touched which card register, newest last (every cycle while the machine runs or replays a recording)
    const std::deque<JournalEntry>& Journal() const { return _journal; }
    void ClearJournal()
    {
        _journal.clear();
        _irqJournal.clear();
    }
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

    void DescribeIrq(int slot, const PioView& pio, StateNode& out) const;

    std::array<Slot, kSlots> _slots;
    std::function<void()> _linesHandler;
    std::function<PioView()> _pioView;
    uint8_t _latch = 0;
    Tracer _tracer;
    Clock _clock;
    StallHandler _stall;
    bool _journalOn = true;
    std::deque<JournalEntry> _journal;
    std::deque<JournalEntry> _irqJournal;
};

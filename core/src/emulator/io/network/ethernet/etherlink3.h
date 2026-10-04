#pragma once

/// @file etherlink3.h
/// @brief The 3Com EtherLink III 3C509B ISA Ethernet card (network tdd §9, phase SN5): its Parallel Tasking ASIC, the
/// 8 KB packet SRAM and the configuration EEPROM. Machine-independent: an IEthernetCard, so any machine's bus (the
/// Sprinter's ISA slot wrapper today) reaches it through IIoBusDevice, and the Ethernet gateway through
/// IEthernetPort.
///
/// Source: 3Com "EtherLink III Parallel Tasking ISA, EISA, Micro Channel, and PCMCIA Adapter Drivers Technical
/// Reference", 09-0398-002B, August 1994 (cited below as "TR <page>"); Linux drivers/net/ethernet/3com/3c509.c; the
/// Sprinter 3C509B Network Kit (EEPROM dumps of real 3C509B-TPO / -TP boards, its driver's register use).
///
/// Bus side (ISA I/O, A15-A0 decoded - the ASIC needs A15-A12 for its EISA slot addressing, TR 7-6; AEN must be 0):
///   ID port   any #1x0 (#100-#1F0): the ID sequence state machine (IDS, TR 7-2). Writes of 0 to any of them pick
///             that port; the 255-byte LFSR sequence (#FF, then x2 ^ #CF on carry, ending #98) enters ID_CMD; then
///             commands: 00-7F back to ID_WAIT, 80-BF read EEPROM word n (162 us), C0-CF global reset, D0-D7 set the
///             tag, D8-DF test the tag, E0-FE activate at #200 + 16 * (cmd & #1F), FF activate at the EEPROM's base.
///             A read in ID_CMD (tag 0) drives EEPROM data bit 15 on D0 (open drain: the other bits float high) and
///             shifts the data register left (the contention test; one card per ISA slot here, so it always wins)
///   base      16 registers once activated: #0E/#0F command (write, runs on the high byte) / status (read) in every
///             window; the rest by window (TR 5-1..5-5): 0 setup + EEPROM access, 1 operating (TX / RX PIO FIFO at
///             +0..+3, RX status +8, timer +A, TX status +B, TX free +C), 2 station address, 3 FIFO management, 4
///             diagnostics (FIFO diag, net diag, Ethernet controller status, media type and status), 5 command results,
///             6 statistics. 16-bit registers in this 8-bit slot: the low byte first, then the high byte with no other
///             card cycle between (TR 6-1); a read of the low byte latches the high one
///
/// The card (TR 3-1, 4-1..4-4, 6-16..6-21, 7-23):
///   SRAM      8 KB split 3:5 (EEPROM internal configuration words 12h / 13h: TX 3 KB, RX 5 KB); 4 bytes of each FIFO
///             are never free (a full FIFO is told from an empty one), every packet costs 4 more (its status / header)
///   TX        preamble word (length, bit 15 interrupt on success, bit 13 disable CRC), a zero word, the data padded
///             to 4; it leaves when complete or past the TX start threshold, takes ((len + 4 + 20) * 2.8) T on the wire
///             (10 Mbit/s at 3.5 MHz T-states), then pushes a TX status (31 deep) if asked or failed and frees its
///             space; a host slower than the wire after an early start underruns (status #90, TX reset needed)
///   RX        a frame the filter takes (individual / group / broadcast / promiscuous) goes into the RX FIFO whole; RX
///             status shows the top packet's length counting down as it is read, RX Discard pops it. A full FIFO
///             makes the gateway keep the frame (a switch buffer, network tdd §7.6) - the same as the NE2000
///   media     10BASE-T: link beat detected 48 ms after the link beat enable with a cable (three link pulses, 16 ms
///             apart, IEEE 802.3 link integrity test); without link pass no frame goes out or comes in
///   IRQ       the interrupt latch (status bit 0) follows (status & read zero mask & interrupt mask); the selected IRQ
///             (resource configuration bits 15-12) is driven only with ENA set and a window other than 0 (TR 7-13), and
///             only 3, 5, 7, 9 exist on an 8-bit slot
///
/// Worked example (the 3C509B kit's IFUP finds the card): writes #00 #00 #FF #FE #FB ... #98 to #C110 (slot open, ID
/// port #110), #D0 (tag 0), #87 then sixteen reads of #C110 -> bit 15..0 of EEPROM word 7 = #6D50; ... #FF activates
/// the card at the EEPROM's base #300; a read of #C300/#C301 -> #50 #6D (manufacturer ID), #C302/#C303 -> product ID.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "emulator/io/network/ethernet/ethernetcard.h"

class EtherLink3 final : public IEthernetCard
{
public:
    enum class Variant : uint8_t
    {
        Tpo = 0,   ///< 3C509B-TPO: RJ-45 only, product ID #9550
        Tp = 1,    ///< 3C509B-TP: RJ-45 + an AUI connector, product ID #9050
    };
    static const char* VariantName(Variant v);

    struct Settings
    {
        Variant variant = Variant::Tpo;
        uint16_t base = 0x300;        ///< the EEPROM's I/O base: #200..#3E0 in steps of #10
        uint8_t irq = 3;              ///< the EEPROM's IRQ (resource configuration bits 15-12)
        std::array<uint8_t, 6> mac{};
        std::string key = "eth";      ///< the port key ("isa2.eth")
    };

    static constexpr int kEepromWords = 64;
    /// Register offsets of the ID port: kIdPortOffset + ((address >> 4) & #F) for the ports #100..#1F0
    static constexpr uint16_t kIdPortOffset = 0x100;
    static constexpr uint16_t kManufacturerId = 0x6D50;   ///< "TCM", byte-swapped (TR 7-13)

    /// `clock`: the machine's time in base T-states (3.5 MHz units)
    EtherLink3(const Settings& settings, std::function<uint64_t()> clock);

    const Settings& GetSettings() const { return _settings; }

    /// The EEPROM image of a board with these settings: the verified real boards' layout (words 00-37h of a
    /// 3C509B-TPO, assembly 03-0020-002, and a 3C509B-TP read by the kit's EL3EEP on a Sprinter) with the MAC, base and
    /// IRQ of the settings and both checksums and the Plug and Play serial identifier's checksum computed again
    static std::array<uint16_t, kEepromWords> BuildEeprom(const Settings& settings);
    /// Both EEPROM checksums (TR 7-28, 7-29: word 0Fh, word 17h) of an image
    static uint16_t PrimaryChecksum(const std::array<uint16_t, kEepromWords>& words);
    static uint16_t SecondaryChecksum(const std::array<uint16_t, kEepromWords>& words);
    const std::array<uint16_t, kEepromWords>& Eeprom() const { return _eeprom; }

    // IEthernetCard
    void SetLink(IEthernetLink* link) override;
    IEthernetLink* Link() const override { return _link; }
    size_t CardStateBound() const override;
    void SaveCardState(std::vector<uint8_t>& out) const override;
    bool LoadCardState(const uint8_t* src, size_t size) override;

    // IIoBusDevice
    const char* Kind() const override { return "el3c509b"; }
    bool Decodes(uint32_t address, uint16_t& offset) const override;
    std::string DecodeNote() const override;
    uint8_t Read(uint16_t offset) override;
    void Write(uint16_t offset, uint8_t value) override;
    uint8_t Peek(uint16_t offset) const override;
    /// ISA RESET DRV: a power-on reset (the EEPROM is read again, the card leaves its I/O base until activated)
    void Reset() override;
    bool IoRange(uint32_t& first, uint32_t& last) const override;
    std::vector<AuxIoRange> AuxIoRanges() const override;
    int IrqLine() const override;
    const char* RegisterName(uint16_t offset, bool write) const override;
    bool Irq() const override { return _s.latch != 0; }
    void SetIrqListener(std::function<void()> changed) override { _irqListener = std::move(changed); }
    bool IrqDriven() const override;
    uint64_t NextIrqEventAt() const override;
    void CatchUp() override { Advance(Now()); }
    std::string IrqCause() const override;
    void OnFrame() override { Advance(Now()); }
    void Describe(StateNode& out) const override;

    // IEthernetPort
    const std::string& PortKey() const override { return _settings.key; }
    /// The EEPROM's station address (words 0-2)
    void StationMac(uint8_t out[6]) const override;
    bool Offer(const uint8_t* frame, size_t length) override;

    /// The ID sequence state machine (TR 7-2)
    enum class IdState : uint8_t
    {
        AutoInit = 0,   ///< reading the EEPROM after a reset (310 us): deaf
        IdWait = 1,
        IdCmd = 2,
    };
    IdState GetIdState() const { return static_cast<IdState>(_s.idState); }
    bool Activated() const { return _s.activated != 0; }
    uint16_t IoBase() const;
    uint8_t Window() const { return _s.window; }
    /// The status register as a read would return it
    uint16_t Status() const;
    /// The ID sequence byte number `index` (0 = #FF, 254 = #98)
    static uint8_t IdSequenceByte(int index);

    /// Observation (not state)
    struct Counters
    {
        uint64_t txFrames = 0;          ///< frames that left on the wire
        uint64_t rxFrames = 0;          ///< frames taken into the RX FIFO
        uint64_t rxFiltered = 0;        ///< frames the filter, a disabled receiver or no link turned away
        uint64_t rxFifoFullWaits = 0;   ///< offers refused for want of RX FIFO room (the gateway keeps them)
        uint64_t txNoLink = 0;          ///< frames sent with no cable or no link pass
        uint64_t txUnderruns = 0, txOverruns = 0, rxUnderruns = 0;
        uint64_t idSequences = 0;       ///< complete ID sequences (ID_WAIT -> ID_CMD)
        uint64_t idEepromReads = 0;     ///< EEPROM words read through the ID port
        uint64_t activations = 0;
        uint64_t globalResets = 0;      ///< power-on resets (RESET DRV, ID #C0-#CF, Global Reset, CC RST)
        uint64_t commands = 0;
    };
    const Counters& GetCounters() const { return _counters; }

    /// What the card did, in words (the last kEventCount; observation, regenerated by a TTD replay)
    struct Event
    {
        uint64_t time = 0;   ///< base T-states
        std::string text;
    };
    static constexpr size_t kEventCount = 64;
    const std::deque<Event>& Events() const { return _events; }

private:
    struct TxPacket
    {
        uint16_t length = 0;          ///< preamble bits 10-0
        bool interruptOnSuccess = false;
        bool noCrc = false;           ///< preamble bit 13 (the data carries its CRC)
        bool complete = false;        ///< every byte up to the dword pad is in
        bool discard = false;         ///< underrun / reset: the rest of its bytes go nowhere
        std::vector<uint8_t> data;    ///< bytes written after the preamble (padding included)
        uint16_t Expected() const { return static_cast<uint16_t>((length + 3u) & ~3u); }
    };
    struct RxPacket
    {
        std::vector<uint8_t> data;
        uint16_t readPos = 0;
        uint8_t error = 0;            ///< 0 none, else 0x80 | RX status bits 13-11
    };

    /// Everything a TTD checkpoint restores besides the FIFOs and the EEPROM (serialized field by field)
    struct State
    {
        // ID sequence state machine
        uint8_t idState = 0;
        uint8_t tag = 0;
        uint8_t seqIndex = 0;
        uint8_t activated = 0;
        uint16_t idPort = 0;          ///< 0: no zero written yet
        uint64_t autoInitUntil = 0;
        // EEPROM access
        uint16_t eepromData = 0;
        uint8_t eepromCommand = 0;    ///< the last command (EEPROM command register bits 7-0)
        uint8_t eepromWriteEnable = 0;
        uint8_t eepromPending = 0;    ///< 0 none, 1 read, 2 write, 3 erase, 4 erase all, 5 write all, 6 enable/disable
        uint8_t eepromAddress = 0;
        uint64_t eepromBusyUntil = 0;
        // window 0 / 3 configuration
        uint16_t productId = 0;
        uint16_t addressConfig = 0;
        uint16_t resourceConfig = 0;
        uint8_t enable = 0;           ///< configuration control ENA
        uint32_t internalConfig = 0;
        uint8_t romControl = 0;
        // bus interface
        uint8_t window = 0;
        uint8_t writeLow = 0;         ///< the low byte of a 16-bit write, until its high byte
        uint8_t writeLowValid = 0;
        uint8_t readHigh = 0;         ///< the high byte latched by a low-byte read
        uint8_t readHighOffset = 0xFF;
        // status and interrupts
        uint16_t intMask = 0, readZeroMask = 0;
        uint8_t latch = 0, interruptRequested = 0, txAvailable = 0, updateStats = 0;
        uint64_t timerBase = 0;
        uint64_t cipUntil = 0;
        // receive / transmit control
        uint16_t rxFilter = 0, rxEarly = 0, txAvailThreshold = 0, txStartThreshold = 0;
        uint8_t rxEnabled = 0, txEnabled = 0, statsEnabled = 0, poweredDown = 0, coax = 0;
        uint8_t txOverrun = 0, rxUnderrun = 0, txResetNeeded = 0;
        uint8_t station[6] = {};
        uint16_t media = 0;           ///< the writable media bits (7, 6, 3, 2)
        uint16_t netDiag = 0;         ///< the writable net diagnostic bits (15-12)
        uint16_t ecStatus = 0;        ///< Ethernet controller status bit 0 (RX TESTEN)
        uint64_t linkBeatSince = 0;   ///< UINT64_MAX: link beat disabled
        uint64_t cableSince = 0;
        // the transmitter
        uint8_t txInFlight = 0;       ///< the head packet is on the wire
        uint64_t txStartedAt = 0, txDoneAt = 0, wireFreeAt = 0;
        uint8_t txStatus[31] = {};
        uint8_t txStatusCount = 0;
        uint8_t txStatusOverflow = 0;
        uint8_t preamble[4] = {};
        uint8_t preambleBytes = 0;    ///< preamble bytes of the next packet collected so far
        // statistics (window 6): 0-8 byte counters, 9 RX bytes OK, 10 TX bytes OK
        uint16_t stats[11] = {};
        uint16_t statsPending[11] = {};   ///< one update request latched per counter while statistics are disabled
        uint16_t pendingTxReset = 0xFFFF; ///< a TX Reset waiting for the packet on the wire (0xFFFF: none)
    };
    static constexpr uint8_t kStateVersion = 1;

    // Time
    uint64_t Now() const { return _clock ? _clock() : 0; }
    void Advance(uint64_t now);
    bool Deaf(uint64_t now) const { return now < _s.autoInitUntil; }

    // Resets
    void PowerOnReset(uint64_t now);
    void GlobalReset(uint16_t mask, uint64_t now);
    void RxReset(uint16_t mask);
    void TxReset(uint16_t mask, uint64_t now);
    void LoadFromEeprom();

    // ID port
    void IdWrite(uint16_t port, uint8_t value, uint64_t now);
    uint8_t IdRead(uint16_t port, uint64_t now, bool peek);
    void StartEepromCommand(uint8_t command, uint64_t now);
    void FinishEeprom(uint64_t now);

    // Registers
    uint16_t ReadWord(uint8_t offset, uint64_t now, bool peek);
    uint8_t ReadByteRegister(uint8_t offset, uint64_t now, bool peek, bool& isByte);
    void WriteWord(uint8_t offset, uint16_t value, uint64_t now);
    bool WriteByteRegister(uint8_t offset, uint8_t value, uint64_t now);
    void Command(uint16_t word, uint64_t now);
    bool IsByteRegister(uint8_t offset) const;

    // FIFOs
    uint16_t TxCapacity() const;
    uint16_t RxCapacity() const;
    uint16_t TxFree() const;
    uint16_t RxFree() const;
    uint16_t RxStatus() const;
    uint8_t RxPop(bool peek);
    void TxPush(uint8_t value, uint64_t now);
    void TryStartTransmit(uint64_t now);
    void FinishTransmit(uint64_t now);
    void PushTxStatus(uint8_t status);
    void Deliver(const std::vector<uint8_t>& frame);
    bool Accepts(const uint8_t* frame, size_t length) const;
    bool LinkPass(uint64_t now) const;
    uint64_t LinkUpAt() const;

    // Interrupts, statistics
    uint8_t RawStatusBits() const;
    void UpdateIrq(uint64_t now);
    void CountStat(int index, uint16_t amount);
    void UpdateStatsFlag();
    uint16_t ReadStat(int index, bool peek);
    /// Window 4's diagnostic registers (no side effects: reports read them too)
    uint16_t FifoDiagnostic() const;
    uint16_t NetDiagnostic(uint64_t now) const;
    uint16_t MediaStatus(uint64_t now) const;
    void Note(uint64_t now, std::string text);

    Settings _settings;
    std::function<uint64_t()> _clock;
    std::array<uint16_t, kEepromWords> _eeprom{};
    State _s;
    std::deque<TxPacket> _tx;
    std::deque<RxPacket> _rx;
    IEthernetLink* _link = nullptr;
    std::function<void()> _irqListener;
    Counters _counters;
    std::deque<Event> _events;
};

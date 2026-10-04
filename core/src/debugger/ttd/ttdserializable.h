#pragma once

/// @file ttdserializable.h
/// @brief Minimal in-RAM state serialization interface for TTD peripherals.
///
/// Per parent TDD §6.4: implemented by AY/TurboSound, WD1793+FDD, Tape, Covox,
/// TSFM, GeneralSound, and any future sound/expansion peripherals.
///
/// The CPU and chipset do NOT implement this interface — they are stored by
/// value in TTDCheckpoint (see ttdcheckpoint.h) because their state layout
/// is fixed and known at compile time.
///
/// Design constraints (TDD §6.4):
///   - TTDSaveState writes into a caller-provided buffer of TTDStateSize()
///     bytes and must not allocate itself (it runs every frame on the emulator
///     thread). The registry around it does allocate per checkpoint: the blob
///     vectors and the compression of each payload.
///   - Per-payload versioning is not used. Session-level schema versioning
///     is handled by SerializeSession/DeserializeSession (ttddumpformat.h).
///   - The blob format is the implementer's choice (typically a memcpy of
///     the device's POD state) — there is no shared envelope or framing.
///
/// Blob encoding:
///   - TTDPeripheralRegistry wraps each device's state in a header and
///     compresses the payload when that makes it smaller.
///   - Every blob is self-describing: its header carries the device id, so a
///     blob filed under the wrong device is rejected rather than loaded.
///
/// The same implementations serve both TTD checkpoints and the in-RAM
/// snapshot serializer (TDD §6.1 "Serializer reuse") so the file-snapshot
/// feature and TTD can never disagree about peripheral state.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace ttd {

/// @brief Peripheral identifier for registry and checkpoint indexing.
/// New peripherals append entries here - never renumber existing values:
/// the id is the checkpoint blob-map key and is persisted in v2 session
/// dump blob headers (EncodeBlob/DecodeBlob), so a session recorded with
/// one numbering must load under the same numbering.
enum class PeripheralId : uint8_t
{
    TurboSound = 0,
    BetaDisk   = 1,
    Tape       = 2,
    Covox      = 3,
    TSFM       = 4,
    GeneralSound = 5,
    ScorpionProfROM = 6,  // Scorpion ZS 256 paging (#1FFD, DOS trigger) + ProfROM state machine
    KempstonMouse = 7,    // Kempston Mouse counters/buttons/wheel (core device, every model)
    AtmPaging = 8,        // ATM Turbo 2+ / ATM3 / ZX-Evo BaseConf memory map
    ProfiPaging = 9,      // Profi 1024: #DFFD latch, hi-res palette
    MoonSound  = 10,      // ZXM-MoonSound (YMF278B / OPL4): Tier A chip + host latches
    GeneralSoundLightweight = 11, // GS lightweight personality (in-tree mod player, no coprocessor)
    NeoGS = 12,           // NeoGS card (neogs-tdd.md §7.4): registers and devices; RAM and flash wait for TTD v2 regions
    Plus3Paging = 13,     // +2A/+3 #1FFD latch (ROM high bit, all-RAM modes, motor)
    Upd765 = 14,          // +3 uPD765A floppy controller (drives ride the BetaDisk blob)
    EvoSdCard = 15,       // ZX-Evo Z-Controller + SD card protocol state (not the card's sectors: storage-manager TTD rule)
    TsConfPaging = 16,    // TSConf (PLAN #41): its whole machine state - registers, paging, CRAM, SFILE, INT, DMA, TSU (technical-design §3.13)
    AtaChannel = 17,      // IDE board: channel, both units (task file, transfer, ATAPI sense), adapter latches; not the media
    Ds12887 = 18,         // MC146818 / DS12887 clock: cells, address latch, time base (ATM3, Profi, Scorpion SMUC; Sprinter, TSConf)
    EvoPs2 = 19,          // ZX-Evo AVR PS/2 keyboard: scan code log, parser flags, modifiers, held keys (ATM3)
    ZxNetUsb = 20,        // ZXNETUSB card + W5300 + the virtual network's guest-side tables; received bytes by journal reference (network TDD §6.3)
    EvoTurboCache = 21,   // ZX-Evo BaseConf at 14 MHz: the DRAM's code and data cache words (EvoTurboOverlay)
    EvoFontRam = 22,      // ZX-Evo BaseConf text-mode font RAM (2 KB, #BF bit 2 loads it) and the glyph byte #0EBD reads
    KempstonJoystick = 23, // Kempston joystick state byte (core device; carried by machines whose decoder answers #1F)
    SerialPort = 24,      // the 16550 on #xxEF (ZX-Evo AVR firmware or a ZX-WiFi card) and its peer (network TDD §7)
    SprinterPld = 25,     // Sprinter Sp2000 PLD state, decoder latches, configuration module, INT source, accelerator slot (tdd-integration §2.1)
    Atm2Kbc = 26,         // ATM Turbo 2+ keyboard controller: the MCS-51 (RAM, SFRs, timers, UART), board latches, PS/2 keyboard
    MachineSerialPeer = 27, // the peer on a machine serial port that is no 16550 on #xxEF (ATM Turbo 2+ controller RS-232)
    SprinterVideoRam = 28,  // Sprinter video RAM, 256 KB whole (a TTD v2 memory region once those exist)
    Z84C15 = 29,          // Zilog Z84C15 on-chip block: system registers, wait generator, watchdog, CTC, SIO (FIFOs), PIO, daisy chain
    SprinterFastRam = 30, // Sprinter fast RAM (the four 16 KB cache pages), 64 KB whole (a TTD v2 memory region once those exist)
    SprinterInput = 31,   // Sprinter AT keyboard byte stream (SIO A) and Microsoft serial mouse packet generator (SIO B)
    SprinterCovoxBlaster = 32, // Sprinter Covox / Covox-Blaster: ring, indices, rate phase, INT request, DAC words (S6)
    SprinterIsa = 33,     // Sprinter ISA slots: the #9FBD latch, the card kind in each slot, the cards' bus state (ISA phase I1)
    // Reserved for a Sprinter device that does not exist yet (Sprinter s7-ttd-outcome.md "Reserved"): no serializer,
    // never declared. The device that lands takes its id, declares it and adds its blob - the existing Sprinter
    // blobs keep their layout, so a checkpoint only gains a blob
    SprinterPads = 34,    // reserved (input extras): the two extended joystick pads and their select counters
    Wd1793Context = 35,   // WD1793 command in flight beyond the BetaDisk blob: queued steps, transfer pointers (ttdwd1793context.h)
    AtmIoBus = 36,        // ATM Turbo 2+ INTERNAL I/O connector: the #FB bus address latch
    Atm2IoEsp = 37,       // the ATM2IOESP card on it: the 16550 and its peer (netstate::SerialPort)
    EvoMouse = 38,        // ZX-Evo AVR PS/2 mouse: the Kempston-address registers, plugged in (ATM3, TSConf)
    ZiFiLine = 39,        // the TS AVR's ZiFi UART (USART0) and its peer (netstate::SerialPort)
    ZiFi = 40,            // the TS AVR's ZiFi API block: registers, selector, last-byte times (ZiFi::State)
    CdDrive = 41,         // the IDE board's ATAPI CD drives beyond their task file (AtaChannel): CD audio play state, head,
                          // page 0Eh volume / routing, the READ CD sector waiting for the data buffer; boards with a CD unit only
    Vdac2Memory = 42,     // TS-Conf VDAC2 card: the FT812's memory regions (RAM_G, display lists, REG, CMD, ...), zero runs dropped, until TTD v2 regions
    Vdac2 = 43,           // TS-Conf VDAC2 card: card time, INT edges, monitor source, FT812 control state (EveSaveState, metrics)
    ProfiXtKbc = 44,      // Profi PROFI-XT keyboard controller: the MCS-48 (RAM, registers, ports, timer), output latch, WAIT
                          // flip-flop, the XT keyboard's wire, time base; the table engine's matrix (ProfiXtKbc::State)
    EthernetNics = 45,    // frame-level network cards in expansion slots (the Sprinter's NE2000): DP8390, packet RAM, EEPROM (network tdd §13)
    SlotSerial1 = 46,     // the UART card in expansion slot 1 (the Sprinter's SprinterESP): its 16550 and peer (netstate::SerialPort)
    SlotSerial2 = 47,     // the same for slot 2
    SlotSerial1B = 48,    // the second UART of the card in expansion slot 1 (SprinterSerial's COM2; network phase SN4)
    SlotSerial2B = 49,    // the same for slot 2
    Ppi8255 = 50,         // an 8255 PPI (the ZX Profi's: joystick, printer / Covox): mode word and output latches (Ppi8255::State)
    // Future: SAA1099, GS512, etc.
    Count
};

class TTDSerializable
{
public:
    virtual ~TTDSerializable() = default;

    /// Fixed-size payload length for this device. Stable for the lifetime of
    /// the device instance (i.e. does not change as the device is configured).
    /// Callers use this to size the destination buffer before calling
    /// TTDSaveState.
    /// @return Number of bytes needed for TTDSaveState's dst buffer.
    virtual size_t TTDStateSize() const = 0;

    /// Snapshot the device's complete runtime state into dst.
    ///
    /// Must be a plain write of exactly TTDStateSize() bytes — no allocation,
    /// no side effects on the device. Runs on the emulator thread at every
    /// captured frame boundary.
    ///
    /// @param dst Destination buffer, at least TTDStateSize() bytes. Caller
    ///            owns the buffer; implementer must not retain the pointer.
    virtual void TTDSaveState(uint8_t* dst) const = 0;

    /// Restore the device's runtime state from src.
    ///
    /// Must fully restore the device to the state captured by TTDSaveState.
    /// Runs on the control thread during SeekTo, with the emulator paused
    /// (TDD §7.2). The implementer must not assume src points at any
    /// particular alignment beyond uint8_t.
    ///
    /// @param src Source buffer of exactly TTDStateSize() bytes. Caller owns
    ///            the buffer; implementer must not retain the pointer.
    virtual void TTDLoadState(const uint8_t* src) = 0;

    /// Human-readable device name for logging/debugging.
    virtual std::string TTDDeviceName() const { return "unknown"; }

    /// Peripheral identifier for checkpoint indexing.
    virtual PeripheralId TTDPeripheralId() const { return PeripheralId::Count; }

    /// Compute a hash contribution for divergence detection.
    /// The framework mixes this with the common chipset hash so model-specific
    /// state participates in divergence detection without the framework
    /// knowing about machine specifics.
    /// Default returns 0 (no contribution). Override for devices with state
    /// that affects determinism (e.g., Scorpion ProfROM quadrant).
    virtual uint64_t TTDHashState() const { return 0; }

    /// Recording starts / returns to idle (TimeTravelManager's recording lock).
    /// Called before the baseline checkpoint is captured, so a device that
    /// switches to a deterministic time base here has the switch in its first
    /// blob. Default: nothing (most devices have no host-time dependence)
    virtual void TTDRecordingStarted() {}
    virtual void TTDRecordingStopped() {}

    /// Variable-size state (default: no). A device whose state is large but
    /// usually compresses away by its own encoding (the VDAC2 card's FT812
    /// memory, zero runs dropped) saves only what it needs: TTDSaveStateTo
    /// fills `out` with at most TTDStateSize() bytes, and a restore hands
    /// TTDLoadState a blob of any size up to that (the blob is self-delimiting).
    /// Then the capture does not allocate, clear and compress the worst case
    /// every frame
    virtual bool TTDVariableSize() const { return false; }
    virtual void TTDSaveStateTo(std::vector<uint8_t>& out) const
    {
        out.resize(TTDStateSize());
        TTDSaveState(out.data());
    }
};

} // namespace ttd

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
    // Reserved for Sprinter devices that do not exist yet (Sprinter s7-ttd-outcome.md "Reserved"): no serializer,
    // never declared. The device that lands takes its id, declares it and adds its blob - the existing Sprinter
    // blobs keep their layout, so a checkpoint only gains a blob
    SprinterIsa = 33,     // reserved (S6b): ISA I/O window latches, ZX-bus adapter
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
    Smuc = 44,            // Scorpion SMUC board: #FFBA / #7FBA latches, IDE window registers, serial EEPROM link (not its contents)
    EvoAvrVolatile = 45,  // ZX-Evo AVR volatile registers (ext type, EEPROM window, LEDs) where the paging blob lacks them: TS-Conf
    KeyboardMatrix = 46,  // ZX keyboard: the 8 matrix rows and the pressed-key counts (key changes are journal events)
    // Future: SAA1099, GS512, etc.
    Count
};


/// region <Device descriptor (TTD v2 engine, Phase 2)>

/// Stable device kind, stored in the engine's files. The values are
/// PeripheralId's, unchanged, so a v1 blob id maps to a type by value. Until
/// the engine replaces v1 (Phase 5) a new device takes the next PeripheralId
/// and the same number here; after that only a type. Slot personalities stay
/// separate types (TSFM is not a TurboSound): a state never crosses kinds.
/// Several instances of one kind (the UARTs) share a type and differ by
/// instance name (TTDDeviceDescriptor::instance).
enum class TTDDeviceType : uint16_t
{
    TurboSound = 0,
    BetaDisk = 1,
    Tape = 2,
    Covox = 3,
    TSFM = 4,
    GeneralSound = 5,
    ScorpionProfROM = 6,
    KempstonMouse = 7,
    AtmPaging = 8,
    ProfiPaging = 9,
    MoonSound = 10,
    GeneralSoundLightweight = 11,
    NeoGS = 12,
    Plus3Paging = 13,
    Upd765 = 14,
    EvoSdCard = 15,
    TsConfPaging = 16,
    AtaChannel = 17,
    Ds12887 = 18,
    EvoPs2 = 19,
    ZxNetUsb = 20,
    EvoTurboCache = 21,
    EvoFontRam = 22,
    KempstonJoystick = 23,
    SerialPort = 24,
    SprinterPld = 25,
    Atm2Kbc = 26,
    MachineSerialPeer = 27,
    SprinterVideoRam = 28,
    Z84C15 = 29,
    SprinterFastRam = 30,
    SprinterInput = 31,
    SprinterCovoxBlaster = 32,
    SprinterIsa = 33,
    SprinterPads = 34,
    Wd1793Context = 35,
    AtmIoBus = 36,
    Atm2IoEsp = 37,
    EvoMouse = 38,
    ZiFiLine = 39,
    ZiFi = 40,
    CdDrive = 41,
    Vdac2Memory = 42,
    Vdac2 = 43,
    Smuc = 44,
    EvoAvrVolatile = 45,
    KeyboardMatrix = 46
};
static_assert(static_cast<uint16_t>(TTDDeviceType::TurboSound) == static_cast<uint16_t>(PeripheralId::TurboSound), "TTDDeviceType::TurboSound must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::BetaDisk) == static_cast<uint16_t>(PeripheralId::BetaDisk), "TTDDeviceType::BetaDisk must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Tape) == static_cast<uint16_t>(PeripheralId::Tape), "TTDDeviceType::Tape must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Covox) == static_cast<uint16_t>(PeripheralId::Covox), "TTDDeviceType::Covox must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::TSFM) == static_cast<uint16_t>(PeripheralId::TSFM), "TTDDeviceType::TSFM must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::GeneralSound) == static_cast<uint16_t>(PeripheralId::GeneralSound), "TTDDeviceType::GeneralSound must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::ScorpionProfROM) == static_cast<uint16_t>(PeripheralId::ScorpionProfROM), "TTDDeviceType::ScorpionProfROM must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::KempstonMouse) == static_cast<uint16_t>(PeripheralId::KempstonMouse), "TTDDeviceType::KempstonMouse must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::AtmPaging) == static_cast<uint16_t>(PeripheralId::AtmPaging), "TTDDeviceType::AtmPaging must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::ProfiPaging) == static_cast<uint16_t>(PeripheralId::ProfiPaging), "TTDDeviceType::ProfiPaging must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::MoonSound) == static_cast<uint16_t>(PeripheralId::MoonSound), "TTDDeviceType::MoonSound must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::GeneralSoundLightweight) == static_cast<uint16_t>(PeripheralId::GeneralSoundLightweight), "TTDDeviceType::GeneralSoundLightweight must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::NeoGS) == static_cast<uint16_t>(PeripheralId::NeoGS), "TTDDeviceType::NeoGS must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Plus3Paging) == static_cast<uint16_t>(PeripheralId::Plus3Paging), "TTDDeviceType::Plus3Paging must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Upd765) == static_cast<uint16_t>(PeripheralId::Upd765), "TTDDeviceType::Upd765 must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::EvoSdCard) == static_cast<uint16_t>(PeripheralId::EvoSdCard), "TTDDeviceType::EvoSdCard must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::TsConfPaging) == static_cast<uint16_t>(PeripheralId::TsConfPaging), "TTDDeviceType::TsConfPaging must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::AtaChannel) == static_cast<uint16_t>(PeripheralId::AtaChannel), "TTDDeviceType::AtaChannel must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Ds12887) == static_cast<uint16_t>(PeripheralId::Ds12887), "TTDDeviceType::Ds12887 must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::EvoPs2) == static_cast<uint16_t>(PeripheralId::EvoPs2), "TTDDeviceType::EvoPs2 must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::ZxNetUsb) == static_cast<uint16_t>(PeripheralId::ZxNetUsb), "TTDDeviceType::ZxNetUsb must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::EvoTurboCache) == static_cast<uint16_t>(PeripheralId::EvoTurboCache), "TTDDeviceType::EvoTurboCache must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::EvoFontRam) == static_cast<uint16_t>(PeripheralId::EvoFontRam), "TTDDeviceType::EvoFontRam must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::KempstonJoystick) == static_cast<uint16_t>(PeripheralId::KempstonJoystick), "TTDDeviceType::KempstonJoystick must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SerialPort) == static_cast<uint16_t>(PeripheralId::SerialPort), "TTDDeviceType::SerialPort must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterPld) == static_cast<uint16_t>(PeripheralId::SprinterPld), "TTDDeviceType::SprinterPld must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Atm2Kbc) == static_cast<uint16_t>(PeripheralId::Atm2Kbc), "TTDDeviceType::Atm2Kbc must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::MachineSerialPeer) == static_cast<uint16_t>(PeripheralId::MachineSerialPeer), "TTDDeviceType::MachineSerialPeer must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterVideoRam) == static_cast<uint16_t>(PeripheralId::SprinterVideoRam), "TTDDeviceType::SprinterVideoRam must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Z84C15) == static_cast<uint16_t>(PeripheralId::Z84C15), "TTDDeviceType::Z84C15 must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterFastRam) == static_cast<uint16_t>(PeripheralId::SprinterFastRam), "TTDDeviceType::SprinterFastRam must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterInput) == static_cast<uint16_t>(PeripheralId::SprinterInput), "TTDDeviceType::SprinterInput must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterCovoxBlaster) == static_cast<uint16_t>(PeripheralId::SprinterCovoxBlaster), "TTDDeviceType::SprinterCovoxBlaster must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterIsa) == static_cast<uint16_t>(PeripheralId::SprinterIsa), "TTDDeviceType::SprinterIsa must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::SprinterPads) == static_cast<uint16_t>(PeripheralId::SprinterPads), "TTDDeviceType::SprinterPads must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Wd1793Context) == static_cast<uint16_t>(PeripheralId::Wd1793Context), "TTDDeviceType::Wd1793Context must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::AtmIoBus) == static_cast<uint16_t>(PeripheralId::AtmIoBus), "TTDDeviceType::AtmIoBus must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Atm2IoEsp) == static_cast<uint16_t>(PeripheralId::Atm2IoEsp), "TTDDeviceType::Atm2IoEsp must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::EvoMouse) == static_cast<uint16_t>(PeripheralId::EvoMouse), "TTDDeviceType::EvoMouse must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::ZiFiLine) == static_cast<uint16_t>(PeripheralId::ZiFiLine), "TTDDeviceType::ZiFiLine must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::ZiFi) == static_cast<uint16_t>(PeripheralId::ZiFi), "TTDDeviceType::ZiFi must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::CdDrive) == static_cast<uint16_t>(PeripheralId::CdDrive), "TTDDeviceType::CdDrive must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Vdac2Memory) == static_cast<uint16_t>(PeripheralId::Vdac2Memory), "TTDDeviceType::Vdac2Memory must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Vdac2) == static_cast<uint16_t>(PeripheralId::Vdac2), "TTDDeviceType::Vdac2 must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::Smuc) == static_cast<uint16_t>(PeripheralId::Smuc), "TTDDeviceType::Smuc must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::EvoAvrVolatile) == static_cast<uint16_t>(PeripheralId::EvoAvrVolatile), "TTDDeviceType::EvoAvrVolatile must keep its v1 number");
static_assert(static_cast<uint16_t>(TTDDeviceType::KeyboardMatrix) == static_cast<uint16_t>(PeripheralId::KeyboardMatrix), "TTDDeviceType::KeyboardMatrix must keep its v1 number");

/// A device in the engine's device table: its kind and its instance name
/// ("betadisk", "betadisk.context", "zifi.uart"; lower case, dots, digits)
struct TTDDeviceKey
{
    TTDDeviceType type = TTDDeviceType::TurboSound;
    std::string instance;

    bool operator==(const TTDDeviceKey& o) const { return type == o.type && instance == o.instance; }
    bool operator<(const TTDDeviceKey& o) const
    {
        return type != o.type ? static_cast<uint16_t>(type) < static_cast<uint16_t>(o.type) : instance < o.instance;
    }
};

/// A counter in the state that advances with time: predicted from its last
/// step, stored as the residual (Phase 2, §5.2.3). For state in vendored
/// libraries; a device whose code is ours keeps an anchor instead (E8)
struct TTDTimeField
{
    uint16_t offset = 0;   ///< byte offset in the state
    uint8_t width = 0;     ///< 2, 4 or 8 bytes, little endian
};

/// What the engine needs to know about a device, given once at registration
struct TTDDeviceDescriptor
{
    TTDDeviceType type = TTDDeviceType::TurboSound;
    std::string instance;                     ///< default: TTDDeviceName() in lower case
    PeripheralId legacyId = PeripheralId::Count;   ///< the v1 blob id (restore order ties follow v1)
    uint16_t layoutVersion = 1;               ///< bumped when the device's bytes change meaning
    uint32_t stateSize = 0;                   ///< TTDStateSize(): exact, or the largest when variableSize
    bool variableSize = false;
    uint64_t firmwareFingerprint = 0;         ///< firmware the session does not record (0 = none)
    std::vector<TTDDeviceKey> restoreAfter;   ///< devices to load first
    std::vector<TTDTimeField> timeFields;
    bool runsBehindCpu = false;               ///< must be synced to the frame boundary before a capture

    TTDDeviceKey Key() const { return {type, instance}; }
};

/// Passed to TTDSerializable::TTDAfterRestore
struct TTDRestoreContext
{
    uint64_t frame = 0;          ///< the restored position
    uint32_t tInFrame = 0;
    bool replayFollows = false;  ///< a replay runs from here to a later target
};

/// endregion </Device descriptor>

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

    /// The device's descriptor for the engine's device table (Phase 2, Step 1).
    /// The default fits a single-instance device whose bytes never changed
    /// meaning: its type is its PeripheralId, its instance its name in lower case
    virtual TTDDeviceDescriptor TTDDescribe() const
    {
        TTDDeviceDescriptor d;
        d.legacyId = TTDPeripheralId();
        d.type = static_cast<TTDDeviceType>(d.legacyId);
        d.instance = TTDDeviceName();
        for (char& c : d.instance)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        d.stateSize = static_cast<uint32_t>(TTDStateSize());
        d.variableSize = TTDVariableSize();
        return d;
    }

    /// Called once per device after a whole restore (CPU, chipset, every
    /// device, banking, every memory region), in restore order: for state a
    /// device derives from other parts of the machine. Work that already sits
    /// in TTDLoadState stays there
    virtual void TTDAfterRestore(const TTDRestoreContext& context) { (void)context; }

    /// Put the device in its power-on state, for a restore whose checkpoint
    /// has no state for it (the same restore then always gives the same
    /// machine). False: the device cannot, it keeps its live state
    virtual bool TTDResetToPowerOn() { return false; }
};

} // namespace ttd

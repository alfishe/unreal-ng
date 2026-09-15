#pragma once
#include "stdafx.h"

#include <array>
#include <memory>
#include <set>
#include "emulator/platform.h"
#include "emulator/ports/portdiagrecorder.h"
#include "debugger/ttd/ttdserializable.h"  // ttd::PeripheralId / TTDSerializable (leaf header)


class ModuleLogger;
class EmulatorContext;
class Memory;
class Screen;
class Beeper;
class Tape;
class SoundManager;
class Keyboard;
class Mouse;
class PortDevice;

/// region <Constants>

constexpr uint16_t PORT_FFFD = 0xFFFD;
constexpr uint16_t PORT_BFFD = 0xBFFD;

// Port 0x7FFD bits
constexpr uint8_t PORT_7FFD_RAM_BANK_BITMASK    = 0b0000'0111;
constexpr uint8_t PORT_7FFD_SCREEN              = (1u << 3);
constexpr uint8_t PORT_7FFD_ROM_BANK            = (1u << 4);
constexpr uint8_t PORT_7FFD_LOCK                = (1u << 5);

constexpr uint8_t PORT_7FFD_SCREEN_NORMAL       = 0;
constexpr uint8_t PORT_7FFD_SCREEN_SHADOW       = (1u << 3);

constexpr uint8_t PORT_7FFD_ROM_BANK_0          = 0;
constexpr uint8_t PORT_7FFD_ROM_BANK_1          = (1u << 4);

constexpr uint8_t PORT_7FFD_RAM_BANK_0          = 0b0000'0000;
constexpr uint8_t PORT_7FFD_RAM_BANK_1          = 0b0000'0001;
constexpr uint8_t PORT_7FFD_RAM_BANK_2          = 0b0000'0010;
constexpr uint8_t PORT_7FFD_RAM_BANK_3          = 0b0000'0011;
constexpr uint8_t PORT_7FFD_RAM_BANK_4          = 0b0000'0100;
constexpr uint8_t PORT_7FFD_RAM_BANK_5          = 0b0000'0101;
constexpr uint8_t PORT_7FFD_RAM_BANK_6          = 0b0000'0110;
constexpr uint8_t PORT_7FFD_RAM_BANK_7          = 0b0000'0111;

/// endregion <Constants>

/// region <Types>

struct PortMatch
{
    uint16_t mask;
    uint16_t match;
    uint16_t resolvedPort;
};

/// One overlap between a model decode rule and a full-decode low-byte claim:
/// some raw Z80 address satisfies BOTH the motherboard partial decode and the
/// card's A0..A7 decode, so without arbitration the cycle would double-deliver
/// (ZXM-MoonSound `out (#C4),a` paging RAM through #7FFD and writing AY #BFFD
/// on a loose-decode machine - the exact corruption seen in MFM Music sample 2)
struct PortDecodeClash
{
    uint8_t ruleIndex = 0;         // Index into the model decode table
    uint16_t ruleMask = 0;         // Rule mask
    uint16_t ruleMatch = 0;        // Rule match value
    uint16_t rulePort = 0;         // Rule canonical decoded port
    uint8_t claimLowByte = 0;      // Claimed low address byte (A0..A7)
    PortDevice* device = nullptr;  // Claiming full-decode observer
    uint16_t sampleAddress = 0;    // One concrete raw address hitting both decode and claim
    bool fdcProtected = false;     // Rule resolves into the Beta-128 set - the FDC session gate keeps precedence (R6)
};

/// Per-rule resolution summary: which claims hit the rule and the minimal
/// mask tightening that would separate ALL of them from the rule while still
/// matching the rule's canonical port (the +2A/+3-style decode refinement real
/// host machines adopted). separable=false means no up-to-3-bit tightening
/// exists - precedence override is the only resolution.
struct PortDecodeRuleOverride
{
    uint8_t ruleIndex = 0;
    uint16_t rulePort = 0;
    std::vector<uint8_t> clashingClaims;
    uint16_t separatingMask = 0;      // Extra mask bits beyond the rule's own mask
    uint16_t separatingMatch = 0;     // Canonical-port bit values for those bits
    uint8_t separatingBitCount = 0;   // Number of extra bits (0 = inseparable / not needed)
    bool precedenceRequired = false;  // No minimal tightening exists (or FDC-protected)
};

/// Base class to mark all devices connected to port decoder
class PortDevice
{
public:
    virtual uint8_t portDeviceInMethod(uint16_t port) = 0;
    virtual void portDeviceOutMethod(uint16_t port, uint8_t) = 0;

    /// Full-decode observer read claim. Returning true makes this device's
    /// read value win over a model-decoded legacy device on the same raw
    /// port in the same cycle - the armed-card behaviour on the shared bus
    /// (ZXM-MoonSound drives #7F over the Beta-128 FDC mirror once the guest
    /// sets OPL4 NEW; verified against the card author's MoonService v0.3a,
    /// which reads the device ID and the wave RAM window at #7F right after
    /// arming FM2 reg 05). The default keeps the legacy-priority rule (R6).
    virtual bool portDeviceClaimsRead(uint16_t) { return false; }
};

typedef uint8_t (PortDevice::* PortDeviceInMethod)(uint16_t port);              // Class method callback
typedef void (PortDevice::* PortDeviceOutMethod)(uint16_t port, uint8_t value); // Class method callback

/// endregion </Types>

/// ============================================================================
/// PORT DECODER ARCHITECTURE
/// ============================================================================
///
/// OVERVIEW:
/// The PortDecoder class hierarchy provides model-specific port decoding for
/// different ZX Spectrum variants. Each model has different port address decoding
/// and may have different peripherals attached.
///
/// IMPORTANT: HARDWARE I/O MUST HAPPEN EXACTLY ONCE
/// -------------------------------------------------
/// Many hardware devices (FDC, AY chip, etc.) have stateful registers where
/// reading clears flags or advances internal state. Double-reading causes data loss.
///
/// CORRECT PATTERN (for subclasses):
/// ```cpp
/// uint8_t PortDecoder_ModelX::DecodePortIn(uint16_t port, uint16_t pc)
/// {
///     uint8_t result = 0xFF;
///     uint16_t decodedPort = decodePort(port);  // Model-specific decoding
///
///     // 1. Perform hardware I/O (ONCE)
///     result = PeripheralPortIn(decodedPort);
///
///     // 2. Call common handler for breakpoints, tracking, analyzers
///     OnPortInComplete(decodedPort, result, pc);
///
///     return result;
/// }
/// ```
///
/// ANTI-PATTERN (DO NOT DO THIS):
/// ```cpp
/// uint8_t PortDecoder_ModelX::DecodePortIn(uint16_t port, uint16_t pc)
/// {
///     PortDecoder::DecodePortIn(port, pc);  // <-- WRONG! May cause double read
///     result = PeripheralPortIn(decodedPort);
///     return result;
/// }
/// ```
///
/// See: docs/emulator/design/ports/port-decoder-architecture.md
/// ============================================================================

/// Base class for all model port decoders
class PortDecoder
{

    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Static methods>
public:
    static PortDecoder* GetPortDecoderForModel(MEM_MODEL model, EmulatorContext* context);
    /// endregion </Static methods>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;

    EmulatorState* _state = nullptr;
    Keyboard* _keyboard = nullptr;
    Mouse* _mouse = nullptr;
    Tape* _tape = nullptr;
    Memory* _memory = nullptr;
    Screen* _screen = nullptr;
    SoundManager* _soundManager = nullptr;
    ModuleLogger* _logger = nullptr;

    // Paging lock latch (hardware emulation of port 7FFD bit 5)
    // When true, subsequent writes to port 7FFD are ignored by the hardware
    // This is the actual hardware latch, separate from the emulatorState.p7FFD bit 5
    // which is just a cached copy of the last written value
    bool _7FFD_Locked = false;

    // Set by DecodePortIn/PeripheralPortIn to indicate whether a real hardware device
    // actually responded to the port read. When false, the port is unmapped and the
    // Z80 floating bus logic may apply (prevents floating bus from clobbering
    // legitimate 0xFF values from devices like WD1793).
    bool _lastPortDecoded = false;

    // Registered port handlers from external peripheral devices
    std::map<uint16_t, PortDevice*> _portDevices;

    // Full-decode observer devices (raw Z80 port address). Real bus cards
    // (e.g. ZXM-MoonSound) decode the whole 16-bit address and observe every
    // cycle on their ports, but the model decode rules map those raw addresses
    // onto other devices (ULA #FE family, AY #FFFD, Beta-128 FDC registers).
    // Registering such a card in the exclusive _portDevices map would steal
    // the port from the original device (observed: MoonSound taking #7F killed
    // TR-DOS reads). Observers are therefore tapped at the Z80 I/O funnel with
    // the RAW port before the model decode runs - both devices see the cycle,
    // like on the shared hardware bus.
    std::map<uint16_t, PortDevice*> _fullDecodeDevices;

    // Low-byte full-decode observers (keyed by port & 0xFF). Cards like the
    // ZXM-MoonSound wire only A0..A7 into the CPLD, so every high-byte alias
    // of the card ports reaches the card. Guest software relies on this: the
    // Z80 immediate forms `out (n),a` / `in a,(n)` execute with A in the HIGH
    // address byte (op_noprefix), and the card author's own driver
    // (MoonService v0.3a) writes registers with `ld a,d / out (n),a` - the
    // register number dirties the high byte and the card must still decode
    // the write. Lookup order in the notify taps: exact 16-bit table first,
    // then this one.
    std::array<PortDevice*, 256> _fullDecodeLowByteDevices {};

    // Cached observer read for the current IN cycle: NotifyFullDecodeIn (Z80
    // funnel tap) stores the card's bus value before the model decode runs,
    // so a claim override inside DecodePortIn can return it directly - the
    // trace and direct callers see the value the guest read, and the card is
    // never read twice (a second read would corrupt stateful status
    // registers). Valid only for the port stored in _lastFullDecodeInPort.
    uint16_t _lastFullDecodeInPort = 0x0000;
    uint8_t _lastFullDecodeInValue = 0xFF;

    // Set of ports to mute logging to
    std::set<uint16_t> _loggingMutePorts;

    /// region <Port trace (runtime feature "porttrace")>

    // Cached FeatureManager state (kPortTrace). Written only from the control path
    // (UpdateFeatureCache), read on the emulator thread in the I/O hooks. When false
    // the hooks cost a single bool test + never-taken branch and _portTrace is null.
    bool _portTraceFeatureCache = false;

    // Recorder instance; allocated lazily when the porttrace feature turns on,
    // released (buffer memory freed) when it turns off
    std::unique_ptr<PortDiagnosticRecorder> _portTrace;

    // Frame-scoped rolling counters (debugger status panel); updated only while
    // the porttrace feature is on
    PortActivitySummary _activitySummary;

    /// endregion </Port trace>

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    PortDecoder() = delete;     // Disable default constructor; C++ 11 feature
    PortDecoder(EmulatorContext* context);
    virtual ~PortDecoder();
    /// endregion </Constructors / destructors>

    /// region <Interface methods>
public:
    virtual void reset() = 0;
    virtual uint8_t DecodePortIn(uint16_t addr, uint16_t pc);
    virtual void DecodePortOut(uint16_t addr, uint8_t value, uint16_t pc);

    virtual void SetRAMPage(uint8_t page) { (void)page; /* Intentionally unused */ };
    virtual void SetROMPage(uint8_t page) { (void)page; /* Intentionally unused */ };

    virtual bool IsFEPort(uint16_t port);

    /// Returns true if the last DecodePortIn call was handled by a real hardware device.
    /// Z80::in() uses this to decide whether to apply the floating bus override:
    /// only unmapped ports (no device responded) get the floating bus byte.
    bool WasLastPortDecoded() const { return _lastPortDecoded; }

    uint8_t Default_Port_FE_In(uint16_t port, uint16_t pc);
    void Default_Port_FE_Out(uint16_t port, uint8_t value, uint16_t pc);

    /// Standard Kempston Mouse address decode (A5-A0 = 011111, A9 = 1; A8/A10 select the register)
    static bool Standard_IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister);
    /// Standard decode gated by presence, TR-DOS ports and explicitly registered peripherals.
    /// Virtual: a model with a documented deviation overrides it (design §3.1)
    virtual bool Default_IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister);
    uint8_t Default_Port_KempstonMouse_In(uint16_t port, uint16_t pc);

    /// Whether a decoded port value belongs to the Beta-128 FDC register set
    /// (#1F status/cmd, #3F track, #5F sector, #7F data, #FF system). The port
    /// set is identical on every Beta-128 machine, so the predicate lives on
    /// the base class and is shared by the model decoders for session gating
    bool IsBeta128Port(uint16_t decodedPort) const;

    /// region <Port trace (runtime feature "porttrace")>

    /// Re-read the porttrace feature flag from FeatureManager and instantiate or
    /// release the recorder accordingly. Called from FeatureManager::onFeatureChanged.
    void UpdateFeatureCache();

    /// Recorder access for transports/tests. nullptr while the feature is off.
    PortDiagnosticRecorder* getPortTraceRecorder() { return _portTrace.get(); }

    /// Frame-scoped I/O counters (valid while the porttrace feature is on)
    const PortActivitySummary& getActivitySummary() const { return _activitySummary; }

    /// Model decode table for self-describing trace exports. If-chain decoders
    /// have no mask/match table and return an empty vector (the default).
    virtual std::vector<PortTraceDecodeRule> getPortTraceDecodeRules() const { return {}; }

    /// region <TTD model-specific state (parent TDD 6.4)>
    ///
    /// TTDChipsetState carries only the standard Spectrum 128K ports, so the
    /// TTD framework cannot know that a machine has extra latches - and a
    /// latch nobody captures is lost silently on restore, which is the worst
    /// possible failure for a time-travel debugger.
    ///
    /// The decoder owns those latches, so the decoder declares them. The
    /// declaration is deliberately split from the implementation:
    ///
    ///   GetTTDModelStateIds()   - "I have state beyond the standard ports"
    ///   CreateTTDSerializers()  - "...and here is how to capture it"
    ///
    /// Declaring without implementing is a HARD ERROR at StartRecording: a
    /// model can state the contract before anyone writes the serializer, and
    /// TTD then refuses to record rather than producing a recording that looks
    /// fine and restores wrong. Both default to empty, which is the correct
    /// answer for any machine fully described by the standard 128K ports.

    /// Model-specific state this machine carries beyond TTDChipsetState.
    virtual std::vector<ttd::PeripheralId> GetTTDModelStateIds() const { return {}; }

    /// Serializers for the ids above. Ownership transfers to the caller.
    /// Every id from GetTTDModelStateIds() must be covered.
    virtual std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const
    {
        return {};
    }
    /// endregion </TTD model-specific state>

    /// Assemble the session metadata (model name, timing base, decode rules)
    /// written into every exported trace
    PortTraceSessionInfo getPortTraceSessionInfo() const;

    /// endregion </Port trace>

protected:
    /// Called by subclasses AFTER hardware I/O completes.
    /// Handles: breakpoints, port access tracking, port trace capture, analyzer notifications.
    /// @param port The RAW port address as seen by the Z80 (breakpoints match on raw)
    /// @param result The value read from the port
    /// @param pc Program counter of the IN instruction
    /// @param disp Decode attribution filled by the subclass dispatch (decoded port,
    ///             rule index, gate/inline flags). Defaults to "unmapped/unknown" for
    ///             legacy callers that have no decode information.
    void OnPortInComplete(uint16_t port, uint8_t result, uint16_t pc,
                          const PortDecodeDisposition& disp = {});

    /// Called by subclasses AFTER hardware I/O completes.
    /// Handles: breakpoints, port access tracking, port trace capture, analyzer notifications.
    /// @param port The RAW port address as seen by the Z80 (breakpoints match on raw)
    /// @param value The value written to the port
    /// @param pc Program counter of the OUT instruction
    /// @param disp Decode attribution filled by the subclass dispatch
    void OnPortOutComplete(uint16_t port, uint8_t value, uint16_t pc,
                           const PortDecodeDisposition& disp = {});

    /// Build and push one PortTraceEvent. Only called when _portTraceFeatureCache is true.
    /// Exactly ONE event is recorded per Z80 I/O operation (single-event invariant) —
    /// inline handlers must never call this directly.
    void RecordPortTrace(bool isOut, uint16_t rawPort, uint8_t value, uint16_t pc,
                         const PortDecodeDisposition& disp);

    virtual std::string GetPCAddressLocator(uint16_t pc);
    /// endregion </Interface methods>


    /// region <Interaction with peripherals>
public:
    bool RegisterPortHandler(uint16_t port, PortDevice* device);
    void UnregisterPortHandler(uint16_t port);

    uint8_t PeripheralPortIn(uint16_t port);
    void PeripheralPortOut(uint16_t port, uint8_t value);

    /// Full-decode observer registration (see _fullDecodeDevices): the device
    /// sees every Z80 IN/OUT on the exact raw port address IN ADDITION to the
    /// regular model decode - the shared-bus semantics of a real bus card.
    /// Duplicate registration of the same port is rejected, like RegisterPortHandler.
    bool RegisterFullDecodePort(uint16_t port, PortDevice* device);

    /// Remove a full-decode observer. The device pointer must match the
    /// registration - a stale observer would keep firing into a dead object.
    void UnregisterFullDecodePort(uint16_t port, PortDevice* device);

    /// Low-byte full-decode observer registration (see
    /// _fullDecodeLowByteDevices): the device sees every Z80 IN/OUT whose raw
    /// port low byte matches, in addition to the regular model decode - the
    /// decode behaviour of a card that wires only A0..A7. Duplicate
    /// registration of the same low byte is rejected, like the exact variant.
    bool RegisterFullDecodeLowBytePort(uint8_t port, PortDevice* device);

    /// Remove a low-byte full-decode observer. The device pointer must match
    /// the registration - a stale observer would keep firing into a dead object.
    void UnregisterFullDecodeLowBytePort(uint8_t port, PortDevice* device);

    /// Whether a full-decode low-byte card currently claims this raw port's
    /// low address byte (A0..A7 decode of a card like the ZXM-MoonSound)
    bool IsLowByteClaimedByFullDecodeDevice(uint16_t rawPort) const
    {
        return _fullDecodeLowByteDevices[rawPort & 0xFF] != nullptr;
    }

    /// Observer bus value cached by the Z80 funnel tap (NotifyFullDecodeIn)
    /// for the given port - the value the claiming card drove onto the bus.
    /// 0xFF when the tap has not serviced this port (direct DecodePortIn
    /// calls outside Z80::in, e.g. unit tests)
    uint8_t GetCachedFullDecodeInValue(uint16_t rawPort) const
    {
        return (rawPort == _lastFullDecodeInPort) ? _lastFullDecodeInValue : 0xFF;
    }

    /// Model-decode claim override: a full-decode low-byte card exclusively
    /// owns its claimed port space, so the motherboard partial decoders
    /// (#7FFD paging / AY #BFFD..#FFFD / ULA #FE) must stand down for that
    /// cycle instead of double-delivering the write into paging or AY state.
    /// This models the strict-decode hosts the card was designed for (the
    /// +2A/+3 A14 refinement, Scorpion GAL decoding) while the card is
    /// attached; with no card registered the base decode is untouched.
    /// Beta-128 register decodes keep their own TR-DOS session arbitration
    /// (R6) and are never overridden. Returns true when the model decode
    /// stood down (decodedPort zeroed, disposition flagged).
    bool OverrideDecodeForFullDecodeClaim(uint16_t rawPort, uint16_t& decodedPort,
                                          PortDecodeDisposition& disp);

    /// region <Full-decode clash analysis>

    /// All overlaps between the model decode rules and the registered
    /// full-decode low-byte claims (see PortDecodeClash). Uses the same
    /// self-describing rule table the port trace exports; if-chain decoders
    /// without a table return an empty vector.
    std::vector<PortDecodeClash> FindFullDecodeClashes() const;

    /// Per-rule resolution summary (see PortDecodeRuleOverride): clashing
    /// claim set plus the minimal extra mask bits separating every claim from
    /// the rule while preserving the rule's canonical port. precedences are
    /// marked where no minimal tightening exists.
    std::vector<PortDecodeRuleOverride> FindFullDecodeRuleResolutions() const;

    /// endregion </Full-decode clash analysis>

    /// Z80 OUT tap: forward a raw-port write to the registered observer (if any).
    void NotifyFullDecodeOut(uint16_t port, uint8_t value);

    /// Z80 IN tap: query the observer for a raw-port read. Returns the
    /// observer's bus value (0xFF when none) and sets handled=true when an
    /// observer is registered; claimsBus=true when the observer claims the
    /// read (portDeviceClaimsRead). Z80::in() applies it with
    /// legacy-device priority unless the observer claims the bus: a port
    /// already handled by the model decode keeps that device's value (R6 -
    /// an observer card must not alter an existing device's reads); a
    /// claimed port is driven by the observer (armed card on the shared
    /// bus); an otherwise-undecoded port is driven by the observer too
    /// (floating bus suppressed for it).
    uint8_t NotifyFullDecodeIn(uint16_t port, bool& handled, bool& claimsBus);
    
    /// Unlock port 7FFD paging for snapshot loading or debug sessions
    /// Clears both the emulatorState.p7FFD lock bit AND the hardware latch (_7FFD_Locked)
    /// This ensures subsequent port writes via DecodePortOut() will be accepted
    void UnlockPaging();
    
    /// Lock port 7FFD paging (for debug sessions only, not used in normal operation)
    void LockPaging();

    /// endregion </Interaction with peripherals>


    /// region <Debug information>
public:
    void MuteLoggingForPort(uint16_t port);
    void UnmuteLoggingForPort(uint16_t port);

protected:
    virtual std::string DumpPortValue(uint16_t refPort, uint16_t port, uint8_t value, uint16_t pc, const char* comment = nullptr);
    virtual std::string Dump_FE_value(uint8_t value);

    /// endregion </Debug information>

};
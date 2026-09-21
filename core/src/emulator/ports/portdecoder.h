#pragma once
#include "stdafx.h"

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "emulator/platform.h"
#include "emulator/ports/portdiagrecorder.h"
#include "debugger/ttd/ttdserializable.h"  // ttd::PeripheralId / TTDSerializable (leaf header)

// Opaque declaration (defined in emulator/memory/memory.h): the base decoder
// interface only passes ROMModeEnum by value
enum ROMModeEnum : uint8_t;


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

/// Semantic categories for decoder-registered ports (port-tags-paging design
/// §3.1, P1-2). Metadata only - decode behavior never branches on these; one
/// port may carry several (7FFD is Memory|Rom|Screen at once).
enum class PortTag : uint32_t
{
    None     = 0,

    // ---- Categories ----
    Keyboard = (1u << 0),   // #FE matrix half
    Memory   = (1u << 1),   // latch steers RAM window mapping
    Rom      = (1u << 2),   // latch steers ROM selection (shadow monitor, ROM page)
    Screen   = (1u << 3),   // latch steers video mode / shadow surface
    Storage  = (1u << 4),   // mass-storage register sets (Beta128 FDC, IDE, SD)
    Mouse    = (1u << 5),   // pointer input registers
    Joystick = (1u << 6),   // Kempston joystick
    System   = (1u << 7),   // service windows, config latches (SMUC, ProfROM...)

    // ---- Sound family: category bit + member bit ----
    // Member tags embed the Sound bit, so family membership is a single
    // AND test: (tags & PortTag::Sound) != 0.
    Sound           = (1u << 8),
    SoundAy         = Sound | (1u << 16),  // #FFFD/#BFFD (TurboSound pairs)
    SoundCovox      = Sound | (1u << 17),  // #FB-class DAC
    SoundSoundDrive = Sound | (1u << 18),  // #F1/#F3/#F9/#FB quad DAC
    SoundGs         = Sound | (1u << 19),  // reserved (GS not on master)
    SoundMoonsound  = Sound | (1u << 20),  // reserved (P2-2/P2-4 design)
    SoundTsFm       = Sound | (1u << 21),  // TurboSound FM (YM2203)

    // ---- Storage family: category bit + member bit ----
    // Member tags embed the Storage bit for family membership tests.
    StorageFdc      = Storage | (1u << 22), // Beta128 / TR-DOS floppy controller
    StorageIde      = Storage | (1u << 23), // IDE / ATA adapters (Nemo, Wild, ATM IDE)
    StorageSd       = Storage | (1u << 24), // SD card interfaces (divMMC, ZXUno, ZXMMC)
    StorageCf       = Storage | (1u << 25), // CompactFlash adapters

    // ---- Network family: reserved for future ----
    Network         = (1u << 9),            // network interface category
    NetworkZifi     = Network | (1u << 26), // ZiFi WiFi module (#C7xx / #57xx)
    NetworkEth      = Network | (1u << 27), // Ethernet adapters (reserved)

    // ---- Expansion / misc: reserved for future ----
    Rtc             = (1u << 10),           // real-time clock (SMUC, GLUK, MC146818)
    Printer         = (1u << 11),           // printer port / ZX Printer
    Serial          = (1u << 12),           // serial / RS-232 interfaces
    Dma             = (1u << 13),           // DMA controllers (reserved)
    Video           = (1u << 14),           // extra video hardware (GS, ATM, TSConf)
};

typedef uint32_t PortTagSet;

constexpr PortTagSet PORT_TAG_CATEGORY_MASK = 0x0000FFFFu;  // category bits
constexpr PortTagSet PORT_TAG_SOUND_MEMBERS = 0xFFFF0000u;  // member bits

/// enum class carries no implicit conversions, so set arithmetic needs
/// explicit operators (design review refinement 1). All constexpr - zero
/// runtime cost, and no implicit-conversion warnings on gcc/clang/msvc.
constexpr PortTagSet operator|(PortTag a, PortTag b)
{
    return static_cast<PortTagSet>(a) | static_cast<PortTagSet>(b);
}
constexpr PortTagSet operator|(PortTagSet a, PortTag b)
{
    return a | static_cast<PortTagSet>(b);
}
constexpr PortTagSet operator&(PortTagSet a, PortTag b)
{
    return a & static_cast<PortTagSet>(b);
}
constexpr PortTagSet operator&(PortTag a, PortTag b)
{
    return static_cast<PortTagSet>(a) & static_cast<PortTagSet>(b);
}

/// Single-tag conversion for row tables - bare `PortTag::Keyboard` would not
/// convert to the set type implicitly in a brace initializer
constexpr PortTagSet Tags(PortTag tag)
{
    return static_cast<PortTagSet>(tag);
}

/// Live-value binding for tagged latch rows: how a row finds its current
/// value in EmulatorState (port-tags-paging design §4.1, P1-2). Kept as an
/// enum (not a pointer/member offset) so PortMapEntry stays an aggregate
/// usable in brace-initialized row tables.
enum class PagingLatch : uint8_t
{
    None,
    P7FFD, P1FFD, PDFFD, PFDFD, P7EFD, PEFF7, PFF77,
    AFE, AFB,                       // ATM 4.50 system ports (atm branch)
    PFFF7Window0, PFFF7Window1,     // ATM 7.10/ATM3 per-window latches
    PFFF7Window2, PFFF7Window3,     // (reserved until the decoders land)
    PBD, PTS, PMEM                  // TSConf (reserved)
};

/// region <Tag / latch serialization - single source for every automation surface>
/// WebAPI /ports + /state/paging, MCP aspects, the CLI ports/paging commands and
/// the Lua/Python ports_map()/paging_state() bindings all render tag and latch
/// names through these functions, so a name exists exactly once (parity rule).

/// @brief Lowercase wire names for every tag carried by a port-map row
/// @param tags Tag-set bitmask as stored in PortMapEntry::tags
/// @return Names in taxonomy order (category bits, then sound members); a row
///         may carry several sound members (Pentagon #FB is covox AND sounddrive),
///         so every member bit is reported - never only the first match
std::vector<std::string> PortTagSetToStrings(PortTagSet tags);

/// @brief Wire name of a live-value latch binding
/// @param latch Latch binding as stored in PortMapEntry::latch
/// @return Static string ("p7FFD", "p1FFD", "pFFF7_w2", ...), or nullptr for
///         PagingLatch::None (callers serialize that as null/absent)
const char* PagingLatchToString(PagingLatch latch);

/// @brief One decoded field of a paging latch value (design §5.1 dictionary)
struct DecodedLatchField
{
    std::string key;
    bool isBool = false;      // false = the field has int semantics
    int intValue = 0;         // valid when !isBool
    bool boolValue = false;   // valid when isBool
};

/// @brief Decode a paging latch value into the §5.1 dictionary keys (ram_bank,
///        shadow_screen, special_paging, ...). Which keys apply to #1FFD depends
///        on the machine model (Scorpion window latch vs +3 special paging).
/// @param latch Which latch the value belongs to
/// @param value Raw latch value as returned by PortDecoder::ReadPagingLatch()
/// @param model Machine model (MEM_MODEL)
/// @param ramSizeKB Optional RAM size for extended bank decode (Pentagon 512:
///        the only master decoder folding #7FFD bits [6:7] into the bank index)
/// @return Decoded fields; empty for latches without a dictionary entry
std::vector<DecodedLatchField> DecodePagingLatch(PagingLatch latch, uint32_t value, MEM_MODEL model, uint32_t ramSizeKB = 0);

/// endregion </Tag / latch serialization>

/// One row of the static port map reported by GET /api/v1/emulator/{id}/ports
/// (P1-5 static port-map introspection). `mask`/`match` mirror the decoder's
/// address qualification ((port & mask) == match), `port` is the canonical
/// representative port, `device` a human-readable name and `gate` the runtime
/// condition that can take the device off the bus (nullptr = always answers).
/// `tags`/`latch` extend the row into the tagged port registry (P1-2 design
/// §4.1); the default member initializers keep legacy 5-element brace rows
/// compiling unchanged.
struct PortMapEntry
{
    uint16_t port;
    uint16_t mask;
    uint16_t match;
    const char* device;                 // human-readable
    const char* gate;                   // nullptr = ungated
    PortTagSet tags = 0;                // semantic categories; 0 = legacy
                                        // registered-peripheral row (untagged)
    PagingLatch latch = PagingLatch::None;  // live-value binding; None for
                                            // non-latch registers
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

    /// @brief Check whether this build has a port decoder for the model
    /// @param model MEM_MODEL value to check
    /// @return True when GetPortDecoderForModel can construct a decoder (no throw),
    ///         false for models whose decode logic is not implemented yet (ATM/TS-Config/
    ///         GMX/KAY/... on master - see the atm branch)
    /// @note MUST stay in sync with the switch in GetPortDecoderForModel - a model listed
    ///       here but missing there makes creation throw; listed there but missing here
    ///       hides a creatable model from GET /emulator/status models_creatable.
    static bool IsModelSupported(MEM_MODEL model);
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

    // Semantic tags passed via the RegisterPortHandler overload, so dynamic
    // devices land in the tag collections instead of the anonymous fallback
    // row (port-tags-paging design §4.2)
    std::map<uint16_t, PortTagSet> _portDeviceTags;

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

    /// Apply model-specific register defaults for the RESET= boot mode (port of the
    /// original reset(mode) model blocks: e.g. ATM installs the FF77/pFFF7
    /// memory-manager defaults when the requested mode is RM_DOS). Base: none.
    virtual void ApplyBootROMDefaults(ROMModeEnum mode) { (void)mode; }

    /// Re-run the model-specific set_banks() memory-manager branch. Only decoders
    /// with their own memory manager (ATM710/ATM3) override this; all other models
    /// use the generic Memory::UpdateZ80Banks() mapping instead.
    virtual void UpdateModelMemoryBanks() {}

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
    virtual bool Default_IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const;
    /// Mouse-port predicate as the model's DecodePortIn actually applies it: the default
    /// is the gated standard decode, models with a documented deviation (Scorpion TR-DOS
    /// trigger / Shadow Monitor beta mirrors) override it. Introspection must probe this
    /// one, not Default_ directly, or model deviations go unreported
    virtual bool IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const;
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

    /// Static port map for introspection ("which devices respond to which ports
    /// on this machine"). Single per-model switch over config.mem_model, mirroring
    /// the decode conditions of the IsPort_* helpers / decode tables in the model
    /// decoders - keep both sides in sync when a decode changes. Rows are
    /// fitment-conditional: mouse rows only when the mouse device is present,
    /// Beta128 rows only when the TR-DOS interface is configured. Every static
    /// row carries its semantic tags and, for latch registers, the live-value
    /// binding (port-tags-paging design §3.2 - the assignment table is
    /// normative for PortDecoder_PortTag_Test).
    std::vector<PortMapEntry> getPortMapEntries() const;

    /// Entries carrying ALL of the given tag bits (exact-subset match over
    /// the full tag set - a member bit in the query demands that member, so
    /// HasAnyTaggedPort(SoundCovox) is a true per-soundcard fitment answer;
    /// use GetSoundEntries to enumerate a whole family). Thin filter over
    /// getPortMapEntries() - the row count is a few dozen and fitment-
    /// conditional rows would invalidate any cached index, so a linear scan
    /// is simpler to keep correct. Rows are returned BY VALUE:
    /// getPortMapEntries() builds a fresh vector per call, so pointer views
    /// into it would dangle.
    std::vector<PortMapEntry> GetEntriesByTags(PortTagSet categories) const;

    /// Entries of one Sound family member (SoundAy, SoundCovox, ...).
    std::vector<PortMapEntry> GetSoundEntries(PortTag member) const;

    /// Whether at least one entry carries ALL of the given category bits -
    /// the fitment/capability answer for future consumers (P2-3).
    bool HasAnyTaggedPort(PortTagSet categories) const;

    /// All latch rows (latch != None) that can steer the given categories -
    /// the direct input to /state/paging's static half (P1-2 Phase 2).
    std::vector<PortMapEntry> GetPagingLatches(PortTagSet categories = Tags(PortTag::Memory)) const;

    /// Single switch PagingLatch -> EmulatorState field (the fields already
    /// exist and are TTD-checkpointed - no new state). Static: the mapping
    /// needs no decoder state. Reserved atm/TSConf members read 0 until their
    /// decoders land on the atm branch.
    static uint32_t ReadPagingLatch(PagingLatch latch, const EmulatorState& state);

    /// Mouse routing answer (design Q4, feeds /mouse/status and /ports live):
    /// would a mouse port read actually be decoded right now, and if not, why.
    /// Probes the canonical buttons port #FADF through the virtual
    /// IsPort_KempstonMouse gate, so model-specific deviations
    /// (Scorpion TR-DOS / Shadow Monitor gating) are honored.
    void GetMouseRoutingState(bool& decoded, std::string& note) const;


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
    bool RegisterPortHandler(uint16_t port, PortDevice* device,
                             PortTagSet tags = 0);
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
    ///
    /// @param isRead Pass true from DecodePortIn, false from DecodePortOut.
    ///        A write always stands the model decode down for a registered
    ///        low byte - there is no "drives the bus" ambiguity for an OUT.
    ///        A read is different: only a device whose portDeviceClaimsRead()
    ///        is true for this exact raw port actually asserts its output
    ///        buffer this cycle (e.g. ZXM-MoonSound answers #C4/#C5/#C7
    ///        unconditionally, #7F only once OPL4 NEW2 is armed, but never
    ///        #C6/#7E - those are write-only address latches). For a
    ///        registered-but-non-claiming read the model's own device (ULA/
    ///        AY/FDC) must answer exactly as if the card were not attached -
    ///        returning false here leaves decodedPort untouched so the
    ///        caller's normal decode chain runs.
    bool OverrideDecodeForFullDecodeClaim(uint16_t rawPort, uint16_t& decodedPort,
                                          PortDecodeDisposition& disp, bool isRead);

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
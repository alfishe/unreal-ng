#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// @file sprinterpldconfiguration.h
/// @brief The extension point for Sprinter PLD configurations (Sprinter
/// tdd-ports-memory §6.1, decision D11).
///
/// The Sprinter's PLD loads a bitstream at power-on and the BIOS (or a game)
/// can load another one at any time. Each bitstream is, in effect, a different
/// machine on the same board. The emulator keeps one decoder and lets a
/// configuration module replace only the parts its firmware changes:
///   1. port decoding (its own internal codes),
///   2. memory mapping (its own bank rule),
///   3. video (renderer + INT source) - phase S2,
///   4. accelerator - phase S5.
/// Every hook a module does not override falls through to Standard, which is
/// itself a module (SprinterPldStandard): the decoder reaches the standard
/// behavior only through this interface.
///
/// A module is identified by a descriptor {name, full-stream hash, head hash};
/// the head hash is MAME's (the first 4 096 configuration writes), so MAME's
/// constants can be reused. After a load the registry looks the module up by
/// the full hash, then by the head hash; an unknown bitstream runs Standard
/// with a warning that names both hashes.
///
/// Worked example: a game reloads the PLD with the "Thunder in the Deep"
/// bitstream. The head hash matches MAME's Game constant #3861CFA4, but v1 has
/// no Game module, so the lookup falls back to Standard and the log says
/// "unknown PLD bitstream, full hash ..., head hash 3861CFA4".

class PortDecoder_Sprinter;
class SprinterMemory;
class SprinterVideoRenderer;
struct SprinterPldState;

/// Reset kinds a module reacts to (tdd-ports-memory §7)
enum class SprinterResetKind : uint8_t
{
    PowerOn,      ///< power-on: cells to their defaults, configuration again
    Button,       ///< RESET button: as power-on, RAM and fast RAM kept
    Reload,       ///< code #2E: back to the loader, the module chosen again after the load
    Configured,   ///< the load finished: the CPU starts the BIOS (the PLD's own reset)
    SoftReset,    ///< a write to page #A0: CPU reset only, the PLD stays configured
};

struct SprinterPldModuleDescriptor
{
    std::string name;
    uint32_t fullHash = 0;  ///< FNV-1a over every byte of the 473 720 configuration writes
    uint32_t headHash = 0;  ///< MAME's hash of the first 4 096 writes
};

class SprinterPldConfiguration
{
public:
    virtual ~SprinterPldConfiguration() = default;

    virtual const SprinterPldModuleDescriptor& Descriptor() const = 0;

    /// region <Hook 1: port decoding>
    /// An IN that the port table resolved to `code`. Return true when the
    /// module answered (value set); false falls through to Standard
    virtual bool ReadCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t& value)
    {
        (void)decoder; (void)code; (void)port; (void)value;
        return false;
    }
    /// An OUT resolved to `code` (the cell storage of #C0-#EF is already done)
    virtual bool WriteCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t value)
    {
        (void)decoder; (void)code; (void)port; (void)value;
        return false;
    }
    /// endregion </Hook 1>

    /// region <Hook 2: memory mapping>
    /// Map the four windows from the PLD state. Return false to use Standard's rule
    virtual bool UpdateBanks(SprinterMemory& memory, const SprinterPldState& pld)
    {
        (void)memory; (void)pld;
        return false;
    }
    /// endregion </Hook 2>

    /// region <Hook 3: video>
    /// The renderer of the module's picture (ScreenSprinter draws every span
    /// through it); null = Standard's. The INT source stays Standard's
    /// SprinterIntSource until a module needs its own rule (tdd-video §1)
    virtual const SprinterVideoRenderer* VideoRenderer() const { return nullptr; }
    /// endregion </Hook 3>

    /// region <Lifecycle and state>
    /// The module became active (after a load, always followed by a CPU reset)
    virtual void OnActivate(SprinterPldState& pld) { (void)pld; }
    virtual void OnReset(SprinterResetKind kind, SprinterPldState& pld) { (void)kind; (void)pld; }
    /// The module's own state (an opaque blob for TTD and snapshots, phase S7)
    virtual size_t StateSize() const { return 0; }
    virtual void SaveState(uint8_t* dst) const { (void)dst; }
    virtual void LoadState(const uint8_t* src) { (void)src; }
    /// endregion </Lifecycle and state>
};

/// The modules the machine knows, Standard first (index 0). Filled at start-up;
/// tests add a stub module
class SprinterPldConfigurationRegistry
{
public:
    SprinterPldConfigurationRegistry();

    /// Add a module; returns its index
    size_t Register(std::unique_ptr<SprinterPldConfiguration> module);

    size_t Count() const { return _modules.size(); }
    SprinterPldConfiguration& At(size_t index) const { return *_modules[index]; }
    SprinterPldConfiguration& Standard() const { return *_modules[kStandardIndex]; }

    /// Index of the module whose full hash, or else head hash, matches; -1 when none does
    int Find(uint32_t fullHash, uint32_t headHash) const;
    /// Index of the module with this name; -1 when none has it (TTD stores the module by name)
    int FindByName(const std::string& name) const;

    static constexpr size_t kStandardIndex = 0;

private:
    std::vector<std::unique_ptr<SprinterPldConfiguration>> _modules;
};

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
///   4. accelerator - phase S5,
///   5. the cells' initial contents (the embedded RAM's, set when a load changes the configuration).
/// Every hook a module does not override falls through to Standard, which is
/// itself a module (SprinterPldStandard): the decoder reaches the standard
/// behavior only through this interface.
///
/// A module is identified by a descriptor {name, full-stream hash, head hash,
/// known streams}; the head hash is MAME's (the first 4 096 configuration
/// writes), so MAME's constants can be reused. After a load the registry looks
/// the module up by the full hash (any of its known streams), then by the head
/// hash; an unknown bitstream runs Standard with a warning that names both hashes.
///
/// Worked example: a game reloads the PLD with the "Thunder in the Deep"
/// bitstream (GAME_00.ACX, LDConf's GC.BIN: the same one). Its full hash
/// #C0FA3055 is the Game module's, so the Game module runs (SprinterPldGame);
/// a stream that matches only MAME's head hash #3861CFA4 would get it too. A
/// stream nobody knows (LDConf's STREAM.300) runs Standard and the log says
/// "unknown PLD bitstream, full hash A65B49FC, head hash D0953276".

class PortDecoder_Sprinter;
class SprinterAccelerator;
class SprinterBeamVideo;
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

/// One known bitstream file of a configuration: its full hash and where it comes from
struct SprinterPldStream
{
    uint32_t fullHash = 0;
    std::string source;  ///< e.g. "BIOS 3.04 ROM page #C", "GAME_00.ACX / GC.BIN"
};

struct SprinterPldModuleDescriptor
{
    std::string name;
    uint32_t fullHash = 0;  ///< FNV-1a over every byte of the 473 720 configuration writes
    uint32_t headHash = 0;  ///< MAME's hash of the first 4 096 writes
    /// Every known bitstream of the configuration (fullHash's first): builds of the same logic shipped in other
    /// BIOS images - the BIOS 3.06 and 3.07 ROMs carry their own Standard builds
    std::vector<SprinterPldStream> streams;

    /// The known stream with this full hash, or null
    const SprinterPldStream* Stream(uint32_t full) const
    {
        for (const SprinterPldStream& stream : streams)
            if (stream.fullHash == full)
                return &stream;
        return nullptr;
    }
    bool KnowsFullHash(uint32_t full) const { return full == fullHash || Stream(full) != nullptr; }
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
    /// A picture with state in beam order (the Game module's grid offset); null = a stateless picture
    /// (Standard). ScreenSprinter runs it on every catch-up, the decoder closes its frames (sprintervideorenderer.h)
    virtual SprinterBeamVideo* BeamVideo() { return nullptr; }
    /// endregion </Hook 3>

    /// region <Hook 4: accelerator>
    /// The accelerator of the module (the CPU's bus agent while it is active); null = Standard's
    /// (Sprinter tdd-accel-sound-input §1). A module without one (a DooM-like stretch accelerator
    /// would bring its own) runs the standard accelerator
    virtual SprinterAccelerator* Accelerator(PortDecoder_Sprinter& decoder)
    {
        (void)decoder;
        return nullptr;
    }
    /// endregion </Hook 4>

    /// region <Hook 5: the cells' initial contents>
    /// The internal cells #C0-#FF live in the PLD's embedded RAM (DCP.TDF MEM), whose contents come with
    /// the bitstream (DCP.MIF). The decoder sets them when a load changes the configuration (a reload of the
    /// running one keeps them: the launchers' reset intercept survives the RESET button). Fill `cells` (64
    /// bytes, index = code - #C0) and return true; false = Standard's (DCP.MIF of the shipped bitstream)
    virtual bool InitialCells(uint8_t* cells) const
    {
        (void)cells;
        return false;
    }
    /// endregion </Hook 5>

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

/// The modules the machine knows: Standard first (index 0), then Game (index 1).
/// Filled at start-up; tests add a stub module
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
    static constexpr size_t kGameIndex = 1;

private:
    std::vector<std::unique_ptr<SprinterPldConfiguration>> _modules;
};

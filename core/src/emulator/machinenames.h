#pragma once

/// @file machinenames.h
/// @brief The machine names a create, a start or a model switch takes, resolved the way EmulatorManager creates them,
/// so a surface validates a name before it touches the running machine and never refuses one the manager accepts:
/// a model's short name ("PENTAGON"), a machine variant ("TSL-VDAC2", "PROFI-PLUS", their aliases; machinevariants.h)
/// or a ZX-Poly configuration ("ZXPOLY-128K"; zxpolygroup.h).
///
/// Also the machine history: the names the machines of this process were created with, newest first (the deck's
/// "back to the last used model", the catalogue action mach.previous).
///
/// Example: Resolve("profiplus", 0) -> {name "PROFI-PLUS", kind Variant, base PROFI, ramKb 1024};
/// Resolve("ZXPOLY-128K", 512) -> error "its RAM size is fixed".

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct TMemModel;
struct CONFIG;

namespace machines
{

enum class NameKind : uint8_t
{
    Model,    ///< a TMemModel short name
    Variant,  ///< a MachineVariant: the base model with a fixed board
    ZxPoly,   ///< a ZX-Poly configuration: four instances of the base model
};

struct ResolvedName
{
    std::string name;                  ///< canonical: the short name, the variant's or the configuration's name
    NameKind kind = NameKind::Model;
    const TMemModel* base = nullptr;   ///< the model (a variant's / a configuration's base model)
    uint32_t ramKb = 0;                ///< the RAM size the machine gets (0: the model's default)
};

/// The name (case-insensitive) as EmulatorManager::CreateEmulatorWithModel(AndRAM) takes it, with the RAM size the
/// caller asks for (0: the default). False with `error` (one sentence naming GET /api/v1/emulator/models) for an
/// unknown name, a model this build cannot create, a variant it does not support, a RAM size the machine has not
bool Resolve(const std::string& name, uint32_t ramKb, ResolvedName& out, std::string& error);

/// The name a running machine was created as: the variant its config matches, else the model's short name ("" when
/// the model has none)
std::string NameOf(const CONFIG& config);

/// The machine history of the process (newest first, a name once: the latest RAM size). EmulatorManager notes every
/// machine it creates by name (not the four modules of a ZX-Poly group: the group's configuration)
class MachineHistory
{
public:
    struct Entry
    {
        std::string name;    ///< as Resolve's canonical name
        uint32_t ramKb = 0;  ///< the RAM size asked for (0: the default)
    };

    static MachineHistory& Instance();

    void Note(const std::string& name, uint32_t ramKb);
    std::vector<Entry> List() const;
    /// The newest entry that is not `current` (a name, case-insensitive); false when there is none
    bool Previous(const std::string& current, Entry& out) const;
    void Clear();

    static constexpr size_t kLimit = 16;

private:
    mutable std::mutex _mutex;
    std::vector<Entry> _entries;
};

}  // namespace machines

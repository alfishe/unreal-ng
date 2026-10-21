#include "stdafx.h"

#include "emulator/machinenames.h"

#include <algorithm>
#include <cctype>

#include "common/stringhelper.h"
#include "emulator/config.h"
#include "emulator/machinevariants.h"
#include "emulator/platform.h"
#include "emulator/zxpoly/zxpolygroup.h"

namespace machines
{

namespace
{
const char* const kListHint = " (GET /api/v1/emulator/models lists them)";

bool SameName(const std::string& a, const std::string& b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}
}  // namespace

bool Resolve(const std::string& name, uint32_t ramKb, ResolvedName& out, std::string& error)
{
    out = ResolvedName{};
    if (name.empty())
    {
        error = std::string("model: a model short name") + kListHint;
        return false;
    }

    if (const ZXPolyGroup::Configuration* configuration = ZXPolyGroup::FindConfiguration(name))
    {
        const TMemModel* base = Config::FindModelByShortName(configuration->baseModel);
        if (base == nullptr || !Config::IsModelCreatable(*base))
        {
            error = "'" + std::string(configuration->name) + "' is not creatable on this build (its " +
                    configuration->baseModel + " modules are not)";
            return false;
        }
        if (ramKb > 0 && ramKb != base->defaultRAM)
        {
            error = "'" + std::string(configuration->name) + "' is a ZX-Poly configuration: its RAM size is fixed (" +
                    std::to_string(base->defaultRAM) + "KB per module), omit ram_kb";
            return false;
        }
        out.name = configuration->name;
        out.kind = NameKind::ZxPoly;
        out.base = base;
        return true;
    }

    if (const MachineVariant* variant = MachineVariants::Find(name))
    {
        const TMemModel* base = Config::FindModelByShortName(variant->baseModel);
        std::string reason;
        if (base == nullptr || !Config::IsModelCreatable(*base))
        {
            error = "'" + std::string(variant->name) + "' is not creatable on this build (its base model " +
                    variant->baseModel + " is not)";
            return false;
        }
        if (!variant->supported(&reason))
        {
            error = "'" + std::string(variant->name) + "' is not available: " + reason;
            return false;
        }
        if (ramKb > 0 && ramKb != variant->ramKb)
        {
            error = "'" + std::string(variant->name) + "' runs with " + std::to_string(variant->ramKb) +
                    "KB RAM: omit ram_kb or pass " + std::to_string(variant->ramKb);
            return false;
        }
        out.name = variant->name;
        out.kind = NameKind::Variant;
        out.base = base;
        out.ramKb = variant->ramKb;
        return true;
    }

    const TMemModel* model = Config::FindModelByShortName(name);
    if (model == nullptr || model->ShortName == nullptr || !SameName(name, model->ShortName))
    {
        error = "Unknown model '" + name + "'" + kListHint;
        return false;
    }
    if (!Config::IsModelCreatable(*model))
    {
        error = "model '" + name + "' is not creatable on this build (port decoder or config folder missing)";
        return false;
    }
    uint32_t ram = ramKb;
    if (ram > 0)
    {
        std::string note;
        ram = Config::EffectiveRamSize(model->Model, ram, note);
        if ((ram & model->AvailRAMs) == 0)
        {
            error = "RAM size " + std::to_string(ramKb) + "KB is not supported by model '" + name + "'" + kListHint;
            return false;
        }
    }
    out.name = model->ShortName;
    out.kind = NameKind::Model;
    out.base = model;
    out.ramKb = ram;
    return true;
}

std::string NameOf(const CONFIG& config)
{
    if (const MachineVariant* variant = MachineVariants::Of(config))
        return variant->name;
    const TMemModel* model = Config::FindModelByEnum(config.mem_model);
    return model != nullptr && model->ShortName != nullptr ? std::string(model->ShortName) : std::string();
}

/// region <MachineHistory>

MachineHistory& MachineHistory::Instance()
{
    static MachineHistory history;
    return history;
}

void MachineHistory::Note(const std::string& name, uint32_t ramKb)
{
    if (name.empty())
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.erase(std::remove_if(_entries.begin(), _entries.end(),
                                  [&](const Entry& entry) { return SameName(entry.name, name); }),
                   _entries.end());
    _entries.insert(_entries.begin(), Entry{name, ramKb});
    if (_entries.size() > kLimit)
        _entries.resize(kLimit);
}

std::vector<MachineHistory::Entry> MachineHistory::List() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _entries;
}

bool MachineHistory::Previous(const std::string& current, Entry& out) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Entry& entry : _entries)
        if (!SameName(entry.name, current))
        {
            out = entry;
            return true;
        }
    return false;
}

void MachineHistory::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _entries.clear();
}

/// endregion </MachineHistory>

}  // namespace machines

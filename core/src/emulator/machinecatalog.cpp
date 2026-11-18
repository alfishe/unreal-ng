#include "machinecatalog.h"

#include <algorithm>
#include <cctype>

#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/machinevariants.h"
#include "emulator/platform.h"
#include "emulator/zxpoly/zxpolygroup.h"

namespace
{

/// A family and its members in menu order: model short names, variant names, ZX-Poly configuration names
struct FamilyDef
{
    const char* id;
    const char* title;
    std::vector<const char*> members;
};

/// The grouping (design §2). A name missing here still shows up, under Other clones (kFallbackFamily)
const std::vector<FamilyDef>& FamilyDefs()
{
    static const std::vector<FamilyDef> defs = {
        {"sinclair", "Sinclair", {"48K", "48K-BETA128", "48K-PLUSD", "128k", "128K-PLUSD", "PLUS2", "PLUS2A", "PLUS3"}},
        {"pentagon", "Pentagon", {"PENTAGON", "P2666"}},
        {"atm", "ATM Turbo", {"ATM450", "ATM710"}},
        {"zx-evo", "ZX-Evo", {"ATM3", "TSL", "TSL-VDAC2", "TSL-VDAC4"}},
        {"profi", "Profi", {"PROFI3", "PROFI", "PROFI-PLUS"}},
        {"scorpion", "Scorpion", {"SCORPION", "PROFSCORP", "GMX"}},
        {"next", "ZX Spectrum Next", {"NEXT", "NEXT-KS1", "NEXT-KS3", "NEXT-DDMA"}},
        {"zx-poly", "ZX-Poly", {"ZXPOLY-48K", "ZXPOLY-128K", "ZXPOLY-PENTAGON"}},
        {"other", "Other clones", {"KAY", "QUORUM", "LSY256", "PHOENIX"}},
        {"sprinter", "Sprinter", {"SPRINTER"}},
    };
    return defs;
}
constexpr const char* kFallbackFamily = "other";

bool SameName(const std::string& a, const std::string& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

std::vector<uint32_t> RamSizes(unsigned mask)
{
    std::vector<uint32_t> sizes;
    for (const uint32_t ram : {48u, 128u, 256u, 512u, 1024u, 2048u, 4096u})
        if ((mask & ram) != 0)
            sizes.push_back(ram);
    return sizes;
}

/// Every machine of the three tables, in table order
std::vector<MachineEntry> Machines()
{
    std::vector<MachineEntry> machines;
    for (const TMemModel& model : Config::GetAvailableModels())
    {
        if (model.ShortName == nullptr || model.ShortName[0] == '\0')
            continue;
        MachineEntry entry;
        entry.name = model.ShortName;
        entry.title = model.FullName != nullptr ? model.FullName : model.ShortName;
        entry.kind = MachineKind::Model;
        entry.baseModel = model.ShortName;
        entry.ramSizesKb = RamSizes(model.AvailRAMs);
        entry.defaultRamKb = model.defaultRAM;
        entry.creatable = Config::IsModelCreatable(model) && !entry.ramSizesKb.empty();
        machines.push_back(std::move(entry));
    }
    for (const MachineVariant& variant : MachineVariants::All())
    {
        const TMemModel* base = Config::FindModelByShortName(variant.baseModel);
        MachineEntry entry;
        entry.name = variant.name;
        entry.title = variant.title;
        entry.description = variant.description;
        entry.kind = MachineKind::Variant;
        entry.baseModel = variant.baseModel;
        entry.ramSizesKb = {variant.ramKb};
        entry.defaultRamKb = variant.ramKb;
        entry.creatable = base != nullptr && Config::IsModelCreatable(*base) && variant.supported(nullptr);
        machines.push_back(std::move(entry));
    }
    for (const ZXPolyGroup::Configuration& configuration : ZXPolyGroup::Configurations())
    {
        const TMemModel* base = Config::FindModelByShortName(configuration.baseModel);
        MachineEntry entry;
        entry.name = configuration.name;
        entry.title = configuration.title;
        entry.kind = MachineKind::ZXPoly;
        entry.baseModel = configuration.baseModel;
        entry.defaultRamKb = base != nullptr ? base->defaultRAM : 0;
        entry.ramSizesKb = {entry.defaultRamKb};
        entry.creatable = base != nullptr && Config::IsModelCreatable(*base);
        machines.push_back(std::move(entry));
    }
    return machines;
}

std::vector<MachineFamily> Build()
{
    std::vector<MachineEntry> machines = Machines();
    std::vector<bool> placed(machines.size(), false);
    std::vector<MachineFamily> families;
    for (const FamilyDef& def : FamilyDefs())
    {
        MachineFamily family{def.id, def.title, {}};
        for (const char* member : def.members)
            for (size_t i = 0; i < machines.size(); ++i)
                if (!placed[i] && SameName(machines[i].name, member))
                {
                    family.machines.push_back(machines[i]);
                    placed[i] = true;
                }
        families.push_back(std::move(family));
    }
    // A machine the family table does not name yet: never lost from the menus
    for (size_t i = 0; i < machines.size(); ++i)
        if (!placed[i])
            for (MachineFamily& family : families)
                if (family.id == kFallbackFamily)
                    family.machines.push_back(machines[i]);
    return families;
}

/// The canonical name a request name stands for: a variant or ZX-Poly name / alias, a model short name or alias
std::string Canonical(const std::string& name)
{
    if (const MachineVariant* variant = MachineVariants::Find(name))
        return variant->name;
    if (const ZXPolyGroup::Configuration* configuration = ZXPolyGroup::FindConfiguration(name))
        return configuration->name;
    // A model's own short name first (exactly), then its aliases through the model lookup
    for (const TMemModel& model : Config::GetAvailableModels())
        if (model.ShortName != nullptr && SameName(name, model.ShortName))
            return model.ShortName;
    if (const TMemModel* model = Config::FindModelByShortName(name); model != nullptr && model->ShortName != nullptr)
        return model->ShortName;
    return name;
}

} // namespace

namespace MachineCatalog
{

const std::vector<MachineFamily>& All()
{
    static const std::vector<MachineFamily> families = Build();
    return families;
}

std::vector<MachineFamily> Creatable()
{
    std::vector<MachineFamily> families;
    for (const MachineFamily& family : All())
    {
        MachineFamily kept{family.id, family.title, {}};
        for (const MachineEntry& machine : family.machines)
            if (machine.creatable)
                kept.machines.push_back(machine);
        if (!kept.machines.empty())
            families.push_back(std::move(kept));
    }
    return families;
}

MachinePlace Locate(const std::vector<MachineFamily>& families, const std::string& name)
{
    const std::string canonical = Canonical(name);
    for (size_t f = 0; f < families.size(); ++f)
        for (size_t m = 0; m < families[f].machines.size(); ++m)
            if (SameName(families[f].machines[m].name, canonical))
                return {static_cast<int>(f), static_cast<int>(m)};
    return {};
}

std::string FamilyOf(const std::string& name)
{
    const MachinePlace place = Locate(All(), name);
    return place.family >= 0 ? All()[static_cast<size_t>(place.family)].id : std::string();
}

std::string NameOf(Emulator& emulator)
{
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(emulator);
    if (!identity.Valid)
        return {};
    if (!identity.Variant.empty())
        return identity.Variant;
    if (identity.ZXPoly)
        for (const ZXPolyGroup::Configuration& configuration : ZXPolyGroup::Configurations())
            if (SameName(configuration.baseModel, identity.Model))
                return configuration.name;
    return identity.Model;
}

StateNode ToState(const std::vector<MachineFamily>& families, const std::string& current, uint32_t currentRamKb)
{
    StateNode root = StateNode::Object();
    StateNode list = StateNode::Array();
    for (const MachineFamily& family : families)
    {
        StateNode node = StateNode::Object();
        node["id"] = family.id;
        node["title"] = family.title;
        StateNode machines = StateNode::Array();
        for (const MachineEntry& machine : family.machines)
        {
            StateNode entry = StateNode::Object();
            entry["name"] = machine.name;
            entry["title"] = machine.title;
            if (!machine.description.empty())
                entry["description"] = machine.description;
            entry["kind"] = machine.kind == MachineKind::Variant  ? "variant"
                            : machine.kind == MachineKind::ZXPoly ? "zxpoly"
                                                                  : "model";
            entry["base_model"] = machine.baseModel;
            StateNode sizes = StateNode::Array();
            for (const uint32_t ram : machine.ramSizesKb)
                sizes.push(StateNode(ram));
            entry["ram_sizes_kb"] = std::move(sizes);
            entry["default_ram_kb"] = machine.defaultRamKb;
            entry["creatable"] = machine.creatable;
            machines.push(std::move(entry));
        }
        node["machines"] = std::move(machines);
        list.push(std::move(node));
    }
    root["families"] = std::move(list);
    if (const MachinePlace place = Locate(families, current); place.family >= 0)
    {
        const MachineFamily& family = families[static_cast<size_t>(place.family)];
        StateNode at = StateNode::Object();
        at["family"] = family.id;
        at["machine"] = family.machines[static_cast<size_t>(place.machine)].name;
        at["ram_kb"] = currentRamKb;
        root["current"] = std::move(at);
    }
    return root;
}

std::string Title(const MachineEntry& machine, uint32_t ramKb)
{
    if (machine.ramSizesKb.size() <= 1)
        return machine.title;
    return machine.title + " " + std::to_string(ramKb) + "K";
}

std::string TitleOf(Emulator& emulator)
{
    const MachineIdentity identity = EmulatorManager::GetMachineIdentity(emulator);
    if (!identity.Valid)
        return {};
    const std::vector<MachineFamily>& all = All();
    const MachinePlace place = Locate(all, NameOf(emulator));
    if (place.family < 0)
        return identity.Model;
    return Title(all[static_cast<size_t>(place.family)].machines[static_cast<size_t>(place.machine)], identity.RamKb);
}

} // namespace MachineCatalog

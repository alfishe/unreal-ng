#include "ttdconfigfingerprint.h"

namespace ttd
{

const TTDConfigField* TTDConfigFingerprint::Find(const std::string& name) const
{
    for (const TTDConfigField& f : fields)
        if (f.name == name)
            return &f;
    return nullptr;
}

std::vector<TTDFingerprintDiff> Compare(const TTDConfigFingerprint& recorded, const TTDConfigFingerprint& live)
{
    std::vector<TTDFingerprintDiff> diffs;
    for (const TTDConfigField& r : recorded.fields)
    {
        const TTDConfigField* l = live.Find(r.name);
        if (!l)
            diffs.push_back({r.name, std::to_string(r.value), "-", r.affectsRestore});
        else if (l->value != r.value)
            diffs.push_back({r.name, std::to_string(r.value), std::to_string(l->value),
                             r.affectsRestore || l->affectsRestore});
    }
    for (const TTDConfigField& l : live.fields)
        if (!recorded.Find(l.name))
            diffs.push_back({l.name, "-", std::to_string(l.value), l.affectsRestore});
    return diffs;
}

}  // namespace ttd

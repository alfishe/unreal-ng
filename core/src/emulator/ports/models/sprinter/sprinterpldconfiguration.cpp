#include "sprinterpldconfiguration.h"

#include "emulator/ports/models/sprinter/sprinterpldstandard.h"

SprinterPldConfigurationRegistry::SprinterPldConfigurationRegistry()
{
    _modules.push_back(std::make_unique<SprinterPldStandard>());
}

size_t SprinterPldConfigurationRegistry::Register(std::unique_ptr<SprinterPldConfiguration> module)
{
    _modules.push_back(std::move(module));
    return _modules.size() - 1;
}

int SprinterPldConfigurationRegistry::Find(uint32_t fullHash, uint32_t headHash) const
{
    for (size_t i = 0; i < _modules.size(); i++)
    {
        if (_modules[i]->Descriptor().fullHash == fullHash)
            return static_cast<int>(i);
    }
    // MAME-compatible fallback: MAME identifies a configuration by its first 4 096 writes only
    for (size_t i = 0; i < _modules.size(); i++)
    {
        if (_modules[i]->Descriptor().headHash == headHash)
            return static_cast<int>(i);
    }
    return -1;
}

int SprinterPldConfigurationRegistry::FindByName(const std::string& name) const
{
    for (size_t i = 0; i < _modules.size(); i++)
    {
        if (_modules[i]->Descriptor().name == name)
            return static_cast<int>(i);
    }
    return -1;
}

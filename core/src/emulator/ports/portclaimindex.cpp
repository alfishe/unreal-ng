#include "portclaimindex.h"

#include "emulator/slots/portowners.h"

PortClaimIndex::PortClaimIndex() : _owners(std::make_unique<slots::PortOwners>())
{
}

PortClaimIndex::~PortClaimIndex() = default;

void PortClaimIndex::Build(const EmulatorContext* context)
{
    Build(slots::PortOwners::Of(context));
}

void PortClaimIndex::Build(const slots::PortOwners& owners)
{
    _owners = std::make_unique<slots::PortOwners>(owners);
    _table.clear();  // the next lookup builds it from the new claims
}

void PortClaimIndex::BuildTable()
{
    _table.assign(size_t(4) << 16, kBoard);
    for (uint32_t key = 0; key < _table.size(); key++)
    {
        const int claim = _owners->FindClaim(static_cast<uint16_t>(key), (key >> 17) & 1, (key >> 16) & 1);
        if (claim >= 0 && claim < kBoard)
            _table[key] = static_cast<uint8_t>(claim);
    }
}

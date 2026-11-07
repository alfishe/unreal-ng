#pragma once

/// @file portclaimindex.h
/// @brief The claim of the slot registry that answers each port access (slots::PortOwners), as a table: one load per
/// access. Shared by the port histories and the device access watch (docs/inprogress/2026-11-02-device-access-watch).
/// Built from the claims snapshot when the machine's slots are built; the table itself on the first lookup (the
/// emulation thread), 256 KB, only once something on the access path looks.

#include <cstdint>
#include <memory>
#include <vector>

// Qt defines `slots` as an empty macro: without this the namespace below turns anonymous in a Qt translation unit
// (gcc -Wsubobject-linkage / -Wodr in unreal-qt)
#pragma push_macro("slots")
#undef slots

class EmulatorContext;
namespace slots
{
class PortOwners;
}

class PortClaimIndex
{
public:
    static constexpr uint8_t kBoard = 0xFF;  ///< no claim: the board's own port (and claims past 254)

    PortClaimIndex();
    ~PortClaimIndex();

    /// The claims of the machine's slots now (control path, before the machine runs)
    void Build(const EmulatorContext* context);
    /// The claims given (tests)
    void Build(const slots::PortOwners& owners);

    const slots::PortOwners& Owners() const
    {
        return *_owners;
    }

    /// The claim that answers the access, kBoard for none (emulation thread)
    uint8_t Claim(uint16_t port, bool out, bool dos)
    {
        if (_table.empty()) [[unlikely]]
            BuildTable();
        return _table[(uint32_t(out) << 17) | (uint32_t(dos) << 16) | port];
    }

private:
    void BuildTable();

    std::unique_ptr<slots::PortOwners> _owners;
    std::vector<uint8_t> _table;  ///< claim by (out << 17 | dos << 16 | port); empty until the first lookup
};

#pragma pop_macro("slots")

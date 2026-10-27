#include "emulator/io/sprinter/isa/isaslotconfig.h"

#include "emulator/platform.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sprinterisa
{
namespace
{
std::string Upper(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == ' ' || c == '\t')
            continue;
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return out;
}

struct KindEntry
{
    CardKind kind;
    const char* name;
};
constexpr KindEntry kKinds[] = {
    {CardKind::None, "NONE"},           {CardKind::ZxBus, "ZXBUS"},         {CardKind::Ram, "RAM"},
    {CardKind::Ne2000, "NE2000"},       {CardKind::El3c509b, "EL3C509B"},   {CardKind::SprinterEsp, "SPRINTERESP"},
    {CardKind::Modem, "MODEM"},         {CardKind::Dual16552, "DUAL16552"},
};
}  // namespace

IsaConfig DefaultConfig()
{
    IsaConfig config{};
    // Slot 1: the ZX-bus adapter with the General Sound behind it ([SOUND] GSType: the NeoGS in the Sprinter config;
    // owner decision Q2, ISA phase I2)
    config.slot[0].kind = static_cast<uint8_t>(CardKind::ZxBus);
    config.slot[0].base = 0x300;
    config.slot[0].irq = 3;
    config.slot[0].macAuto = 1;
    // Slot 2: the network card is fitted by default (owner decision 2026-10-02, network Q1 = B)
    config.slot[1].kind = static_cast<uint8_t>(CardKind::Ne2000);
    config.slot[1].chip = static_cast<uint8_t>(Ne2000Chip::Rtl8019as);
    config.slot[1].base = 0x300;
    config.slot[1].irq = 3;
    config.slot[1].macAuto = 1;
    return config;
}

const char* KindName(CardKind kind)
{
    for (const KindEntry& e : kKinds)
    {
        if (e.kind == kind)
            return e.name;
    }
    return "UNKNOWN";
}

std::string KindKey(CardKind kind)
{
    std::string key = KindName(kind);
    for (char& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

bool ParseKind(const std::string& text, CardKind& kind)
{
    const std::string upper = Upper(text);
    for (const KindEntry& e : kKinds)
    {
        if (upper == e.name)
        {
            kind = e.kind;
            return true;
        }
    }
    if (upper == "EMPTY" || upper.empty())
    {
        kind = CardKind::None;
        return true;
    }
    return false;
}

const char* ChipName(Ne2000Chip chip)
{
    switch (chip)
    {
        case Ne2000Chip::Rtl8019as: return "RTL8019AS";
        case Ne2000Chip::Um9003: return "UM9003";
        case Ne2000Chip::Ne1000: return "NE1000";
    }
    return "RTL8019AS";
}

bool ParseChip(const std::string& text, Ne2000Chip& chip)
{
    const std::string upper = Upper(text);
    if (upper == "RTL8019AS" || upper == "RTL8019" || upper == "RTL")
        chip = Ne2000Chip::Rtl8019as;
    else if (upper == "UM9003" || upper == "UM9003AF" || upper == "UMC")
        chip = Ne2000Chip::Um9003;
    else if (upper == "NE1000")
        chip = Ne2000Chip::Ne1000;
    else
        return false;
    return true;
}

const char* El3ChipName(El3Chip chip)
{
    return chip == El3Chip::Tp ? "3C509B-TP" : "3C509B-TPO";
}

bool ParseEl3Chip(const std::string& text, El3Chip& chip)
{
    std::string upper = Upper(text);
    for (const char* prefix : {"3C509B-", "3C509B", "EL3-"})
    {
        const size_t n = std::strlen(prefix);
        if (upper.compare(0, n, prefix) == 0 && upper.size() > n)
        {
            upper.erase(0, n);
            break;
        }
    }
    if (upper == "TPO")
        chip = El3Chip::Tpo;
    else if (upper == "TP")
        chip = El3Chip::Tp;
    else
        return false;
    return true;
}

std::string SlotChipName(const SlotConfig& slot)
{
    switch (static_cast<CardKind>(slot.kind))
    {
        case CardKind::Ne2000: return ChipName(static_cast<Ne2000Chip>(slot.chip));
        case CardKind::El3c509b: return El3ChipName(static_cast<El3Chip>(slot.chip));
        default: return {};
    }
}

namespace
{
bool ParseNumber(const std::string& text, unsigned long& value)
{
    std::string t;
    for (char c : text)
    {
        if (c != ' ' && c != '\t')
            t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    int radix = 10;
    if (!t.empty() && (t[0] == '#' || t[0] == '$'))
    {
        t.erase(0, 1);
        radix = 16;
    }
    else if (t.size() > 2 && t[0] == '0' && t[1] == 'X')
    {
        t.erase(0, 2);
        radix = 16;
    }
    else if (!t.empty() && t.back() == 'H')
    {
        t.pop_back();
        radix = 16;
    }
    if (t.empty())
        return false;
    char* end = nullptr;
    value = std::strtoul(t.c_str(), &end, radix);
    return end && *end == '\0';
}
}  // namespace

bool ParseModemBase(const std::string& text, uint16_t& base)
{
    unsigned long value = 0;
    if (!ParseNumber(text, value) || (value != 0x3F8 && value != 0x2F8 && value != 0x3E8 && value != 0x2E8))
        return false;
    base = static_cast<uint16_t>(value);
    return true;
}

bool ValidModemIrq(uint8_t irq)
{
    return irq == 2 || irq == 3 || irq == 4 || irq == 5 || irq == 7;
}

bool ValidSerialJumper(int channel, uint8_t irq)
{
    return irq == 0 || irq == 2 || irq == (channel == 0 ? 3 : 4);
}

namespace
{
bool ParseIoBase(const std::string& text, unsigned step, uint16_t& base)
{
    std::string t = Upper(text);
    int radix = 10;
    if (!t.empty() && (t[0] == '#' || t[0] == '$'))
    {
        t.erase(0, 1);
        radix = 16;
    }
    else if (t.size() > 2 && t[0] == '0' && t[1] == 'X')
    {
        t.erase(0, 2);
        radix = 16;
    }
    else if (!t.empty() && t.back() == 'H')
    {
        t.pop_back();
        radix = 16;
    }
    if (t.empty())
        return false;
    char* end = nullptr;
    const unsigned long value = std::strtoul(t.c_str(), &end, radix);
    if (!end || *end != '\0' || value < 0x200 || value > 0x3E0 || (value & (step - 1)) != 0)
        return false;
    base = static_cast<uint16_t>(value);
    return true;
}
}  // namespace

bool ParseNe2000Base(const std::string& text, uint16_t& base)
{
    return ParseIoBase(text, 0x20, base);
}

bool ParseSlotBase(const std::string& text, CardKind kind, uint16_t& base)
{
    return ParseIoBase(text, kind == CardKind::El3c509b ? 0x10 : 0x20, base);
}

bool ParseMac(const std::string& text, SlotConfig& slot)
{
    const std::string upper = Upper(text);
    if (upper.empty() || upper == "AUTO")
    {
        slot.macAuto = 1;
        std::memset(slot.mac, 0, sizeof(slot.mac));
        return true;
    }
    uint8_t mac[6] = {};
    size_t at = 0;
    for (int i = 0; i < 6; ++i)
    {
        if (at + 2 > upper.size())
            return false;
        char* end = nullptr;
        const std::string pair = upper.substr(at, 2);
        const unsigned long b = std::strtoul(pair.c_str(), &end, 16);
        if (!end || *end != '\0')
            return false;
        mac[i] = static_cast<uint8_t>(b);
        at += 2;
        if (i < 5)
        {
            if (at >= upper.size() || (upper[at] != ':' && upper[at] != '-'))
                return false;
            ++at;
        }
    }
    if (at != upper.size() || (mac[0] & 0x01))   // a group address is no station address
        return false;
    slot.macAuto = 0;
    std::memcpy(slot.mac, mac, sizeof(mac));
    return true;
}

void EffectiveMac(const SlotConfig& slot, int slotIndex, uint8_t instance, uint8_t out[6])
{
    if (!slot.macAuto)
    {
        std::memcpy(out, slot.mac, 6);
        return;
    }
    out[0] = 0x02;
    out[1] = 0x53;
    out[2] = 0x50;
    out[3] = 0x00;
    out[4] = instance;
    out[5] = static_cast<uint8_t>(slotIndex + 1);
}

std::string SlotWarning(const SlotConfig& slot, int slotIndex)
{
    if (static_cast<CardKind>(slot.kind) == CardKind::Dual16552 && slot.irq != 0 && slot.irqB != 0)
        return "slot " + std::to_string(slotIndex + 1) + ": SprinterSerial with both IRQ jumpers fitted (J5 IRQ " +
               std::to_string(slot.irq) + ", J6 IRQ " + std::to_string(slot.irqB) +
               "): the Sprinter joins every IRQ pin of a slot, so COM1's and COM2's push-pull INTR outputs fight "
               "whenever their levels differ (modeled: the high one wins); fit one jumper";
    return {};
}

bool KindAvailable(CardKind kind, std::string* why)
{
    const char* reason = nullptr;
    switch (kind)
    {
        case CardKind::None:
            return true;
        case CardKind::ZxBus:
            break;   // ISA phase I2: the decoder fits the adapter, SoundManager the GS / NeoGS behind it
        case CardKind::Ram:
            reason = "the ISA RAM card is ISA phase I3, not built yet";
            break;
        case CardKind::Ne2000:
            break;   // network phase SN1: NetworkManager builds the board, the Sprinter fits it
        case CardKind::El3c509b:
            break;   // network phase SN5: NetworkManager builds the EtherLink3, the Sprinter fits it
        case CardKind::SprinterEsp:
            break;   // network phase SN3: NetworkManager builds the PcSerialCard, the Sprinter fits it
        case CardKind::Modem:
        case CardKind::Dual16552:
            break;   // network phase SN4: NetworkManager builds the PcSerialCard, the Sprinter fits it
    }
    if (why && reason)
        *why = reason;
    return reason == nullptr;
}

std::vector<SlotSummary> SlotSummaries(const CONFIG& config)
{
    std::vector<SlotSummary> out;
    for (int n = 0; n < kSlots; n++)
    {
        const SlotConfig& isa = config.sprinter.isa.slot[n];
        const auto kind = static_cast<CardKind>(isa.kind);
        SlotSummary slot;
        slot.key = KindKey(kind);
        switch (kind)
        {
            case CardKind::None:
                slot.name = "empty";
                break;
            case CardKind::ZxBus:
                slot.name = "ISA to ZX-bus adapter";
                slot.hostsZxBus = true;
                break;
            case CardKind::Ram:
                slot.name = "ISA RAM";
                break;
            case CardKind::Ne2000:
                slot.name = "NE2000 Ethernet";
                break;
            case CardKind::El3c509b:
                slot.name = "3Com EtherLink III";
                break;
            case CardKind::SprinterEsp:
                slot.name = "SprinterESP Wi-Fi";
                break;
            case CardKind::Modem:
                slot.name = "ISA Hayes modem";
                break;
            case CardKind::Dual16552:
                slot.name = "SprinterSerial (2 x 16550)";
                break;
        }
        slot.details = SlotChipName(isa);
        if (kind == CardKind::Ne2000 || kind == CardKind::El3c509b || kind == CardKind::Modem)
        {
            char resources[32] = {};
            std::snprintf(resources, sizeof(resources), "#%03X, IRQ %u", static_cast<unsigned>(isa.base),
                          static_cast<unsigned>(isa.irq));
            slot.details += (slot.details.empty() ? "" : ", ") + std::string(resources);
        }
        out.push_back(std::move(slot));
    }
    return out;
}

}  // namespace sprinterisa

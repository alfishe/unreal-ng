#include "stdafx.h"

#include "emulator/bus/bustraceevent.h"

#include <cstdio>

namespace
{
constexpr const char* kKindNames[] = {
    "select", "deselect", "byte", "early", "truncated", "command", "read", "write", "reg_read", "reg_write", "ata_command", "data",
};
static_assert(sizeof(kKindNames) / sizeof(kKindNames[0]) == static_cast<size_t>(BusEventKind::Count));

constexpr const char* kRuleNames[] = {
    "early_access", "select_mid_byte", "deselect_mid_command", "command_while_busy", "command_drops_answer", "crc_error",
    "dma_collision", "ata_data_without_drq", "ata_command_drops_data", "ata_write_while_busy",
};
static_assert(sizeof(kRuleNames) / sizeof(kRuleNames[0]) == static_cast<size_t>(BusRule::Count));
}  // namespace

const char* BusProtocolName(BusProtocol protocol)
{
    switch (protocol)
    {
        case BusProtocol::Spi: return "spi";
        case BusProtocol::I2c: return "i2c";
        case BusProtocol::Uart: return "uart";
        case BusProtocol::Ps2: return "ps2";
        case BusProtocol::Ata: return "ata";
        case BusProtocol::Fdc: return "fdc";
        case BusProtocol::Parallel: return "parallel";
    }
    return "unknown";
}

const char* BusSourceName(BusSource source)
{
    switch (source)
    {
        case BusSource::Cpu: return "cpu";
        case BusSource::Dma: return "dma";
        case BusSource::CardCpu: return "card_cpu";
        case BusSource::CardDma: return "card_dma";
        case BusSource::Host: return "host";
    }
    return "unknown";
}

const char* BusEventKindName(BusEventKind kind)
{
    const size_t index = static_cast<size_t>(kind);
    return index < static_cast<size_t>(BusEventKind::Count) ? kKindNames[index] : "unknown";
}

bool BusEventKindFromName(std::string_view name, BusEventKind& kind)
{
    for (size_t i = 0; i < static_cast<size_t>(BusEventKind::Count); i++)
    {
        if (name == kKindNames[i])
        {
            kind = static_cast<BusEventKind>(i);
            return true;
        }
    }
    return false;
}

const char* BusRuleName(BusRule rule)
{
    const size_t index = static_cast<size_t>(rule);
    return index < static_cast<size_t>(BusRule::Count) ? kRuleNames[index] : "unknown";
}

bool BusRuleFromName(std::string_view name, BusRule& rule)
{
    for (size_t i = 0; i < static_cast<size_t>(BusRule::Count); i++)
    {
        if (name == kRuleNames[i])
        {
            rule = static_cast<BusRule>(i);
            return true;
        }
    }
    return false;
}

BusSeverity BusRuleSeverity(BusRule rule)
{
    switch (rule)
    {
        case BusRule::EarlyAccess:
        case BusRule::SelectMidByte:
        case BusRule::CommandWhileBusy:
        case BusRule::DmaCollision:
        case BusRule::AtaDataWithoutDrq:
        case BusRule::AtaWriteWhileBusy:
            return BusSeverity::Error;
        case BusRule::DeselectMidCommand:
        case BusRule::CommandDropsAnswer:
        case BusRule::CrcError:
        default:
            return BusSeverity::Warning;
    }
}

const char* BusSeverityName(BusSeverity severity)
{
    switch (severity)
    {
        case BusSeverity::Info: return "info";
        case BusSeverity::Warning: return "warning";
        case BusSeverity::Error: return "error";
    }
    return "unknown";
}

const char* BusRuleDetail(BusRule rule)
{
    switch (rule)
    {
        case BusRule::EarlyAccess:
            return "accessed the data port before the byte in flight finished: the read gets the previous byte, the write is lost or "
                   "cuts the byte off";
        case BusRule::SelectMidByte:
            return "changed a chip select while a byte was still being shifted: the device sees a partial byte";
        case BusRule::DeselectMidCommand:
            return "deselected the card inside a 6-byte command: the command is lost";
        case BusRule::CommandWhileBusy:
            return "sent a command while the card still held the line busy after a write: a real card ignores it";
        case BusRule::CommandDropsAnswer:
            return "sent a command before the data block of the previous one was read out: a real card keeps sending the block";
        case BusRule::CrcError:
            return "the card answered with the CRC error bit: the command's CRC was wrong while CRC checking was on";
        case BusRule::DmaCollision:
            return "the CPU accessed the SPI ports while an SPI DMA transfer was running on the same master: bytes of the DMA and the "
                   "CPU interleave on the real board";
        case BusRule::AtaDataWithoutDrq:
            return "accessed the data register while DRQ was clear: a real drive returns garbage or ignores the word (the driver did "
                   "not check the status, or ignored an error)";
        case BusRule::AtaCommandDropsData:
            return "wrote a command while DRQ was set: the transfer of the previous command was not finished (a sector left unread "
                   "or not fully written)";
        case BusRule::AtaWriteWhileBusy:
            return "wrote a task-file register while BSY was set: a real drive ignores it (the driver did not wait for BSY to fall)";
        default:
            return "";
    }
}

std::string SdCommandName(uint8_t index, bool app)
{
    return std::string(app ? "ACMD" : "CMD") + std::to_string(index);
}

std::string AtaCommandName(uint8_t command)
{
    const char* name = nullptr;
    switch (command)
    {
        case 0x08: name = "DEVICE RESET"; break;
        case 0x20: case 0x21: name = "READ SECTORS"; break;
        case 0x24: name = "READ SECTORS EXT"; break;
        case 0x29: name = "READ MULTIPLE EXT"; break;
        case 0x30: case 0x31: name = "WRITE SECTORS"; break;
        case 0x34: name = "WRITE SECTORS EXT"; break;
        case 0x39: name = "WRITE MULTIPLE EXT"; break;
        case 0x40: case 0x41: case 0x42: name = "READ VERIFY"; break;
        case 0x50: name = "FORMAT TRACK"; break;
        case 0x90: name = "EXECUTE DIAGNOSTIC"; break;
        case 0x91: name = "INITIALIZE DEVICE PARAMETERS"; break;
        case 0xA0: name = "PACKET"; break;
        case 0xA1: name = "IDENTIFY PACKET DEVICE"; break;
        case 0xC4: name = "READ MULTIPLE"; break;
        case 0xC5: name = "WRITE MULTIPLE"; break;
        case 0xC6: name = "SET MULTIPLE MODE"; break;
        case 0xE0: case 0xE2: name = "STANDBY"; break;
        case 0xE1: case 0xE3: name = "IDLE"; break;
        case 0xE5: name = "CHECK POWER MODE"; break;
        case 0xE6: name = "SLEEP"; break;
        case 0xE7: case 0xEA: name = "FLUSH CACHE"; break;
        case 0xEC: name = "IDENTIFY DEVICE"; break;
        case 0xEF: name = "SET FEATURES"; break;
        default:
            if (command >= 0x10 && command <= 0x1F)
                name = "RECALIBRATE";
            else if (command >= 0x70 && command <= 0x7F)
                name = "SEEK";
            break;
    }
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X", command);
    return name ? std::string(name) + " (" + hex + ")" : std::string("command ") + hex;
}

const char* AtaRegisterName(uint8_t reg, bool write)
{
    switch (reg)
    {
        case 0: return "data";
        case 1: return write ? "features" : "error";
        case 2: return "sector_count";
        case 3: return "lba_low";
        case 4: return "lba_mid";
        case 5: return "lba_high";
        case 6: return "device";
        case 7: return write ? "command" : "status";
        case 8: return write ? "device_control" : "alt_status";
        default: return "unknown";
    }
}

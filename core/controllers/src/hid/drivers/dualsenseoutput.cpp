#include "hid/drivers/dualsenseoutput.h"

namespace controllers
{

namespace
{
constexpr uint16_t kSony = 0x054C;
constexpr uint8_t kFlag0CompatibleVibration = 0x01;
constexpr uint8_t kFlag0HapticsSelect = 0x02;
constexpr uint8_t kFlag0RightTrigger = 0x04;
constexpr uint8_t kFlag0LeftTrigger = 0x08;
constexpr uint8_t kFlag2CompatibleVibration2 = 0x04;
constexpr uint8_t kFlag1LightbarControl = 0x04;
constexpr uint8_t kFlag2LightbarSetup = 0x02;
constexpr uint8_t kLightbarSetupLightOut = 0x02;
constexpr uint8_t kTriggerOff = 0x00;
constexpr uint8_t kTriggerResistance = 0x01;  // start position, force

uint8_t Byte(float level)
{
    return static_cast<uint8_t>((level < 0 ? 0 : level > 1 ? 1 : level) * 255.0f + 0.5f);
}

void Trigger(float level, uint8_t* effect)
{
    if (level <= 0)
    {
        effect[0] = kTriggerOff;
        return;
    }
    effect[0] = kTriggerResistance;
    effect[1] = 0x00;
    effect[2] = Byte(level);
}
}  // namespace

bool DualSenseOutput::Handles(uint16_t vendor, uint16_t product)
{
    return vendor == kSony && (product == 0x0CE6 || product == 0x0DF2);
}

void DualSenseOutput::Common(const RumbleLevels& levels, uint8_t* common)
{
    common[0] = kFlag0CompatibleVibration | kFlag0HapticsSelect | kFlag0RightTrigger | kFlag0LeftTrigger;
    common[2] = Byte(levels.high);
    common[3] = Byte(levels.low);
    Trigger(levels.rightTrigger, common + 10);
    Trigger(levels.leftTrigger, common + 21);
    common[38] = kFlag2CompatibleVibration2;
}

std::vector<uint8_t> DualSenseOutput::UsbReport(const RumbleLevels& levels)
{
    std::vector<uint8_t> report(48, 0);
    report[0] = 0x02;
    Common(levels, report.data() + 1);
    return report;
}

std::vector<uint8_t> DualSenseOutput::UsbLightReport(const LightColor& color, bool release)
{
    std::vector<uint8_t> report(48, 0);
    report[0] = 0x02;
    uint8_t* common = report.data() + 1;
    common[1] = kFlag1LightbarControl;
    if (release)
    {
        common[38] = kFlag2LightbarSetup;
        common[41] = kLightbarSetupLightOut;
    }
    common[44] = color.red;
    common[45] = color.green;
    common[46] = color.blue;
    return report;
}

std::vector<uint8_t> DualSenseOutput::BluetoothReport(const RumbleLevels& levels, uint8_t sequence)
{
    std::vector<uint8_t> report(78, 0);
    report[0] = 0x31;
    report[1] = static_cast<uint8_t>((sequence & 0x0F) << 4);
    report[2] = 0x10;
    Common(levels, report.data() + 3);
    const uint8_t seed = 0xA2;
    const uint32_t crc = Crc32(report.data(), report.size() - 4, Crc32(&seed, 1));
    for (int index = 0; index < 4; index++)
        report[74 + index] = static_cast<uint8_t>(crc >> (8 * index));
    return report;
}

uint32_t DualSenseOutput::Crc32(const uint8_t* data, size_t size, uint32_t crc)
{
    // The zlib CRC-32 (reflected 0xEDB88320), continued from @p crc
    crc = ~crc;
    for (size_t index = 0; index < size; index++)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

}  // namespace controllers

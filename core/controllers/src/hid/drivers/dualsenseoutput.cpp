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
constexpr uint8_t kFlag1PlayerIndicator = 0x10;
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
    common[0] |= kFlag0CompatibleVibration | kFlag0HapticsSelect | kFlag0RightTrigger | kFlag0LeftTrigger;
    common[2] = Byte(levels.high);
    common[3] = Byte(levels.low);
    Trigger(levels.rightTrigger, common + 10);
    Trigger(levels.leftTrigger, common + 21);
    common[38] |= kFlag2CompatibleVibration2;
}

std::vector<uint8_t> DualSenseOutput::UsbReport(const RumbleLevels& levels)
{
    DualSenseOutputState state;
    state.rumble = levels;
    return Report(state, false, 0);
}

std::vector<uint8_t> DualSenseOutput::UsbLightReport(const LightColor& color, bool release)
{
    DualSenseOutputState state;
    state.light = color;
    state.releaseLight = release;
    return Report(state, false, 0);
}

std::vector<uint8_t> DualSenseOutput::BluetoothReport(const RumbleLevels& levels, uint8_t sequence)
{
    DualSenseOutputState state;
    state.rumble = levels;
    return Report(state, true, sequence);
}

std::vector<uint8_t> DualSenseOutput::Report(const DualSenseOutputState& state, bool bluetooth, uint8_t sequence)
{
    std::vector<uint8_t> report(bluetooth ? 78 : 48, 0);
    uint8_t* common = nullptr;
    if (bluetooth)
    {
        report[0] = 0x31;
        report[1] = static_cast<uint8_t>((sequence & 0x0F) << 4);  // DS_OUTPUT_SEQ_NO; the tag nibble 0
        report[2] = 0x10;                                          // DS_OUTPUT_TAG
        common = report.data() + 3;
    }
    else
    {
        report[0] = 0x02;
        common = report.data() + 1;
    }
    if (state.rumble)
        Common(*state.rumble, common);
    if (state.light)
    {
        common[1] |= kFlag1LightbarControl;
        if (state.releaseLight)
        {
            common[38] |= kFlag2LightbarSetup;
            common[41] = kLightbarSetupLightOut;
        }
        common[44] = state.light->red;
        common[45] = state.light->green;
        common[46] = state.light->blue;
    }
    if (state.playerLeds)
    {
        common[1] |= kFlag1PlayerIndicator;
        common[43] = static_cast<uint8_t>(*state.playerLeds & 0x1F);
    }
    if (bluetooth)
    {
        const uint8_t seed = 0xA2;  // PS_OUTPUT_CRC32_SEED
        const uint32_t crc = Crc32(report.data(), report.size() - 4, Crc32(&seed, 1));
        for (int index = 0; index < 4; index++)
            report[74 + index] = static_cast<uint8_t>(crc >> (8 * index));
    }
    return report;
}

uint8_t DualSenseOutput::PlayerLeds(int player)
{
    static const uint8_t kPlayers[5] = {0x04, 0x0A, 0x15, 0x1B, 0x1F};
    return player <= 0 ? 0 : kPlayers[(player - 1) % 5];
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

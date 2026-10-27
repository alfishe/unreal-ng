#include "stdafx.h"

#include "nextregtable.h"

#include "common/stringhelper.h"

namespace
{
const NextRegInfo kTable[] = {
#include "nextregtable.inc"
};
}  // namespace

const NextRegInfo* NextRegTable(size_t& count)
{
    count = sizeof kTable / sizeof kTable[0];
    return kTable;
}

const NextRegInfo* FindNextReg(uint8_t number)
{
    static const NextRegInfo* index[256];
    static bool built = false;
    if (!built)
    {
        for (const NextRegInfo& r : kTable)
            index[r.number] = &r;
        built = true;
    }
    return index[number];
}

std::string NextRegDecode(uint8_t reg, uint8_t value)
{
    static const char* const kSpeeds[4] = {"3.5 MHz", "7 MHz", "14 MHz", "28 MHz"};
    switch (reg)
    {
        case 0x02:
        {
            std::string text;
            if (value & 0x02) text += "hard reset ";
            if (value & 0x01) text += "soft reset ";
            if (value & 0x04) text += "drive NMI ";
            if (value & 0x08) text += "multiface NMI ";
            if (value & 0x80) text += "bus reset ";
            return text.empty() ? "no request" : text.substr(0, text.size() - 1);
        }
        case 0x03:
            return StringHelper::Format("machine type %u, timing %u", value & 7u, (value >> 4) & 7u);
        case 0x07:
            return StringHelper::Format("CPU speed %s", kSpeeds[value & 3]);
        default:
            return "";
    }
}

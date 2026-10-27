#include "hid/hiddescriptor.h"

#include <map>

namespace controllers
{

namespace
{
struct Globals
{
    uint16_t usagePage = 0;
    int32_t logicalMin = 0;
    int32_t logicalMax = 0;
    uint32_t reportSize = 0;
    uint32_t reportCount = 0;
    uint8_t reportId = 0;
};

int32_t Signed(uint32_t value, size_t bytes)
{
    if (bytes == 1)
        return static_cast<int8_t>(value);
    if (bytes == 2)
        return static_cast<int16_t>(value);
    return static_cast<int32_t>(value);
}

constexpr uint16_t kGenericDesktop = 0x01;
constexpr uint8_t kCollectionApplication = 0x01;
}  // namespace

bool ParseHidDescriptor(const uint8_t* data, size_t size, HidDescriptor& out, std::string& error)
{
    HidDescriptor result;
    Globals globals;
    std::vector<Globals> stack;
    std::vector<uint32_t> usages;  ///< page << 16 | usage
    uint32_t usageMin = 0;
    uint32_t usageMax = 0;
    bool haveRange = false;
    std::vector<uint16_t> applications;
    std::map<uint8_t, uint32_t> inputBits;  ///< report id -> bits so far
    int depth = 0;

    const auto fullUsage = [&globals](uint32_t value, size_t bytes) {
        return bytes == 4 ? value : (static_cast<uint32_t>(globals.usagePage) << 16) | (value & 0xFFFF);
    };

    size_t pos = 0;
    while (pos < size)
    {
        const uint8_t prefix = data[pos++];
        if (prefix == 0xFE)  // long item: skipped
        {
            if (pos + 2 > size)
                break;
            pos += 2 + data[pos];
            continue;
        }
        const size_t bytes = (prefix & 0x03) == 3 ? 4 : (prefix & 0x03);
        if (pos + bytes > size)
        {
            error = "the descriptor ends inside an item at byte " + std::to_string(pos - 1);
            return false;
        }
        uint32_t value = 0;
        for (size_t i = 0; i < bytes; ++i)
            value |= static_cast<uint32_t>(data[pos + i]) << (8 * i);
        pos += bytes;
        const uint8_t type = (prefix >> 2) & 0x03;
        const uint8_t tag = prefix >> 4;

        if (type == 0)  // main
        {
            if (tag == 0x08)  // input
            {
                const bool constant = value & 0x01;
                const bool variable = value & 0x02;
                uint32_t& bits = inputBits[globals.reportId];
                const uint16_t application = applications.empty() ? 0 : applications.front();
                for (uint32_t i = 0; i < globals.reportCount; ++i)
                {
                    uint32_t usage = 0;
                    if (haveRange)
                        usage = usageMin + i <= usageMax ? usageMin + i : usageMax;
                    else if (!usages.empty())
                        usage = usages[i < usages.size() ? i : usages.size() - 1];
                    if (!constant && variable && usage != 0 && globals.reportSize >= 1 && globals.reportSize <= 32)
                    {
                        HidField field;
                        field.reportId = globals.reportId;
                        field.bitOffset = bits;
                        field.bitSize = static_cast<uint8_t>(globals.reportSize);
                        field.usagePage = static_cast<uint16_t>(usage >> 16);
                        field.usage = static_cast<uint16_t>(usage & 0xFFFF);
                        field.logicalMin = globals.logicalMin;
                        field.logicalMax = globals.logicalMax;
                        field.nullState = value & 0x40;
                        field.application = application;
                        result.fields.push_back(field);
                    }
                    bits += globals.reportSize;
                }
            }
            else if (tag == 0x0A)  // collection
            {
                ++depth;
                if (value == kCollectionApplication && !usages.empty() && (usages.front() >> 16) == kGenericDesktop)
                    applications.push_back(static_cast<uint16_t>(usages.front() & 0xFFFF));
                else
                    applications.push_back(applications.empty() ? 0 : applications.back());
            }
            else if (tag == 0x0C)  // end collection
            {
                if (depth == 0)
                {
                    error = "an end collection without a collection";
                    return false;
                }
                --depth;
                applications.pop_back();
            }
            usages.clear();
            haveRange = false;
            continue;
        }
        if (type == 1)  // global
        {
            switch (tag)
            {
                case 0x0: globals.usagePage = static_cast<uint16_t>(value); break;
                case 0x1: globals.logicalMin = Signed(value, bytes); break;
                case 0x2:
                    // a maximum is unsigned when the minimum is not negative (`27 ff ff 00 00`, `26 ff 03`)
                    globals.logicalMax = globals.logicalMin < 0 ? Signed(value, bytes) : static_cast<int32_t>(value);
                    break;
                case 0x7: globals.reportSize = value; break;
                case 0x8:
                    globals.reportId = static_cast<uint8_t>(value);
                    result.hasReportIds = true;
                    break;
                case 0x9: globals.reportCount = value; break;
                case 0xA: stack.push_back(globals); break;
                case 0xB:
                    if (stack.empty())
                    {
                        error = "a pop without a push";
                        return false;
                    }
                    globals = stack.back();
                    stack.pop_back();
                    break;
                default: break;  // physical range, unit, exponent: not needed
            }
            continue;
        }
        if (type == 2)  // local
        {
            if (tag == 0x0)
                usages.push_back(fullUsage(value, bytes));
            else if (tag == 0x1)
            {
                usageMin = fullUsage(value, bytes);
                haveRange = true;
            }
            else if (tag == 0x2)
                usageMax = fullUsage(value, bytes);
        }
    }
    if (depth != 0)
    {
        error = "a collection is not closed";
        return false;
    }
    out = std::move(result);
    return true;
}

bool ReadHidField(const HidField& field, bool hasReportIds, const uint8_t* report, size_t size, int32_t& value)
{
    size_t start = 0;
    if (hasReportIds)
    {
        if (size == 0 || report[0] != field.reportId)
            return false;
        start = 1;
    }
    const uint32_t end = field.bitOffset + field.bitSize;
    if ((end + 7) / 8 > size - start)
        return false;
    uint64_t raw = 0;
    const size_t first = start + field.bitOffset / 8;
    const size_t last = start + (end - 1) / 8;
    for (size_t i = last + 1; i-- > first;)
        raw = (raw << 8) | report[i];
    raw >>= field.bitOffset % 8;
    const uint64_t mask = field.bitSize == 32 ? 0xFFFFFFFFull : ((1ull << field.bitSize) - 1);
    raw &= mask;
    if (field.logicalMin < 0 && field.bitSize < 32 && (raw & (1ull << (field.bitSize - 1))))
        raw |= ~mask;
    value = static_cast<int32_t>(static_cast<uint32_t>(raw));
    return true;
}

}  // namespace controllers
